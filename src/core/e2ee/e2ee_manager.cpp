/**
 *
 * Copyright (c) 2024 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */
#include "livekit/core/e2ee/e2ee_manager.h"

#include "../detail/frame_metadata.h"
#include "../detail/tracing.h"
#include "e2ee_manager_internal.h"
#include "key_provider_internal.h"

#include "api/crypto/frame_crypto_transformer.h"
#include "api/make_ref_counted.h"
#include "rtc_base/thread.h"

#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace livekit {
namespace core {
namespace {

using CryptorKey = std::pair<FrameCryptorDirection, std::string>;

FrameCryptorState FromNativeState(webrtc::FrameCryptionState state) {
	switch (state) {
	case webrtc::FrameCryptionState::kOk:
		return FrameCryptorState::Ok;
	case webrtc::FrameCryptionState::kEncryptionFailed:
		return FrameCryptorState::EncryptionFailed;
	case webrtc::FrameCryptionState::kDecryptionFailed:
		return FrameCryptorState::DecryptionFailed;
	case webrtc::FrameCryptionState::kMissingKey:
		return FrameCryptorState::MissingKey;
	case webrtc::FrameCryptionState::kKeyRatcheted:
		return FrameCryptorState::KeyRatcheted;
	case webrtc::FrameCryptionState::kInternalError:
		return FrameCryptorState::InternalError;
	case webrtc::FrameCryptionState::kNew:
	default:
		return FrameCryptorState::New;
	}
}

webrtc::FrameCryptorTransformer::MediaType ToNativeMediaType(TrackKind kind) {
	return kind == TrackKind::Video ? webrtc::FrameCryptorTransformer::MediaType::kVideoFrame
	                                : webrtc::FrameCryptorTransformer::MediaType::kAudioFrame;
}

struct SharedState {
	std::mutex mutex;
	bool enabled = true;
	bool accepting_callbacks = true;
	EncryptionStateCallback callback;
	std::map<CryptorKey, FrameCryptorInfo> cryptors;
};

class CryptorObserver : public webrtc::FrameCryptorTransformerObserver {
public:
	CryptorObserver(std::weak_ptr<SharedState> state, CryptorKey key)
	    : state_(std::move(state)), key_(std::move(key)) {}

	void OnFrameCryptionStateChanged(const std::string participant_id,
	                                 webrtc::FrameCryptionState state) override {
		auto shared = state_.lock();
		if (!shared) {
			return;
		}
		EncryptionStateCallback callback;
		EncryptionStateEvent event;
		{
			std::lock_guard<std::mutex> guard(shared->mutex);
			if (!shared->accepting_callbacks) {
				return;
			}
			auto found = shared->cryptors.find(key_);
			if (found == shared->cryptors.end()) {
				return;
			}
			found->second.participant_identity = participant_id;
			found->second.state = FromNativeState(state);
			event.cryptor = found->second;
			callback = shared->callback;
		}
		if (callback) {
			try {
				callback(event);
			} catch (...) {
				// User callbacks must not unwind into libwebrtc's worker thread.
			}
		}
	}

private:
	std::weak_ptr<SharedState> state_;
	CryptorKey key_;
};

} // namespace

class E2EEManager::Impl {
public:
	explicit Impl(E2eeOptions options)
	    : keys_(options.key_provider), shared_state_(std::make_shared<SharedState>()),
	      signaling_thread_(webrtc::Thread::Create()) {
		if (options.encryption_type != EncryptionType::Gcm) {
			throw std::invalid_argument("E2EEManager currently supports AES-GCM encryption only");
		}
		shared_state_->enabled = options.enabled;
		if (!signaling_thread_) {
			throw std::runtime_error("Unable to create the E2EE signaling thread");
		}
		signaling_thread_->SetName("livekit_e2ee_signaling", nullptr);
		if (!signaling_thread_->Start()) {
			throw std::runtime_error("Unable to start the E2EE signaling thread");
		}
		data_cryptor_ = webrtc::make_ref_counted<webrtc::DataPacketCryptor>(
		    webrtc::FrameCryptorTransformer::Algorithm::kAesGcm,
		    KeyProviderNativeAccess::Get(keys_));
		if (options.shared_key) {
			auto result = keys_.SetSharedKey(std::move(*options.shared_key), 0);
			options.shared_key.reset();
			if (!result.Ok()) {
				throw std::invalid_argument(result.error->message);
			}
		}
	}

	~Impl() {
		DetachAll();
		{
			std::lock_guard<std::mutex> guard(shared_state_->mutex);
			shared_state_->accepting_callbacks = false;
			shared_state_->callback = {};
		}
		if (signaling_thread_) {
			signaling_thread_->Stop();
		}
	}

	struct Entry {
		bool individually_enabled = true;
		webrtc::scoped_refptr<webrtc::RtpSenderInterface> sender;
		webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver;
		webrtc::scoped_refptr<webrtc::FrameCryptorTransformer> transformer;
		webrtc::scoped_refptr<webrtc::FrameTransformerInterface> installed_transformer;
		webrtc::scoped_refptr<webrtc::FrameCryptorTransformerObserver> observer;
	};

	bool AttachSender(std::string track_id, std::string participant_identity, TrackKind kind,
	                  webrtc::scoped_refptr<webrtc::RtpSenderInterface> sender,
	                  std::shared_ptr<detail::FrameMetadataStore> metadata_store,
	                  FrameMetadataFeatures metadata_features) {
		if (!sender) {
			return false;
		}
		Entry entry;
		entry.sender = std::move(sender);
		return Attach(std::move(track_id), std::move(participant_identity), kind,
		              FrameCryptorDirection::Sender, std::move(entry), std::move(metadata_store),
		              metadata_features);
	}

	bool AttachReceiver(std::string track_id, std::string participant_identity, TrackKind kind,
	                    webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
	                    std::shared_ptr<detail::FrameMetadataStore> metadata_store) {
		if (!receiver) {
			return false;
		}
		Entry entry;
		entry.receiver = std::move(receiver);
		return Attach(std::move(track_id), std::move(participant_identity), kind,
		              FrameCryptorDirection::Receiver, std::move(entry), std::move(metadata_store),
		              {});
	}

	bool Detach(const std::string& track_id, FrameCryptorDirection direction) {
		Entry entry;
		{
			std::lock_guard<std::mutex> guard(entries_mutex_);
			auto found = entries_.find({direction, track_id});
			if (found == entries_.end()) {
				return false;
			}
			entry = std::move(found->second);
			entries_.erase(found);
		}
		{
			std::lock_guard<std::mutex> guard(shared_state_->mutex);
			shared_state_->cryptors.erase({direction, track_id});
		}
		DetachEntry(entry);
		return true;
	}

	void DetachAll() {
		std::map<CryptorKey, Entry> entries;
		{
			std::lock_guard<std::mutex> guard(entries_mutex_);
			entries.swap(entries_);
		}
		{
			std::lock_guard<std::mutex> guard(shared_state_->mutex);
			shared_state_->cryptors.clear();
		}
		for (auto& [key, entry] : entries) {
			DetachEntry(entry);
		}
	}

	bool Enabled() const noexcept {
		std::lock_guard<std::mutex> guard(shared_state_->mutex);
		return shared_state_->enabled;
	}

	bool SetEnabled(bool enabled) {
		std::function<bool(bool)> callback;
		{
			std::lock_guard<std::mutex> guard(shared_state_->mutex);
			if (shared_state_->enabled == enabled) {
				return true;
			}
			shared_state_->enabled = enabled;
		}
		{
			std::lock_guard<std::mutex> guard(entries_mutex_);
			for (auto& [key, entry] : entries_) {
				const bool effective = enabled && entry.individually_enabled;
				entry.transformer->SetEnabled(effective);
				std::lock_guard<std::mutex> state_guard(shared_state_->mutex);
				shared_state_->cryptors[key].enabled = effective;
			}
		}
		{
			std::lock_guard<std::mutex> guard(enabled_callback_mutex_);
			callback = enabled_callback_;
		}
		return !callback || callback(enabled);
	}

	void SetEnabledCallback(std::function<bool(bool)> callback) {
		std::lock_guard<std::mutex> guard(enabled_callback_mutex_);
		enabled_callback_ = std::move(callback);
	}

	std::vector<FrameCryptorInfo> FrameCryptors() const {
		std::lock_guard<std::mutex> guard(shared_state_->mutex);
		std::vector<FrameCryptorInfo> result;
		result.reserve(shared_state_->cryptors.size());
		for (const auto& entry : shared_state_->cryptors) {
			result.push_back(entry.second);
		}
		return result;
	}

	bool SetFrameCryptorEnabled(const std::string& track_id, FrameCryptorDirection direction,
	                            bool enabled) {
		const CryptorKey key{direction, track_id};
		std::lock_guard<std::mutex> guard(entries_mutex_);
		auto found = entries_.find(key);
		if (found == entries_.end()) {
			return false;
		}
		found->second.individually_enabled = enabled;
		const bool effective = enabled && Enabled();
		found->second.transformer->SetEnabled(effective);
		std::lock_guard<std::mutex> state_guard(shared_state_->mutex);
		shared_state_->cryptors[key].enabled = effective;
		return true;
	}

	bool SetFrameCryptorKeyIndex(const std::string& track_id, FrameCryptorDirection direction,
	                             std::size_t key_index) {
		if (key_index >= keys_.Options().key_ring_size) {
			return false;
		}
		const CryptorKey key{direction, track_id};
		std::lock_guard<std::mutex> guard(entries_mutex_);
		auto found = entries_.find(key);
		if (found == entries_.end()) {
			return false;
		}
		found->second.transformer->SetKeyIndex(static_cast<int>(key_index));
		std::lock_guard<std::mutex> state_guard(shared_state_->mutex);
		shared_state_->cryptors[key].key_index = key_index;
		return true;
	}

	bool SetDataKeyIndex(std::size_t key_index) {
		if (key_index >= keys_.Options().key_ring_size) {
			return false;
		}
		std::lock_guard<std::mutex> guard(shared_state_->mutex);
		data_key_index_ = key_index;
		return true;
	}

	std::size_t DataKeyIndex() const noexcept {
		std::lock_guard<std::mutex> guard(shared_state_->mutex);
		return data_key_index_;
	}

	std::optional<E2EEManagerNativeAccess::EncryptedData>
	EncryptData(const std::string& participant_identity, const std::vector<std::uint8_t>& payload) {
		std::size_t key_index = 0;
		{
			std::lock_guard<std::mutex> guard(shared_state_->mutex);
			if (!shared_state_->enabled) {
				return std::nullopt;
			}
			key_index = data_key_index_;
		}
		auto result =
		    data_cryptor_->Encrypt(participant_identity, static_cast<int>(key_index), payload);
		if (!result.ok() || !result.value()) {
			return std::nullopt;
		}
		return E2EEManagerNativeAccess::EncryptedData{result.value()->data, result.value()->iv,
		                                              result.value()->key_index};
	}

	std::optional<std::vector<std::uint8_t>>
	DecryptData(const std::string& participant_identity,
	            const E2EEManagerNativeAccess::EncryptedData& encrypted) {
		{
			std::lock_guard<std::mutex> guard(shared_state_->mutex);
			if (!shared_state_->enabled || encrypted.key_index >= keys_.Options().key_ring_size) {
				return std::nullopt;
			}
		}
		auto packet = webrtc::make_ref_counted<webrtc::EncryptedPacket>(
		    encrypted.payload, encrypted.iv, static_cast<std::uint8_t>(encrypted.key_index));
		auto result = data_cryptor_->Decrypt(participant_identity, packet);
		return result.ok() ? std::optional<std::vector<std::uint8_t>>(result.value())
		                   : std::nullopt;
	}

	std::size_t SetParticipantEnabled(const std::string& participant_identity, bool enabled) {
		std::size_t updated = 0;
		std::lock_guard<std::mutex> guard(entries_mutex_);
		const bool globally_enabled = Enabled();
		for (auto& [key, entry] : entries_) {
			std::lock_guard<std::mutex> state_guard(shared_state_->mutex);
			auto& info = shared_state_->cryptors[key];
			if (info.participant_identity != participant_identity) {
				continue;
			}
			entry.individually_enabled = enabled;
			info.enabled = globally_enabled && enabled;
			entry.transformer->SetEnabled(info.enabled);
			++updated;
		}
		return updated;
	}

	void SetStateCallback(EncryptionStateCallback callback) {
		std::lock_guard<std::mutex> guard(shared_state_->mutex);
		shared_state_->callback = std::move(callback);
	}

	KeyProvider& Keys() noexcept { return keys_; }
	const KeyProvider& Keys() const noexcept { return keys_; }

private:
	bool Attach(std::string track_id, std::string participant_identity, TrackKind kind,
	            FrameCryptorDirection direction, Entry entry,
	            std::shared_ptr<detail::FrameMetadataStore> metadata_store,
	            FrameMetadataFeatures metadata_features) {
		if (track_id.empty() || participant_identity.empty() || kind == TrackKind::Unknown) {
			return false;
		}
		Detach(track_id, direction);
		const CryptorKey key{direction, track_id};
		const bool enabled = Enabled();
		entry.transformer = webrtc::scoped_refptr<webrtc::FrameCryptorTransformer>(
		    new webrtc::FrameCryptorTransformer(signaling_thread_.get(), participant_identity,
		                                        ToNativeMediaType(kind),
		                                        webrtc::FrameCryptorTransformer::Algorithm::kAesGcm,
		                                        KeyProviderNativeAccess::Get(keys_)));
		entry.observer = webrtc::make_ref_counted<CryptorObserver>(shared_state_, key);
		entry.transformer->RegisterFrameCryptorTransformerObserver(entry.observer);
		entry.transformer->SetEnabled(enabled);
		entry.installed_transformer = entry.transformer;
		if (kind == TrackKind::Video && metadata_store) {
			entry.installed_transformer = detail::CreateFrameMetadataTransformer(
			    direction == FrameCryptorDirection::Sender, std::move(metadata_store),
			    metadata_features, entry.transformer);
		}
		if (entry.sender) {
			entry.sender->SetFrameTransformer(entry.installed_transformer);
		} else {
			entry.receiver->SetFrameTransformer(entry.installed_transformer);
		}
		{
			std::lock_guard<std::mutex> guard(shared_state_->mutex);
			shared_state_->cryptors[key] = {
			    track_id, participant_identity,  kind, direction, enabled,
			    0,        FrameCryptorState::New};
		}
		{
			std::lock_guard<std::mutex> guard(entries_mutex_);
			entries_.emplace(key, std::move(entry));
		}
		return true;
	}

	static void DetachEntry(Entry& entry) {
		if (entry.transformer) {
			// This libwebrtc version does not accept nullptr in
			// RtpReceiverInterface::SetFrameTransformer: it constructs a delegate and
			// dereferences the null transformer on its worker thread. Disable the transformer
			// and let the owning RTP sender/receiver release its reference during teardown.
			entry.transformer->SetEnabled(false);
			entry.transformer->UnRegisterFrameCryptorTransformerObserver();
		}
		entry.sender = nullptr;
		entry.receiver = nullptr;
		entry.observer = nullptr;
		entry.installed_transformer = nullptr;
		entry.transformer = nullptr;
	}

	KeyProvider keys_;
	std::shared_ptr<SharedState> shared_state_;
	std::unique_ptr<webrtc::Thread> signaling_thread_;
	webrtc::scoped_refptr<webrtc::DataPacketCryptor> data_cryptor_;
	std::size_t data_key_index_ = 0;
	mutable std::mutex entries_mutex_;
	std::map<CryptorKey, Entry> entries_;
	std::mutex enabled_callback_mutex_;
	std::function<bool(bool)> enabled_callback_;
};

E2EEManager::E2EEManager(E2eeOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

E2EEManager::~E2EEManager() = default;

bool E2EEManager::Enabled() const noexcept { return impl_->Enabled(); }

bool E2EEManager::SetEnabled(bool enabled) {
	LKC_TRACE_SPAN(TraceCategory::E2ee, "e2ee.set_enabled");
	return impl_->SetEnabled(enabled);
}

KeyProvider& E2EEManager::Keys() noexcept { return impl_->Keys(); }

const KeyProvider& E2EEManager::Keys() const noexcept { return impl_->Keys(); }

std::vector<FrameCryptorInfo> E2EEManager::FrameCryptors() const { return impl_->FrameCryptors(); }

bool E2EEManager::SetFrameCryptorEnabled(const std::string& track_id,
                                         FrameCryptorDirection direction, bool enabled) {
	return impl_->SetFrameCryptorEnabled(track_id, direction, enabled);
}

bool E2EEManager::SetFrameCryptorKeyIndex(const std::string& track_id,
                                          FrameCryptorDirection direction, std::size_t key_index) {
	return impl_->SetFrameCryptorKeyIndex(track_id, direction, key_index);
}

bool E2EEManager::SetDataKeyIndex(std::size_t key_index) {
	return impl_->SetDataKeyIndex(key_index);
}

std::size_t E2EEManager::DataKeyIndex() const noexcept { return impl_->DataKeyIndex(); }

std::size_t E2EEManager::SetParticipantEnabled(const std::string& participant_identity,
                                               bool enabled) {
	return impl_->SetParticipantEnabled(participant_identity, enabled);
}

void E2EEManager::SetStateCallback(EncryptionStateCallback callback) {
	impl_->SetStateCallback(std::move(callback));
}

bool E2EEManagerNativeAccess::AttachSender(
    E2EEManager& manager, std::string track_id, std::string participant_identity, TrackKind kind,
    webrtc::scoped_refptr<webrtc::RtpSenderInterface> sender,
    std::shared_ptr<detail::FrameMetadataStore> metadata_store,
    FrameMetadataFeatures metadata_features) {
	LKC_TRACE_SPAN(TraceCategory::E2ee, "e2ee.attach_sender");
	return manager.impl_->AttachSender(std::move(track_id), std::move(participant_identity), kind,
	                                   std::move(sender), std::move(metadata_store),
	                                   metadata_features);
}

bool E2EEManagerNativeAccess::AttachReceiver(
    E2EEManager& manager, std::string track_id, std::string participant_identity, TrackKind kind,
    webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    std::shared_ptr<detail::FrameMetadataStore> metadata_store) {
	LKC_TRACE_SPAN(TraceCategory::E2ee, "e2ee.attach_receiver");
	return manager.impl_->AttachReceiver(std::move(track_id), std::move(participant_identity), kind,
	                                     std::move(receiver), std::move(metadata_store));
}

bool E2EEManagerNativeAccess::Detach(E2EEManager& manager, const std::string& track_id,
                                     FrameCryptorDirection direction) {
	return manager.impl_->Detach(track_id, direction);
}

void E2EEManagerNativeAccess::DetachAll(E2EEManager& manager) { manager.impl_->DetachAll(); }

void E2EEManagerNativeAccess::SetEnabledCallback(E2EEManager& manager,
                                                 std::function<bool(bool)> callback) {
	manager.impl_->SetEnabledCallback(std::move(callback));
}

std::optional<E2EEManagerNativeAccess::EncryptedData>
E2EEManagerNativeAccess::EncryptData(E2EEManager& manager, const std::string& participant_identity,
                                     const std::vector<std::uint8_t>& payload) {
	return manager.impl_->EncryptData(participant_identity, payload);
}

std::optional<std::vector<std::uint8_t>>
E2EEManagerNativeAccess::DecryptData(E2EEManager& manager, const std::string& participant_identity,
                                     const EncryptedData& encrypted) {
	return manager.impl_->DecryptData(participant_identity, encrypted);
}

} // namespace core
} // namespace livekit
