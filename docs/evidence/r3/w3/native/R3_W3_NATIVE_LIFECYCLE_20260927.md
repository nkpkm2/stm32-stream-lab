# R3 W3 Native Lifecycle Execution Record

- Source commit: `31e49ce`
- Date: 2026-09-27
- Host compiler: MSVC 19.50.35729.0, x64
- CMake: STM32CubeCLT CMake; generator: Ninja
- Build directory: external temporary directory `stm32-r3-native`
- Configuration: `R3_LIFECYCLE_NATIVE_TESTS=ON`

The test executable links the actual `firmware/runtime/r3_lifecycle.c` with
the native FreeRTOS type stubs.  It does not emulate STM32 peripherals and is
not offered as a substitute for W3's required board evidence.

## Executed result

```text
10 / 10 PASS
r3.lifecycle.normal
r3.lifecycle.invalid
r3.lifecycle.prepare_rollback
r3.lifecycle.arm_rollback
r3.lifecycle.stop_before_commit
r3.lifecycle.commit_failure
r3.lifecycle.stale
r3.lifecycle.rollback_failure
r3.lifecycle.running_stop
r3.lifecycle.stale_running_stop
```

The invalid case is the native evidence for W3-START-B: validation rejects the
request before prepare/arm/commit mutation.  The stale case is the native
evidence for W3-START-D: a modified start ticket is rejected fail-closed.  The
remaining cases establish rollback and STOP state-machine behavior used by W3
and W4; their hardware-required counterparts are recorded separately under
`docs/evidence/r3/w3/`.
