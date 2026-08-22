#include "livekit/core/e2ee/e2ee_manager.h"
#include "livekit/core/room_interface.h"

#include "e2ee_manager_internal.h"
#include "key_provider_internal.h"

#include "api/crypto/frame_crypto_transformer.h"
#include "api/make_ref_counted.h"
#include "rtc_base/thread.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace webrtc {

class MockTransformableAudioFrame final : public TransformableAudioFrameInterface {
public:
	MockTransformableAudioFrame(std::vector<std::uint8_t> data, Direction direction)
	    : TransformableAudioFrameInterface(Passkey()), data_(std::move(data)),
	      direction_(direction) {}

	ArrayView<const std::uint8_t> GetData() const override { return data_; }
	void SetData(ArrayView<const std::uint8_t> data) override {
		data_.assign(data.begin(), data.end());
	}
	std::uint8_t GetPayloadType() const override { return 111; }
	std::uint32_t GetSsrc() const override { return 42; }
	std::uint32_t GetTimestamp() const override { return timestamp_; }
	void SetRTPTimestamp(std::uint32_t timestamp) override { timestamp_ = timestamp; }
	Direction GetDirection() const override { return direction_; }
	std::string GetMimeType() const override { return "audio/opus"; }
	std::optional<Timestamp> ReceiveTime() const override { return std::nullopt; }
	std::optional<Timestamp> CaptureTime() const override { return std::nullopt; }
	std::optional<TimeDelta> SenderCaptureTimeOffset() const override { return std::nullopt; }
	ArrayView<const std::uint32_t> GetContributingSources() const override { return sources_; }
	const std::optional<std::uint16_t> SequenceNumber() const override { return 1; }
	std::optional<std::uint64_t> AbsoluteCaptureTimestamp() const override { return std::nullopt; }
	std::optional<std::uint8_t> AudioLevel() const override { return std::nullopt; }

private:
	std::vector<std::uint8_t> data_;
	std::vector<std::uint32_t> sources_;
	Direction direction_;
	std::uint32_t timestamp_ = 1234;
};

class MockTransformableVideoFrame final : public TransformableVideoFrameInterface {
public:
	MockTransformableVideoFrame(std::vector<std::uint8_t> data, Direction direction,
	                            VideoCodecType codec, bool key_frame)
	    : TransformableVideoFrameInterface(Passkey()), data_(std::move(data)),
	      direction_(direction), key_frame_(key_frame) {
		header_.codec = codec;
	}

	ArrayView<const std::uint8_t> GetData() const override { return data_; }
	void SetData(ArrayView<const std::uint8_t> data) override {
		data_.assign(data.begin(), data.end());
	}
	std::uint8_t GetPayloadType() const override { return 96; }
	std::uint32_t GetSsrc() const override { return 84; }
	std::uint32_t GetTimestamp() const override { return timestamp_; }
	void SetRTPTimestamp(std::uint32_t timestamp) override { timestamp_ = timestamp; }
	Direction GetDirection() const override { return direction_; }
	std::string GetMimeType() const override { return "video/test"; }
	std::optional<Timestamp> ReceiveTime() const override { return std::nullopt; }
	std::optional<Timestamp> CaptureTime() const override { return std::nullopt; }
	std::optional<TimeDelta> SenderCaptureTimeOffset() const override { return std::nullopt; }
	bool IsKeyFrame() const override { return key_frame_; }
	VideoFrameMetadata Metadata() const override { return metadata_; }
	void SetMetadata(const VideoFrameMetadata& metadata) override { metadata_ = metadata; }
	const RTPVideoHeader& header() const override { return header_; }

private:
	std::vector<std::uint8_t> data_;
	Direction direction_;
	bool key_frame_;
	std::uint32_t timestamp_ = 5678;
	RTPVideoHeader header_;
	VideoFrameMetadata metadata_;
};

class CapturingFrameCallback : public TransformedFrameCallback {
public:
	explicit CapturingFrameCallback(std::shared_ptr<std::promise<std::vector<std::uint8_t>>> result)
	    : result_(std::move(result)) {}

	void OnTransformedFrame(std::unique_ptr<TransformableFrameInterface> frame) override {
		const auto data = frame->GetData();
		result_->set_value({data.begin(), data.end()});
	}

private:
	std::shared_ptr<std::promise<std::vector<std::uint8_t>>> result_;
};

} // namespace webrtc

namespace livekit::core {
namespace {

std::vector<std::uint8_t>
TransformAudioFrame(const webrtc::scoped_refptr<webrtc::FrameCryptorTransformer>& transformer,
                    std::vector<std::uint8_t> data,
                    webrtc::TransformableFrameInterface::Direction direction) {
	auto promise = std::make_shared<std::promise<std::vector<std::uint8_t>>>();
	auto future = promise->get_future();
	auto callback = webrtc::make_ref_counted<webrtc::CapturingFrameCallback>(promise);
	webrtc::scoped_refptr<webrtc::FrameTransformerInterface> public_transformer(transformer);
	public_transformer->RegisterTransformedFrameCallback(callback);
	public_transformer->Transform(
	    std::make_unique<webrtc::MockTransformableAudioFrame>(std::move(data), direction));
	EXPECT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
	return future.get();
}

std::vector<std::uint8_t>
TransformVideoFrame(const webrtc::scoped_refptr<webrtc::FrameCryptorTransformer>& transformer,
                    std::vector<std::uint8_t> data,
                    webrtc::TransformableFrameInterface::Direction direction,
                    webrtc::VideoCodecType codec, bool key_frame) {
	auto promise = std::make_shared<std::promise<std::vector<std::uint8_t>>>();
	auto future = promise->get_future();
	auto callback = webrtc::make_ref_counted<webrtc::CapturingFrameCallback>(promise);
	webrtc::scoped_refptr<webrtc::FrameTransformerInterface> public_transformer(transformer);
	public_transformer->RegisterTransformedFrameSinkCallback(callback, 84);
	public_transformer->Transform(std::make_unique<webrtc::MockTransformableVideoFrame>(
	    std::move(data), direction, codec, key_frame));
	EXPECT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
	return future.get();
}

TEST(E2EEManagerTest, OwnsInitialSharedKeyAndGlobalState) {
	E2eeOptions options;
	options.shared_key = E2eeKey{1, 2, 3, 4};
	E2EEManager manager(options);

	EXPECT_TRUE(manager.Enabled());
	ASSERT_TRUE(manager.Keys().ExportSharedKey().Ok());
	EXPECT_EQ(manager.Keys().ExportSharedKey().key, *options.shared_key);
	EXPECT_TRUE(manager.FrameCryptors().empty());
	EXPECT_FALSE(manager.SetFrameCryptorEnabled("missing", FrameCryptorDirection::Sender, false));
	EXPECT_FALSE(manager.SetFrameCryptorKeyIndex("missing", FrameCryptorDirection::Receiver, 1));
	EXPECT_EQ(manager.SetParticipantEnabled("alice", false), 0u);

	manager.SetEnabled(false);
	EXPECT_FALSE(manager.Enabled());
}

TEST(E2EEManagerTest, RejectsUnsupportedOrEmptyInitialEncryptionConfiguration) {
	E2eeOptions options;
	options.encryption_type = EncryptionType::Custom;
	EXPECT_THROW(E2EEManager manager(options), std::invalid_argument);

	options.encryption_type = EncryptionType::Gcm;
	options.shared_key = E2eeKey{};
	EXPECT_THROW(E2EEManager manager(options), std::invalid_argument);
}

TEST(E2EEManagerTest, RoomOwnsManagerWithMainstreamNonOwningAccess) {
	RoomOptions options;
	options.e2ee = E2eeOptions{};
	options.e2ee->shared_key = E2eeKey{9, 8, 7, 6};
	auto room = CreateRoomUnique(options);

	auto* manager = room->GetE2EEManager();
	ASSERT_NE(manager, nullptr);
	EXPECT_EQ(manager->Keys().ExportSharedKey().key, *options.e2ee->shared_key);
}

TEST(E2EEManagerTest, NativeAesGcmDataCryptorRoundTripsWithDerivedSharedKey) {
	KeyProvider provider;
	ASSERT_TRUE(provider.SetSharedKey({0, 1, 2, 3, 4, 5, 6, 7}).Ok());
	auto cryptor = webrtc::make_ref_counted<webrtc::DataPacketCryptor>(
	    webrtc::FrameCryptorTransformer::Algorithm::kAesGcm,
	    KeyProviderNativeAccess::Get(provider));
	const std::vector<std::uint8_t> plaintext{0, 1, 2, 3, 4, 0, 255, 42};

	auto encrypted = cryptor->Encrypt("alice", 0, plaintext);
	ASSERT_TRUE(encrypted.ok());
	ASSERT_NE(encrypted.value(), nullptr);
	EXPECT_NE(encrypted.value()->data, plaintext);
	EXPECT_EQ(encrypted.value()->iv.size(), 12u);

	auto decrypted = cryptor->Decrypt("alice", encrypted.value());
	ASSERT_TRUE(decrypted.ok());
	EXPECT_EQ(decrypted.value(), plaintext);

	auto invalid = cryptor->Decrypt("alice", nullptr);
	EXPECT_FALSE(invalid.ok());
}

TEST(E2EEManagerTest, DataCryptorAutomaticallyRatchetsWithinConfiguredWindow) {
	KeyProviderOptions options;
	options.ratchet_window_size = 1;
	KeyProvider sender(options);
	KeyProvider receiver(options);
	const E2eeKey initial_key{0, 1, 2, 3, 4, 5, 6, 7};
	ASSERT_TRUE(sender.SetSharedKey(initial_key).Ok());
	ASSERT_TRUE(receiver.SetSharedKey(initial_key).Ok());
	const auto ratcheted = sender.RatchetSharedKey();
	ASSERT_TRUE(ratcheted.Ok());
	auto encryptor = webrtc::make_ref_counted<webrtc::DataPacketCryptor>(
	    webrtc::FrameCryptorTransformer::Algorithm::kAesGcm, KeyProviderNativeAccess::Get(sender));
	auto decryptor = webrtc::make_ref_counted<webrtc::DataPacketCryptor>(
	    webrtc::FrameCryptorTransformer::Algorithm::kAesGcm,
	    KeyProviderNativeAccess::Get(receiver));
	const std::vector<std::uint8_t> plaintext{10, 20, 30, 40};

	auto encrypted = encryptor->Encrypt("alice", 0, plaintext);
	ASSERT_TRUE(encrypted.ok());
	auto decrypted = decryptor->Decrypt("alice", encrypted.value());
	ASSERT_TRUE(decrypted.ok());
	EXPECT_EQ(decrypted.value(), plaintext);
	auto receiver_handler = KeyProviderNativeAccess::Get(receiver)->GetSharedKey("alice");
	ASSERT_NE(receiver_handler, nullptr);
	ASSERT_NE(receiver_handler->GetKeySet(0), nullptr);
	EXPECT_EQ(receiver_handler->GetKeySet(0)->material, ratcheted.key);
}

TEST(E2EEManagerTest, ManagerEncryptsDataUsingSelectedKeySlot) {
	E2eeOptions options;
	options.key_provider.key_ring_size = 2;
	E2EEManager manager(options);
	ASSERT_TRUE(manager.Keys().SetSharedKey({9, 8, 7, 6}, 1).Ok());
	ASSERT_TRUE(manager.SetDataKeyIndex(1));
	EXPECT_EQ(manager.DataKeyIndex(), 1u);
	EXPECT_FALSE(manager.SetDataKeyIndex(2));
	const std::vector<std::uint8_t> plaintext{4, 3, 2, 1};

	auto encrypted = E2EEManagerNativeAccess::EncryptData(manager, "alice", plaintext);
	ASSERT_TRUE(encrypted.has_value());
	EXPECT_EQ(encrypted->key_index, 1u);
	auto decrypted = E2EEManagerNativeAccess::DecryptData(manager, "alice", *encrypted);
	ASSERT_TRUE(decrypted.has_value());
	EXPECT_EQ(*decrypted, plaintext);
}

TEST(E2EEManagerTest, NativeEncodedAudioFrameRoundTripsWithSframeTrailer) {
	KeyProvider provider;
	ASSERT_TRUE(provider.SetSharedKey({0, 1, 2, 3, 4, 5, 6, 7}).Ok());
	auto signaling_thread = webrtc::Thread::Create();
	ASSERT_NE(signaling_thread, nullptr);
	ASSERT_TRUE(signaling_thread->Start());
	auto encryptor = webrtc::scoped_refptr<webrtc::FrameCryptorTransformer>(
	    new webrtc::FrameCryptorTransformer(signaling_thread.get(), "alice",
	                                        webrtc::FrameCryptorTransformer::MediaType::kAudioFrame,
	                                        webrtc::FrameCryptorTransformer::Algorithm::kAesGcm,
	                                        KeyProviderNativeAccess::Get(provider)));
	auto decryptor = webrtc::scoped_refptr<webrtc::FrameCryptorTransformer>(
	    new webrtc::FrameCryptorTransformer(signaling_thread.get(), "alice",
	                                        webrtc::FrameCryptorTransformer::MediaType::kAudioFrame,
	                                        webrtc::FrameCryptorTransformer::Algorithm::kAesGcm,
	                                        KeyProviderNativeAccess::Get(provider)));
	encryptor->SetEnabled(true);
	decryptor->SetEnabled(true);
	const std::vector<std::uint8_t> plaintext{0xf8, 1, 2, 3, 4, 5, 6, 7};

	auto encrypted = TransformAudioFrame(encryptor, plaintext,
	                                     webrtc::TransformableFrameInterface::Direction::kSender);
	ASSERT_GT(encrypted.size(), plaintext.size());
	EXPECT_EQ(encrypted.front(), plaintext.front());
	EXPECT_EQ(encrypted[encrypted.size() - 2], 12);
	EXPECT_EQ(encrypted.back(), 0);

	auto decrypted = TransformAudioFrame(decryptor, encrypted,
	                                     webrtc::TransformableFrameInterface::Direction::kReceiver);
	EXPECT_EQ(decrypted, plaintext);
}

TEST(E2EEManagerTest, NativeEncodedVideoFramesRoundTripForSupportedFraming) {
	KeyProvider provider;
	ASSERT_TRUE(provider.SetSharedKey({0, 1, 2, 3, 4, 5, 6, 7}).Ok());
	auto signaling_thread = webrtc::Thread::Create();
	ASSERT_NE(signaling_thread, nullptr);
	ASSERT_TRUE(signaling_thread->Start());
	auto make_cryptor = [&]() {
		return webrtc::scoped_refptr<webrtc::FrameCryptorTransformer>(
		    new webrtc::FrameCryptorTransformer(
		        signaling_thread.get(), "alice",
		        webrtc::FrameCryptorTransformer::MediaType::kVideoFrame,
		        webrtc::FrameCryptorTransformer::Algorithm::kAesGcm,
		        KeyProviderNativeAccess::Get(provider)));
	};
	auto encryptor = make_cryptor();
	auto decryptor = make_cryptor();
	encryptor->SetEnabled(true);
	decryptor->SetEnabled(true);
	const std::vector<std::pair<webrtc::VideoCodecType, std::vector<std::uint8_t>>> cases{
	    {webrtc::VideoCodecType::kVideoCodecVP8,
	     {0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6}},
	    {webrtc::VideoCodecType::kVideoCodecAV1, {0x12, 0x34, 0x56, 0x78}},
	    {webrtc::VideoCodecType::kVideoCodecH264,
	     {0, 0, 0, 1, 0x65, 0x88, 0x84, 0x21, 0xa0, 0, 0, 3, 1}},
	};

	for (const auto& [codec, plaintext] : cases) {
		auto encrypted = TransformVideoFrame(
		    encryptor, plaintext, webrtc::TransformableFrameInterface::Direction::kSender, codec,
		    true);
		ASSERT_GT(encrypted.size(), plaintext.size());
		auto decrypted = TransformVideoFrame(
		    decryptor, encrypted, webrtc::TransformableFrameInterface::Direction::kReceiver, codec,
		    true);
		EXPECT_EQ(decrypted, plaintext);
	}
}

} // namespace
} // namespace livekit::core
