#!/usr/bin/env python3
"""Aggregate run/scenario/assertion reports with strict failure semantics.

M10 task 9.2 protocol: the device/host runner emits one JSON report per run
(see run_report.py). The aggregator combines reports across runs and fails
the run when ANY of the following holds:

* the report is missing (never received),
* the report timed out (deadline expired before receipt),
* the report is malformed (bad JSON or schema violation),
* any scenario timed out,
* a SINGLE assertion has ``passed`` != True,
* any expected scenario is missing (or an unexpected one appears),
* the run id does not match the expected run,
* any cookie/credential value appears in the report or diagnostics.

There is no partial pass: one failed assertion fails the whole run, and one
failed run fails the aggregate. Diagnostics never include secret values;
leaked values are replaced with ``***`` before they reach logs.

Python 3 standard library only. All paths handled here are ASCII.
"""

try:
    from run_report import (
        REDACTED,
        contains_secret_text,
        parse_report,
        redact_text,
    )
except ImportError:  # pragma: no cover - package-style discovery fallback
    from Tools.w10m.NavigationTests.protocol.run_report import (
        REDACTED,
        contains_secret_text,
        parse_report,
        redact_text,
    )


class AggregateResult:
    """Verdict for one run (or several runs combined)."""

    def __init__(self, passed, failures=None, run_id=None,
                 scenarios_total=0, assertions_total=0, assertions_failed=0):
        self.passed = bool(passed)
        self.failures = list(failures or [])
        self.run_id = run_id
        self.scenarios_total = scenarios_total
        self.assertions_total = assertions_total
        self.assertions_failed = assertions_failed

    def to_dict(self, secrets=()):
        """Return a JSON-serializable verdict with secrets redacted."""
        return {
            "passed": self.passed,
            "run_id": self.run_id,
            "scenarios_total": self.scenarios_total,
            "assertions_total": self.assertions_total,
            "assertions_failed": self.assertions_failed,
            "failures": [redact_text(message, secrets) for message in self.failures],
        }

    def format_diagnostics(self, secrets=()):
        """Render one log-safe line per failure (secrets redacted)."""
        status = "PASS" if self.passed else "FAIL"
        lines = ["aggregate %s run_id=%s scenarios=%d assertions=%d failed=%d" % (
            status, self.run_id, self.scenarios_total,
            self.assertions_total, self.assertions_failed)]
        for message in self.failures:
            lines.append("  - " + redact_text(message, secrets))
        return "\n".join(lines)

    def __str__(self):
        return self.format_diagnostics()

    def __bool__(self):
        return self.passed


def _raw_text(data):
    """Best-effort decode of raw report bytes for secret scanning."""
    if data is None:
        return ""
    if isinstance(data, str):
        return data
    try:
        return bytes(data).decode("utf-8", errors="replace")
    except (TypeError, ValueError):
        return ""


def _scan_raw_for_secrets(data, secrets):
    """Return True when raw bytes contain a caller-supplied secret value."""
    text = _raw_text(data)
    for secret in secrets or ():
        if isinstance(secret, str) and secret and secret in text:
            return True
    return False


def aggregate_report(data, expected_run_id=None, expected_scenarios=None,
                     secrets=(), timed_out=False):
    """Aggregate a single run report, returning an AggregateResult.

    Args:
        data: UTF-8 JSON report bytes (or None when the report is missing).
        expected_run_id: required run id; mismatches fail the run.
        expected_scenarios: iterable of scenario ids that must appear exactly.
        secrets: known cookie/credential values that must never appear.
        timed_out: True when the report deadline expired (missing/timeout).
    """
    secrets = tuple(secrets or ())
    failures = []

    if timed_out:
        failures.append("report timed out before receipt")
        if data is None:
            return AggregateResult(False, failures, expected_run_id)
    if data is None:
        failures.append("report missing: no payload received")
        return AggregateResult(False, failures, expected_run_id)

    if _scan_raw_for_secrets(data, secrets):
        failures.append("report contains a cookie/credential value (%s redacted)" % REDACTED)
        return AggregateResult(False, failures, expected_run_id)

    report, errors = parse_report(data)
    if errors:
        failures.extend("malformed report: " + message for message in errors)
        return AggregateResult(False, failures, expected_run_id)

    if expected_run_id is not None and report["run_id"] != expected_run_id:
        failures.append("run_id mismatch: expected %r got %r" % (
            expected_run_id, report["run_id"]))

    scenarios = report["scenarios"]
    by_id = {scenario["scenario_id"]: scenario for scenario in scenarios}
    if expected_scenarios is not None:
        expected = list(expected_scenarios)
        for scenario_id in expected:
            if scenario_id not in by_id:
                failures.append("missing scenario report: " + str(scenario_id))
        for scenario_id in by_id:
            if scenario_id not in expected:
                failures.append("unexpected scenario report: " + str(scenario_id))

    scenarios_total = len(scenarios)
    assertions_total = 0
    assertions_failed = 0
    for scenario in scenarios:
        scenario_id = scenario["scenario_id"]
        if scenario.get("timed_out") is True:
            failures.append("scenario timed out: " + str(scenario_id))
        for assertion in scenario["assertions"]:
            assertions_total += 1
            detail = assertion.get("detail", "")
            if isinstance(detail, str) and contains_secret_text(detail, secrets):
                failures.append("secret value in diagnostics for %s/%s (%s redacted)" % (
                    scenario_id, assertion.get("assertion_id"), REDACTED))
                assertions_failed += 1
            elif assertion.get("passed") is not True:
                failures.append("assertion failed: %s/%s" % (
                    scenario_id, assertion.get("assertion_id")))
                assertions_failed += 1

    passed = not failures
    return AggregateResult(passed, failures, report["run_id"],
                           scenarios_total, assertions_total, assertions_failed)


def aggregate_runs(items):
    """Aggregate several runs; one failed run fails the aggregate.

    Args:
        items: iterable of dicts accepted as aggregate_report() kwargs
            (``data``, ``expected_run_id``, ``expected_scenarios``,
            ``secrets``, ``timed_out``).

    An empty item list fails (missing evidence must not pass).
    """
    items = list(items or [])
    if not items:
        return AggregateResult(False, ["no runs to aggregate"])
    failures = []
    scenarios_total = 0
    assertions_total = 0
    assertions_failed = 0
    run_ids = []
    for position, item in enumerate(items):
        kwargs = dict(item or {})
        result = aggregate_report(**kwargs) if kwargs else aggregate_report(None)
        run_ids.append(result.run_id)
        scenarios_total += result.scenarios_total
        assertions_total += result.assertions_total
        assertions_failed += result.assertions_failed
        if not result.passed:
            failures.append("run[%d] failed: run_id=%s" % (position, result.run_id))
            failures.extend("run[%d]: %s" % (position, message) for message in result.failures)
    passed = not failures
    return AggregateResult(passed, failures, run_ids if len(run_ids) > 1 else (run_ids[0] if run_ids else None),
                           scenarios_total, assertions_total, assertions_failed)
