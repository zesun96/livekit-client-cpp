param(
  [Parameter(Mandatory = $true)]
  [string]$Header,

  [Parameter(Mandatory = $true)]
  [string]$Dll
)

$ErrorActionPreference = "Stop"

$headerPath = (Resolve-Path -LiteralPath $Header).Path
$dllPath = (Resolve-Path -LiteralPath $Dll).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
  throw "vswhere.exe was not found"
}
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
  throw "A Visual Studio installation with the x64 C++ tools was not found"
}
$link = Get-ChildItem -LiteralPath (Join-Path $visualStudio "VC\Tools\MSVC") -Directory |
  Sort-Object Name -Descending |
  ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\link.exe" } |
  Where-Object { Test-Path -LiteralPath $_ } |
  Select-Object -First 1
if (-not $link) {
  throw "link.exe was not found"
}

$declarations = [regex]::Matches(
  (Get-Content -LiteralPath $headerPath -Raw),
  'LKC_API\s+[\s\S]*?\b(lk_[a-zA-Z0-9_]+)\s*\('
) | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$exports = & $link /dump /exports $dllPath |
  Select-String '\b(lk_[a-zA-Z0-9_]+)(?:\s|$)' |
  ForEach-Object { $_.Matches[0].Groups[1].Value } |
  Sort-Object -Unique

$differences = @(Compare-Object $declarations $exports)
$missing = $differences |
  Where-Object SideIndicator -eq '<=' |
  Select-Object -ExpandProperty InputObject
$unexpected = $differences |
  Where-Object SideIndicator -eq '=>' |
  Select-Object -ExpandProperty InputObject
if ($missing -or $unexpected) {
  if ($missing) {
    Write-Error "Missing C ABI exports: $($missing -join ', ')"
  }
  if ($unexpected) {
    Write-Error "Unexpected C ABI exports: $($unexpected -join ', ')"
  }
  exit 1
}

Write-Output "Verified $($declarations.Count) C ABI exports in $dllPath"
