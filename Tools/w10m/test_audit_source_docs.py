"""Regression checks for authored/CX documentation coverage and lexical exclusions."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


# Load the existing hyphenated CLI module without introducing an alternate audit.
SPEC = importlib.util.spec_from_file_location("audit_source_docs", Path(__file__).with_name("audit-source-docs.py"))
# The same implementation is exercised by unit checks and the targeted CLI gate.
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class AuditSourceDocsTests(unittest.TestCase):
    """Ensure broader CX coverage does not flag constructor-initializer expressions."""

    def candidates(self, source):
        """Audit a temporary authored snippet and return its candidate messages."""
        with tempfile.TemporaryDirectory(dir=Path(tempfile.gettempdir()) / "opencode") as directory:
            path = Path(directory) / "snippet.cpp"
            path.write_text(source, encoding="utf-8")
            return [message for _, message in AUDIT.inspect(path)]

    def test_cx_declarations_and_constants_require_comments(self):
        """Catch public ref classes, properties, events, constants and qualified accessors."""
        messages = self.candidates("public ref class Sample sealed {\nproperty bool Ready;\nevent Handler^ Changed;\n};\nconstexpr int limit = 1;\nbool Sample::Ready::get() { return true; }\n")
        self.assertEqual(len(messages), 5)

    def test_one_comment_documents_a_contiguous_constant_group(self):
        """Allow a rationale comment to cover a same-scope constants block."""
        messages = self.candidates("// Stable ABI test canaries.\nconstexpr int first = 1;\nconstexpr int second = 2;\n\nconstexpr int undocumented = 3;\n")
        self.assertEqual(messages, ["declaration needs documentation: constexpr int undocumented = 3;"])

    def test_block_comment_documents_the_following_declaration(self):
        """Recognize the block-comment style used by frozen C ABI headers."""
        messages = self.candidates("// Sample layout.\nstruct Sample {\n    /* Borrowed pointer; size is supplied by length. */\n    const int* data;\n};\n")
        self.assertEqual(messages, [])

    def test_constructor_definition_requires_comment(self):
        """Catch constructors/destructors and qualified definitions without misreading initializers."""
        messages = self.candidates("// Owned type.\nclass Sample {\nSample();\n~Sample();\n};\nSample::Sample()\n: m_value(value)\n{ }\n")
        self.assertEqual(len(messages), 3)

    def test_local_initializers_are_not_methods(self):
        """Exclude local constructions/constants while retaining declaration checks."""
        messages = self.candidates("// Construct local values.\nvoid run()\n{\nstd::wstring package(argv[1]);\nstd::string output(count, 'x');\nDriver fixture(101);\nHStringReference name(L\"value\");\nconst bool localFlag = true;\nconstexpr int localLimit = 3;\nif (localFlag) { const bool nestedFlag = false; }\nelse if (ready) { }\n}\n")
        self.assertEqual(messages, [])

    def test_authored_webview_header_is_not_generated(self):
        """Ensure the API-root WebKitWebView.h participates in the audit."""
        authored = AUDIT.ROOT / "Source/WebKit/UIProcess/API/w10m/WebKitWebView.h"
        self.assertNotIn(authored.parent, AUDIT.GENERATED_DIRECTORIES)
        self.assertIn(AUDIT.ROOT / "Tools/w10m/CXComponent", AUDIT.GENERATED_DIRECTORIES)
        self.assertFalse(AUDIT.is_generated(authored))

    def test_default_scope_covers_implemented_m10_placement(self):
        """Audit Shared/w10m ABI headers alongside engine, control and tooling."""
        self.assertIn(AUDIT.ROOT / "Source/WebKit/Shared/w10m", AUDIT.SOURCE_DIRECTORIES)
        self.assertIn(AUDIT.ROOT / "Source/WebCore/platform/w10m", AUDIT.SOURCE_DIRECTORIES)
        self.assertIn(AUDIT.ROOT / "Source/WebKit/UIProcess/API/w10m", AUDIT.SOURCE_DIRECTORIES)
        self.assertIn(AUDIT.ROOT / "Tools/w10m", AUDIT.SOURCE_DIRECTORIES)

    def test_midl_outputs_excluded_only_in_generated_homes(self):
        """Exclude legacy MIDL products without hiding the authored control header."""
        component = AUDIT.ROOT / "Tools/w10m/CXComponent/WebKitWebView.h"
        stray_root = AUDIT.ROOT / "WebKitWebView_i.c"
        self.assertTrue(AUDIT.is_generated(component))
        self.assertTrue(AUDIT.is_generated(stray_root))
        self.assertFalse(AUDIT.is_generated(
            AUDIT.ROOT / "Source/WebKit/UIProcess/API/w10m/WebKitWebView.h"))

    def test_legacy_generated_dirs_and_g_files_excluded(self):
        """Exclude generator-owned directories and XAML/MIDL *.g.* sources."""
        with tempfile.TemporaryDirectory(dir=Path(tempfile.gettempdir()) / "opencode") as directory:
            root = Path(directory)
            legacy = root / "legacy-generated" / "Probe.h"
            legacy.parent.mkdir()
            legacy.write_text("constexpr int kProbeValue = 1;\n", encoding="utf-8")
            generated = root / "MainPage.g.h"
            generated.write_text("constexpr int kGeneratedValue = 1;\n", encoding="utf-8")
            authored = root / "ProbeW10M.h"
            authored.write_text("constexpr int kAuthoredValue = 1;\n", encoding="utf-8")
            self.assertTrue(AUDIT.is_generated(legacy))
            self.assertTrue(AUDIT.is_generated(generated))
            self.assertFalse(AUDIT.is_generated(authored))
            self.assertEqual([message for _, message in AUDIT.inspect(authored)], [
                "declaration needs documentation: constexpr int kAuthoredValue = 1;"])


if __name__ == "__main__":
    unittest.main()
