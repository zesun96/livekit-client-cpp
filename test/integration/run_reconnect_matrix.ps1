param(
  [Parameter(Mandatory = $true)]
  [string]$ServerExecutable,

  [Parameter(Mandatory = $true)]
  [string]$LkExecutable,

  [Parameter(Mandatory = $true)]
  [string]$ApiKey,

  [Parameter(Mandatory = $true)]
  [string]$ApiSecret,

  [string]$BuildDirectory = "out/build/vs2022-x64-release",
  [string]$Configuration = "Release",
  [string]$NodeIp = "",
  [string]$ExistingServerNodeIp = "",
  [int]$Port = 17880,
  [string]$ConfigPath = "",
  [string]$ExistingServerExecutable = "",
  [switch]$ReplaceExistingServer,
  [ValidateSet(
    "All", "Restart", "TokenRefresh", "Media", "E2EE", "DataTrack", "CAPI", "OfficialCpp"
  )]
  [string]$Scenario = "All",
  [ValidateSet("vp8", "h264", "vp9", "av1")]
  [string]$VideoCodec = "vp8",
  [string]$OfficialCppPeerExecutable = ""
)

$ErrorActionPreference = "Stop"
$processEnvironment = [Environment]::GetEnvironmentVariables("Process")
$canonicalPath = $processEnvironment.GetEnumerator() |
  Where-Object { $_.Key -ceq "Path" } |
  Select-Object -ExpandProperty Value -First 1
if (-not [string]::IsNullOrEmpty($canonicalPath)) {
  [Environment]::SetEnvironmentVariable("PATH", $null, "Process")
  [Environment]::SetEnvironmentVariable("Path", $canonicalPath, "Process")
}

function Resolve-RequiredPath([string]$Path, [string]$Description) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "$Description does not exist: $Path"
  }
  return (Resolve-Path -LiteralPath $Path).Path
}

function Wait-TcpPort([int]$TcpPort, [Diagnostics.Process]$Process, [int]$TimeoutSeconds = 15) {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    if ($Process.HasExited) {
      throw "LiveKit server exited before port $TcpPort became ready"
    }
    $client = [Net.Sockets.TcpClient]::new()
    try {
      $connect = $client.ConnectAsync("127.0.0.1", $TcpPort)
      if ($connect.Wait(200) -and $client.Connected) {
        return
      }
    } catch {
      # The server is still starting.
    } finally {
      $client.Dispose()
    }
    Start-Sleep -Milliseconds 100
  }
  throw "Timed out waiting for LiveKit port $TcpPort"
}

function Start-TestServer {
  $script:serverGeneration++
  $stdout = Join-Path $tempRoot "server-${script:serverGeneration}.out.log"
  $stderr = Join-Path $tempRoot "server-${script:serverGeneration}.err.log"
  $process = Start-Process -FilePath $serverPath `
    -ArgumentList @("--bind", "127.0.0.1", "--node-ip", $NodeIp, "--config", $configPath) `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -WindowStyle Hidden -PassThru
  try {
    Wait-TcpPort -TcpPort $Port -Process $process
    Start-Sleep -Milliseconds 500
    return $process
  } catch {
    Stop-OwnedProcess $process
    throw
  }
}

function Stop-OwnedProcess([Diagnostics.Process]$Process) {
  if ($null -ne $Process -and -not $Process.HasExited) {
    Stop-Process -Id $Process.Id
    $Process.WaitForExit(10000) | Out-Null
  }
}

function New-ParticipantToken(
  [string]$Room,
  [string]$Identity,
  [switch]$AllowUpdateMetadata
) {
  $previousErrorAction = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    $tokenArguments = @(
      "token", "create", "--join", "--room", $Room, "--identity", $Identity, "--valid-for", "15m"
    )
    if ($AllowUpdateMetadata) {
      $tokenArguments += "--allow-update-metadata"
    }
    $raw = & $lkPath @tokenArguments 2>&1
    $tokenExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }
  if ($tokenExitCode -ne 0) {
    throw "LiveKit token generation failed"
  }
  $token = [regex]::Match(($raw -join "`n"),
    'eyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+').Value
  if ([string]::IsNullOrEmpty($token)) {
    throw "LiveKit token generation failed"
  }
  return $token
}

function Invoke-CoordinatedTest(
  [string]$TestName,
  [string]$ReadyFile,
  [scriptblock]$FaultAction
) {
  Remove-Item -LiteralPath $ReadyFile -Force -ErrorAction SilentlyContinue
  $stdout = Join-Path $tempRoot "$TestName.out.log"
  $stderr = Join-Path $tempRoot "$TestName.err.log"
  $test = Start-Process -FilePath $testExecutable `
    -ArgumentList "--gtest_filter=LiveKitServerTest.$TestName" `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -WindowStyle Hidden -PassThru
  try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (-not (Test-Path -LiteralPath $ReadyFile)) {
      if ($test.HasExited) {
        $test.WaitForExit()
        $details = ((Get-Content -LiteralPath $stdout -Tail 80) +
          (Get-Content -LiteralPath $stderr -Tail 80)) -join "`n"
        throw "$TestName exited before reaching the coordination point:`n$details"
      }
      if ([DateTime]::UtcNow -ge $deadline) {
        throw "Timed out waiting for $TestName coordination point"
      }
      Start-Sleep -Milliseconds 50
    }

    & $FaultAction
    if (-not $test.WaitForExit(90000)) {
      Stop-OwnedProcess $test
      throw "$TestName timed out"
    }
    $test.WaitForExit()
    $test.Refresh()
    $passed = Select-String -LiteralPath $stdout -Pattern '^\[  PASSED  \] 1 test\.$' -Quiet
    if (-not $passed) {
      $assertions = Select-String -LiteralPath $stdout `
        -Pattern 'Failure$|Value of:|Expected:|Actual:|Expected equality|Which is:' `
        -Context 1, 3 |
        ForEach-Object { $_.ToString() }
      $details = ((Get-Content -LiteralPath $stdout -Tail 120) +
        (Get-Content -LiteralPath $stderr -Tail 120)) -join "`n"
      throw "$TestName failed:`n$($assertions -join "`n")`n$details"
    }
    Write-Host "PASS $TestName"
  } finally {
    Stop-OwnedProcess $test
  }
}

function Invoke-SimpleTest([string]$TestName) {
  $stdout = Join-Path $tempRoot "$TestName.out.log"
  $stderr = Join-Path $tempRoot "$TestName.err.log"
  $test = Start-Process -FilePath $testExecutable `
    -ArgumentList "--gtest_filter=LiveKitServerTest.$TestName" `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -WindowStyle Hidden -PassThru
  try {
    if (-not $test.WaitForExit(120000)) {
      Stop-OwnedProcess $test
      throw "$TestName timed out"
    }
    $test.WaitForExit()
    $test.Refresh()
    $passed = Select-String -LiteralPath $stdout -Pattern '^\[  PASSED  \] 1 test\.$' -Quiet
    if (-not $passed) {
      $assertions = Select-String -LiteralPath $stdout `
        -Pattern 'Failure$|Value of:|Expected:|Actual:|Expected equality|Which is:' `
        -Context 1, 3 |
        ForEach-Object { $_.ToString() }
      $details = ((Get-Content -LiteralPath $stdout -Tail 160) +
        (Get-Content -LiteralPath $stderr -Tail 160)) -join "`n"
      throw "$TestName failed:`n$($assertions -join "`n")`n$details"
    }
    Write-Host "PASS $TestName"
  } finally {
    Stop-OwnedProcess $test
  }
}

function Invoke-OfficialCppTest {
  $readyFile = Join-Path $tempRoot "official-cpp.ready"
  Remove-Item -LiteralPath $readyFile -Force -ErrorAction SilentlyContinue
  $stdout = Join-Path $tempRoot "official-cpp-peer.out.log"
  $stderr = Join-Path $tempRoot "official-cpp-peer.err.log"
  $peer = Start-Process -FilePath $officialCppPeerPath `
    -ArgumentList @(
      "--url", "ws://127.0.0.1:$Port",
      "--token", $env:LIVEKIT_TOKEN_2,
      "--sender-identity", "e2ee-sender",
      "--ready-file", $readyFile
    ) `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -WindowStyle Hidden -PassThru
  try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while (-not (Test-Path -LiteralPath $readyFile)) {
      if ($peer.HasExited) {
        $details = ((Get-Content -LiteralPath $stdout -Tail 100) +
          (Get-Content -LiteralPath $stderr -Tail 100)) -join "`n"
        throw "Official C++ peer exited before becoming ready:`n$details"
      }
      if ([DateTime]::UtcNow -ge $deadline) {
        throw "Timed out waiting for the official C++ peer"
      }
      Start-Sleep -Milliseconds 50
    }

    $env:LIVEKIT_OFFICIAL_CPP_PEER_IDENTITY = "e2ee-receiver"
    Invoke-SimpleTest "InteroperatesWithOfficialCppE2EEPeer"
    if (-not $peer.WaitForExit(15000)) {
      throw "Official C++ peer timed out after the interoperability test"
    }
    $peer.WaitForExit()
    $passed = Select-String -LiteralPath $stdout -Pattern '^PASS official C\+\+' -Quiet
    if (-not $passed) {
      $details = ((Get-Content -LiteralPath $stdout -Tail 120) +
        (Get-Content -LiteralPath $stderr -Tail 120)) -join "`n"
      throw "Official C++ peer failed:`n$details"
    }
    Write-Host "PASS official C++ E2EE peer"
  } finally {
    Stop-OwnedProcess $peer
  }
}

$serverPath = Resolve-RequiredPath $ServerExecutable "LiveKit server executable"
$expectedExistingServerPath = $serverPath
if (-not [string]::IsNullOrEmpty($ExistingServerExecutable)) {
  $expectedExistingServerPath = Resolve-RequiredPath `
    $ExistingServerExecutable "Existing LiveKit server executable"
}
$lkPath = Resolve-RequiredPath $LkExecutable "LiveKit CLI executable"
$buildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$testExecutable = Resolve-RequiredPath `
  (Join-Path $buildPath "test/integration/$Configuration/livekit_server_integration_tests.exe") `
  "Reconnect integration test executable"
$officialCppPeerPath = $null
if (-not [string]::IsNullOrEmpty($OfficialCppPeerExecutable)) {
  $officialCppPeerPath = Resolve-RequiredPath `
    $OfficialCppPeerExecutable "Official LiveKit C++ peer executable"
} elseif ($Scenario -eq "OfficialCpp") {
  throw "-OfficialCppPeerExecutable is required for -Scenario OfficialCpp"
}
if ([string]::IsNullOrEmpty($NodeIp)) {
  $defaultRoute = Get-NetRoute -AddressFamily IPv4 -DestinationPrefix "0.0.0.0/0" |
    Sort-Object RouteMetric, InterfaceMetric |
    Select-Object -First 1
  if ($null -eq $defaultRoute) {
    throw "Unable to determine the local IPv4 address; pass -NodeIp explicitly"
  }
  $NodeIp = Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $defaultRoute.InterfaceIndex |
    Where-Object { $_.IPAddress -notlike "169.254.*" } |
    Select-Object -ExpandProperty IPAddress -First 1
  if ([string]::IsNullOrEmpty($NodeIp)) {
    throw "Unable to determine the local IPv4 address; pass -NodeIp explicitly"
  }
}
if ([string]::IsNullOrEmpty($ExistingServerNodeIp)) {
  $ExistingServerNodeIp = $NodeIp
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("livekit-cpp-reconnect-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tempRoot | Out-Null
if ([string]::IsNullOrEmpty($ConfigPath)) {
  $ConfigPath = Join-Path $tempRoot "livekit.yaml"
  $safeKey = "'" + $ApiKey.Replace("'", "''") + "'"
  $safeSecret = "'" + $ApiSecret.Replace("'", "''") + "'"
  @(
    "port: $Port"
    "enable_participant_data_blob: true"
    "rtc:"
    "  tcp_port: $($Port + 1)"
    "  udp_port: $($Port + 2)"
    "  use_external_ip: false"
    "keys:"
    "  ${safeKey}: ${safeSecret}"
    "logging:"
    "  level: warn"
  ) | Set-Content -LiteralPath $ConfigPath -Encoding utf8
} else {
  $ConfigPath = Resolve-RequiredPath $ConfigPath "LiveKit config"
}
$configPath = $ConfigPath

$server = $null
$serverGeneration = 0
$restoreExistingServer = $false
$originalServerPath = $null
$originalEnvironment = @{
  LIVEKIT_URL = $env:LIVEKIT_URL
  LIVEKIT_API_KEY = $env:LIVEKIT_API_KEY
  LIVEKIT_API_SECRET = $env:LIVEKIT_API_SECRET
  LIVEKIT_TOKEN_RESTART = $env:LIVEKIT_TOKEN_RESTART
  LIVEKIT_TOKEN_REFRESH = $env:LIVEKIT_TOKEN_REFRESH
  LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN
  LIVEKIT_TOKEN_2 = $env:LIVEKIT_TOKEN_2
  LIVEKIT_VIDEO_CODEC = $env:LIVEKIT_VIDEO_CODEC
  LIVEKIT_OFFICIAL_CPP_PEER_IDENTITY = $env:LIVEKIT_OFFICIAL_CPP_PEER_IDENTITY
  LIVEKIT_SERVER_RESTART_READY_FILE = $env:LIVEKIT_SERVER_RESTART_READY_FILE
  LIVEKIT_TOKEN_REFRESH_READY_FILE = $env:LIVEKIT_TOKEN_REFRESH_READY_FILE
}

try {
  if ($ReplaceExistingServer) {
    $listenerPattern = "^\s*TCP\s+\S+:${Port}\s+\S+\s+LISTENING\s+(\d+)\s*$"
    $listenerMatch = netstat -ano -p TCP |
      Select-String -Pattern $listenerPattern |
      Select-Object -First 1
    if ($null -eq $listenerMatch) {
      throw "No existing LiveKit listener found on port $Port"
    }
    $listenerPid = [int]$listenerMatch.Matches[0].Groups[1].Value
    $existingServer = Get-Process -Id $listenerPid -ErrorAction Stop
    $existingPath = (Resolve-Path -LiteralPath $existingServer.Path).Path
    if (-not $existingPath.Equals(
        $expectedExistingServerPath, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Port $Port is owned by an unexpected executable: $existingPath"
    }
    $originalServerPath = $existingPath
    Stop-OwnedProcess $existingServer
    $restoreExistingServer = $true
  }
  $env:LIVEKIT_URL = "http://127.0.0.1:$Port/rtc"
  $env:LIVEKIT_API_KEY = $ApiKey
  $env:LIVEKIT_API_SECRET = $ApiSecret
  $room = "cpp-reconnect-matrix-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $restartIdentity = "restart-client"
  $refreshIdentity = "refresh-client"
  $env:LIVEKIT_TOKEN_RESTART = New-ParticipantToken $room $restartIdentity
  $env:LIVEKIT_TOKEN_REFRESH = New-ParticipantToken $room $refreshIdentity
  $e2eeRoom = "cpp-e2ee-matrix-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $env:LIVEKIT_TOKEN = New-ParticipantToken $e2eeRoom "e2ee-sender" -AllowUpdateMetadata
  $env:LIVEKIT_TOKEN_2 = New-ParticipantToken $e2eeRoom "e2ee-receiver"
  $env:LIVEKIT_VIDEO_CODEC = $VideoCodec
  $env:LIVEKIT_SERVER_RESTART_READY_FILE = Join-Path $tempRoot "restart.ready"
  $env:LIVEKIT_TOKEN_REFRESH_READY_FILE = Join-Path $tempRoot "refresh.ready"

  $server = Start-TestServer
  if ($Scenario -in @("All", "Restart")) {
    Invoke-CoordinatedTest "RecoversAfterExplicitServerRestart" `
      $env:LIVEKIT_SERVER_RESTART_READY_FILE {
        Stop-OwnedProcess $server
        Start-Sleep -Seconds 2
        $script:server = Start-TestServer
      }
  }

  if ($Scenario -in @("All", "TokenRefresh")) {
    Invoke-CoordinatedTest "UsesRefreshedTokenForResumeAndFullReconnect" `
      $env:LIVEKIT_TOKEN_REFRESH_READY_FILE {
        # JoinResponse supplies a server-refreshed token. Reaching this point
        # confirms that the client retained it before recovery is triggered.
        Start-Sleep -Milliseconds 100
      }
  }

  if ($Scenario -in @("All", "Media")) {
    Invoke-SimpleTest "PublishesAndReceivesSelectedVideoCodec"
    Invoke-SimpleTest "PublishesBackupCodecWhenRequestedByServer"
    Invoke-SimpleTest "PublishesAndReceivesAudioAndVideo"
  }

  if ($Scenario -in @("All", "E2EE")) {
    Invoke-SimpleTest "CApiEncryptsAudioAndDataAndControlsKeys"
    Invoke-SimpleTest "EncryptsAudioVideoAndDataEndToEnd"
    Invoke-SimpleTest "PreservesE2EEAfterPublisherAndSubscriberReconnect"
    Invoke-SimpleTest "ReportsAndRecoversFromE2EEKeyErrors"
  }

  if ($Scenario -in @("All", "DataTrack")) {
    Invoke-SimpleTest "PublishesReadsAndUnpublishesEncryptedDataTrack"
  }

  if ($Scenario -in @("All", "DataTrack", "CAPI")) {
    Invoke-SimpleTest "CApiPublishesReadsAndUnpublishesDataTrack"
  }

  if ($Scenario -in @("All", "CAPI")) {
    Invoke-SimpleTest "CApiReportsParticipantProfileChanges"
    Invoke-SimpleTest "CApiPreservesDataStreamMetadata"
  }

  if ($Scenario -eq "OfficialCpp" -or
      ($Scenario -eq "All" -and $null -ne $officialCppPeerPath)) {
    Invoke-OfficialCppTest
  }
} finally {
  Stop-OwnedProcess $server
  if ($restoreExistingServer) {
    $restoredServer = Start-Process -FilePath $originalServerPath `
      -ArgumentList @(
        "--bind", "127.0.0.1", "--node-ip", $ExistingServerNodeIp, "--config", $configPath
      ) `
      -WindowStyle Hidden -PassThru
    Wait-TcpPort -TcpPort $Port -Process $restoredServer
  }
  foreach ($entry in $originalEnvironment.GetEnumerator()) {
    [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
  }
  if (Test-Path -LiteralPath $tempRoot) {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force
  }
}
