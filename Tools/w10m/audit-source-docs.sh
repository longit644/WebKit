#!/bin/sh
# CX-aware W10M source documentation/format audit entry point (POSIX sh).
#
# Forwards to Tools/w10m/audit-source-docs.py from the repository root, so the
# audit runs against the implemented M10 placement regardless of the caller's
# working directory. Old direct invocations (python Tools/w10m/audit-source-docs.py)
# keep working; this wrapper only fixes the working directory.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT"
exec python "$ROOT/Tools/w10m/audit-source-docs.py" "$@"
