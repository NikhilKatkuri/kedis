# kedis

A lightweight, Redis-inspired key-value server written in C. `kedis` implements a small text/RESP-style command protocol on a default TCP port of `7379`.

## Overview

The implementation uses a few deliberate design choices:

- **Raw byte-buffer values:** keys and values are stored as `char` buffers with explicit lengths. Strings and compact JSON therefore need no type tags or JSON-specific encoding.
- **A protected dictionary:** storage is a hash table with separate chaining, dynamic resizing, and a `pthread_mutex_t` around dictionary operations.
- **A JSON-backed command registry:** `commands/commands.json` declares command names, argument limits, and allowed options. `cmd_execute()` validates a parsed command before invoking its handler.
- **TTL and persistence:** entries may have millisecond absolute expiration times. A worker removes expired entries, and snapshots preserve live entries.

This is a focused server subset, not a Redis drop-in replacement.

## Project Structure

| Path | Purpose |
|---|---|
| `src/server.c` | TCP server entry point: snapshot setup/load, worker threads, command registry loading, socket setup, and client loop. |
| `src/dictionary.c`, `include/dictionary.h` | Hash-table storage, entry lifecycle, resizing, expiration removal, statistics, and mutex-protected operations. |
| `src/commands.c`, `include/commands.h` | `commands.json` loading, universal line parsing, argument/option validation, and handler dispatch. |
| `commands/commands.json` | Command definitions: name, `min_args`, `max_args`, and `allowed_opts`. `-1` means unbounded/variadic arguments. |
| `src/handlers.c`, `include/handlers.h` | Table-driven `handlers_init` registration and implementations of the built-in commands. |
| `src/snapshot.c`, `include/snapshot.h` | Binary `KEDIS001` snapshot load/save/delete logic and platform-specific default snapshot path. |
| `src/background_snapshot.c`, `src/active_expire.c` | Background snapshot worker and active TTL-expiration worker. |
| `tests/` | Unit tests for dictionary, handlers, and snapshots. |
| `Makefile` | Server/test builds, sanitizer build, test runner, and `nc` client target. |

The default Linux snapshot location is `$XDG_DATA_HOME/kedis/dump.rdb` when `XDG_DATA_HOME` is set, otherwise `$HOME/.local/share/kedis/dump.rdb`. Snapshot startup is implemented as load-then-run, snapshots are written periodically, and the server also writes a snapshot when an accepted client disconnects. `FLUSHDB` deletes the snapshot file.

## Building and Running

Build with GCC and C17:

```sh
make
```

This builds the server and test binaries under `build/`. The server binary is `build/kedis-server`; test binaries are also placed in `build/`.

Useful targets:

```sh
make build       # same binary target name in this Makefile? use make
make test        # run dictionary, snapshot, and handler tests
make san         # clean, rebuild, and run tests with ASan/UBSan
make clean       # remove build/
make run         # build if needed and start build/kedis-server
make client      # open nc to 127.0.0.1:7379
```

**Note:** `make build` is not a Makefile target in the current repository. Use `make` or `make run`.

Start the server directly:

```sh
make run
```

Or:

```sh
./build/kedis-server
```

The server binds `INADDR_ANY` on port `7379`; it currently accepts no host, port, or other command-line flags. Run it from the `server/` directory because it loads `commands/commands.json` relative to the working directory.

Connect with `netcat`:

```sh
nc 127.0.0.1 7379
PING
SET greeting hello
GET greeting
```

The server currently accepts one client connection per process. Type `exit` to disconnect the current client and shut down the close/save path.

## Supported Commands and Options

All command names are case-insensitive: the parser normalizes the command token to uppercase before dispatch.

| Command | Syntax | Response / behavior |
|---|---|---|
| `PING` | `PING` | `+PONG` |
| `GET` | `GET key` | Bulk string `$<length>`; `$-1` if absent |
| `SET` | `SET key value [-ttl=milliseconds]` | `+OK`, or `-ERR` for an invalid TTL/set failure |
| `DEL` | `DEL key [key ...]` | Integer count of deleted keys; `:N` |
| `COUNT` | `COUNT` | Integer dictionary entry count: `:N` |
| `STATS` | `STATS` | Simple string containing a JSON statistics object |
| `EXISTS` | `EXISTS key` | `:1` if present, `:0` if absent |
| `PERSIST` | `PERSIST key` | `:1` if an expiration was removed, `:0` otherwise |
| `FLUSHDB` | `FLUSHDB` | `+OK`; deletes all entries and the snapshot file |
| `MGET` | `MGET key [key ...]` | RESP array of bulk values, one per key; missing keys are `$-1` |
| `MSET` | `MSET key value [key value ...]` | `+OK`; pairs must be complete |
| `SETNX` | `SETNX key value` | `:1` when set successfully, `:0` when the key already exists |

### Options and variadic arguments

Options use `-name=value` syntax. Currently only `ttl` is implemented:

```text
SET session:user42 abc123 -ttl=5000
```

`ttl` is a positive integer number of milliseconds converted to an absolute expiration time. An invalid, missing, duplicate, or non-positive TTL is rejected.

The registry defines `MGET` and `MSET` with `max_args: -1`; `-1` means no fixed upper bound. `MSET` requires an even number of positional arguments. `MGET` requires at least one key.

The `DEL` handler itself loops over all positional keys, but `commands.json` currently declares `max_args: 1`, so only one key is accepted by normal registry validation at present.

### RESP-style responses

Responses use a small Redis-compatible wire subset:

- `+OK\r\n` and `+PONG\r\n`: simple strings
- `-ERR ...\r\n`: errors
- `:<integer>\r\n`: integers
- `$<length>\r\n<data>\r\n`: bulk strings
- `$-1\r\n`: null bulk string
- `*<count>\r\n` followed by bulk values: arrays

The response buffer is fixed at 256 bytes by `buffer_to_command()`. Commands that would overflow it return `-ERR response too large` or `-ERR response buffer overflow`.

## Wire Protocol and Input Notes

Send plain newline-terminated text over TCP:

```text
SET city "Tokyo"
```

Quotes are not a special feature of the parser. `parse_line_universal()` tokenizes with `strtok()` on spaces, tabs, carriage returns, and newlines. It does **not** preserve quoted phrases or whitespace inside values.

Best practices:

- Keep keys and values whitespace-free.
- Format JSON compactly, for example `SET profile {"name":"Ada","role":"dev"}`. `JSON.stringify(...)` output is usually compact and has no internal spaces, so it fits this parser.
- Do not include a C/JavaScript `"` escaping distinction as a quoted string; the parser forwards token characters directly.
- Put a TTL option as a separate token, such as `-ttl=5000`.

`MAX_TOKENS` is `16`. The parser rejects input with more than 16 tokens; it also limits options to `MAX_OPTS` (`16`) and commands to `MAX_CMDS` (`64`). Commands and values are further constrained by the 1024-byte read buffer and 256-byte response buffer.

A whitespace-free JSON example:

```sh
printf 'SET profile:{"name":"Ada","active":true}\nGET profile\n' | nc 127.0.0.1 7379
```

The server reads up to 1023 bytes per `read()` call and processes the received buffer as one command line. Sending an input line, the server writes the corresponding response immediately.

## Development

### Add a command

1. Add a command specification to `commands/commands.json` with `name`, `min_args`, `max_args`, and `allowed_opts`.
2. Implement a `bool` handler with the `CommandHandler` signature in `src/handlers.c` (and declare it in `include/handlers.h` if it is called outside the file).
3. Register `{"NAME", handle_name}` in the `cmds[]` table in `handlers_init()`.
4. Rebuild and run the tests:

```sh
make clean
make
make test
```

The registry layer validates names and argument counts/options; the handler is responsible for command-specific behavior and RESP output.

### Near-term roadmap

- Add a Node.js client/shell for interactive use and scripting.
- Add numeric commands such as `INCR` and `DECR`.
- Expand variadic behavior consistently (including `DEL` once its registry limit is changed).
- Improve support for quoted strings, escaped whitespace, and larger streamed payloads.
- Revisit one-client-per-process server behavior for concurrent connections.
