/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifndef _LKC_CORE_TRACK_MEDIA_STREAM_H_
#define _LKC_CORE_TRACK_MEDIA_STREAM_H_

#include "audio_frame.h"
#include "encoded_video_frame.h"
#include "video_frame.h"

#include <chrono>
#include <cstddef>
#include <memory>

namespace livekit {
namespace core {

struct MediaStreamOptions {
	// The oldest unread frame is dropped when the bounded queue is full.
	std::size_t capacity = 16;
};

class AudioStream {
public:
	~AudioStream();
	AudioStream(const AudioStream&) = delete;
	AudioStream& operator=(const AudioStream&) = delete;

	bool Read(AudioFrame& frame);
	bool ReadFor(AudioFrame& frame, std::chrono::milliseconds timeout);
	bool TryRead(AudioFrame& frame);
	void Close();
	bool IsClosed() const;
	std::size_t DroppedFrames() const;

private:
	class Impl;
	explicit AudioStream(std::size_t capacity);
	void Push(AudioFrame frame);
	std::shared_ptr<Impl> impl_;

	friend class MediaStreamTestAccess;
	friend class RemoteAudioTrack;
};

class VideoStream {
public:
	~VideoStream();
	VideoStream(const VideoStream&) = delete;
	VideoStream& operator=(const VideoStream&) = delete;

	bool Read(VideoFrame& frame);
	bool ReadFor(VideoFrame& frame, std::chrono::milliseconds timeout);
	bool TryRead(VideoFrame& frame);
	void Close();
	bool IsClosed() const;
	std::size_t DroppedFrames() const;

private:
	class Impl;
	explicit VideoStream(std::size_t capacity);
	void Push(VideoFrame frame);
	std::shared_ptr<Impl> impl_;

	friend class MediaStreamTestAccess;
	friend class RemoteVideoTrack;
};

class EncodedVideoStream {
public:
	~EncodedVideoStream();
	EncodedVideoStream(const EncodedVideoStream&) = delete;
	EncodedVideoStream& operator=(const EncodedVideoStream&) = delete;

	bool Read(EncodedVideoFrame& frame);
	bool ReadFor(EncodedVideoFrame& frame, std::chrono::milliseconds timeout);
	bool TryRead(EncodedVideoFrame& frame);
	void Close();
	bool IsClosed() const;
	std::size_t DroppedFrames() const;

private:
	class Impl;
	explicit EncodedVideoStream(std::size_t capacity);
	void Push(EncodedVideoFrame frame);
	void SetAttachment(std::shared_ptr<void> attachment);
	std::shared_ptr<Impl> impl_;

	friend class MediaStreamTestAccess;
	friend class EncodedVideoSubscription;
	friend class RemoteVideoTrack;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_MEDIA_STREAM_H_
