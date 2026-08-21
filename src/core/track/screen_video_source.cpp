/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "screen_video_source.h"

#include <utility>

namespace livekit::core {

ScreenVideoSource::ScreenVideoSource(ScreenCaptureOptions options)
    : VideoSource({true}), options_(std::move(options)),
      capture_(CreateAdapter(options_.source_id)) {}

ScreenVideoSource::~ScreenVideoSource() { Stop(); }

bool ScreenVideoSource::CaptureFrame(const VideoFrame& frame) {
	return VideoSource::CaptureFrame(frame);
}

uint32_t ScreenVideoSource::Width() const { return VideoSource::Width(); }

uint32_t ScreenVideoSource::Height() const { return VideoSource::Height(); }

std::unique_ptr<capture::ScreenCaptureAdapter>
ScreenVideoSource::CreateAdapter(const std::string& source_id) {
	return std::make_unique<capture::ScreenCaptureAdapter>(
	    source_id, options_.frames_per_second, options_.include_cursor,
	    [this](const capture::CapturedVideoFrame& frame) { OnFrame(frame); });
}

bool ScreenVideoSource::Start() {
	std::lock_guard<std::mutex> guard(mutex_);
	return capture_ && capture_->Start();
}

void ScreenVideoSource::Stop() {
	std::lock_guard<std::mutex> guard(mutex_);
	if (capture_) {
		capture_->Stop();
	}
}

bool ScreenVideoSource::IsCapturing() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return capture_ && capture_->IsRunning();
}

std::string ScreenVideoSource::SourceId() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return capture_ ? capture_->SourceId() : std::string{};
}

bool ScreenVideoSource::SwitchSource(const std::string& source_id) {
	if (source_id.empty()) {
		return false;
	}
	std::lock_guard<std::mutex> guard(mutex_);
	if (capture_ && capture_->SourceId() == source_id) {
		return true;
	}
	auto replacement = CreateAdapter(source_id);
	if (!replacement || !replacement->Start()) {
		return false;
	}
	if (capture_) {
		capture_->Stop();
	}
	capture_ = std::move(replacement);
	options_.source_id = source_id;
	return true;
}

void ScreenVideoSource::OnFrame(const capture::CapturedVideoFrame& frame) {
	VideoFrame video_frame;
	video_frame.data = frame.i420;
	video_frame.width = frame.width;
	video_frame.height = frame.height;
	video_frame.timestamp_us = frame.timestamp_us;
	VideoSource::CaptureFrame(video_frame);
}

std::vector<ScreenCaptureSourceInfo> EnumerateScreenCaptureSources() {
	std::vector<ScreenCaptureSourceInfo> result;
	for (auto& source : capture::EnumerateScreenSources()) {
		result.push_back({std::move(source.id), std::move(source.label),
		                  source.kind == capture::ScreenSourceKind::Monitor
		                      ? ScreenCaptureSourceKind::Monitor
		                      : ScreenCaptureSourceKind::Window,
		                  source.x, source.y, source.width, source.height});
	}
	return result;
}

ScreenVideoSourceInterface* CreateScreenVideoSource(ScreenCaptureOptions options) {
	if (options.source_id.empty() || options.frames_per_second == 0 ||
	    options.frames_per_second > 60) {
		return nullptr;
	}
	auto source = std::make_unique<ScreenVideoSource>(std::move(options));
	if (!source->Start()) {
		return nullptr;
	}
	return source.release();
}

} // namespace livekit::core
