#include "livekit/core/e2ee/key_provider.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace livekit::core {
namespace {

E2eeKey SequentialKey() {
	E2eeKey key(32);
	for (std::size_t index = 0; index < key.size(); ++index) {
		key[index] = static_cast<std::uint8_t>(index);
	}
	return key;
}

E2eeKey HexToBytes(std::string_view hex) {
	E2eeKey bytes;
	bytes.reserve(hex.size() / 2);
	const auto value = [](char digit) -> std::uint8_t {
		if (digit >= '0' && digit <= '9') {
			return static_cast<std::uint8_t>(digit - '0');
		}
		return static_cast<std::uint8_t>(digit - 'a' + 10);
	};
	for (std::size_t index = 0; index < hex.size(); index += 2) {
		bytes.push_back(
		    static_cast<std::uint8_t>((value(hex[index]) << 4) | value(hex[index + 1])));
	}
	return bytes;
}

TEST(KeyProviderTest, UsesValidatedDefaults) {
	KeyProvider provider;
	EXPECT_EQ(provider.Options().ratchet_window_size, kDefaultE2eeRatchetWindowSize);
	EXPECT_EQ(provider.Options().failure_tolerance, kDefaultE2eeFailureTolerance);
	EXPECT_EQ(provider.Options().key_ring_size, kDefaultE2eeKeyRingSize);
	EXPECT_EQ(provider.Options().key_derivation, KeyDerivationFunction::Pbkdf2Sha256);
	EXPECT_EQ(provider.Options().ratchet_salt,
	          E2eeKey(kDefaultE2eeRatchetSalt.begin(), kDefaultE2eeRatchetSalt.end()));

	KeyProviderOptions empty_salt;
	empty_salt.ratchet_salt.clear();
	KeyProvider normalized(std::move(empty_salt));
	EXPECT_FALSE(normalized.Options().ratchet_salt.empty());
}

TEST(KeyProviderTest, RejectsInvalidOptions) {
	KeyProviderOptions options;
	options.key_ring_size = 0;
	EXPECT_THROW(KeyProvider(std::move(options)), std::invalid_argument);

	options = {};
	options.key_ring_size = 257;
	EXPECT_THROW(KeyProvider(std::move(options)), std::invalid_argument);

	options = {};
	options.failure_tolerance = -2;
	EXPECT_THROW(KeyProvider(std::move(options)), std::invalid_argument);

	options = {};
	options.key_derivation = static_cast<KeyDerivationFunction>(255);
	EXPECT_THROW(KeyProvider(std::move(options)), std::invalid_argument);
}

TEST(KeyProviderTest, ValidatesInputsAndMissingKeys) {
	KeyProviderOptions options;
	options.key_ring_size = 2;
	KeyProvider provider(std::move(options));

	auto result = provider.SetSharedKey({}, 0);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, KeyProviderErrorCode::EmptyKey);

	result = provider.SetSharedKey({1}, 2);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, KeyProviderErrorCode::InvalidKeyIndex);

	result = provider.ExportSharedKey(0);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, KeyProviderErrorCode::KeyNotFound);

	result = provider.SetKey("", {1});
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, KeyProviderErrorCode::EmptyParticipantIdentity);
}

TEST(KeyProviderTest, KeepsSharedAndParticipantKeyRingsIndependent) {
	KeyProvider provider;
	ASSERT_TRUE(provider.SetSharedKey({1, 2, 3}, 1).Ok());
	ASSERT_TRUE(provider.SetKey("alice", {4, 5, 6}, 1).Ok());
	ASSERT_TRUE(provider.SetKey("bob", {7, 8, 9}, 1).Ok());

	EXPECT_EQ(provider.ExportSharedKey(1).key, (E2eeKey{1, 2, 3}));
	EXPECT_EQ(provider.ExportKey("alice", 1).key, (E2eeKey{4, 5, 6}));
	EXPECT_EQ(provider.ExportKey("bob", 1).key, (E2eeKey{7, 8, 9}));

	ASSERT_TRUE(provider.RemoveKey("alice", 1).Ok());
	EXPECT_FALSE(provider.ExportKey("alice", 1).Ok());
	EXPECT_TRUE(provider.ExportKey("bob", 1).Ok());
	ASSERT_TRUE(provider.RemoveParticipantKeys("bob").Ok());
	EXPECT_FALSE(provider.ExportKey("bob", 1).Ok());

	provider.Clear();
	EXPECT_FALSE(provider.ExportSharedKey(1).Ok());
}

TEST(KeyProviderTest, RatchetsPbkdf2KeysCompatiblyWithOfficialClientBehavior) {
	KeyProvider provider;
	ASSERT_TRUE(provider.SetSharedKey(SequentialKey()).Ok());

	const auto ratcheted = provider.RatchetSharedKey();
	ASSERT_TRUE(ratcheted.Ok());
	EXPECT_EQ(ratcheted.key,
	          HexToBytes("74884f8739e616fffc831a2172ff98b7589b553813c3610b64f2ccc54a0331ab"));
	EXPECT_EQ(provider.ExportSharedKey().key, ratcheted.key);
}

TEST(KeyProviderTest, RatchetsHkdfParticipantKeysCompatiblyWithOfficialClientBehavior) {
	KeyProviderOptions options;
	options.key_derivation = KeyDerivationFunction::HkdfSha256;
	KeyProvider provider(std::move(options));
	ASSERT_TRUE(provider.SetKey("alice", SequentialKey()).Ok());

	const auto ratcheted = provider.RatchetKey("alice");
	ASSERT_TRUE(ratcheted.Ok());
	EXPECT_EQ(ratcheted.key,
	          HexToBytes("6d0dd7b3d6f4efde47ab9a9e15abde7635c45a13bddf5da15eed01f40178d328"));
	EXPECT_EQ(provider.ExportKey("alice").key, ratcheted.key);
}

} // namespace
} // namespace livekit::core
