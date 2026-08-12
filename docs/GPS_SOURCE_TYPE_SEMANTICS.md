# GPS Source-Type Semantics

For company departure detection, platform GPS `source_type` is the authoritative
company-gate semantic:

| Value | Meaning | Departure behavior |
|---|---|---|
| `2` | Inside company | May enter a departure prediction episode. |
| `1` | Outside company gate | Confirms departure only when it follows `2`; never send a departure reminder. |

The only departure truth is the first `2 -> 1` transition. A session beginning
at `type=1` is already outside. When it later becomes `type=2`, it is a
return-to-company trace, not a departure process, and departure push is blocked.

The C++ runtime passes `source_type` into `TickFeatures`; it overrides a noisy
coordinate fence for the company relation. The offline replay uses the same
rule, so return traces cannot be scored as departure candidates.
