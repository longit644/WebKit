#!/usr/bin/env python3
"""Host-runnable unit tests for the 9.2 report/aggregation protocol.

Covers task 9.2 acceptance: a valid run passes, while each failure mode
(malformed, missing, timeout, a single failed assertion, secret leak)
fails as specified, and cookie/credential values never reach logs.

Run from the repository root either as::

    python -m unittest discover -s Tools/w10m/NavigationTests/protocol -p test_reports.py -v

or::

    python Tools/w10m/NavigationTests/protocol/test_reports.py

Legacy root paths (Tools/w10m/NavigationTests/test_reports.py and discovery
with -s Tools/w10m/NavigationTests) keep working through thin forwarders
(M10 task 9.1) and run the same suite.

Python 3 standard library only. All paths used here are ASCII.
"""

import contextlib
import io
import json
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve()
# Appended (never prepended) so rooted discovery keeps resolving the legacy
# root modules first; these entries are a last resort for protocol-dir runs.
for _candidate in (_HERE.parents[1], _HERE.parents[4]):
    if str(_candidate) not in sys.path:
        sys.path.append(str(_candidate))

try:
    import aggregate
    import run_report
    from aggregate import aggregate_report, aggregate_runs
    from run_report import encode_report, make_assertion, make_report, make_scenario
except ImportError:  # pragma: no cover - repository-root discovery fallback
    from Tools.w10m.NavigationTests.protocol import aggregate, run_report
    from Tools.w10m.NavigationTests.protocol.aggregate import aggregate_report, aggregate_runs
    from Tools.w10m.NavigationTests.protocol.run_report import (
        encode_report,
        make_assertion,
        make_report,
        make_scenario,
    )

try:
    from server import FixtureHandler
except ImportError:  # pragma: no cover - new-layout package fallback
    from Tools.w10m.NavigationTests.host.fixtures.server import FixtureHandler

COOKIE_SECRET = "m9=fixture-secret-value"
AUTH_SECRET = "Bearer device-token-abc123"


def valid_payload(run_id="run-001", scenarios=("nav-basic",)):
    """Return encoded bytes for an all-passing report."""
    return encode_report(make_report(
        run_id,
        [make_scenario(scenario_id, [make_assertion("a-loads", True, "PASS loaded"),
                                     make_assertion("a-style", True, "PASS style")])
         for scenario_id in scenarios],
        package_identity="WebKitProbe.MiniBrowser_1.0.0.99_arm",
    ))


class AggregationProtocolTests(unittest.TestCase):
    """Verify strict run/scenario/assertion aggregation semantics."""

    def test_valid_run_passes(self):
        """An all-passing report with matching identity aggregates to pass."""
        result = aggregate_report(valid_payload(),
                                  expected_run_id="run-001",
                                  expected_scenarios=["nav-basic"])
        self.assertTrue(result.passed, result.format_diagnostics())
        self.assertEqual(result.assertions_failed, 0)
        self.assertEqual(result.scenarios_total, 1)
        self.assertEqual(result.assertions_total, 2)

    def test_malformed_report_fails(self):
        """Bad JSON and schema violations each fail the run."""
        malformed = [
            b"{not json",
            b"[1, 2, 3]",
            json.dumps({"protocol": "wrong", "version": 1,
                        "run_id": "r", "scenarios": []}).encode(),
            json.dumps({"protocol": "w10m-navigation-report", "version": 999,
                        "run_id": "r", "scenarios": [{"scenario_id": "s",
                        "assertions": [{"assertion_id": "a", "passed": True}]}]}).encode(),
            json.dumps({"protocol": "w10m-navigation-report", "version": 1,
                        "run_id": "", "scenarios": [{"scenario_id": "s",
                        "assertions": [{"assertion_id": "a", "passed": True}]}]}).encode(),
            json.dumps({"protocol": "w10m-navigation-report", "version": 1,
                        "run_id": "r", "scenarios": []}).encode(),
            # Duplicate scenario ids.
            encode_report(make_report("r", [make_scenario("s", [make_assertion("a", True)]),
                                            make_scenario("s", [make_assertion("b", True)])])),
            # Duplicate assertion ids.
            encode_report(make_report("r", [make_scenario(
                "s", [make_assertion("a", True), make_assertion("a", True)])])),
            # Non-boolean passed (int 1 must not count as pass).
            json.dumps({"protocol": "w10m-navigation-report", "version": 1,
                        "run_id": "r", "scenarios": [{"scenario_id": "s",
                        "assertions": [{"assertion_id": "a", "passed": 1}]}]}).encode(),
            # Empty assertions list.
            encode_report({"protocol": "w10m-navigation-report", "version": 1,
                           "run_id": "r",
                           "scenarios": [{"scenario_id": "s", "assertions": []}]}),
        ]
        for position, payload in enumerate(malformed):
            with self.subTest(case=position):
                result = aggregate_report(payload, expected_run_id="r")
                self.assertFalse(result.passed)
                self.assertTrue(any("malformed" in message or "mismatch" in message
                                    for message in result.failures),
                                result.format_diagnostics())

    def test_missing_report_fails(self):
        """A missing (never received) report fails the run."""
        result = aggregate_report(None, expected_run_id="run-001",
                                  expected_scenarios=["nav-basic"])
        self.assertFalse(result.passed)
        self.assertTrue(any("missing" in message for message in result.failures))

    def test_timed_out_report_fails(self):
        """A report deadline expiry fails even with otherwise valid bytes."""
        result = aggregate_report(valid_payload(), expected_run_id="run-001",
                                  expected_scenarios=["nav-basic"], timed_out=True)
        self.assertFalse(result.passed)
        self.assertTrue(any("timed out" in message for message in result.failures))

    def test_scenario_timeout_fails(self):
        """A scenario marked timed_out fails the run."""
        payload = encode_report(make_report(
            "run-001", [make_scenario("nav-basic", [make_assertion("a", True)],
                                      timed_out=True)]))
        result = aggregate_report(payload, expected_run_id="run-001")
        self.assertFalse(result.passed)
        self.assertTrue(any("timed out" in message for message in result.failures))

    def test_single_failed_assertion_fails_run(self):
        """Exactly one failed assertion fails the whole run (no partial pass)."""
        payload = encode_report(make_report(
            "run-001",
            [make_scenario("nav-basic", [make_assertion("a-loads", True, "PASS loaded"),
                                         make_assertion("a-style", False, "FAIL color")]),
             make_scenario("nav-history", [make_assertion("a-back", True, "PASS back")])]))
        result = aggregate_report(payload, expected_run_id="run-001")
        self.assertFalse(result.passed)
        self.assertEqual(result.assertions_failed, 1)
        self.assertTrue(any("nav-basic/a-style" in message for message in result.failures))

    def test_missing_and_unexpected_scenario_fail(self):
        """Absent expected scenarios and unexpected extras both fail."""
        missing = aggregate_report(valid_payload(scenarios=("nav-basic",)),
                                   expected_scenarios=["nav-basic", "nav-history"])
        self.assertFalse(missing.passed)
        self.assertTrue(any("missing scenario" in message for message in missing.failures))
        extra = aggregate_report(valid_payload(scenarios=("nav-basic", "rogue",)),
                                 expected_scenarios=["nav-basic"])
        self.assertFalse(extra.passed)
        self.assertTrue(any("unexpected scenario" in message for message in extra.failures))

    def test_run_id_mismatch_fails(self):
        """A report for the wrong run must not satisfy the expected run."""
        result = aggregate_report(valid_payload(run_id="run-002"),
                                  expected_run_id="run-001")
        self.assertFalse(result.passed)
        self.assertTrue(any("run_id mismatch" in message for message in result.failures))

    def test_secret_value_leak_fails_and_is_redacted(self):
        """A known cookie value in a detail fails and never appears in output."""
        payload = encode_report(make_report(
            "run-001", [make_scenario("cookies", [make_assertion(
                "a-persist", True, "echoed " + COOKIE_SECRET)])]))
        result = aggregate_report(payload, expected_run_id="run-001",
                                  secrets=(COOKIE_SECRET,))
        self.assertFalse(result.passed)
        for rendered in (result.format_diagnostics(secrets=(COOKIE_SECRET,)),
                         str(result),
                         json.dumps(result.to_dict(secrets=(COOKIE_SECRET,)))):
            self.assertNotIn(COOKIE_SECRET, rendered)
            self.assertNotIn("fixture-secret", rendered)

    def test_secret_key_value_shape_fails_without_supplied_secrets(self):
        """A ``cookie=...`` detail is a leak even when no secret list is given."""
        payload = encode_report(make_report(
            "run-001", [make_scenario("cookies", [make_assertion(
                "a-echo", True, "cookie=" + COOKIE_SECRET)])]))
        result = aggregate_report(payload, expected_run_id="run-001")
        self.assertFalse(result.passed)
        # Schema validation rejects secret-shaped details as malformed.
        self.assertTrue(any("malformed" in message or "secret" in message
                            for message in result.failures))

    def test_plain_cookie_word_is_not_a_leak(self):
        """Regression guard: 'PASS cookie' wording must still pass."""
        payload = encode_report(make_report(
            "run-001", [make_scenario("cookies", [make_assertion(
                "a-echo", True, "PASS cookie marker present")])]))
        result = aggregate_report(payload, expected_run_id="run-001")
        self.assertTrue(result.passed, result.format_diagnostics())

    def test_authorization_header_leak_fails(self):
        """An Authorization header value in diagnostics fails the run."""
        payload = encode_report(make_report(
            "run-001", [make_scenario("tls", [make_assertion(
                "a-auth", True, "Authorization: " + AUTH_SECRET)])]))
        result = aggregate_report(payload, expected_run_id="run-001",
                                  secrets=(AUTH_SECRET,))
        self.assertFalse(result.passed)
        self.assertNotIn(AUTH_SECRET, result.format_diagnostics(secrets=(AUTH_SECRET,)))

    def test_fixture_access_log_never_carries_secrets(self):
        """The fixture server suppresses access logs so cookies never leak."""
        stream = io.StringIO()
        with contextlib.redirect_stderr(stream):
            FixtureHandler.log_message(
                object.__new__(FixtureHandler),
                "%s %s", "/cookies/echo",
                "Cookie: " + COOKIE_SECRET)
        captured = stream.getvalue()
        self.assertNotIn(COOKIE_SECRET, captured)
        self.assertNotIn("fixture-secret", captured)

    def test_aggregate_runs_requires_every_run_to_pass(self):
        """One failed run fails the multi-run aggregate."""
        passing = {"data": valid_payload(run_id="run-001"),
                   "expected_run_id": "run-001"}
        failing = {"data": None, "expected_run_id": "run-002"}
        combined = aggregate_runs([passing, failing])
        self.assertFalse(combined.passed)
        self.assertTrue(any("run[1]" in message for message in combined.failures))
        all_passing = aggregate_runs([passing, dict(passing)])
        self.assertTrue(all_passing.passed, all_passing.format_diagnostics())
        empty = aggregate_runs([])
        self.assertFalse(empty.passed)


if __name__ == "__main__":
    unittest.main()
