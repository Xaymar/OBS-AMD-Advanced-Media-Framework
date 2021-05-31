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

#pragma once
#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "amf.hpp"
#include "api-base.hpp"
#include "plugin.hpp"

#include <components/Component.h>

#ifndef LITE_OBS
#pragma warning(push)
#pragma warning(disable : 4201)
extern "C" {
#include <obs-encoder.h>
#include <obs.h>
}
#pragma warning(pop)
#endif

#define AMF_TIMESTAMP_ALLOCATE L"TS_Allocate"
#define AMF_TIME_ALLOCATE L"T_Allocate"
#define AMF_TIMESTAMP_STORE L"TS_Store"
#define AMF_TIME_STORE L"T_Store"
#define AMF_TIMESTAMP_CONVERT L"TS_Convert"
#define AMF_TIME_CONVERT L"T_Convert"
#define AMF_TIMESTAMP_SUBMIT L"TS_Submit"
#define AMF_TIMESTAMP_QUERY L"TS_Query"
#define AMF_TIME_MAIN L"T_Main" // Time between Submit and Query

#define AMF_PRESENT_TIMESTAMP L"PTS"

namespace Plugin {
	namespace AMD {
		// Initialization Parameters
		enum class Codec : uint8_t {
			AVC,
			SVC,
			HEVC,
		};
		enum class ColorFormat : uint8_t {
			/*	Support Table
			 *	Open Broadcaster	AMD AMF
			 *	-- 4:2:0 Formats --
			 *	VIDEO_FORMAT_I420	AMF_SURFACE_YUV420P
			 *	VIDEO_FORMAT_NV12	AMF_SURFACE_NV12
			 *						AMF_SURFACE_YV12
			 *	-- 4:2:2 Formats --
			 *	VIDEO_FORMAT_YVYV
			 *	VIDEO_FORMAT_YUY2	AMF_SURFACE_YUY2
			 *	VIDEO_FORMAT_UYVY
			 *
			 *	-- 4:4:4 Formats --
			 *	VIDEO_FORMAT_I444
			 *
			 *	-- Packed Uncompressed Formats --
			 *	VIDEO_FORMAT_RGBA	AMF_SURFACE_RGBA
			 *	VIDEO_FORMAT_BGRA	AMF_SURFACE_BGRA
			 *						AMF_SURFACE_ARGB
			 *	VIDEO_FORMAT_BGRX
			 *
			 *	-- Single/Dual Channel Formats --
			 *	VIDEO_FORMAT_Y800	AMF_SURFACE_GRAY8
			 *						AMF_SURFACE_U8V8
			 *
			 *	-- HDR Color Formats --
			 *						AMF_SURFACE_P010
			 *						AMF_SURFACE_RGBA_F16
			 */

			I420,
			NV12,
			YUY2,
			BGRA,
			RGBA,
			GRAY,
		};
		enum class ColorSpace : uint8_t {
			BT601,
			BT709,
			BT2020,
			SRGB,
		};

		// Properties
		enum class Usage : uint8_t { Transcoding, UltraLowLatency, LowLatency, Webcam };
		enum class QualityPreset : uint8_t {
			Speed,
			Balanced,
			Quality,
		};
		enum class Profile : uint16_t {
			ConstrainedBaseline = 256,
			Baseline            = 66,
			Main                = 77,
			ConstrainedHigh     = 257,
			High                = 100,
		};
		enum class ProfileLevel : uint8_t {
			Automatic,
			L10 = 10,
			L11,
			L12,
			L13,
			L20 = 20,
			L21,
			L22,
			L30 = 30,
			L31,
			L32,
			L40 = 40,
			L41,
			L42,
			L50 = 50,
			L51,
			L52,
			L60 = 60,
			L61,
			L62,
		};
		enum class CodingType : uint8_t {
			Automatic,
			CALVC,
			CABAC,
		};
		enum class RateControlMethod : uint8_t {
			ConstantQP,
			LatencyConstrainedVariableBitrate,
			PeakConstrainedVariableBitrate,
			ConstantBitrate,
		};
		enum class PrePassMode : uint8_t {
			Disabled,
			Enabled,
			EnabledAtHalfScale,
			EnabledAtQuarterScale,
		};
		enum class SliceControlMode : uint8_t {
			Unknown0,
			Unknown1,
			Unknown2,
			Unknown3,
		};

		class Encoder {
			protected:
			Encoder(Codec codec, std::shared_ptr<API::IAPI> videoAPI, const API::Adapter& videoAdapter,
					bool useOpenCLSubmission, bool useOpenCLConversion, ColorFormat colorFormat, ColorSpace colorSpace,
					bool fullRangeColor, bool multiThreaded, size_t queueSize);

			public:
			virtual ~Encoder();

			public:
			uint64_t GetUniqueId();

			//void SetCodec(Codec v);
			Codec GetCodec();

			//void SetVideoAPI(std::shared_ptr<API::IAPI> v);
			std::shared_ptr<API::IAPI> GetVideoAPI();

			//void SetVideoAdapter(API::Adapter v);
			API::Adapter GetVideoAdapter();

			//void SetOpenCLEnabled(bool v);
			bool IsOpenCLEnabled();

			//void SetColorFormat(ColorFormat v);
			ColorFormat GetColorFormat();

			//void SetColorSpace(ColorSpace v);
			ColorSpace GetColorSpace();

			//void SetFullRangeColor(bool v);
			bool IsFullRangeColor();

			//void SetAsynchronousQueueEnabled(bool v);
			bool IsMultiThreaded();

			//void SetAsynchronousQueueSize(size_t v);
			size_t GetQueueSize();

			void SetDebug(bool v);
			bool IsDebug();

			//bool Initialize();
			virtual std::vector<Usage> CapsUsage()       = 0;
			virtual void               SetUsage(Usage v) = 0;
			virtual Usage              GetUsage()        = 0;

			virtual std::vector<QualityPreset> CapsQualityPreset()               = 0;
			virtual void                       SetQualityPreset(QualityPreset v) = 0;
			virtual QualityPreset              GetQualityPreset()                = 0;

#ifndef LITE_OBS
			virtual std::pair<std::pair<uint32_t, uint32_t>, std::pair<uint32_t, uint32_t>> CapsResolution() = 0;
			virtual void                          SetResolution(std::pair<uint32_t, uint32_t> v)             = 0;
			virtual std::pair<uint32_t, uint32_t> GetResolution()                                            = 0;

			virtual void                          SetAspectRatio(std::pair<uint32_t, uint32_t> v) = 0;
			virtual std::pair<uint32_t, uint32_t> GetAspectRatio()                                = 0;

			virtual void                          SetFrameRate(std::pair<uint32_t, uint32_t> v) = 0;
			virtual std::pair<uint32_t, uint32_t> GetFrameRate()                                = 0;

			virtual std::vector<Profile> CapsProfile()         = 0;
			virtual void                 SetProfile(Profile v) = 0;
			virtual Profile              GetProfile()          = 0;

			virtual std::vector<ProfileLevel> CapsProfileLevel()                               = 0;
			virtual void                      SetProfileLevel(ProfileLevel v)                  = 0;
			virtual void                      SetProfileLevel(ProfileLevel v, std::pair<uint32_t, uint32_t> r,
															  std::pair<uint32_t, uint32_t> h) = 0;
			virtual ProfileLevel              GetProfileLevel()                                = 0;

			virtual std::vector<CodingType> CapsCodingType()            = 0;
			virtual void                    SetCodingType(CodingType v) = 0;
			virtual CodingType              GetCodingType()             = 0;

			virtual std::pair<uint64_t, uint64_t> CapsMaximumReferenceFrames()          = 0;
			virtual void                          SetMaximumReferenceFrames(uint64_t v) = 0;
			virtual uint64_t                      GetMaximumReferenceFrames()           = 0;

			virtual std::pair<uint32_t, uint32_t> CapsMaximumLongTermReferenceFrames()          = 0;
			virtual void                          SetMaximumLongTermReferenceFrames(uint32_t v) = 0;
			virtual uint32_t                      GetMaximumLongTermReferenceFrames()           = 0;

			virtual std::vector<RateControlMethod> CapsRateControlMethod()                   = 0;
			virtual void                           SetRateControlMethod(RateControlMethod v) = 0;
			virtual RateControlMethod              GetRateControlMethod()                    = 0;

			virtual std::vector<PrePassMode> CapsPrePassMode()             = 0;
			virtual void                     SetPrePassMode(PrePassMode v) = 0;
			virtual PrePassMode              GetPrePassMode()              = 0;

			virtual void SetVarianceBasedAdaptiveQuantizationEnabled(bool v) = 0;
			virtual bool IsVarianceBasedAdaptiveQuantizationEnabled()        = 0;

			virtual void SetFrameSkippingEnabled(bool v) = 0;
			virtual bool IsFrameSkippingEnabled()        = 0;

			virtual void     SetFrameSkippingPeriod(uint32_t v);
			virtual uint32_t GetFrameSkippingPeriod();

			/// false skips every nth frame, true keeps every nth frame.
			virtual void SetFrameSkippingBehaviour(bool v);
			virtual bool GetFrameSkippingBehaviour();

			/// Enforce Hypothethical Reference Decoder Restrictions
			virtual void SetEnforceHRDEnabled(bool v) = 0;
			virtual bool IsEnforceHRDEnabled()        = 0;

			virtual void SetFillerDataEnabled(bool v) = 0;
			virtual bool IsFillerDataEnabled()        = 0;

			virtual std::pair<uint64_t, uint64_t> CapsTargetBitrate()          = 0;
			virtual void                          SetTargetBitrate(uint64_t v) = 0;
			virtual uint64_t                      GetTargetBitrate()           = 0;

			virtual std::pair<uint64_t, uint64_t> CapsPeakBitrate()          = 0;
			virtual void                          SetPeakBitrate(uint64_t v) = 0;
			virtual uint64_t                      GetPeakBitrate()           = 0;

			virtual std::pair<uint8_t, uint8_t> CapsIFrameQP()         = 0;
			virtual void                        SetIFrameQP(uint8_t v) = 0;
			virtual uint8_t                     GetIFrameQP()          = 0;

			virtual std::pair<uint8_t, uint8_t> CapsPFrameQP()         = 0;
			virtual void                        SetPFrameQP(uint8_t v) = 0;
			virtual uint8_t                     GetPFrameQP()          = 0;

			virtual void     SetMaximumAccessUnitSize(uint32_t v) = 0;
			virtual uint32_t GetMaximumAccessUnitSize()           = 0;

			virtual std::pair<uint64_t, uint64_t> CapsVBVBufferSize()          = 0;
			virtual void                          SetVBVBufferSize(uint64_t v) = 0;
			void                                  SetVBVBufferStrictness(double_t v);
			virtual uint64_t                      GetVBVBufferSize() = 0;

			virtual void  SetVBVBufferInitialFullness(double v) = 0;
			virtual float GetInitialVBVBufferFullness()         = 0;

			virtual void     SetIDRPeriod(uint32_t v) = 0;
			virtual uint32_t GetIDRPeriod()           = 0;

			virtual void     SetIFramePeriod(uint32_t v);
			virtual uint32_t GetIFramePeriod();

			virtual void     SetPFramePeriod(uint32_t v);
			virtual uint32_t GetPFramePeriod();

			virtual void     SetBFramePeriod(uint32_t v);
			virtual uint32_t GetBFramePeriod();

			virtual void SetGOPAlignmentEnabled(bool v) = 0;
			virtual bool IsGOPAlignmentEnabled()        = 0;

			virtual void SetDeblockingFilterEnabled(bool v) = 0;
			virtual bool IsDeblockingFilterEnabled()        = 0;

			virtual void SetMotionEstimationQuarterPixelEnabled(bool v) = 0;
			virtual bool IsMotionEstimationQuarterPixelEnabled()        = 0;

			virtual void SetMotionEstimationHalfPixelEnabled(bool v) = 0;
			virtual bool IsMotionEstimationHalfPixelEnabled()        = 0;

			void Start();
			void Restart();
			void Stop();

			bool         IsStarted();
			virtual void LogProperties() = 0;

			bool Encode(struct encoder_frame* f, struct encoder_packet* p, bool* b);
			void GetVideoInfo(struct video_scale_info* info);
			bool GetExtraData(uint8_t** extra_data, size_t* size);

			protected:
			void UpdateFrameRateValues();

			private:
			virtual void        PacketPriorityAndKeyframe(amf::AMFDataPtr& d, struct encoder_packet* p) = 0;
			virtual AMF_RESULT  GetExtraDataInternal(amf::AMFVariant* p)                                = 0;
			virtual std::string HandleTypeOverride(amf::AMFSurfacePtr& d, uint64_t index)               = 0;

			bool EncodeAllocate(OUT amf::AMFSurfacePtr& surface);
			bool EncodeStore(OUT amf::AMFSurfacePtr& surface, IN struct encoder_frame* frame);
			bool EncodeConvert(IN amf::AMFSurfacePtr& surface, OUT amf::AMFDataPtr& data);
			bool EncodeMain(IN amf::AMFDataPtr& data, OUT amf::AMFDataPtr& packet);
			bool EncodeLoad(IN amf::AMFDataPtr& data, OUT struct encoder_packet* packet, OUT bool* received_packet);

			static int32_t AsyncSendMain(Encoder* obj);
			int32_t        AsyncSendLocalMain();
			static int32_t AsyncRetrieveMain(Encoder* obj);
			int32_t        AsyncRetrieveLocalMain();

#endif

			protected:
			// AMF Internals
			Plugin::AMD::AMF*       m_AMF;
			amf::AMFFactory*        m_AMFFactory;
			amf::AMFContextPtr      m_AMFContext;
			amf::AMFComputePtr      m_AMFCompute;
			amf::AMFComponentPtr    m_AMFEncoder;
			amf::AMFComponentPtr    m_AMFConverter;
			amf::AMF_MEMORY_TYPE    m_AMFMemoryType;
			amf::AMF_SURFACE_FORMAT m_AMFSurfaceFormat;

			// API Related
			std::shared_ptr<API::IAPI>     m_API;
			API::Adapter                   m_APIAdapter;
			std::shared_ptr<API::Instance> m_APIDevice;

			// Buffers
			std::vector<uint8_t> m_PacketDataBuffer;
			std::vector<uint8_t> m_ExtraDataBuffer;

			// Flags
			bool m_Initialized;
			bool m_Started;
			bool m_OpenCL;
			bool m_OpenCLSubmission; // Submit Frames using OpenCL
			bool m_OpenCLConversion; // Convert Frames using OpenCL instead of DirectCompute
			bool m_Debug;

			// Properties
			uint64_t    m_UniqueId;
			Codec       m_Codec;
			ColorFormat m_ColorFormat;
			ColorSpace  m_ColorSpace;
			bool        m_FullColorRange;
			size_t      m_QueueSize;

			/// Resolution + Rate
			std::pair<uint32_t, uint32_t> m_Resolution;
			std::pair<uint32_t, uint32_t> m_FrameRate;
			double_t                      m_FrameRateFraction;

			/// Timings
			double_t                 m_TimestampStep;
			uint64_t                 m_TimestampStepRounded;
			uint64_t                 m_TimestampOffset;
			std::chrono::nanoseconds m_SubmitQueryWaitTimer;
			uint64_t                 m_SubmitQueryAttempts;
			uint64_t                 m_InitialFrameLatency;

			/// Status
			uint64_t m_SubmittedFrameCount;
			bool     m_InitialFramesSent;
			bool     m_InitialPacketRetrieved;

			/// Periods
			uint32_t m_PeriodIDR;
			uint32_t m_PeriodIFrame;
			uint32_t m_PeriodPFrame;
			uint32_t m_PeriodBFrame;
			uint32_t m_FrameSkipPeriod;
			bool     m_FrameSkipKeepOnlyNth; // false = drop every xth frame, true = drop all but every xth frame

			/// Multi-Threading
			bool m_MultiThreading;
			struct EncoderThreadingData {
				// Thread
				std::thread worker;
				bool        shutdown;
				// Semaphore
				size_t                  wakeupcount = 0;
				std::condition_variable condvar;
				std::mutex              mutex;
				// Data
				amf::AMFDataPtr data;
			} * m_AsyncSend, *m_AsyncRetrieve;
		};
	} // namespace AMD
} // namespace Plugin
