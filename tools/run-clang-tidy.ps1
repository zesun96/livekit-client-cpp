[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDirectory,
  [int]$Jobs = 4,
  [string]$FileRegex = ".*livekit-client-cpp[\\/](src|test[\\/]unit[\\/]core_utils)[\\/].*",
  [string]$ClangTidy = ""
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDirectory = (Resolve-Path $BuildDirectory).Path
$compileDatabase = Join-Path $resolvedBuildDirectory "compile_commands.json"
if (-not (Test-Path $compileDatabase -PathType Leaf)) {
  throw "compile_commands.json was not found in $resolvedBuildDirectory"
}

if (-not $ClangTidy) {
  $ClangTidy = (Get-Command clang-tidy -ErrorAction Stop).Source
}
$runner = (Get-Command run-clang-tidy -ErrorAction Stop).Source
$python = (Get-Command python -ErrorAction Stop).Source
$configuration = Join-Path $repositoryRoot ".clang-tidy"

& $python $runner `
  -p $resolvedBuildDirectory `
  -clang-tidy-binary $ClangTidy `
  -config-file $configuration `
  -j $Jobs `
  -quiet `
  $FileRegex
if ($LASTEXITCODE -ne 0) {
  throw "clang-tidy failed with exit code $LASTEXITCODE"
}
