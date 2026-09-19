from __future__ import annotations

CASES = [
    ("W2-T03-A", "W2", "native+hardware", "ReadyQueue empty + STOP"),
    ("W2-T03-C", "W2", "native+hardware", "dequeued READY + STOP => CANCEL"),
    ("W2-T03-D", "W2", "native+hardware", "PROCESSING current block + STOP"),
    ("W2-T05-A", "W2", "native+hardware", "Interference pending + STOP"),
    ("W2-T05-B", "W2", "native+hardware", "Interference running + STOP"),
    ("W3-T06-A", "W3", "native+hardware", "ADC/DBM arm failure rollback"),
    ("W3-T06-B", "W3", "native+hardware", "STOP before final START commit"),
    ("W4-T04-A", "W4", "hardware", "half-filled DMA block + STOP"),
    ("W4-T04-B", "W4", "hardware", "pending TC at STOP edge"),
    ("W4-STOP-A", "W4", "native+hardware", "READY backlog cancellation"),
    ("W4-STOP-B", "W4", "native+hardware", "current PROCESSING completion"),
    ("W4-STOP-C", "W4", "hardware", "K1 DROP region + STOP"),
    ("W5-T13-A", "W5", "native+hardware", "duplicate START same binding"),
    ("W5-T13-B", "W5", "native", "request conflict"),
    ("W5-T13-C", "W5", "native+hardware", "duplicate STOP"),
    ("W5-T13-G", "W5", "native+hardware", "last ACK immediate restart"),
    ("W5-T20-A", "W5", "native+hardware", "result TX referenced => RESULT_BUSY"),
    ("W6-A", "W6", "hardware", "K8 NORMAL N256 repeated lifecycle"),
    ("W6-B", "W6", "hardware", "K1 DROP N256 repeated lifecycle"),
    ("W6-C", "W6", "hardware", "K4 NORMAL N512 lifecycle smoke"),
]
