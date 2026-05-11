# demo — frame search UI

A small Flask app that queries the MongoDB collection populated by the receiver and lists matching `.jph` frames for download. Search by a local-time range; results are sorted newest-first and capped by a `limit` parameter.

## Setup

```sh
cd demo
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## Run

The receiver writes `.jph` files into its working directory. Point the demo at that directory via `ARCHIVE_DIR`:

```sh
ARCHIVE_DIR=/path/to/jph/archive .venv/bin/python app.py
```

Open http://127.0.0.1:5000/.

Environment overrides (all optional):

| Variable | Default | Purpose |
| --- | --- | --- |
| `HOST` | `127.0.0.1` | Bind address. Use `0.0.0.0` to listen on all interfaces, or a specific LAN/WAN IP. |
| `PORT` | `5000` | TCP port |
| `DEBUG` | `1` | Flask debug + auto-reload. Set to `0` when binding to a non-loopback address (see warning below). |
| `MONGODB_URI` | `mongodb://localhost:27017/` | Driver URI |
| `MONGODB_DB` | `test` | Database name (matches `main.cpp`) |
| `MONGODB_COLLECTION` | `item1` | Collection name (matches `main.cpp`) |
| `ARCHIVE_DIR` | `.` (cwd) | Where `.jph` files live on disk |
| `DISPLAY_TZ` | _(system local)_ | IANA tz name used for both the form inputs and the result column, e.g. `Asia/Tokyo`. Set this if the server is running in UTC but you want to query/display in another zone. |

> **⚠ Security note.** Flask's debug mode exposes the Werkzeug interactive debugger, which executes arbitrary Python from anyone who can reach the port. **Always set `DEBUG=0` when binding to anything other than `127.0.0.1`** (e.g. `HOST=0.0.0.0` or a LAN IP). For production-style serving in front of untrusted networks, run behind a real WSGI server (gunicorn, uWSGI) instead of `app.py`.

Example, exposing on the LAN:

```sh
HOST=0.0.0.0 DEBUG=0 ARCHIVE_DIR=/path/to/jph .venv/bin/python app.py
```

## Preview (TODO)

The result rows currently show metadata + download only. The UI has a `Preview` column with a placeholder and an integration note pointing at the [OpenHTJ2K](https://github.com/osamu620/OpenHTJ2K) wasm decoder (reference deployment: <https://htj2k-demo.pages.dev>). The `/file/<fname>` endpoint already serves the raw `.jph` bytes, so wiring the decoder up is a frontend-only change — load the wasm module once, fetch each row's bytes, decode, blit to a `<canvas>`.

## Notes

- Filename validation rejects anything not matching the receiver's
  `YYYY-MM-DD-HH-MM-SS.mmm.jph` pattern, which prevents path traversal in `/file/`.
- `tz_aware=True` on the Mongo client keeps timestamps as UTC-aware
  `datetime` objects; the route converts to local time for display only.
