#include "livekit/core/track/media_stream.h"

#include <gtest/gtest.h>

#include <future>

namespace livekit::core {

class MediaStreamTestAccess {
public:
	static std::shared_ptr<AudioStream> CreateAudio(std::size_t capacity) {
		return std::shared_ptr<AudioStream>(new AudioStream(capacity));
	}
	static std::shared_ptr<VideoStream> CreateVideo(std::size_t capacity) {
		return std::shared_ptr<VideoStream>(new VideoStream(capacity));
	}
	static void Push(AudioStream& stream, AudioFrame frame) { stream.Push(std::move(frame)); }
	static void Push(VideoStream& stream, VideoFrame frame) { stream.Push(std::move(frame)); }
};

namespace {

TEST(MediaStreamTest, AudioStreamDropsOldestFrameAndSupportsEveryReadMode) {
	auto stream = MediaStreamTestAccess::CreateAudio(2);
	ASSERT_NE(stream, nullptr);
	for (std::int16_t sample = 1; sample <= 3; ++sample) {
		AudioFrame frame;
		frame.data = {sample};
		frame.sample_rate = 48000;
		frame.num_channels = 1;
		frame.samples_per_channel = 1;
		MediaStreamTestAccess::Push(*stream, std::move(frame));
	}
	EXPECT_EQ(stream->DroppedFrames(), 1u);

	AudioFrame frame;
	ASSERT_TRUE(stream->TryRead(frame));
	EXPECT_EQ(frame.data, (std::vector<std::int16_t>{2}));
	ASSERT_TRUE(stream->ReadFor(frame, std::chrono::milliseconds(1)));
	EXPECT_EQ(frame.data, (std::vector<std::int16_t>{3}));
	EXPECT_FALSE(stream->TryRead(frame));
	EXPECT_FALSE(stream->ReadFor(frame, std::chrono::milliseconds(-1)));
}

TEST(MediaStreamTest, CloseWakesBlockedReaderAndRejectsLaterFrames) {
	auto stream = MediaStreamTestAccess::CreateVideo(1);
	ASSERT_NE(stream, nullptr);
	auto read = std::async(std::launch::async, [stream] {
		VideoFrame frame;
		return stream->Read(frame);
	});
	stream->Close();
	EXPECT_FALSE(read.get());
	EXPECT_TRUE(stream->IsClosed());

	VideoFrame frame;
	frame.width = 2;
	frame.height = 2;
	frame.data.resize(6, 128);
	MediaStreamTestAccess::Push(*stream, std::move(frame));
	VideoFrame received;
	EXPECT_FALSE(stream->TryRead(received));
	stream->Close();
}

TEST(MediaStreamTest, VideoStreamKeepsNewestOwnedFrame) {
	auto stream = MediaStreamTestAccess::CreateVideo(1);
	VideoFrame first;
	first.width = 2;
	first.height = 2;
	first.timestamp_us = 10;
	first.data.resize(6, 10);
	VideoFrame second = first;
	second.timestamp_us = 20;
	second.data.assign(6, 20);
	MediaStreamTestAccess::Push(*stream, std::move(first));
	MediaStreamTestAccess::Push(*stream, std::move(second));

	VideoFrame received;
	ASSERT_TRUE(stream->ReadFor(received, std::chrono::milliseconds(1)));
	EXPECT_EQ(received.timestamp_us, 20);
	EXPECT_EQ(received.data, (std::vector<std::uint8_t>(6, 20)));
	EXPECT_EQ(stream->DroppedFrames(), 1u);
}

} // namespace
} // namespace livekit::core
