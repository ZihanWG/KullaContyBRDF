# Kulla–Conty multi-scattering BRDF in Unreal Engine 5

An experimental real-time implementation of Kulla–Conty energy compensation
for isotropic GGX materials. The project precomputes directional albedo LUTs on
the CPU, evaluates a multiple-scattering BRDF term at runtime, and compares it
with a single-scattering GGX baseline in controlled UE5 scenes.

![Roughness row comparison](roughness_row.png)

![Cornell box comparison](cornell_box.png)

## Why this project exists

A conventional microfacet BRDF accounts for paths that reflect from one
microfacet and then leave the surface. At higher roughness, masking and
shadowing cause some energy to remain inside the microfacet layer. Ignoring
subsequent bounces makes the material lose energy and can darken rough specular
reflection.

Kulla and Conty approximate those higher-order paths with an additional lobe:

```text
f = f_single + f_multi

          (1 - E(NoV)) (1 - E(NoL))
f_multi = --------------------------- * F_ms
                 pi (1 - E_avg)
```

`E(mu)` is the directional albedo of the GGX BRDF with Fresnel set to one, and
`E_avg` is its cosine-weighted hemispherical average. `F_ms` accounts for
Fresnel absorption over repeated bounces.

## What is implemented

- CPU Monte Carlo integration using a Hammersley sequence
- GGX half-vector importance sampling
- Exact separable Smith GGX masking-shadowing
- `E(NdotX, roughness)` directional-albedo LUT
- `E_avg(roughness)` cosine-weighted average LUT
- Optional Schlick split-sum A/B LUT
- UE-ready 16-bit linear PNG, Radiance HDR and unquantized PFM output
- Complete light- and view-dependent Kulla–Conty reference HLSL
- UE5 roughness-row and Cornell-box evaluation scenes

The project distinguishes a controlled BRDF experiment from a production UE
shading-model integration. A regular Default Lit Custom Expression does not
replace UE's internal BRDF; see [the UE5 integration guide](Docs/UE5_INTEGRATION.md)
for both supported paths.

## Repository layout

```text
Config/                         UE project configuration
Content/                        Test maps, materials and imported LUT assets
Docs/UE5_INTEGRATION.md         Material setup and evaluation protocol
LUT/LUT/LUT.cpp                 Reproducible LUT generator
LUT/CMakeLists.txt              Portable generator build
Shaders/KullaContyBRDF.hlsl     Reference GGX + Kulla–Conty implementation
KC.uproject                     Unreal Engine project
```

## Build and generate the LUTs

The generator requires a C++17 compiler. You can build `LUT/LUT.sln` in Visual
Studio, or use CMake:

```text
cmake -S LUT -B LUT/build
cmake --build LUT/build --config Release
```

Generate the reference 256×256 textures with 1,024 samples per texel:

```text
KullaContyLUTGenerator --size 256 --samples 1024 --output LUT/Generated
```

For a fast smoke test:

```text
KullaContyLUTGenerator --size 16 --samples 64 --output LUT/Generated
```

Generated files:

| Output | Purpose |
| --- | --- |
| `E_mu.png` | 16-bit runtime directional albedo sampled with `(NdotX, roughness)` |
| `E_avg.png` | 16-bit runtime average albedo sampled with roughness |
| `BRDF_SplitSum.png` | Optional 16-bit A/B split-sum reference |
| `*.hdr` | Linear Radiance-HDR alternatives |
| `*.pfm` | Full-float validation data |
| `*_preview.png` | Human-readable 8-bit previews only |
| `metadata.json` | Parameters, conventions and numerical ranges |

Using texel centers avoids the `NdotX = 0` singularity. Both generator and HLSL
use perceptual roughness with `alpha = roughness²`.

## Runtime evaluation

For each shading point:

1. Compute `NoV = saturate(dot(N, V))`.
2. Compute `NoL = saturate(dot(N, L))` for the current light.
3. Sample `E_NoV = E_mu(NoV, roughness)`.
4. Sample `E_NoL = E_mu(NoL, roughness)`.
5. Sample `E_Avg = E_avg(roughness)`.
6. Evaluate `KC_MultiScatterFromDirectionalAlbedo`.
7. Add the result to the matching single-scatter GGX BRDF.

The implementation is in
[`Shaders/KullaContyBRDF.hlsl`](Shaders/KullaContyBRDF.hlsl). It does not clamp
the resulting BRDF to `[0, 1]`; BRDF values are not colors and may legitimately
exceed one while their integrated reflected energy remains bounded.

## Existing visual results

The original experiments used metallic spheres with roughness values spanning
0 to 1 and a Cornell box under controlled lighting.

Observed behavior:

- low roughness: baseline and compensated results are nearly identical
- medium roughness: the clearest increase in retained specular energy
- high roughness: differences remain subtle and depend on lighting and exposure

These screenshots demonstrate a visible appearance change, but do not by
themselves prove lower physical error. A stronger evaluation should use a fixed
mesh and camera, manual exposure, linear HDR captures, difference images,
hemispherical energy measurements and a multiple-scattering reference. The full
protocol is documented in [Docs/UE5_INTEGRATION.md](Docs/UE5_INTEGRATION.md).

## Important limitations

- The existing `.uasset` materials were created as an early prototype and must
  be reconnected to the corrected `NoV` + `NoL` implementation.
- Production integration requires access to UE's per-light shading loop through
  a custom shading model or a supported Substrate path.
- The included scenes cover simple spheres rather than layered or anisotropic
  materials.
- GPU performance and numerical error still need to be measured.
- The current LUT and shader use separable Smith GGX. A production integration
  must regenerate the LUT if its single-scatter BRDF uses a different geometry
  approximation.

## References

- Christopher Kulla and Alejandro Conty, *Revisiting Physically Based Shading
  at Imageworks*, SIGGRAPH 2017 course notes
- Eric Heitz et al., *Multiple-Scattering Microfacet BSDFs with the Smith Model*
- Unreal Engine rendering and material documentation
- Parametric Cornell Box scene from the UE Fab library

## Author

Zihan Wang (王滋涵) — computer graphics, rendering and XR

- [GitHub](https://github.com/ZihanWG)
- [Technical write-up](https://zihanwportfolio.wordpress.com/2025/05/06/integrating-kulla-conty-brdf-for-real-time-pbr-in-ue5/)
