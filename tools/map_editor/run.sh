#!/bin/sh
# Quark Map Editor: starts the local server (127.0.0.1 only, a free port) and opens the page in your browser.
cd "$(dirname "$0")" && exec python3 ./serve.py "$@"
