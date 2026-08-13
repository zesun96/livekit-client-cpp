/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#pragma once

#ifndef _LKC_CORE_SUBSCRIPTION_PERMISSION_H_
#define _LKC_CORE_SUBSCRIPTION_PERMISSION_H_

#include <string>
#include <vector>

namespace livekit {
namespace core {

// Grants a participant access to every published track or to an explicit list of track SIDs.
// At least one of participant_sid and participant_identity must be set.
struct ParticipantTrackPermission {
	std::string participant_sid;
	std::string participant_identity;
	bool allow_all = false;
	std::vector<std::string> allowed_track_sids;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_SUBSCRIPTION_PERMISSION_H_
