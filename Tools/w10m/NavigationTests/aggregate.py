#!/usr/bin/env python3
"""Backward-compatible aggregation import path (M10 task 9.1).

Canonical implementation moved to protocol/aggregate.py. This shim keeps
the legacy ``import aggregate`` path working by delegating attribute
access to the canonical module. ASCII paths only.
"""
import importlib.util
import sys
from pathlib import Path

_CANONICAL = Path(__file__).resolve().parent / "protocol" / "aggregate.py"


def _load_canonical():
    """Load the canonical aggregation module without relying on sys.path layout."""
    if str(_CANONICAL.parent) not in sys.path:
        sys.path.append(str(_CANONICAL.parent))
    spec = importlib.util.spec_from_file_location("protocol_aggregate", _CANONICAL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


_canonical = _load_canonical()


def __getattr__(name):
    """Delegate legacy ``from aggregate import <name>`` access to the canonical module."""
    return getattr(_canonical, name)
