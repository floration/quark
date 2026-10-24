#!/bin/sh
# Quark Weapons Editor: starts the local server (http://127.0.0.1:8773/) and opens it in the browser.
# Extra arguments go to serve.py (--port N, --maptool PATH, --quark-tests PATH, --data-dir DIR, --maps-dir DIR, --no-open, --install [FOLDER]).
cd "$(dirname "$0")" || exit 1
exec python3 serve.py "$@"
