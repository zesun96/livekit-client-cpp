package io.livekit.client;

public record RpcInvocation(String requestId, String callerIdentity, String payload, int timeoutMs) {}
