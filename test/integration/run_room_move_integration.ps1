param(
  [Parameter(Mandatory = $true)]
  [string]$ServerUrl,

  [Parameter(Mandatory = $true)]
  [string]$LkExecutable,

  [Parameter(Mandatory = $true)]
  [string]$ApiKey,

  [Parameter(Mandatory = $true)]
  [string]$ApiSecret,

  [Parameter(Mandatory = $true)]
  [string]$SourceToken,

  [Parameter(Mandatory = $true)]
  [string]$DestinationToken,

  [Parameter(Mandatory = $true)]
  [string]$SourceRoom,

  [Parameter(Mandatory = $true)]
  [string]$DestinationRoom,

  [string]$Identity = "room-move-client",
  [string]$BuildDirectory = "out/build/vs2022-x64-release",
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
# The hosting tool can provide both PATH and Path. ProcessStartInfo uses a case-insensitive
# environment dictionary on Windows, so normalize the duplicate for this process and its children.
$normalizedPath = $env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $normalizedPath
$lkPath = (Resolve-Path -LiteralPath $LkExecutable).Path
$buildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$testExecutable = Join-Path $buildPath `
  "test/integration/$Configuration/livekit_server_integration_tests.exe"
if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
  throw "Integration test executable not found: $testExecutable"
}

$serviceUrl = $ServerUrl.TrimEnd('/')
$rtcUrl = if ($serviceUrl.EndsWith('/rtc', [StringComparison]::OrdinalIgnoreCase)) {
  $serviceUrl
} else {
  "$serviceUrl/rtc"
}
$apiUrl = if ($serviceUrl.EndsWith('/rtc', [StringComparison]::OrdinalIgnoreCase)) {
  $serviceUrl.Substring(0, $serviceUrl.Length - 4)
} else {
  $serviceUrl
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("livekit-cpp-room-move-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$readyFile = Join-Path $tempRoot "room-move.ready"
$test = $null
$stdoutTask = $null
$stderrTask = $null
$capturedStdout = ""
$capturedStderr = ""
$sourceParticipantsAfterMove = @()
$destinationParticipantsAfterMove = @()
$environmentNames = @(
  "LIVEKIT_URL",
  "LIVEKIT_TOKEN_ROOM_MOVE_SOURCE",
  "LIVEKIT_TOKEN_ROOM_MOVE_DESTINATION",
  "LIVEKIT_ROOM_MOVE_SOURCE",
  "LIVEKIT_ROOM_MOVE_DESTINATION",
  "LIVEKIT_ROOM_MOVE_READY_FILE",
  "LIVEKIT_API_KEY",
  "LIVEKIT_API_SECRET"
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
  $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

try {
  $env:LIVEKIT_URL = $rtcUrl
  $env:LIVEKIT_TOKEN_ROOM_MOVE_SOURCE = $SourceToken
  $env:LIVEKIT_TOKEN_ROOM_MOVE_DESTINATION = $DestinationToken
  $env:LIVEKIT_ROOM_MOVE_SOURCE = $SourceRoom
  $env:LIVEKIT_ROOM_MOVE_DESTINATION = $DestinationRoom
  $env:LIVEKIT_ROOM_MOVE_READY_FILE = $readyFile

  # Start-Process -PassThru can expose a null ExitCode in the Windows PowerShell host even after
  # WaitForExit succeeds. Use Process directly so a passing test cannot be reported as failed.
  $startInfo = [Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $testExecutable
  $startInfo.Arguments = `
    "--gtest_filter=LiveKitServerTest.MovesRoomAndRefreshesDynamicCredentials"
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $test = [Diagnostics.Process]::new()
  $test.StartInfo = $startInfo
  if (-not $test.Start()) {
    throw "Failed to start the room-move integration test"
  }
  $stdoutTask = $test.StandardOutput.ReadToEndAsync()
  $stderrTask = $test.StandardError.ReadToEndAsync()
  $deadline = [DateTime]::UtcNow.AddSeconds(30)
  while (-not (Test-Path -LiteralPath $readyFile -PathType Leaf)) {
    if ($test.HasExited) {
      throw "Room-move test exited before reaching the coordination point"
    }
    if ([DateTime]::UtcNow -ge $deadline) {
      throw "Timed out waiting for the room-move coordination point"
    }
    Start-Sleep -Milliseconds 50
  }

  $previousErrorAction = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    # Keep credentials out of the child process command line. The lk CLI reads these values from
    # its inherited process environment, which is restored in the outer finally block.
    $env:LIVEKIT_URL = $apiUrl
    $env:LIVEKIT_API_KEY = $ApiKey
    $env:LIVEKIT_API_SECRET = $ApiSecret
    $moveOutput = @(& $lkPath --yes room participants move --room $SourceRoom `
        --identity $Identity --destination-room $DestinationRoom 2>&1)
    $moveExitCode = $LASTEXITCODE
    if ($moveExitCode -eq 0) {
      Start-Sleep -Milliseconds 500
      $sourceParticipantsAfterMove = @(& $lkPath --quiet room participants list $SourceRoom 2>&1)
      $destinationParticipantsAfterMove = @(
        & $lkPath --quiet room participants list $DestinationRoom 2>&1
      )
    }
    $env:LIVEKIT_URL = $rtcUrl
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }
  if ($moveExitCode -ne 0) {
    $moveError = ($moveOutput | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    throw "LiveKit participant move command failed: $moveError"
  }
  if (-not $test.WaitForExit(90000)) {
    Stop-Process -Id $test.Id -ErrorAction SilentlyContinue
    throw "Room-move integration test timed out"
  }
  $test.WaitForExit()
  $capturedStdout = $stdoutTask.GetAwaiter().GetResult()
  $capturedStderr = $stderrTask.GetAwaiter().GetResult()
  $testExitCode = $test.ExitCode
  if ($testExitCode -ne 0) {
    Write-Host "Source-room participants after move:"
    $sourceParticipantsAfterMove | ForEach-Object { Write-Host $_.ToString() }
    Write-Host "Destination-room participants after move:"
    $destinationParticipantsAfterMove | ForEach-Object { Write-Host $_.ToString() }
    throw "Room-move integration test failed with exit code $testExitCode"
  }
  Write-Host "PASS room move, server token refresh, and destination-room TokenSource reconnect"
} catch {
  if ($null -ne $test -and -not $test.HasExited) {
    Stop-Process -Id $test.Id -ErrorAction SilentlyContinue
    $test.WaitForExit()
  }
  if ($null -ne $stdoutTask -and [string]::IsNullOrEmpty($capturedStdout)) {
    $capturedStdout = $stdoutTask.GetAwaiter().GetResult()
  }
  if ($null -ne $stderrTask -and [string]::IsNullOrEmpty($capturedStderr)) {
    $capturedStderr = $stderrTask.GetAwaiter().GetResult()
  }
  if (-not [string]::IsNullOrEmpty($capturedStdout)) {
    $capturedStdout -split "`r?`n" | Select-Object -Last 120 | Write-Host
  }
  if (-not [string]::IsNullOrEmpty($capturedStderr)) {
    $capturedStderr -split "`r?`n" | Select-Object -Last 120 | Write-Host
  }
  throw
} finally {
  if ($null -ne $test -and -not $test.HasExited) {
    Stop-Process -Id $test.Id -ErrorAction SilentlyContinue
  }
  if ($null -ne $test) {
    $test.Dispose()
  }
  foreach ($name in $environmentNames) {
    [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
  }
  if (Test-Path -LiteralPath $tempRoot -PathType Container) {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force
  }
}
