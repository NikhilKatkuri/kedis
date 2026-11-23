# kedis

A lightweight, Redis-inspired key-value server written in C, with a small Node.js shell. The server listens on TCP port `7379` and speaks a simple newline-terminated text protocol.

> kedis is a focused subset for learning and experimentation. It is **not** a Redis drop-in replacement and does not speak RESP.

## Getting started

kedis listens on **TCP port 7379**. You can run it with Docker (works on any OS) or build it from source (Linux only, because the server uses `epoll`).

### Option 1: Docker (recommended)

Requires Docker with the Compose plugin (`docker compose`).

```sh
docker compose up -d --build   # build the image and start the server in the background
docker compose ps              # status shows "healthy" after about 10 seconds
docker compose logs -f         # follow the server log (Ctrl+C stops following, not the server)
docker compose down            # stop the server; saved data is kept
```

What you get:

- A small Alpine-based image that runs the server as a non-root user (uid 10001).
- A named volume, `kedis-data`, mounted at `/data`. Snapshots are written to `/data/kedis/dump.rdb`, so data survives restarts.
- The port published on `127.0.0.1:7379` only, so the server is reachable from your machine but not from your network.
- A healthcheck that confirms the port accepts connections.

Run it without Compose:

```sh
docker build -t kedis .
docker run -d --name kedis -p 127.0.0.1:7379:7379 -v kedis-data:/data kedis
```

After you change any source file, rebuild with `docker compose up -d --build`. A plain `up` reuses the old image.

To delete the saved data as well, run `docker compose down -v`.

### Option 2: Build from source (Linux)

Requires `gcc` and `make` (on Debian/Ubuntu: `sudo apt install build-essential`).

```sh
cd server
make run
```

`make run` builds the server if needed and starts it. Stop it with **Ctrl+C**: the server shuts down cleanly and saves a final snapshot.

Start the server from inside `server/`. It loads `commands/commands.json` relative to its working directory and fails to start if it can't find it.

| Command (from `server/`) | What it does |
|---|---|
| `make` | Build the server and all test binaries into `build/`. |
| `make run` | Build if needed, then start `build/kedis-server`. |
| `make client` | Open `nc` connected to `127.0.0.1:7379`. |
| `make test` | Run the unit tests (dictionary, snapshot, handlers). |
| `make san` | Clean, rebuild with AddressSanitizer and UBSan, and run the tests. |
| `make clean` | Remove `build/`. |

macOS and Windows can't build the server natively. Use Docker there (Docker Desktop runs a Linux VM), or WSL2 on Windows.

### Connect to the server

**The interactive shell** (requires Node.js 16 or newer):

```sh
node cli/shell.js
```

```text
kedis> PING
PONG
kedis> SET city Tokyo
OK
kedis> SET session abc123 -ttl=5000
OK
kedis> GET city
Tokyo
kedis> STATS
(printed as a table)
kedis> exit
```

Shell commands:

| Command | Description |
|---|---|
| `HELP` | List the available commands. |
| `CONNECT host[:port]` | Switch to another server. The port defaults to 7379. A URL such as `http://host/` is also accepted, but see the note under Remote access. |
| `exit` / `quit` | Close the shell. |

The shell connects to `127.0.0.1:7379` by default. Override it with environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `KEDIS_HOST` | `127.0.0.1` | Server host. |
| `KEDIS_PORT` | `7379` | Server port. |

```sh
KEDIS_HOST=192.168.1.20 KEDIS_PORT=7379 node cli/shell.js
```

**Any TCP client** works too, because the protocol is plain text, one command per line:

```sh
nc 127.0.0.1 7379
printf 'PING\n' | nc -q1 127.0.0.1 7379
```

See [Commands](#commands) for the full command list.

### Configuration

The server has no command-line flags. The port is fixed at 7379.

| Setting | How to change it |
|---|---|
| Snapshot location | Set `XDG_DATA_HOME`. The file is `$XDG_DATA_HOME/kedis/dump.rdb`. If it isn't set, the server uses `~/.local/share/kedis/dump.rdb`. (Docker sets it to `/data`.) |
| Snapshot interval | A snapshot is saved every 60 seconds and again on shutdown. |

`FLUSHDB` clears all keys and deletes the snapshot file.

### Check that persistence works

```sh
printf 'SET city Tokyo\n' | nc -q1 127.0.0.1 7379    # OK
docker compose down                                    # graceful stop saves a snapshot
docker compose up -d
printf 'GET city\n' | nc -q1 127.0.0.1 7379           # Tokyo
```

### Remote access

kedis has **no authentication and no encryption**. Don't expose port 7379 to the internet. To reach a server on another machine, forward the port over SSH instead:

```sh
ssh -N -L 7379:127.0.0.1:7379 user@your-server
node cli/shell.js            # now talks to the remote server through the tunnel
```

Web-based tunnels and URLs (HTTP proxies, `*.devtunnels.ms` links, load balancers that only forward HTTP) can't carry kedis traffic, because it uses its own TCP protocol. Forward the raw port instead, as above.

### Run the tests

```sh
cd server
make test      # unit tests
make san       # same tests under ASan/UBSan
```

CI runs the C build, unit tests, sanitizers and a two-client smoke test (`ci.yml`), and builds and tests the Docker image (`docker-ci.yaml`).

### Troubleshooting

| Problem | Likely cause and fix |
|---|---|
| `ECONNREFUSED` from the shell | The server isn't running, or it's on another port. Check `docker compose ps` or the terminal running `make run`. |
| `Address already in use` / port 7379 taken | Another copy is already running, for example the Docker container and `make run` at the same time. Stop one of them. |
| `Failed to load commands` on startup | You started the server outside `server/`. `cd server` first. |
| `Permission denied` and `snapshot saving failed` in the Docker log | The `/data` volume was created with files owned by a different user. Run `docker compose down -v` once to recreate it. This wipes the saved data. |
| Your code changes don't show up in Docker | The old image is being reused. Run `docker compose up -d --build`. |
| Build fails on macOS or Windows | The server needs Linux (`epoll`). Use Docker or WSL2. |
| `connect` fails with `ENOTFOUND` | The hostname is wrong or isn't reachable. Use a plain host or `host:port`. |