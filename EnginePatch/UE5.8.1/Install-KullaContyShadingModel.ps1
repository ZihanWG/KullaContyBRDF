[CmdletBinding(DefaultParameterSetName = 'Apply')]
param(
    [Parameter(Mandatory = $true)]
    [string] $EngineRoot,

    [Parameter(ParameterSetName = 'Check')]
    [switch] $CheckOnly,

    [Parameter(ParameterSetName = 'Revert')]
    [switch] $Revert
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-NormalizedText([string] $Path) {
    return [IO.File]::ReadAllText($Path).Replace("`r`n", "`n").Replace("`r", "`n")
}

function Write-NormalizedText([string] $Path, [string] $Text, [bool] $UseCrLf) {
    if ($UseCrLf) {
        $Text = $Text.Replace("`n", "`r`n")
    }
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Count-Literal([string] $Text, [string] $Needle) {
    $count = 0
    $offset = 0
    while (($offset = $Text.IndexOf($Needle, $offset, [StringComparison]::Ordinal)) -ge 0) {
        $count++
        $offset += $Needle.Length
    }
    return $count
}

$resolvedEngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$engineDirectory = Join-Path $resolvedEngineRoot 'Engine'
$versionPath = Join-Path $engineDirectory 'Build\Build.version'
if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "EngineRoot must contain Engine/Build/Build.version: $resolvedEngineRoot"
}

$version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8 -or $version.PatchVersion -ne 1) {
    throw "This patch targets UE 5.8.1; found $($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)."
}

$installedBuildMarker = Join-Path $engineDirectory 'Build\InstalledBuild.txt'
if ((Test-Path -LiteralPath $installedBuildMarker) -and -not $CheckOnly -and -not $Revert) {
    throw 'A Launcher/Installed Build cannot rebuild the modified Engine modules. Use a UE 5.8.1 source checkout and build it from source.'
}

$patchRoot = Join-Path $engineDirectory 'Saved\KullaContyShadingModelPatch'
$backupRoot = Join-Path $patchRoot 'Backup'
$manifestPath = Join-Path $patchRoot 'manifest.json'
$shaderPayloads = @(
    [pscustomobject]@{
        Source = Join-Path $PSScriptRoot 'KullaContyBxDF.ush'
        RelativePath = 'Engine/Shaders/Private/KullaContyBxDF.ush'
    },
    [pscustomobject]@{
        Source = Join-Path $PSScriptRoot 'KullaContyEAvg.ush'
        RelativePath = 'Engine/Shaders/Private/KullaContyEAvg.ush'
    }
)

if ($Revert) {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "No Kulla-Conty patch manifest was found at $manifestPath"
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($file in $manifest.Files) {
        $target = Join-Path $resolvedEngineRoot ($file.RelativePath.Replace('/', '\'))
        if ($file.ExistedBefore) {
            $backup = Join-Path $backupRoot ($file.RelativePath.Replace('/', '\'))
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                throw "Missing backup file: $backup"
            }
            Copy-Item -LiteralPath $backup -Destination $target -Force
        }
        elseif (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target -Force
        }
    }

    Remove-Item -LiteralPath $patchRoot -Recurse -Force
    Write-Host 'Kulla-Conty shading-model patch reverted successfully.'
    exit 0
}

foreach ($shaderPayload in $shaderPayloads) {
    if (-not (Test-Path -LiteralPath $shaderPayload.Source -PathType Leaf)) {
        throw "Missing shader payload: $($shaderPayload.Source)"
    }
}

$replacements = [Collections.Generic.List[object]]::new()
function Add-Replacement([string] $RelativePath, [string] $Before, [string] $After) {
    $script:replacements.Add([pscustomobject]@{
        RelativePath = $RelativePath
        Before = $Before.Replace("`r`n", "`n")
        After = $After.Replace("`r`n", "`n")
    })
}

Add-Replacement 'Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h' @'
	MSM_Strata					UMETA(DisplayName="Substrate", Hidden),
	/** Number of unique shading models. */
'@ @'
	MSM_Strata					UMETA(DisplayName="Substrate", Hidden),
	MSM_KullaConty			UMETA(DisplayName="Kulla-Conty"),
	/** Number of unique shading models. */
'@

Add-Replacement 'Engine/Source/Runtime/RenderCore/Public/ShaderMaterial.h' @'
	uint8 MATERIAL_SHADINGMODEL_DEFAULT_LIT : 1;
	uint8 MATERIAL_SHADINGMODEL_SUBSURFACE : 1;
'@ @'
	uint8 MATERIAL_SHADINGMODEL_DEFAULT_LIT : 1;
	uint8 MATERIAL_SHADINGMODEL_KULLA_CONTY : 1;
	uint8 MATERIAL_SHADINGMODEL_SUBSURFACE : 1;
'@

Add-Replacement 'Engine/Shaders/Private/Definitions.usf' @'
#ifndef MATERIAL_SHADINGMODEL_DEFAULT_LIT
#define MATERIAL_SHADINGMODEL_DEFAULT_LIT				0
#endif

#ifndef MATERIAL_SHADINGMODEL_SUBSURFACE
'@ @'
#ifndef MATERIAL_SHADINGMODEL_DEFAULT_LIT
#define MATERIAL_SHADINGMODEL_DEFAULT_LIT				0
#endif

#ifndef MATERIAL_SHADINGMODEL_KULLA_CONTY
#define MATERIAL_SHADINGMODEL_KULLA_CONTY				0
#endif

#ifndef MATERIAL_SHADINGMODEL_SUBSURFACE
'@

Add-Replacement 'Engine/Shaders/Private/ShadingCommon.ush' @'
#define SHADINGMODELID_SUBSTRATE			12		// Temporary while we convert everything to Substrate
#define SHADINGMODELID_SUBSTRATE_TOON		13
#define SHADINGMODELID_NUM					14
'@ @'
#define SHADINGMODELID_SUBSTRATE			12		// Temporary while we convert everything to Substrate
#define SHADINGMODELID_KULLA_CONTY		13
#define SHADINGMODELID_SUBSTRATE_TOON		14
#define SHADINGMODELID_NUM					15
'@

Add-Replacement 'Engine/Shaders/Private/ShadingCommon.ush' @'
	else if (ShadingModelID == SHADINGMODELID_SUBSTRATE) return float3(1.0f, 1.0f, 0.0f);
	else return float3(1.0f, 1.0f, 1.0f); // White
'@ @'
	else if (ShadingModelID == SHADINGMODELID_SUBSTRATE) return float3(1.0f, 1.0f, 0.0f);
	else if (ShadingModelID == SHADINGMODELID_KULLA_CONTY) return float3(0.9f, 0.2f, 1.0f); // Magenta
	else return float3(1.0f, 1.0f, 1.0f); // White
'@

Add-Replacement 'Engine/Shaders/Private/ShadingCommon.ush' @'
		case SHADINGMODELID_SUBSTRATE: return float3(1.0f, 1.0f, 0.0f);
		default: return float3(1.0f, 1.0f, 1.0f); // White
'@ @'
		case SHADINGMODELID_SUBSTRATE: return float3(1.0f, 1.0f, 0.0f);
		case SHADINGMODELID_KULLA_CONTY: return float3(0.9f, 0.2f, 1.0f); // Magenta
		default: return float3(1.0f, 1.0f, 1.0f); // White
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp' @'
		if (EnvironmentDefines->HasShadingModel(MSM_DefaultLit))
		{
			OutEnvironment.SetDefine(TEXT("MATERIAL_SHADINGMODEL_DEFAULT_LIT"), TEXT("1"));
		}

		if (EnvironmentDefines->HasShadingModel(MSM_Subsurface) || EnumHasAnyFlags(SubstrateTranslatorData.SubstrateMaterialBsdfFeatures, ESubstrateBsdfFeature::SSS))
'@ @'
		if (EnvironmentDefines->HasShadingModel(MSM_DefaultLit))
		{
			OutEnvironment.SetDefine(TEXT("MATERIAL_SHADINGMODEL_DEFAULT_LIT"), TEXT("1"));
		}

		if (EnvironmentDefines->HasShadingModel(MSM_KullaConty))
		{
			OutEnvironment.SetDefine(TEXT("MATERIAL_SHADINGMODEL_KULLA_CONTY"), TEXT("1"));
		}

		if (EnvironmentDefines->HasShadingModel(MSM_Subsurface) || EnumHasAnyFlags(SubstrateTranslatorData.SubstrateMaterialBsdfFeatures, ESubstrateBsdfFeature::SSS))
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/Materials/MaterialIRToHLSLTranslator.cpp' @'
		case MSM_DefaultLit: return TEXT("MATERIAL_SHADINGMODEL_DEFAULT_LIT");
		case MSM_Subsurface: return TEXT("MATERIAL_SHADINGMODEL_SUBSURFACE");
'@ @'
		case MSM_DefaultLit: return TEXT("MATERIAL_SHADINGMODEL_DEFAULT_LIT");
		case MSM_KullaConty: return TEXT("MATERIAL_SHADINGMODEL_KULLA_CONTY");
		case MSM_Subsurface: return TEXT("MATERIAL_SHADINGMODEL_SUBSURFACE");
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/Materials/MaterialShader.cpp' @'
		case MSM_DefaultLit:		ShadingModelName = TEXT("MSM_DefaultLit"); break;
		case MSM_Subsurface:		ShadingModelName = TEXT("MSM_Subsurface"); break;
'@ @'
		case MSM_DefaultLit:		ShadingModelName = TEXT("MSM_DefaultLit"); break;
		case MSM_KullaConty:		ShadingModelName = TEXT("MSM_KullaConty"); break;
		case MSM_Subsurface:		ShadingModelName = TEXT("MSM_Subsurface"); break;
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/Materials/MaterialShader.cpp' @'
else if (ShadingModels.HasAnyShadingModel({ MSM_DefaultLit, MSM_Subsurface, MSM_PreintegratedSkin, MSM_ClearCoat, MSM_Cloth, MSM_SubsurfaceProfile, MSM_TwoSidedFoliage, MSM_SingleLayerWater, MSM_ThinTranslucent }))
'@ @'
else if (ShadingModels.HasAnyShadingModel({ MSM_DefaultLit, MSM_KullaConty, MSM_Subsurface, MSM_PreintegratedSkin, MSM_ClearCoat, MSM_Cloth, MSM_SubsurfaceProfile, MSM_TwoSidedFoliage, MSM_SingleLayerWater, MSM_ThinTranslucent }))
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderGenerationUtil.cpp' @'
	FETCH_COMPILE_BOOL(MATERIAL_SHADINGMODEL_DEFAULT_LIT);
	FETCH_COMPILE_BOOL(MATERIAL_SHADINGMODEL_SUBSURFACE);
'@ @'
	FETCH_COMPILE_BOOL(MATERIAL_SHADINGMODEL_DEFAULT_LIT);
	FETCH_COMPILE_BOOL(MATERIAL_SHADINGMODEL_KULLA_CONTY);
	FETCH_COMPILE_BOOL(MATERIAL_SHADINGMODEL_SUBSURFACE);
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderGenerationUtil.cpp' @'
	case MSM_DefaultLit:
		SetSharedGBufferSlots(Slots);
'@ @'
	case MSM_DefaultLit:
	case MSM_KullaConty:
		SetSharedGBufferSlots(Slots);
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderGenerationUtil.cpp' @'
	if (Mat.MATERIAL_SHADINGMODEL_DEFAULT_LIT)
	{
		SetStandardGBufferSlots(Slots, bWriteEmissive, bHasTangent, bHasVelocity, bWritesVelocity, bHasStaticLighting, bIsSubstrateMaterial, bIsSubstrateNewGBuffer);
	}
'@ @'
	if (Mat.MATERIAL_SHADINGMODEL_DEFAULT_LIT || Mat.MATERIAL_SHADINGMODEL_KULLA_CONTY)
	{
		SetStandardGBufferSlots(Slots, bWriteEmissive, bHasTangent, bHasVelocity, bWritesVelocity, bHasStaticLighting, bIsSubstrateMaterial, bIsSubstrateNewGBuffer);
	}
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionShadingModel.h' @'
meta=(ValidEnumValues="MSM_DefaultLit, MSM_Subsurface, MSM_PreintegratedSkin, MSM_ClearCoat, MSM_SubsurfaceProfile, MSM_TwoSidedFoliage, MSM_Hair, MSM_Cloth, MSM_Eye", ShowAsInputPin = "Primary")
'@ @'
meta=(ValidEnumValues="MSM_DefaultLit, MSM_KullaConty, MSM_Subsurface, MSM_PreintegratedSkin, MSM_ClearCoat, MSM_SubsurfaceProfile, MSM_TwoSidedFoliage, MSM_Hair, MSM_Cloth, MSM_Eye", ShowAsInputPin = "Primary")
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp' @'
void UEngine::LoadEnergyTextures()
{
'@ @'
static TAutoConsoleVariable<int32> CVarKullaContyRequireEnergyLUT(
	TEXT("r.KullaConty.RequireEnergyLUT"),
	1,
	TEXT("Keep UE's GGX directional-albedo LUT resident for the Kulla-Conty shading model.\n")
	TEXT(" 0: off (Kulla-Conty materials are invalid)\n")
	TEXT(" 1: on"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

void UEngine::LoadEnergyTextures()
{
'@

Add-Replacement 'Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp' @'
	static const auto CVarEnergyConservation = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Material.EnergyConservation"));
	const bool bEnergyConservationEnabled = CVarEnergyConservation && CVarEnergyConservation->GetValueOnAnyThread() > 0;

	// NOTE: this logic must match GetSettings from ShadingEnergyConservation.cpp
	const bool bLoadEnergyConservationTextures = bPathTracingEnabled || bSubstrateEnabled || bEnergyConservationEnabled;
'@ @'
	static const auto CVarEnergyConservation = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Material.EnergyConservation"));
	const bool bEnergyConservationEnabled = CVarEnergyConservation && CVarEnergyConservation->GetValueOnAnyThread() > 0;
	const bool bKullaContyRequiresEnergyLUT = CVarKullaContyRequireEnergyLUT.GetValueOnAnyThread() > 0;

	// NOTE: this logic must match GetSettings from ShadingEnergyConservation.cpp
	const bool bLoadEnergyConservationTextures = bKullaContyRequiresEnergyLUT || bPathTracingEnabled || bSubstrateEnabled || bEnergyConservationEnabled;
'@

Add-Replacement 'Engine/Source/Runtime/Renderer/Private/ShadingEnergyConservation.cpp' @'
	// Enabled based on settings
	const bool bMaterialEnergyConservationEnabled = CVarMaterialEnergyConservation.GetValueOnRenderThread() > 0;
	Out.bIsEnergyConservationEnabled = CVarShadingEnergyConservation.GetValueOnRenderThread() > 0;
	Out.bIsEnergyPreservationEnabled = CVarShadingEnergyConservation_Preservation.GetValueOnRenderThread() > 0;

	// Build/bind table if energy conservation is enabled or if Substrate is enabled in order to have 
	// the correct tables built & bound. Even if we are not using energy conservation, we want to 
	// have access to directional albedo information for env. lighting for instance)
	Out.bNeedData = (bMaterialEnergyConservationEnabled || Substrate::IsSubstrateEnabled() || (View.Family->EngineShowFlags.PathTracing)) && (Out.bIsEnergyPreservationEnabled || Out.bIsEnergyConservationEnabled);
'@ @'
	// Enabled based on settings
	const bool bMaterialEnergyConservationEnabled = CVarMaterialEnergyConservation.GetValueOnRenderThread() > 0;
	static const auto CVarKullaContyRequireEnergyLUT = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.KullaConty.RequireEnergyLUT"));
	const bool bKullaContyRequiresEnergyLUT = CVarKullaContyRequireEnergyLUT && CVarKullaContyRequireEnergyLUT->GetValueOnRenderThread() > 0;
	Out.bIsEnergyConservationEnabled = CVarShadingEnergyConservation.GetValueOnRenderThread() > 0;
	Out.bIsEnergyPreservationEnabled = CVarShadingEnergyConservation_Preservation.GetValueOnRenderThread() > 0;

	// Kulla-Conty evaluates the raw single-scatter GGX lobe and reads directional
	// albedo directly. It therefore needs the LUT even when legacy material energy
	// conservation and Substrate are both disabled.
	Out.bNeedData = bKullaContyRequiresEnergyLUT ||
		((bMaterialEnergyConservationEnabled || Substrate::IsSubstrateEnabled() || (View.Family->EngineShowFlags.PathTracing)) &&
		(Out.bIsEnergyPreservationEnabled || Out.bIsEnergyConservationEnabled));
'@

Add-Replacement 'Engine/Shaders/Private/ShadingModels.ush' @'
// UE_DEPRECATED 5.7 - Deprecated by Substrate
FDirectLighting DefaultLitBxDF( FGBufferData GBuffer, half3 N, half3 V, FAreaLight AreaLight, FShadowTerms Shadow )
'@ @'
#include "KullaContyBxDF.ush"

// UE_DEPRECATED 5.7 - Deprecated by Substrate
FDirectLighting DefaultLitBxDF( FGBufferData GBuffer, half3 N, half3 V, FAreaLight AreaLight, FShadowTerms Shadow )
'@

Add-Replacement 'Engine/Shaders/Private/ShadingModels.ush' @'
		case SHADINGMODELID_THIN_TRANSLUCENT:
			return DefaultLitBxDF( GBuffer, N, V, AreaLight, Shadow );
		case SHADINGMODELID_SUBSURFACE:
'@ @'
		case SHADINGMODELID_THIN_TRANSLUCENT:
			return DefaultLitBxDF( GBuffer, N, V, AreaLight, Shadow );
		case SHADINGMODELID_KULLA_CONTY:
			return KullaContyBxDF( GBuffer, N, V, AreaLight, Shadow );
		case SHADINGMODELID_SUBSURFACE:
'@

Add-Replacement 'Engine/Shaders/Private/ClusteredDeferredShadingPixelShader.usf' @'
	GET_LIGHT_GRID_LOCAL_LIGHTING_SINGLE_SM(SHADINGMODELID_DEFAULT_LIT,			PixelShadingModelID, CompositedLighting, ScreenUV, CulledLightGridHeader, Dither);
	GET_LIGHT_GRID_LOCAL_LIGHTING_SINGLE_SM(SHADINGMODELID_SUBSURFACE,			PixelShadingModelID, CompositedLighting, ScreenUV, CulledLightGridHeader, Dither);
'@ @'
	GET_LIGHT_GRID_LOCAL_LIGHTING_SINGLE_SM(SHADINGMODELID_DEFAULT_LIT,			PixelShadingModelID, CompositedLighting, ScreenUV, CulledLightGridHeader, Dither);
	GET_LIGHT_GRID_LOCAL_LIGHTING_SINGLE_SM(SHADINGMODELID_KULLA_CONTY,		PixelShadingModelID, CompositedLighting, ScreenUV, CulledLightGridHeader, Dither);
	GET_LIGHT_GRID_LOCAL_LIGHTING_SINGLE_SM(SHADINGMODELID_SUBSURFACE,			PixelShadingModelID, CompositedLighting, ScreenUV, CulledLightGridHeader, Dither);
'@

Add-Replacement 'Engine/Source/Editor/PixelInspector/Private/PixelInspectorResult.h' @'
#define PIXEL_INSPECTOR_SHADINGMODELID_SUBSTRATE 12
#define PIXEL_INSPECTOR_SHADINGMODELID_MASK 0xF
'@ @'
#define PIXEL_INSPECTOR_SHADINGMODELID_SUBSTRATE 12
#define PIXEL_INSPECTOR_SHADINGMODELID_KULLA_CONTY 13
#define PIXEL_INSPECTOR_SHADINGMODELID_MASK 0xF
'@

Add-Replacement 'Engine/Source/Editor/PixelInspector/Private/PixelInspectorResult.cpp' @'
		case PIXEL_INSPECTOR_SHADINGMODELID_SUBSTRATE:
			return EMaterialShadingModel::MSM_Strata;
		};
'@ @'
		case PIXEL_INSPECTOR_SHADINGMODELID_SUBSTRATE:
			return EMaterialShadingModel::MSM_Strata;
		case PIXEL_INSPECTOR_SHADINGMODELID_KULLA_CONTY:
			return EMaterialShadingModel::MSM_KullaConty;
		};
'@

Add-Replacement 'Engine/Shaders/Private/PostProcessGBufferHints.usf' @'
	if (In == SHADINGMODELID_SUBSTRATE)				{ Print(Ctx, TEXT("SUBSTRATE"), FontRed); return; }
	if (In == SHADINGMODELID_SUBSTRATE_TOON)		{ Print(Ctx, TEXT("TOON"), FontRed); return; }
'@ @'
	if (In == SHADINGMODELID_SUBSTRATE)				{ Print(Ctx, TEXT("SUBSTRATE"), FontRed); return; }
	if (In == SHADINGMODELID_KULLA_CONTY)			{ Print(Ctx, TEXT("KULLA_CONTY"), FontRed); return; }
	if (In == SHADINGMODELID_SUBSTRATE_TOON)		{ Print(Ctx, TEXT("TOON"), FontRed); return; }
'@

$states = [Collections.Generic.List[string]]::new()
foreach ($replacement in $replacements) {
    $target = Join-Path $resolvedEngineRoot ($replacement.RelativePath.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Missing target file: $target"
    }
    $text = Read-NormalizedText $target
    $beforeCount = Count-Literal $text $replacement.Before
    $afterCount = Count-Literal $text $replacement.After
    # Check the longer replacement first: in insertion-style edits, the old
    # block can remain as a literal substring of the patched block.
    if ($afterCount -eq 1) {
        $states.Add('Applied')
    }
    elseif ($beforeCount -eq 1) {
        $states.Add('Pending')
    }
    else {
        throw "Patch context mismatch in $($replacement.RelativePath). Expected one unambiguous before/after block; found before=$beforeCount, after=$afterCount."
    }
}

$pendingCount = @($states | Where-Object { $_ -eq 'Pending' }).Count
$appliedCount = @($states | Where-Object { $_ -eq 'Applied' }).Count
if ($pendingCount -gt 0 -and $appliedCount -gt 0) {
    throw 'The engine is partially patched. Restore a clean UE 5.8.1 source tree before applying this patch.'
}

if ($pendingCount -eq 0) {
    foreach ($shaderPayload in $shaderPayloads) {
        $shaderTarget = Join-Path $resolvedEngineRoot ($shaderPayload.RelativePath.Replace('/', '\'))
        if (-not (Test-Path -LiteralPath $shaderTarget -PathType Leaf)) {
            throw "The C++ patch is present but a shader payload is missing: $shaderTarget"
        }
        $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $shaderPayload.Source).Hash
        $targetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $shaderTarget).Hash
        if ($sourceHash -ne $targetHash) {
            throw "The installed shader payload differs from this repository: $shaderTarget"
        }
    }
}

if ($CheckOnly) {
    $buildKind = if (Test-Path -LiteralPath $installedBuildMarker) { 'Launcher/Installed Build (validation only; cannot rebuild Engine modules)' } else { 'Source Build' }
    $state = if ($pendingCount -eq 0) { 'already applied' } else { 'ready to apply' }
    Write-Host "UE 5.8.1 patch validation passed: $state. Engine type: $buildKind."
    exit 0
}

if ($pendingCount -eq 0) {
    Write-Host 'Kulla-Conty shading-model patch is already present.'
    exit 0
}

if (Test-Path -LiteralPath $manifestPath) {
    throw "A patch manifest already exists: $manifestPath"
}

$uniqueFiles = @($replacements.RelativePath + $shaderPayloads.RelativePath | Sort-Object -Unique)
$manifestFiles = [Collections.Generic.List[object]]::new()

try {
    foreach ($relativePath in $uniqueFiles) {
        $target = Join-Path $resolvedEngineRoot ($relativePath.Replace('/', '\'))
        $existed = Test-Path -LiteralPath $target -PathType Leaf
        $manifestFiles.Add([pscustomobject]@{ RelativePath = $relativePath; ExistedBefore = $existed })
        if ($existed) {
            $backup = Join-Path $backupRoot ($relativePath.Replace('/', '\'))
            New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $backup -Force
        }
    }

    foreach ($replacement in $replacements) {
        $target = Join-Path $resolvedEngineRoot ($replacement.RelativePath.Replace('/', '\'))
        $raw = [IO.File]::ReadAllText($target)
        $useCrLf = $raw.Contains("`r`n")
        $text = $raw.Replace("`r`n", "`n").Replace("`r", "`n")
        $text = $text.Replace($replacement.Before, $replacement.After)
        Write-NormalizedText $target $text $useCrLf
    }

    foreach ($shaderPayload in $shaderPayloads) {
        $shaderTarget = Join-Path $resolvedEngineRoot ($shaderPayload.RelativePath.Replace('/', '\'))
        Copy-Item -LiteralPath $shaderPayload.Source -Destination $shaderTarget -Force
    }
    New-Item -ItemType Directory -Path $patchRoot -Force | Out-Null
    [pscustomobject]@{
        Patch = 'KullaContyShadingModel'
        EngineVersion = '5.8.1'
        AppliedAtUtc = [DateTime]::UtcNow.ToString('o')
        Files = $manifestFiles
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}
catch {
    foreach ($file in $manifestFiles) {
        $target = Join-Path $resolvedEngineRoot ($file.RelativePath.Replace('/', '\'))
        $backup = Join-Path $backupRoot ($file.RelativePath.Replace('/', '\'))
        if ($file.ExistedBefore -and (Test-Path -LiteralPath $backup)) {
            Copy-Item -LiteralPath $backup -Destination $target -Force
        }
        elseif (-not $file.ExistedBefore -and (Test-Path -LiteralPath $target)) {
            Remove-Item -LiteralPath $target -Force
        }
    }
    throw
}

Write-Host 'Kulla-Conty shading-model patch applied. Rebuild UnrealEditor from the UE 5.8.1 source solution, then clear the project DDC before first launch.'
