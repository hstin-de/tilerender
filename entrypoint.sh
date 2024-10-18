#!/bin/bash
set -e

Xvfb :99 -screen 0 4096x4096x24 &

exec "$@"
