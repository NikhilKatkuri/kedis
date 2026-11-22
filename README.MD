# kedis

A lightweight, Redis-inspired key-value server written in C, with a small Node.js shell. The server listens on TCP port `7379` and speaks a simple newline-terminated text protocol.

> kedis is a focused subset for learning and experimentation. It is **not** a Redis drop-in replacement and does not speak RESP.

## Features

- **Raw byte-buffer storage:** keys and values are `char` buffers with explicit lengths.
- **Thread-safe dictionary:** hash table with separate chaining, dynamic resizing, and a `pthread_mutex_t` around operations.
- **JSON-driven command registry:** `server/commands/commands.json` declares each command's name, argument limits, and allowed options. `cmd_execute()` validates a parsed line before calling its handler.
- **TTL support:** `SET key value -ttl=<ms>`; a background worker removes expired entries every second.
- **Persistence:** binary `KEDIS001` snapshots, written every 60 seconds and on shutdown.

## Repository layout

```text
kedis/
├── client-ts/          Node.js interactive shell (kedis-cli)
└── server/
    ├── commands/       commands.json (command registry)
    ├── include/        headers
    ├── src/            server, dictionary, commands, handlers, snapshot, workers
    ├── tests/          unit tests (dictionary, handlers, snapshot)
    └── Makefile
```

| File | Purpose |
|---|---|
| `src/server.c` | Entry point: snapshot load, worker threads, registry loading, socket setup, client loop. |
| `src/dictionary.c` | Hash table, entry lifecycle, resizing, expiration, stats. |
| `src/commands.c` | `commands.json` loader, line parser, validation, dispatch. |
| `src/handlers.c` | Command implementations and handler registration (`handlers_init`). |
| `src/snapshot.c` | Snapshot load/save/delete and default snapshot path. |
| `src/background_snapshot.c`, `src/active_expire.c` | Periodic snapshot worker and TTL-expiry worker. |

## Build and run

Requires GCC (C17) and `make`. Run everything from the `server/` directory.

```sh
cd server
make          # builds build/kedis-server and the test binaries
make run      # builds if needed, then starts the server
```

| Target | Description |
|---|---|
| `make` | Build the server and all test binaries into `build/`. |
| `make test` | Run the dictionary, snapshot, and handler tests. |
| `make san` | Clean, rebuild with ASan/UBSan, and run the tests. |
| `make run` | Start `build/kedis-server`. |
| `make client` | Open `nc` to `127.0.0.1:7379`. |
| `make clean` | Remove `build/`. |

The server takes no command-line flags: it binds `0.0.0.0:7379`. It reads `commands/commands.json` relative to the working directory, so **start it from `server/`**.

### Connect with netcat

```sh
nc 127.0.0.1 7379
PING
SET greeting hello
GET greeting
```

### Connect with the Node.js shell

Requires Node.js. From the repo root:

```sh
node client-ts/shell.js
```

Set `KEDIS_HOST` / `KEDIS_PORT` to change the default target. Inside the shell: `HELP` lists commands, `CONNECT <host> <port>` switches servers, and `exit` / `quit` closes the shell. JSON replies (such as `STATS`) are printed as a table.

> **Current limitation:** the server accepts a **single client connection per process**. When that client disconnects (or sends `exit`), the server saves a snapshot and shuts down. Restart it to connect again.

## Commands

Command names are case-insensitive. Options use `-name=value` syntax; only `ttl` exists today.

| Command | Syntax | Reply |
|---|---|---|
| `PING` | `PING` | `PONG` |
| `SET` | `SET key value [-ttl=ms]` | `OK`, or `-ERR invalid TTL` / `-ERR SET failed` |
| `GET` | `GET key` | the value, or `(nil)` if absent |
| `DEL` | `DEL key [key ...]` | `no keys deleted`, `dropped 1 key`, or `dropped N keys` |
| `EXISTS` | `EXISTS key` | `1` if present, `0` if not |
| `PERSIST` | `PERSIST key` | `persisted`, or `no ttl or key missing` |
| `COUNT` | `COUNT` | number of entries |
| `STATS` | `STATS` | a JSON statistics object |
| `FLUSHDB` | `FLUSHDB` | `OK`; clears all entries and deletes the snapshot file |
| `MSET` | `MSET key value [key value ...]` | `OK` (arguments must come in pairs) |
| `MGET` | `MGET key [key ...]` | numbered list: `1) value`, `2) (nil)`, ... |
| `SETNX` | `SETNX key value` | `ok`, or `already exists` if the key is present |

Errors are returned as `-ERR ...` (for example `-ERR unknown command` or `-ERR wrong number of arguments for 'SET'`).

TTL example:

```text
SET session:user42 abc123 -ttl=5000
```

`ttl` must be a positive integer number of milliseconds. Invalid, empty, or duplicate `-ttl` options are rejected.

## Input rules

The parser is intentionally simple:

- Input is plain text; the server reads up to 1023 bytes per `read()` and treats it as **one command line**.
- Tokens are split on spaces, tabs, `\r` and `\n` using `strtok()`. **Quotes are not special**, so values cannot contain whitespace.
- Send JSON in compact form with no spaces inside it, e.g. `SET profile {"name":"Ada","active":true}`. `JSON.stringify(obj)` output works.
- A token starting with `-` and containing `=` is parsed as an option; other tokens are positional arguments.
- Limits: at most 16 tokens, 16 options, and 64 registered commands.
- Responses are written into a fixed 256-byte buffer. Larger results return `-ERR response too large` (`GET`) or a truncated/`ERR response too large` reply (`MGET`).

## Persistence

- Snapshot format: binary, magic header `KEDIS001`.
- Default path:
  - Linux: `$XDG_DATA_HOME/kedis/dump.rdb`, else `$HOME/.local/share/kedis/dump.rdb`
  - macOS: `~/Library/Application Support/Kedis/dump.rdb`
  - Windows: `%LOCALAPPDATA%\Kedis\dump.rdb`
- The snapshot is loaded at startup, saved every 60 seconds by a background thread, and saved again when the server shuts down.
- `FLUSHDB` deletes the snapshot file.