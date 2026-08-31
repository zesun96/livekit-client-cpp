# Engineering quality gates

The quality checks are opt-in and do not change a normal SDK build.

## Formatting

Run the repository-wide check with LLVM `clang-format` 18 or newer:

```powershell
./tools/check-format.ps1
```

The script checks every tracked or untracked, non-ignored C and C++ source file using
`.clang-format` and does not modify files.

## Static analysis

The core utility slice provides a dependency-light compilation database for Clang-Tidy:

```powershell
cmake -S test/unit/core_utils -B out/build/analysis-unit -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=C:/path/to/googletest
cmake --build out/build/analysis-unit --parallel
./tools/run-clang-tidy.ps1 -BuildDirectory out/build/analysis-unit
```

The checked rule set is stored in `.clang-tidy`. It focuses on analyzer correctness,
use-after-move, suspicious memory operations, and high-signal performance diagnostics. Broader
style and API-modernization checks can be enabled separately without weakening this gate.

## Visual Leak Detector

Visual Leak Detector is supported only by opt-in Windows MSVC Debug builds. Configure a separate
build tree and point `LKC_VLD_ROOT` at an existing VLD installation:

```powershell
cmake -S . -B out/build/vs2022-x64-vld `
  -G "Visual Studio 17 2022" -A x64 `
  <normal-dependency-options> `
  -DLKC_ENABLE_VLD=ON `
  -DLKC_VLD_ROOT="D:/path-conf-items/Visual Leak Detector"
cmake --build out/build/vs2022-x64-vld --config Debug --parallel
ctest --test-dir out/build/vs2022-x64-vld -C Debug -L memory --output-on-failure
```

The dedicated SDK lifecycle probe initializes the runtime, baselines WebRTC's intentional
process-lifetime locks and random-number state, then exercises disconnected Room construction,
cleanup, and SDK shutdown. It fails with exit code 86 when later allocations remain. The
`memory.VisualLeakDetectorSelfTest` case first verifies that VLD observes a controlled allocation
before the lifecycle result is trusted. Both cases write reports to standard output. Release
builds skip them and are not instrumented because VLD must not inspect release-CRT modules.

The supported prebuilt libwebrtc package uses the static MSVC runtime. Keep the SDK and all native
dependencies on the same runtime; switching only the SDK DLL to `/MD` or `/MDd` produces an
unsupported mixed-CRT binary. The lifecycle probe consequently avoids transferring owning Debug
STL values across the DLL boundary and exercises pointer- and state-based lifecycle APIs instead.
