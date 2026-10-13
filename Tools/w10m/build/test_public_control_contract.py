"""Negative variants prove the public component source boundary gate detects regressions."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


# Load the one hyphenated CLI implementation instead of maintaining a second checker.
SPEC = importlib.util.spec_from_file_location("public_control_contract", Path(__file__).with_name("check-public-control-contract.py"))
# The same gate is used by build manifests and negative fixtures.
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)
# Required actual compiled source names, used to create isolated mutation fixtures.
SOURCES = [
    "Source/WebKit/UIProcess/API/w10m/WebKitWebView.cpp",
    "Source/WebKit/UIProcess/API/w10m/WebKitNavigationEventArgs.cpp",
    "Source/WebKit/UIProcess/API/w10m/WebKitCertificateFailureToken.cpp",
    "Source/WebKit/UIProcess/API/w10m/platform/PageCallbacksW10M.cpp",
    "Source/WebKit/UIProcess/API/w10m/platform/InitializationActionW10M.cpp",
    "Source/WebKit/UIProcess/API/w10m/native/PageControllerW10M.cpp",
    "Source/WebKit/UIProcess/API/w10m/native/EngineModuleW10M.cpp",
]


class PublicControlContractTests(unittest.TestCase):
    """Ensure prototype/log/fixture substitutions cannot satisfy the source manifest gate."""

    def fixture(self, mutation="", prototype=False):
        """Build a minimal structured temporary source tree and run the real boundary gate."""
        with tempfile.TemporaryDirectory(dir=Path(tempfile.gettempdir()) / "opencode") as directory:
            repository = Path(directory)
            for name in SOURCES:
                path = repository / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("// Authored empty fixture body.\n", encoding="utf-8")
            (repository / SOURCES[0]).write_text(
                "HandleStarting(const WKW10MPageEvent& event) {}\nHandleNotification(const WKW10MPageEvent& event) {}\n"
                "event.errorCode; event.hasHttpStatus; event.certificateToken;\n"
                "ref new WebKitNavigationCompletedEventArgs; ref new WebKitNavigationStartingEventArgs;\n" + mutation,
                encoding="utf-8",
            )
            (repository / SOURCES[3]).write_text("target->HandleStarting(*event, cancel); target->HandleNotification(*event);", encoding="utf-8")
            sources = SOURCES + (["Tools/w10m/CXComponent/WebKitWebViewComponent.cpp"] if prototype else [])
            GATE.verify_sources(repository, sources)

    def test_structured_fixture_passes(self):
        """The normal frozen-field/actual-input boundary is accepted."""
        self.fixture()

    def test_legacy_log_type_fails(self):
        """A legacy log callback cannot become the public event source."""
        with self.assertRaisesRegex(AssertionError, "Legacy log"):
            self.fixture("WKPageLogUWP logger;\n")

    def test_prototype_compiler_input_fails(self):
        """An old WRL source cannot replace the compiler-authored control authority."""
        with self.assertRaisesRegex(AssertionError, "Historical prototype"):
            self.fixture(prototype=True)

    def test_fixture_content_fails(self):
        """Framework code cannot acquire production fixture URLs or suite triggers."""
        with self.assertRaisesRegex(AssertionError, "Diagnostic content"):
            self.fixture('const char* url = "https://example.test/?uwp-suite=1";\n')


if __name__ == "__main__":
    unittest.main()
