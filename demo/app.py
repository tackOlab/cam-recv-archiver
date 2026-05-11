import os
import re
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

from flask import Flask, abort, render_template, request, send_from_directory
from pymongo import DESCENDING, MongoClient

MONGODB_URI = os.environ.get("MONGODB_URI", "mongodb://localhost:27017/")
MONGODB_DB = os.environ.get("MONGODB_DB", "test")
MONGODB_COLLECTION = os.environ.get("MONGODB_COLLECTION", "item1")
ARCHIVE_DIR = Path(os.environ.get("ARCHIVE_DIR", ".")).resolve()
HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "5000"))
DEBUG = os.environ.get("DEBUG", "1") not in ("0", "false", "False", "")
_TZ_NAME = os.environ.get("DISPLAY_TZ", "").strip()
DISPLAY_TZ = ZoneInfo(_TZ_NAME) if _TZ_NAME else None  # None → system local tz

FNAME_RE = re.compile(r"^\d{4}-\d{2}-\d{2}-\d{2}-\d{2}-\d{2}\.\d{3}\.jph$")

app = Flask(__name__)
_collection = MongoClient(MONGODB_URI, tz_aware=True)[MONGODB_DB][MONGODB_COLLECTION]


def _parse_local_dt(value):
    if not value:
        return None
    dt = datetime.fromisoformat(value)
    if dt.tzinfo is None:
        # Interpret form value in DISPLAY_TZ (what the user sees in labels);
        # fall back to system local when DISPLAY_TZ is unset.
        dt = dt.replace(tzinfo=DISPLAY_TZ) if DISPLAY_TZ else dt.astimezone()
    return dt.astimezone(timezone.utc)


def _to_display_tz(dt):
    if dt is None:
        return None
    return dt.astimezone(DISPLAY_TZ) if DISPLAY_TZ else dt.astimezone()


@app.get("/")
def index():
    frm_raw = request.args.get("from", "")
    to_raw = request.args.get("to", "")
    try:
        limit = max(1, min(int(request.args.get("limit") or 100), 500))
    except ValueError:
        limit = 100

    results = None
    error = None
    if frm_raw or to_raw:
        try:
            frm_dt = _parse_local_dt(frm_raw)
            to_dt = _parse_local_dt(to_raw)
            q = {}
            if frm_dt or to_dt:
                ts = {}
                if frm_dt:
                    ts["$gte"] = frm_dt
                if to_dt:
                    ts["$lte"] = to_dt
                q["timestamp"] = ts

            results = []
            for doc in _collection.find(q).sort("timestamp", DESCENDING).limit(limit):
                fname = doc.get("fname")
                size = None
                if fname:
                    p = ARCHIVE_DIR / fname
                    if p.is_file():
                        size = p.stat().st_size
                results.append({
                    "fname": fname,
                    "timestamp": _to_display_tz(doc.get("timestamp")),
                    "size": size,
                })
        except (ValueError, TypeError) as e:
            error = f"Invalid query: {e}"

    tz_label = _to_display_tz(datetime.now(timezone.utc)).tzname() or "local"

    return render_template(
        "index.html",
        results=results,
        error=error,
        frm=frm_raw,
        to=to_raw,
        limit=limit,
        archive_dir=str(ARCHIVE_DIR),
        mongo_uri=MONGODB_URI,
        mongo_ns=f"{MONGODB_DB}.{MONGODB_COLLECTION}",
        tz_label=tz_label,
    )


@app.get("/file/<string:fname>")
def file_(fname):
    if not FNAME_RE.match(fname):
        abort(400)
    return send_from_directory(
        ARCHIVE_DIR, fname, mimetype="application/octet-stream"
    )


if __name__ == "__main__":
    app.run(host=HOST, port=PORT, debug=DEBUG)
