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

#pragma once

#ifndef _LKC_CORE_E2EE_E2EE_MANAGER_H_
#define _LKC_CORE_E2EE_E2EE_MANAGER_H_

#include "key_provider.h"
#include "livekit/core/option/media_option.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace livekit {
namespace core {

enum class FrameCryptorDirection {
	Sender,
	Receiver,
};

enum class FrameCryptorState {
	New,
	Ok,
	EncryptionFailed,
	DecryptionFailed,
	MissingKey,
	KeyRatcheted,
	InternalError,
};

struct FrameCryptorInfo {
	std::string track_id;
	std::string participant_identity;
	TrackKind kind = TrackKind::Unknown;
	FrameCryptorDirection direction = FrameCryptorDirection::Sender;
	bool enabled = false;
	std::size_t key_index = 0;
	FrameCryptorState state = FrameCryptorState::New;
};

struct EncryptionStateEvent {
	FrameCryptorInfo cryptor;
};

using EncryptionStateCallback = std::function<void(const EncryptionStateEvent&)>;

class E2EEManagerNativeAccess;

// Owns the room's frame cryptors and key provider. Returned FrameCryptorInfo values are
// snapshots; callers never own or retain WebRTC frame-transformer objects.
class E2EEManager final {
public:
	explicit E2EEManager(E2eeOptions options = {});
	~E2EEManager();

	E2EEManager(const E2EEManager&) = delete;
	E2EEManager& operator=(const E2EEManager&) = delete;
	E2EEManager(E2EEManager&&) = delete;
	E2EEManager& operator=(E2EEManager&&) = delete;

	bool Enabled() const noexcept;
	// Existing local publications are republished when this manager belongs to a connected Room.
	// Returns false only when that room-level transition could not be completed.
	bool SetEnabled(bool enabled);
	KeyProvider& Keys() noexcept;
	const KeyProvider& Keys() const noexcept;

	std::vector<FrameCryptorInfo> FrameCryptors() const;
	bool SetFrameCryptorEnabled(const std::string& track_id, FrameCryptorDirection direction,
	                            bool enabled);
	bool SetFrameCryptorKeyIndex(const std::string& track_id, FrameCryptorDirection direction,
	                             std::size_t key_index);
	bool SetDataKeyIndex(std::size_t key_index);
	std::size_t DataKeyIndex() const noexcept;
	std::size_t SetParticipantEnabled(const std::string& participant_identity, bool enabled);
	void SetStateCallback(EncryptionStateCallback callback);

private:
	class Impl;
	friend class E2EEManagerNativeAccess;
	std::unique_ptr<Impl> impl_;
};

} // namespace core
} // namespace livekit

#endif //
