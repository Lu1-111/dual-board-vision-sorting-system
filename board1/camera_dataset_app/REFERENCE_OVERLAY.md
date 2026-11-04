# Fixed parking reference overlay

When `--track-centers` is enabled, the preview encoder optionally reads
`<tracking-output-dir>/reference.json`, normally `/tmp/triangle-track/reference.json`.
The file is read only, capped at 4096 bytes, and never generated or updated from
tracked objects. Install/replace the file atomically to change the reference.
Removing it disables the additional reference drawing without changing tracking.

Required fields:

```json
{
  "schema": 1,
  "width": 1920,
  "height": 1080,
  "center_xy": [1235, 543],
  "region_xyxy": [1135, 531, 1335, 555],
  "calibrated": false,
  "motion_enabled": false
}
```

Unrelated audit metadata is allowed and ignored by the renderer. Unknown schema,
missing or wrong required fields, invalid/out-of-frame geometry, a center outside
the region, or either boolean set to true causes the file to be ignored. Missing,
malformed, oversized or symlinked files also produce no reference drawing.

The preview gains a yellow dashed region, a fixed yellow cross, and the literal
label `REFERENCE / UNCALIBRATED`. These coordinates are in the original image and
are scaled only for preview display. The existing tracker data is unchanged.
The same frame's localization JSON includes a `reference_overlay` copy of only
the seven validated fields so the rendered reference can be audited. This field
has no effect on motion, inference, classification, or object identity.

The reference is a proposed visualization, not a calibrated stopping zone.
No GPIO/CAN/arm/controller commands are issued, and no enable flag is set.
The original full-resolution image/JPEG remains untouched for both models.
The camera does not infer pixel scale from the 400 mm belt or convert the
reported 75–80 mm gripper range / 40 mm block size into motion permissions.

Native regression adds strict parser negatives, source immutability and fixed
coordinates while a synthetic blue object's center moves from (1020,540) to
(1260,540). The reference remains at (1235,543), the file bytes remain unchanged,
and tracking-enabled full JPEG bytes still match the unannotated baseline.
Host tests are not board timing or stopping-distance validation.
