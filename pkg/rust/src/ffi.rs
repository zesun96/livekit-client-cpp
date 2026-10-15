//! Complete generated C ABI: media, capture, streams, RPC, E2EE, DataTrack,
//! recording, diagnostics, snapshots, and room operations.
//!
//! # Safety
//! Follow `include/livekit/capi/livekit.h` ownership and callback contracts.
//! Destroy all handles before shutdown; keep published tracks and their sources
//! alive through disconnect. Never destroy a room from its callback. Callback
//! pointers are borrowed only for that invocation. Rust panics must not unwind
//! across the C boundary. Do not manually destroy handles owned by safe wrappers.
#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals)]
#![allow(clippy::all)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
