#include "livekit/core/recording/track_recorder.h"

#include "livekit/core/track/media_stream.h"
#include "livekit/core/track/remote_track_interface.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace livekit::core {

class MediaStreamTestAccess {
public:
	static std::shared_ptr<AudioStream> CreateAudio(std::size_t capacity) {
		return std::shared_ptr<AudioStream>(new AudioStream(capacity));
	}
	static std::shared_ptr<EncodedVideoStream> CreateVideo(std::size_t capacity) {
		return std::shared_ptr<EncodedVideoStream>(new EncodedVideoStream(capacity));
	}
	static void Push(AudioStream& stream, AudioFrame frame) { stream.Push(std::move(frame)); }
	static void Push(EncodedVideoStream& stream, EncodedVideoFrame frame) {
		stream.Push(std::move(frame));
	}
};

namespace {

using namespace std::chrono_literals;

class FakeRemoteTrack final : public RemoteTrackInterface {
public:
	explicit FakeRemoteTrack(TrackKind kind) : kind_(kind) {
		if (kind == TrackKind::Audio) {
			audio_stream_ = MediaStreamTestAccess::CreateAudio(32);
		} else if (kind == TrackKind::Video) {
			video_stream_ = MediaStreamTestAccess::CreateVideo(32);
		}
	}

	std::string GetRTCStats() override { return {}; }
	void SetEnabled(bool enabled) override { enabled_ = enabled; }
	bool IsEnabled() override { return enabled_; }
	TrackKind Kind() override { return kind_; }
	TrackSource Source() override { return TrackSource::Unknown; }
	std::string Sid() override { return "track"; }
	std::string Name() override { return "recording-test"; }
	TrackStreamState StreamState() override { return TrackStreamState::Active; }
	TrackDimensions Dimensions() override { return {}; }

	std::shared_ptr<AudioStream> CreateAudioStream(MediaStreamOptions) override {
		return audio_stream_;
	}
	std::shared_ptr<EncodedVideoStream> CreateEncodedVideoStream(MediaStreamOptions) override {
		return video_stream_;
	}

	void Push(AudioFrame frame) { MediaStreamTestAccess::Push(*audio_stream_, std::move(frame)); }
	void Push(EncodedVideoFrame frame) {
		MediaStreamTestAccess::Push(*video_stream_, std::move(frame));
	}

private:
	TrackKind kind_;
	bool enabled_ = true;
	std::shared_ptr<AudioStream> audio_stream_;
	std::shared_ptr<EncodedVideoStream> video_stream_;
};

std::filesystem::path TemporaryBasePath(const std::string& name) {
	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	return std::filesystem::temp_directory_path() /
	       ("livekit-recorder-" + name + "-" + std::to_string(unique));
}

bool WaitForFrames(const TrackRecorder& recorder, std::uint64_t expected) {
	for (int attempt = 0; attempt < 200; ++attempt) {
		if (recorder.Stats().frames_written >= expected) {
			return true;
		}
		std::this_thread::sleep_for(5ms);
	}
	return false;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint32_t ReadLittleEndian32(const std::vector<std::uint8_t>& data, std::size_t offset) {
	return static_cast<std::uint32_t>(data[offset]) |
	       (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
	       (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
	       (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

TEST(TrackRecorderTest, WritesAndFinalizesRemotePcmAsWav) {
	FakeRemoteTrack track(TrackKind::Audio);
	const auto base_path = TemporaryBasePath("audio");
	TrackRecorderOptions options;
	options.output_path = base_path.string();
	std::string error;
	auto recorder = StartTrackRecording(track, options, &error);
	ASSERT_NE(recorder, nullptr) << error;

	AudioFrame frame;
	frame.sample_rate = 48000;
	frame.num_channels = 1;
	frame.samples_per_channel = 4;
	frame.data = {1, -2, 3, -4};
	track.Push(frame);
	track.Push(frame);
	ASSERT_TRUE(WaitForFrames(*recorder, 2));
	recorder->Stop();

	const auto stats = recorder->Stats();
	EXPECT_EQ(stats.state, TrackRecorderState::Stopped);
	EXPECT_EQ(stats.frames_written, 2u);
	EXPECT_EQ(stats.bytes_written, 16u);
	EXPECT_TRUE(stats.error.empty());
	const auto data = ReadFile(stats.output_path);
	ASSERT_EQ(data.size(), 60u);
	EXPECT_EQ(std::string(data.begin(), data.begin() + 4), "RIFF");
	EXPECT_EQ(std::string(data.begin() + 8, data.begin() + 12), "WAVE");
	EXPECT_EQ(ReadLittleEndian32(data, 24), 48000u);
	EXPECT_EQ(ReadLittleEndian32(data, 40), 16u);
	std::filesystem::remove(stats.output_path);
}

TEST(TrackRecorderTest, WaitsForKeyFrameAndWritesTimestampedIvf) {
	FakeRemoteTrack track(TrackKind::Video);
	const auto base_path = TemporaryBasePath("video");
	TrackRecorderOptions options;
	options.output_path = base_path.string();
	auto recorder = StartTrackRecording(track, options);
	ASSERT_NE(recorder, nullptr);

	EncodedVideoFrame delta;
	delta.codec = EncodedVideoCodec::VP8;
	delta.width = 640;
	delta.height = 360;
	delta.timestamp_us = 1000000;
	delta.data = {9};
	track.Push(delta);

	EncodedVideoFrame key = delta;
	key.key_frame = true;
	key.timestamp_us = 1033000;
	key.data = {1, 2, 3};
	track.Push(key);
	delta.timestamp_us = 1066000;
	delta.data = {4, 5};
	track.Push(delta);
	ASSERT_TRUE(WaitForFrames(*recorder, 2));
	recorder->Stop();

	const auto stats = recorder->Stats();
	EXPECT_EQ(stats.state, TrackRecorderState::Stopped);
	EXPECT_EQ(stats.frames_written, 2u);
	EXPECT_EQ(stats.bytes_written, 5u);
	EXPECT_EQ(std::filesystem::path(stats.output_path).extension(), ".ivf");
	const auto data = ReadFile(stats.output_path);
	ASSERT_EQ(data.size(), 61u);
	EXPECT_EQ(std::string(data.begin(), data.begin() + 4), "DKIF");
	EXPECT_EQ(std::string(data.begin() + 8, data.begin() + 12), "VP80");
	EXPECT_EQ(ReadLittleEndian32(data, 24), 2u);
	EXPECT_EQ(ReadLittleEndian32(data, 32), 3u);
	EXPECT_EQ(ReadLittleEndian32(data, 47), 2u);
	std::filesystem::remove(stats.output_path);
}

TEST(TrackRecorderTest, WritesH264AnnexBWithoutTranscoding) {
	FakeRemoteTrack track(TrackKind::Video);
	TrackRecorderOptions options;
	options.output_path = TemporaryBasePath("h264").string();
	auto recorder = StartTrackRecording(track, options);
	ASSERT_NE(recorder, nullptr);

	EncodedVideoFrame frame;
	frame.codec = EncodedVideoCodec::H264;
	frame.key_frame = true;
	frame.width = 1280;
	frame.height = 720;
	frame.timestamp_us = 1000;
	frame.data = {0, 0, 0, 1, 0x65, 0x01, 0x02};
	track.Push(frame);
	ASSERT_TRUE(WaitForFrames(*recorder, 1));
	recorder->Stop();

	const auto stats = recorder->Stats();
	EXPECT_EQ(stats.state, TrackRecorderState::Stopped);
	EXPECT_EQ(stats.frames_written, 1u);
	EXPECT_EQ(std::filesystem::path(stats.output_path).extension(), ".h264");
	EXPECT_EQ(ReadFile(stats.output_path), frame.data);
	std::filesystem::remove(stats.output_path);
}

TEST(TrackRecorderTest, RejectsZeroCapacityBeforeStarting) {
	FakeRemoteTrack track(TrackKind::Audio);
	TrackRecorderOptions options;
	options.output_path = TemporaryBasePath("invalid").string();
	options.queue_capacity = 0;
	std::string error;
	EXPECT_EQ(StartTrackRecording(track, options, &error), nullptr);
	EXPECT_NE(error.find("capacity"), std::string::npos);
}

} // namespace
} // namespace livekit::core
