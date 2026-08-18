/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "video_source.h"

#include "../../capture/screen_capture_adapter.h"

#include <memory>
#include <mutex>

namespace livekit::core {

class ScreenVideoSource final : public ScreenVideoSourceInterface, public VideoSource {
public:
	explicit ScreenVideoSource(ScreenCaptureOptions options);
	~ScreenVideoSource() override;

	bool CaptureFrame(const VideoFrame& frame) override;
	uint32_t Width() const override;
	uint32_t Height() const override;
	bool Start() override;
	void Stop() override;
	bool IsCapturing() const override;
	std::string SourceId() const override;
	bool SwitchSource(const std::string& source_id) override;

private:
	std::unique_ptr<capture::ScreenCaptureAdapter> CreateAdapter(const std::string& source_id);
	void OnFrame(const capture::CapturedVideoFrame& frame);

	mutable std::mutex mutex_;
	ScreenCaptureOptions options_;
	std::unique_ptr<capture::ScreenCaptureAdapter> capture_;
};

} // namespace livekit::core
