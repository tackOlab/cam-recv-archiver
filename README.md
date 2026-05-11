# cam-recv-archiver

[日本語版 README はこちら](README.ja.md)

A TCP receiver for an HTJ2K-based intelligent camera, an on-disk archiver, and a MongoDB index. Built and tested on **Ubuntu 24.04 LTS** with g++ 13 and CMake 3.28.

```
   camera (HTJ2K codestream)              this repo
   ┌──────────────────────┐               ┌────────────────────────────────┐
   │ length-prefixed      │   TCP :4001   │ recv                           │
   │ frames over TCP      │ ────────────► │  ├── wraps codestream in JPH   │
   └──────────────────────┘               │  │   container                 │
                                          │  ├── writes <timestamp>.jph    │ ──► <archive_dir>
                                          │  └── inserts {fname,timestamp} │ ──► mongod (test.item1)
                                          │                                │
                                          │ demo/  (Flask)                 │
                                          │  └── range-query the index,    │ ◄── HTTP :5000
                                          │      download / preview frames │
                                          └────────────────────────────────┘
```

## Repository layout

| Path | Purpose |
| --- | --- |
| `main.cpp` | The receiver: TCP server + JPH wrapping + Mongo insert |
| `simple_tcp.hpp` | Header-only TCP wrapper with length-prefixed framing |
| `CMakeLists.txt` | Build glue — `find_package(mongocxx CONFIG REQUIRED)` |
| `demo/` | Flask app to search the archive by timestamp range |

---

## Environment setup (from scratch)

These steps take a clean Ubuntu 24.04 install to a working build. Run them in order.

### 0. Bring apt up to date

```sh
sudo apt update
```

### 1. Install build tooling and library dependencies

```sh
sudo apt install -y build-essential cmake pkg-config \
                    libssl-dev libsasl2-dev libmongocrypt-dev \
                    git curl
```

What each one is for:

| Package | Why |
| --- | --- |
| `build-essential` | g++, make, libc headers |
| `cmake`, `pkg-config` | Configure the build |
| `libssl-dev` | TLS in the Mongo drivers |
| `libsasl2-dev` | SASL auth in the Mongo drivers |
| `libmongocrypt-dev` | Client-side encryption support pulled in by mongo-cxx-driver |
| `git`, `curl` | Fetching the drivers and MongoDB repo key |

Sanity check:

```sh
g++ --version    # expect 13.x
cmake --version  # expect >= 3.15
```

### 2. Set the system timezone (optional but recommended)

`recv` builds its filenames from the system's local time, and the demo's date-range form interprets inputs in system-local time when `DISPLAY_TZ` is unset. Picking one zone up front avoids confusion later:

```sh
sudo timedatectl set-timezone Asia/Tokyo   # or your zone
date                                       # verify
```

You can keep the system in UTC and override only the *display* zone in the demo via `DISPLAY_TZ` — see `demo/README.md`. The filename will still be in system-local, though.

### 3. Install MongoDB server

Use MongoDB's official apt repo; the `mongodb` package shipped by Ubuntu is unmaintained. Follow the upstream guide for adding the repo key, source line, and installing the metapackage:

→ <https://www.mongodb.com/docs/manual/tutorial/install-mongodb-on-ubuntu/>

After install, start it and verify:

```sh
sudo systemctl enable --now mongod
systemctl is-active mongod                       # → active
mongosh --quiet --eval 'db.runCommand({ping:1})' # → { ok: 1 }
```

`recv` connects to `mongodb://localhost:27017/` with no auth.

### 4. Build & install mongo-c-driver from source

`mongocxx` 4.x requires `libmongoc` ≥ 1.28; the apt package is older. Tested with **1.29.0**:

```sh
git clone --depth 1 --branch 1.29.0 \
    https://github.com/mongodb/mongo-c-driver.git
cd mongo-c-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
cd ..
```

Installs to `/usr/local` by default — that's important so the C++ driver's CMake config files end up where `find_package` looks.

Verify:

```sh
pkg-config --modversion libmongoc-1.0 libbson-1.0   # → 1.29.0
```

### 5. Build & install mongo-cxx-driver from source

This step is what makes the `mongo::mongocxx_shared` / `mongo::bsoncxx_shared` CMake targets available — without it the receiver won't configure. Tested with **4.0.0**:

```sh
git clone --depth 1 --branch r4.0.0 \
    https://github.com/mongodb/mongo-cxx-driver.git
cd mongo-cxx-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBSONCXX_POLY_USE_IMPLS=ON
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
cd ..
```

Verify:

```sh
pkg-config --modversion libmongocxx libbsoncxx     # → 4.0.0
ls /usr/local/lib/cmake/mongocxx-4.0.0/            # CMake config files
```

---

## Build the receiver

From the repo root:

```sh
cmake -B build
cmake --build build -j"$(nproc)"
```

You should get a single executable: `./build/recv`.

If the drivers were installed somewhere other than `/usr/local`, pass `-DCMAKE_PREFIX_PATH=/your/prefix` to the configure step.

## Run the receiver

```sh
./build/recv [archive_dir]
```

- `archive_dir` (optional) selects where `.jph` files are written. The directory is created if it doesn't exist. Default is the current working directory.
- The receiver listens on **TCP 4001** and prints `Awaiting connection on :4001 ...` when ready.
- On each frame received it prints `<filename> = <N> bytes` and inserts one document into MongoDB `test.item1`.

If the loader can't find `libmongocxx.so` (`error while loading shared libraries`), prepend `LD_LIBRARY_PATH=/usr/local/lib` to the command.

The MongoDB document schema is:

```json
{ "_id": ObjectId(...), "fname": "2026-05-11-17-17-15.123.jph", "timestamp": ISODate(...) }
```

`fname` is the **basename only** — the demo combines it with its own `ARCHIVE_DIR` to locate the file on disk.

## End-to-end smoke test (no camera required)

To verify the network and Mongo paths without the camera, send a length-prefixed dummy payload. In one terminal:

```sh
mkdir -p /tmp/jph-test
./build/recv /tmp/jph-test
```

In another terminal:

```sh
python3 - <<'PY'
import socket, struct, os
payload = os.urandom(4096)   # any bytes — not a real codestream
with socket.create_connection(('127.0.0.1', 4001)) as s:
    s.sendall(struct.pack('!I', len(payload)) + payload)
PY
```

You should see a `…bytes` line from `recv`, a new `.jph` file under `/tmp/jph-test/`, and a new document in `test.item1`:

```sh
ls /tmp/jph-test/
mongosh --quiet --eval 'db.getSiblingDB("test").item1.find().sort({_id:-1}).limit(1)'
```

(The resulting `.jph` file is not a valid HTJ2K image — it's random bytes wrapped in the JPH header — but it exercises the full receive/write/insert path.)

## Search UI (demo)

A small Flask app under [`demo/`](demo/) queries `test.item1` by timestamp range and serves the matching `.jph` files. Setup, env vars (`HOST`, `PORT`, `ARCHIVE_DIR`, `DISPLAY_TZ`, …), and the planned [OpenHTJ2K](https://github.com/osamu620/OpenHTJ2K) wasm-decoder preview integration are documented in [`demo/README.md`](demo/README.md).

---

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `cmake` can't find `mongocxx` | Step 4 or 5 above wasn't installed, or it landed outside `/usr/local`. Re-run `sudo ldconfig`, or pass `-DCMAKE_PREFIX_PATH=/your/prefix`. |
| `./build/recv: error while loading shared libraries: libmongocxx.so...` | Loader doesn't see `/usr/local/lib`. Either `sudo ldconfig` (one-time) or run with `LD_LIBRARY_PATH=/usr/local/lib ./build/recv`. |
| Receiver prints `bind/listen` and exits | Another process is on port 4001 (likely a previous `recv` instance). `ss -ltnp '( sport = :4001 )'` to find it. |
| `mongocxx::exception` on first frame | `mongod` isn't running, or it's bound to a non-default port. `systemctl status mongod`. |
| Demo's `python3 -m venv .venv` fails with "ensurepip is not available" | `sudo apt install python3.12-venv`. |
| Filenames look 9 hours off | System tz isn't what you expect — see step 2. New files use the tz `recv` started under; restart `recv` after changing tz. |
