package io.livekit.client;

import java.util.Objects;

/** Detached participant permission snapshot. Source values match the C ABI. */
public record ParticipantPermissions(boolean canSubscribe, boolean canPublish,
                                     boolean canPublishData, int[] canPublishSources,
                                     boolean hidden, boolean recorder,
                                     boolean canUpdateMetadata, boolean agent,
                                     boolean canSubscribeMetrics,
                                     boolean canManageAgentSession) {
    public ParticipantPermissions {
        canPublishSources = Objects.requireNonNull(canPublishSources).clone();
    }

    @Override public int[] canPublishSources() { return canPublishSources.clone(); }
}
