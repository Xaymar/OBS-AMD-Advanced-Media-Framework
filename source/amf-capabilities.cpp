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

#include "amf-capabilities.hpp"
#include "utility.hpp"

using namespace Plugin;
using namespace Plugin::AMD;

static CapabilityManager* __instance;
static std::mutex         __instance_mutex;
void                      Plugin::AMD::CapabilityManager::Initialize()
{
	const std::lock_guard<std::mutex> lock(__instance_mutex);
	if (!__instance)
		__instance = new CapabilityManager();
}

CapabilityManager* Plugin::AMD::CapabilityManager::Instance()
{
	const std::lock_guard<std::mutex> lock(__instance_mutex);
	return __instance;
}

void Plugin::AMD::CapabilityManager::Finalize()
{
	const std::lock_guard<std::mutex> lock(__instance_mutex);
	if (__instance)
		delete __instance;
	__instance = nullptr;
}

Plugin::AMD::CapabilityManager::CapabilityManager()
{
	// Potential fix for unintended crashes by AMD including asserts in release builds.
	AMD::AMF::Instance()->EnableDebugTrace(false);

	// Key order: API, Adapter, Codec
	for (auto api : API::EnumerateAPIs()) {
		PLOG_DEBUG("[Capability Manager] Testing %s API...", api->GetName().c_str());
		for (auto adapter : api->EnumerateAdapters()) {
			std::pair<Codec, bool> test_codecs[] = {
				std::make_pair(Codec::AVC, false),
				std::make_pair(Codec::HEVC, false),
			};

			for (auto& codec : test_codecs) {
				try {
					std::unique_ptr<AMD::Encoder> enc;

					if (codec.first == Codec::AVC || codec.first == Codec::SVC) {
						enc = std::make_unique<AMD::EncoderH264>(api, adapter);
					} else if (codec.first == Codec::HEVC) {
						enc = std::make_unique<AMD::EncoderH265>(api, adapter);
					}

					if (enc != nullptr) {
						codec.second = true;
					}
				} catch (const std::exception& e) {
					PLOG_DEBUG("[Capability Manager] Testing %s Adapter '%s' with codec %s failed, reason: %s",
							   api->GetName().c_str(), adapter.Name.c_str(), Utility::CodecToString(codec.first),
							   e.what());
#ifdef LITE_OBS
					e;
#endif
				}

				std::tuple<API::Type, API::Adapter, AMD::Codec> key =
					std::make_tuple(api->GetType(), adapter, codec.first);
				m_CapabilityMap[key] = codec.second;
			}

			PLOG_INFO(
				"[Capability Manager] Testing %s Adapter '%s':\n"
				"  %s: %s\n"
				"  %s: %s\n",
				api->GetName().c_str(), adapter.Name.c_str(), Utility::CodecToString(test_codecs[0].first),
				test_codecs[0].second ? "Supported" : "Not Supported", Utility::CodecToString(test_codecs[1].first),
				test_codecs[1].second ? "Supported" : "Not Supported");
		}
	}
}

Plugin::AMD::CapabilityManager::~CapabilityManager() {}

bool Plugin::AMD::CapabilityManager::IsCodecSupported(AMD::Codec codec)
{
	for (auto api : API::EnumerateAPIs()) {
		if (IsCodecSupportedByAPI(codec, api->GetType()))
			return true;
	}
	return false;
}

bool Plugin::AMD::CapabilityManager::IsCodecSupportedByAPI(AMD::Codec codec, API::Type type)
{
	auto api = API::GetAPI(type);
	for (auto adapter : api->EnumerateAdapters()) {
		if (IsCodecSupportedByAPIAdapter(codec, type, adapter) == true)
			return true;
	}
	return false;
}

bool Plugin::AMD::CapabilityManager::IsCodecSupportedByAPIAdapter(AMD::Codec codec, API::Type api, API::Adapter adapter)
{
	return m_CapabilityMap[std::make_tuple(api, adapter, codec)];
}
