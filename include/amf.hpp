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
#include <memory>
#include "plugin.hpp"

#include <components\Component.h>
#include <components\ComponentCaps.h>
#include <components\VideoEncoderVCE.h>
#include <core\Factory.h>

extern "C" {
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#endif
}

namespace Plugin {
	namespace AMD {
		class AMF {
			public:
			static void Initialize();
			static AMF* Instance();
			static void Finalize();

			private: // Private Initializer & Finalizer
			AMF();
			~AMF();

			public: // Remove all Copy operators
			AMF(AMF const&) = delete;
			void operator=(AMF const&) = delete;

			public:
			amf::AMFFactory* GetFactory();
			amf::AMFTrace*   GetTrace();
			amf::AMFDebug*   GetDebug();

			void EnableDebugTrace(bool enable);

			uint64_t GetPluginVersion();
			uint64_t GetRuntimeVersion();

			private:
			uint32_t m_TimerPeriod; /// High-Precision Timer Accuracy (nanoseconds)

			/// AMF Values
			HMODULE  m_AMFModule;
			uint64_t m_AMFVersion_Plugin;
			uint64_t m_AMFVersion_Runtime;

			/// AMF Functions
			AMFQueryVersion_Fn AMFQueryVersion;
			AMFInit_Fn         AMFInit;

			/// AMF Objects
			amf::AMFFactory*     m_AMFFactory;
			amf::AMFTrace*       m_AMFTrace;
			amf::AMFDebug*       m_AMFDebug;
			amf::AMFTraceWriter* m_TraceWriter;
		};
	} // namespace AMD
} // namespace Plugin
