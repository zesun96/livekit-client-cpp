/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifndef _LKC_CORE_RECORDING_TRACK_RECORDER_H_
#define _LKC_CORE_RECORDING_TRACK_RECORDER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace livekit {
namespace core {

class RemoteTrackInterface;

enum class TrackRecorderState {
	Recording,
	Stopped,
	Failed,
};

struct TrackRecorderOptions {
	// A filename prefix. The recorder appends .wav, .h264, .h265, or .ivf unless the matching
	// extension is already present. The parent directory must exist.
	std::string output_path;
	// The number of owned frames retained between the libwebrtc callback and the file writer.
	std::size_t queue_capacity = 256;
};

struct TrackRecorderStats {
	TrackRecorderState state = TrackRecorderState::Stopped;
	std::string output_path;
	std::uint64_t frames_written = 0;
	std::uint64_t bytes_written = 0;
	std::uint64_t frames_dropped = 0;
	std::string error;
};

// Records one subscribed remote track to one file. Video keeps the received codec and writes
// H264/H265 Annex-B or VP8/VP9/AV1 IVF. Audio is decoded by libwebrtc and written as PCM WAV.
// Destruction calls Stop(); Stop is idempotent and finalizes container headers.
class TrackRecorder {
public:
	~TrackRecorder();
	TrackRecorder(const TrackRecorder&) = delete;
	TrackRecorder& operator=(const TrackRecorder&) = delete;

	void Stop() noexcept;
	bool IsRecording() const noexcept;
	TrackRecorderStats Stats() const;

private:
	class Impl;
	explicit TrackRecorder(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;

	friend std::unique_ptr<TrackRecorder> StartTrackRecording(RemoteTrackInterface&,
	                                                          TrackRecorderOptions, std::string*);
};

// Starts recording immediately. The returned recorder owns its media reader but not the remote
// track. A destroyed or unsubscribed track closes the reader and finalizes the file. On immediate
// validation or setup failure, returns null and writes a diagnostic to error when provided.
std::unique_ptr<TrackRecorder> StartTrackRecording(RemoteTrackInterface& track,
                                                   TrackRecorderOptions options,
                                                   std::string* error = nullptr);

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_RECORDING_TRACK_RECORDER_H_
