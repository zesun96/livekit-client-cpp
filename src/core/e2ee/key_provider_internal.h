#pragma once

#include "livekit/core/e2ee/key_provider.h"

#include "api/crypto/frame_crypto_transformer.h"
#include "api/scoped_refptr.h"

namespace livekit {
namespace core {

class KeyProviderNativeAccess {
public:
	static webrtc::scoped_refptr<webrtc::KeyProvider> Get(KeyProvider& provider);
};

} // namespace core
} // namespace livekit
