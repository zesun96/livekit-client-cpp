param(
  [string]$NodeIncludeDir = $env:NODE_INCLUDE_DIR,
  [string]$NodeLibrary = $env:NODE_LIBRARY,
  [string]$SdkLibrary = $env:LIVEKIT_CLIENT_CPP_LIBRARY,
  [string]$SdkRuntimeDir = $env:LIVEKIT_CLIENT_CPP_RUNTIME_DIR,
  [switch]$Testing
)
$ErrorActionPreference = 'Stop'
if (!$NodeIncludeDir -or !$NodeLibrary -or !$SdkLibrary -or !$SdkRuntimeDir) {
  throw 'Supply -NodeIncludeDir, -NodeLibrary, -SdkLibrary and -SdkRuntimeDir (or their documented environment variables).'
}
$bindingDir = Split-Path $PSScriptRoot -Parent
$sdkDir = Split-Path (Split-Path $bindingDir -Parent) -Parent
$buildName = if ($Testing) { 'node-binding-testing' } else { 'node-binding' }
$buildDir = Join-Path $sdkDir "out/build/$buildName"
$testFlag = if ($Testing) { 'ON' } else { 'OFF' }
$normalizedPath = $env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $normalizedPath
cmake -S $bindingDir -B $buildDir -G 'Visual Studio 17 2022' -A x64 "-DNODE_INCLUDE_DIR=$NodeIncludeDir" "-DNODE_LIBRARY=$NodeLibrary" "-DLIVEKIT_CLIENT_CPP_LIBRARY=$SdkLibrary" "-DLIVEKIT_NODE_TESTING=$testFlag"
if ($LASTEXITCODE) { throw 'Node addon configure failed' }
cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE) { throw 'Node addon build failed' }
$destination = if ($Testing) { Join-Path $buildDir 'Release' } else { Join-Path $bindingDir 'native' }
if (!$Testing) {
  Copy-Item -LiteralPath (Join-Path $buildDir 'Release/livekitclient_node.node') -Destination $destination
}
# Deploy only DLLs from the explicitly supplied SDK runtime directory.
Get-ChildItem -LiteralPath $SdkRuntimeDir -Filter '*.dll' -File | Copy-Item -Destination $destination
