# Repository Guidelines

## Scope and priorities

- These instructions apply to the entire `livekit-client-cpp` repository.
- Preserve public API and ABI compatibility unless the task explicitly requires a breaking
  change. Prefer compatibility wrappers for corrected names or ownership-safe overloads.
- Keep changes focused. Do not mix dependency updates, broad formatting, refactors, and behavior
  changes unless they are required by the same task.
- Inspect the working tree before editing. Existing changes belong to the user and must not be
  overwritten, reverted, or included in a commit accidentally.

## Repository layout

- Public headers: `include/livekit/`
- Library implementation: `src/core/`
- CMake helpers: `cmake/`
- Examples: `examples/`
- Tests:
  - `test/unit/`: isolated utilities; no LiveKit server and preferably no libwebrtc link.
  - `test/functional/`: local public API and lifecycle behavior.
  - `test/integration/`: real LiveKit server behavior; opt-in only.
  - `test/cpp_utils/`: legacy manual tools; disabled by default.

## C++ rules

- Use C++20 and portable standard-library facilities where practical.
- Format changed C/C++ files with the repository `.clang-format`. Do not reformat unrelated lines
  or entire legacy files merely to satisfy personal style preferences.
- The checked-in style uses tabs for C/C++ indentation, a 100-column limit, left-aligned pointers
  and references, and case-sensitive include sorting.
- Follow `.editorconfig`: UTF-8, LF endings, trim trailing whitespace, and end files with a newline.
- Prefer RAII and value semantics. Use `std::unique_ptr` for sole ownership and `std::shared_ptr`
  only when lifetime is genuinely shared. Raw pointers should normally be non-owning.
- Avoid detached threads. Thread-owning types must define deterministic stop/join behavior and must
  not let callbacks outlive the objects they reference.
- Synchronize shared mutable state explicitly. Do not invoke external callbacks while holding an
  internal mutex unless the lock ordering and re-entrancy behavior are documented.
- Use scoped enums, `nullptr`, `override`, `const`, and `noexcept` where they clarify contracts.
- Throw standard exception types by value. Do not throw strings, exception pointers, or use
  `throw e;` when rethrowing the current exception.
- Validate pointer downcasts and externally supplied data before use. Check protobuf payload sizes
  before converting them to an `int` API.
- Keep headers self-contained: include what they use and avoid relying on transitive includes.
- Correct misspellings in new APIs, but retain a forwarding compatibility header or wrapper for an
  already published misspelled API when feasible.

## CMake and dependencies

- Use target-based CMake (`target_link_libraries`, `target_include_directories`, and
  `target_compile_features`). Avoid global include or link directories and global compiler flags.
- Format `CMakeLists.txt` and `*.cmake` with two-space indentation.
- Prefer small, versioned release/source archives declared through `FetchContent`, pinned with a
  cryptographic `URL_HASH`. Do not replace them with unpinned branches or full-history clones.
- Preserve `USE_SYSTEM_*` options when adding or changing a vendored dependency.
- The legacy source-tree paths `protocol`, `deps/plog`, `deps/nlohmann_json`, and `deps/dr_libs` are
  intentionally ignored. Production CMake uses pinned archives; do not reintroduce them as Git
  submodules or commit local copies unless a task explicitly requests a dependency migration.
- Keep the protocol archive revision aligned with the protobuf schemas expected by the client.
- Keep protobuf compatible with the Abseil ABI embedded in the supported libwebrtc package. A
  protobuf upgrade requires a complete configure, build, and link validation.
- Use `LIBWEBRTC_ROOT` to test a locally built package containing `include/` and `lib/`.
- Never commit downloaded archives, `_deps`, generated build trees, vcpkg package trees, logs, or
  generated protobuf files from a local build.

## Testing and validation

- Add or update tests for behavior changes and regressions.
- Unit tests must be deterministic, fast, and independent of the network, wall-clock timing where
  avoidable, and external services.
- Functional tests may link `livekitclient` but must not require a LiveKit server.
- Integration tests must remain behind `BUILD_INTEGRATION_TESTS=ON`; read credentials from
  `LIVEKIT_URL` and `LIVEKIT_TOKEN`, and skip clearly when they are absent.
- Before committing, run the smallest relevant checks and expand them in proportion to risk:

  ```powershell
  git diff --check
  cmake --build <build-dir>
  ctest --test-dir <build-dir> -L unit --output-on-failure
  ctest --test-dir <build-dir> -L functional --output-on-failure
  ```

- Run integration tests only when explicitly requested and when a test server and credentials are
  available:

  ```powershell
  ctest --test-dir <build-dir> -L integration --output-on-failure
  ```

- If a full build cannot run because libwebrtc, vcpkg packages, credentials, or network access are
  unavailable, still run relevant syntax/static checks and report the exact validation gap.

## Commit rules

- Review `git status --short`, `git diff`, and `git diff --cached` before every commit.
- Stage explicit paths or use exclusion pathspecs. Never stage local dependency source trees, build
  output, logs, credentials, editor state, or unrelated user changes.
- Use Conventional Commit subjects:

  ```text
  <type>(optional-scope): concise imperative summary
  ```

- Allowed common types are `feat`, `fix`, `refactor`, `test`, `build`, `docs`, and `chore`.
- Keep the subject lowercase where natural, without a trailing period, and preferably at or below
  72 characters. Do not commit `WIP` changes.
- Each commit must represent one reviewable logical change. Explain important motivation,
  compatibility concerns, and validation in the body when the subject is insufficient.
- Run `git diff --cached --check` immediately before committing and inspect the staged file list.
- Do not amend, rebase, squash, force-push, or push commits unless the user explicitly requests it.
- After committing, report the commit hash, validation performed, validation not performed, and any
  remaining working-tree changes.
