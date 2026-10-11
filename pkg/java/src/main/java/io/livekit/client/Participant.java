package io.livekit.client;

/** Detached participant value; safe to retain after a room callback or room close. */
public record Participant(String sid, String identity, String name, String metadata) {}
