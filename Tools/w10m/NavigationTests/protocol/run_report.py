#!/usr/bin/env python3
"""Run/scenario/assertion report format for W10M NavigationTests (M10 task 9.2).

Protocol ``w10m-navigation-report`` version 1 (JSON, UTF-8):

    {
        "protocol": "w10m-navigation-report",
        "version": 1,
        "run_id": "<non-empty string>",
        "package_identity": "<optional string>",
        "scenarios": [
            {
                "scenario_id": "<non-empty string, unique within the run>",
                "timed_out": false,
                "assertions": [
                    {
                        "assertion_id": "<non-empty string, unique within the scenario>",
                        "passed": true,
                        "detail": "<optional free text, must not carry secrets>"
                    }
                ]
            }
        ]
    }

Strictness rules (see aggregate.py for run verdicts):

* Only ``protocol`` == ``REPORT_PROTOCOL`` and ``version`` == ``REPORT_VERSION``
  are accepted. Anything else is malformed.
* ``run_id`` and every ``scenario_id``/``assertion_id`` must be a non-empty
  string. Duplicate scenario or assertion ids are malformed.
* Every scenario must carry a non-empty ``assertions`` list. ``passed`` must
  be an actual JSON boolean (``True``/``False``, never ``0``/``1`` or
  strings). ``detail`` must be a string when present. ``timed_out`` must be
  a boolean when present; ``True`` marks a scenario-level timeout.
* Diagnostics must never contain cookie/credential values. ``detail`` text
  matching a secret ``key=value``/``key: value`` shape, or containing a
  caller-supplied known secret value, is a secret leak (malformed for
  aggregation purposes and always redacted from diagnostics).

Python 3 standard library only. All paths handled here are ASCII.
"""

import json
import re

REPORT_PROTOCOL = "w10m-navigation-report"
REPORT_VERSION = 1

# Detail text must never log values for these keys (case-insensitive).
# A bare word such as "PASS cookie" is fine; "cookie=m9=fixture" is a leak.
SECRET_KEY_TOKENS = (
    "cookie",
    "set-cookie",
    "authorization",
    "proxy-authorization",
    "password",
    "passwd",
    "credential",
    "secret",
    "session-key",
    "api-key",
    "apikey",
    "access-token",
    "auth-token",
    "bearer",
)

_SECRET_KEY_PATTERN = re.compile(
    r"(?i)\b(" + "|".join(re.escape(token) for token in SECRET_KEY_TOKENS) + r")\b\s*[:=]"
)

REDACTED = "***"


def _is_non_empty_string(value):
    """Return True when value is a non-empty (after strip) string."""
    return isinstance(value, str) and bool(value.strip())


def redact_text(text, secrets=()):
    """Return text with every known secret value replaced by REDACTED."""
    redacted = text if isinstance(text, str) else str(text)
    for secret in secrets or ():
        if isinstance(secret, str) and secret:
            redacted = redacted.replace(secret, REDACTED)
    return redacted


def contains_secret_text(text, secrets=()):
    """Return True when text appears to carry a cookie/credential value.

    Two independent signals: an explicit caller-supplied secret value
    occurs verbatim, or the text matches a ``secret-key [: lower.=] value``
    shape (e.g. ``cookie=m9=fixture`` or ``Authorization: Bearer abc``).
    A bare word such as ``PASS cookie`` without ``:``/``=`` is not a leak.
    """
    if not isinstance(text, str):
        return False
    for secret in secrets or ():
        if isinstance(secret, str) and secret and secret in text:
            return True
    return _SECRET_KEY_PATTERN.search(text) is not None


def make_assertion(assertion_id, passed, detail=""):
    """Build one assertion record (validation happens in parse/aggregate)."""
    return {
        "assertion_id": assertion_id,
        "passed": passed,
        "detail": detail,
    }


def make_scenario(scenario_id, assertions, timed_out=False):
    """Build one scenario record from a list of assertion records."""
    return {
        "scenario_id": scenario_id,
        "timed_out": timed_out,
        "assertions": list(assertions),
    }


def make_report(run_id, scenarios, package_identity=None):
    """Build a report record for run_id covering the given scenarios."""
    report = {
        "protocol": REPORT_PROTOCOL,
        "version": REPORT_VERSION,
        "run_id": run_id,
        "scenarios": list(scenarios),
    }
    if package_identity is not None:
        report["package_identity"] = package_identity
    return report


def encode_report(report):
    """Encode a report record to deterministic UTF-8 JSON bytes."""
    return json.dumps(report, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _validate_assertion(record, index, seen_ids):
    """Validate one assertion record, returning a list of error strings."""
    errors = []
    where = "scenarios[].assertions[%d]" % index
    if not isinstance(record, dict):
        return [where + ": assertion must be an object"]
    assertion_id = record.get("assertion_id")
    if not _is_non_empty_string(assertion_id):
        errors.append(where + ": assertion_id must be a non-empty string")
    elif assertion_id in seen_ids:
        errors.append("duplicate assertion_id: " + assertion_id)
    else:
        seen_ids.add(assertion_id)
    if record.get("passed") is not True and record.get("passed") is not False:
        errors.append(where + ": passed must be a JSON boolean")
    detail = record.get("detail", "")
    if "detail" in record and not isinstance(detail, str):
        errors.append(where + ": detail must be a string")
    elif isinstance(detail, str) and contains_secret_text(detail):
        errors.append(where + ": detail must not contain cookie/credential values")
    return errors


def _validate_scenario(record, index, seen_ids):
    """Validate one scenario record, returning a list of error strings."""
    errors = []
    where = "scenarios[%d]" % index
    if not isinstance(record, dict):
        return [where + ": scenario must be an object"]
    scenario_id = record.get("scenario_id")
    if not _is_non_empty_string(scenario_id):
        errors.append(where + ": scenario_id must be a non-empty string")
        scenario_id = where
    elif scenario_id in seen_ids:
        errors.append("duplicate scenario_id: " + scenario_id)
    else:
        seen_ids.add(scenario_id)
    if "timed_out" in record and record["timed_out"] is not True and record["timed_out"] is not False:
        errors.append(where + ": timed_out must be a boolean")
    assertions = record.get("assertions")
    if not isinstance(assertions, list) or not assertions:
        errors.append(where + ": assertions must be a non-empty list")
    else:
        seen_assertions = set()
        for position, item in enumerate(assertions):
            errors.extend(_validate_assertion(item, position, seen_assertions))
    return errors


def validate_report(report):
    """Validate a decoded report object, returning a list of error strings."""
    if not isinstance(report, dict):
        return ["report must be a JSON object"]
    errors = []
    if report.get("protocol") != REPORT_PROTOCOL:
        errors.append("protocol must be %r" % REPORT_PROTOCOL)
    if report.get("version") != REPORT_VERSION:
        errors.append("version must be %r" % REPORT_VERSION)
    if not _is_non_empty_string(report.get("run_id")):
        errors.append("run_id must be a non-empty string")
    if "package_identity" in report and not isinstance(report["package_identity"], str):
        errors.append("package_identity must be a string")
    scenarios = report.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        errors.append("scenarios must be a non-empty list")
    else:
        seen_scenarios = set()
        for position, item in enumerate(scenarios):
            errors.extend(_validate_scenario(item, position, seen_scenarios))
    return errors


def parse_report(data):
    """Decode and validate report bytes, returning (report, errors).

    ``report`` is the decoded object on success or None when malformed.
    ``errors`` is empty on success. Error strings never echo secret values.
    """
    if isinstance(data, str):
        data = data.encode("utf-8")
    if not isinstance(data, (bytes, bytearray)):
        return None, ["report must be UTF-8 JSON bytes"]
    try:
        report = json.loads(bytes(data).decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as exc:
        return None, ["report is not valid JSON: %s" % exc.__class__.__name__]
    errors = validate_report(report)
    if errors:
        return None, errors
    return report, []
