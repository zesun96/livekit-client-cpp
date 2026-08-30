#pragma once

#include "livekit/core/e2ee/e2ee_manager.h"

#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace livekit {
namespace core {

namespace detail {
class FrameMetadataStore;
}

class E2EEManagerNativeAccess {
public:
	struct EncryptedData {
		std::vector<std::uint8_t> payload;
		std::vector<std::uint8_t> iv;
		std::size_t key_index = 0;
	};

	static bool AttachSender(E2EEManager& manager, std::string track_id,
	                         std::string participant_identity, TrackKind kind,
	                         webrtc::scoped_refptr<webrtc::RtpSenderInterface> sender,
	                         std::shared_ptr<detail::FrameMetadataStore> metadata_store = {},
	                         FrameMetadataFeatures metadata_features = {});
	static bool AttachReceiver(E2EEManager& manager, std::string track_id,
	                           std::string participant_identity, TrackKind kind,
	                           webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
	                           std::shared_ptr<detail::FrameMetadataStore> metadata_store = {});
	static bool Detach(E2EEManager& manager, const std::string& track_id,
	                   FrameCryptorDirection direction);
	static void DetachAll(E2EEManager& manager);
	static void SetEnabledCallback(E2EEManager& manager, std::function<bool(bool)> callback);
	static std::optional<EncryptedData> EncryptData(E2EEManager& manager,
	                                                const std::string& participant_identity,
	                                                const std::vector<std::uint8_t>& payload);
	static std::optional<std::vector<std::uint8_t>>
	DecryptData(E2EEManager& manager, const std::string& participant_identity,
	            const EncryptedData& encrypted);
};

} // namespace core
} // namespace livekit
