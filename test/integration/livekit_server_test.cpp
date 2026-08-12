#include "livekit/core/livekit_client.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

namespace livekit::core {
namespace {

class ClientRuntime {
public:
	ClientRuntime() : initialized_(Init()) {}
	~ClientRuntime() {
		if (initialized_) {
			Destroy();
		}
	}

	bool initialized() const { return initialized_; }

private:
	bool initialized_;
};

bool WaitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	do {
		if (predicate()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	} while (std::chrono::steady_clock::now() < deadline);
	return predicate();
}

TEST(LiveKitServerTest, ConnectsWithEnvironmentCredentials) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* token = std::getenv("LIVEKIT_TOKEN_SINGLE");
	if (url == nullptr || token == nullptr || *url == '\0' || *token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL and LIVEKIT_TOKEN_SINGLE to run the single-client "
		                "integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto room = CreateRoomUnique();
	ASSERT_NE(room, nullptr);
	ASSERT_TRUE(room->Connect(url, token));
	ASSERT_NE(room->GetLocalParticipant(), nullptr);
	EXPECT_FALSE(room->GetLocalParticipant()->Sid().empty());
	EXPECT_FALSE(room->GetLocalParticipant()->Identity().empty());
	EXPECT_TRUE(room->Disconnect());
	EXPECT_FALSE(room->IsConnected());
}

TEST(LiveKitServerTest, SynchronizesParticipantJoinAndLeave) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* first_token = std::getenv("LIVEKIT_TOKEN");
	const char* second_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || first_token == nullptr || second_token == nullptr || *url == '\0' ||
	    *first_token == '\0' || *second_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the "
		                "participant integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto first_room = CreateRoomUnique();
	auto second_room = CreateRoomUnique();
	ASSERT_TRUE(first_room->Connect(url, first_token));
	ASSERT_TRUE(second_room->Connect(url, second_token));

	auto* first_local = first_room->GetLocalParticipant();
	auto* second_local = second_room->GetLocalParticipant();
	ASSERT_NE(first_local, nullptr);
	ASSERT_NE(second_local, nullptr);
	ASSERT_FALSE(first_local->Sid().empty());
	ASSERT_FALSE(second_local->Sid().empty());
	ASSERT_NE(first_local->Identity(), second_local->Identity());

	ASSERT_TRUE(WaitUntil(
	    [&] { return first_room->GetRemoteParticipantBySid(second_local->Sid()) != nullptr; }));
	ASSERT_TRUE(WaitUntil(
	    [&] { return second_room->GetRemoteParticipantBySid(first_local->Sid()) != nullptr; }));
	EXPECT_EQ(first_room->GetRemoteParticipantBySid(second_local->Sid())->Identity(),
	          second_local->Identity());
	EXPECT_EQ(second_room->GetRemoteParticipantBySid(first_local->Sid())->Identity(),
	          first_local->Identity());

	ASSERT_TRUE(second_room->Disconnect());
	EXPECT_TRUE(WaitUntil(
	    [&] { return first_room->GetRemoteParticipantBySid(second_local->Sid()) == nullptr; }));
	EXPECT_TRUE(first_room->Disconnect());
}

} // namespace
} // namespace livekit::core
