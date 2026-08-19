[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $EngineRoot,

    [string] $ProjectPath = (Join-Path $PSScriptRoot '..\..\KC.uproject'),
    [string] $Map = '/Game/RowCompareTest',

    [ValidateRange(1, 20)]
    [int] $TrialsPerMode = 5,

    [ValidateRange(300, 10000)]
    [int] $CaptureFrames = 900,

    [ValidateRange(0, 5000)]
    [int] $WarmupFrames = 300,

    [ValidateRange(0, 5000)]
    [int] $TrimStartFrames = 120,

    [ValidateRange(0, 5000)]
    [int] $TrimEndFrames = 30,

    [ValidateRange(320, 7680)]
    [int] $ResolutionX = 1920,

    [ValidateRange(240, 4320)]
    [int] $ResolutionY = 1080,

    [string] $ResultDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-FileSha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Set-EAvgMode([string] $Path, [byte[]] $OriginalBytes, [string] $Mode) {
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $text = $utf8.GetString($OriginalBytes)
    $replacement = if ($Mode -eq 'Reference') { '${1}1' } else { '${1}0' }
    $updated = [regex]::Replace(
        $text,
        '(?m)^(\s*#define\s+KULLA_CONTY_REFERENCE_EAVG\s+)[01]\s*$',
        $replacement
    )
    if ($updated -eq $text -and $Mode -eq 'Reference') {
        throw 'Could not switch KULLA_CONTY_REFERENCE_EAVG to the reference path.'
    }
    [System.IO.File]::WriteAllText($Path, $updated, $utf8)
}

$resolvedEngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$engineDirectory = Join-Path $resolvedEngineRoot 'Engine'
$versionPath = Join-Path $engineDirectory 'Build\Build.version'
$installedBuildMarker = Join-Path $engineDirectory 'Build\InstalledBuild.txt'
$patchManifest = Join-Path $engineDirectory 'Saved\KullaContyShadingModelPatch\manifest.json'
$installedHelper = Join-Path $engineDirectory 'Shaders\Private\KullaContyEAvg.ush'
$repositoryHelper = Join-Path $PSScriptRoot 'KullaContyEAvg.ush'
$editorCommand = Join-Path $engineDirectory 'Binaries\Win64\UnrealEditor-Cmd.exe'
$python = Get-Command python -ErrorAction SilentlyContinue
$pythonArguments = @()
if (-not $python) {
    $bundledPython = Join-Path $engineDirectory 'Binaries\ThirdParty\Python3\Win64\python.exe'
    if (Test-Path -LiteralPath $bundledPython -PathType Leaf) {
        $python = Get-Item -LiteralPath $bundledPython
    }
}
if (-not $python) {
    $python = Get-Command py -ErrorAction SilentlyContinue
    if ($python) {
        $pythonArguments = @('-3')
    }
}
if (-not $python) {
    throw 'Python 3 is required to summarize UE CSV captures. Install python or the Windows py launcher.'
}

if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "EngineRoot must contain Engine/Build/Build.version: $resolvedEngineRoot"
}
if (Test-Path -LiteralPath $installedBuildMarker) {
    throw 'GPU benchmarking requires a patched UE 5.8.1 source build, not a Launcher/Installed Build.'
}
$version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8 -or $version.PatchVersion -ne 1) {
    throw "Expected UE 5.8.1; found $($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)."
}
foreach ($requiredPath in @($patchManifest, $installedHelper, $repositoryHelper, $editorCommand)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file is missing: $requiredPath"
    }
}
if ((Get-FileSha256 $installedHelper) -ne (Get-FileSha256 $repositoryHelper)) {
    throw 'The installed E_avg helper differs from the repository payload. Re-apply or check the engine patch before benchmarking.'
}
if ($TrimStartFrames + $TrimEndFrames -ge $CaptureFrames) {
    throw 'TrimStartFrames + TrimEndFrames must be smaller than CaptureFrames.'
}

if ([string]::IsNullOrWhiteSpace($ResultDirectory)) {
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $ResultDirectory = Join-Path $repositoryRoot "BenchmarkResults\$timestamp"
}
$resolvedResultDirectory = [System.IO.Path]::GetFullPath($ResultDirectory)
New-Item -ItemType Directory -Path $resolvedResultDirectory -Force | Out-Null
$projectDirectory = Split-Path -Parent $resolvedProjectPath
$profilingDirectory = Join-Path $projectDirectory 'Saved\Profiling\CSV'
$originalBytes = [System.IO.File]::ReadAllBytes($installedHelper)

# Counterbalance run order to reduce thermal/order bias: F,R then R,F.
$runModes = [System.Collections.Generic.List[string]]::new()
if ($WarmupFrames -gt 0) {
    # Populate the shader/DDC/driver caches for both source variants before any
    # measured capture is retained.
    $runModes.Add('Fast')
    $runModes.Add('Reference')
}
for ($trial = 1; $trial -le $TrialsPerMode; $trial++) {
    if (($trial % 2) -eq 1) {
        $runModes.Add('Fast')
        $runModes.Add('Reference')
    }
    else {
        $runModes.Add('Reference')
        $runModes.Add('Fast')
    }
}

$initialModeCount = if ($WarmupFrames -gt 0) { -1 } else { 0 }
$modeCounts = @{ Fast = $initialModeCount; Reference = $initialModeCount }
try {
    foreach ($mode in $runModes) {
        $modeCounts[$mode]++
        $trialNumber = $modeCounts[$mode]
        $isWarmup = $trialNumber -eq 0
        $captureFramesForRun = if ($isWarmup) { $WarmupFrames } else { $CaptureFrames }
        $runLabel = if ($isWarmup) { 'Warmup' } else { "{0:D2}" -f $trialNumber }
        $captureName = "KullaConty_{0}_{1}_{2}" -f $mode, $runLabel, (Get-Date -Format 'yyyyMMddHHmmss')
        Set-EAvgMode -Path $installedHelper -OriginalBytes $originalBytes -Mode $mode

        $before = @{}
        if (Test-Path -LiteralPath $profilingDirectory) {
            Get-ChildItem -LiteralPath $profilingDirectory -Recurse -File -Filter '*.csv' | ForEach-Object {
                $before[$_.FullName] = $_.LastWriteTimeUtc.Ticks
            }
        }

        if ($isWarmup) {
            Write-Host "Prewarming $mode shader/DDC/driver caches ($captureFramesForRun frames)..."
        }
        else {
            Write-Host "Running $mode trial $trialNumber/$TrialsPerMode ($captureFramesForRun frames at ${ResolutionX}x${ResolutionY})..."
        }
        $execCommands = @(
            'r.VSync 0',
            't.MaxFPS 0',
            'r.DynamicRes.OperationMode 0',
            "CsvProfile STARTFILE=$captureName",
            'CsvProfile EXITONCOMPLETION',
            "CsvProfile FRAMES=$captureFramesForRun"
        ) -join ','
        $metadata = "KullaContyMode=$mode,KullaContyTrial=$trialNumber"

        & $editorCommand $resolvedProjectPath $Map `
            -game -unattended -nop4 -nosplash -nosound -RenderOffscreen `
            -benchmark -deterministic -novsync -ForceRes `
            "-ResX=$ResolutionX" "-ResY=$ResolutionY" `
            -csvGpuStats -csvCompression=0 "-csvMetadata=$metadata" `
            "-ExecCmds=$execCommands" -log
        if ($LASTEXITCODE -ne 0) {
            throw "$mode trial $trialNumber failed with UnrealEditor exit code $LASTEXITCODE."
        }

        $changed = @(Get-ChildItem -LiteralPath $profilingDirectory -Recurse -File -Filter '*.csv' -ErrorAction SilentlyContinue |
            Where-Object { -not $before.ContainsKey($_.FullName) -or $before[$_.FullName] -ne $_.LastWriteTimeUtc.Ticks } |
            Sort-Object LastWriteTimeUtc -Descending)
        if ($changed.Count -eq 0) {
            throw "UnrealEditor completed but did not produce a new CSV capture under $profilingDirectory"
        }
        if (-not $isWarmup) {
            $destination = Join-Path $resolvedResultDirectory "$captureName.csv"
            Copy-Item -LiteralPath $changed[0].FullName -Destination $destination
        }
    }
}
finally {
    [System.IO.File]::WriteAllBytes($installedHelper, $originalBytes)
    if ((Get-FileSha256 $installedHelper) -ne (Get-FileSha256 $repositoryHelper)) {
        Write-Warning 'The engine helper was restored, but its hash does not match the repository payload.'
    }
}

$summarizer = Join-Path $repositoryRoot 'Tools\SummarizeUECsvProfile.py'
& $python.Source @pythonArguments $summarizer $resolvedResultDirectory `
    --trim-start $TrimStartFrames --trim-end $TrimEndFrames `
    --output-directory $resolvedResultDirectory --require-pair
if ($LASTEXITCODE -ne 0) {
    throw "CSV summary failed with exit code $LASTEXITCODE."
}

Write-Host "Kulla-Conty Fast/Reference GPU benchmark completed: $resolvedResultDirectory"
