package io.livekit.client;

/** SIP DTMF event from a participant. */
public record SipDtmf(long code, String digit, String participantIdentity) {}
