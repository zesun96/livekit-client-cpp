#include "../../src/core/track/track.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace livekit::core {
namespace {

TEST(TrackStatsTest, UsesProviderAndHandlesUnavailableStats) {
	Track track("TR_test", "test", TrackKind::Video);
	EXPECT_TRUE(track.GetRTCStats().empty());

	track.SetStatsProvider([] { return R"([{"id":"outbound","type":"outbound-rtp"}])"; });
	EXPECT_NE(track.GetRTCStats().find("outbound-rtp"), std::string::npos);

	track.SetStatsProvider([]() -> std::string { throw std::runtime_error("stats failed"); });
	EXPECT_TRUE(track.GetRTCStats().empty());

	track.SetStatsProvider({});
	EXPECT_TRUE(track.GetRTCStats().empty());
}

} // namespace
} // namespace livekit::core
