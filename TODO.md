# TODO

Findings from a project review (2026-11-02).

## Priority

1. ~~Fix CI (no tests registered, so `ctest` fails).~~ Done.
2. ~~Add a connect timeout.~~ Done.
3. ~~Trim input lines and skip blank/comment lines.~~ Done.
4. ~~Return a meaningful exit code.~~ Done.

## Bugs / correctness

- [x] **CI is very likely failing.** `CMakeLists.txt` never calls `enable_testing()` or `add_test()`, so `ctest` has nothing to run. Locally it prints `No test configuration file found!` and exits 1, so the Test step in `.github/workflows/cmake.yml` should fail on every push. Add `enable_testing()` and a real test, or remove the step.
- [x] **Unreachable hosts hang for a long time.** `connect()` in `checker.cc` has no timeout. A non-routable address (`10.255.255.1`) took 75 seconds on macOS before failing (about 2 minutes on Linux), for each resolved address. Use a non-blocking socket plus `poll()` with a timeout of a few seconds.
- [x] **Windows line endings break every domain.** `getline` keeps the `\r`, so a file with CRLF line endings makes lookups fail ("nodename nor servname provided"). Strip trailing whitespace from each line.
- [x] **Blank lines are treated as domains.** An empty line still runs both checks. Skip empty lines and support `#` comments.
- [x] **The exit code is always 0.** `main` returns 0 even when every check fails. Return non-zero if any check fails so the tool works in scripts and CI.
- [x] **Connection failures don't say why.** `connectToPort` drops `errno`. Print `strerror(errno)` for the last attempt (refused, timed out, unreachable, etc.).

## Smaller code issues

- [x] **Each domain is resolved twice.** `performDNSLookup` and `connectToPort` both call `getaddrinfo`. Resolve once and pass the results to the connect step.
- [x] **Address family is assumed.** The `else` branch in `performDNSLookup` treats any non-IPv4 address as IPv6. Use `else if (p->ai_family == AF_INET6)` and skip anything else.
- [x] **`inet_ntop` isn't checked.** Handle its return value.
- [x] **Redundant zeroing.** `struct addrinfo hints {}` already zero-initialises; drop the `memset` calls.
- [x] **Missing include.** `snprintf` needs `<cstdio>`. `std::to_string(port)` would be simpler.
- [x] **Hardcoded inputs.** Accept the domains file (and optionally the port) via `argv` instead of hardcoding `domains.txt` and 443.
- [x] **Noisy output.** Consider one line per domain, e.g. `example.com  93.184.215.14, 2606:…  443:OK`.

## Build / CI

- [x] **Compiler flags are overwritten.** `set(CMAKE_CXX_FLAGS ...)` replaces user/toolchain flags. Use `target_compile_options(checker PRIVATE -Wall -Wextra -pedantic)`.
- [x] **C++17 isn't enforced.** Add `set(CMAKE_CXX_STANDARD_REQUIRED ON)`.
- [x] **The MSVC check is pointless.** The code uses POSIX sockets and won't build on Windows. Remove `if (NOT MSVC)` or document that the tool is POSIX-only.
- [x] **The macOS settings are never built in CI.** Add `macos-latest` to a build matrix so the universal-binary config is exercised.
- [x] **The checkout action is old.** Bump `actions/checkout@v3` to the latest major (now `@v7`) and remove the template boilerplate comments.

## Docs

- [ ] **The README is two lines.** Add build steps, usage, the `domains.txt` format and sample output.
- [x] **`domains.txt` likely contains a stale domain.** `bard.google.com` now redirects to `gemini.google.com`; replaced it and added commented examples.
