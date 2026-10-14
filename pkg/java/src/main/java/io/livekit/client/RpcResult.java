package io.livekit.client;

public record RpcResult(boolean ok, String payload, int errorCode, String errorMessage, String errorData) {}
