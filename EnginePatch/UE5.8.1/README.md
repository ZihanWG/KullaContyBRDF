# UE 5.8.1 Kulla-Conty shading model

This directory contains a repository-owned patch for a real
`MSM_KullaConty` material shading model. Unlike the earlier material-node
prototype, the model runs inside Unreal Engine's light loop and receives each
light's direction, radiance and attenuation.

The patch is intentionally not applied to an Epic Games Launcher installation.
It changes Engine C++ headers and modules, so it requires a UE 5.8.1 source
checkout that can rebuild `UnrealEditor`.

## Distribution boundary

This directory does not contain an Unreal Engine checkout. Users must obtain a
separately licensed UE 5.8.1 source tree from Epic. Small Engine Code matching
anchors used to support installation remain governed by the Unreal Engine EULA
and are not licensed by this project. Do not copy additional Engine Code into
this repository. See the repository's [licensing boundary](../../Docs/LICENSING.md)
and [third-party notices](../../THIRD_PARTY_NOTICES.md).

## What the patch changes

- adds `Kulla-Conty` to the Material Shading Model selector
- assigns GBuffer shading-model ID 13 to Kulla-Conty and moves the shader-only
  Substrate Toon ID to 14; both remain inside the existing four-bit field
- adds definitions for legacy and MIR material translation
- routes deferred, forward and clustered-deferred direct lighting through
  `KullaContyBxDF`
- keeps UE's GGX directional-albedo LUT resident even when legacy material
  energy conservation and Substrate are disabled
- exposes the model in Pixel Inspector and GBuffer debug output
- uses the regular Default Lit GBuffer layout without custom-data channels

`KullaContyBxDF.ush` evaluates UE's raw single-scatter GGX lobe, then adds the
reciprocal Kulla-Conty multiple-scatter lobe. It does not call Default Lit's
energy-conservation multiplier, which prevents double compensation.

## Requirements

- Unreal Engine 5.8.1 source checkout
- Windows desktop target using SM5 or SM6
- Substrate disabled (`r.Substrate=0`)
- a compiler/toolchain supported by that UE source release

The installer validates the exact engine version and all patch contexts before
writing. It refuses to patch a Launcher/Installed Build.

## Validate without modifying the engine

From the repository root:

```powershell
.\EnginePatch\UE5.8.1\Install-KullaContyShadingModel.ps1 `
  -EngineRoot 'D:\UnrealEngine-5.8.1' `
  -CheckOnly
```

## Apply and build

```powershell
.\EnginePatch\UE5.8.1\Install-KullaContyShadingModel.ps1 `
  -EngineRoot 'D:\UnrealEngine-5.8.1'
```

Then regenerate the source-engine solution if needed and build the editor:

```powershell
& 'D:\UnrealEngine-5.8.1\GenerateProjectFiles.bat'
& 'D:\UnrealEngine-5.8.1\Engine\Build\BatchFiles\Build.bat' `
  UnrealEditor Win64 Development -WaitMutex
```

Alternatively, after applying the patch, use the repository workflow to build
`UnrealEditor` and launch the project once with an offscreen desktop RHI. This
keeps shader compilation enabled and reports a non-zero exit code on startup or
shader failures:

```powershell
.\EnginePatch\UE5.8.1\Build-And-SmokeTest.ps1 `
  -EngineRoot 'D:\UnrealEngine-5.8.1'
```

This workflow is syntax-checked in the repository but cannot be executed on a
Launcher Build; its first real run must use a source engine.

Associate `KC.uproject` with that source build, open the project, and change a
surface material's **Shading Model** to **Kulla-Conty**. Keep the usual Base
Color, Metallic, Specular, Roughness and Normal inputs. The first launch must
recompile the affected material and global shaders.

Use the viewport **Buffer Visualization > Shading Model** view or Pixel
Inspector to confirm the Kulla-Conty ID; its debug color is magenta.

## Roll back

The installer stores byte-for-byte backups under
`Engine/Saved/KullaContyShadingModelPatch`. To restore them:

```powershell
.\EnginePatch\UE5.8.1\Install-KullaContyShadingModel.ps1 `
  -EngineRoot 'D:\UnrealEngine-5.8.1' `
  -Revert
```

Rebuild `UnrealEditor` after reverting.

## Runtime cost and current scope

The default direct-light path performs two directional-albedo texture samples
per evaluated light. `E_avg(roughness)` is stored as 32 scalar constants derived
from UE 5.8.1's own energy texture and linearly interpolated, removing the four
extra texture samples used by the initial reference implementation. Define
`KULLA_CONTY_REFERENCE_EAVG=1` when compiling shaders to restore that four-point
Gauss-Legendre reference path for A/B comparison.

`Tools/ExportUEEnergyLUT.py` exports the official Engine texture to EXR, and
`Tools/AnalyzeUEEnergyLUT.py` reads it without third-party EXR dependencies. On
the full roughness domain from `0.02` through `1.0`, the embedded float32 table
has a maximum `E_avg` error of `2.9066e-8`, RMS error of `1.4883e-8`, and maximum
white-furnace error of `9.4947e-6` against 128-point integration of UE's
bilinearly sampled texture. These are numerical results, not GPU timings; source
build profiling still needs to report the actual frame/GPU cost reduction.

The read-only `r.KullaConty.RequireEnergyLUT` console variable defaults to `1`.
Keep it enabled whenever Kulla-Conty materials are present. This makes the
renderer load UE's precomputed energy tables independently of
`r.Material.EnergyConservation`; disabling it makes the model invalid. The
Kulla-Conty path specifically consumes the small GGX reflection-energy table;
unavailable unrelated tables continue to use UE's normal fallback resources.

The standalone exact-Smith reference LUT measures a maximum absolute error of
`0.0006462380` for the four-point reference quadrature. This supports the
reference path but does not replace source-build GPU profiling.

The fast-path shader can also be compiled independently with the Windows SDK
SM5 and SM6 compilers:

```powershell
.\EnginePatch\UE5.8.1\Test-EAvgShader.ps1
```

The test asserts that SM5 uses an immediate constant buffer and approximately
12 instruction slots. For SM6 it asserts a read-only 32-float global, two
indexed loads, no bound resources and no temporary table copy. This is a
compiler/disassembly check, not a GPU benchmark of the complete light loop.

After applying and building the patch, run the paired GPU benchmark:

```powershell
.\EnginePatch\UE5.8.1\Benchmark-EAvgModes.ps1 `
  -EngineRoot 'D:\UnrealEngine-5.8.1' `
  -Map '/Game/RowCompareTest' `
  -TrialsPerMode 5
```

It prewarms each shader variant for 300 frames, alternates Fast/Reference order,
captures 900 measured frames per trial at 1080p, discards startup/tail frames,
and writes raw Unreal CSV captures plus JSON/CSV summaries under
`BenchmarkResults/`. Matching trials are paired; the report includes median
deltas, cross-trial variation and deterministic 95% bootstrap confidence
intervals. Fewer than three pairs is marked insufficient, and an interval that
crosses zero is marked inconclusive. The script refuses Launcher builds and
restores the installed helper byte-for-byte even if a run fails. Use a
direct-light stress scene for a publishable claim; whole-frame differences in a
light scene can fall below normal GPU timing noise.

Current limitations:

- isotropic GGX only; the material's Anisotropy input is intentionally disabled
- direct punctual and directional lights are the primary validated design path
- rectangular lights retain UE's LTC single-scatter approximation and add an
  approximate diffuse-shaped multiple-scatter term
- indirect-light and reflection paths continue to use UE's shared legacy
  material handling rather than a dedicated Kulla-Conty IBL integration
- Path Tracer, hardware ray-tracing hit shaders and mobile are not implemented
- UE 5.8 marks the legacy `ShadingModels.ush` path as deprecated in favor of
  Substrate, so a future version should become a native Substrate BSDF

These constraints should be stated in portfolio captures rather than presented
as fully production-ready coverage.
