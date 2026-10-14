param(
    [string]$NativeDirectory = "../../out/build/sdk-dll-md-release/Release",
    [ValidateSet("build", "test", "run")]
    [string]$Action = "test",
    [ValidateSet("livekit-room", "livekit-features")]
    [string]$Example = "livekit-room",
    [string[]]$ExampleArguments = @()
)

$ErrorActionPreference = "Stop"
$nativePath = if ([IO.Path]::IsPathRooted($NativeDirectory)) {
    (Resolve-Path -LiteralPath $NativeDirectory).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot $NativeDirectory)).Path
}
foreach ($file in @("livekitclient.lib", "livekitclient.dll")) {
    if (!(Test-Path -LiteralPath (Join-Path $nativePath $file))) {
        throw "Missing $file in $nativePath. Build the shared native SDK first."
    }
}
$savedPath = $env:Path
try {
    $env:Path = "$nativePath;$savedPath"
    $arguments = @(
        $Action, "--package-path", $PSScriptRoot,
        "--scratch-path", (Join-Path $PSScriptRoot "../../out/build/swift-cpp-binding"),
        "-Xlinker", "-L", "-Xlinker", $nativePath
    )
    if ($Action -eq "run") { $arguments += $Example; $arguments += $ExampleArguments }
    # Windows PowerShell treats redirected native stderr progress as errors.
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & swift @arguments
        $swiftExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($swiftExitCode -ne 0) { throw "Swift $Action failed with exit code $swiftExitCode" }
} finally {
    $env:Path = $savedPath
}
