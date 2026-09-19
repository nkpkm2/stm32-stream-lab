from __future__ import annotations

import subprocess
from pathlib import Path

def run_git(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    cp = subprocess.run(
        ["git", "-C", str(repo), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and cp.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed ({cp.returncode}): "
            + cp.stderr.decode("utf-8", "replace")
        )
    return cp

def parse_porcelain_v1_z(data: bytes) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    parts = data.split(b"\0")
    i = 0
    while i < len(parts):
        raw = parts[i]
        if not raw:
            i += 1
            continue
        if len(raw) < 4 or raw[2:3] != b" ":
            raise ValueError(f"malformed porcelain record: {raw!r}")
        xy = raw[:2].decode("ascii", "strict")
        path = raw[3:].decode("utf-8", "surrogateescape").replace("\\", "/")
        row = {"xy": xy, "path": path}
        if "R" in xy or "C" in xy:
            if i + 1 >= len(parts) or not parts[i + 1]:
                raise ValueError("malformed rename/copy porcelain record")
            row["other_path"] = parts[i + 1].decode(
                "utf-8", "surrogateescape"
            ).replace("\\", "/")
            i += 1
        rows.append(row)
        i += 1
    return rows

def status(repo: Path) -> list[dict[str, str]]:
    return parse_porcelain_v1_z(
        run_git(
            repo, "status", "--porcelain=v1", "-z",
            "--untracked-files=all"
        ).stdout
    )

def head(repo: Path) -> str:
    return run_git(repo, "rev-parse", "HEAD").stdout.decode().strip()

def r2_pass_peeled(repo: Path) -> str:
    return run_git(repo, "rev-parse", "r2-pass^{}").stdout.decode().strip()
