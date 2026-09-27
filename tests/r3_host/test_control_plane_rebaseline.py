from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS_R3 = ROOT / "tools" / "r3"
if str(TOOLS_R3) not in sys.path:
    sys.path.insert(0, str(TOOLS_R3))

from r3lib.cases import CASES
from r3lib.control_state import (
    derive_operator_gate,
    derive_progress,
)
from r3lib.seal import (
    SealRelation,
    classify_heads,
    classify_worktree,
    inspect_seal,
)


class ControlPlanePureTests(unittest.TestCase):
    def test_catalog_matches_w1_freeze(self):
        self.assertEqual(len(CASES), 33)
        ids = {row[0] for row in CASES}
        for required in (
            "W2-T03-B", "W2-ACK-A", "W2-ACK-B",
            "W3-START-A", "W4-STOP-E", "W5-T20-B", "W6-C",
        ):
            self.assertIn(required, ids)

    def test_progress_at_current_w2b_baseline(self):
        p = derive_progress(
            w1_sealed=True,
            w2a_sealed=True,
            w2b_sealed=True,
            w2_evidence_present=False,
        )
        self.assertEqual(p.current_work_package, "R3-W2")
        self.assertEqual(
            p.next_allowed,
            "W2_DIRECTED_HARDWARE_HARNESS_PREFLIGHT",
        )
        self.assertFalse(p.target_firmware_modification_allowed)

    def test_partial_w2_evidence_remains_in_w2(self):
        p = derive_progress(
            w1_sealed=True,
            w2a_sealed=True,
            w2b_sealed=True,
            w2_evidence_present=True,
            w2_hw_freeze_sealed=True,
        )
        self.assertEqual(p.w2_evidence, "IN_PROGRESS")
        self.assertEqual(p.next_allowed, "W2_HARDWARE_EVIDENCE_REMAINING")
        self.assertTrue(p.target_firmware_modification_allowed)

    def test_w4_evidence_advances_to_w5(self):
        p = derive_progress(
            w1_sealed=True,
            w2a_sealed=True,
            w2b_sealed=True,
            w2_evidence_present=True,
            w2_evidence_complete=True,
            w2_hw_freeze_sealed=True,
            w3_evidence_complete=True,
            w4_evidence_complete=True,
        )
        self.assertEqual(p.w4_evidence, "SEALED")
        self.assertEqual(p.current_work_package, "R3-W5")
        self.assertEqual(p.next_allowed, "W5_ISOLATION_RESTART_IMPLEMENTATION")

    def test_operator_gate_blocks_dirty_hardware_progression(self):
        p = derive_progress(
            w1_sealed=True,
            w2a_sealed=True,
            w2b_sealed=True,
            w2_evidence_present=False,
        )
        clean = derive_operator_gate(p, worktree_dirty=False)
        dirty = derive_operator_gate(p, worktree_dirty=True)
        self.assertEqual(clean.state, "OPEN")
        self.assertEqual(
            clean.next_allowed,
            "W2_DIRECTED_HARDWARE_HARNESS_PREFLIGHT",
        )
        self.assertEqual(dirty.state, "WORKTREE_DIRTY")
        self.assertEqual(
            dirty.next_allowed,
            "REVIEW_AND_SEAL_DIRTY_WORKTREE",
        )
        self.assertFalse(dirty.target_firmware_modification_allowed)

    def test_seal_relations(self):
        self.assertEqual(
            classify_heads("b", "a", "b"),
            SealRelation.CLEAN_SYNCED,
        )
        self.assertEqual(
            classify_heads("b", "a", "a"),
            SealRelation.LOCAL_COMMIT_REMOTE_PARENT,
        )
        self.assertEqual(
            classify_heads("b", "a", None),
            SealRelation.REMOTE_UNKNOWN,
        )
        self.assertEqual(
            classify_heads("b", "a", "x"),
            SealRelation.REMOTE_DIVERGED,
        )

    def test_worktree_relations(self):
        self.assertIsNone(classify_worktree([]))
        self.assertEqual(
            classify_worktree([{"xy": " M", "path": "a"}]),
            SealRelation.CANDIDATE_DIRTY,
        )
        self.assertEqual(
            classify_worktree([{"xy": "??", "path": "a"}]),
            SealRelation.CANDIDATE_DIRTY,
        )
        self.assertEqual(
            classify_worktree([{"xy": "M ", "path": "a"}]),
            SealRelation.CANDIDATE_STAGED,
        )
        self.assertEqual(
            classify_worktree([{"xy": "MM", "path": "a"}]),
            SealRelation.CANDIDATE_MIXED,
        )


class ControlPlaneGitFixtureTests(unittest.TestCase):
    def _git(self, repo: Path, *args: str) -> str:
        cp = subprocess.run(
            ["git", "-C", str(repo), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if cp.returncode != 0:
            self.fail(
                f"git {' '.join(args)} failed ({cp.returncode}): {cp.stderr}"
            )
        return cp.stdout.strip()

    def _init_repo(self, root: Path) -> tuple[Path, Path]:
        repo = root / "repo"
        remote = root / "remote.git"
        subprocess.run(
            ["git", "init", "--bare", str(remote)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        subprocess.run(
            ["git", "init", "-b", "main", str(repo)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._git(repo, "config", "user.name", "R3 Fixture")
        self._git(repo, "config", "user.email", "fixture@example.invalid")
        self._git(repo, "remote", "add", "origin", str(remote))
        (repo / "a.txt").write_text("a\n", encoding="utf-8")
        self._git(repo, "add", "--", "a.txt")
        self._git(repo, "commit", "-m", "base")
        self._git(repo, "push", "-u", "origin", "main")
        return repo, remote

    def test_local_commit_remote_parent_then_resume_push(self):
        with tempfile.TemporaryDirectory() as td:
            repo, _ = self._init_repo(Path(td))
            (repo / "a.txt").write_text("b\n", encoding="utf-8")
            self._git(repo, "add", "--", "a.txt")
            self._git(repo, "commit", "-m", "candidate")

            before = inspect_seal(repo)
            self.assertEqual(
                before.relation,
                SealRelation.LOCAL_COMMIT_REMOTE_PARENT,
            )

            self._git(repo, "push", "origin", "HEAD:main")
            after = inspect_seal(repo)
            self.assertEqual(after.relation, SealRelation.CLEAN_SYNCED)

    def test_dirty_repo_does_not_need_remote(self):
        with tempfile.TemporaryDirectory() as td:
            repo, remote = self._init_repo(Path(td))
            self._git(repo, "remote", "remove", "origin")
            (repo / "a.txt").write_text("dirty\n", encoding="utf-8")
            state = inspect_seal(repo)
            self.assertEqual(state.relation, SealRelation.CANDIDATE_DIRTY)
            self.assertIsNone(state.remote_head)


    def _init_progress_repo(self, root: Path) -> Path:
        repo, _ = self._init_repo(root)
        required = (
            "docs/r3/w1/R3_W1_SPEC_FREEZE.md",
            "docs/r3/w1/R3_DIRECTED_TEST_MATRIX.md",
            "docs/r3/w1/R3_EVIDENCE_SCHEMA_V1.md",
            "docs/r3/w1/R3_HOST_HARNESS_ARCHITECTURE.md",
            "tools/r3/r3_harness.py",
            "firmware/runtime/r3_worker_contract.c",
            "firmware/runtime/r3_worker_contract.h",
            "tests/native/test_r3_worker_contract.c",
            "docs/r3/w2/R3_W2A_WORKER_CONTRACT_DESIGN.md",
            "firmware/runtime/r3_worker_tasks.c",
            "firmware/runtime/r3_worker_tasks.h",
            "tests/native/test_r3_worker_tasks.c",
            "docs/r3/w2/R3_W2B_TASK_GLUE_DESIGN.md",
        )
        for rel in required:
            path = repo / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(rel + "\n", encoding="utf-8")
        self._git(repo, "add", "--", *required)
        self._git(repo, "commit", "-m", "r3 progress baseline")
        self._git(repo, "tag", "-a", "r2-pass", "-m", "fixture r2-pass")
        (repo / "dirty.tmp").write_text("dirty\n", encoding="utf-8")
        return repo

    def _run_harness(self, repo: Path, command: str) -> str:
        harness = ROOT / "tools" / "r3" / "r3_harness.py"
        cp = subprocess.run(
            [sys.executable, str(harness), "--repo", str(repo), command],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if cp.returncode != 0:
            self.fail(
                f"harness {command} failed ({cp.returncode}): "
                f"{cp.stdout}\n{cp.stderr}"
            )
        return cp.stdout

    def test_selftest_cli_respects_dirty_operator_gate(self):
        with tempfile.TemporaryDirectory() as td:
            repo = self._init_progress_repo(Path(td))
            out = self._run_harness(repo, "selftest")
            self.assertIn("WORKTREE: DIRTY", out)
            self.assertIn(
                "NEXT_ALLOWED: REVIEW_AND_SEAL_DIRTY_WORKTREE",
                out,
            )
            self.assertNotIn(
                "NEXT_ALLOWED: W2_DIRECTED_HARDWARE_HARNESS_PREFLIGHT",
                out,
            )

    def test_list_cases_cli_respects_dirty_operator_gate(self):
        with tempfile.TemporaryDirectory() as td:
            repo = self._init_progress_repo(Path(td))
            out = self._run_harness(repo, "list-cases")
            self.assertIn("CASE_COUNT: 33", out)
            self.assertIn(
                "NEXT_ALLOWED: REVIEW_AND_SEAL_DIRTY_WORKTREE",
                out,
            )
            self.assertNotIn(
                "NEXT_ALLOWED: W2_DIRECTED_HARDWARE_HARNESS_PREFLIGHT",
                out,
            )


if __name__ == "__main__":
    unittest.main()
