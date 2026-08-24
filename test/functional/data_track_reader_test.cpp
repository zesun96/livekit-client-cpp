#include "../../src/core/data_track.h"

#include <gtest/gtest.h>

namespace livekit::core {
namespace {

TEST(DataTrackReaderTest, ProvidesBoundedPullBasedFanoutAndLifecycle) {
	DataTrackInfo info;
	info.publisher_handle = 4;
	info.sid = "DT_test";
	info.name = "telemetry";
	int subscribe_calls = 0;
	int unsubscribe_calls = 0;
	auto track = std::make_shared<RemoteDataTrack>(
	    info, "publisher",
	    [&](const std::string& sid, bool subscribe, const DataTrackSubscriptionOptions& options) {
		    EXPECT_EQ(sid, info.sid);
		    EXPECT_EQ(options.max_partial_frames, subscribe ? 2u : 1u);
		    subscribe ? ++subscribe_calls : ++unsubscribe_calls;
		    return true;
	    });
	DataTrackSubscriptionOptions options;
	options.buffer_capacity = 2;
	options.max_partial_frames = 2;
	auto reader = track->Subscribe(options);
	ASSERT_NE(reader, nullptr);
	EXPECT_EQ(subscribe_calls, 1);

	track->PushFrame({{1}, 10});
	track->PushFrame({{2}, 20});
	track->PushFrame({{3}, 30});
	EXPECT_EQ(reader->DroppedFrames(), 1u);
	DataTrackFrame frame;
	ASSERT_TRUE(reader->TryRead(frame));
	EXPECT_EQ(frame.payload, std::vector<uint8_t>({2}));
	ASSERT_TRUE(reader->ReadFor(frame, std::chrono::milliseconds(1)));
	EXPECT_EQ(frame.user_timestamp, 30);
	EXPECT_FALSE(reader->TryRead(frame));

	reader->Close();
	EXPECT_TRUE(reader->IsClosed());
	EXPECT_EQ(unsubscribe_calls, 1);
	track->MarkUnpublished();
	EXPECT_FALSE(track->IsPublished());
	EXPECT_EQ(track->Subscribe(options), nullptr);
}

TEST(DataTrackReaderTest, ClosesAllReadersWhenTrackIsUnpublished) {
	DataTrackInfo info;
	info.sid = "DT_close";
	auto track = std::make_shared<RemoteDataTrack>(
	    info, "publisher",
	    [](const std::string&, bool, const DataTrackSubscriptionOptions&) { return true; });
	auto first = track->Subscribe();
	auto second = track->Subscribe();
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	track->MarkUnpublished();
	EXPECT_TRUE(first->IsClosed());
	EXPECT_TRUE(second->IsClosed());
	DataTrackFrame frame;
	EXPECT_FALSE(first->ReadFor(frame, std::chrono::milliseconds(1)));
}

} // namespace
} // namespace livekit::core
