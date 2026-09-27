#!/usr/bin/env python3
"""Verify the fixed R4 long-soak SRAM result without trusting prose decoding."""
import argparse
import json
import re
from pathlib import Path

MAGIC = 0x52344857
COMPLETE = 0x5234444E
OFF = {"magic": 0, "init": 4, "runtime": 20, "complete": 24,
       "completion_count": 96, "budget": 100, "malformed": 76,
       "tick_config": 256, "tick_snapshot": 260, "tick_count": 264,
       "tick_max_interval": 272, "tick_phase": 280, "tick_over_limit": 288,
       "soak_ms": 296, "soak_start_high": 300, "soak_end_high": 304,
       "dma_snapshot": 308, "soak_start": 312, "soak_end": 320,
       "dma_irq": 328, "dma_yield": 336, "dma_no_yield": 344,
       "no_event_status": 352, "no_event_irq_before": 360,
       "no_event_irq_after": 368, "no_event_no_yield_before": 376,
       "no_event_no_yield_after": 384}


def parse_words(text: str) -> list[int]:
    words: list[int] = []
    for line in text.splitlines():
        if re.match(r"^0x[0-9A-Fa-f]+\s*:", line):
            payload = line.split(":", 1)[1]
            words.extend(int(value, 16) for value in
                         re.findall(r"\b[0-9A-Fa-f]{8}\b", payload))
    if not words:
        raise ValueError("no STM32CubeProgrammer SRAM rows found")
    return words


def u64(words: list[int], offset: int) -> int:
    index = offset // 4
    return words[index] | (words[index + 1] << 32)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    w = parse_words(args.input.read_text(encoding="utf-8"))
    scalar = {"magic", "init", "runtime", "complete", "completion_count",
              "budget", "malformed", "tick_config", "tick_snapshot",
              "tick_over_limit", "soak_ms", "soak_start_high",
              "soak_end_high", "dma_snapshot", "no_event_status"}
    value = lambda name: u64(w, OFF[name]) if name not in scalar else w[OFF[name] // 4]
    checks = {
        "result_magic": value("magic") == MAGIC,
        "complete_magic": value("complete") == COMPLETE,
        "runtime_ok": value("init") == 0 and value("runtime") == 0,
        "completion_budget": value("completion_count") > 0 and value("budget") == 1 and value("malformed") == 0,
        "tick_timing": value("tick_config") == 0 and value("tick_snapshot") == 0 and value("tick_count") >= 65000 and value("tick_over_limit") == 0,
        "dwt_wrap": value("soak_ms") == 65000 and value("soak_end_high") > value("soak_start_high") and value("soak_end") > value("soak_start"),
        "dma_yield": value("dma_snapshot") == 0 and value("dma_irq") > 0 and value("dma_yield") > 0,
        "dma_no_event": value("no_event_status") == 0 and value("no_event_irq_after") == value("no_event_irq_before") + 1 and value("no_event_no_yield_after") == value("no_event_no_yield_before") + 1,
    }
    record = {"schema": "r4-soak-verdict-v1", "checks": checks,
              "values": {name: value(name) for name in OFF},
              "result": "PASS" if all(checks.values()) else "FAIL"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(record["result"])
    if record["result"] != "PASS":
        raise SystemExit(2)


if __name__ == "__main__":
    main()
