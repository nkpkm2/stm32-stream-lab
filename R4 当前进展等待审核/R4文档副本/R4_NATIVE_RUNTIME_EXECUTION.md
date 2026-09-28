# R4 native RuntimeEvent execution record

Status: PASS for the native model/ledger scope only.  This record is not an
MCU timing, FreeRTOS-port, NVIC, or hardware provenance claim.

## Reproducible invocation

Executed from committed source `0ec8528` on 2026-09-28 using MSVC 19.50
(Visual Studio 18 Community), CMake 3.31.12, and Ninja after `vcvars64.bat`:

```text
cmake -S tests/native -B build/r4-native-formal-msvc -G Ninja \
  -DR4_RUNTIME_NATIVE_TESTS=ON -DR4_TICK_SERVICE_NATIVE_TESTS=ON \
  -DR2_NATIVE_SANITIZERS=OFF
cmake --build build/r4-native-formal-msvc --parallel
ctest --test-dir build/r4-native-formal-msvc --output-on-failure -R "^r4\\."
```

Result: **20 / 20 tests passed**, total CTest time 0.78 s.  The MSVC profile
uses `/W4 /WX`; `R2_NATIVE_SANITIZERS` remains off because this CMake profile
only enables ASan/UBSan for GCC/Clang.

## Covered claims

The executable tests each named case separately, rather than treating
aggregate conservation as attribution proof:

* Clock wrap, equal-cycle ordering, and multiple synthetic wraps;
* atomic RuntimeEvent window operations, caller-mask restoration, and
  fail-closed time regression;
* interval clipping, an execution interval entirely outside the window, and
  task/IRQ per-owner attribution inside a sealed window;
* idle interrupted by IRQ and idle/task switch attribution, proving IRQ time
  is not also charged to Idle;
* nested exclusive IRQ ownership and duplicate-exit rejection;
* locked checkpoint behavior; and
* TickService suspension, no-catch-up, and future-ticket handling.

The source-bound test registrations are in
`tests/native/CMakeLists.txt`; individual assertions are in
`tests/native/test_r4_runtime_event.c` and
`tests/native/test_r4_tick_service.c`.

## Deliberate limits

Native fake-clock tests cannot establish real DWT wrap servicing, Cortex-M
mask behavior, FreeRTOS trace-port wiring, physical DMA S0/S1 boundaries, or
actual IRQ nesting.  Those claims remain supported only by their corresponding
sealed target attempts and are retained separately in the Principal audit.
