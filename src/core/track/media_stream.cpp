/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "livekit/core/track/media_stream.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace livekit {
namespace core {
namespace {

template <typename Frame> class FrameQueue {
public:
	explicit FrameQueue(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) {}

	bool Read(Frame& frame, std::optional<std::chrono::milliseconds> timeout) {
		std::unique_lock<std::mutex> guard(mutex_);
		if (timeout) {
			if (!cv_.wait_for(guard, *timeout, [this] { return closed_ || !frames_.empty(); })) {
				return false;
			}
		} else {
			cv_.wait(guard, [this] { return closed_ || !frames_.empty(); });
		}
		if (frames_.empty()) {
			return false;
		}
		frame = std::move(frames_.front());
		frames_.pop_front();
		return true;
	}

	bool TryRead(Frame& frame) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (frames_.empty()) {
			return false;
		}
		frame = std::move(frames_.front());
		frames_.pop_front();
		return true;
	}

	void Push(Frame frame) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (closed_) {
			return;
		}
		if (frames_.size() >= capacity_) {
			frames_.pop_front();
			++dropped_frames_;
		}
		frames_.push_back(std::move(frame));
		cv_.notify_one();
	}

	void Close() {
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (closed_) {
				return;
			}
			closed_ = true;
			frames_.clear();
		}
		cv_.notify_all();
	}

	bool IsClosed() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return closed_;
	}

	std::size_t DroppedFrames() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return dropped_frames_;
	}

private:
	const std::size_t capacity_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<Frame> frames_;
	std::size_t dropped_frames_ = 0;
	bool closed_ = false;
};

} // namespace

class AudioStream::Impl : public FrameQueue<AudioFrame> {
public:
	using FrameQueue::FrameQueue;
};

class VideoStream::Impl : public FrameQueue<VideoFrame> {
public:
	using FrameQueue::FrameQueue;
};

class EncodedVideoStream::Impl : public FrameQueue<EncodedVideoFrame> {
public:
	using FrameQueue::FrameQueue;

	void SetAttachment(std::shared_ptr<void> attachment) {
		std::lock_guard<std::mutex> guard(attachment_mutex_);
		attachment_ = std::move(attachment);
	}

	void Detach() {
		std::shared_ptr<void> attachment;
		{
			std::lock_guard<std::mutex> guard(attachment_mutex_);
			attachment = std::move(attachment_);
		}
		attachment.reset();
	}

private:
	std::mutex attachment_mutex_;
	std::shared_ptr<void> attachment_;
};

AudioStream::AudioStream(std::size_t capacity) : impl_(std::make_shared<Impl>(capacity)) {}

AudioStream::~AudioStream() { Close(); }

bool AudioStream::Read(AudioFrame& frame) { return impl_ && impl_->Read(frame, std::nullopt); }

bool AudioStream::ReadFor(AudioFrame& frame, std::chrono::milliseconds timeout) {
	return impl_ && timeout >= std::chrono::milliseconds::zero() && impl_->Read(frame, timeout);
}

bool AudioStream::TryRead(AudioFrame& frame) { return impl_ && impl_->TryRead(frame); }

void AudioStream::Close() {
	if (impl_) {
		impl_->Close();
	}
}

bool AudioStream::IsClosed() const { return !impl_ || impl_->IsClosed(); }

std::size_t AudioStream::DroppedFrames() const { return impl_ ? impl_->DroppedFrames() : 0; }

void AudioStream::Push(AudioFrame frame) {
	if (impl_) {
		impl_->Push(std::move(frame));
	}
}

VideoStream::VideoStream(std::size_t capacity) : impl_(std::make_shared<Impl>(capacity)) {}

VideoStream::~VideoStream() { Close(); }

bool VideoStream::Read(VideoFrame& frame) { return impl_ && impl_->Read(frame, std::nullopt); }

bool VideoStream::ReadFor(VideoFrame& frame, std::chrono::milliseconds timeout) {
	return impl_ && timeout >= std::chrono::milliseconds::zero() && impl_->Read(frame, timeout);
}

bool VideoStream::TryRead(VideoFrame& frame) { return impl_ && impl_->TryRead(frame); }

void VideoStream::Close() {
	if (impl_) {
		impl_->Close();
	}
}

bool VideoStream::IsClosed() const { return !impl_ || impl_->IsClosed(); }

std::size_t VideoStream::DroppedFrames() const { return impl_ ? impl_->DroppedFrames() : 0; }

void VideoStream::Push(VideoFrame frame) {
	if (impl_) {
		impl_->Push(std::move(frame));
	}
}

EncodedVideoStream::EncodedVideoStream(std::size_t capacity)
    : impl_(std::make_shared<Impl>(capacity)) {}

EncodedVideoStream::~EncodedVideoStream() { Close(); }

bool EncodedVideoStream::Read(EncodedVideoFrame& frame) {
	return impl_ && impl_->Read(frame, std::nullopt);
}

bool EncodedVideoStream::ReadFor(EncodedVideoFrame& frame, std::chrono::milliseconds timeout) {
	return impl_ && timeout >= std::chrono::milliseconds::zero() && impl_->Read(frame, timeout);
}

bool EncodedVideoStream::TryRead(EncodedVideoFrame& frame) {
	return impl_ && impl_->TryRead(frame);
}

void EncodedVideoStream::Close() {
	if (impl_) {
		impl_->Detach();
		impl_->Close();
	}
}

bool EncodedVideoStream::IsClosed() const { return !impl_ || impl_->IsClosed(); }

std::size_t EncodedVideoStream::DroppedFrames() const { return impl_ ? impl_->DroppedFrames() : 0; }

void EncodedVideoStream::Push(EncodedVideoFrame frame) {
	if (impl_) {
		impl_->Push(std::move(frame));
	}
}

void EncodedVideoStream::SetAttachment(std::shared_ptr<void> attachment) {
	if (impl_) {
		impl_->SetAttachment(std::move(attachment));
	}
}

} // namespace core
} // namespace livekit
