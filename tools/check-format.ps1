[CmdletBinding()]
param(
  [string]$ClangFormat = ""
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot

if (-not $ClangFormat) {
  $formatCommand = Get-Command clang-format -ErrorAction Stop
  $ClangFormat = $formatCommand.Source
}

Push-Location $repositoryRoot
try {
  $sourceFiles = @(git ls-files --cached --others --exclude-standard -- `
    "*.h" "*.hpp" "*.c" "*.cc" "*.cpp")
  if ($LASTEXITCODE -ne 0) {
    throw "git ls-files failed with exit code $LASTEXITCODE"
  }

  $failedFiles = [System.Collections.Generic.List[string]]::new()
  foreach ($sourceFile in $sourceFiles) {
    & $ClangFormat --dry-run --Werror --style=file -- $sourceFile
    if ($LASTEXITCODE -ne 0) {
      $failedFiles.Add($sourceFile)
    }
  }

  if ($failedFiles.Count -ne 0) {
    Write-Error ("clang-format rejected {0} file(s):`n{1}" -f
      $failedFiles.Count, ($failedFiles -join "`n"))
  }

  Write-Host "clang-format accepted $($sourceFiles.Count) versioned C/C++ files."
} finally {
  Pop-Location
}
