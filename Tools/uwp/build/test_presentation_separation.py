"""Negative variants prove the presentation separation gate detects regressions."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


# Load the one hyphenated CLI implementation instead of maintaining a second checker.
SPEC = importlib.util.spec_from_file_location("presentation_separation", Path(__file__).with_name("check-presentation-separation.py"))
# The same gate is used by the real tree and negative fixtures.
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)
# Owned framework areas covered by the separation gate.
OWNED = list(GATE.OWNED_DIRECTORIES)


class PresentationSeparationTests(unittest.TestCase):
    """Ensure completion inference, fixtures and engine coupling cannot enter owned code."""

    def fixture(self, relative="Source/WebKit/UIProcess/API/uwp/rendering/Sample.cpp", body="// Clean owned body.\n"):
        """Build a minimal owned tree and run the real separation gate."""
        with tempfile.TemporaryDirectory(dir=Path(tempfile.gettempdir()) / "opencode") as directory:
            repository = Path(directory)
            for name in OWNED:
                (repository / name).mkdir(parents=True, exist_ok=True)
            (repository / "Source/WebKit/UIProcess/API/uwp").mkdir(parents=True, exist_ok=True)
            path = repository / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body, encoding="utf-8")
            GATE.verify_tree(repository)

    def test_clean_owned_tree_passes(self):
        """An owned tree without inference markers is accepted."""
        self.fixture()

    def test_log_derived_completion_fails(self):
        """A log-scraped completion cannot become a presentation outcome."""
        with self.assertRaisesRegex(AssertionError, "inference"):
            self.fixture(body='if (strstr(line, "navigation-completed")) finish();\n')

    def test_fixture_content_fails(self):
        """Framework code cannot acquire fixture URLs or suite triggers."""
        with self.assertRaisesRegex(AssertionError, "fixture"):
            self.fixture(body='const char* url = "http://192.168.1.69/?uwp-suite=1";\n')

    def test_engine_include_fails(self):
        """Owned presentation code cannot include loader or DOM implementation headers."""
        with self.assertRaisesRegex(AssertionError, "coupling"):
            self.fixture(body='#include "LoaderStrategyUWP.h"\n')

    def test_downloader_reference_fails(self):
        """A transport downloader reference cannot enter the owned boundary."""
        with self.assertRaisesRegex(AssertionError, "downloader"):
            self.fixture(body='CurlRequest* request;\n')

    def test_terminal_payload_outside_control_fails(self):
        """Only the real control may synthesize terminal completion payloads."""
        with self.assertRaisesRegex(AssertionError, "Terminal payload"):
            self.fixture(relative="Source/WebKit/UIProcess/API/uwp/rendering/Sample.cpp",
                         body='auto args = ref new WebKitNavigationCompletedEventArgs;\n')


if __name__ == "__main__":
    unittest.main()
