# Version 01.00 hardware crash

The first device launch loaded WAR01A and displayed its geometry, then raised
C2-12828-1. Two complete dumps from separate launches show the same fault.

The exact deployed ELF and its receipt were preserved before rebuilding. After
accounting for each process's relocated module base:

| Evidence | Both dumps |
| --- | --- |
| Thread | `NXVM00001` |
| Stop reason | `0x30004`, data abort |
| PC relative to executable text | `0x1d83a4` |
| Resolved function | `strlen` |
| Caller | `_svfprintf_r` |
| Faulting instruction | `ldrd r2, r3, [r1]` |
| `r1`, attempted string address | `0x00220e00` = 2,231,808 |

That address is exactly the texture payload count at the entry view. The surviving
log also prints `tiles=zu walls=zu`, showing that the Vita formatter does not
consume the size arguments for `%zu`.

The once-per-second telemetry message put three `%zu` fields before a final `%s`.
Because those conversions left their arguments unconsumed, the final `%s` used
the first byte count as its string pointer. This explains the short period of
visible rendering before the crash. The fault is in diagnostic formatting, not
evidence of exhaustion of the sprite cache or a GXM fault.

Version 01.01 uses supported `%u` conversions with explicit bounded-count casts
in all viewer log messages. The status format is shared with an on-target startup
check that validates numeric fields and a trailing string through SDL's actual
formatter. The target ELF now keeps line information for future symbolication.
Desktop formatting and rendering checks complement this guard; sustained hardware
operation still requires the user's retest.

Full dumps remain in local build evidence and are not published with source.
