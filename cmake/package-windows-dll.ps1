param(
  [Parameter(Mandatory = $true)]
  [string]$ReleaseBuildDirectory,

  [Parameter(Mandatory = $true)]
  [string]$DebugBuildDirectory,

  [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"

function Get-CacheValue {
  param(
    [Parameter(Mandatory = $true)]
    [string]$CachePath,

    [Parameter(Mandatory = $true)]
    [string]$Name
  )

  $match = Select-String -LiteralPath $CachePath -Pattern "^$([regex]::Escape($Name)):[^=]+=(.*)$" |
    Select-Object -First 1
  if (-not $match) {
    throw "$Name was not found in $CachePath"
  }
  return $match.Matches[0].Groups[1].Value
}

function Invoke-CMake {
  param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

  & cmake @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "cmake exited with code $LASTEXITCODE"
  }
}

$releaseBuild = (Resolve-Path -LiteralPath $ReleaseBuildDirectory).Path
$debugBuild = (Resolve-Path -LiteralPath $DebugBuildDirectory).Path
$releaseCache = Join-Path $releaseBuild "CMakeCache.txt"
$debugCache = Join-Path $debugBuild "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $releaseCache) -or
    -not (Test-Path -LiteralPath $debugCache)) {
  throw "Both build directories must contain CMakeCache.txt"
}

foreach ($cache in @($releaseCache, $debugCache)) {
  if ((Get-CacheValue -CachePath $cache -Name "BUILD_SHARED_LIBS") -ne "ON") {
    throw "$cache must be configured with BUILD_SHARED_LIBS=ON"
  }
}

$version = Get-CacheValue -CachePath $releaseCache -Name "CMAKE_PROJECT_VERSION"
if ((Get-CacheValue -CachePath $debugCache -Name "CMAKE_PROJECT_VERSION") -ne $version) {
  throw "Release and Debug build directories use different SDK versions"
}
$architecture = Get-CacheValue -CachePath $releaseCache -Name "CMAKE_GENERATOR_PLATFORM"
if (-not $architecture) {
  $architecture = "x64"
}
if ((Get-CacheValue -CachePath $debugCache -Name "CMAKE_GENERATOR_PLATFORM") -ne $architecture) {
  throw "Release and Debug build directories use different architectures"
}

if ($OutputDirectory) {
  $output = [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
  $output = Join-Path (Split-Path -Parent $releaseBuild) "packages"
}
$packageName = "livekit-client-cpp-$version-Windows-$architecture-dll"
$stagingRoot = Join-Path $output "_staging"
$packageRoot = Join-Path $stagingRoot $packageName
$archive = Join-Path $output "$packageName.zip"

New-Item -ItemType Directory -Force -Path $output | Out-Null
$resolvedOutput = (Resolve-Path -LiteralPath $output).Path
if ($packageRoot -notlike "$resolvedOutput\*") {
  throw "Refusing to clean a staging directory outside $resolvedOutput"
}
if (Test-Path -LiteralPath $packageRoot) {
  Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $archive) {
  Remove-Item -LiteralPath $archive -Force
}

Invoke-CMake --build $releaseBuild --config Release --target livekitclient --parallel
Invoke-CMake --build $debugBuild --config Debug --target livekitclient --parallel
Invoke-CMake --install $releaseBuild --config Release --prefix $packageRoot
Invoke-CMake --install $debugBuild --config Debug --prefix $packageRoot

$required = @(
  "bin/Release/livekitclient.dll",
  "bin/Release/websockets.dll",
  "bin/Debug/livekitclientd.dll",
  "bin/Debug/livekitclientd.pdb",
  "bin/Debug/websockets.dll",
  "lib/Release/livekitclient.lib",
  "lib/Debug/livekitclientd.lib",
  "lib/cmake/LiveKitClient/LiveKitClientConfig.cmake",
  "lib/cmake/LiveKitClient/LiveKitClientTargets-release.cmake",
  "lib/cmake/LiveKitClient/LiveKitClientTargets-debug.cmake"
)
foreach ($relativePath in $required) {
  $path = Join-Path $packageRoot $relativePath
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Installed package is missing $relativePath"
  }
}

Push-Location $stagingRoot
try {
  Invoke-CMake -Arguments @(
    "-E", "tar", "cf", $archive, "--format=zip", "--", $packageName
  )
} finally {
  Pop-Location
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $archive
Write-Output "Package: $archive"
Write-Output "SHA256: $($hash.Hash)"
