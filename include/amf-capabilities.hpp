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
#include <cinttypes>
#include <list>
#include <map>
#include <tuple>
#include <vector>
#include "amf-encoder-h264.hpp"
#include "amf-encoder-h265.hpp"
#include "amf-encoder.hpp"
#include "amf.hpp"
#include "api-base.hpp"
#include "plugin.hpp"

#include <components\ComponentCaps.h>

namespace Plugin {
	namespace AMD {
		class CapabilityManager {
			public:
			static void               Initialize();
			static CapabilityManager* Instance();
			static void               Finalize();

			private: // Private Initializer & Finalizer
			CapabilityManager();
			~CapabilityManager();

			public: // Remove all Copy operators
			CapabilityManager(CapabilityManager const&) = delete;
			void operator=(CapabilityManager const&) = delete;

			bool IsCodecSupported(AMD::Codec codec);
			bool IsCodecSupportedByAPI(AMD::Codec codec, API::Type api);
			bool IsCodecSupportedByAPIAdapter(AMD::Codec codec, API::Type api, API::Adapter adapter);

			private:
			std::map<std::tuple<API::Type, API::Adapter, AMD::Codec>, bool> m_CapabilityMap;
		};
	} // namespace AMD
} // namespace Plugin
