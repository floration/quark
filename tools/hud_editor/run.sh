#!/bin/sh
# Quark HUD Editor: starts the local server (http://127.0.0.1:8774/) and opens the browser.
cd "$(dirname "$0")" && exec python3 serve.py "$@"
