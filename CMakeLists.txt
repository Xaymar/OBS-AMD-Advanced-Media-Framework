# A Plugin that integrates the AMD AMF encoder into OBS Studio
# Copyright (C) 2016 - 2017 Michael Fabian Dirks
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

# CMake Setup
cmake_minimum_required(VERSION 3.1.0)
include("cmake/util.cmake")

# Automatic Versioning
set(VERSION_MAJOR 2)
set(VERSION_MINOR 7)
set(VERSION_PATCH 0)
set(VERSION_TWEAK 0)
set(PROJECT_COMMIT "N/A")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/.git")
	set(GIT_RESULT "")
	set(GIT_OUTPUT "")
	execute_process(
		COMMAND git rev-list --count --topo-order ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}..HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
		RESULT_VARIABLE GIT_RESULT
		OUTPUT_VARIABLE GIT_OUTPUT
		OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE ERROR_QUIET
	)
	if(GIT_RESULT EQUAL 0)
		set(VERSION_TWEAK ${GIT_OUTPUT})
	endif()
	execute_process(
		COMMAND git rev-parse HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
		RESULT_VARIABLE GIT_RESULT
		OUTPUT_VARIABLE GIT_OUTPUT
		OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE ERROR_QUIET
	)
	if(GIT_RESULT EQUAL 0)
		set(PROJECT_COMMIT ${GIT_OUTPUT})
	endif()
endif()

# Define Project
project(
	enc-amf
	VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_TWEAK}
)

################################################################################
# CMake / Compiler
################################################################################

# Detect Build Type
if("${CMAKE_SOURCE_DIR}" STREQUAL "${PROJECT_SOURCE_DIR}")
	set(PropertyPrefix "")
else()
	set(PropertyPrefix "${PROJECT_NAME}_")
endif()

# Detect Architecture
math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
if("${BITS}" STREQUAL "32")
	set(ARCH "x86")
else()
	set(ARCH "x64")
endif()

# Configure Version Header
configure_file(
	"${PROJECT_SOURCE_DIR}/include/version.hpp.in"
	"${PROJECT_BINARY_DIR}/include/version.hpp"
)

# Windows Specific Resource Definition
if(WIN32)
	set(PROJECT_PRODUCT_NAME "OBS Studio AMD Encoder")
	set(PROJECT_DESCRIPTION "")
	set(PROJECT_COMPANY_NAME "Xaymar")
	set(PROJECT_COPYRIGHT "Xaymar © 2016 - 2018")
	set(PROJECT_LEGAL_TRADEMARKS_1 "Advanced Micro Devices, AMD, AMD Ryzen, Ryzen, AMD Radeon and Radeon are Trademarks of Advanced Micro Devices.")
	set(PROJECT_LEGAL_TRADEMARKS_2 "")
	set(PROJECT_DESCRIPTION "AMD Encoder integration for OBS Studio")
	
	configure_file(
		"${PROJECT_SOURCE_DIR}/cmake/version.rc.in"
		"${PROJECT_BINARY_DIR}/cmake/version.rc"
		@ONLY
	)	
endif()

math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")

# Detect OBS Studio Type
if(TARGET libobs)
	set(${PropertyPrefix}OBS_NATIVE TRUE)	
else()
	message(STATUS "${PROJECT_NAME}: Using downloaded libOBS...")
	set(${PropertyPrefix}OBS_NATIVE FALSE)
endif()

################################################################################
# Options
################################################################################
if (NOT ${PropertyPrefix}OBS_NATIVE)
	set(${PropertyPrefix}DOWNLOAD_OBS_VERSION "" CACHE STRING "(Optional) OBS Studio version to build against.")
	set(${PropertyPrefix}DOWNLOAD_OBS_HASH_32 "" CACHE STRING "")
	set(${PropertyPrefix}DOWNLOAD_OBS_HASH_64 "" CACHE STRING "")
	mark_as_advanced(
		${PropertyPrefix}DOWNLOAD_OBS_VERSION
		${PropertyPrefix}DOWNLOAD_OBS_HASH_32
		${PropertyPrefix}DOWNLOAD_OBS_HASH_64
	)

	set(${PropertyPrefix}DOWNLOAD_OBSDEPS_VERSION "" CACHE STRING "(Optional) OBS Studio dependencies version to build against.")
	set(${PropertyPrefix}DOWNLOAD_OBSDEPS_HASH "" CACHE STRING "")
	mark_as_advanced(
		${PropertyPrefix}DOWNLOAD_OBSDEPS_VERSION
		${PropertyPrefix}DOWNLOAD_OBSDEPS_HASH
	)
endif()

################################################################################
# Dependencies
################################################################################

# Load OBS Studio
if(${PropertyPrefix}OBS_NATIVE)
	option(BUILD_AMD_ENCODER "Build AMD Encoder module" ON)	
	if (NOT BUILD_AMD_ENCODER)
		message(STATUS "Not building AMD Encoder")
		return()
	endif()
else()
	include("cmake/DownloadProject.cmake")
	
	# Consider setting these with CMAKE_SYSTEM_PROCESSOR in the future.
	set(OBS_DOWNLOAD_OS "windows")
	set(OBS_DOWNLOAD_BITS 64)
	set(OBS_DOWNLOAD_ARCH "x64")

	# Download libOBS
	if(${PropertyPrefix}DOWNLOAD_OBS_VERSION)
		set(_DOWNLOAD_OBS_VERSION ${PropertyPrefix}DOWNLOAD_OBS_VERSION)
		set(_DOWNLOAD_OBS_HASH_32 ${PropertyPrefix}DOWNLOAD_OBS_HASH_32)
		set(_DOWNLOAD_OBS_HASH_64 ${PropertyPrefix}DOWNLOAD_OBS_HASH_64)
	else()
		set(_DOWNLOAD_OBS_VERSION "26.0.2-ci")
		if(WIN32)
			set(_DOWNLOAD_OBS_HASH_32 "SHA512=468A999ECC8C826683E208B8758FBAE2FD3C9C034A73A147F6BE0F40B6E0AF0B0C9A99DFCB8D3A856206A26FC45F5F0A3B1FF781949B3EF68B765FECEB5CEB55")
			set(_DOWNLOAD_OBS_HASH_64 "SHA512=571B4196DF5CFBD3A8E113C97C7890000C490DDF1C6747529DE0F06063F928E7488095775919017104DE7A693CCF51A7A82BD04BCE62D6B3F0ED1F74A5F32B32")
		elseif(UNIX AND NOT APPLE)
			set(_DOWNLOAD_OBS_HASH_32 "")
			set(_DOWNLOAD_OBS_HASH_64 "SHA512=62EAD42890CEF953806F5780ED37304D6BD110F65D3ED83A88ADA462B0A0E46E9DEE477A76D456C444102EBA4F9F9A9EAAA299DCFDB2872233E2B35F775CCD0D")
		elseif(APPLE)
			set(_DOWNLOAD_OBS_HASH_32 "")
			set(_DOWNLOAD_OBS_HASH_64 "SHA512=2A7E312666FAABF539740DAD8E30788DEB5882BE8D2FF062848741FD6F8C2077FB237CFB399634BC9C3DE38CF78B3043B1EAE92D833CE6A8B32F5E1EB92D6618")
		else()
			message(FATAL_ERROR "No prebuilt packages available for this operating system.")
			return()
		endif()
	endif()

	download_project(
		PROJ libobs
		URL "https://github.com/Xaymar/obs-studio/releases/download/${_DOWNLOAD_OBS_VERSION}/obs-studio-${OBS_DOWNLOAD_ARCH}-0.0.0.0-${OBS_DOWNLOAD_OS}-${OBS_DOWNLOAD_BITS}.7z"
		URL_HASH "${_DOWNLOAD_OBS_HASH_${BITS}}"
		DOWNLOAD_NAME "libobs.7z"
		DOWNLOAD_NO_PROGRESS OFF
		UPDATE_DISCONNECTED OFF
	)

	download_project(
		PROJ libobs
		URL ${OBS_DOWNLOAD_URL}
		UPDATE_DISCONNECTED 1
	)
	include("${libobs_SOURCE_DIR}/cmake/LibObs/LibObsConfig.cmake")
endif()

################################################################################
# Code
################################################################################
set(PROJECT_HEADERS
	"${PROJECT_SOURCE_DIR}/include/amf.hpp"
	"${PROJECT_SOURCE_DIR}/include/amf-capabilities.hpp"
	"${PROJECT_SOURCE_DIR}/include/amf-encoder.hpp"
	"${PROJECT_SOURCE_DIR}/include/amf-encoder-h264.hpp"
	"${PROJECT_SOURCE_DIR}/include/enc-h264.hpp"
	"${PROJECT_SOURCE_DIR}/include/amf-encoder-h265.hpp"
	"${PROJECT_SOURCE_DIR}/include/enc-h265.hpp"
	"${PROJECT_SOURCE_DIR}/include/api-base.hpp"
	"${PROJECT_SOURCE_DIR}/include/api-host.hpp"
	"${PROJECT_SOURCE_DIR}/include/api-opengl.hpp"
	"${PROJECT_SOURCE_DIR}/include/utility.hpp"
	"${PROJECT_SOURCE_DIR}/include/plugin.hpp"
	"${PROJECT_SOURCE_DIR}/include/strings.hpp"
	"${PROJECT_BINARY_DIR}/include/version.hpp"
)
set(PROJECT_SOURCES
	"${PROJECT_SOURCE_DIR}/source/amf.cpp"
	"${PROJECT_SOURCE_DIR}/source/amf-capabilities.cpp"
	"${PROJECT_SOURCE_DIR}/source/amf-encoder.cpp"
	"${PROJECT_SOURCE_DIR}/source/amf-encoder-h264.cpp"
	"${PROJECT_SOURCE_DIR}/source/enc-h264.cpp"
	"${PROJECT_SOURCE_DIR}/source/amf-encoder-h265.cpp"
	"${PROJECT_SOURCE_DIR}/source/enc-h265.cpp"
	"${PROJECT_SOURCE_DIR}/source/api-base.cpp"
	"${PROJECT_SOURCE_DIR}/source/api-host.cpp"
	"${PROJECT_SOURCE_DIR}/source/api-opengl.cpp"
	"${PROJECT_SOURCE_DIR}/source/utility.cpp"
	"${PROJECT_SOURCE_DIR}/source/plugin.cpp"
)
set(PROJECT_DATA
	"${PROJECT_SOURCE_DIR}/resources/locale/en-US.ini"
	"${PROJECT_SOURCE_DIR}/LICENSE"
)
set(PROJECT_LIBRARIES
	version
	winmm
)

if(WIN32) # Windows Only
	list(APPEND PROJECT_HEADERS
		"include/api-d3d9.hpp"
		"include/api-d3d11.hpp"
	)
	list(APPEND PROJECT_SOURCES
		"source/api-d3d9.cpp"
		"source/api-d3d11.cpp"
		"${PROJECT_BINARY_DIR}/cmake/version.rc"
	)
endif()

# Source Grouping
source_group("Data Files" FILES ${enc-amf_DATA})

################################################################################
# Target
################################################################################

add_library(${PROJECT_NAME} MODULE
	${PROJECT_HEADERS}
	${PROJECT_SOURCES}
	${PROJECT_DATA}
)
if(${PropertyPrefix}OBS_NATIVE)
	set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "plugins/enc-amf")
endif()

# Include Directories
target_include_directories(${PROJECT_NAME}
	PUBLIC
		"${PROJECT_BINARY_DIR}/include"
		"${PROJECT_SOURCE_DIR}/include"
		"${PROJECT_SOURCE_DIR}/AMF/amf/public/include"
	PRIVATE
		"${PROJECT_BINARY_DIR}/source"
		"${PROJECT_SOURCE_DIR}/source"
		"${PROJECT_BINARY_DIR}"
		"${PROJECT_SOURCE_DIR}"
		"${CMAKE_SOURCE_DIR}"
)

# OBS Studio
target_link_libraries(${PROJECT_NAME}
	libobs
)

# Link Libraries
target_link_libraries(${PROJECT_NAME}
	"${PROJECT_LIBRARIES}"
)

# Definitions
if (WIN32)
	target_compile_definitions(${PROJECT_NAME}
		PRIVATE
			_CRT_SECURE_NO_WARNINGS
			# windows.h
			WIN32_LEAN_AND_MEAN
			NOGPICAPMASKS
			NOVIRTUALKEYCODES
			#NOWINMESSAGES
			NOWINSTYLES
			NOSYSMETRICS
			NOMENUS
			NOICONS
			NOKEYSTATES
			NOSYSCOMMANDS
			NORASTEROPS
			NOSHOWWINDOW
			NOATOM
			NOCLIPBOARD
			NOCOLOR
			NOCTLMGR
			NODRAWTEXT
			#NOGDI
			NOKERNEL
			#NOUSER
			#NONLS
			NOMB
			NOMEMMGR
			NOMETAFILE
			NOMINMAX
			#NOMSG
			NOOPENFILE
			NOSCROLL
			NOSERVICE
			NOSOUND
			#NOTEXTMETRIC
			NOWH
			NOWINOFFSETS
			NOCOMM
			NOKANJI
			NOHELP
			NOPROFILER
			NODEFERWINDOWPOS
			NOMCX
			NOIME
			NOMDI
			NOINOUT
	)
endif()

# File Version
if(WIN32)
	set_target_properties(
		${PROJECT_NAME}
		PROPERTIES
		VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}
		SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}
	)
else()
	set_target_properties(
		${PROJECT_NAME}
		PROPERTIES
		VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}
		SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}
	)
endif()

################################################################################
# Installation
################################################################################

if(${PropertyPrefix}OBS_NATIVE)
	install_obs_plugin_with_data(${PROJECT_NAME} resources)
else()
	install(
		TARGETS ${PROJECT_NAME}
		RUNTIME DESTINATION "obs-plugins/${BITS}bit/" COMPONENT Runtime
		LIBRARY DESTINATION "obs-plugins/${BITS}bit/" COMPONENT Runtime
	)
	if(MSVC)
		install(
			FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
			DESTINATION "obs-plugins/${BITS}bit/"
			OPTIONAL
		)
	endif()
	
	install(
		DIRECTORY "${PROJECT_SOURCE_DIR}/resources/"
		DESTINATION "data/obs-plugins/${PROJECT_NAME}/"
	)
endif()

################################################################################
# Packaging
################################################################################

set(CPACK_PACKAGE_NAME "obs-amd-encoder")
set(CPACK_PACKAGE_VENDOR "The OBS Project")
set(CPACK_PACKAGE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_TWEAK}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)

################################################################################
# Child Projects
################################################################################

# Sub Project
add_subdirectory(amf-test)
