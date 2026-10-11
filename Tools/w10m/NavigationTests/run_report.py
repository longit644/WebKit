#!/usr/bin/env python3
"""Backward-compatible report-format import path (M10 task 9.1).

Canonical implementation moved to protocol/run_report.py. This shim keeps
the legacy ``import run_report`` path working by delegating attribute
access to the canonical module. ASCII paths only.
"""
import importlib.util
import sys
from pathlib import Path

_CANONICAL = Path(__file__).resolve().parent / "protocol" / "run_report.py"


def _load_canonical():
    """Load the canonical report module without relying on sys.path layout."""
    spec = importlib.util.spec_from_file_location("protocol_run_report", _CANONICAL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


_canonical = _load_canonical()


def __getattr__(name):
    """Delegate legacy ``from run_report import <name>`` access to the canonical module."""
    return getattr(_canonical, name)
