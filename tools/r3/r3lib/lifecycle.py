from __future__ import annotations

from enum import Enum

class LifecycleState(str, Enum):
    IDLE = "IDLE"
    STARTING = "STARTING"
    RUNNING = "RUNNING"
    QUIESCING = "QUIESCING"
    RESET_REQUIRED = "RESET_REQUIRED"

def start_allowed(state: LifecycleState) -> bool:
    return state is LifecycleState.IDLE

def stop_allowed(state: LifecycleState) -> bool:
    return state in (LifecycleState.STARTING, LifecycleState.RUNNING, LifecycleState.QUIESCING)

def complete_current_lease_allowed(
    state: LifecycleState,
    processing_lease_valid: bool,
) -> bool:
    return (
        processing_lease_valid
        and state in (LifecycleState.RUNNING, LifecycleState.QUIESCING)
    )

def new_claim_allowed(state: LifecycleState, gate: bool) -> bool:
    return state is LifecycleState.RUNNING and bool(gate)
