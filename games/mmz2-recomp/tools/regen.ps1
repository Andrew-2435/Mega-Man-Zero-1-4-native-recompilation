param(
    [string]$Rom = (Join-Path $PSScriptRoot '..\roms\megaman_zero_2_usa.gba'),
    [string]$GbarecompRoot = (Join-Path $PSScriptRoot '..\..\gbarecomp'),
    [int]$MaxFunctions = 65536,
    [string]$Recompiler = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$romPath = (Resolve-Path $Rom).Path
$engine = (Resolve-Path $GbarecompRoot).Path
$tool = if ($Recompiler) { (Resolve-Path -LiteralPath $Recompiler).Path } else { Join-Path $engine 'build\gba_recompile.exe' }
if (-not (Test-Path -LiteralPath $tool)) {
    throw "Missing $tool. Configure and build the sibling gbarecomp checkout first."
}

$actual = (Get-FileHash -LiteralPath $romPath -Algorithm SHA1).Hash.ToLowerInvariant()
$expected = 'c4d93a58f0f82c526dec8a3fdbda170336303689'
if ($actual -ne $expected) {
    throw "Mega Man Zero 2 ROM SHA-1 mismatch: got $actual expected $expected"
}

& $tool --rom $romPath --config (Join-Path $root 'game.toml') `
    --out (Join-Path $root 'generated') --max-functions $MaxFunctions
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

