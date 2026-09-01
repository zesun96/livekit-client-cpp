/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "file_writer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <limits>
#include <string_view>

namespace livekit::core::recording {
namespace {

template <typename Value>
void WriteLittleEndian(std::ostream& output, Value value, std::size_t byte_count = sizeof(Value)) {
	for (std::size_t index = 0; index < byte_count; ++index) {
		output.put(static_cast<char>((static_cast<std::uint64_t>(value) >> (index * 8)) & 0xff));
	}
}

bool EndsWithCaseInsensitive(std::string_view value, std::string_view suffix) {
	if (value.size() < suffix.size()) {
		return false;
	}
	return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(), [](char left, char right) {
		return std::tolower(static_cast<unsigned char>(left)) ==
		       std::tolower(static_cast<unsigned char>(right));
	});
}

std::string VideoExtension(EncodedVideoCodec codec) {
	switch (codec) {
	case EncodedVideoCodec::H264:
		return ".h264";
	case EncodedVideoCodec::H265:
		return ".h265";
	case EncodedVideoCodec::VP8:
	case EncodedVideoCodec::VP9:
	case EncodedVideoCodec::AV1:
		return ".ivf";
	default:
		return {};
	}
}

std::array<char, 4> IvfFourCc(EncodedVideoCodec codec) {
	switch (codec) {
	case EncodedVideoCodec::VP8:
		return {'V', 'P', '8', '0'};
	case EncodedVideoCodec::VP9:
		return {'V', 'P', '9', '0'};
	case EncodedVideoCodec::AV1:
		return {'A', 'V', '0', '1'};
	default:
		return {};
	}
}

bool ValidParentDirectory(const std::string& output_path, std::string& error) {
	std::error_code filesystem_error;
	const auto parent = std::filesystem::path(output_path).parent_path();
	if (!parent.empty() && !std::filesystem::is_directory(parent, filesystem_error)) {
		error = "recording output directory does not exist: " + parent.string();
		return false;
	}
	return true;
}

} // namespace

std::string OutputPathWithExtension(const std::string& base_path, const std::string& extension) {
	return EndsWithCaseInsensitive(base_path, extension) ? base_path : base_path + extension;
}

WavFileWriter::~WavFileWriter() {
	std::string ignored;
	Close(ignored);
}

bool WavFileWriter::Open(const std::string& base_path, std::string& error) {
	if (base_path.empty()) {
		error = "recording output path is empty";
		return false;
	}
	output_path_ = OutputPathWithExtension(base_path, ".wav");
	if (!ValidParentDirectory(output_path_, error)) {
		return false;
	}
	output_.open(output_path_, std::ios::binary | std::ios::trunc);
	if (!output_) {
		error = "failed to open recording output: " + output_path_;
		return false;
	}
	const std::array<char, 44> placeholder{};
	output_.write(placeholder.data(), static_cast<std::streamsize>(placeholder.size()));
	if (!output_) {
		error = "failed to initialize WAV output: " + output_path_;
		return false;
	}
	closed_ = false;
	return true;
}

bool WavFileWriter::Write(const AudioFrame& frame, std::string& error) {
	if (closed_ || !output_) {
		error = "WAV output is not open";
		return false;
	}
	if (frame.sample_rate == 0 || frame.num_channels == 0 || frame.num_channels > 65535 ||
	    frame.samples_per_channel == 0 ||
	    frame.data.size() !=
	        static_cast<std::size_t>(frame.num_channels) * frame.samples_per_channel) {
		error = "invalid PCM audio frame";
		return false;
	}
	if (sample_rate_ == 0) {
		sample_rate_ = frame.sample_rate;
		channels_ = static_cast<std::uint16_t>(frame.num_channels);
	} else if (sample_rate_ != frame.sample_rate || channels_ != frame.num_channels) {
		error = "audio format changed during recording";
		return false;
	}
	const auto byte_count = frame.data.size() * sizeof(std::int16_t);
	if (bytes_written_ + byte_count > std::numeric_limits<std::uint32_t>::max() - 36ULL) {
		error = "WAV recording exceeded the 4 GiB RIFF limit";
		return false;
	}
	output_.write(reinterpret_cast<const char*>(frame.data.data()),
	              static_cast<std::streamsize>(byte_count));
	if (!output_) {
		error = "failed to write WAV audio data";
		return false;
	}
	++frames_written_;
	bytes_written_ += byte_count;
	return true;
}

bool WavFileWriter::Close(std::string& error) noexcept {
	if (closed_) {
		return true;
	}
	closed_ = true;
	const auto sample_rate = sample_rate_ == 0 ? 48000U : sample_rate_;
	const auto channels = channels_ == 0 ? static_cast<std::uint16_t>(1) : channels_;
	output_.seekp(0, std::ios::beg);
	output_.write("RIFF", 4);
	WriteLittleEndian(output_, static_cast<std::uint32_t>(36 + bytes_written_));
	output_.write("WAVEfmt ", 8);
	WriteLittleEndian(output_, static_cast<std::uint32_t>(16));
	WriteLittleEndian(output_, static_cast<std::uint16_t>(1));
	WriteLittleEndian(output_, channels);
	WriteLittleEndian(output_, sample_rate);
	WriteLittleEndian(output_,
	                  static_cast<std::uint32_t>(sample_rate * channels * sizeof(std::int16_t)));
	WriteLittleEndian(output_, static_cast<std::uint16_t>(channels * sizeof(std::int16_t)));
	WriteLittleEndian(output_, static_cast<std::uint16_t>(16));
	output_.write("data", 4);
	WriteLittleEndian(output_, static_cast<std::uint32_t>(bytes_written_));
	output_.close();
	if (output_.fail()) {
		error = "failed to finalize WAV output: " + output_path_;
		return false;
	}
	return true;
}

bool EncodedVideoFileWriter::Open(const std::string& base_path, const EncodedVideoFrame& frame,
                                  std::string& error) {
	if (base_path.empty()) {
		error = "recording output path is empty";
		return false;
	}
	const auto extension = VideoExtension(frame.codec);
	if (extension.empty()) {
		error = "unsupported encoded video codec";
		return false;
	}
	const bool is_ivf = frame.codec == EncodedVideoCodec::VP8 ||
	                    frame.codec == EncodedVideoCodec::VP9 ||
	                    frame.codec == EncodedVideoCodec::AV1;
	if (is_ivf &&
	    (frame.width == 0 || frame.height == 0 || frame.width > 65535 || frame.height > 65535)) {
		error = "IVF recording requires a valid 16-bit frame resolution";
		return false;
	}
	output_path_ = OutputPathWithExtension(base_path, extension);
	if (!ValidParentDirectory(output_path_, error)) {
		return false;
	}
	codec_ = frame.codec;
	ivf_ = is_ivf;
	output_.open(output_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
	if (!output_) {
		error = "failed to open recording output: " + output_path_;
		return false;
	}
	closed_ = false;
	first_timestamp_us_ = frame.timestamp_us;
	return !ivf_ || WriteIvfHeader(frame, error);
}

EncodedVideoFileWriter::~EncodedVideoFileWriter() {
	std::string ignored;
	Close(ignored);
}

bool EncodedVideoFileWriter::WriteIvfHeader(const EncodedVideoFrame& frame, std::string& error) {
	if (frame.width == 0 || frame.height == 0 || frame.width > 65535 || frame.height > 65535) {
		error = "IVF recording requires a valid 16-bit frame resolution";
		return false;
	}
	const auto fourcc = IvfFourCc(frame.codec);
	output_.write("DKIF", 4);
	WriteLittleEndian(output_, static_cast<std::uint16_t>(0));
	WriteLittleEndian(output_, static_cast<std::uint16_t>(32));
	output_.write(fourcc.data(), static_cast<std::streamsize>(fourcc.size()));
	WriteLittleEndian(output_, static_cast<std::uint16_t>(frame.width));
	WriteLittleEndian(output_, static_cast<std::uint16_t>(frame.height));
	WriteLittleEndian(output_, static_cast<std::uint32_t>(1000000));
	WriteLittleEndian(output_, static_cast<std::uint32_t>(1));
	WriteLittleEndian(output_, static_cast<std::uint32_t>(0));
	WriteLittleEndian(output_, static_cast<std::uint32_t>(0));
	if (!output_) {
		error = "failed to initialize IVF output";
		return false;
	}
	return true;
}

bool EncodedVideoFileWriter::Write(const std::string& base_path, const EncodedVideoFrame& frame,
                                   std::string& error) {
	if (frame.data.empty()) {
		error = "encoded video frame is empty";
		return false;
	}
	if (closed_ && !Open(base_path, frame, error)) {
		return false;
	}
	if (frame.codec != codec_) {
		error = "video codec changed during recording";
		return false;
	}
	if (frame.data.size() > std::numeric_limits<std::uint32_t>::max()) {
		error = "encoded video frame is too large";
		return false;
	}
	if (ivf_) {
		WriteLittleEndian(output_, static_cast<std::uint32_t>(frame.data.size()));
		std::uint64_t timestamp = 0;
		if (frame.timestamp_us > first_timestamp_us_) {
			timestamp = static_cast<std::uint64_t>(frame.timestamp_us - first_timestamp_us_);
		}
		if (frames_written_ != 0 && timestamp <= last_timestamp_) {
			timestamp = last_timestamp_ + 1;
		}
		WriteLittleEndian(output_, timestamp);
		last_timestamp_ = timestamp;
	}
	output_.write(reinterpret_cast<const char*>(frame.data.data()),
	              static_cast<std::streamsize>(frame.data.size()));
	if (!output_) {
		error = "failed to write encoded video data";
		return false;
	}
	++frames_written_;
	bytes_written_ += frame.data.size();
	return true;
}

bool EncodedVideoFileWriter::Close(std::string& error) noexcept {
	if (closed_) {
		return true;
	}
	closed_ = true;
	if (ivf_) {
		output_.seekp(24, std::ios::beg);
		WriteLittleEndian(output_, static_cast<std::uint32_t>(frames_written_));
	}
	output_.close();
	if (output_.fail()) {
		error = "failed to finalize video output: " + output_path_;
		return false;
	}
	return true;
}

} // namespace livekit::core::recording
