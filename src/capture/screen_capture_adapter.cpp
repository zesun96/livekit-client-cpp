/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "screen_capture_adapter.h"

#include "frame_queue.h"

#include "media_capture/screen_capture.h"
#include "media_capture/screen_source.h"

#include <utility>

namespace livekit::capture {
class ScreenCaptureAdapter::Impl {
public:
	Impl(std::string source_id, std::uint32_t frames_per_second, bool include_cursor,
	     ScreenFrameCallback callback)
	    : frame_queue_([callback = std::move(callback)](const OwnedBgraFrame& frame) {
		      CapturedVideoFrame converted;
		      if (ConvertBgraToI420(frame.data.data(), frame.width, frame.height,
		                            frame.row_stride_bytes, frame.timestamp_us, converted)) {
			      converted.rotation_degrees = frame.rotation_degrees;
			      converted.mirrored = frame.mirrored;
			      callback(converted);
		      }
	      }) {
		media_capture::ScreenCaptureConfig config;
		config.source_id = std::move(source_id);
		config.frames_per_second = frames_per_second;
		config.include_cursor = include_cursor;
		capture_ = media_capture::CreateScreenCapture(std::move(config), [this](const auto& frame) {
			frame_queue_.Push(frame.data, frame.width, frame.height, frame.row_stride_bytes,
			                  frame.timestamp_us);
		});
	}

	~Impl() { Stop(); }

	bool Start() {
		if (!capture_ || !frame_queue_.Start()) {
			return false;
		}
		if (!capture_->Start()) {
			frame_queue_.Stop();
			return false;
		}
		return true;
	}

	void Stop() noexcept {
		if (capture_) {
			capture_->Stop();
		}
		frame_queue_.Stop();
	}

	LatestVideoFrameQueue frame_queue_;
	std::unique_ptr<media_capture::ScreenCapture> capture_;
};

ScreenCaptureAdapter::ScreenCaptureAdapter(std::string source_id, std::uint32_t frames_per_second,
                                           bool include_cursor, ScreenFrameCallback callback)
    : impl_(std::make_unique<Impl>(std::move(source_id), frames_per_second, include_cursor,
                                   std::move(callback))) {}

ScreenCaptureAdapter::~ScreenCaptureAdapter() = default;

bool ScreenCaptureAdapter::Start() { return impl_->Start(); }

void ScreenCaptureAdapter::Stop() noexcept { impl_->Stop(); }

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
