/**
 * Copyright (c) 2024 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "livekit/core/e2ee/key_provider.h"

#include "key_provider_internal.h"

#include <openssl/crypto.h>

#include "api/make_ref_counted.h"
#include "rtc_base/synchronization/mutex.h"

#include <map>
#include <stdexcept>
#include <utility>

namespace livekit {
namespace core {
namespace {

KeyProviderError MissingKeyError() {
	return {KeyProviderErrorCode::KeyNotFound, "The requested E2EE key is not set"};
}

void Cleanse(E2eeKey& key) noexcept {
	if (!key.empty()) {
		OPENSSL_cleanse(key.data(), key.size());
	}
}

class NativeKeyProvider : public webrtc::KeyProvider {
public:
	explicit NativeKeyProvider(const KeyProviderOptions& options) {
		options_.shared_key = true;
		options_.ratchet_salt = options.ratchet_salt;
		options_.uncrypted_magic_bytes = options.unencrypted_magic_bytes;
		options_.ratchet_window_size = static_cast<int>(options.ratchet_window_size);
		options_.failure_tolerance = options.failure_tolerance;
		options_.key_ring_size = static_cast<int>(options.key_ring_size);
		options_.discard_frame_when_cryptor_not_ready = true;
		options_.key_derivation_algorithm =
		    options.key_derivation == KeyDerivationFunction::HkdfSha256 ? webrtc::kHKDF
		                                                                : webrtc::kPBKDF2;
	}

	bool SetSharedKey(int key_index, E2eeKey key) override {
		webrtc::MutexLock lock(&mutex_);
		if (!ValidIndex(key_index) || key.empty()) {
			return false;
		}
		if (!shared_) {
			shared_ = webrtc::make_ref_counted<webrtc::ParticipantKeyHandler>(this);
		}
		shared_->SetKey(key, key_index);
		for (auto& entry : shared_clones_) {
			entry.second->SetKey(key, key_index);
		}
		Cleanse(key);
		return true;
	}

	const webrtc::scoped_refptr<webrtc::ParticipantKeyHandler>
	GetSharedKey(const std::string participant_id) override {
		webrtc::MutexLock lock(&mutex_);
		if (const auto explicit_key = participant_keys_.find(participant_id);
		    explicit_key != participant_keys_.end()) {
			return explicit_key->second;
		}
		if (const auto clone = shared_clones_.find(participant_id); clone != shared_clones_.end()) {
			return clone->second;
		}
		if (!shared_) {
			return nullptr;
		}
		auto clone = shared_->Clone();
		shared_clones_.emplace(participant_id, clone);
		return clone;
	}

	const E2eeKey RatchetSharedKey(int key_index) override {
		webrtc::MutexLock lock(&mutex_);
		if (!ValidIndex(key_index) || !shared_) {
			return {};
		}
		auto key = shared_->RatchetKey(key_index);
		if (key.empty()) {
			return {};
		}
		for (auto& entry : shared_clones_) {
			entry.second->SetKey(key, key_index);
		}
		return key;
	}

	const E2eeKey ExportSharedKey(int key_index) const override {
		webrtc::MutexLock lock(&mutex_);
		return Export(shared_, key_index);
	}

	bool SetKey(const std::string participant_id, int key_index, E2eeKey key) override {
		webrtc::MutexLock lock(&mutex_);
		if (participant_id.empty() || !ValidIndex(key_index) || key.empty()) {
			return false;
		}
		RemoveAndCleanse(shared_clones_, participant_id);
		auto& handler = participant_keys_[participant_id];
		if (!handler) {
			handler = webrtc::make_ref_counted<webrtc::ParticipantKeyHandler>(this);
		}
		handler->SetKey(key, key_index);
		Cleanse(key);
		return true;
	}

	const webrtc::scoped_refptr<webrtc::ParticipantKeyHandler>
	GetKey(const std::string participant_id) const override {
		webrtc::MutexLock lock(&mutex_);
		const auto handler = participant_keys_.find(participant_id);
		return handler != participant_keys_.end() ? handler->second : nullptr;
	}

	const E2eeKey RatchetKey(const std::string participant_id, int key_index) override {
		webrtc::MutexLock lock(&mutex_);
		if (participant_id.empty() || !ValidIndex(key_index)) {
			return {};
		}
		auto handler = participant_keys_.find(participant_id);
		if (handler == participant_keys_.end()) {
			auto clone = shared_clones_.find(participant_id);
			if (clone == shared_clones_.end()) {
				if (!shared_) {
					return {};
				}
				clone = shared_clones_.emplace(participant_id, shared_->Clone()).first;
			}
			handler = participant_keys_.emplace(participant_id, clone->second).first;
			shared_clones_.erase(clone);
		}
		return handler->second->RatchetKey(key_index);
	}

	const E2eeKey ExportKey(const std::string participant_id, int key_index) const override {
		webrtc::MutexLock lock(&mutex_);
		const auto handler = participant_keys_.find(participant_id);
		if (handler != participant_keys_.end()) {
			return Export(handler->second, key_index);
		}
		const auto clone = shared_clones_.find(participant_id);
		return clone != shared_clones_.end() ? Export(clone->second, key_index) : E2eeKey{};
	}

	void SetSifTrailer(const E2eeKey trailer) override {
		webrtc::MutexLock lock(&mutex_);
		options_.uncrypted_magic_bytes = trailer;
	}

	webrtc::KeyProviderOptions& options() override { return options_; }

	bool RemoveSharedKey(int key_index) {
		webrtc::MutexLock lock(&mutex_);
		if (!ValidIndex(key_index) || Export(shared_, key_index).empty()) {
			return false;
		}
		shared_ = RebuildWithout(shared_, key_index);
		CleanseMap(shared_clones_);
		shared_clones_.clear();
		return true;
	}

	bool RemoveKey(const std::string& participant_id, int key_index) {
		webrtc::MutexLock lock(&mutex_);
		if (participant_id.empty() || !ValidIndex(key_index)) {
			return false;
		}
		auto handler = participant_keys_.find(participant_id);
		if (handler == participant_keys_.end() || Export(handler->second, key_index).empty()) {
			return false;
		}
		handler->second = RebuildWithout(handler->second, key_index);
		return true;
	}

	bool RemoveParticipantKeys(const std::string& participant_id) {
		webrtc::MutexLock lock(&mutex_);
		const bool explicit_removed = RemoveAndCleanse(participant_keys_, participant_id);
		const bool clone_removed = RemoveAndCleanse(shared_clones_, participant_id);
		return explicit_removed || clone_removed;
	}

	void Clear() {
		webrtc::MutexLock lock(&mutex_);
		CleanseHandler(shared_);
		shared_ = nullptr;
		CleanseMap(participant_keys_);
		CleanseMap(shared_clones_);
		participant_keys_.clear();
		shared_clones_.clear();
	}

private:
	using Handler = webrtc::scoped_refptr<webrtc::ParticipantKeyHandler>;
	using HandlerMap = std::map<std::string, Handler>;

	bool ValidIndex(int index) const { return index >= 0 && index < options_.key_ring_size; }

	E2eeKey Export(const Handler& handler, int index) const {
		if (!handler || !ValidIndex(index)) {
			return {};
		}
		auto key_set = handler->GetKeySet(index);
		return key_set ? key_set->material : E2eeKey{};
	}

	Handler RebuildWithout(const Handler& old_handler, int removed_index) {
		auto replacement = webrtc::make_ref_counted<webrtc::ParticipantKeyHandler>(this);
		for (int index = 0; index < options_.key_ring_size; ++index) {
			if (index == removed_index) {
				continue;
			}
			auto material = Export(old_handler, index);
			if (!material.empty()) {
				replacement->SetKey(std::move(material), index);
			}
		}
		CleanseHandler(old_handler);
		return replacement;
	}

	void CleanseHandler(const Handler& handler) const {
		if (!handler) {
			return;
		}
		for (int index = 0; index < options_.key_ring_size; ++index) {
			auto key_set = handler->GetKeySet(index);
			if (key_set) {
				Cleanse(key_set->material);
				Cleanse(key_set->encryption_key);
			}
		}
	}

	void CleanseMap(HandlerMap& handlers) const {
		for (auto& entry : handlers) {
			CleanseHandler(entry.second);
		}
	}

	bool RemoveAndCleanse(HandlerMap& handlers, const std::string& identity) const {
		const auto handler = handlers.find(identity);
		if (handler == handlers.end()) {
			return false;
		}
		CleanseHandler(handler->second);
		handlers.erase(handler);
		return true;
	}

	mutable webrtc::Mutex mutex_;
	webrtc::KeyProviderOptions options_;
	Handler shared_;
	HandlerMap participant_keys_;
	HandlerMap shared_clones_;
};

KeyOperationResult ValidateKeyIndex(const KeyProviderOptions& options, std::size_t key_index) {
	if (key_index >= options.key_ring_size) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::InvalidKeyIndex,
		                                   "E2EE key index is outside the configured key ring");
	}
	return KeyOperationResult::Success();
}

} // namespace

class KeyProvider::Impl {
public:
	explicit Impl(KeyProviderOptions value)
	    : options(std::move(value)), native(webrtc::make_ref_counted<NativeKeyProvider>(options)) {}

	KeyProviderOptions options;
	webrtc::scoped_refptr<NativeKeyProvider> native;
};

KeyOperationResult KeyOperationResult::Success(E2eeKey key) {
	return {std::move(key), std::nullopt};
}

KeyOperationResult KeyOperationResult::Failure(KeyProviderErrorCode code, std::string message) {
	return {{}, KeyProviderError{code, std::move(message)}};
}

KeyProvider::KeyProvider(KeyProviderOptions options) {
	if (options.key_ring_size == 0 || options.key_ring_size > 256) {
		throw std::invalid_argument("E2EE key ring size must be between 1 and 256");
	}
	if (options.ratchet_window_size > 256) {
		throw std::invalid_argument("E2EE ratchet window size must not exceed 256");
	}
	if (options.failure_tolerance < -1) {
		throw std::invalid_argument("E2EE failure tolerance must be -1 or greater");
	}
	if (options.ratchet_salt.empty()) {
		options.ratchet_salt.assign(kDefaultE2eeRatchetSalt.begin(), kDefaultE2eeRatchetSalt.end());
	}
	switch (options.key_derivation) {
	case KeyDerivationFunction::Pbkdf2Sha256:
	case KeyDerivationFunction::HkdfSha256:
		break;
	default:
		throw std::invalid_argument("Unsupported E2EE key derivation function");
	}
	impl_ = std::make_unique<Impl>(std::move(options));
}

KeyProvider::~KeyProvider() { Clear(); }

const KeyProviderOptions& KeyProvider::Options() const noexcept { return impl_->options; }

KeyOperationResult KeyProvider::SetSharedKey(E2eeKey key, std::size_t key_index) {
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	if (key.empty()) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::EmptyKey,
		                                   "E2EE keys must not be empty");
	}
	return impl_->native->SetSharedKey(static_cast<int>(key_index), std::move(key))
	           ? KeyOperationResult::Success()
	           : KeyOperationResult::Failure(KeyProviderErrorCode::CryptoFailure,
	                                         "Failed to derive the E2EE shared key");
}

KeyOperationResult KeyProvider::ExportSharedKey(std::size_t key_index) const {
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	auto key = impl_->native->ExportSharedKey(static_cast<int>(key_index));
	return key.empty() ? KeyOperationResult{{}, MissingKeyError()}
	                   : KeyOperationResult::Success(std::move(key));
}

KeyOperationResult KeyProvider::RatchetSharedKey(std::size_t key_index) {
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	auto key = impl_->native->RatchetSharedKey(static_cast<int>(key_index));
	return key.empty() ? KeyOperationResult{{}, MissingKeyError()}
	                   : KeyOperationResult::Success(std::move(key));
}

KeyOperationResult KeyProvider::RemoveSharedKey(std::size_t key_index) {
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	return impl_->native->RemoveSharedKey(static_cast<int>(key_index))
	           ? KeyOperationResult::Success()
	           : KeyOperationResult{{}, MissingKeyError()};
}

KeyOperationResult KeyProvider::SetKey(std::string participant_identity, E2eeKey key,
                                       std::size_t key_index) {
	if (participant_identity.empty()) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::EmptyParticipantIdentity,
		                                   "Participant identity must not be empty");
	}
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	if (key.empty()) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::EmptyKey,
		                                   "E2EE keys must not be empty");
	}
	return impl_->native->SetKey(participant_identity, static_cast<int>(key_index), std::move(key))
	           ? KeyOperationResult::Success()
	           : KeyOperationResult::Failure(KeyProviderErrorCode::CryptoFailure,
	                                         "Failed to derive the participant E2EE key");
}

KeyOperationResult KeyProvider::ExportKey(const std::string& participant_identity,
                                          std::size_t key_index) const {
	if (participant_identity.empty()) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::EmptyParticipantIdentity,
		                                   "Participant identity must not be empty");
	}
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	auto key = impl_->native->ExportKey(participant_identity, static_cast<int>(key_index));
	return key.empty() ? KeyOperationResult{{}, MissingKeyError()}
	                   : KeyOperationResult::Success(std::move(key));
}

KeyOperationResult KeyProvider::RatchetKey(const std::string& participant_identity,
                                           std::size_t key_index) {
	if (participant_identity.empty()) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::EmptyParticipantIdentity,
		                                   "Participant identity must not be empty");
	}
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	auto key = impl_->native->RatchetKey(participant_identity, static_cast<int>(key_index));
	return key.empty() ? KeyOperationResult{{}, MissingKeyError()}
	                   : KeyOperationResult::Success(std::move(key));
}

KeyOperationResult KeyProvider::RemoveKey(const std::string& participant_identity,
                                          std::size_t key_index) {
	if (participant_identity.empty()) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::EmptyParticipantIdentity,
		                                   "Participant identity must not be empty");
	}
	if (const auto validation = ValidateKeyIndex(impl_->options, key_index); !validation.Ok()) {
		return validation;
	}
	return impl_->native->RemoveKey(participant_identity, static_cast<int>(key_index))
	           ? KeyOperationResult::Success()
	           : KeyOperationResult{{}, MissingKeyError()};
}

KeyOperationResult KeyProvider::RemoveParticipantKeys(const std::string& participant_identity) {
	if (participant_identity.empty()) {
		return KeyOperationResult::Failure(KeyProviderErrorCode::EmptyParticipantIdentity,
		                                   "Participant identity must not be empty");
	}
	return impl_->native->RemoveParticipantKeys(participant_identity)
	           ? KeyOperationResult::Success()
	           : KeyOperationResult{{}, MissingKeyError()};
}

void KeyProvider::Clear() {
	if (impl_) {
		impl_->native->Clear();
	}
}

webrtc::scoped_refptr<webrtc::KeyProvider> KeyProviderNativeAccess::Get(KeyProvider& provider) {
	return provider.impl_->native;
}

} // namespace core
} // namespace livekit
