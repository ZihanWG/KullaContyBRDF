# UE5 integration guide

This project now separates two integration targets:

1. **Controlled BRDF experiment** — evaluates the complete single-scatter GGX +
   Kulla–Conty multi-scatter BRDF under one known light. This is the recommended
   way to produce defensible comparison images without changing engine source.
2. **Production shading-model integration** — adds the multi-scatter term inside
   an engine or Substrate BSDF implementation where every light's direction,
   attenuation and shadowing are available.

A regular Default Lit material Custom Expression cannot replace UE's internal
specular BRDF. Feeding a correction into Base Color, Specular or Emissive is an
appearance approximation, not a shading-model replacement.

## 1. Generate the LUTs

Build the generator in `LUT/` with Visual Studio or CMake, then run:

```text
KullaContyLUTGenerator --size 256 --samples 1024 --output Generated
```

The primary outputs are:

| File | Contents |
| --- | --- |
| `E_mu.png` | 16-bit scalar directional albedo `E(NdotX, roughness)`, replicated to RGB |
| `E_avg.png` | 16-bit `2 * integral(E(mu) * mu, 0..1)`, replicated across each row |
| `BRDF_SplitSum.png` | Optional 16-bit split-sum A/B terms in R/G |
| `*.hdr` | Linear Radiance-HDR alternatives |
| `*.pfm` | Unquantized reference copies for validation |
| `*_preview.png` | 8-bit previews only; do not use them for final measurements |
| `metadata.json` | Resolution, sample count, model and output ranges |
| `validation.json` | Automated white-furnace and quadrature checks |

The generator and runtime shader both use perceptual roughness with
`alpha = roughness²` and exact separable Smith GGX geometry.

## 2. Import settings

Import the 16-bit `E_mu.png` and `E_avg.png` into UE and set:

- sRGB: Off
- Address X/Y: Clamp
- Mip Gen Settings: NoMipmaps for exact validation; restore filtered mips only
  after confirming they do not bias the result
- Filter: Bilinear
- Compression: use an uncompressed 16-bit or float format and verify in the
  texture details that the imported source remains 16-bit and linear

Do not use the preview PNGs as production LUTs. They are quantized to 8 bits.

## 3. LUT coordinates

For normal `N`, view direction `V`, light direction `L` and perceptual roughness
`r`:

```text
NoV = saturate(dot(N, V))
NoL = saturate(dot(N, L))
E_NoV = E_mu.Sample(float2(NoV, r)).r
E_NoL = E_mu.Sample(float2(NoL, r)).r
E_Avg = E_avg.Sample(float2(0.5, r)).r
```

`E_avg` is constant across X, so the X coordinate is arbitrary. Sampling 0.5
avoids edge filtering.

## 4. Custom Expression inputs

For the multi-scatter term alone, create a Custom Expression with output type
`CMOT Float 3` and these inputs:

| Input | Type |
| --- | --- |
| `E_NoV` | float |
| `E_NoL` | float |
| `E_Avg` | float |
| `F0` | float3 |

Use this code:

```hlsl
E_NoV = saturate(E_NoV);
E_NoL = saturate(E_NoL);
E_Avg = saturate(E_Avg);
F0 = saturate(F0);

float3 FAvg = F0 + (1.0 - F0) / 21.0;
float3 FMs = FAvg * E_Avg /
    max(1.0 - FAvg * (1.0 - E_Avg), 1.0e-5);

return FMs * (1.0 - E_NoV) * (1.0 - E_NoL) /
    (3.141592653589793 * max(1.0 - E_Avg, 1.0e-5));
```

The previous project node used only `NdotV`; that omitted the light-side
`(1 - E(NoL))` term and could not represent the complete Kulla–Conty BRDF.

For metals, `F0` is the linear RGB normal-incidence reflectance, normally the
metal base color. `0.04` is a typical dielectric value and should not be used as
the F0 of a nickel or colored-metal comparison.

## 5. Controlled comparison without engine changes

Use an Unlit material and one known directional light vector supplied through a
Material Parameter Collection:

1. Compute `N`, `V`, `L`, `H`, `NoV`, `NoL`, `NoH` and `VoH`.
2. Sample `E_NoV`, `E_NoL` and `E_Avg` as above.
3. Evaluate the functions in `Shaders/KullaContyBRDF.hlsl`:
   `KC_GGXSingleScatter` and `KC_MultiScatterFromDirectionalAlbedo`.
4. Output `(singleScatter + multiScatter) * LightRadiance * NoL` to Emissive.
5. For the baseline, output `singleScatter * LightRadiance * NoL`.

This route intentionally bypasses UE's Default Lit light loop, so it does not
automatically include engine shadows or light attenuation. It is appropriate
for a controlled BRDF experiment, not a production material.

## 6. Production integration

The repository now includes an experimental UE 5.8.1 engine shading-model patch
under `EnginePatch/UE5.8.1`. It adds `MSM_KullaConty` to the material editor and
evaluates the compensation inside UE's light loop, where every light's
direction, radiance and attenuation are available. It deliberately evaluates
raw single-scatter GGX and then adds the Kulla-Conty lobe, so UE's optional
Default Lit energy compensation is not applied a second time.

The engine implementation reuses UE's own GGX directional-albedo texture. It
therefore does not bind the generated project LUTs. The default path stores the
32 per-roughness `E_avg` values derived from UE 5.8.1's texture as shader
constants and linearly interpolates them, reducing the direct-light path from
six texture samples to two. A compile-time four-point Gauss-Legendre path is
retained for comparison by defining `KULLA_CONTY_REFERENCE_EAVG=1`.

The source data and fast path are reproducible: run
`Tools/ExportUEEnergyLUT.py` through UnrealEditor-Cmd, then run
`Tools/AnalyzeUEEnergyLUT.py`. Against 128-point integration of UE's bilinearly
sampled 32×32 texture, the embedded float32 table measures `2.9066e-8` maximum
`E_avg` error and `9.4947e-6` maximum white-furnace error on roughness
`[0.02, 1]`. These numerical results do not replace a source-build GPU timing.

The runtime A/B is reproducible once a patched source build is available:

```powershell
.\EnginePatch\UE5.8.1\Benchmark-EAvgModes.ps1 `
  -EngineRoot 'D:\UnrealEngine-5.8.1' `
  -Map '/Game/RowCompareTest' `
  -TrialsPerMode 5
```

The script switches only the installed helper's compile-time default, runs
counterbalanced Fast/Reference captures through Unreal's GPU CSV profiler, then
restores the original shader bytes. Raw CSV files plus `summary.json`,
`summary.csv`, and chart-ready `paired_trials.csv` are written under ignored
`BenchmarkResults/`. The report uses paired-trial deltas and deterministic 95%
bootstrap confidence intervals; fewer than three pairs cannot produce a
performance claim. Use a source build; the workflow deliberately refuses a
Launcher installation.

See [`EnginePatch/UE5.8.1/README.md`](../EnginePatch/UE5.8.1/README.md) for source
engine requirements, installation, rollback and current renderer limitations.

For the standalone material experiment, keep the generated LUT geometry
function consistent with the reference single-scatter GGX implementation. The
engine shading model avoids that mismatch by using UE's own energy LUT.

## 7. Evaluation protocol

Use one mesh and switch materials between captures rather than placing variants
at different world positions. Lock all variables:

- manual exposure and identical tone-mapping settings
- Local Exposure disabled
- identical camera, mesh, normal, F0 and roughness
- linear HDR output before display mapping
- fixed light radiance and direction

Capture roughness values 0.0 to 1.0 and include:

- baseline image
- Kulla–Conty image
- amplified absolute-difference image
- mean and maximum luminance difference
- hemispherical reflected-energy error against a path-traced or numerical
  multiple-scattering reference
- GPU time and texture-sample count

Save the baseline and candidate as separate pixel-aligned files. Use
`KullaContyImageCompare` to produce linear metrics and difference images; do not
measure the existing combined presentation screenshots. The exact capture and
comparison contract is in [`IMAGE_VALIDATION.md`](IMAGE_VALIDATION.md).

These measurements support an energy-conservation claim much more strongly than
tone-mapped screenshots alone.

The CPU-side white-furnace and `E_avg` quadrature checks, compiler disassembly,
and paired GPU timing capture are automated; see
[`VALIDATION.md`](VALIDATION.md). The remaining evidence work is to execute the
GPU run on a source engine and produce the image-space/path-traced comparison.
