# Runtime tracing

The SDK provides optional process-wide tracing for connection lifecycle, signaling, transport,
track, data, RPC, and E2EE operations. Its built-in JSON sink writes the Chrome Trace Event format,
which can be opened directly in [Perfetto](https://ui.perfetto.dev/) or Chrome's trace viewer.
Tracing is disabled by default and adds no runtime dependency.

## C++ API

Configure tracing before `Init()` when startup events are needed, and release the sink after
`Destroy()` so the JSON document is finalized:

```cpp
#include <livekit/core/livekit_client.h>

auto trace = livekit::core::CreateJsonTraceSink("livekit-trace.json");
if (trace) {
	livekit::core::SetTraceOptions(
	    {.enabled = true, .category_mask = livekit::core::kAllTraceCategories});
	livekit::core::SetTraceSink(trace);
}

livekit::core::Init();
// Connect rooms and use the SDK.
livekit::core::Destroy();

livekit::core::SetTraceSink(nullptr);
trace.reset();
livekit::core::SetTraceOptions({});
```

Applications that already have a telemetry pipeline can implement `TraceSinkInterface`. Its
`OnTrace()` method is called synchronously from SDK threads, must be thread-safe, and should return
quickly. Exceptions thrown by a C++ sink are contained by the SDK. Replacing a sink does not change
the enabled categories.

## C API

The stable C ABI supports both a callback and the built-in JSON sink:

```c
lk_trace_options_t options;
lk_trace_options_init(&options);
options.enabled = 1;
options.category_mask = LK_TRACE_CATEGORY_ALL;
if (lk_trace_set_options(&options) != LK_STATUS_OK ||
    lk_trace_start_json_file("livekit-trace.json") != LK_STATUS_OK) {
	fprintf(stderr, "Tracing setup failed: %s\n", lk_last_error());
}

lk_init();
/* Connect rooms and use the SDK. */
lk_shutdown();
lk_trace_stop();
```

`lk_trace_set_callback()` replaces the process-wide JSON sink or callback. Callback records and
their `name` strings are borrowed for the duration of the call. `lk_trace_stop()` waits for active C
callbacks to finish, after which callback `user_data` may be released. A callback must not call a
tracing configuration function.

## Event model

Duration events use begin/end phases. Recovery is an asynchronous event so its begin and end can
be correlated across threads with `correlation_id`. Instant events mark decisions such as recovery
escalation and E2EE attachment. Timestamps are monotonic microseconds relative to process tracing
startup; thread IDs are stable hashes suitable for grouping, not operating-system thread handles.

The initial event set covers:

- runtime, room, connection, signal, and transport lifecycle;
- signal resume, ICE restart, full reconnect, and recovery outcome;
- local track publish/unpublish;
- data messages, DataStreams, and RPC;
- E2EE configuration and sender/receiver attachment.

Only fixed SDK operation names and numeric timing/correlation fields are emitted. Tokens, URLs,
participant data, SDP, ICE candidates, RPC payloads, encryption keys, and media contents are not
included. Applications should apply their normal access control and retention policy to trace
files because timing and operation sequences can still be operationally sensitive.

When disabled, instrumentation takes the atomic category check fast path and does not acquire the
sink mutex. The JSON sink serializes and flushes enabled events, so it is intended for diagnostics
and bounded profiling sessions rather than permanent high-volume collection.
