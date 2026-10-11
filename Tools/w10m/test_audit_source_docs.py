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

    def test_constructor_definition_requires_comment(self):
        """Catch constructors/destructors and qualified definitions without misreading initializers."""
        messages = self.candidates("// Owned type.\nclass Sample {\nSample();\n~Sample();\n};\nSample::Sample()\n: m_value(value)\n{ }\n")
        self.assertEqual(len(messages), 3)

    def test_local_initializers_are_not_methods(self):
        """Exclude local direct construction and else-if expressions from method candidates."""
        messages = self.candidates("// Construct local values.\nvoid run()\n{\nstd::wstring package(argv[1]);\nstd::string output(count, 'x');\nDriver fixture(101);\nelse if (ready) { }\n}\n")
        self.assertEqual(messages, [])

    def test_authored_webview_header_is_not_generated(self):
        """Ensure the API-root WebKitWebView.h participates in the audit."""
        authored = AUDIT.ROOT / "Source/WebKit/UIProcess/API/w10m/WebKitWebView.h"
        self.assertNotIn(authored.parent, AUDIT.GENERATED_DIRECTORIES)
        self.assertIn(AUDIT.ROOT / "Tools/w10m/CXComponent", AUDIT.GENERATED_DIRECTORIES)


if __name__ == "__main__":
    unittest.main()
