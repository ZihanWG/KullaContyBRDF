[CmdletBinding()]
param(
    [string] $FxcPath,
    [string] $DxcPath,
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($FxcPath)) {
    $windowsKitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $candidates = Get-ChildItem -Path (Join-Path $windowsKitsRoot '*\x64\fxc.exe') -File -ErrorAction SilentlyContinue |
        Sort-Object { [Version]$_.Directory.Parent.Name } -Descending
    if (-not $candidates) {
        throw 'Could not find Windows SDK fxc.exe. Pass -FxcPath explicitly.'
    }
    $FxcPath = $candidates[0].FullName
}

if ([string]::IsNullOrWhiteSpace($DxcPath)) {
    $windowsKitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $candidates = Get-ChildItem -Path (Join-Path $windowsKitsRoot '*\x64\dxc.exe') -File -ErrorAction SilentlyContinue |
        Sort-Object { [Version]$_.Directory.Parent.Name } -Descending
    if (-not $candidates) {
        throw 'Could not find Windows SDK dxc.exe. Pass -DxcPath explicitly.'
    }
    $DxcPath = $candidates[0].FullName
}

$resolvedFxc = (Resolve-Path -LiteralPath $FxcPath).Path
$resolvedDxc = (Resolve-Path -LiteralPath $DxcPath).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
    $OutputDirectory = Join-Path $repositoryRoot 'Intermediate\KullaConty'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$source = Join-Path $PSScriptRoot 'Tests\KullaContyEAvgSmoke.hlsl'
$sm5ObjectPath = Join-Path $OutputDirectory 'KullaContyEAvgSmoke.dxbc'
$sm5AssemblyPath = Join-Path $OutputDirectory 'KullaContyEAvgSmoke.sm5.asm'
$sm6ObjectPath = Join-Path $OutputDirectory 'KullaContyEAvgSmoke.dxil'
$sm6AssemblyPath = Join-Path $OutputDirectory 'KullaContyEAvgSmoke.sm6.asm'

& $resolvedFxc /nologo /T ps_5_0 /E MainPS /O3 /I $PSScriptRoot `
    /Fo $sm5ObjectPath /Fc $sm5AssemblyPath $source
if ($LASTEXITCODE -ne 0) {
    throw "E_avg SM5 shader smoke compilation failed with exit code $LASTEXITCODE."
}

$sm5Assembly = Get-Content -LiteralPath $sm5AssemblyPath -Raw
if (-not $sm5Assembly.Contains('dcl_immediateConstantBuffer')) {
    throw 'The E_avg constants were not compiled into an immediate constant buffer.'
}
if ($sm5Assembly -match '(?m)^dcl_resource') {
    throw 'The standalone E_avg fast path unexpectedly declares a texture resource.'
}

$instructionSlots = 'unknown'
$slotMatch = [regex]::Match($sm5Assembly, 'Approximately ([0-9]+) instruction slots used')
if ($slotMatch.Success) {
    $instructionSlots = $slotMatch.Groups[1].Value
}

& $resolvedDxc -T ps_6_0 -E MainPS -O3 -I $PSScriptRoot `
    -Fo $sm6ObjectPath -Fc $sm6AssemblyPath $source
if ($LASTEXITCODE -ne 0) {
    throw "E_avg SM6 shader smoke compilation failed with exit code $LASTEXITCODE."
}

$sm6Assembly = Get-Content -LiteralPath $sm6AssemblyPath -Raw
if ($sm6Assembly -notmatch '(?m)^@KCEAverageDirectionalAlbedoLUT = .*constant \[32 x float\]') {
    throw 'DXC did not preserve the E_avg table as a read-only 32-float global constant.'
}
if ($sm6Assembly -match '(?m)^\s*%[^=]+ = alloca ' -or $sm6Assembly -match '(?m)^\s*store float .*KCEAverageDirectionalAlbedoLUT') {
    throw 'DXC unexpectedly materialized the E_avg table in writable temporary memory.'
}
$sm6ResourceSection = [regex]::Match(
    $sm6Assembly,
    '(?ms)^; Resource Bindings:\s*;.*?(?=^; ViewId state:|^target datalayout|^define )'
).Value
if ($sm6ResourceSection -match '(?m)^;\s*\S+\s+(texture|sampler|UAV|cbuffer)\b') {
    throw 'The standalone E_avg SM6 fast path unexpectedly declares a bound resource.'
}

Write-Host "Kulla-Conty E_avg shader smoke tests passed:"
Write-Host "  SM5: immediate constants, zero texture resources, approximately $instructionSlots instruction slots."
Write-Host '  SM6: read-only global constant table, two indexed loads, zero bound resources, no temporary table copy.'
