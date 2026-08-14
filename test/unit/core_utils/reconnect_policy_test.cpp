#include "livekit/core/option/reconnect_policy.h"

#include <gtest/gtest.h>

#include <chrono>

namespace livekit::core {
namespace {

TEST(ReconnectPolicyTest, UsesBoundedQuadraticBackoff) {
	DefaultReconnectPolicy policy;
	ReconnectContext context;

	context.retry_count = 0;
	EXPECT_EQ(policy.NextRetryDelay(context), std::chrono::milliseconds(0));
	context.retry_count = 1;
	EXPECT_EQ(policy.NextRetryDelay(context), std::chrono::milliseconds(300));
	context.retry_count = 2;
	EXPECT_EQ(policy.NextRetryDelay(context), std::chrono::milliseconds(1'200));
	context.retry_count = 4;
	EXPECT_EQ(policy.NextRetryDelay(context), std::chrono::milliseconds(4'800));
	context.retry_count = 5;
	EXPECT_EQ(policy.NextRetryDelay(context), std::chrono::milliseconds(7'000));
	context.retry_count = 100;
	EXPECT_EQ(policy.NextRetryDelay(context), std::chrono::milliseconds(7'000));
}

TEST(ReconnectPolicyTest, CreatesIndependentPolicyInstances) {
	auto first = CreateDefaultReconnectPolicy();
	auto second = CreateDefaultReconnectPolicy();
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_NE(first.get(), second.get());
}

} // namespace
} // namespace livekit::core
