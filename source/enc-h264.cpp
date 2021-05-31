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

#include "enc-h264.hpp"
#include "amf-capabilities.hpp"
#include "amf-encoder-h264.hpp"
#include "strings.hpp"
#include "utility.hpp"

#define PREFIX "[H264/AVC]"

using namespace Plugin;
using namespace Plugin::AMD;
using namespace Plugin::Interface;
using namespace Utility;

static obs_encoder_info _oei = {};

void Plugin::Interface::H264Interface::encoder_register()
{
	_oei.type  = OBS_ENCODER_VIDEO;
	_oei.id    = "amd_amf_h264";
	_oei.codec = "h264";

	_oei.get_name       = get_name;
	_oei.get_defaults   = get_defaults;
	_oei.get_properties = get_properties;
	_oei.create         = create;
	_oei.destroy        = destroy;
	_oei.encode         = encode;
	_oei.update         = update;
	_oei.get_video_info = get_video_info;
	_oei.get_extra_data = get_extra_data;

	// Test if we actually have AVC support.
	if (!AMD::CapabilityManager::Instance()->IsCodecSupported(Codec::AVC)) {
		PLOG_WARNING(PREFIX " Not supported by any GPU, disabling...");
		return;
	}

	obs_register_encoder(&_oei);
	PLOG_DEBUG(PREFIX " Registered.");
}

const char* Plugin::Interface::H264Interface::get_name(void*) noexcept
{
	return "H264/AVC Encoder (" PLUGIN_NAME ")";
}

void* Plugin::Interface::H264Interface::create(obs_data_t* settings, obs_encoder_t* encoder) noexcept
try {
	try {
		return new Plugin::Interface::H264Interface(settings, encoder);
	} catch (const std::exception& e) {
		PLOG_ERROR("%s", e.what());
	}
	return nullptr;
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
	return nullptr;
}

void Plugin::Interface::H264Interface::destroy(void* data) noexcept
try {
	if (data)
		delete static_cast<Plugin::Interface::H264Interface*>(data);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in %s: %s", __FUNCTION_NAME__, ex.what());
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
}

bool Plugin::Interface::H264Interface::encode(void* data, struct encoder_frame* frame, struct encoder_packet* packet,
											  bool* received_packet) noexcept
try {
	if (data)
		return static_cast<Plugin::Interface::H264Interface*>(data)->encode(frame, packet, received_packet);
	return false;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in %s: %s", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
	return false;
}

void Plugin::Interface::H264Interface::get_defaults(obs_data_t* data) noexcept
{
#pragma region OBS - Enforce Streaming Service Restrictions
	obs_data_set_default_int(data, "keyint_sec", 0);
	obs_data_set_default_string(data, "rate_control", "");
	obs_data_set_default_string(data, "profile", "");
	obs_data_set_default_string(data, "preset", "");
#pragma endregion OBS - Enforce Streaming Service Restrictions

	// Preset
	obs_data_set_default_int(data, P_PRESET, static_cast<int64_t>(Presets::None));

	// Static Properties
	//obs_data_set_default_int(data, P_USAGE, static_cast<int64_t>(Usage::Transcoding));
	obs_data_set_default_int(data, P_QUALITYPRESET, static_cast<int64_t>(QualityPreset::Balanced));
	obs_data_set_default_int(data, P_PROFILE, static_cast<int64_t>(Profile::High));
	obs_data_set_default_int(data, P_PROFILELEVEL, static_cast<int64_t>(ProfileLevel::Automatic));
	//obs_data_set_default_frames_per_second(data, P_ASPECTRATIO, media_frames_per_second{ 1, 1 }, "");
	obs_data_set_default_int(data, P_CODINGTYPE, static_cast<int64_t>(CodingType::Automatic));
	obs_data_set_default_int(data, P_MAXIMUMREFERENCEFRAMES, 4);

	// Rate Control Properties
	obs_data_set_default_int(data, ("last" P_RATECONTROLMETHOD), -1);
	obs_data_set_default_int(data, P_RATECONTROLMETHOD, static_cast<int64_t>(RateControlMethod::ConstantBitrate));
	obs_data_set_default_int(data, P_PREPASSMODE, static_cast<int64_t>(PrePassMode::Disabled));
	obs_data_set_default_int(data, "bitrate", 3500);
	obs_data_set_default_int(data, P_BITRATE_PEAK, 9000);
	obs_data_set_default_int(data, P_QP_IFRAME, 22);
	obs_data_set_default_int(data, P_QP_PFRAME, 22);
	obs_data_set_default_int(data, P_QP_BFRAME, 22);
	obs_data_set_default_int(data, P_QP_MINIMUM, 18);
	obs_data_set_default_int(data, P_QP_MAXIMUM, 51);
	obs_data_set_default_int(data, P_FILLERDATA, 1);
	obs_data_set_default_int(data, P_FRAMESKIPPING, 0);
	obs_data_set_default_int(data, P_FRAMESKIPPING_PERIOD, 0);
	obs_data_set_default_int(data, P_FRAMESKIPPING_BEHAVIOUR, 0);
	obs_data_set_default_int(data, P_VBAQ, 0);
	obs_data_set_default_int(data, P_ENFORCEHRD, 1);
	obs_data_set_default_int(data, P_HIGHMOTIONQUALITYBOOST, -1);

	// VBV Buffer
	obs_data_set_default_int(data, ("last" P_VBVBUFFER), -1);
	obs_data_set_default_int(data, P_VBVBUFFER, 0);
	obs_data_set_default_int(data, P_VBVBUFFER_SIZE, 3500);
	obs_data_set_default_double(data, P_VBVBUFFER_STRICTNESS, 50);
	obs_data_set_default_double(data, P_VBVBUFFER_INITIALFULLNESS, 100);

	// Picture Control
	obs_data_set_default_double(data, P_INTERVAL_KEYFRAME, 2.0);
	obs_data_set_default_int(data, P_PERIOD_IDR_H264, 0);
	obs_data_set_default_double(data, P_INTERVAL_IFRAME, 0.0);
	obs_data_set_default_int(data, P_PERIOD_IFRAME, 0);
	obs_data_set_default_double(data, P_INTERVAL_PFRAME, 0.0);
	obs_data_set_default_int(data, P_PERIOD_PFRAME, 0);
	obs_data_set_default_double(data, P_INTERVAL_BFRAME, 0.0);
	obs_data_set_default_int(data, P_PERIOD_BFRAME, 0);
	obs_data_set_default_int(data, ("last" P_BFRAME_PATTERN), -1);
	obs_data_set_default_int(data, P_BFRAME_PATTERN, 0);
	obs_data_set_default_int(data, ("last" P_BFRAME_REFERENCE), -1);
	obs_data_set_default_int(data, P_BFRAME_REFERENCE, 0);
	obs_data_set_default_int(data, P_BFRAME_REFERENCEDELTAQP, 2);
	obs_data_set_default_int(data, P_BFRAME_DELTAQP, 4);
	obs_data_set_default_int(data, P_DEBLOCKINGFILTER, 1);
	obs_data_set_default_int(data, P_MOTIONESTIMATION, 3);

	// System Properties
	obs_data_set_default_string(data, ("last" P_VIDEO_API), "");
	obs_data_set_default_string(data, P_VIDEO_API, "");
	obs_data_set_default_int(data, ("last" P_VIDEO_ADAPTER), 0);
	obs_data_set_default_int(data, P_VIDEO_ADAPTER, 0);
	obs_data_set_default_int(data, P_OPENCL_TRANSFER, 0);
	obs_data_set_default_int(data, P_OPENCL_CONVERSION, 0);
	obs_data_set_default_int(data, P_MULTITHREADING, 0);
	obs_data_set_default_int(data, P_QUEUESIZE, 8);
	obs_data_set_default_int(data, ("last" P_VIEW), -1);
	obs_data_set_default_int(data, P_VIEW, static_cast<int64_t>(ViewMode::Basic));
	obs_data_set_default_bool(data, P_DEBUG, false);
	obs_data_set_default_int(data, P_VERSION, PLUGIN_VERSION_FULL);
}

obs_properties_t* Plugin::Interface::H264Interface::get_properties(void* data) noexcept
try {
	obs_properties* props = obs_properties_create();
	obs_property_t* p;

#pragma region Preset
	p = obs_properties_add_list(props, P_PRESET, P_TRANSLATE(P_PRESET), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_modified_callback(p, properties_modified);
	obs_property_list_add_int(p, "", -1);
	obs_property_list_add_int(p, P_TRANSLATE(P_PRESET_RESETTODEFAULTS), static_cast<int32_t>(Presets::ResetToDefaults));
	obs_property_list_add_int(p, P_TRANSLATE(P_PRESET_RECORDING), static_cast<int32_t>(Presets::Recording));
	obs_property_list_add_int(p, P_TRANSLATE(P_PRESET_HIGHQUALITY), static_cast<int32_t>(Presets::HighQuality));
	obs_property_list_add_int(p, P_TRANSLATE(P_PRESET_INDISTINGUISHABLE),
							  static_cast<int32_t>(Presets::Indistinguishable));
	obs_property_list_add_int(p, P_TRANSLATE(P_PRESET_LOSSLESS), static_cast<int32_t>(Presets::Lossless));
	obs_property_list_add_int(p, P_TRANSLATE(P_PRESET_TWITCH), static_cast<int32_t>(Presets::Twitch));
	obs_property_list_add_int(p, P_TRANSLATE(P_PRESET_YOUTUBE), static_cast<int32_t>(Presets::YouTube));
#pragma endregion Preset

	// Static Properties
#pragma region Quality Preset
	p = obs_properties_add_list(props, P_QUALITYPRESET, P_TRANSLATE(P_QUALITYPRESET), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_QUALITYPRESET)));
	obs_property_list_add_int(p, P_TRANSLATE(P_QUALITYPRESET_SPEED), static_cast<int32_t>(QualityPreset::Speed));
	obs_property_list_add_int(p, P_TRANSLATE(P_QUALITYPRESET_BALANCED), static_cast<int32_t>(QualityPreset::Balanced));
	obs_property_list_add_int(p, P_TRANSLATE(P_QUALITYPRESET_QUALITY), static_cast<int32_t>(QualityPreset::Quality));
#pragma endregion Quality Preset

#pragma region Profile, Levels
	p = obs_properties_add_list(props, P_PROFILE, P_TRANSLATE(P_PROFILE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_PROFILE)));
	obs_property_list_add_int(p, "Constrained Baseline", static_cast<int32_t>(Profile::ConstrainedBaseline));
	obs_property_list_add_int(p, "Baseline", static_cast<int32_t>(Profile::Baseline));
	obs_property_list_add_int(p, "Main", static_cast<int32_t>(Profile::Main));
	obs_property_list_add_int(p, "Constrained High", static_cast<int32_t>(Profile::ConstrainedHigh));
	obs_property_list_add_int(p, "High", static_cast<int32_t>(Profile::High));

	p = obs_properties_add_list(props, P_PROFILELEVEL, P_TRANSLATE(P_PROFILELEVEL), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_PROFILELEVEL)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_AUTOMATIC), static_cast<int32_t>(ProfileLevel::Automatic));
	obs_property_list_add_int(p, "1.0", static_cast<int32_t>(ProfileLevel::L10));
	obs_property_list_add_int(p, "1.1", static_cast<int32_t>(ProfileLevel::L11));
	obs_property_list_add_int(p, "1.2", static_cast<int32_t>(ProfileLevel::L12));
	obs_property_list_add_int(p, "1.3", static_cast<int32_t>(ProfileLevel::L13));
	obs_property_list_add_int(p, "2.0", static_cast<int32_t>(ProfileLevel::L20));
	obs_property_list_add_int(p, "2.1", static_cast<int32_t>(ProfileLevel::L21));
	obs_property_list_add_int(p, "2.2", static_cast<int32_t>(ProfileLevel::L22));
	obs_property_list_add_int(p, "3.0", static_cast<int32_t>(ProfileLevel::L30));
	obs_property_list_add_int(p, "3.1", static_cast<int32_t>(ProfileLevel::L31));
	obs_property_list_add_int(p, "3.2", static_cast<int32_t>(ProfileLevel::L32));
	obs_property_list_add_int(p, "4.0", static_cast<int32_t>(ProfileLevel::L40));
	obs_property_list_add_int(p, "4.1", static_cast<int32_t>(ProfileLevel::L41));
	obs_property_list_add_int(p, "4.2", static_cast<int32_t>(ProfileLevel::L42));
	obs_property_list_add_int(p, "5.0", static_cast<int32_t>(ProfileLevel::L50));
	obs_property_list_add_int(p, "5.1", static_cast<int32_t>(ProfileLevel::L51));
	obs_property_list_add_int(p, "5.2", static_cast<int32_t>(ProfileLevel::L52));
#pragma endregion Profile, Levels

#pragma region Coding Type
	p = obs_properties_add_list(props, P_CODINGTYPE, P_TRANSLATE(P_CODINGTYPE), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_CODINGTYPE)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_AUTOMATIC), static_cast<int32_t>(CodingType::Automatic));
	obs_property_list_add_int(p, "CABAC", static_cast<int32_t>(CodingType::CABAC));
	obs_property_list_add_int(p, "CALVC", static_cast<int32_t>(CodingType::CALVC));
#pragma endregion Coding Type

#pragma region Maximum Reference Frames
	p = obs_properties_add_int_slider(props, P_MAXIMUMREFERENCEFRAMES, P_TRANSLATE(P_MAXIMUMREFERENCEFRAMES), 1, 16, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_MAXIMUMREFERENCEFRAMES)));
#pragma endregion Maximum Reference Frames

	// Rate Control
#pragma region Rate Control Method
	p = obs_properties_add_list(props, P_RATECONTROLMETHOD, P_TRANSLATE(P_RATECONTROLMETHOD), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_RATECONTROLMETHOD)));
	obs_property_list_add_int(p, P_TRANSLATE(P_RATECONTROLMETHOD_CQP),
							  static_cast<int32_t>(RateControlMethod::ConstantQP));
	obs_property_list_add_int(p, P_TRANSLATE(P_RATECONTROLMETHOD_CBR),
							  static_cast<int32_t>(RateControlMethod::ConstantBitrate));
	obs_property_list_add_int(p, P_TRANSLATE(P_RATECONTROLMETHOD_VBR),
							  static_cast<int32_t>(RateControlMethod::PeakConstrainedVariableBitrate));
	obs_property_list_add_int(p, P_TRANSLATE(P_RATECONTROLMETHOD_VBRLAT),
							  static_cast<int32_t>(RateControlMethod::LatencyConstrainedVariableBitrate));
	obs_property_set_modified_callback(p, properties_modified);
#pragma endregion Rate Control Method

#pragma region Pre - Pass
	p = obs_properties_add_list(props, P_PREPASSMODE, P_TRANSLATE(P_PREPASSMODE), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_PREPASSMODE)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), static_cast<int32_t>(PrePassMode::Disabled));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), static_cast<int32_t>(PrePassMode::Enabled));
#pragma endregion Pre - Pass

#pragma region Parameters
	/// Bitrate Constraints
	p = obs_properties_add_int(props, "bitrate", P_TRANSLATE(P_BITRATE_TARGET), 0, 1, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_BITRATE_TARGET)));
	p = obs_properties_add_int(props, P_BITRATE_PEAK, P_TRANSLATE(P_BITRATE_PEAK), 0, 1, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_BITRATE_PEAK)));

	/// Minimum QP, Maximum QP
	p = obs_properties_add_int_slider(props, P_QP_MINIMUM, P_TRANSLATE(P_QP_MINIMUM), 0, 51, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_QP_MINIMUM)));
	p = obs_properties_add_int_slider(props, P_QP_MAXIMUM, P_TRANSLATE(P_QP_MAXIMUM), 0, 51, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_QP_MAXIMUM)));

	/// Method: Constant QP
	p = obs_properties_add_int_slider(props, P_QP_IFRAME, P_TRANSLATE(P_QP_IFRAME), 0, 51, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_QP_IFRAME)));
	p = obs_properties_add_int_slider(props, P_QP_PFRAME, P_TRANSLATE(P_QP_PFRAME), 0, 51, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_QP_PFRAME)));
	p = obs_properties_add_int_slider(props, P_QP_BFRAME, P_TRANSLATE(P_QP_BFRAME), 0, 51, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_QP_BFRAME)));
#pragma endregion Parameters

#pragma region Filler Data
	p = obs_properties_add_list(props, P_FILLERDATA, P_TRANSLATE(P_FILLERDATA), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_FILLERDATA)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
#pragma endregion Filler Data

#pragma region Frame Skipping
	p = obs_properties_add_list(props, P_FRAMESKIPPING, P_TRANSLATE(P_FRAMESKIPPING), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_FRAMESKIPPING)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
	p = obs_properties_add_int(props, P_FRAMESKIPPING_PERIOD, P_TRANSLATE(P_FRAMESKIPPING_PERIOD), 0, 1000, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_FRAMESKIPPING_PERIOD)));
	p = obs_properties_add_list(props, P_FRAMESKIPPING_BEHAVIOUR, P_TRANSLATE(P_FRAMESKIPPING_BEHAVIOUR),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_FRAMESKIPPING_BEHAVIOUR)));
	obs_property_list_add_int(p, P_TRANSLATE(P_FRAMESKIPPING_SKIPNTH), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_FRAMESKIPPING_KEEPNTH), 1);
#pragma endregion Frame Skipping

#pragma region VBAQ
	p = obs_properties_add_list(props, P_VBAQ, P_TRANSLATE(P_VBAQ), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VBAQ)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
#pragma endregion VBAQ

#pragma region Enforce Hypothetical Reference Decoder Restrictions
	p = obs_properties_add_list(props, P_ENFORCEHRD, P_TRANSLATE(P_ENFORCEHRD), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_ENFORCEHRD)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
#pragma endregion Enforce Hypothetical Reference Decoder Restrictions

	{ // High Motion Quality Boost
		p = obs_properties_add_list(props, P_HIGHMOTIONQUALITYBOOST, P_TRANSLATE(P_HIGHMOTIONQUALITYBOOST),
									OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_HIGHMOTIONQUALITYBOOST)));
		obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_DEFAULT), -1);
		obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
		obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
	}

	// VBV Buffer
#pragma region VBV Buffer Mode
	p = obs_properties_add_list(props, P_VBVBUFFER, P_TRANSLATE(P_VBVBUFFER), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VBVBUFFER)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_AUTOMATIC), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_MANUAL), 1);
	obs_property_set_modified_callback(p, properties_modified);
#pragma endregion VBV Buffer Mode

#pragma region VBV Buffer Strictness
	p = obs_properties_add_float_slider(props, P_VBVBUFFER_STRICTNESS, P_TRANSLATE(P_VBVBUFFER_STRICTNESS), 0.0, 100.0,
										0.1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VBVBUFFER_STRICTNESS)));
#pragma endregion VBV Buffer Strictness

#pragma region VBV Buffer Size
	p = obs_properties_add_int_slider(props, P_VBVBUFFER_SIZE, P_TRANSLATE(P_VBVBUFFER_SIZE), 1, 1000000, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VBVBUFFER_SIZE)));
#pragma endregion VBV Buffer Size

#pragma region VBV Buffer Initial Fullness
	p = obs_properties_add_float_slider(props, P_VBVBUFFER_INITIALFULLNESS, P_TRANSLATE(P_VBVBUFFER_INITIALFULLNESS),
										0.0, 100.0, 100.0 / 64.0);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VBVBUFFER_INITIALFULLNESS)));
#pragma endregion VBV Buffer Initial Fullness

	// Picture Control
#pragma region Interval and Periods
	/// Keyframe, IDR
	p = obs_properties_add_float(props, P_INTERVAL_KEYFRAME, P_TRANSLATE(P_INTERVAL_KEYFRAME), 0, 100, 0.001);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_INTERVAL_KEYFRAME)));
	p = obs_properties_add_int(props, P_PERIOD_IDR_H264, P_TRANSLATE(P_PERIOD_IDR_H264), 0, 1000, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_PERIOD_IDR_H264)));
	/// I-Frame
	p = obs_properties_add_float(props, P_INTERVAL_IFRAME, P_TRANSLATE(P_INTERVAL_IFRAME), 0, 100, 0.001);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_INTERVAL_IFRAME)));
	p = obs_properties_add_int(props, P_PERIOD_IFRAME, P_TRANSLATE(P_PERIOD_IFRAME), 0, 1000, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_PERIOD_IFRAME)));
	/// P-Frame
	p = obs_properties_add_float(props, P_INTERVAL_PFRAME, P_TRANSLATE(P_INTERVAL_PFRAME), 0, 100, 0.001);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_INTERVAL_PFRAME)));
	p = obs_properties_add_int(props, P_PERIOD_PFRAME, P_TRANSLATE(P_PERIOD_PFRAME), 0, 1000, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_PERIOD_PFRAME)));
	/// B-Frame
	p = obs_properties_add_float(props, P_INTERVAL_BFRAME, P_TRANSLATE(P_INTERVAL_BFRAME), 0, 100, 0.001);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_INTERVAL_BFRAME)));
	p = obs_properties_add_int(props, P_PERIOD_BFRAME, P_TRANSLATE(P_PERIOD_BFRAME), 0, 1000, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_PERIOD_BFRAME)));
#pragma endregion Interval and Periods

#pragma region B - Frames Pattern
	p = obs_properties_add_int_slider(props, P_BFRAME_PATTERN, P_TRANSLATE(P_BFRAME_PATTERN), 0, 3, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_BFRAME_PATTERN)));
	obs_property_set_modified_callback(p, properties_modified);
#pragma endregion B - Frames Pattern

#pragma region B - Frames Reference
	p = obs_properties_add_list(props, P_BFRAME_REFERENCE, P_TRANSLATE(P_BFRAME_REFERENCE), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_BFRAME_REFERENCE)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
	obs_property_set_modified_callback(p, properties_modified);
#pragma endregion B - Frames Reference

#pragma region B - Frames Delta QP
	p = obs_properties_add_int_slider(props, P_BFRAME_REFERENCEDELTAQP, P_TRANSLATE(P_BFRAME_REFERENCEDELTAQP), -10, 10,
									  1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_BFRAME_REFERENCEDELTAQP)));

	p = obs_properties_add_int_slider(props, P_BFRAME_DELTAQP, P_TRANSLATE(P_BFRAME_DELTAQP), -10, 10, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_BFRAME_DELTAQP)));
#pragma endregion B - Frames Delta QP

#pragma region Deblocking Filter
	p = obs_properties_add_list(props, P_DEBLOCKINGFILTER, P_TRANSLATE(P_DEBLOCKINGFILTER), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_DEBLOCKINGFILTER)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
#pragma endregion Deblocking Filter

#pragma region Motion Estimation
	p = obs_properties_add_list(props, P_MOTIONESTIMATION, P_TRANSLATE(P_MOTIONESTIMATION), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_MOTIONESTIMATION)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_MOTIONESTIMATION_HALF), 1);
	obs_property_list_add_int(p, P_TRANSLATE(P_MOTIONESTIMATION_QUARTER), 2);
	obs_property_list_add_int(p, P_TRANSLATE(P_MOTIONESTIMATION_FULL), 3);
#pragma endregion Motion Estimation

	// System
#pragma region Video APIs
	p = obs_properties_add_list(props, P_VIDEO_API, P_TRANSLATE(P_VIDEO_API), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VIDEO_API)));
	obs_property_set_modified_callback(p, properties_modified);
	Utility::fill_api_list(p, Codec::AVC);
#pragma endregion Video APIs

#pragma region Video Adapters
	p = obs_properties_add_list(props, P_VIDEO_ADAPTER, P_TRANSLATE(P_VIDEO_ADAPTER), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VIDEO_ADAPTER)));
	obs_property_set_modified_callback(p, properties_modified);
#pragma endregion Video Adapters

#pragma region OpenCL
	p = obs_properties_add_list(props, P_OPENCL_TRANSFER, P_TRANSLATE(P_OPENCL_TRANSFER), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_OPENCL_TRANSFER)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);

	p = obs_properties_add_list(props, P_OPENCL_CONVERSION, P_TRANSLATE(P_OPENCL_CONVERSION), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_OPENCL_CONVERSION)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);
#pragma endregion OpenCL

#pragma region Asynchronous Queue
	p = obs_properties_add_list(props, P_MULTITHREADING, P_TRANSLATE(P_MULTITHREADING), OBS_COMBO_TYPE_LIST,
								OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_MULTITHREADING)));
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_DISABLED), 0);
	obs_property_list_add_int(p, P_TRANSLATE(P_UTIL_SWITCH_ENABLED), 1);

	p = obs_properties_add_int_slider(props, P_QUEUESIZE, P_TRANSLATE(P_QUEUESIZE), 1, 32, 1);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_QUEUESIZE)));
#pragma endregion Asynchronous Queue

#pragma region View Mode
	p = obs_properties_add_list(props, P_VIEW, P_TRANSLATE(P_VIEW), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_VIEW)));
	obs_property_list_add_int(p, P_TRANSLATE(P_VIEW_BASIC), static_cast<int32_t>(ViewMode::Basic));
	obs_property_list_add_int(p, P_TRANSLATE(P_VIEW_ADVANCED), static_cast<int32_t>(ViewMode::Advanced));
	obs_property_list_add_int(p, P_TRANSLATE(P_VIEW_EXPERT), static_cast<int32_t>(ViewMode::Expert));
	obs_property_list_add_int(p, P_TRANSLATE(P_VIEW_MASTER), static_cast<int32_t>(ViewMode::Master));
	obs_property_set_modified_callback(p, properties_modified);
#pragma endregion View Mode

	/// Debug
	p = obs_properties_add_bool(props, P_DEBUG, P_TRANSLATE(P_DEBUG));
	obs_property_set_long_description(p, P_TRANSLATE(P_DESC(P_DEBUG)));

	// Disable non-dynamic properties if we have an encoder.
	obs_properties_set_param(props, data, nullptr);

	return props;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in %s: %s", __FUNCTION_NAME__, ex.what());
	return nullptr;
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
	return nullptr;
}

static void obs_data_transfer_settings(obs_data_t* data)
{
#define TRANSFER_STRING(xold, xnew) obs_data_set_string(data, xnew, obs_data_get_string(data, xold))
#define TRANSFER_FLOAT(xold, xnew) obs_data_set_double(data, xnew, obs_data_get_double(data, xold))
#define TRANSFER_INT(xold, xnew) obs_data_set_int(data, xnew, obs_data_get_int(data, xold))
#define TRANSFER_BOOL(xold, xnew) obs_data_set_bool(data, xnew, obs_data_get_bool(data, xold))

	uint64_t version = obs_data_get_int(data, P_VERSION);
	switch (version) {
	case (((uint64_t)(1 & 0xFFFF) << 48ull) | ((uint64_t)(4 & 0xFFFF) << 32ull) | ((uint64_t)(3 & 0xFFFF) << 16ull)
		  | ((uint64_t)(5) & 0xFFFF)):
		obs_data_set_double(data, "AMF.H264.VBVBuffer.Strictness",
							obs_data_get_double(data, "AMF.H264.VBVBuffer.Strictness") + 50.0);
	case (((uint64_t)(1 & 0xFFFF) << 48ull) | ((uint64_t)(4 & 0xFFFF) << 32ull) | ((uint64_t)(3 & 0xFFFF) << 16ull)
		  | ((uint64_t)(8) & 0xFFFF)):
		TRANSFER_INT("AMF.H264.Preset", P_PRESET);

		// Static
		//TRANSFER_INT("AMF.H264.Usage", P_USAGE);
		TRANSFER_INT("AMF.H264.QualityPreset", P_QUALITYPRESET);
		TRANSFER_INT("AMF.H264.Profile", P_PROFILE);
		TRANSFER_INT("AMF.H264.ProfileLevel", P_PROFILELEVEL);
		TRANSFER_INT("AMF.H264.AspectRatio", P_ASPECTRATIO);
		TRANSFER_INT("AMF.H264.CodingType", P_CODINGTYPE);
		TRANSFER_INT("AMF.H264.MaximumReferenceFrames", P_MAXIMUMREFERENCEFRAMES);

		// Rate Control
		TRANSFER_INT("AMF.H264.RateControlMethod", P_RATECONTROLMETHOD);
		TRANSFER_INT("AMF.H264.PreAnalysisPass", P_PREPASSMODE);
		TRANSFER_INT("AMF.H264.Bitrate.Target", P_BITRATE_TARGET);
		TRANSFER_INT("AMF.H264.Bitrate.Peak", P_BITRATE_PEAK);
		TRANSFER_INT("AMF.H264.QP.IFrame", P_QP_IFRAME);
		TRANSFER_INT("AMF.H264.QP.PFrame", P_QP_PFRAME);
		TRANSFER_INT("AMF.H264.QP.BFrame", P_QP_BFRAME);
		TRANSFER_INT("AMF.H264.QP.Minimum", P_QP_MINIMUM);
		TRANSFER_INT("AMF.H264.QP.Maximum", P_QP_MAXIMUM);
		TRANSFER_INT("AMF.H264.FillerData", P_FILLERDATA);
		TRANSFER_INT("AMF.H264.FrameSkipping", P_FRAMESKIPPING);
		TRANSFER_INT("AMF.H264.VBAQ", P_VBAQ);
		TRANSFER_INT("AMF.H264.EnforceHRD", P_ENFORCEHRD);

		// VBV Buffer
		TRANSFER_INT("AMF.H264.VBVBuffer", P_VBVBUFFER);
		TRANSFER_FLOAT("AMF.H264.VBVBuffer.Strictness", P_VBVBUFFER_STRICTNESS);
		TRANSFER_INT("AMF.H264.VBVBuffer.Size", P_VBVBUFFER_SIZE);
		TRANSFER_FLOAT("AMF.H264.VBVBuffer.Fullness", P_VBVBUFFER_INITIALFULLNESS);

		// Picture Control
		TRANSFER_FLOAT("AMF.H264.KeyframeInterval", P_INTERVAL_KEYFRAME);
		TRANSFER_INT("AMF.H264.IDRPeriod", P_PERIOD_IDR_H264);
		/// GOP Type
		/// GOP Size
		/// GOP Alignment
		TRANSFER_INT("AMF.H264.BFrame.Pattern", P_BFRAME_PATTERN);
		TRANSFER_INT("AMF.H264.BFrame.DeltaQP", P_BFRAME_DELTAQP);
		TRANSFER_INT("AMF.H264.BFrame.Reference", P_BFRAME_REFERENCE);
		TRANSFER_INT("AMF.H264.BFrame.DeltaQP", P_BFRAME_REFERENCEDELTAQP);
		TRANSFER_INT("AMF.H264.DeblockingFilter", P_DEBLOCKINGFILTER);
		TRANSFER_INT("AMF.H264.MotionEstimation", P_MOTIONESTIMATION);

		// System
		TRANSFER_STRING("AMF.H264.VideoAPI", P_VIDEO_API);
		TRANSFER_INT("AMF.H264.VideoAdapter", P_VIDEO_ADAPTER);
		TRANSFER_INT("AMF.H264.OpenCL", P_OPENCL_TRANSFER);
		TRANSFER_INT("AMF.H264.OpenCL", P_OPENCL_CONVERSION);
		TRANSFER_INT("AMF.H264.View", P_VIEW);
		TRANSFER_INT("AMF.H264.Debug", P_DEBUG);
	case (((uint64_t)(2 & 0xFFFF) << 48ull) | ((uint64_t)(0 & 0xFFFF) << 32ull) | ((uint64_t)(0) & 0xFFFFFFFF)):
		TRANSFER_FLOAT("KeyframeInterval", P_INTERVAL_KEYFRAME);
		TRANSFER_INT("H264.IDRPeriod", P_PERIOD_IDR_H264);
		TRANSFER_INT("H265.IDRPeriod", P_PERIOD_IDR_H265);
	case (((uint64_t)(2 & 0xFFFF) << 48ull) | ((uint64_t)(3 & 0xFFFF) << 32ull) | ((uint64_t)(0) & 0xFFFFFFFF)):
		TRANSFER_INT(P_BITRATE_TARGET, "bitrate");
	}
	obs_data_set_int(data, P_VERSION, PLUGIN_VERSION_FULL);
}

bool Plugin::Interface::H264Interface::properties_modified(obs_properties_t* props, obs_property_t*,
														   obs_data_t*       data) noexcept
try {
	bool            result = false;
	obs_property_t* p;

	// Transfer settings from older Plugin versions to newer ones.
	obs_data_transfer_settings(data);

#pragma region Video API& Adapter
	// Video API
	const char *videoAPI_last = obs_data_get_string(data, ("last" P_VIDEO_API)),
			   *videoAPI_cur  = obs_data_get_string(data, P_VIDEO_API);
	if (strlen(videoAPI_cur) == 0) {
		p = obs_properties_get(props, P_VIDEO_API);
		obs_data_set_string(data, P_VIDEO_API, obs_property_list_item_string(p, 0));
		videoAPI_cur = obs_data_get_string(data, P_VIDEO_API);

		result = true;
	}
	/// If a different API was selected, rebuild the device list.
	if (strcmp(videoAPI_last, videoAPI_cur) != 0) {
		obs_data_set_string(data, ("last" P_VIDEO_API), videoAPI_cur);
		Utility::fill_device_list(obs_properties_get(props, P_VIDEO_ADAPTER), videoAPI_cur, Codec::AVC);
		result = true;

		// Reset Video Adapter to first in list.
		obs_data_set_int(data, P_VIDEO_ADAPTER,
						 obs_property_list_item_int(obs_properties_get(props, P_VIDEO_ADAPTER), 0));
	}

	// Video Adapter
	int64_t videoAdapter_last = obs_data_get_int(data, ("last" P_VIDEO_ADAPTER)),
			videoAdapter_cur  = obs_data_get_int(data, P_VIDEO_ADAPTER);
	if (videoAdapter_last != videoAdapter_cur) {
		obs_data_set_int(data, ("last" P_VIDEO_ADAPTER), videoAdapter_cur);
		result = true;

		auto api = Plugin::API::GetAPI(obs_data_get_string(data, P_VIDEO_API));
		union {
			int64_t  v;
			uint32_t id[2];
		} adapterid  = {videoAdapter_cur};
		auto adapter = api->GetAdapterById(adapterid.id[0], adapterid.id[1]);
		try {
			auto enc = EncoderH264(api, adapter);

#define TEMP_LIMIT_DROPDOWN(func, enm, prop)                                             \
	{                                                                                    \
		auto tmp_p = obs_properties_get(props, prop);                                    \
		auto tmp_l = enc.func();                                                         \
		enm  tmp_s = static_cast<enm>(obs_data_get_int(data, obs_property_name(tmp_p))); \
		for (size_t idx = 0; idx < obs_property_list_item_count(tmp_p); idx++) {         \
			bool enabled = false;                                                        \
			enm  tmp_v   = static_cast<enm>(obs_property_list_item_int(tmp_p, idx));     \
			for (auto tmp_k : tmp_l) {                                                   \
				if (tmp_k == tmp_v) {                                                    \
					enabled = true;                                                      \
					break;                                                               \
				}                                                                        \
			}                                                                            \
			obs_property_list_item_disable(tmp_p, idx, !enabled);                        \
			if ((enabled == false) && (tmp_s == tmp_v))                                  \
				obs_data_unset_user_value(data, obs_property_name(tmp_p));               \
		}                                                                                \
	}
#define TEMP_LIMIT_SLIDER(func, prop)                                               \
	{                                                                               \
		auto tmp_p = obs_properties_get(props, prop);                               \
		auto tmp_l = enc.func();                                                    \
		obs_property_int_set_limits(tmp_p, (int)tmp_l.first, (int)tmp_l.second, 1); \
	}
#define TEMP_LIMIT_SLIDER_BITRATE(func, prop)                                                     \
	{                                                                                             \
		auto tmp_p = obs_properties_get(props, prop);                                             \
		auto tmp_l = enc.func();                                                                  \
		obs_property_int_set_limits(tmp_p, (int)tmp_l.first / 1000, (int)tmp_l.second / 1000, 1); \
	}

			//TEMP_LIMIT_DROPDOWN(CapsUsage, AMD::Usage, P_USAGE);
			TEMP_LIMIT_DROPDOWN(CapsQualityPreset, AMD::QualityPreset, P_QUALITYPRESET);
			TEMP_LIMIT_DROPDOWN(CapsProfile, AMD::Profile, P_PROFILE);
			TEMP_LIMIT_DROPDOWN(CapsProfileLevel, AMD::ProfileLevel, P_PROFILELEVEL);
			{
				auto tmp_p = obs_properties_get(props, P_PROFILELEVEL);
				obs_property_list_item_disable(tmp_p, 0, false);
			}
			TEMP_LIMIT_DROPDOWN(CapsCodingType, AMD::CodingType, P_CODINGTYPE);
			TEMP_LIMIT_SLIDER(CapsMaximumReferenceFrames, P_MAXIMUMREFERENCEFRAMES);
			TEMP_LIMIT_DROPDOWN(CapsRateControlMethod, AMD::RateControlMethod, P_RATECONTROLMETHOD);
			try {
				TEMP_LIMIT_DROPDOWN(CapsPrePassMode, AMD::PrePassMode, P_PREPASSMODE);
				obs_property_set_enabled(obs_properties_get(props, P_PREPASSMODE), true);
			} catch (...) {
				obs_property_set_enabled(obs_properties_get(props, P_PREPASSMODE), false);
			}

			TEMP_LIMIT_SLIDER_BITRATE(CapsTargetBitrate, "bitrate");
			TEMP_LIMIT_SLIDER_BITRATE(CapsPeakBitrate, P_BITRATE_PEAK);
			TEMP_LIMIT_SLIDER_BITRATE(CapsVBVBufferSize, P_VBVBUFFER_SIZE);
			{
				auto bframep    = obs_properties_get(props, P_BFRAME_PATTERN);
				auto bframecaps = enc.CapsBFramePattern();
				obs_property_int_set_limits(bframep, 0, (int)bframecaps, 1);
				if (obs_data_get_int(data, obs_property_name(bframep)) > bframecaps) {
					obs_data_set_int(data, obs_property_name(bframep), bframecaps);
				}
			}
		} catch (const std::exception& e) {
			PLOG_ERROR("Exception occurred while updating capabilities: %s", e.what());
		}
	}
#pragma endregion Video API& Adapter

#pragma region Presets
	Presets lastPreset = static_cast<Presets>(obs_data_get_int(data, ("last" P_PRESET))),
			preset     = static_cast<Presets>(obs_data_get_int(data, P_PRESET));
	if (lastPreset != preset) { // Reset State
		obs_property_t* pn = obs_properties_first(props);
		do {
			obs_property_set_enabled(pn, true);
		} while (obs_property_next(&pn));

		result = true;
	}
	if (preset != Presets::None)
		result = true;

	switch (preset) {
	case Presets::ResetToDefaults:
#pragma region Default
	{
		obs_property_t* pn = obs_properties_first(props);
		do {
			const char* name = obs_property_name(pn);

			// Do not reset Video Adapter or API.
			if ((strcmp(name, P_VIDEO_API) == 0) || (strcmp(name, P_VIDEO_ADAPTER) == 0))
				continue;

			switch (obs_property_get_type(pn)) {
			case obs_property_type::OBS_PROPERTY_BOOL:
				obs_data_set_bool(data, name, obs_data_get_default_bool(data, name));
				break;
			case obs_property_type::OBS_PROPERTY_FLOAT:
				obs_data_set_double(data, name, obs_data_get_default_double(data, name));
				break;
			case obs_property_type::OBS_PROPERTY_INT:
				obs_data_set_int(data, name, obs_data_get_default_int(data, name));
				break;
			case obs_property_type::OBS_PROPERTY_TEXT:
				obs_data_set_string(data, name, obs_data_get_default_string(data, name));
				break;
			case obs_property_type::OBS_PROPERTY_LIST:
				switch (obs_property_list_format(pn)) {
				case obs_combo_format::OBS_COMBO_FORMAT_INT:
					obs_data_set_int(data, name, obs_data_get_default_int(data, name));
					break;
				case obs_combo_format::OBS_COMBO_FORMAT_FLOAT:
					obs_data_set_double(data, name, obs_data_get_default_double(data, name));
					break;
				case obs_combo_format::OBS_COMBO_FORMAT_STRING:
					obs_data_set_string(data, name, obs_data_get_default_string(data, name));
					break;
				}
				break;
			}
			obs_property_set_enabled(pn, true);
		} while (obs_property_next(&pn));
	} break;
#pragma endregion Default
	case Presets::Recording:
#pragma region Recording
		// Static Properties
		//obs_data_set_int(data, P_USAGE, static_cast<int32_t>(Usage::Transcoding));
		//obs_property_set_enabled(obs_properties_get(props, P_USAGE), false);
		obs_data_set_int(data, P_PROFILE, static_cast<int32_t>(Profile::High));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILE), false);
		obs_data_set_int(data, P_PROFILELEVEL, static_cast<int32_t>(ProfileLevel::Automatic));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILELEVEL), false);

		// Rate Control Properties
		obs_data_set_int(data, P_RATECONTROLMETHOD,
						 static_cast<int32_t>(RateControlMethod::LatencyConstrainedVariableBitrate));
		obs_property_set_enabled(obs_properties_get(props, P_RATECONTROLMETHOD), false);
		if (obs_data_get_int(data, "bitrate") < 10000)
			obs_data_set_int(data, "bitrate", 10000);
		obs_data_unset_user_value(data, P_QP_MINIMUM);
		obs_property_set_enabled(obs_properties_get(props, P_QP_MINIMUM), false);
		obs_data_unset_user_value(data, P_QP_MAXIMUM);
		obs_property_set_enabled(obs_properties_get(props, P_QP_MAXIMUM), false);
		obs_data_set_int(data, P_FILLERDATA, 0);
		obs_property_set_enabled(obs_properties_get(props, P_FILLERDATA), false);

		// Frame Control Properties
		if (obs_data_get_double(data, P_INTERVAL_KEYFRAME) < 2)
			obs_data_set_double(data, P_INTERVAL_KEYFRAME, 2);

		// Miscellaneous Properties
		obs_data_set_int(data, P_MOTIONESTIMATION, 3);
		obs_property_set_enabled(obs_properties_get(props, P_MOTIONESTIMATION), false);
		break;
#pragma endregion Recording
	case Presets::HighQuality:
#pragma region High Quality
		// Static Properties
		//obs_data_set_int(data, P_USAGE, static_cast<int32_t>(Usage::Transcoding));
		//obs_property_set_enabled(obs_properties_get(props, P_USAGE), false);
		obs_data_set_int(data, P_PROFILE, static_cast<int32_t>(Profile::High));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILE), false);
		obs_data_set_int(data, P_PROFILELEVEL, static_cast<int32_t>(ProfileLevel::Automatic));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILELEVEL), false);

		// Rate Control Properties
		obs_data_set_int(data, P_RATECONTROLMETHOD, static_cast<int32_t>(RateControlMethod::ConstantQP));
		obs_property_set_enabled(obs_properties_get(props, P_RATECONTROLMETHOD), false);
		obs_data_set_int(data, P_QP_IFRAME, 18);
		obs_property_set_enabled(obs_properties_get(props, P_QP_IFRAME), false);
		obs_data_set_int(data, P_QP_PFRAME, 18);
		obs_property_set_enabled(obs_properties_get(props, P_QP_PFRAME), false);
		obs_data_set_int(data, P_QP_BFRAME, 18);
		obs_property_set_enabled(obs_properties_get(props, P_QP_BFRAME), false);

		// Frame Control Properties
		if (obs_data_get_double(data, P_INTERVAL_KEYFRAME) < 2)
			obs_data_set_double(data, P_INTERVAL_KEYFRAME, 2);

		// Miscellaneous Properties
		obs_data_set_int(data, P_MOTIONESTIMATION, 3);
		obs_property_set_enabled(obs_properties_get(props, P_MOTIONESTIMATION), false);
		break;
#pragma endregion High Quality
	case Presets::Indistinguishable:
#pragma region Indistinguishable
		// Static Properties
		//obs_data_set_int(data, P_USAGE, static_cast<int32_t>(Usage::Transcoding));
		//obs_property_set_enabled(obs_properties_get(props, P_USAGE), false);
		obs_data_set_int(data, P_PROFILE, static_cast<int32_t>(Profile::High));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILE), false);
		obs_data_set_int(data, P_PROFILELEVEL, static_cast<int32_t>(ProfileLevel::Automatic));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILELEVEL), false);

		// Rate Control Properties
		obs_data_set_int(data, P_RATECONTROLMETHOD, static_cast<int32_t>(RateControlMethod::ConstantQP));
		obs_property_set_enabled(obs_properties_get(props, P_RATECONTROLMETHOD), false);
		obs_data_set_int(data, P_QP_IFRAME, 15);
		obs_property_set_enabled(obs_properties_get(props, P_QP_IFRAME), false);
		obs_data_set_int(data, P_QP_PFRAME, 15);
		obs_property_set_enabled(obs_properties_get(props, P_QP_PFRAME), false);
		obs_data_set_int(data, P_QP_BFRAME, 15);
		obs_property_set_enabled(obs_properties_get(props, P_QP_BFRAME), false);

		// Frame Control Properties
		if (obs_data_get_double(data, P_INTERVAL_KEYFRAME) < 2)
			obs_data_set_double(data, P_INTERVAL_KEYFRAME, 2);

		// Miscellaneous Properties
		obs_data_set_int(data, P_MOTIONESTIMATION, 3);
		obs_property_set_enabled(obs_properties_get(props, P_MOTIONESTIMATION), false);
		break;
#pragma endregion Indistinguishable
	case Presets::Lossless:
#pragma region Lossless
		// Static Properties
		//obs_data_set_int(data, P_USAGE, static_cast<int32_t>(Usage::Transcoding));
		//obs_property_set_enabled(obs_properties_get(props, P_USAGE), false);
		obs_data_set_int(data, P_PROFILE, static_cast<int32_t>(Profile::High));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILE), false);
		obs_data_set_int(data, P_PROFILELEVEL, static_cast<int32_t>(ProfileLevel::Automatic));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILELEVEL), false);

		// Rate Control Properties
		obs_data_set_int(data, P_RATECONTROLMETHOD, static_cast<int32_t>(RateControlMethod::ConstantQP));
		obs_property_set_enabled(obs_properties_get(props, P_RATECONTROLMETHOD), false);
		obs_data_set_int(data, P_QP_IFRAME, 0);
		obs_property_set_enabled(obs_properties_get(props, P_QP_IFRAME), false);
		obs_data_set_int(data, P_QP_PFRAME, 0);
		obs_property_set_enabled(obs_properties_get(props, P_QP_PFRAME), false);

		// Frame Control Properties
		if (obs_data_get_double(data, P_INTERVAL_KEYFRAME) < 2)
			obs_data_set_double(data, P_INTERVAL_KEYFRAME, 2);
		//obs_data_set_double(data, P_INTERVAL_KEYFRAME, 2);
		//obs_property_set_enabled(obs_properties_get(props, P_INTERVAL_KEYFRAME), true);
		obs_data_set_int(data, P_BFRAME_PATTERN, 0);
		obs_property_set_enabled(obs_properties_get(props, P_BFRAME_PATTERN), false);

		// Miscellaneous Properties
		obs_data_set_int(data, P_MOTIONESTIMATION, 3);
		obs_property_set_enabled(obs_properties_get(props, P_MOTIONESTIMATION), false);
		break;
#pragma endregion Lossless
	case Presets::Twitch:
#pragma region Twitch
		// Static Properties
		//obs_data_set_int(data, P_USAGE, static_cast<int32_t>(Usage::Transcoding));
		//obs_property_set_enabled(obs_properties_get(props, P_USAGE), false);
		obs_data_set_int(data, P_PROFILE, static_cast<int32_t>(Profile::Main));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILE), false);
		obs_data_set_int(data, P_PROFILELEVEL, static_cast<int32_t>(ProfileLevel::Automatic));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILELEVEL), false);

		// Rate Control Properties
		obs_data_set_int(data, P_RATECONTROLMETHOD, static_cast<int32_t>(RateControlMethod::ConstantBitrate));
		obs_property_set_enabled(obs_properties_get(props, P_RATECONTROLMETHOD), false);
		if (obs_data_get_int(data, "bitrate") < 500)
			obs_data_set_int(data, "bitrate", 500);
		obs_data_unset_user_value(data, P_QP_MINIMUM);
		obs_property_set_enabled(obs_properties_get(props, P_QP_MINIMUM), false);
		obs_data_unset_user_value(data, P_QP_MAXIMUM);
		obs_property_set_enabled(obs_properties_get(props, P_QP_MAXIMUM), false);
		obs_data_set_int(data, P_FILLERDATA, 1);
		obs_property_set_enabled(obs_properties_get(props, P_FILLERDATA), false);

		// Frame Control Properties
		obs_data_set_double(data, P_INTERVAL_KEYFRAME, 2);
		obs_property_set_enabled(obs_properties_get(props, P_INTERVAL_KEYFRAME), false);

		// Miscellaneous Properties
		obs_data_set_int(data, P_MOTIONESTIMATION, 3);
		obs_property_set_enabled(obs_properties_get(props, P_MOTIONESTIMATION), false);
		break;
#pragma endregion Twitch
	case Presets::YouTube:
#pragma region YouTube
		// Static Properties
		//obs_data_set_int(data, P_USAGE, static_cast<int32_t>(Usage::Transcoding));
		//obs_property_set_enabled(obs_properties_get(props, P_USAGE), false);
		obs_data_set_int(data, P_PROFILE, static_cast<int32_t>(Profile::Main));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILE), false);
		obs_data_set_int(data, P_PROFILELEVEL, static_cast<int32_t>(ProfileLevel::Automatic));
		obs_property_set_enabled(obs_properties_get(props, P_PROFILELEVEL), false);

		// Rate Control Properties
		obs_data_set_int(data, P_RATECONTROLMETHOD, static_cast<int32_t>(RateControlMethod::ConstantBitrate));
		obs_property_set_enabled(obs_properties_get(props, P_RATECONTROLMETHOD), false);
		if (obs_data_get_int(data, "bitrate") < 500)
			obs_data_set_int(data, "bitrate", 500);
		obs_data_unset_user_value(data, P_QP_MINIMUM);
		obs_property_set_enabled(obs_properties_get(props, P_QP_MINIMUM), false);
		obs_data_unset_user_value(data, P_QP_MAXIMUM);
		obs_property_set_enabled(obs_properties_get(props, P_QP_MAXIMUM), false);
		obs_data_set_int(data, P_FILLERDATA, 1);
		obs_property_set_enabled(obs_properties_get(props, P_FILLERDATA), false);

		// Frame Control Properties
		obs_data_set_double(data, P_INTERVAL_KEYFRAME, 2);
		obs_property_set_enabled(obs_properties_get(props, P_INTERVAL_KEYFRAME), false);

		// Miscellaneous Properties
		obs_data_set_int(data, P_MOTIONESTIMATION, 3);
		obs_property_set_enabled(obs_properties_get(props, P_MOTIONESTIMATION), false);
		break;
#pragma endregion YouTube
	}
#pragma endregion Presets

#pragma region View Mode
	ViewMode lastView = static_cast<ViewMode>(obs_data_get_int(data, ("last" P_VIEW))),
			 curView  = static_cast<ViewMode>(obs_data_get_int(data, P_VIEW));
	if (lastView != curView) {
		obs_data_set_int(data, ("last" P_VIEW), static_cast<int32_t>(curView));
		result = true;
	}

	std::vector<std::pair<const char*, ViewMode>> viewstuff = {
		std::make_pair(P_PRESET, ViewMode::Basic),
		// ----------- Static Section
		//std::make_pair(P_USAGE, ViewMode::Master),
		std::make_pair(P_QUALITYPRESET, ViewMode::Basic),
		std::make_pair(P_PROFILE, ViewMode::Advanced),
		std::make_pair(P_PROFILELEVEL, ViewMode::Advanced),
		std::make_pair(P_ASPECTRATIO, ViewMode::Master),
		std::make_pair(P_CODINGTYPE, ViewMode::Expert),
		std::make_pair(P_MAXIMUMREFERENCEFRAMES, ViewMode::Expert),
		// ----------- Rate Control Section
		std::make_pair(P_RATECONTROLMETHOD, ViewMode::Basic),
		//std::make_pair(P_PREPASSMODE, ViewMode::Basic),
		//std::make_pair(P_BITRATE_TARGET, ViewMode::Basic),
		//std::make_pair(P_BITRATE_PEAK, ViewMode::Basic),
		//std::make_pair(P_QP_IFRAME, ViewMode::Basic),
		//std::make_pair(P_QP_PFRAME, ViewMode::Basic),
		//std::make_pair(P_QP_BFRAME, ViewMode::Basic),
		//std::make_pair(P_QP_MINIMUM, ViewMode::Advanced),
		//std::make_pair(P_QP_MAXIMUM, ViewMode::Advanced),
		//std::make_pair(P_FILLERDATA, ViewMode::Basic),
		std::make_pair(P_FRAMESKIPPING, ViewMode::Advanced),
		std::make_pair(P_FRAMESKIPPING_PERIOD, ViewMode::Master),
		std::make_pair(P_FRAMESKIPPING_BEHAVIOUR, ViewMode::Master),
		//std::make_pair(P_VBAQ, ViewMode::Expert),
		std::make_pair(P_ENFORCEHRD, ViewMode::Expert),
		std::make_pair(P_HIGHMOTIONQUALITYBOOST, ViewMode::Advanced),
		// ----------- VBV Buffer
		std::make_pair(P_VBVBUFFER, ViewMode::Advanced),
		//std::make_pair(P_VBVBUFFER_STRICTNESS, ViewMode::Advanced),
		//std::make_pair(P_VBVBUFFER_SIZE, ViewMode::Advanced),
		std::make_pair(P_VBVBUFFER_INITIALFULLNESS, ViewMode::Expert),
		// ----------- Picture Control
		std::make_pair(P_INTERVAL_KEYFRAME, ViewMode::Basic),
		std::make_pair(P_PERIOD_IDR_H264, ViewMode::Master),
		std::make_pair(P_INTERVAL_IFRAME, ViewMode::Master),
		std::make_pair(P_PERIOD_IFRAME, ViewMode::Master),
		std::make_pair(P_INTERVAL_PFRAME, ViewMode::Master),
		std::make_pair(P_PERIOD_PFRAME, ViewMode::Master),
		//std::make_pair(P_INTERVAL_BFRAME, ViewMode::Master),
		//std::make_pair(P_PERIOD_BFRAME, ViewMode::Master),
		std::make_pair(P_BFRAME_PATTERN, ViewMode::Advanced),
		std::make_pair(P_BFRAME_DELTAQP, ViewMode::Advanced),
		std::make_pair(P_BFRAME_REFERENCE, ViewMode::Advanced),
		std::make_pair(P_BFRAME_REFERENCEDELTAQP, ViewMode::Advanced),
		std::make_pair(P_DEBLOCKINGFILTER, ViewMode::Expert),
		std::make_pair(P_MOTIONESTIMATION, ViewMode::Expert),
		// ----------- Intra-Refresh
		//std::make_pair("", ViewMode::Master),
		// ----------- System
		std::make_pair(P_VIDEO_API, ViewMode::Advanced),
		std::make_pair(P_VIDEO_ADAPTER, ViewMode::Advanced),
		std::make_pair(P_OPENCL_TRANSFER, ViewMode::Advanced),
		std::make_pair(P_OPENCL_CONVERSION, ViewMode::Advanced),
		std::make_pair(P_MULTITHREADING, ViewMode::Expert),
		std::make_pair(P_QUEUESIZE, ViewMode::Expert),
		std::make_pair(P_VIEW, ViewMode::Basic),
		std::make_pair(P_DEBUG, ViewMode::Basic),
	};
	for (std::pair<const char*, ViewMode> kv : viewstuff) {
		bool vis  = curView >= kv.second;
		auto visp = obs_properties_get(props, kv.first);
		if (visp != nullptr) {
			obs_property_set_visible(visp, vis);
			if (!vis)
				obs_data_unset_user_value(data, kv.first);
		}
	}

	// Special Logic
#pragma region B - Frames
	auto bframeProperty  = obs_properties_get(props, P_BFRAME_PATTERN);
	bool bframeSupported = obs_property_int_max(bframeProperty) > 0;
	bool bframeVisible   = (curView >= ViewMode::Advanced) && bframeSupported;

	/// Pattern
	obs_property_set_visible(bframeProperty, (curView >= ViewMode::Advanced) && bframeSupported);
	if (!bframeVisible)
		obs_data_unset_user_value(data, P_BFRAME_PATTERN);
	bool lastUsingBFrames = obs_data_get_int(data, ("last" P_BFRAME_PATTERN)) != 0,
		 usingBFrames     = obs_data_get_int(data, P_BFRAME_PATTERN) != 0;
	if (usingBFrames != lastUsingBFrames) {
		obs_data_set_int(data, ("last" P_BFRAME_PATTERN), obs_data_get_int(data, P_BFRAME_PATTERN));
		result = true;
	}

	/// Reference
	bool bframeReferenceVisible = (curView >= ViewMode::Advanced) && bframeSupported && usingBFrames;
	obs_property_set_visible(obs_properties_get(props, P_BFRAME_REFERENCE), bframeReferenceVisible);
	if (!bframeReferenceVisible)
		obs_data_unset_user_value(data, P_BFRAME_REFERENCE);
	bool lastUsingBFrameReference = obs_data_get_int(data, ("last" P_BFRAME_REFERENCE)) != 0,
		 usingBFrameReference     = obs_data_get_int(data, P_BFRAME_REFERENCE) == 1;
	if (usingBFrameReference != lastUsingBFrameReference) {
		obs_data_set_int(data, ("last" P_BFRAME_REFERENCE), obs_data_get_int(data, P_BFRAME_REFERENCE));
		result = true;
	}

	/// QP Delta
	obs_property_set_visible(obs_properties_get(props, P_BFRAME_DELTAQP), bframeVisible && usingBFrames);
	if (!bframeVisible || !usingBFrames)
		obs_data_unset_user_value(data, P_BFRAME_DELTAQP);
	obs_property_set_visible(obs_properties_get(props, P_BFRAME_REFERENCEDELTAQP),
							 bframeVisible && usingBFrames && usingBFrameReference);
	if (!bframeVisible || !usingBFrames || !usingBFrameReference)
		obs_data_unset_user_value(data, P_BFRAME_REFERENCEDELTAQP);
#pragma endregion B - Frames

#pragma region Rate Control
	bool vis_rcm_bitrate_target = false, vis_rcm_bitrate_peak = false, vis_rcm_qp = false, vis_rcm_qp_b = false,
		 vis_rcm_fillerdata = false;

	RateControlMethod lastRCM = static_cast<RateControlMethod>(obs_data_get_int(data, ("last" P_RATECONTROLMETHOD))),
					  curRCM  = static_cast<RateControlMethod>(obs_data_get_int(data, P_RATECONTROLMETHOD));
	if (lastRCM != curRCM) {
		obs_data_set_int(data, ("last" P_RATECONTROLMETHOD), static_cast<int32_t>(curRCM));
		result = true;
	}
	switch (curRCM) {
	case RateControlMethod::ConstantQP:
		vis_rcm_qp   = true;
		vis_rcm_qp_b = bframeSupported && usingBFrames;
		break;
	case RateControlMethod::ConstantBitrate:
		vis_rcm_bitrate_target = true;
		vis_rcm_fillerdata     = true;
		break;
	case RateControlMethod::PeakConstrainedVariableBitrate:
		vis_rcm_bitrate_target = true;
		vis_rcm_bitrate_peak   = true;
		break;
	case RateControlMethod::LatencyConstrainedVariableBitrate:
		vis_rcm_bitrate_target = true;
		vis_rcm_bitrate_peak   = true;
		break;
	}

	/// Bitrate
	obs_property_set_visible(obs_properties_get(props, "bitrate"), vis_rcm_bitrate_target);
	if (!vis_rcm_bitrate_target)
		obs_data_unset_user_value(data, "bitrate");
	obs_property_set_visible(obs_properties_get(props, P_BITRATE_PEAK), vis_rcm_bitrate_peak);
	if (!vis_rcm_bitrate_peak)
		obs_data_unset_user_value(data, P_BITRATE_PEAK);

	/// QP
	obs_property_set_visible(obs_properties_get(props, P_QP_IFRAME), vis_rcm_qp);
	obs_property_set_visible(obs_properties_get(props, P_QP_PFRAME), vis_rcm_qp);
	if (!vis_rcm_qp) {
		obs_data_unset_user_value(data, P_QP_IFRAME);
		obs_data_unset_user_value(data, P_QP_PFRAME);
	}
	obs_property_set_visible(obs_properties_get(props, P_QP_BFRAME), vis_rcm_qp_b);
	if (!vis_rcm_qp_b)
		obs_data_unset_user_value(data, P_QP_BFRAME);

	/// QP Min/Max
	obs_property_set_visible(obs_properties_get(props, P_QP_MINIMUM), (curView >= ViewMode::Advanced) && !vis_rcm_qp);
	obs_property_set_visible(obs_properties_get(props, P_QP_MAXIMUM), (curView >= ViewMode::Advanced) && !vis_rcm_qp);
	if (!(curView >= ViewMode::Advanced) || vis_rcm_qp) {
		obs_data_unset_user_value(data, P_QP_MINIMUM);
		obs_data_unset_user_value(data, P_QP_MAXIMUM);
	}

	/// Pre-Pass
	obs_property_set_visible(obs_properties_get(props, P_PREPASSMODE), (curView >= ViewMode::Basic) && !vis_rcm_qp);
	if (!(curView >= ViewMode::Basic) || vis_rcm_qp)
		obs_data_unset_user_value(data, P_PREPASSMODE);

	/// VBAQ
	obs_property_set_visible(obs_properties_get(props, P_VBAQ), (curView >= ViewMode::Expert) && !vis_rcm_qp);
	if (!(curView >= ViewMode::Expert) || vis_rcm_qp)
		obs_data_unset_user_value(data, P_VBAQ);

	/// Filler Data (CBR only at the moment)
	obs_property_set_visible(obs_properties_get(props, P_FILLERDATA), vis_rcm_fillerdata);
	if (!vis_rcm_fillerdata)
		obs_data_unset_user_value(data, P_FILLERDATA);
#pragma endregion Rate Control

#pragma region VBV Buffer
	uint32_t vbvBufferMode    = static_cast<uint32_t>(obs_data_get_int(data, P_VBVBUFFER));
	bool     vbvBufferVisible = (curView >= ViewMode::Advanced);

	uint32_t lastVBVBufferMode = static_cast<uint32_t>(obs_data_get_int(data, ("last" P_VBVBUFFER)));
	if (lastVBVBufferMode != vbvBufferMode) {
		obs_data_set_int(data, ("last" P_VBVBUFFER), vbvBufferMode);
		result = true;
	}

	obs_property_set_visible(obs_properties_get(props, P_VBVBUFFER_STRICTNESS),
							 vbvBufferVisible && (vbvBufferMode == 0));
	obs_property_set_visible(obs_properties_get(props, P_VBVBUFFER_SIZE), vbvBufferVisible && (vbvBufferMode == 1));
	if (!vbvBufferVisible || vbvBufferMode == 0)
		obs_data_unset_user_value(data, P_VBVBUFFER_SIZE);
	if (!vbvBufferVisible || vbvBufferMode == 1)
		obs_data_unset_user_value(data, P_VBVBUFFER_STRICTNESS);
#pragma endregion VBV Buffer

#pragma region B - Frame Interval
	bool bframeIntervalVisible = bframeSupported && (curView >= ViewMode::Master);
	obs_property_set_visible(obs_properties_get(props, P_PERIOD_BFRAME), bframeIntervalVisible);
	obs_property_set_visible(obs_properties_get(props, P_INTERVAL_BFRAME), bframeIntervalVisible);
	if (!bframeIntervalVisible) {
		obs_data_unset_user_value(data, P_PERIOD_BFRAME);
		obs_data_unset_user_value(data, P_INTERVAL_BFRAME);
	}
#pragma endregion B - Frame Interval
#pragma endregion View Mode

	// Permanently disable static properties while encoding.
	void* enc = obs_properties_get_param(props);
	if (enc) {
		std::vector<const char*> hiddenProperties = {
			// Static
			///P_USAGE,
			P_QUALITYPRESET,
			P_PROFILE,
			P_PROFILELEVEL,
			P_CODINGTYPE,
			P_MAXIMUMREFERENCEFRAMES,

			P_BFRAME_PATTERN,
			P_BFRAME_REFERENCE,

			// Dynamic
			/// Rate Control
			//P_RATECONTROLMETHOD,
			//P_PREPASSMODE,
			//P_FRAMESKIPPING,
			//P_FRAMESKIPPING_PERIOD,
			//P_FRAMESKIPPING_BEHAVIOUR,
			//P_DEBLOCKINGFILTER,

			//P_VBVBUFFER,
			//P_VBVBUFFER_STRICTNESS,
			//P_VBVBUFFER_SIZE,
			//P_VBVBUFFER_INITIALFULLNESS,
			//P_VBAQ,

			//// Picture Control
			//P_INTERVAL_KEYFRAME,
			//P_PERIOD_IDR_H264,
			//P_INTERVAL_IFRAME,
			//P_PERIOD_IFRAME,
			//P_INTERVAL_PFRAME,
			//P_PERIOD_PFRAME,
			//P_INTERVAL_BFRAME,
			//P_PERIOD_BFRAME,

			//P_BFRAME_PATTERN,
			//P_BFRAME_REFERENCE,

			// System
			P_VIDEO_API,
			P_VIDEO_ADAPTER,
			P_OPENCL_TRANSFER,
			P_OPENCL_CONVERSION,
			P_MULTITHREADING,
			P_QUEUESIZE,
			P_DEBUG,
		};
		for (const char* pr : hiddenProperties) {
			obs_property_set_enabled(obs_properties_get(props, pr), false);
		}
	}

	return result;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in %s: %s", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
	return false;
}

bool Plugin::Interface::H264Interface::update(void* data, obs_data_t* settings) noexcept
try {
	if (data)
		return static_cast<Plugin::Interface::H264Interface*>(data)->update(settings);
	return false;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in %s: %s", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
	return false;
}

void Plugin::Interface::H264Interface::get_video_info(void* data, struct video_scale_info* info) noexcept
try {
	if (data)
		return static_cast<Plugin::Interface::H264Interface*>(data)->get_video_info(info);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in %s: %s", __FUNCTION_NAME__, ex.what());
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
}

bool Plugin::Interface::H264Interface::get_extra_data(void* data, uint8_t** extra_data, size_t* size) noexcept
try {
	if (data)
		return static_cast<Plugin::Interface::H264Interface*>(data)->get_extra_data(extra_data, size);
	return false;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in %s: %s", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected unknown exception in %s.", __FUNCTION_NAME__);
	return false;
}

//////////////////////////////////////////////////////////////////////////
// Module Code
//////////////////////////////////////////////////////////////////////////
Plugin::Interface::H264Interface::H264Interface(obs_data_t* data, obs_encoder_t* encoder)
{
	PLOG_DEBUG("<%s> Initializing...", __FUNCTION_NAME__);

	m_Encoder = encoder;

	// OBS Settings
	uint32_t                        obsWidth     = obs_encoder_get_width(encoder);
	uint32_t                        obsHeight    = obs_encoder_get_height(encoder);
	video_t*                        obsVideoInfo = obs_encoder_video(encoder);
	const struct video_output_info* voi          = video_output_get_info(obsVideoInfo);
	uint32_t                        obsFPSnum    = voi->fps_num;
	uint32_t                        obsFPSden    = voi->fps_den;

	//////////////////////////////////////////////////////////////////////////
	/// Initialize Encoder
	bool debug = obs_data_get_bool(data, P_DEBUG);
	Plugin::AMD::AMF::Instance()->EnableDebugTrace(debug);

	ColorFormat colorFormat = ColorFormat::NV12;
	switch (voi->format) {
	case VIDEO_FORMAT_NV12:
		colorFormat = ColorFormat::NV12;
		break;
	case VIDEO_FORMAT_I420:
		colorFormat = ColorFormat::I420;
		break;
	case VIDEO_FORMAT_YUY2:
		colorFormat = ColorFormat::YUY2;
		break;
	case VIDEO_FORMAT_RGBA:
		colorFormat = ColorFormat::RGBA;
		break;
	case VIDEO_FORMAT_BGRA:
		colorFormat = ColorFormat::BGRA;
		break;
	case VIDEO_FORMAT_Y800:
		colorFormat = ColorFormat::GRAY;
		break;
	}
	ColorSpace colorSpace = ColorSpace::BT601;
	switch (voi->colorspace) {
	case VIDEO_CS_601:
		colorSpace = ColorSpace::BT601;
		break;
	case VIDEO_CS_DEFAULT:
	case VIDEO_CS_709:
		colorSpace = ColorSpace::BT709;
		break;
	case VIDEO_CS_SRGB:
		colorSpace = ColorSpace::SRGB;
		break;
	}

	auto api = API::GetAPI(obs_data_get_string(data, P_VIDEO_API));
	union {
		int64_t  v;
		uint32_t id[2];
	} adapterid  = {obs_data_get_int(data, P_VIDEO_ADAPTER)};
	auto adapter = api->GetAdapterById(adapterid.id[0], adapterid.id[1]);

	m_VideoEncoder = std::make_unique<EncoderH264>(
		api, adapter, !!obs_data_get_int(data, P_OPENCL_TRANSFER), !!obs_data_get_int(data, P_OPENCL_CONVERSION),
		colorFormat, colorSpace, voi->range == VIDEO_RANGE_FULL, !!obs_data_get_int(data, P_MULTITHREADING),
		(size_t)obs_data_get_int(data, P_QUEUESIZE));

	/// Static Properties
	m_VideoEncoder->SetUsage(Plugin::AMD::Usage::Transcoding);
	m_VideoEncoder->SetQualityPreset(static_cast<QualityPreset>(obs_data_get_int(data, P_QUALITYPRESET)));

	/// Frame
	m_VideoEncoder->SetResolution(std::make_pair(obsWidth, obsHeight));
	m_VideoEncoder->SetFrameRate(std::make_pair(obsFPSnum, obsFPSden));

	/// Profile & Level
	m_VideoEncoder->SetProfile(static_cast<Profile>(obs_data_get_int(data, P_PROFILE)));
	m_VideoEncoder->SetProfileLevel(static_cast<ProfileLevel>(obs_data_get_int(data, P_PROFILELEVEL)),
									std::make_pair(obsWidth, obsHeight), std::make_pair(obsFPSnum, obsFPSden));

	try {
		m_VideoEncoder->SetCodingType(static_cast<CodingType>(obs_data_get_int(data, P_CODINGTYPE)));
	} catch (...) {
	}
	try {
		m_VideoEncoder->SetMaximumReferenceFrames(obs_data_get_int(data, P_MAXIMUMREFERENCEFRAMES));
	} catch (...) {
	}

	// OBS - Enforce Streaming Service Restrictions
#pragma region OBS - Enforce Streaming Service Restrictions
	{
		// Profile
		const char* p_str = obs_data_get_string(data, "profile");
		if (strcmp(p_str, "") != 0) {
			if (strcmp(p_str, "constrained_baseline") == 0) {
				m_VideoEncoder->SetProfile(Profile::ConstrainedBaseline);
			} else if (strcmp(p_str, "baseline") == 0) {
				m_VideoEncoder->SetProfile(Profile::Baseline);
			} else if (strcmp(p_str, "main") == 0) {
				m_VideoEncoder->SetProfile(Profile::Main);
			} else if (strcmp(p_str, "constrained_high") == 0) {
				m_VideoEncoder->SetProfile(Profile::ConstrainedHigh);
			} else if (strcmp(p_str, "high") == 0) {
				m_VideoEncoder->SetProfile(Profile::High);
			}
			obs_data_unset_user_value(data, "profile");
		}

		// Preset
		const char* preset = obs_data_get_string(data, "preset");
		if (strcmp(preset, "") != 0) {
			if (strcmp(preset, "speed") == 0) {
				m_VideoEncoder->SetQualityPreset(QualityPreset::Speed);
			} else if (strcmp(preset, "balanced") == 0) {
				m_VideoEncoder->SetQualityPreset(QualityPreset::Balanced);
			} else if (strcmp(preset, "quality") == 0) {
				m_VideoEncoder->SetQualityPreset(QualityPreset::Quality);
			}
			obs_data_set_int(data, P_QUALITYPRESET, (int32_t)m_VideoEncoder->GetQualityPreset());
			obs_data_unset_user_value(data, "preset");
		}
	}
#pragma endregion OBS - Enforce Streaming Service Restrictions

	// Dynamic Properties (Can be changed during Encoding)
	this->update(data);

	// Initialize (locks static properties)
	try {
		m_VideoEncoder->Start();
	} catch (...) {
		throw;
	}

	// Dynamic Properties (Can be changed during Encoding)
	this->update(data);

	PLOG_DEBUG("<%s> Complete.", __FUNCTION_NAME__);
}

Plugin::Interface::H264Interface::~H264Interface()
{
	PLOG_DEBUG("<%s> Finalizing...", __FUNCTION_NAME__);
	if (m_VideoEncoder) {
		m_VideoEncoder->Stop();
		m_VideoEncoder = nullptr;
	}
	PLOG_DEBUG("<%s> Complete.", __FUNCTION_NAME__);
}

bool Plugin::Interface::H264Interface::update(obs_data_t* data)
{
	const video_t*                  obsVideoInfo = obs_encoder_video(m_Encoder);
	const struct video_output_info* voi          = video_output_get_info(obsVideoInfo);
	uint32_t                        obsFPSnum    = voi->fps_num;
	uint32_t                        obsFPSden    = voi->fps_den;

	// Rate Control
	RateControlMethod rcm = static_cast<RateControlMethod>(obs_data_get_int(data, P_RATECONTROLMETHOD));
	m_VideoEncoder->SetRateControlMethod(rcm);
	if (rcm == RateControlMethod::ConstantQP) {
		m_VideoEncoder->SetQPMinimum(0);
		m_VideoEncoder->SetQPMaximum(51);
		m_VideoEncoder->SetIFrameQP(static_cast<uint8_t>(obs_data_get_int(data, P_QP_IFRAME)));
		m_VideoEncoder->SetPFrameQP(static_cast<uint8_t>(obs_data_get_int(data, P_QP_PFRAME)));
		if (m_VideoEncoder->CapsBFramePattern() > 0) {
			try {
				m_VideoEncoder->SetBFrameQP(static_cast<uint8_t>(obs_data_get_int(data, P_QP_BFRAME)));
			} catch (...) {
			}
		}
		try {
			m_VideoEncoder->SetPrePassMode(PrePassMode::Disabled);
		} catch (...) {
		}
		try {
			m_VideoEncoder->SetVarianceBasedAdaptiveQuantizationEnabled(false);
		} catch (...) {
		}
	} else {
		m_VideoEncoder->SetQPMinimum(static_cast<uint8_t>(obs_data_get_int(data, P_QP_MINIMUM)));
		m_VideoEncoder->SetQPMaximum(static_cast<uint8_t>(obs_data_get_int(data, P_QP_MAXIMUM)));
		m_VideoEncoder->SetTargetBitrate(static_cast<uint32_t>(obs_data_get_int(data, "bitrate") * 1000));
		m_VideoEncoder->SetPeakBitrate(static_cast<uint32_t>(obs_data_get_int(data, P_BITRATE_PEAK) * 1000));
		try {
			m_VideoEncoder->SetPrePassMode(static_cast<PrePassMode>(obs_data_get_int(data, P_PREPASSMODE)));
		} catch (...) {
		}
		try {
			m_VideoEncoder->SetVarianceBasedAdaptiveQuantizationEnabled(!!obs_data_get_int(data, P_VBAQ));
		} catch (...) {
		}
	}
	if (rcm == RateControlMethod::ConstantBitrate) {
		m_VideoEncoder->SetPeakBitrate(static_cast<uint32_t>(obs_data_get_int(data, "bitrate") * 1000));
		m_VideoEncoder->SetFillerDataEnabled(!!obs_data_get_int(data, P_FILLERDATA));
	} else {
		m_VideoEncoder->SetFillerDataEnabled(false);
	}
	m_VideoEncoder->SetFrameSkippingEnabled(!!obs_data_get_int(data, P_FRAMESKIPPING));
	m_VideoEncoder->SetEnforceHRDEnabled(!!obs_data_get_int(data, P_ENFORCEHRD));
	m_VideoEncoder->SetVBVBufferInitialFullness((float)obs_data_get_double(data, P_VBVBUFFER_INITIALFULLNESS) / 100.0f);
	if (obs_data_get_int(data, P_VBVBUFFER) == 0) {
		m_VideoEncoder->SetVBVBufferStrictness(obs_data_get_double(data, P_VBVBUFFER_STRICTNESS) / 100.0);
	} else {
		m_VideoEncoder->SetVBVBufferSize(static_cast<uint32_t>(obs_data_get_int(data, P_VBVBUFFER_SIZE) * 1000));
	}
	try {
		int64_t v = obs_data_get_int(data, P_HIGHMOTIONQUALITYBOOST);
		if (v >= 0) {
			m_VideoEncoder->SetHighMotionQualityBoost(!!v);
		}
	} catch (...) {
	}

	// Picture Control
	double_t framerate = (double_t)obsFPSnum / (double_t)obsFPSden;
	/// Keyframe Interval/Period
	{
		uint32_t idrperiod = static_cast<uint32_t>(obs_data_get_int(data, P_PERIOD_IDR_H264));
		if (idrperiod == 0) {
			double_t keyinterv = obs_data_get_double(data, P_INTERVAL_KEYFRAME);
			idrperiod          = static_cast<uint32_t>(ceil((keyinterv * framerate)));
		}
		m_VideoEncoder->SetIDRPeriod(idrperiod);
		m_VideoEncoder->SetHeaderInsertionSpacing(idrperiod);
	}
	/// I/P/Skip Frame Interval/Period
	{
		uint32_t period = static_cast<uint32_t>(obs_data_get_double(data, P_INTERVAL_IFRAME) * framerate);
		period          = max(period, static_cast<uint32_t>(obs_data_get_int(data, P_PERIOD_IFRAME)));
		m_VideoEncoder->SetIFramePeriod(period);
	}
	{
		uint32_t period = static_cast<uint32_t>(obs_data_get_double(data, P_INTERVAL_PFRAME) * framerate);
		period          = max(period, static_cast<uint32_t>(obs_data_get_int(data, P_PERIOD_PFRAME)));
		m_VideoEncoder->SetPFramePeriod(period);
	}
	{
		uint32_t period = static_cast<uint32_t>(obs_data_get_double(data, P_INTERVAL_BFRAME) * framerate);
		period          = max(period, static_cast<uint32_t>(obs_data_get_int(data, P_PERIOD_BFRAME)));
		m_VideoEncoder->SetBFramePeriod(period);
	}
	{
		uint32_t period = static_cast<uint32_t>(obs_data_get_int(data, P_FRAMESKIPPING_PERIOD));
		m_VideoEncoder->SetFrameSkippingPeriod(period);
		m_VideoEncoder->SetFrameSkippingBehaviour(!!obs_data_get_int(data, P_FRAMESKIPPING_BEHAVIOUR));
	}
	m_VideoEncoder->SetDeblockingFilterEnabled(!!obs_data_get_int(data, P_DEBLOCKINGFILTER));

#pragma region B - Frames
	if (m_VideoEncoder->CapsBFramePattern() > 0) {
		try {
			m_VideoEncoder->SetBFramePattern(static_cast<uint8_t>(obs_data_get_int(data, P_BFRAME_PATTERN)));
			if (obs_data_get_int(data, P_BFRAME_PATTERN) != 0)
				m_VideoEncoder->SetBFrameDeltaQP(static_cast<int8_t>(obs_data_get_int(data, P_BFRAME_DELTAQP)));
			m_VideoEncoder->SetBFrameReferenceEnabled(!!obs_data_get_int(data, P_BFRAME_REFERENCE));
			if (!!obs_data_get_int(data, P_BFRAME_REFERENCE))
				m_VideoEncoder->SetBFrameReferenceDeltaQP(
					static_cast<int8_t>(obs_data_get_int(data, P_BFRAME_REFERENCEDELTAQP)));
		} catch (...) {
		}
	}
#pragma endregion B - Frames

	// Motion Estimation
	m_VideoEncoder->SetMotionEstimationHalfPixelEnabled(!!(obs_data_get_int(data, P_MOTIONESTIMATION) & 1));
	m_VideoEncoder->SetMotionEstimationQuarterPixelEnabled(!!(obs_data_get_int(data, P_MOTIONESTIMATION) & 2));

#pragma region OBS Enforce Streaming Service Settings
	{
		// Rate Control Method
		const char* t_str = obs_data_get_string(data, "rate_control");
		if (strcmp(t_str, "") != 0) {
			if (strcmp(t_str, "CBR") == 0) {
				m_VideoEncoder->SetRateControlMethod(RateControlMethod::ConstantBitrate);
				m_VideoEncoder->SetFillerDataEnabled(true);
			} else if (strcmp(t_str, "VBR") == 0) {
				m_VideoEncoder->SetRateControlMethod(RateControlMethod::PeakConstrainedVariableBitrate);
			} else if (strcmp(t_str, "VBR_LAT") == 0) {
				m_VideoEncoder->SetRateControlMethod(RateControlMethod::LatencyConstrainedVariableBitrate);
			} else if (strcmp(t_str, "CQP") == 0) {
				m_VideoEncoder->SetRateControlMethod(RateControlMethod::ConstantQP);
			}

			obs_data_set_int(data, P_RATECONTROLMETHOD, (int32_t)m_VideoEncoder->GetRateControlMethod());
			obs_data_unset_user_value(data, "rate_control");
		}

		// IDR-Period (Keyframes)
		uint32_t fpsNum = m_VideoEncoder->GetFrameRate().first;
		uint32_t fpsDen = m_VideoEncoder->GetFrameRate().second;
		if (obs_data_get_int(data, "keyint_sec") > 0) {
			uint32_t idrperiod = static_cast<uint32_t>(obs_data_get_int(data, "keyint_sec")
									  * (static_cast<double_t>(fpsNum) / static_cast<double_t>(fpsDen)));
			m_VideoEncoder->SetIDRPeriod(idrperiod);
			m_VideoEncoder->SetHeaderInsertionSpacing(idrperiod);

			obs_data_set_int(data, P_PERIOD_IDR_H264, idrperiod);
			obs_data_set_double(data, P_INTERVAL_KEYFRAME, static_cast<double_t>(obs_data_get_int(data, "keyint_sec")));
			obs_data_unset_user_value(data, "keyint_sec");
		}
	}
#pragma endregion OBS Enforce Streaming Service Settings

	m_VideoEncoder->SetDebug(obs_data_get_bool(data, P_DEBUG));

	if (m_VideoEncoder->IsStarted()) {
		m_VideoEncoder->LogProperties();
		if (static_cast<ViewMode>(obs_data_get_int(data, P_VIEW)) >= ViewMode::Master)
			PLOG_ERROR(
				"View Mode 'Master' is active, avoid giving anything but basic support. Error is most likely caused by "
				"user settings themselves.");
	}

	return true;
}

bool Plugin::Interface::H264Interface::encode(struct encoder_frame* frame, struct encoder_packet* packet,
											  bool* received_packet)
{
	if (!frame || !packet || !received_packet)
		return false;

	bool retVal = false;

	try {
		retVal = m_VideoEncoder->Encode(frame, packet, received_packet);
	} catch (std::exception e) {
		PLOG_ERROR("Exception during encoding: %s", e.what());
	} catch (...) {
		PLOG_ERROR("Unknown exception during encoding.");
	}

	return retVal;
}

void Plugin::Interface::H264Interface::get_video_info(struct video_scale_info* info)
{
	m_VideoEncoder->GetVideoInfo(info);
}

bool Plugin::Interface::H264Interface::get_extra_data(uint8_t** extra_data, size_t* size)
{
	return m_VideoEncoder->GetExtraData(extra_data, size);
}
