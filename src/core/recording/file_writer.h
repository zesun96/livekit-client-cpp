/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "livekit/core/track/audio_frame.h"
#include "livekit/core/track/encoded_video_frame.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace livekit::core::recording {

std::string OutputPathWithExtension(const std::string& base_path, const std::string& extension);

class WavFileWriter {
public:
	~WavFileWriter();
	bool Open(const std::string& base_path, std::string& error);
	bool Write(const AudioFrame& frame, std::string& error);
	bool Close(std::string& error) noexcept;
	const std::string& OutputPath() const noexcept { return output_path_; }
	std::uint64_t FramesWritten() const noexcept { return frames_written_; }
	std::uint64_t BytesWritten() const noexcept { return bytes_written_; }

private:
	std::ofstream output_;
	std::string output_path_;
	std::uint32_t sample_rate_ = 0;
	std::uint16_t channels_ = 0;
	std::uint64_t frames_written_ = 0;
	std::uint64_t bytes_written_ = 0;
	bool closed_ = true;
};

class EncodedVideoFileWriter {
public:
	~EncodedVideoFileWriter();
	bool Write(const std::string& base_path, const EncodedVideoFrame& frame, std::string& error);
	bool Close(std::string& error) noexcept;
	const std::string& OutputPath() const noexcept { return output_path_; }
	std::uint64_t FramesWritten() const noexcept { return frames_written_; }
	std::uint64_t BytesWritten() const noexcept { return bytes_written_; }
	EncodedVideoCodec Codec() const noexcept { return codec_; }

private:
	bool Open(const std::string& base_path, const EncodedVideoFrame& frame, std::string& error);
	bool WriteIvfHeader(const EncodedVideoFrame& frame, std::string& error);

	std::fstream output_;
	std::string output_path_;
	EncodedVideoCodec codec_ = EncodedVideoCodec::Unknown;
	std::int64_t first_timestamp_us_ = 0;
	std::uint64_t last_timestamp_ = 0;
	std::uint64_t frames_written_ = 0;
	std::uint64_t bytes_written_ = 0;
	bool ivf_ = false;
	bool closed_ = true;
};

} // namespace livekit::core::recording
