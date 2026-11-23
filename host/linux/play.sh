#!/bin/sh
# play.sh -- start Ghost's launcher from this folder (any extra options are passed on)
cd "$(dirname "$0")" || exit 1
exec ./ghost "$@"
