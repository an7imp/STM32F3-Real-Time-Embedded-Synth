# Real-time performance measurement

## Test configuration

- Sample rate: 48 kHz
- Audio frames per DMA half-buffer: 256
- Buffer deadline: 5.333 ms
- CPU clock inferred from `SystemCoreClock`: 48 MHz
- Build configuration: Debug
- Measurement source: Cortex-M4 DWT cycle counter (`DWT->CYCCNT`)

## Observed results

| Metric | Value |
|---|---:|
| Last measured execution | 106,632 cycles |
| Maximum observed execution | 110,994 cycles |
| Available cycles per deadline | 256,000 cycles |
| Deadline misses | 0 |

## Derived values

Maximum observed execution time:

```text
110,994 / 48,000,000 = 2.312 ms
```

Available time per DMA half-buffer:

```text
256 / 48,000 = 5.333 ms
```

Maximum observed callback budget usage:

```text
110,994 / 256,000 * 100 = 43.36%
```

Remaining observed margin:

```text
100% - 43.36% = 56.64%
```

## Conclusion

During the performed stress test, `FillI2SBuffer()` completed in at most
approximately 2.31 ms against a 5.33 ms deadline. No deadline misses were
observed, leaving approximately 3.02 ms (56.64%) of measured timing margin.

These figures are the maximum observed values from the test session. They are
useful real-time profiling evidence, but they are not a formal WCET proof.
