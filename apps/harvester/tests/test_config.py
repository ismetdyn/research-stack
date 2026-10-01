from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HARVESTER_DIR = Path(__file__).resolve().parents[1]


def run_python(code: str, root: Path | None) -> subprocess.CompletedProcess[str]:
    # Gerçek .env yüklenmesin diye ortam temiz ve ROOT geçici dizine işaret eder.
    env = {
        "PATH": os.environ.get("PATH", ""),
        "PYTHONPATH": str(HARVESTER_DIR),
    }
    if root is not None:
        env["RESEARCH_STACK_ROOT"] = str(root)
    return subprocess.run(
        [sys.executable, "-c", code],
        env=env,
        capture_output=True,
        text=True,
        cwd=HARVESTER_DIR,
    )


class ConfigRootTests(unittest.TestCase):
    def test_import_without_password_does_not_raise(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result = run_python("import research_harvester.config", Path(tmp))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_root_honors_env_override(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            result = run_python(
                "from research_harvester import config; print(config.ROOT)",
                root,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str(root))

    def test_connect_without_password_raises_runtime_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            code = (
                "from research_harvester import db\n"
                "try:\n"
                "    db.connect()\n"
                "except RuntimeError as exc:\n"
                "    print(exc)\n"
                "    raise SystemExit(0)\n"
                "raise SystemExit(1)\n"
            )
            result = run_python(code, Path(tmp))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("POSTGRES_PASSWORD", result.stdout)

    def test_default_root_is_repo_root_and_paths_keep_layout(self) -> None:
        # Yalnız yollar yazdırılır; varsayılan kökte gerçek .env yüklense de DB'ye bağlanılmaz.
        code = (
            "from research_harvester import config, fulltext, storage\n"
            "print(config.ROOT)\n"
            "print(fulltext.PAPER_DIR)\n"
        )
        result = run_python(code, None)
        self.assertEqual(result.returncode, 0, result.stderr)
        root_line, paper_line = result.stdout.splitlines()[:2]
        repo_root = HARVESTER_DIR.parents[1]
        self.assertEqual(Path(root_line), repo_root)
        self.assertEqual(Path(paper_line), repo_root / "data" / "papers")

    def test_relative_override_resolves_to_absolute(self) -> None:
        result = run_python(
            "from research_harvester import config; print(config.ROOT)",
            Path("~"),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(Path(result.stdout.strip()).is_absolute())


if __name__ == "__main__":
    unittest.main()
