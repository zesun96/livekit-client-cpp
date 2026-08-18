/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "screen_capture_adapter.h"

#include "media_capture/screen_capture.h"
#include "media_capture/screen_source.h"

#include "libyuv/convert.h"

#include <limits>
#include <utility>

namespace livekit::capture {
bool ConvertBgraToI420(const std::uint8_t* data, std::uint32_t source_width,
                       std::uint32_t source_height, std::uint32_t row_stride_bytes,
                       std::int64_t timestamp_us, CapturedVideoFrame& destination) {
	const std::uint32_t width = source_width & ~1U;
	const std::uint32_t height = source_height & ~1U;
	if (data == nullptr || width == 0 || height == 0 ||
	    source_width > std::numeric_limits<std::uint32_t>::max() / 4U ||
	    width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
	    height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
	    row_stride_bytes < source_width * 4U ||
	    row_stride_bytes > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
		return false;
	}
	const std::size_t y_size = static_cast<std::size_t>(width) * height;
	const std::size_t chroma_size = y_size / 4;
	destination.i420.resize(y_size + chroma_size * 2);
	auto* y = destination.i420.data();
	auto* u = y + y_size;
	auto* v = u + chroma_size;
	if (libyuv::ARGBToI420(data, static_cast<int>(row_stride_bytes), y, static_cast<int>(width), u,
	                       static_cast<int>(width / 2), v, static_cast<int>(width / 2),
	                       static_cast<int>(width), static_cast<int>(height)) != 0) {
		return false;
	}
	destination.width = width;
	destination.height = height;
	destination.timestamp_us = timestamp_us;
	return true;
}

class ScreenCaptureAdapter::Impl {
public:
	Impl(std::string source_id, std::uint32_t frames_per_second, ScreenFrameCallback callback) {
		media_capture::ScreenCaptureConfig config;
		config.source_id = std::move(source_id);
		config.frames_per_second = frames_per_second;
		capture_ = media_capture::CreateScreenCapture(
		    std::move(config), [callback = std::move(callback)](const auto& frame) {
			    CapturedVideoFrame converted;
			    if (ConvertBgraToI420(frame.data, frame.width, frame.height, frame.row_stride_bytes,
			                          frame.timestamp_us, converted)) {
				    callback(converted);
			    }
		    });
	}

	std::unique_ptr<media_capture::ScreenCapture> capture_;
};

ScreenCaptureAdapter::ScreenCaptureAdapter(std::string source_id, std::uint32_t frames_per_second,
                                           ScreenFrameCallback callback)
    : impl_(std::make_unique<Impl>(std::move(source_id), frames_per_second, std::move(callback))) {}

ScreenCaptureAdapter::~ScreenCaptureAdapter() = default;

bool ScreenCaptureAdapter::Start() { return impl_->capture_ && impl_->capture_->Start(); }

void ScreenCaptureAdapter::Stop() noexcept {
	if (impl_->capture_) {
		impl_->capture_->Stop();
	}
}

bool ScreenCaptureAdapter::IsRunning() const noexcept {
	return impl_->capture_ && impl_->capture_->IsRunning();
}

std::string ScreenCaptureAdapter::SourceId() const {
	return impl_->capture_ ? impl_->capture_->SourceId() : std::string{};
}

std::string ScreenCaptureAdapter::LastError() const {
	return impl_->capture_ ? impl_->capture_->LastError() : "screen capture is unavailable";
}

std::vector<ScreenSourceInfo> EnumerateScreenSources() {
	std::vector<ScreenSourceInfo> result;
	for (auto& source : media_capture::EnumerateScreenSources()) {
		result.push_back({std::move(source.id), std::move(source.label),
		                  source.kind == media_capture::ScreenSourceKind::Monitor
		                      ? ScreenSourceKind::Monitor
		                      : ScreenSourceKind::Window,
		                  source.x, source.y, source.width, source.height});
	}
	return result;
}

} // namespace livekit::capture
