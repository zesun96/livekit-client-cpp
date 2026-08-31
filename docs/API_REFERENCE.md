# Public API reference

The SDK can generate an HTML reference for every installed C and C++ header without resolving
libwebrtc, vcpkg, `media-capture`, or the other runtime dependencies.

## Build the reference

Doxygen 1.9.8 or newer and CMake are required. From the repository root:

```powershell
cmake -S . -B out/build/docs -DLKC_DOCUMENTATION_ONLY=ON
cmake --build out/build/docs --target livekit_docs
```

Open `out/build/docs/api/html/index.html` after the build. To choose another output directory,
set `LKC_DOXYGEN_OUTPUT_DIR` during configuration.

The documentation build treats malformed documentation as an error while allowing declarations
that do not yet have an API comment. This keeps the reference buildable as documentation coverage
is improved incrementally.

## Build alongside the SDK

Pass `-DLKC_BUILD_DOCUMENTATION=ON` to a normal SDK configuration. The `livekit_docs` target is
part of the default build in that configuration. Installing it places the generated HTML under
`share/livekit-client-cpp/api`.

`LKC_DOCUMENTATION_ONLY=ON` is intended for documentation development and CI. It stops CMake
before runtime dependency discovery and does not create the SDK, examples, or test targets.
