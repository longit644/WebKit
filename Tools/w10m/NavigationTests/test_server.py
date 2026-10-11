#!/usr/bin/env python3
"""Backward-compatible fixture-host test entry point (M10 task 9.1).

Canonical tests moved to host/fixtures/test_server.py. Importing this shim
defines no test cases (so rooted discovery never double-runs the suite);
executing it directly runs the canonical 15-test suite::

    python Tools/w10m/NavigationTests/test_server.py -v
    python -m unittest discover -s Tools/w10m/NavigationTests -p test_server.py -v
"""
import importlib.util
import sys
from pathlib import Path

_CANONICAL = Path(__file__).resolve().parent / "host" / "fixtures" / "test_server.py"


def _load_canonical():
    """Load the canonical test module with its own directory first on sys.path."""
    sys.path.insert(0, str(_CANONICAL.parent))
    spec = importlib.util.spec_from_file_location("host_fixtures_test_server", _CANONICAL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


if __name__ == "__main__":
    import unittest

    _module = _load_canonical()
    unittest.main(module=_module)
