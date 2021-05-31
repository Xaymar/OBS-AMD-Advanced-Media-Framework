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

#include "api-d3d11.hpp"
#include <cinttypes>
#include <mutex>
#include <sstream>

using namespace Plugin::API;

class SingletonDXGI {
	public:
	static std::shared_ptr<SingletonDXGI> GetInstance()
	{
		static std::shared_ptr<SingletonDXGI> __instance = std::make_shared<SingletonDXGI>();
		static std::mutex                     __mutex;

		const std::lock_guard<std::mutex> lock(__mutex);
		return __instance;
	}

	SingletonDXGI()
	{
		hModule = LoadLibrary(TEXT("dxgi.dll"));
		if (hModule == 0)
			throw std::exception("Unable to load 'dxgi.dll'.");
	}
	~SingletonDXGI()
	{
		if (hModule)
			FreeLibrary(hModule);
	}

	HRESULT CreateDXGIFactory(REFIID riid, _Out_ void** ppFactory)
	{
		if (ppFactory)
			*ppFactory = nullptr;

		if (hModule == 0)
			return S_FALSE;

		typedef HRESULT(__stdcall * t_CreateDXGIFactory)(REFIID, void**);
		t_CreateDXGIFactory pCreateDXGIFactory = (t_CreateDXGIFactory)GetProcAddress(hModule, "CreateDXGIFactory");

		if (pCreateDXGIFactory) {
			return pCreateDXGIFactory(riid, ppFactory);
		}
		return S_FALSE;
	}
	HRESULT CreateDXGIFactory1(REFIID riid, _Out_ void** ppFactory)
	{
		if (ppFactory)
			*ppFactory = nullptr;

		if (hModule == 0)
			return S_FALSE;

		typedef HRESULT(__stdcall * t_CreateDXGIFactory1)(REFIID, void**);
		t_CreateDXGIFactory1 pCreateDXGIFactory1 = (t_CreateDXGIFactory1)GetProcAddress(hModule, "CreateDXGIFactory1");

		if (pCreateDXGIFactory1) {
			return pCreateDXGIFactory1(riid, ppFactory);
		}
		return S_FALSE;
	}

	private:
	HMODULE hModule;
};

class SingletonD3D11 {
	public:
	static std::shared_ptr<SingletonD3D11> GetInstance()
	{
		static std::shared_ptr<SingletonD3D11> __instance = std::make_shared<SingletonD3D11>();
		static std::mutex                      __mutex;

		const std::lock_guard<std::mutex> lock(__mutex);
		return __instance;
	}

	SingletonD3D11()
	{
		hModule = LoadLibrary(TEXT("d3d11.dll"));
		if (hModule == 0)
			throw std::exception("Unable to load 'd3d11.dll'.");
	}
	~SingletonD3D11()
	{
		if (hModule)
			FreeLibrary(hModule);
	}

	HRESULT WINAPI D3D11CreateDevice(_In_opt_ IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
									 UINT Flags, _In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
									 UINT FeatureLevels, UINT SDKVersion, _Out_opt_ ID3D11Device** ppDevice,
									 _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
									 _Out_opt_ ID3D11DeviceContext** ppImmediateContext)
	{
		if (ppDevice)
			*ppDevice = nullptr;
		pFeatureLevel = nullptr;
		if (ppImmediateContext)
			*ppImmediateContext = nullptr;

		if (hModule == 0)
			return S_FALSE;

		typedef HRESULT(__stdcall * t_D3D11CreateDevice)(_In_opt_ IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
														 CONST D3D_FEATURE_LEVEL*, UINT, UINT, _Out_opt_ ID3D11Device**,
														 _Out_opt_ D3D_FEATURE_LEVEL*, _Out_opt_ ID3D11DeviceContext**);
		t_D3D11CreateDevice pD3D11CreateDevice = (t_D3D11CreateDevice)GetProcAddress(hModule, "D3D11CreateDevice");

		if (pD3D11CreateDevice) {
			return pD3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion,
									  ppDevice, pFeatureLevel, ppImmediateContext);
		}
		return S_FALSE;
	}

	private:
	HMODULE hModule;
};

Plugin::API::Direct3D11::Direct3D11()
{
	auto    dxgiInst = SingletonDXGI::GetInstance();
	HRESULT hr       = dxgiInst->CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&m_DXGIFactory);
	if (FAILED(hr)) {
		std::vector<char> buf(1024);
		snprintf(buf.data(), buf.size(), "<%s> Unable to create DXGI, error code %X.", __FUNCTION_NAME__, hr);
		throw std::exception(buf.data());
	}

	// Enumerate Adapters
	IDXGIAdapter1* dxgiAdapter = nullptr;
	for (size_t adapterIndex = 0; !FAILED(m_DXGIFactory->EnumAdapters1((UINT)adapterIndex, &dxgiAdapter));
		 adapterIndex++) {
		DXGI_ADAPTER_DESC1 desc = DXGI_ADAPTER_DESC1();
		dxgiAdapter->GetDesc1(&desc);

		if (desc.VendorId != 0x1002 /* AMD */)
			continue;

		std::vector<char> buf(1024);
		snprintf(buf.data(), buf.size(), "%ls (VEN_%04x/DEV_%04x/SUB_%04x/REV_%04x)", desc.Description, desc.VendorId,
				 desc.DeviceId, desc.SubSysId, desc.Revision);

		m_AdapterList.emplace_back(desc.AdapterLuid.LowPart, desc.AdapterLuid.HighPart, std::string(buf.data()));
	}
}

Plugin::API::Direct3D11::~Direct3D11() {}

std::string Plugin::API::Direct3D11::GetName()
{
	return std::string("Direct3D 11");
}

std::vector<Adapter> Plugin::API::Direct3D11::EnumerateAdapters()
{
	// We shouldn't expect HW to change during Runtime, at least not in a normal System.
	return m_AdapterList;
}

std::shared_ptr<Instance> Plugin::API::Direct3D11::CreateInstance(Adapter adapter)
{
	//std::lock_guard<std::mutex> lock(m_InstanceMapMutex);
	//std::pair<int32_t, int32_t> key = std::make_pair(adapter.idLow, adapter.idHigh);
	//auto inst = m_InstanceMap.find(key);
	//if (inst != m_InstanceMap.end())
	//	return inst->second;

	auto inst2 = std::make_shared<Direct3D11Instance>(this, adapter);
	//m_InstanceMap.insert_or_assign(key, inst2);
	return inst2;
}

Plugin::API::Type Plugin::API::Direct3D11::GetType()
{
	return Type::Direct3D11;
}

Plugin::API::Direct3D11Instance::Direct3D11Instance(Direct3D11* api, Adapter adapter)
	: m_API(api), m_Adapter(adapter), m_Device(nullptr), m_DeviceContext(nullptr)
{
	LUID adapterLUID;
	adapterLUID.LowPart  = adapter.idLow;
	adapterLUID.HighPart = adapter.idHigh;

	HRESULT       hr = E_FAIL;
	IDXGIAdapter* dxgiAdapter;
	for (size_t adapterIndex = 0; !FAILED(api->m_DXGIFactory->EnumAdapters((UINT)adapterIndex, &dxgiAdapter));
		 adapterIndex++) {
		DXGI_ADAPTER_DESC desc = DXGI_ADAPTER_DESC();
		dxgiAdapter->GetDesc(&desc);

		if (desc.VendorId != 0x1002 /* AMD */)
			continue;

		if ((desc.AdapterLuid.HighPart == adapterLUID.HighPart) && (desc.AdapterLuid.LowPart == adapterLUID.LowPart)) {
			hr = NOERROR;
			break;
		} else {
			hr = E_INVALIDARG;
		}
	}
	if (FAILED(hr))
		throw std::invalid_argument("adapter");

	// Create D3D Stuff
	auto              d3dInst         = SingletonD3D11::GetInstance();
	D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
	for (size_t featureLevel = 0; featureLevel < _countof(featureLevels); featureLevel++) {
		for (size_t enabledFlags = 0; enabledFlags < 3; enabledFlags++) {
			uint32_t flags = 0;

			switch (enabledFlags) {
			case 0:
				flags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
			case 1:
				flags |= D3D11_CREATE_DEVICE_BGRA_SUPPORT;
			case 2:
				break;
			}

			hr = d3dInst->D3D11CreateDevice(
				dxgiAdapter, dxgiAdapter == NULL ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_UNKNOWN, NULL, flags,
				featureLevels + featureLevel, UINT(_countof(featureLevels) - featureLevel), D3D11_SDK_VERSION,
				&m_Device, NULL, &m_DeviceContext);
			if (SUCCEEDED(hr)) {
				break;
			} else {
				PLOG_DEBUG("<%s> Unable to create D3D11 device, error code %X (mode %lld, level %lld).",
						   __FUNCTION_NAME__, hr, enabledFlags, featureLevel);
			}
		}
	}
	if (FAILED(hr)) {
		std::vector<char> buf(1024);
		snprintf(buf.data(), buf.size(), "<%s> Unable to create D3D11 device, error code %X.", __FUNCTION_NAME__, hr);
		throw std::exception(buf.data());
	}
}

Plugin::API::Direct3D11Instance::~Direct3D11Instance()
{
	if (m_Device)
		m_Device->Release();

	//std::lock_guard<std::mutex> lock(m_API->m_InstanceMapMutex);
	//std::pair<int32_t, int32_t> key = std::make_pair(m_Adapter.idLow, m_Adapter.idHigh);
	//m_API->m_InstanceMap.erase(key);
}

Plugin::API::Adapter Plugin::API::Direct3D11Instance::GetAdapter()
{
	return m_Adapter;
}

void* Plugin::API::Direct3D11Instance::GetContext()
{
	return m_Device;
}
