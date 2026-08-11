#include "livekit/core/livekit_client.h"

#include <gtest/gtest.h>

namespace livekit::core {
namespace {

TEST(ClientLifecycleTest, InitAndDestroyAreReferenceCounted) {
	ASSERT_TRUE(Init());
	ASSERT_TRUE(Init());
	EXPECT_TRUE(Destroy());
	EXPECT_TRUE(Destroy());
	EXPECT_TRUE(Destroy());
}

TEST(PublicApiTest, CreatesOwnedDisconnectedRoom) {
	auto room = CreateRoomUnique();
	ASSERT_NE(room, nullptr);
	EXPECT_FALSE(room->IsConnected());
	EXPECT_NE(room->GetLocalParticipant(), nullptr);
}

TEST(PublicApiTest, ExposesSemanticVersion) { EXPECT_EQ(Version(), "0.0.1"); }

} // namespace
} // namespace livekit::core
