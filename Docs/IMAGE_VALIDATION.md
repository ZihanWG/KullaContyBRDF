# Image-space validation

The existing comparison screenshots are useful illustrations, but they are
tone-mapped contact sheets rather than measurement inputs. A defensible image
comparison uses two separate, pixel-aligned captures with identical camera,
lighting, exposure, resolution and render settings.

## Capture contract

For every pair:

- use the same map, camera transform and frame
- disable automatic and local exposure
- keep light intensity, color, shadowing and environment unchanged
- change only the BRDF/shading-model variant
- allow shader, texture and geometry streaming to finish before capture
- prefer linear OpenEXR with HALF or FLOAT RGB channels and NONE, ZIPS or ZIP
  scanline compression
- if PNG is unavoidable, record whether it is sRGB or linear and do not compare
  it numerically with an EXR
- save variants as separate files, not halves or rows of one presentation image

The comparison tool intentionally rejects different dimensions. That catches a
common invalid test: a camera, crop or screen-percentage change between renders.

## Build and run

The normal CMake build produces `KullaContyImageCompare` beside the LUT
generator:

```text
cmake -S LUT -B LUT/build
cmake --build LUT/build --config Release
```

Compare two full-frame linear EXRs:

```powershell
.\LUT\build\Release\KullaContyImageCompare.exe `
  --baseline Captures\Baseline_Roughness050.exr `
  --candidate Captures\KullaConty_Roughness050.exr `
  --output ImageComparison\Roughness050 `
  --gain 8
```

For a small object, restrict metrics and exported difference images to one
pixel-aligned region. This prevents an unchanged background from hiding the
material effect:

```powershell
  --roi "640,220,640,640"
```

For an 8-bit screenshot pair, add `--input-space srgb`. For a linear PNG, use
`--input-space linear`.

## Outputs

| File | Meaning |
| --- | --- |
| `metrics.json` | Linear RGB/luminance MAE and RMSE, maximum error, mean luminance change, LDR PSNR and affected-pixel coverage |
| `absolute_difference.hdr` | Unscaled linear absolute RGB difference for further analysis |
| `absolute_difference.png` | Displayable absolute difference multiplied by `--gain` |
| `signed_luminance_difference.png` | Red means the candidate is brighter; blue means it is darker |

`--pixel-threshold` controls the affected-pixel coverage statistic.
`--fail-mae` gives the tool a non-zero exit code above a chosen RGB MAE and is
used for regression tests.

## What the numbers prove

A Baseline-versus-Kulla–Conty comparison measures the size and location of the
appearance change. It does **not** prove that Kulla–Conty is more accurate.

For an accuracy claim, render or obtain an independent multiple-scattering
reference and run two comparisons using the same ROI:

1. Baseline versus Reference
2. Kulla–Conty versus Reference

Only then compare their RGB/luminance errors. Publish all three source images,
both amplified difference images, the ROI, `metrics.json` files and the exact
render settings. This separates a visible-effect claim from a lower-error claim.

## Current repository images

`roughness_row.png` and `cornell_box.png` remain presentation images. Their
variants are already combined, and the roughness rows are not pixel aligned.
They must not be passed to the numerical comparison tool or cited as quantitative
accuracy evidence. New captures should follow the contract above.
