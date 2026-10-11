package io.livekit.client;

import java.util.Objects;

public record IceServer(String[] urls, String username, String password) {
    public IceServer {
        Objects.requireNonNull(urls);
        if (urls.length == 0) throw new IllegalArgumentException("ICE server requires a URL");
        urls = urls.clone();
        for (String url : urls) Objects.requireNonNull(url);
        username = username == null ? "" : username;
        password = password == null ? "" : password;
    }

    @Override public String[] urls() { return urls.clone(); }
}
