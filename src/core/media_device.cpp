/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/core/media_device.h"

#include "modules/video_capture/video_capture_factory.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <mmdeviceapi.h>
#include <propsys.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#endif

namespace livekit::core {
namespace {

void EnumerateVideoInputs(std::vector<MediaDeviceInfo>& devices) {
	std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> device_info(
	    webrtc::VideoCaptureFactory::CreateDeviceInfo());
	if (device_info == nullptr) {
		return;
	}

	for (uint32_t index = 0; index < device_info->NumberOfDevices(); ++index) {
		std::array<char, 256> label{};
		std::array<char, 512> id{};
		if (device_info->GetDeviceName(index, label.data(), static_cast<uint32_t>(label.size()),
		                               id.data(), static_cast<uint32_t>(id.size())) != 0) {
			continue;
		}
		devices.push_back({id.data(), label.data(), MediaDeviceKind::VideoInput, false});
	}
}

#if defined(_WIN32)

using Microsoft::WRL::ComPtr;

class ComApartment {
public:
	ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
	~ComApartment() {
		if (result_ == S_OK || result_ == S_FALSE) {
			CoUninitialize();
		}
	}

	bool available() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }

private:
	HRESULT result_;
};

std::string WideToUtf8(const wchar_t* value) {
	if (value == nullptr || *value == L'\0') {
		return {};
	}
	const auto required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
	if (required <= 1) {
		return {};
	}
	std::string result(static_cast<std::size_t>(required), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
	result.pop_back();
	return result;
}

std::wstring DefaultEndpointId(IMMDeviceEnumerator* enumerator, EDataFlow flow) {
	ComPtr<IMMDevice> endpoint;
	if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &endpoint))) {
		return {};
	}
	wchar_t* id = nullptr;
	if (FAILED(endpoint->GetId(&id)) || id == nullptr) {
		return {};
	}
	std::wstring result(id);
	CoTaskMemFree(id);
	return result;
}

std::string FriendlyName(IMMDevice* device) {
	ComPtr<IPropertyStore> properties;
	if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
		return {};
	}
	PROPVARIANT value;
	PropVariantInit(&value);
	const auto result = properties->GetValue(PKEY_Device_FriendlyName, &value);
	std::string label;
	if (SUCCEEDED(result) && value.vt == VT_LPWSTR) {
		label = WideToUtf8(value.pwszVal);
	}
	PropVariantClear(&value);
	return label;
}

void EnumerateAudioEndpoints(IMMDeviceEnumerator* enumerator, EDataFlow flow, MediaDeviceKind kind,
                             std::vector<MediaDeviceInfo>& devices) {
	ComPtr<IMMDeviceCollection> collection;
	if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection))) {
		return;
	}
	const auto default_id = DefaultEndpointId(enumerator, flow);
	UINT count = 0;
	if (FAILED(collection->GetCount(&count))) {
		return;
	}
	for (UINT index = 0; index < count; ++index) {
		ComPtr<IMMDevice> endpoint;
		if (FAILED(collection->Item(index, &endpoint))) {
			continue;
		}
		wchar_t* raw_id = nullptr;
		if (FAILED(endpoint->GetId(&raw_id)) || raw_id == nullptr) {
			continue;
		}
		std::wstring wide_id(raw_id);
		CoTaskMemFree(raw_id);
		devices.push_back({WideToUtf8(wide_id.c_str()), FriendlyName(endpoint.Get()), kind,
		                   wide_id == default_id});
	}
}

void EnumerateAudioDevices(std::vector<MediaDeviceInfo>& devices) {
	ComApartment apartment;
	if (!apartment.available()) {
		return;
	}
	ComPtr<IMMDeviceEnumerator> enumerator;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
	                            IID_PPV_ARGS(&enumerator)))) {
		return;
	}
	EnumerateAudioEndpoints(enumerator.Get(), eCapture, MediaDeviceKind::AudioInput, devices);
	EnumerateAudioEndpoints(enumerator.Get(), eRender, MediaDeviceKind::AudioOutput, devices);
}

#else

void EnumerateAudioDevices(std::vector<MediaDeviceInfo>&) {}

#endif

} // namespace

std::vector<MediaDeviceInfo> EnumerateMediaDevices() {
	std::vector<MediaDeviceInfo> devices;
	EnumerateAudioDevices(devices);
	EnumerateVideoInputs(devices);
	return devices;
}

} // namespace livekit::core
