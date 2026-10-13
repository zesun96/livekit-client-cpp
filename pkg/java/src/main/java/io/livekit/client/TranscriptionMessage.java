package io.livekit.client;

import java.util.List;
import java.util.Objects;

/** Transcription for one participant and track. */
public record TranscriptionMessage(String participantIdentity, String trackId,
                                   List<TranscriptionSegment> segments) {
    public TranscriptionMessage {
        segments = List.copyOf(Objects.requireNonNull(segments));
    }
}
