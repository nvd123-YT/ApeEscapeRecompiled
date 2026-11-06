# Native frame wait (PsyQ v_wait)

Ape Escape's regenerated `func_8002E698` is the PsyQ `v_wait` routine. It
polls the frame counter at `0x800A9BB0`; its audited interrupt-check sites are
`0x8002E6C0` and `0x8002E708`. The routine also decrements its stack timeout
and reaches the original timeout callbacks at `0x800A37B8`, `0x8002F1F0`
and `0x8002F270`.

The enhanced build declares those exact sites through
`NATIVE_IDLE_WAIT_SITES`. The framework verifies the loop shape and limits
induction to whole polling iterations, preserving the timeout and interrupt
edges. This removes host polling overhead without advancing the guest clock.
The framework retains its netplay, precise-mode and self-check exclusions.

Use `-DAPE_NATIVE_IDLE_WAIT=OFF` to build the original polling path for a
baseline comparison. Use a separate build directory when changing this option
or `PSX_EXECUTION_PROFILE`; the framework includes execution-policy choices in
the runtime identity.

## Regression coverage

CTest's `ape_vwait_contract_test` extracts the actual generated
`func_8002E698` from the supported SCUS-94423 output and runs the framework's
shared `native_wait_caller.py` harness. The harness compiles temporary input
and compares original polling with native induction across event, timeout and
counter cases. Retail-derived function text is not committed to this
repository. A bare checkout without generated sources reports a CTest skip;
release CI generates the caller before running the test.

This is a bounded caller-level equivalence test, not a claim of whole-game
performance or complete gameplay qualification.
