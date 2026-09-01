/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "livekit/core/recording/track_recorder.h"

#include "file_writer.h"
#include "livekit/core/option/option.h"
#include "livekit/core/track/media_stream.h"
#include "livekit/core/track/remote_track_interface.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

namespace livekit {
namespace core {

using namespace std::chrono_literals;

class TrackRecorder::Impl {
public:
	Impl(RemoteTrackInterface& track, TrackRecorderOptions options)
	    : track_kind_(track.Kind()), options_(std::move(options)) {
		stats_.state = TrackRecorderState::Stopped;
		if (options_.output_path.empty()) {
			setup_error_ = "recording output path is empty";
			return;
		}
		if (options_.queue_capacity == 0) {
			setup_error_ = "recording queue capacity must be greater than zero";
			return;
		}
		std::error_code filesystem_error;
		const auto parent = std::filesystem::path(options_.output_path).parent_path();
		if (!parent.empty() && !std::filesystem::is_directory(parent, filesystem_error)) {
			setup_error_ = "recording output directory does not exist: " + parent.string();
			return;
		}

		MediaStreamOptions stream_options;
		stream_options.capacity = options_.queue_capacity;
		if (track_kind_ == TrackKind::Audio) {
			audio_stream_ = track.CreateAudioStream(stream_options);
			if (!audio_stream_) {
				setup_error_ = "remote audio track does not provide an audio stream";
				return;
			}
			std::string writer_error;
			if (!audio_writer_.Open(options_.output_path, writer_error)) {
				setup_error_ = std::move(writer_error);
				return;
			}
			stats_.output_path = audio_writer_.OutputPath();
		} else if (track_kind_ == TrackKind::Video) {
			video_stream_ = track.CreateEncodedVideoStream(stream_options);
			if (!video_stream_) {
				setup_error_ = "remote video track does not provide encoded output";
				return;
			}
		} else {
			setup_error_ = "only remote audio and video tracks can be recorded";
			return;
		}
		stats_.state = TrackRecorderState::Recording;
	}

	~Impl() { Stop(); }

	bool Start() {
		if (!setup_error_.empty()) {
			return false;
		}
		try {
			worker_ = std::thread([this] { Run(); });
		} catch (const std::exception& exception) {
			setup_error_ = std::string("failed to start recording worker: ") + exception.what();
			Fail(setup_error_);
			return false;
		}
		return true;
	}

	const std::string& SetupError() const noexcept { return setup_error_; }

	void Stop() noexcept {
		stop_requested_.store(true);
		if (worker_.joinable()) {
			worker_.join();
		}
		if (audio_stream_) {
			audio_stream_->Close();
		}
		if (video_stream_) {
			video_stream_->Close();
		}
	}

	bool IsRecording() const noexcept {
		std::lock_guard<std::mutex> guard(stats_mutex_);
		return stats_.state == TrackRecorderState::Recording;
	}

	TrackRecorderStats Stats() const {
		std::lock_guard<std::mutex> guard(stats_mutex_);
		return stats_;
	}

private:
	void Run() noexcept {
		if (track_kind_ == TrackKind::Audio) {
			RunAudio();
		} else {
			RunVideo();
		}
		Finalize();
	}

	void RunAudio() noexcept {
		while (!stop_requested_.load()) {
			AudioFrame frame;
			if (!audio_stream_->ReadFor(frame, 20ms)) {
				if (audio_stream_->IsClosed()) {
					break;
				}
				continue;
			}
			std::string error;
			if (!audio_writer_.Write(frame, error)) {
				Fail(std::move(error));
				break;
			}
			UpdateStats(audio_writer_.FramesWritten(), audio_writer_.BytesWritten(),
			            audio_stream_->DroppedFrames(), audio_writer_.OutputPath());
		}
	}

	void RunVideo() noexcept {
		bool waiting_for_key_frame = true;
		std::size_t observed_drops = 0;
		while (!stop_requested_.load()) {
			EncodedVideoFrame frame;
			if (!video_stream_->ReadFor(frame, 20ms)) {
				if (video_stream_->IsClosed()) {
					break;
				}
				continue;
			}
			const auto dropped = video_stream_->DroppedFrames();
			if (dropped != observed_drops) {
				observed_drops = dropped;
				waiting_for_key_frame = true;
			}
			if (waiting_for_key_frame) {
				if (!frame.key_frame) {
					UpdateStats(video_writer_.FramesWritten(), video_writer_.BytesWritten(),
					            dropped, video_writer_.OutputPath());
					continue;
				}
				waiting_for_key_frame = false;
			}
			std::string error;
			if (!video_writer_.Write(options_.output_path, frame, error)) {
				Fail(std::move(error));
				break;
			}
			UpdateStats(video_writer_.FramesWritten(), video_writer_.BytesWritten(), dropped,
			            video_writer_.OutputPath());
		}
	}

	void Finalize() noexcept {
		std::string error;
		const bool finalized = track_kind_ == TrackKind::Audio ? audio_writer_.Close(error)
		                                                       : video_writer_.Close(error);
		if (!finalized) {
			Fail(std::move(error));
		}
		if (track_kind_ == TrackKind::Audio) {
			UpdateStats(audio_writer_.FramesWritten(), audio_writer_.BytesWritten(),
			            audio_stream_ ? audio_stream_->DroppedFrames() : 0,
			            audio_writer_.OutputPath());
		} else {
			UpdateStats(video_writer_.FramesWritten(), video_writer_.BytesWritten(),
			            video_stream_ ? video_stream_->DroppedFrames() : 0,
			            video_writer_.OutputPath());
		}
		std::lock_guard<std::mutex> guard(stats_mutex_);
		if (stats_.state != TrackRecorderState::Failed) {
			stats_.state = TrackRecorderState::Stopped;
		}
	}

	void Fail(std::string error) noexcept {
		std::lock_guard<std::mutex> guard(stats_mutex_);
		stats_.state = TrackRecorderState::Failed;
		stats_.error = std::move(error);
	}

	void UpdateStats(std::uint64_t frames, std::uint64_t bytes, std::uint64_t dropped,
	                 const std::string& output_path) {
		std::lock_guard<std::mutex> guard(stats_mutex_);
		stats_.frames_written = frames;
		stats_.bytes_written = bytes;
		stats_.frames_dropped = dropped;
		if (!output_path.empty()) {
			stats_.output_path = output_path;
		}
	}

	TrackKind track_kind_;
	TrackRecorderOptions options_;
	std::shared_ptr<AudioStream> audio_stream_;
	std::shared_ptr<EncodedVideoStream> video_stream_;
	recording::WavFileWriter audio_writer_;
	recording::EncodedVideoFileWriter video_writer_;
	std::atomic<bool> stop_requested_{false};
	std::thread worker_;
	mutable std::mutex stats_mutex_;
	TrackRecorderStats stats_;
	std::string setup_error_;
};

TrackRecorder::TrackRecorder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

TrackRecorder::~TrackRecorder() { Stop(); }

void TrackRecorder::Stop() noexcept {
	if (impl_) {
		impl_->Stop();
	}
}

bool TrackRecorder::IsRecording() const noexcept { return impl_ && impl_->IsRecording(); }

TrackRecorderStats TrackRecorder::Stats() const {
	return impl_ ? impl_->Stats() : TrackRecorderStats{};
}

std::unique_ptr<TrackRecorder>
StartTrackRecording(RemoteTrackInterface& track, TrackRecorderOptions options, std::string* error) {
	auto impl = std::make_unique<TrackRecorder::Impl>(track, std::move(options));
	if (!impl->Start()) {
		if (error) {
			*error = impl->SetupError();
		}
		return nullptr;
	}
	if (error) {
		error->clear();
	}
	return std::unique_ptr<TrackRecorder>(new TrackRecorder(std::move(impl)));
}

} // namespace core
} // namespace livekit
