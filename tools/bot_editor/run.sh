#!/bin/sh
# Quark Bot Editor: starts the local server (http://127.0.0.1:8769/) and opens it in the browser.
# Extra arguments go to serve.py (--port N, --maptool PATH, --data-dir DIR, --maps-dir DIR, --no-open).
cd "$(dirname "$0")" || exit 1
exec python3 serve.py "$@"
