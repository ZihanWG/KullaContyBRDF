[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $EngineRoot,

    [string] $ProjectPath = (Join-Path $PSScriptRoot '..\..\KC.uproject'),

    [ValidateSet('DebugGame', 'Development', 'Shipping')]
    [string] $Configuration = 'Development',

    [switch] $SkipBuild,
    [switch] $SkipSmokeTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedEngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$engineDirectory = Join-Path $resolvedEngineRoot 'Engine'
$versionPath = Join-Path $engineDirectory 'Build\Build.version'
$installedBuildMarker = Join-Path $engineDirectory 'Build\InstalledBuild.txt'
$patchManifest = Join-Path $engineDirectory 'Saved\KullaContyShadingModelPatch\manifest.json'

if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "EngineRoot must contain Engine/Build/Build.version: $resolvedEngineRoot"
}
if (Test-Path -LiteralPath $installedBuildMarker) {
    throw 'This workflow requires a UE 5.8.1 source build, not a Launcher/Installed Build.'
}

$version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8 -or $version.PatchVersion -ne 1) {
    throw "Expected UE 5.8.1; found $($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)."
}
if (-not (Test-Path -LiteralPath $patchManifest -PathType Leaf)) {
    throw 'The Kulla-Conty patch is not installed. Run Install-KullaContyShadingModel.ps1 first.'
}

if (-not $SkipBuild) {
    $buildScript = Join-Path $engineDirectory 'Build\BatchFiles\Build.bat'
    if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
        throw "Missing Unreal Build Tool entry point: $buildScript"
    }

    & $buildScript UnrealEditor Win64 $Configuration -WaitMutex -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) {
        throw "UnrealEditor build failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipSmokeTest) {
    $editorCommand = Join-Path $engineDirectory 'Binaries\Win64\UnrealEditor-Cmd.exe'
    if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
        throw "Missing built editor command: $editorCommand"
    }

    # RenderOffscreen keeps a real desktop RHI and shader compilation active,
    # unlike NullRHI. Quitting after startup catches global/material shader
    # parse failures without opening a visible editor window.
    & $editorCommand $resolvedProjectPath `
        -unattended `
        -nop4 `
        -nosplash `
        -RenderOffscreen `
        '-ExecCmds=Quit' `
        -log
    if ($LASTEXITCODE -ne 0) {
        throw "UnrealEditor smoke test failed with exit code $LASTEXITCODE. Inspect the project Saved/Logs directory."
    }
}

Write-Host 'UE 5.8.1 Kulla-Conty source build and smoke test completed successfully.'
