"""Regressions for stale dependency DLLs re-entering a phone candidate."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "candidate_stager", Path(__file__).with_name("stage-minibrowser-candidate.py"))
STAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGER)
RUNTIME_SPEC = importlib.util.spec_from_file_location(
    "runtime_stager", Path(__file__).parents[1] / "RuntimeSmoke/stage-app.py")
RUNTIME = importlib.util.module_from_spec(RUNTIME_SPEC)
RUNTIME_SPEC.loader.exec_module(RUNTIME)


class DependencyBaselineTests(unittest.TestCase):
    """Use real files/hashes to reject altered or incomplete dependency baselines."""

    def setUp(self):
        """Create an independent recorded font/text dependency closure."""
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        names = ("freetype.dll", "fontconfig-1.dll", "icuuc78.dll", "icuin78.dll",
                 "icudt78.dll", "harfbuzz.dll", "harfbuzz-icu.dll")
        for name in names:
            (self.directory / name).write_bytes(("repaired " + name).encode())
        self.hashes = {name: STAGER.sha256(self.directory / name) for name in names}
        self.identity = self.directory / "identity.json"
        self.identity.write_text(json.dumps({"files": self.hashes}))

    def test_accepts_recorded_baseline(self):
        """A matching recorded closure is returned for package provenance."""
        identity = STAGER.validate_dependency_baseline(self.directory, self.identity)
        self.assertEqual(identity["files"], self.hashes)

    def test_rejects_stale_replacement(self):
        """Replacing FreeType with stale prefix bytes must fail before packaging."""
        (self.directory / "freetype.dll").write_bytes(b"stale division helper")
        with self.assertRaisesRegex(ValueError, "freetype.dll"):
            STAGER.validate_dependency_baseline(self.directory, self.identity)

    def test_rejects_missing_icu(self):
        """A missing ICU DLL cannot silently fall back to the stale prefix."""
        (self.directory / "icuuc78.dll").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            STAGER.validate_dependency_baseline(self.directory, self.identity)

    def test_rejects_unrecorded_dll(self):
        """Additional dependencies must carry their own recorded hash."""
        (self.directory / "libpng16.dll").write_bytes(b"unrecorded dependency")
        with self.assertRaisesRegex(ValueError, "libpng16.dll"):
            STAGER.validate_dependency_baseline(self.directory, self.identity)


class EngineSelectionTests(unittest.TestCase):
    """Regression for a dependency baseline silently shadowing a configured engine."""

    def test_overrides_cannot_mix_engine_outputs(self):
        """All four native engine outputs retain provenance despite conflicting baseline files."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build, engine, baseline, prefix = (root / name for name in ("host", "engine", "baseline", "prefix"))
            for folder in (build, engine, baseline, prefix / "bin"):
                folder.mkdir(parents=True)
            for name in ("WebCore.dll", "JavaScriptCore.dll", "libEGL.dll", "libGLESv2.dll"):
                (engine / name).write_bytes(b"configured output")
                (baseline / name).write_bytes(b"old baseline")
            (baseline / "freetype.dll").write_bytes(b"repaired dependency")
            (engine / "freetype.dll").write_bytes(b"stale dependency")
            chosen = RUNTIME.binary_candidates(build, engine, [baseline], prefix)
            for name in ("WebCore.dll", "JavaScriptCore.dll", "libEGL.dll", "libGLESv2.dll"):
                self.assertEqual(chosen[name.lower()], engine / name)
            self.assertEqual(chosen["freetype.dll"], baseline / "freetype.dll")


if __name__ == "__main__":
    unittest.main()
