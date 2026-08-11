#include "uri.h"
#include "websocket_uri.h"

#include <gtest/gtest.h>

#include <string>

namespace livekit::core {
namespace {

TEST(UrlTest, ParsesAndFormatsAbsoluteUrl) {
	const Url url("WSS://example.com:8443/rtc/path?token=a%20b&sdk=cpp");

	EXPECT_EQ(url.GetScheme(), "wss");
	EXPECT_EQ(url.GetHost(), "example.com");
	EXPECT_EQ(url.GetPort(), 8443);
	EXPECT_EQ(url.GetRelativeUrl(), "/rtc/path?sdk=cpp&token=a%20b");
	EXPECT_EQ(url.GetAbsoluteUrl(), "wss://example.com:8443/rtc/path?sdk=cpp&token=a%20b");
}

TEST(UrlTest, EncodesAndDecodesComponents) {
	EXPECT_EQ(Url::Decode("a%20b+c"), "a b c");
	EXPECT_EQ(Url::Encode("a b+c"), "a%20b%2Bc");
	EXPECT_THROW((void)Url::Decode("invalid%2"), std::runtime_error);
}

TEST(UrlTest, ParsesFlagsAndIgnoresFragment) {
	const Url url("ws://example.com/rtc?flag&value=1#ignored");
	EXPECT_EQ(url.GetRelativeUrl(), "/rtc?flag=&value=1");
}

void ExpectWebsocketUri(const char* input, bool secure, std::uint16_t port,
                        const char* relative_url) {
	const auto uri = WebsocketUri::parse_and_validate(input);
	EXPECT_EQ(uri.is_secure(), secure);
	EXPECT_EQ(uri.get_port(), port);
	EXPECT_EQ(uri.get_relative_url(), relative_url);
}

TEST(WebsocketUriTest, ParsesWs) { ExpectWebsocketUri("ws://localhost/rtc", false, 80, "/rtc"); }

TEST(WebsocketUriTest, ParsesWss) { ExpectWebsocketUri("wss://example.com", true, 443, "/"); }

TEST(WebsocketUriTest, UpgradesHttp) {
	ExpectWebsocketUri("http://example.com/rtc", false, 80, "/rtc");
}

TEST(WebsocketUriTest, UpgradesHttps) {
	ExpectWebsocketUri("https://example.com/rtc", true, 443, "/rtc");
}

TEST(WebsocketUriValidationTest, RejectsUnsupportedOrIncompleteUris) {
	EXPECT_THROW((void)WebsocketUri::parse_and_validate("ftp://example.com"),
	             std::invalid_argument);
	EXPECT_THROW((void)WebsocketUri::parse_and_validate("wss:///rtc"), std::invalid_argument);
	EXPECT_THROW((void)WebsocketUri::parse_and_validate(""), std::invalid_argument);
}

} // namespace
} // namespace livekit::core
