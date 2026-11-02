# checker

Check domains in C++: for each domain in a file, `checker` looks up its IPv4
and IPv6 addresses and tests whether it accepts a TCP connection on a port
(443 by default). Each connection attempt gives up after 5 seconds, so an
unreachable host can't stall the run.

## Requirements

- macOS or Linux. `checker` uses POSIX sockets and does not build on Windows.
- CMake 3.10 or later
- A C++17 compiler

## Build and test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
(cd build && ctest)
```

The binary is `build/checker`. On macOS it is a universal binary that runs
natively on both Intel and Apple Silicon.

The tests need no network access: they use `localhost`, a reserved `.invalid`
domain and an unroutable address.

## Usage

```
checker [-v] [-p PORT] [FILE]
```

| Option    | Meaning                                                  |
|-----------|----------------------------------------------------------|
| `FILE`    | Domains to check, one per line. Default: `domains.txt`   |
| `-p PORT` | TCP port to test, 1-65535. Default: `443`                |
| `-v`      | Show every address and each step instead of one line per domain |
| `-h`      | Show usage                                               |

Examples:

```sh
build/checker                      # check domains.txt on port 443
build/checker -p 22 servers.txt    # check servers.txt on port 22
build/checker -v                   # detailed output
```

## Domains file format

One domain (or IP address) per line. Anything after `#` is a comment, blank
lines are ignored, and surrounding whitespace and Windows line endings are
fine:

```
# Production hosts
mail.google.com
gemini.google.com     # formerly bard.google.com
```

See [`domains.txt`](domains.txt) for the default list.

## Output

By default `checker` prints one line per domain: the first two addresses (with
a count of the rest) and the port result. Failures include the reason:

```
example.com           172.66.147.243, 104.20.23.154 (+2 more)  443:OK
checker-test.invalid  DNS FAILED (nodename nor servname provided, or not known)
localhost             127.0.0.1, ::1  443:FAILED (Connection refused)
```

With `-v` it lists every address and each step:

```
Checking DNS for domain: example.com
IP addresses for domain example.com:
Forward DNS for example.com: 172.66.147.243
Forward DNS for example.com: 104.20.23.154
Forward DNS for example.com: 2606:4700:10::6814:179a
Forward DNS for example.com: 2606:4700:10::ac42:93f3
DNS lookup successful.
Checking connection to port 443 for domain: example.com
Connection to port 443 successful for example.com
```

Error messages come from the operating system, so their wording differs
between macOS and Linux.

## Exit codes

| Code | Meaning                                                   |
|------|-----------------------------------------------------------|
| `0`  | Every domain resolved and accepted a connection           |
| `1`  | At least one check failed, or the file couldn't be opened |
| `2`  | Usage error: bad port, unknown option or extra arguments  |

This makes `checker` usable in scripts and CI:

```sh
build/checker hosts.txt || echo "some hosts are down"
```
