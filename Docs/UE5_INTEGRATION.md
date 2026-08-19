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
float3 FMs = FAvg * FAvg * E_Avg /
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

For a production result, call the same functions from a custom engine shading
model or a supported Substrate BSDF path. The integration point must have access
to each light's `L`, radiance, attenuation and shadow term. Add `f_ms` to the
single-scatter GGX BRDF before multiplication by incident radiance and `NoL`.

Keep the LUT geometry function consistent with the single-scatter GGX geometry.
If the engine BRDF uses a different correlated Smith approximation, regenerate
the LUT using that same function.

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

These measurements support an energy-conservation claim much more strongly than
tone-mapped screenshots alone.
