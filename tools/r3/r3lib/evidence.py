from __future__ import annotations

from pathlib import Path

PHASES = ("H0", "H1", "H2", "H3", "H4", "H5")

def manifest_path(value: str) -> str:
    if not value:
        raise ValueError("empty manifest path")
    s = value.replace("\\", "/")
    if s.startswith("/") or (len(s) >= 2 and s[1] == ":"):
        raise ValueError("absolute manifest path forbidden")
    parts = s.split("/")
    if any(p in ("", ".", "..") for p in parts):
        raise ValueError("ambiguous/traversal manifest path forbidden")
    return "/".join(parts)

def next_phase(completed: list[str]) -> str:
    if completed != list(PHASES[:len(completed)]):
        raise ValueError("completed phases are not a legal prefix")
    return "COMPLETE" if len(completed) == len(PHASES) else PHASES[len(completed)]

def require_phase(requested: str, completed: list[str]) -> str:
    if requested == "H3" and "H3" in completed:
        return "H3 ALREADY COMPLETE — DO NOT RERUN"
    nxt = next_phase(completed)
    if requested != nxt:
        raise ValueError(f"illegal phase request: requested={requested}, next={nxt}")
    return f"{requested} ALLOWED"

def validate_host_timeout(target_bound_s: float, host_timeout_s: float) -> None:
    if target_bound_s <= 0:
        raise ValueError("target bound must be positive")
    if host_timeout_s < 2.0 * target_bound_s:
        raise ValueError("host timeout below 2x target bound")
