/*
 * A Plugin that integrates the AMD AMF encoder into OBS Studio
 * Copyright (C) 2016 - 2018 Michael Fabian Dirks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "amf-encoder.hpp"
#include <cinttypes>
#include <thread>
#include "utility.hpp"

#include <components/VideoConverter.h>
#include <components/VideoEncoderHEVC.h>
#include <components/VideoEncoderVCE.h>

using namespace Plugin;
using namespace Plugin::AMD;

Plugin::AMD::Encoder::Encoder(Codec codec, std::shared_ptr<API::IAPI> videoAPI, const API::Adapter& videoAdapter,
							  bool useOpenCLSubmission, bool useOpenCLConversion, ColorFormat colorFormat,
							  ColorSpace colorSpace, bool fullRangeColor, bool multiThreading, size_t queueSize)
{
	m_UniqueId = Utility::GetUniqueIdentifier();

	/// AMF Internals
	m_AMF              = nullptr;
	m_AMFFactory       = nullptr;
	m_AMFContext       = nullptr;
	m_AMFEncoder       = nullptr;
	m_AMFConverter     = nullptr;
	m_AMFMemoryType    = amf::AMF_MEMORY_UNKNOWN;
	m_AMFSurfaceFormat = Utility::ColorFormatToAMF(colorFormat);

	/// API Related
	m_API              = nullptr;
	m_APIDevice        = nullptr;
	m_OpenCLSubmission = false;

	/// Properties
	m_QueueSize = queueSize;

	/// Resolution + Rate
	m_Resolution        = std::make_pair<uint32_t, uint32_t>(0, 0);
	m_FrameRate         = std::make_pair<uint32_t, uint32_t>(0, 0);
	m_FrameRateFraction = 0;

	/// Flags
	m_Initialized = true;
	m_Started     = false;
	m_OpenCL      = false;
	m_Debug       = false;

	/// Timings
	m_TimestampStep        = 0;
	m_TimestampStepRounded = 0;
	m_TimestampOffset      = 0;
	m_SubmitQueryWaitTimer = std::chrono::milliseconds(1);
	m_SubmitQueryAttempts  = 16;
	m_InitialFrameLatency  = 0;

	/// Status
	m_SubmittedFrameCount    = 0;
	m_InitialFramesSent      = false;
	m_InitialPacketRetrieved = false;

	/// Periods
	m_PeriodIDR            = 0;
	m_PeriodIFrame         = 0;
	m_PeriodPFrame         = 0;
	m_PeriodBFrame         = 0;
	m_FrameSkipPeriod      = 0;
	m_FrameSkipKeepOnlyNth = false;

	/// Multi-Threading
	m_MultiThreading = multiThreading;
	m_AsyncRetrieve  = nullptr;
	m_AsyncSend      = nullptr;

	// Setup
	m_Codec            = codec;
	m_ColorFormat      = colorFormat;
	m_ColorSpace       = colorSpace;
	m_FullColorRange   = fullRangeColor;
	m_OpenCLSubmission = useOpenCLSubmission;
	m_OpenCLConversion = useOpenCLConversion;

	// Initialize selected API on Video Adapter
	m_API        = videoAPI;
	m_APIAdapter = videoAdapter;
	m_APIDevice  = m_API->CreateInstance(m_APIAdapter);

	// Initialize Advanced Media Framework
	m_AMF = AMF::Instance();
	m_AMF->EnableDebugTrace(m_Debug);
	m_AMFFactory = m_AMF->GetFactory();

	// Create Context for Conversion and Encoding
	AMF_RESULT res = m_AMFFactory->CreateContext(&m_AMFContext);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Creating a AMF Context failed, error %ls (code %d).", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}
	/// Initialize Context using selected API
	switch (m_API->GetType()) {
	case API::Type::Direct3D11:
	case API::Type::Direct3D9:
		break;
	default:
		m_API = API::GetAPI(0);
		switch (m_API->GetType()) {
		case API::Type::Direct3D9:
			m_APIAdapter = m_API->EnumerateAdapters()[0];
			m_APIDevice  = m_API->CreateInstance(m_APIAdapter);
			break;
		case API::Type::Direct3D11:
			m_APIAdapter = m_API->EnumerateAdapters()[0];
			m_APIDevice  = m_API->CreateInstance(m_APIAdapter);
			break;
		}
	}
	switch (m_API->GetType()) {
	case API::Type::Direct3D9:
		m_AMFMemoryType = amf::AMF_MEMORY_DX9;
		res             = m_AMFContext->InitDX9(m_APIDevice->GetContext());
		break;
	case API::Type::Direct3D11:
		m_AMFMemoryType = amf::AMF_MEMORY_DX11;
		res             = m_AMFContext->InitDX11(m_APIDevice->GetContext());
		break;
	}
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Initializing %s API with Adapter '%s' failed, error %ls (code %d).",
							 m_UniqueId, m_API->GetName().c_str(), m_APIAdapter.Name.c_str(),
							 m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}

	// Initialize OpenCL (if possible)
	if (m_OpenCLSubmission || m_OpenCLConversion) {
		res = m_AMFContext->InitOpenCL();
		if (res == AMF_OK) {
			m_OpenCL = true;

			res = m_AMFContext->GetCompute(amf::AMF_MEMORY_OPENCL, &m_AMFCompute);
			if (res != AMF_OK) {
				m_OpenCLSubmission = false;
				m_OpenCLConversion = false;

				QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Retrieving Compute object failed, error %ls (code %d)",
									 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
				PLOG_WARNING("%s", errMsg.data());
			}
		} else {
			m_OpenCL           = false;
			m_OpenCLSubmission = false;
			m_OpenCLConversion = false;

			QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Initialising OpenCL failed, error %ls (code %d)", m_UniqueId,
								 m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_WARNING("%s", errMsg.data());
		}
	}

	// Create Converter
	res = m_AMFFactory->CreateComponent(m_AMFContext, AMFVideoConverter, &m_AMFConverter);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Creating frame converter component failed, error %ls (code %d)",
							 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}
	res = m_AMFConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, amf::AMF_MEMORY_UNKNOWN);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Unable to set converter memory type, error %ls (code %d)", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}
	res = m_AMFConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, amf::AMF_SURFACE_NV12);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Unable to set converter output format, error %ls (code %d)",
							 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}
	res =
		m_AMFConverter->SetProperty(AMF_VIDEO_CONVERTER_COLOR_PROFILE, Utility::ColorSpaceToAMFConverter(m_ColorSpace));
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Unable to set converter color profile, error %ls (code %d)",
							 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}
	res = m_AMFConverter->SetProperty(AMF_VIDEO_CONVERTER_TRANSFER_CHARACTERISTIC,
									  Utility::ColorSpaceToTransferCharacteristic(m_ColorSpace));
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Unable to set converter transfer characteristic, error %ls (code %d)",
							 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
		PLOG_WARNING("%s", errMsg.c_str());
	}

	// Create Encoder
	res = m_AMFFactory->CreateComponent(m_AMFContext, Utility::CodecToAMF(codec), &m_AMFEncoder);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Unable to create %s encoder, error %ls (code %d)", m_UniqueId,
							 Utility::CodecToString(codec), m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}

	// Show complete initialization in log.
	QUICK_FORMAT_MESSAGE(notice, "<Id: %llu> Initialized.", m_UniqueId);
	PLOG_DEBUG("%s", notice.data());
}

Plugin::AMD::Encoder::~Encoder()
{
	// Destroy AMF Encoder
	if (m_AMFEncoder) {
		m_AMFEncoder->Terminate();
		m_AMFEncoder = nullptr;
	}

	// Destroy AMF Converter
	if (m_AMFConverter) {
		m_AMFConverter->Terminate();
		m_AMFConverter = nullptr;
	}

	// Destroy AMF Context
	if (m_AMFContext) {
		m_AMFContext->Terminate();
		m_AMFContext = nullptr;
	}

	// Destroy API
	if (m_API) {
		m_APIDevice = nullptr;
		m_API       = nullptr;
	}

	m_AMF = nullptr;

	// Show complete initialization in log.
	QUICK_FORMAT_MESSAGE(notice, "<Id: %llu> Finalized.", m_UniqueId);
	PLOG_DEBUG("%s", notice.c_str());
}

uint64_t Plugin::AMD::Encoder::GetUniqueId()
{
	return m_UniqueId;
}

Plugin::AMD::Codec Plugin::AMD::Encoder::GetCodec()
{
	return m_Codec;
}

std::shared_ptr<API::IAPI> Plugin::AMD::Encoder::GetVideoAPI()
{
	return m_API;
}

Plugin::API::Adapter Plugin::AMD::Encoder::GetVideoAdapter()
{
	return m_APIAdapter;
}

bool Plugin::AMD::Encoder::IsOpenCLEnabled()
{
	return m_OpenCLSubmission;
}

Plugin::AMD::ColorFormat Plugin::AMD::Encoder::GetColorFormat()
{
	return m_ColorFormat;
}

Plugin::AMD::ColorSpace Plugin::AMD::Encoder::GetColorSpace()
{
	return m_ColorSpace;
}

bool Plugin::AMD::Encoder::IsFullRangeColor()
{
	return m_FullColorRange;
}

bool Plugin::AMD::Encoder::IsMultiThreaded()
{
	return m_MultiThreading;
}

size_t Plugin::AMD::Encoder::GetQueueSize()
{
	return m_QueueSize;
}

void Plugin::AMD::Encoder::SetDebug(bool v)
{
	m_Debug = v;
	m_AMF->EnableDebugTrace(m_Debug);
}

bool Plugin::AMD::Encoder::IsDebug()
{
	return m_Debug;
}

#ifndef LITE_OBS
void Plugin::AMD::Encoder::UpdateFrameRateValues()
{
	// 1			Second
	// 1000			Millisecond
	// 1000000		Microsecond
	// 10000000		amf_pts
	// 1000000000	Nanosecond
	m_FrameRateFraction    = ((double_t)m_FrameRate.second / (double_t)m_FrameRate.first);
	m_TimestampStep        = AMF_SECOND * m_FrameRateFraction;
	m_TimestampStepRounded = (uint64_t)round(m_TimestampStep);
	m_SubmitQueryWaitTimer = std::chrono::milliseconds(
		1); // std::chrono::nanoseconds((uint64_t)round(m_TimestampStep / m_SubmitQueryAttempts));
}

void Plugin::AMD::Encoder::SetVBVBufferStrictness(double_t v)
{
	auto     bitrateCaps  = CapsVBVBufferSize();
	uint64_t looseBitrate = bitrateCaps.second, targetBitrate = 0, strictBitrate = bitrateCaps.first;

	Usage usage = GetUsage();
	if (usage == Usage::UltraLowLatency) {
		targetBitrate = amf_clamp(GetTargetBitrate(), bitrateCaps.first, bitrateCaps.second);
	} else {
		switch (this->GetRateControlMethod()) {
		case RateControlMethod::ConstantBitrate:
			targetBitrate = amf_clamp(GetTargetBitrate(), bitrateCaps.first, bitrateCaps.second);
			break;
		case RateControlMethod::LatencyConstrainedVariableBitrate:
		case RateControlMethod::PeakConstrainedVariableBitrate:
			targetBitrate = max(this->GetTargetBitrate(), this->GetPeakBitrate());
			break;
		case RateControlMethod::ConstantQP:
			targetBitrate = bitrateCaps.second / 2;
			break;
		}
	}
	strictBitrate = amf_clamp(
		static_cast<uint64_t>(round(targetBitrate * ((double_t)m_FrameRate.second / (double_t)m_FrameRate.first))),
		bitrateCaps.first, targetBitrate);

	// Three-Point Linear Lerp
	// 0% = looseBitrate, 50% = targetBitrate, 100% = strictBitrate
	v                 = amf_clamp(v, 0.0, 1.0);
	double_t aFadeVal = amf_clamp(v * 2.0, 0.0, 1.0);       // 0 - 0.5
	double_t bFadeVal = amf_clamp(v * 2.0 - 1.0, 0.0, 1.0); // 0.5 - 1.0

	double_t aFade = (looseBitrate * (1.0 - aFadeVal)) + (targetBitrate * aFadeVal);
	double_t bFade = (aFade * (1.0 - bFadeVal)) + (strictBitrate * bFadeVal);

	uint64_t vbvBufferSize = static_cast<uint64_t>(round(bFade));
	this->SetVBVBufferSize(vbvBufferSize);
}

void Plugin::AMD::Encoder::SetIFramePeriod(uint32_t v)
{
	m_PeriodIFrame = v;
}

uint32_t Plugin::AMD::Encoder::GetIFramePeriod()
{
	return m_PeriodIFrame;
}

void Plugin::AMD::Encoder::SetPFramePeriod(uint32_t v)
{
	m_PeriodPFrame = v;
}

uint32_t Plugin::AMD::Encoder::GetPFramePeriod()
{
	return m_PeriodPFrame;
}

void Plugin::AMD::Encoder::SetBFramePeriod(uint32_t v)
{
	m_PeriodBFrame = v;
}

uint32_t Plugin::AMD::Encoder::GetBFramePeriod()
{
	return m_PeriodBFrame;
}

void Plugin::AMD::Encoder::SetFrameSkippingPeriod(uint32_t v)
{
	m_FrameSkipPeriod = v;
}

uint32_t Plugin::AMD::Encoder::GetFrameSkippingPeriod()
{
	return m_FrameSkipPeriod;
}

void Plugin::AMD::Encoder::SetFrameSkippingBehaviour(bool v)
{
	m_FrameSkipKeepOnlyNth = v;
}

bool Plugin::AMD::Encoder::GetFrameSkippingBehaviour()
{
	return m_FrameSkipKeepOnlyNth;
}

void Plugin::AMD::Encoder::Start()
{
	AMF_RESULT res;

	res = m_AMFConverter->Init(Utility::ColorFormatToAMF(m_ColorFormat), m_Resolution.first, m_Resolution.second);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Unable to initialize converter, error %ls (code %d)", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}

	res = m_AMFEncoder->Init(amf::AMF_SURFACE_NV12, m_Resolution.first, m_Resolution.second);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Failed to initialize encoder, error %ls (code %d)", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}

	// Threading
	if (m_MultiThreading) {
		m_AsyncSend                  = new EncoderThreadingData;
		m_AsyncSend->shutdown        = false;
		m_AsyncSend->wakeupcount     = 0; // 2 ^ 32;
		m_AsyncSend->data            = nullptr;
		m_AsyncSend->worker          = std::thread(AsyncSendMain, this);
		m_AsyncRetrieve              = new EncoderThreadingData;
		m_AsyncRetrieve->shutdown    = false;
		m_AsyncRetrieve->wakeupcount = 0;
		m_AsyncRetrieve->data        = nullptr;
		m_AsyncRetrieve->worker      = std::thread(AsyncRetrieveMain, this);
	}

	m_Started = true;
}

void Plugin::AMD::Encoder::Restart()
{
	AMF_RESULT res = m_AMFEncoder->ReInit(m_Resolution.first, m_Resolution.second);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Could not re-initialize encoder, error %ls (code %d)", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		throw std::exception(errMsg.c_str());
	}
}

void Plugin::AMD::Encoder::Stop()
{
	if (!m_Started)
		throw std::logic_error("Can't stop an encoder that isn't running!");

	m_AMFConverter->Drain();
	m_AMFConverter->Flush();
	m_AMFEncoder->Drain();
	m_AMFEncoder->Flush();

	// Threading
	if (m_MultiThreading) {
		{
			std::unique_lock<std::mutex> lock(m_AsyncRetrieve->mutex);
			m_AsyncRetrieve->shutdown    = true;
			m_AsyncRetrieve->wakeupcount = 2 ^ 32;
			m_AsyncRetrieve->condvar.notify_all();
			m_AsyncRetrieve->data = nullptr;
		}
		m_AsyncRetrieve->worker.join();
		delete m_AsyncRetrieve;
		{
			std::unique_lock<std::mutex> lock(m_AsyncSend->mutex);
			m_AsyncSend->shutdown    = true;
			m_AsyncSend->wakeupcount = 2 ^ 32;
			m_AsyncSend->condvar.notify_all();
			m_AsyncSend->data = nullptr;
		}
		m_AsyncSend->worker.join();
		delete m_AsyncSend;
	}

	m_Started = false;
}

bool Plugin::AMD::Encoder::IsStarted()
{
	return m_Started;
}

bool Plugin::AMD::Encoder::Encode(struct encoder_frame* frame, struct encoder_packet* packet, bool* received_packet)
{
	if (!m_Started)
		return false;

	amf::AMFSurfacePtr surface      = nullptr;
	amf::AMFDataPtr    surface_data = nullptr;
	amf::AMFDataPtr    packet_data  = nullptr;

	// Encoding Steps
	if (!EncodeAllocate(surface))
		return false;
	if (!EncodeStore(surface, frame))
		return false;
	if (!EncodeConvert(surface, surface_data))
		return false;
	if (!EncodeMain(surface_data, packet_data))
		return false;
	if (!EncodeLoad(packet_data, packet, received_packet))
		return false;

	return true;
}

void Plugin::AMD::Encoder::GetVideoInfo(struct video_scale_info* info)
{
	if (!m_AMFContext || !m_AMFEncoder)
		throw std::exception("Called while not initialized.");

	switch (m_ColorFormat) {
	// 4:2:0 Formats
	case ColorFormat::NV12:
		info->format = VIDEO_FORMAT_NV12;
		break;
	case ColorFormat::I420:
		info->format = VIDEO_FORMAT_I420;
		break;
		// 4:2:2 Formats
	case ColorFormat::YUY2:
		info->format = VIDEO_FORMAT_YUY2;
		break;
		// Uncompressed
	case ColorFormat::RGBA:
		info->format = VIDEO_FORMAT_RGBA;
		break;
	case ColorFormat::BGRA:
		info->format = VIDEO_FORMAT_BGRA;
		break;
		// Other
	case ColorFormat::GRAY:
		info->format = VIDEO_FORMAT_Y800;
		break;
	}

	if (m_FullColorRange) { // Only use Full range if actually enabled.
		info->range = VIDEO_RANGE_FULL;
	} else {
		info->range = VIDEO_RANGE_PARTIAL;
	}
}

bool Plugin::AMD::Encoder::GetExtraData(uint8_t** extra_data, size_t* size)
{
	if (!m_AMFContext || !m_AMFEncoder)
		throw std::exception("Called while not initialized.");

	amf::AMFVariant var;
	AMF_RESULT      res = GetExtraDataInternal(&var);
	if (res == AMF_OK && var.type == amf::AMF_VARIANT_INTERFACE) {
		amf::AMFBufferPtr buf(var.pInterface);

		*size = buf->GetSize();
		m_ExtraDataBuffer.resize(*size);
		std::memcpy(m_ExtraDataBuffer.data(), buf->GetNative(), *size);
		*extra_data = m_ExtraDataBuffer.data();

		return true;
	}
	return false;
}

bool Plugin::AMD::Encoder::EncodeAllocate(OUT amf::AMFSurfacePtr& surface)
{
	AMF_RESULT res;
	auto       clk_start = std::chrono::high_resolution_clock::now();

	// Allocate
	if (m_OpenCLSubmission) {
		res = m_AMFContext->AllocSurface(m_AMFMemoryType, m_AMFSurfaceFormat, m_Resolution.first, m_Resolution.second,
										 &surface);
	} else {
		// Required when not using OpenCL, can't directly write to GPU memory with memcpy.
		res = m_AMFContext->AllocSurface(amf::AMF_MEMORY_HOST, m_AMFSurfaceFormat, m_Resolution.first,
										 m_Resolution.second, &surface);
	}
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Unable to allocate Surface, error %ls (code %d)", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		PLOG_ERROR("%s", errMsg.data());
		return false;
	}

	// Performance Tracking
	auto     clk_end      = std::chrono::high_resolution_clock::now();
	uint64_t pf_timestamp = std::chrono::nanoseconds(clk_end.time_since_epoch()).count();
	uint64_t pf_time      = std::chrono::nanoseconds(clk_end - clk_start).count();

	surface->SetProperty(AMF_TIMESTAMP_ALLOCATE, pf_timestamp);
	surface->SetProperty(AMF_TIME_ALLOCATE, pf_time);

	return true;
}

bool Plugin::AMD::Encoder::EncodeStore(OUT amf::AMFSurfacePtr& surface, IN struct encoder_frame* frame)
{
	AMF_RESULT                  res;
	amf::AMFComputeSyncPointPtr pSyncPoint;
	auto                        clk_start = std::chrono::high_resolution_clock::now();

	if (m_OpenCLSubmission) {
		m_AMFCompute->PutSyncPoint(&pSyncPoint);
		res = surface->Convert(amf::AMF_MEMORY_OPENCL);
		if (res != AMF_OK) {
			QUICK_FORMAT_MESSAGE(errMsg,
								 "<Id: %llu> [Store] Conversion of Surface to OpenCL failed, error %ls (code %d)",
								 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_WARNING("%s", errMsg.data());
			return false;
		}
	}

	size_t planeCount = surface->GetPlanesCount();
	for (uint8_t i = 0; i < planeCount; i++) {
		amf::AMFPlanePtr plane  = surface->GetPlaneAt(i);
		int32_t          width  = plane->GetWidth();
		int32_t          height = plane->GetHeight();
		int32_t          hpitch = plane->GetHPitch();

		if (m_OpenCLSubmission) {
			static const amf_size l_origin[] = {0, 0, 0};
			const amf_size        l_size[]   = {(amf_size)width, (amf_size)height, 1};
			res = m_AMFCompute->CopyPlaneFromHost(frame->data[i], l_origin, l_size, frame->linesize[i],
												  surface->GetPlaneAt(i), false);
			if (res != AMF_OK) {
				QUICK_FORMAT_MESSAGE(errMsg,
									 "<Id: %llu> [Store] Unable to copy plane %d with OpenCL, error %ls (code %d)",
									 m_UniqueId, i, m_AMF->GetTrace()->GetResultText(res), res);
				PLOG_WARNING("%s", errMsg.data());
				return false;
			}
		} else {
			void* plane_nat = plane->GetNative();
			for (int32_t py = 0; py < height; py++) {
				int32_t plane_off = py * hpitch;
				int32_t frame_off = py * frame->linesize[i];
				std::memcpy(static_cast<void*>(static_cast<uint8_t*>(plane_nat) + plane_off),
							static_cast<void*>(frame->data[i] + frame_off), frame->linesize[i]);
			}
		}
	}

	if (m_OpenCLSubmission) {
		res = m_AMFCompute->FinishQueue();
		if (res != AMF_OK) {
			QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Store] Failed to finish OpenCL queue, error %ls (code %d)",
								 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_WARNING("%s", errMsg.data());
			return false;
		}
		pSyncPoint->Wait();
	}
	res = surface->Convert(m_AMFMemoryType);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Store] Conversion of Surface failed, error %ls (code %d)", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		PLOG_WARNING("%s", errMsg.data());
		return false;
	}

	// Data Stuff
	int64_t tsLast = (int64_t)round((frame->pts - 1) * m_TimestampStep);
	int64_t tsNow  = (int64_t)round(frame->pts * m_TimestampStep);

	/// Decode Timestamp
	surface->SetPts(tsNow);
	/// Presentation Timestamp
	surface->SetProperty(AMF_PRESENT_TIMESTAMP, frame->pts);
	/// Duration
	surface->SetDuration(tsNow - tsLast);
	/// Type override
	std::string printableType = HandleTypeOverride(surface, frame->pts);

	// Performance Tracking
	auto     clk_end      = std::chrono::high_resolution_clock::now();
	uint64_t pf_timestamp = std::chrono::nanoseconds(clk_end.time_since_epoch()).count();
	uint64_t pf_time      = std::chrono::nanoseconds(clk_end - clk_start).count();
	surface->SetProperty(AMF_TIMESTAMP_STORE, pf_timestamp);
	surface->SetProperty(AMF_TIME_STORE, pf_time);

	if (m_Debug) {
		PLOG_DEBUG("<Id: %llu> EncodeStore: PTS(%8lld) DTS(%8lld) TS(%16lld) Duration(%16lld) Type(%s)", m_UniqueId,
				   frame->pts, frame->pts, surface->GetPts(), surface->GetDuration(), printableType.c_str());
	}

	return true;
}

bool Plugin::AMD::Encoder::EncodeConvert(IN amf::AMFSurfacePtr& surface, OUT amf::AMFDataPtr& data)
{
	AMF_RESULT res;
	auto       clk_start = std::chrono::high_resolution_clock::now();

	if (m_OpenCLConversion) {
		res = surface->Convert(amf::AMF_MEMORY_OPENCL);
		if (res != AMF_OK) {
			QUICK_FORMAT_MESSAGE(errMsg,
								 "<Id: %llu> [Convert] Conversion of Surface to OpenCL failed, error %ls (code %d)",
								 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_WARNING("%s", errMsg.data());
			return false;
		}
	}
	res = m_AMFConverter->SubmitInput(surface);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Convert] Submit to converter failed, error %ls (code %d)", m_UniqueId,
							 m_AMF->GetTrace()->GetResultText(res), res);
		PLOG_WARNING("%s", errMsg.data());
		return false;
	}
	res = m_AMFConverter->QueryOutput(&data);
	if (res != AMF_OK) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Convert] Querying output from converter failed, error %ls (code %d)",
							 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
		PLOG_WARNING("%s", errMsg.data());
		return false;
	}
	if (m_OpenCLConversion) {
		res = surface->Convert(m_AMFMemoryType);
		if (res != AMF_OK) {
			QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Convert] Conversion of Surface failed, error %ls (code %d)",
								 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_WARNING("%s", errMsg.data());
			return false;
		}
	}

	// Performance Tracking
	auto     clk_end      = std::chrono::high_resolution_clock::now();
	uint64_t pf_timestamp = std::chrono::nanoseconds(clk_end.time_since_epoch()).count();
	uint64_t pf_time      = std::chrono::nanoseconds(clk_end - clk_start).count();
	surface->SetProperty(AMF_TIMESTAMP_CONVERT, pf_timestamp);
	surface->SetProperty(AMF_TIME_CONVERT, pf_time);

	return true;
}

bool Plugin::AMD::Encoder::EncodeMain(IN amf::AMFDataPtr& data, OUT amf::AMFDataPtr& packet)
{
	bool frameSubmitted = false, packetRetrieved = false;

	bool keepLooping = true;
	for (uint64_t attempt = 1; keepLooping; attempt++) {
		// Lets just change the stupid huge bitwise and/or into proper ifs.
		// Since this is rather small and can be kept in L1 we should not see
		// any differences in performance.
		if (m_InitialFramesSent) {
			if (m_InitialPacketRetrieved) {
				if (attempt <= m_SubmitQueryAttempts) {
					keepLooping = (!frameSubmitted || !packetRetrieved);
				} else {
					keepLooping = false;
				}
			} else {
				keepLooping = !frameSubmitted || !packetRetrieved;
			}
		} else {
			keepLooping = !frameSubmitted;
		}

		// Submit
		if (!frameSubmitted) {
			if (m_MultiThreading) { // Asynchronous
				std::unique_lock<std::mutex> slock(m_AsyncSend->mutex);
				if (m_AsyncSend->data == nullptr) {
					m_AsyncSend->data = data;
					m_AsyncSend->condvar.notify_all();
					frameSubmitted = true;
				} else {
					m_AsyncSend->condvar.notify_all();
				}
			} else {
				// Performance Tracking
				auto     clk   = std::chrono::high_resolution_clock::now();
				uint64_t pf_ts = std::chrono::nanoseconds(clk.time_since_epoch()).count();
				data->SetProperty(AMF_TIMESTAMP_SUBMIT, pf_ts);

				AMF_RESULT res = m_AMFEncoder->SubmitInput(data);
				if (m_Debug) {
					QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Main/Submit] SubmitInput returned %ls (code %d).",
										 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
					PLOG_WARNING("%s", errMsg.c_str());
				}

				if (res == AMF_OK) {
					frameSubmitted = true;
					m_SubmittedFrameCount++;
				} else if (res == AMF_INPUT_FULL) {
					if (m_InitialFramesSent == false) {
						QUICK_FORMAT_MESSAGE(
							errMsg, "<Id: %llu> Queue Size is too large, starting to query for packets...", m_UniqueId);
						PLOG_ERROR("%s", errMsg.data());
						m_InitialFramesSent = true;
					}
				} else {
					QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Main] Submitting Surface failed, error %ls (code %d)",
										 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
					PLOG_ERROR("%s", errMsg.data());
					return false;
				}
			}
		}

		// Retrieve
		if (m_InitialFramesSent && !packetRetrieved) {
			if (m_MultiThreading) {
				std::unique_lock<std::mutex> rlock(m_AsyncRetrieve->mutex);
				if (m_AsyncRetrieve->data != nullptr) {
					packet                   = m_AsyncRetrieve->data;
					m_AsyncRetrieve->data    = nullptr;
					packetRetrieved          = true;
					m_InitialPacketRetrieved = true;
				} else {
					m_AsyncRetrieve->condvar.notify_all();
					if (m_AsyncRetrieve->wakeupcount == 0)
						m_AsyncRetrieve->wakeupcount = 1;
				}
			} else {
				AMF_RESULT res = m_AMFEncoder->QueryOutput(&packet);
				if (m_Debug) {
					QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Main/Query] QueryOutput returned %ls (code %d).",
										 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
					PLOG_WARNING("%s", errMsg.c_str());
				}

				if (res == AMF_OK) {
					m_InitialPacketRetrieved = true;
					packetRetrieved          = true;

					// Performance Tracking
					auto     clk      = std::chrono::high_resolution_clock::now();
					uint64_t pf_query = std::chrono::nanoseconds(clk.time_since_epoch()).count(), pf_submit, pf_main;
					packet->GetProperty(AMF_TIMESTAMP_SUBMIT, &pf_submit);
					packet->SetProperty(AMF_TIMESTAMP_QUERY, pf_query);
					pf_main = (pf_query - pf_submit);
					packet->SetProperty(AMF_TIME_MAIN, pf_main);
				} else if (res == AMF_NEED_MORE_INPUT) {
					// Returned with B-Frames, means that we need more frames.
					if (!m_InitialPacketRetrieved)
						packetRetrieved = true;
				} else if (res != AMF_REPEAT) {
					QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Main] Retrieving Packet failed, error %ls (code %d)",
										 m_UniqueId, m_AMF->GetTrace()->GetResultText(res), res);
					PLOG_ERROR("%s", errMsg.data());
					return false;
				}
			}
		}

		if (!packetRetrieved || !frameSubmitted)
			std::this_thread::sleep_for(m_SubmitQueryWaitTimer);
	}
	if (!frameSubmitted) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Input Queue is full, encoder is overloaded!", m_UniqueId);
		PLOG_WARNING("%s", errMsg.data());
	}
	if (!m_InitialPacketRetrieved) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Waiting for initial frame...", m_UniqueId);
		PLOG_DEBUG("%s", errMsg.data());
	}
	if (m_InitialPacketRetrieved && !packetRetrieved) {
		QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> No output Packet, encoder is overloaded!", m_UniqueId);
		PLOG_WARNING("%s", errMsg.data());
	}
	if (m_SubmittedFrameCount >= (m_TimestampOffset + m_QueueSize))
		m_InitialFramesSent = true;
	return true;
}

bool Plugin::AMD::Encoder::EncodeLoad(IN amf::AMFDataPtr& data, OUT struct encoder_packet* packet,
									  OUT bool* received_packet)
{
	if (data == nullptr)
		return true;

	amf::AMFBufferPtr pBuffer   = amf::AMFBufferPtr(data);
	auto              clk_start = std::chrono::high_resolution_clock::now();

	// Timestamps
	packet->type = OBS_ENCODER_VIDEO;
	/// Present Timestamp
	data->GetProperty(AMF_PRESENT_TIMESTAMP, &packet->pts);
	/// Decode Timestamp
	packet->dts = (int64_t)round((double_t)data->GetPts() / m_TimestampStep) - m_TimestampOffset;
	/// Data
	PacketPriorityAndKeyframe(data, packet);
	packet->size = pBuffer->GetSize();
	if (m_PacketDataBuffer.size() < packet->size) {
		size_t newBufferSize = (size_t)exp2(ceil(log2((double)packet->size)));
		//AMF_LOG_DEBUG("Packet Buffer was resized to %d byte from %d byte.", newBufferSize, m_PacketDataBuffer.size());
		m_PacketDataBuffer.resize(newBufferSize);
	}
	packet->data = m_PacketDataBuffer.data();
	std::memcpy(packet->data, pBuffer->GetNative(), packet->size);

	// Performance Tracking
	auto     clk_end = std::chrono::high_resolution_clock::now();
	uint64_t pf_allocate_ts, pf_allocate_t, pf_store_ts, pf_store_t, pf_convert_ts, pf_convert_t, pf_submit_ts,
		pf_query_ts, pf_main_t, pf_load_ts, pf_load_t;

	data->GetProperty(AMF_TIMESTAMP_ALLOCATE, &pf_allocate_ts);
	data->GetProperty(AMF_TIME_ALLOCATE, &pf_allocate_t);
	data->GetProperty(AMF_TIMESTAMP_STORE, &pf_store_ts);
	data->GetProperty(AMF_TIME_STORE, &pf_store_t);
	data->GetProperty(AMF_TIMESTAMP_CONVERT, &pf_convert_ts);
	data->GetProperty(AMF_TIME_CONVERT, &pf_convert_t);
	data->GetProperty(AMF_TIMESTAMP_SUBMIT, &pf_submit_ts);
	data->GetProperty(AMF_TIMESTAMP_QUERY, &pf_query_ts);
	data->GetProperty(AMF_TIME_MAIN, &pf_main_t);
	pf_load_ts = std::chrono::nanoseconds(clk_end.time_since_epoch()).count();
	pf_load_t  = std::chrono::nanoseconds(clk_end - clk_start).count();

	if (m_Debug) {
		std::string printableType = "Unknown";
		if (m_Codec == Codec::AVC || m_Codec == Codec::SVC) {
			uint64_t type = AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR;
			data->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &type);
			switch ((AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_ENUM)type) {
			case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR:
				printableType = "IDR";
				break;
			case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I:
				printableType = "I";
				break;
			case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_P:
				printableType = "P";
				break;
			case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_B:
				printableType = "B";
				break;
			}
		}
		if (m_Codec == Codec::HEVC) {
			uint64_t type = AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR;
			data->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, &type);
			switch ((AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_ENUM)type) {
			case AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR:
				printableType = "IDR";
				break;
			case AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_I:
				printableType = "I";
				break;
			case AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_P:
				printableType = "P";
				break;
			}
		}

		PLOG_DEBUG("<Id: %" PRIu64 "> EncodeLoad: PTS(%8" PRIu64 ") DTS(%8" PRIu64 ") TS(%16" PRIu64
				   ") Duration(%16" PRIu64 ") Size(%16" PRIuPTR ") Type(%s)",
				   m_UniqueId, packet->pts, packet->dts, data->GetPts(), data->GetDuration(), packet->size,
				   printableType.c_str());
		PLOG_DEBUG("<Id: %" PRIu64 ">    Timings: Allocate(%8" PRIu64 " ns) Store(%8" PRIu64 " ns) Convert(%8" PRIu64
				   " ns) Main(%8" PRIu64 " ns) Load(%8" PRIu64 " ns)",
				   m_UniqueId, pf_allocate_t, pf_store_t, pf_convert_t, pf_main_t, pf_load_t);
	}
	if (m_InitialFrameLatency == 0) {
		m_InitialFrameLatency = pf_main_t;
		PLOG_INFO("<Id: %" PRIu64 "> Initial Frame Latency is %" PRIu64 " nanoseconds.", m_UniqueId,
				  m_InitialFrameLatency);
	}

	*received_packet = true;

	return true;
}

int32_t Plugin::AMD::Encoder::AsyncSendMain(Encoder* obj)
{
	return obj->AsyncSendLocalMain();
}

int32_t Plugin::AMD::Encoder::AsyncSendLocalMain()
{
	EncoderThreadingData* own = m_AsyncSend;

	std::unique_lock<std::mutex> lock(own->mutex);
	while (!own->shutdown) {
		own->condvar.wait(lock, [&own] { return own->shutdown || (own->data != nullptr); });

		if (own->data == nullptr)
			continue;

		{
			// Performance Tracking
			auto     clk   = std::chrono::high_resolution_clock::now();
			uint64_t pf_ts = std::chrono::nanoseconds(clk.time_since_epoch()).count();
			own->data->SetProperty(AMF_TIMESTAMP_SUBMIT, pf_ts);
		}

		AMF_RESULT res = m_AMFEncoder->SubmitInput(own->data);
		if (m_Debug) {
			QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Main/Submit] SubmitInput returned %ls (code %d).", m_UniqueId,
								 m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_WARNING("%s", errMsg.c_str());
		}

		if (res == AMF_OK) {
			own->data = nullptr;
			m_SubmittedFrameCount++;
		} else if (res == AMF_INPUT_FULL) {
			if (m_InitialFramesSent == false) {
				QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Queue Size is too large, starting to query for packets...",
									 m_UniqueId);
				PLOG_ERROR("%s", errMsg.data());
				m_InitialFramesSent = true;
			}
		} else {
			QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Submitting Surface failed, error %ls (code %d)", m_UniqueId,
								 m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_ERROR("%s", errMsg.data());
			return -1;
		}

		std::this_thread::sleep_for(m_SubmitQueryWaitTimer);
	}
	return 0;
}

int32_t Plugin::AMD::Encoder::AsyncRetrieveMain(Encoder* obj)
{
	return obj->AsyncRetrieveLocalMain();
}

int32_t Plugin::AMD::Encoder::AsyncRetrieveLocalMain()
{
	EncoderThreadingData* own = m_AsyncRetrieve;

	std::unique_lock<std::mutex> lock(own->mutex);
	while (!own->shutdown) {
		own->condvar.wait(lock, [&own] { return own->shutdown || (own->wakeupcount > 0); });

		if (own->wakeupcount == 0)
			continue;

		if (own->data != nullptr)
			continue;

		amf::AMFDataPtr packet;
		AMF_RESULT      res = m_AMFEncoder->QueryOutput(&packet);
		if (m_Debug) {
			QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> [Main/Query] QueryOutput returned %ls (code %d).", m_UniqueId,
								 m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_WARNING("%s", errMsg.c_str());
		}

		if (res == AMF_OK) {
			own->data = packet;
			own->wakeupcount--;

			// Performance Tracking
			{
				auto     clk      = std::chrono::high_resolution_clock::now();
				uint64_t pf_query = std::chrono::nanoseconds(clk.time_since_epoch()).count(), pf_submit, pf_main;
				packet->GetProperty(AMF_TIMESTAMP_SUBMIT, &pf_submit);
				packet->SetProperty(AMF_TIMESTAMP_QUERY, pf_query);
				pf_main = (pf_query - pf_submit);
				packet->SetProperty(AMF_TIME_MAIN, pf_main);
			}
		} else if (res == AMF_REPEAT) {
			m_AsyncRetrieve->condvar.notify_all();
		} else if (res == AMF_NEED_MORE_INPUT) {
			{
				std::unique_lock<std::mutex> slock(m_AsyncSend->mutex);
				if (m_AsyncSend->data != nullptr)
					m_AsyncSend->condvar.notify_all();
			}
		} else {
			QUICK_FORMAT_MESSAGE(errMsg, "<Id: %llu> Retrieving Packet failed, error %ls (code %d)", m_UniqueId,
								 m_AMF->GetTrace()->GetResultText(res), res);
			PLOG_ERROR("%s", errMsg.data());
			return -1;
		}

		std::this_thread::sleep_for(m_SubmitQueryWaitTimer);
	}
	return 0;
}
#endif
