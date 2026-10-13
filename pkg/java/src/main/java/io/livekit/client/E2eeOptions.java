package io.livekit.client;

import java.util.Objects;

/** GCM E2EE connection settings. Key bytes are copied by the native connect call. */
public record E2eeOptions(byte[] sharedKey, byte[] ratchetSalt, boolean enabled, int keyDerivation,
                          byte[] unencryptedMagicBytes, int ratchetWindowSize,
                          int failureTolerance, int keyRingSize) {
    public E2eeOptions {
        sharedKey = Objects.requireNonNull(sharedKey).clone();
        ratchetSalt = Objects.requireNonNull(ratchetSalt).clone();
        unencryptedMagicBytes = Objects.requireNonNull(unencryptedMagicBytes).clone();
        if (keyDerivation < 0 || keyDerivation > 1)
            throw new IllegalArgumentException("Invalid E2EE key derivation");
        if (ratchetWindowSize < 1 || ratchetWindowSize > 256 || failureTolerance < -1 ||
            keyRingSize < 1 || keyRingSize > 256)
            throw new IllegalArgumentException("Invalid E2EE key provider settings");
    }

    public E2eeOptions(byte[] sharedKey, byte[] ratchetSalt, boolean enabled, int keyDerivation) {
        this(sharedKey, ratchetSalt, enabled, keyDerivation, new byte[0], 16, -1, 16);
    }

    public E2eeOptions(byte[] sharedKey, byte[] ratchetSalt) {
        this(sharedKey, ratchetSalt, true, 0);
    }

    @Override public byte[] sharedKey() { return sharedKey.clone(); }
    @Override public byte[] ratchetSalt() { return ratchetSalt.clone(); }
    @Override public byte[] unencryptedMagicBytes() { return unencryptedMagicBytes.clone(); }
}
