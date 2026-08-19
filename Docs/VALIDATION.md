# Numerical validation

The LUT generator performs numerical checks every time it produces the reference
textures. A failed check writes `validation.json` and returns a non-zero process
exit code, making the validation suitable for CI.

## Reference configuration

```text
resolution: 256 x 256
samples per texel: 1024
sequence: Hammersley
microfacet sampling: Heitz isotropic GGX VNDF
geometry: exact separable Smith GGX
roughness convention: perceptual roughness, alpha = roughness^2
```

VNDF sampling is important at smooth grazing angles. With a visible-normal GGX
sample, the directional-albedo Monte Carlo weight simplifies to `G1(L)`, which
has substantially lower variance than sampling the full normal distribution.

## Tests

### White furnace

For Fresnel equal to one, an energy-conserving single-plus-multiple-scattering
BRDF must integrate to one:

```text
E_single(NoV) + E_multi(NoV) = 1
```

The test numerically integrates the incident side of the Kulla-Conty lobe for
every LUT view and roughness sample. The reported domain uses roughness and
`NdotV` greater than or equal to 0.02, matching this project's configured
minimum-roughness operating range. The exact smooth limit has `E_avg = 1` and
is handled by the shader's epsilon rather than used as a division test.

### Colored Fresnel energy

The test evaluates Schlick `F0` values 0.04, 0.5 and 0.9 using:

```text
F_avg = F0 + (1 - F0) / 21
F_ms  = F_avg * E_avg / (1 - F_avg * (1 - E_avg))
```

It combines the split-sum single-scatter directional albedo with the integrated
multiple-scatter contribution and verifies that the result does not exceed one
beyond the declared numerical tolerance.

### Engine quadrature

The engine patch retains a four-point Gauss-Legendre `E_avg` reference path from
UE's GGX energy LUT. The generator evaluates the same quadrature against
its high-resolution midpoint integral and reports maximum and RMS error.

### UE 5.8.1 embedded `E_avg` fast path

`Tools/ExportUEEnergyLUT.py` exports UE's official 32×32 HALF energy texture as
EXR. `Tools/AnalyzeUEEnergyLUT.py` decodes the EXR, integrates each roughness row
with 128-point Gauss-Legendre quadrature, and simulates UE's bilinear clamp
sampling. The resulting 32 float32 constants reproduce the texture's roughness
interpolation with these measured errors on roughness `[0.02, 1]`:

| Metric | Result |
| --- | ---: |
| Embedded `E_avg` maximum absolute error | 0.0000000291 |
| Embedded `E_avg` RMS error | 0.0000000149 |
| Embedded-path white-furnace maximum absolute error | 0.0000094947 |

The four-point reference uses four additional texture fetches per direct-light
evaluation; the embedded default uses none for `E_avg`, leaving only the `NoV`
and `NoL` directional-albedo fetches.

### Shader compiler validation

`EnginePatch/UE5.8.1/Test-EAvgShader.ps1` compiles the isolated embedded-table
lookup with both Windows SDK compilers. The automated disassembly assertions
verify:

| Target | Verified result |
| --- | --- |
| FXC, pixel shader model 5 | Immediate constant buffer, zero texture resources, approximately 12 instruction slots |
| DXC, pixel shader model 6 | Read-only 32-float global, two indexed loads, zero bound resources, no temporary table copy |

This closes the risk that the HLSL array syntax could silently become 32
per-invocation stores under SM6. It is still a compiler-level result, not an
end-to-end light-loop GPU timing.

### Source-build GPU A/B protocol

`EnginePatch/UE5.8.1/Benchmark-EAvgModes.ps1` automates the remaining runtime
measurement. It alternates Fast and Reference runs to counterbalance order and
thermal bias, uses fixed resolution and capture length, enables Unreal's GPU
CSV stats, prewarms both shader variants for 300 frames, trims startup and tail
frames, and reports median, mean and p95 GPU frame time. It pairs matching trial
numbers, reports per-trial median deltas, cross-trial variation, and deterministic
95% bootstrap confidence intervals for mean delta and speedup. Fewer than three
paired trials is labeled `insufficient_trials`; a confidence interval crossing
zero is labeled `inconclusive`. The installed helper is restored byte-for-byte
in a `finally` block even when UnrealEditor fails.

The default test map is `/Game/RowCompareTest`; for a publishable performance
claim, populate or replace it with a direct-light stress scene where the
Kulla-Conty material covers most pixels. The default is five trials per mode;
never report fewer than three. Include the raw captures with hardware, driver,
resolution and engine commit.
The generated `paired_trials.csv` is directly suitable for a portfolio chart;
`summary.json` retains the full statistical result and conclusion.

### Image-space comparison

`KullaContyImageCompare` loads separate pixel-aligned PNG, Radiance HDR or
UE-style scanline OpenEXR captures, converts LDR inputs to linear light when
requested, and reports RGB/luminance MAE and RMSE, maximum error, LDR PSNR, mean
luminance change and affected-pixel coverage. Optional ROI evaluation prevents
an unchanged background from diluting a material comparison. The tool exports
unscaled HDR absolute difference plus amplified absolute and signed-luminance
PNG views.

The Windows build has been checked against UE 5.8's ZIP-compressed HALF EXR
output; the CI PNG self-comparison checks decoding, metrics, ROI and
image-writing paths on MSVC and GCC. See
[`IMAGE_VALIDATION.md`](IMAGE_VALIDATION.md) for the capture contract and claim
boundaries.

## Reference results

| Metric | Result | Tolerance |
| --- | ---: | ---: |
| White-furnace maximum absolute error | 0.0002363713 | 0.0005 |
| White-furnace RMS error | 0.0000039344 | — |
| Four-point `E_avg` maximum absolute error | 0.0006462380 | 0.001 |
| Four-point `E_avg` RMS error | 0.0001741692 | — |
| Maximum colored directional albedo | 0.9510494811 | 1.001 |

Result: **passed**.

These tests validate the CPU reference and the mathematical compensation term.
They do not establish image-space accuracy against a multiple-scattering path
tracer. The GPU protocol is implemented, but this repository does not claim a
runtime result until its captures are produced on a patched source engine.
