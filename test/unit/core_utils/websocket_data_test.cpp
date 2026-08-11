#include "websocket_data.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace livekit::core {
namespace {

TEST(WebsocketDataTest, OwnsPayloadAndPreservesType) {
	const std::string payload = "payload";
	WebsocketData data(payload.data(), payload.size(), WebsocketDataType::Binary);

	EXPECT_EQ(data.size(), payload.size());
	EXPECT_EQ(data.type(), WebsocketDataType::Binary);
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(data.data()), data.size()), payload);
}

TEST(WebsocketDataTest, AcceptsEmptyPayload) {
	const WebsocketData data(nullptr, 0, WebsocketDataType::Text);
	EXPECT_TRUE(data.empty());
	EXPECT_EQ(data.size(), 0U);
}

TEST(WebsocketDataTest, RejectsNullNonEmptyPayload) {
	EXPECT_THROW((WebsocketData(nullptr, 1, WebsocketDataType::Binary)), std::invalid_argument);
}

} // namespace
} // namespace livekit::core
