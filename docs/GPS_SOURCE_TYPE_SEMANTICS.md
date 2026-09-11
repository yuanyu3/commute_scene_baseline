# GPS Source-Type Semantics

`source_type` is platform metadata retained in sensor events and replay evidence.
It is **not an online baseline feature**: values `1`, `2`, and unknown are scored
identically when coordinates, accuracy, and trajectory are identical.

The online engine first unifies locations and anchors to WGS84, then derives
anchor relation and outbound evidence from distance. GPS reliability is computed
from horizontal accuracy and radial jump speed. Low-reliability fixes contribute
less `geo_outbound` evidence and cannot independently trigger approach/return or
GPS-speed ETA logic.

Offline evaluation may still use `source_type` as post-hoc annotation metadata,
for example to compare platform labels with coordinate-fence outcomes. Such labels
must not be fed back into the baseline observation or push gate.
