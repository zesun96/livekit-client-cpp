package io.livekit.client;

import java.util.Objects;

/** GCM E2EE connection settings. Key bytes are copied by the native connect call. */
public record E2eeOptions(byte[] sharedKey, byte[] ratchetSalt, boolean enabled, int keyDerivation) {
    public E2eeOptions {
        sharedKey = Objects.requireNonNull(sharedKey).clone();
        ratchetSalt = Objects.requireNonNull(ratchetSalt).clone();
        if (keyDerivation < 0 || keyDerivation > 1)
            throw new IllegalArgumentException("Invalid E2EE key derivation");
    }

    public E2eeOptions(byte[] sharedKey, byte[] ratchetSalt) {
        this(sharedKey, ratchetSalt, true, 0);
    }

    @Override public byte[] sharedKey() { return sharedKey.clone(); }
    @Override public byte[] ratchetSalt() { return ratchetSalt.clone(); }
}
