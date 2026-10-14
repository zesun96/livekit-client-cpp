package io.livekit.client;

/** Publication state beyond the basic track description. Enum values match the C ABI. */
public record RemoteTrackPublicationDetails(RemoteTrackPublication publication,
                                            boolean simulcasted, boolean subscriptionAllowed,
                                            int subscriptionStatus, boolean hasSubscriptionError,
                                            int subscriptionError, int encryption,
                                            RemoteTrackState track) {}
