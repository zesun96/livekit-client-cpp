package io.livekit.client;

import java.util.Objects;

/** Tracks that one participant may subscribe to. Provide a SID or identity. */
public record ParticipantTrackPermission(String participantSid, String participantIdentity,
                                         boolean allowAll, String[] allowedTrackSids) {
    public ParticipantTrackPermission {
        participantSid = participantSid == null ? "" : participantSid;
        participantIdentity = participantIdentity == null ? "" : participantIdentity;
        if (participantSid.isEmpty() && participantIdentity.isEmpty())
            throw new IllegalArgumentException("Participant SID or identity is required");
        allowedTrackSids = Objects.requireNonNull(allowedTrackSids).clone();
        for (String sid : allowedTrackSids)
            if (sid == null || sid.isEmpty())
                throw new IllegalArgumentException("Track SID is required");
    }

    @Override public String[] allowedTrackSids() { return allowedTrackSids.clone(); }
}
