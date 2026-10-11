#!/usr/bin/env python3
"""Backward-compatible fixture-server launcher (M10 task 9.1).

Canonical implementation moved to host/fixtures/server.py. This shim keeps
every documented launch path working::

    python Tools/w10m/NavigationTests/server.py --host 0.0.0.0 --port 8000
    python Tools/w10m/NavigationTests/server.py --host 0.0.0.0 --port 8001

and preserves the legacy import path ``from server import ...`` by
delegating attribute access to the canonical module. ASCII paths only.
"""
import importlib.util
import sys
from pathlib import Path

_CANONICAL = Path(__file__).resolve().parent / "host" / "fixtures" / "server.py"


def _load_canonical():
    """Load the canonical fixture server without relying on sys.path layout."""
    spec = importlib.util.spec_from_file_location("host_fixtures_server", _CANONICAL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


_canonical = _load_canonical()
main = _canonical.main


def __getattr__(name):
    """Delegate legacy ``from server import <name>`` access to the canonical module."""
    return getattr(_canonical, name)


if __name__ == "__main__":
    main()
