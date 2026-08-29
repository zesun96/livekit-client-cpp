#include "livekit/core/token_source.h"

#include <gtest/gtest.h>

using namespace livekit::core;

namespace {

// Payload: {"exp":4102444800,"nbf":0}; signature validity is intentionally outside the source's
// responsibility because the LiveKit server verifies it during join.
constexpr auto kReusableJwt = "e30.eyJleHAiOjQxMDI0NDQ4MDAsIm5iZiI6MH0.signature";

TEST(TokenSourceTest, LiteralSourceReturnsConfiguredCredentials) {
	auto source = CreateLiteralTokenSource("ws://localhost:7880", "token");
	auto result = source->Fetch({});
	ASSERT_TRUE(result);
	EXPECT_EQ(result.response.server_url, "ws://localhost:7880");
	EXPECT_EQ(result.response.participant_token, "token");
}

TEST(TokenSourceTest, CallbackCachesReusableJwtForEqualOptions) {
	int calls = 0;
	auto source =
	    CreateCallbackTokenSource([&](const TokenSourceFetchOptions& options, bool force) {
		    ++calls;
		    EXPECT_EQ(options.room_name, "room");
		    EXPECT_EQ(force, calls == 2);
		    return TokenSourceResult{{"ws://localhost:7880", kReusableJwt}, {}};
	    });
	TokenSourceFetchOptions options;
	options.room_name = "room";
	EXPECT_TRUE(source->Fetch(options));
	EXPECT_TRUE(source->Fetch(options));
	EXPECT_EQ(calls, 1);
	EXPECT_TRUE(source->Fetch(options, true));
	EXPECT_EQ(calls, 2);
}

TEST(TokenSourceTest, CallbackDoesNotCacheOpaqueOrInvalidResults) {
	int calls = 0;
	auto source = CreateCallbackTokenSource([&](const TokenSourceFetchOptions&, bool) {
		++calls;
		return calls == 1 ? TokenSourceResult{{"", "opaque"}, {}}
		                  : TokenSourceResult{{"ws://localhost:7880", "opaque"}, {}};
	});
	EXPECT_FALSE(source->Fetch({}));
	EXPECT_TRUE(source->Fetch({}));
	EXPECT_TRUE(source->Fetch({}));
	EXPECT_EQ(calls, 3);
}

} // namespace
