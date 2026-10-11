#!/usr/bin/env python3
"""Backward-compatible report-protocol test entry point (M10 task 9.1).

Canonical tests moved to protocol/test_reports.py. Importing this shim
defines no test cases (so rooted discovery never double-runs the suite);
executing it directly runs the canonical 14-test suite::

    python Tools/w10m/NavigationTests/test_reports.py -v
    python -m unittest discover -s Tools/w10m/NavigationTests -p test_reports.py -v
"""
import importlib.util
import sys
from pathlib import Path

_CANONICAL = Path(__file__).resolve().parent / "protocol" / "test_reports.py"


def _load_canonical():
    """Load the canonical test module with its own directory first on sys.path."""
    sys.path.insert(0, str(_CANONICAL.parent))
    spec = importlib.util.spec_from_file_location("protocol_test_reports", _CANONICAL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


if __name__ == "__main__":
    import unittest

    _module = _load_canonical()
    unittest.main(module=_module)
