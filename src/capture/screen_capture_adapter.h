/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "video_frame_converter.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace livekit::capture {

enum class ScreenSourceKind {
	Monitor,
	Window,
};

struct ScreenSourceInfo {
	std::string id;
	std::string label;
	ScreenSourceKind kind = ScreenSourceKind::Monitor;
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

using ScreenFrameCallback = std::function<void(const CapturedVideoFrame& frame)>;

class ScreenCaptureAdapter {
public:
	ScreenCaptureAdapter(std::string source_id, std::uint32_t frames_per_second,
	                     ScreenFrameCallback callback);
	~ScreenCaptureAdapter();

	ScreenCaptureAdapter(const ScreenCaptureAdapter&) = delete;
	ScreenCaptureAdapter& operator=(const ScreenCaptureAdapter&) = delete;

	bool Start();
	void Stop() noexcept;
	bool IsRunning() const noexcept;
	std::string SourceId() const;
	std::string LastError() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

std::vector<ScreenSourceInfo> EnumerateScreenSources();

} // namespace livekit::capture
