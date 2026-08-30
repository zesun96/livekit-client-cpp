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
  [string]$ClumsyExecutable = "",
  [switch]$ReplaceExistingServer,
  [ValidateSet(
    "All", "Participants", "Restart", "TokenRefresh", "Media", "E2EE", "DataTrack", "CAPI",
    "OfficialCpp", "WeakNetwork", "DataRecovery", "CodecMatrix", "Soak", "AudioQuality",
    "FrameMetadata"
  )]
  [string]$Scenario = "All",
  [ValidateRange(1, 100)]
  [int]$Iterations = 1,
  [ValidateSet("vp8", "h264", "vp9", "av1")]
  [string]$VideoCodec = "vp8",
  [ValidateSet("All", "Loss", "Latency", "Jitter", "Outage")]
  [string]$WeakNetworkProfile = "All",
  [ValidateRange(5, 7200)]
  [int]$CodecSoakSeconds = 30,
  [ValidateRange(30, 7200)]
  [int]$SoakSeconds = 1800,
  [ValidateRange(5, 60)]
  [int]$AudioQualitySeconds = 10,
  [ValidateRange(-20.0, 60.0)]
  [double]$AudioQualityMinErleDb = 6.0,
  [ValidateRange(0.0, 1.0)]
  [double]$AudioQualityMaxResidualEcho = 0.75,
  [ValidateRange(0.01, 1.0)]
  [double]$AudioQualitySpeakerVolume = 0.50,
  [ValidateRange(0.01, 1.0)]
  [double]$AudioFixtureVolume = 0.80,
  [ValidateRange(0.01, 1.0)]
  [double]$AudioDoubleTalkFarEndVolume = 0.20,
  [ValidateRange(0.01, 1.0)]
  [double]$AudioNoiseFixtureVolume = 1.0,
  [ValidateRange(0.0, 2.0)]
  [double]$AudioDoubleTalkMinRetention = 0.60,
  [ValidateRange(0.0, 2.0)]
  [double]$AudioNoiseMaxRatio = 0.75,
  [ValidateRange(3, 15)]
  [int]$AudioHardwarePhaseSeconds = 5,
  [string]$AudioQualityInputDeviceId = "",
  [string]$AudioQualityOutputDeviceId = "",
  [ValidateRange(0, 1024)]
  [int]$MaxHandleGrowth = 64,
  [ValidateRange(0, 256)]
  [int]$MaxThreadGrowth = 16,
  [ValidateRange(0, 4096)]
  [int]$MaxPrivateMemoryGrowthMb = 256,
  [string]$OfficialCppPeerExecutable = "",
  [string]$ResultLogPath = ""
)

$ErrorActionPreference = "Stop"
$script:integrationTestStarted = $false
$resultLogPathResolved = $null
if (-not [string]::IsNullOrEmpty($ResultLogPath)) {
  $resultLogPathResolved = [IO.Path]::GetFullPath($ResultLogPath)
  Set-Content -LiteralPath $resultLogPathResolved -Value "LiveKit integration harness" -Encoding utf8
}

function Write-HarnessResult([string]$Message) {
  Write-Host $Message
  if ($null -ne $resultLogPathResolved) {
    Add-Content -LiteralPath $resultLogPathResolved -Value $Message -Encoding utf8
  }
}

function Get-StructuredDiagnostics(
  [string]$Label,
  [string]$StdoutPath,
  [string]$StderrPath,
  [int]$TailLines = 160
) {
  $sections = @()
  foreach ($entry in @(
      @{ Name = "gtest"; Path = $StdoutPath },
      @{ Name = "sdk-transport"; Path = $StderrPath }
    )) {
    $sections += "--- BEGIN $Label $($entry.Name) ---"
    if (Test-Path -LiteralPath $entry.Path -PathType Leaf) {
      $content = @(Get-Content -LiteralPath $entry.Path -Tail $TailLines)
      if ($content.Count -eq 0) {
        $sections += "<empty>"
      } else {
        $sections += $content
      }
    } else {
      $sections += "<missing>"
    }
    $sections += "--- END $Label $($entry.Name) ---"
  }
  return $sections -join "`n"
}

function Get-HarnessFailureDiagnostics([string]$Directory) {
  if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
    return ""
  }
  $logs = @(Get-ChildItem -LiteralPath $Directory -Filter "*.log" -File |
      Sort-Object LastWriteTimeUtc -Descending |
      Select-Object -First 8)
  if ($logs.Count -eq 0) {
    return ""
  }
  $sections = @("=== BEGIN HARNESS FAILURE DIAGNOSTICS ===")
  foreach ($log in $logs) {
    $sections += "--- $($log.Name) ---"
    $content = @(Get-Content -LiteralPath $log.FullName -Tail 120)
    if ($content.Count -eq 0) {
      $sections += "<empty>"
    } else {
      $sections += $content
    }
  }
  $sections += "=== END HARNESS FAILURE DIAGNOSTICS ==="
  return $sections -join "`n"
}

function Assert-LogsContainNoSensitiveData([string]$Directory) {
  $patterns = @(
    @{ Name = "JWT"; Pattern = 'eyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+' },
    @{ Name = "access token"; Pattern = '(?i)access[_-]?token\s*[:=]\s*(?!\[credential redacted\])\S+' },
    @{ Name = "authorization header"; Pattern = '(?i)authorization\s*:\s*(?!\[credential redacted\])\S+' },
    @{ Name = "SDP"; Pattern = '(?m)^v=0\s*$' },
    @{ Name = "ICE candidate"; Pattern = '(?i)(?:^|\s)(?:a=)?candidate:' },
    @{ Name = "WebRTC ICE details"; Pattern = '(?i)(?:cand|conn|port)\[' },
    @{ Name = "credentialed TURN URL"; Pattern = '(?i)turns?:\S*@\S+' }
  )
  if (-not [string]::IsNullOrEmpty($ApiKey)) {
    $patterns += @{ Name = "API key"; Pattern = [regex]::Escape($ApiKey) }
  }
  if (-not [string]::IsNullOrEmpty($ApiSecret)) {
    $patterns += @{ Name = "API secret"; Pattern = [regex]::Escape($ApiSecret) }
  }

  $paths = @()
  if (Test-Path -LiteralPath $Directory -PathType Container) {
    $paths += Get-ChildItem -LiteralPath $Directory -Filter "*.log" -File |
      Select-Object -ExpandProperty FullName
  }
  if ($null -ne $resultLogPathResolved -and
      (Test-Path -LiteralPath $resultLogPathResolved -PathType Leaf)) {
    $paths += $resultLogPathResolved
  }
  foreach ($path in @($paths | Select-Object -Unique)) {
    foreach ($pattern in $patterns) {
      $match = Select-String -LiteralPath $path -Pattern $pattern.Pattern |
        Select-Object -First 1
      if ($null -ne $match) {
        throw "Sensitive-log audit found $($pattern.Name) in " +
          "$([IO.Path]::GetFileName($path)) at line $($match.LineNumber)"
      }
    }
  }

  $capturedSources = @()
  foreach ($path in @($paths | Select-Object -Unique)) {
    $sourceMatches = Select-String -LiteralPath $path `
      -Pattern '\[livekit-sdk\]\[(livekit|webrtc|websocket)\]' -AllMatches
    foreach ($sourceMatch in $sourceMatches) {
      foreach ($match in $sourceMatch.Matches) {
        $capturedSources += $match.Groups[1].Value
      }
    }
  }
  $capturedSources = @($capturedSources | Sort-Object -Unique)
  if ($script:integrationTestStarted -and $capturedSources.Count -eq 0) {
    throw "Unified SDK log audit found no integration log records"
  }
  return $capturedSources
}

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
      "token", "create", "--join", "--room", $Room, "--identity", $Identity, "--valid-for",
      $tokenValidFor
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
  $script:integrationTestStarted = $true
  $test = Start-Process -FilePath $testExecutable `
    -ArgumentList "--gtest_filter=LiveKitServerTest.$TestName" `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -WindowStyle Hidden -PassThru
  try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (-not (Test-Path -LiteralPath $ReadyFile)) {
      if ($test.HasExited) {
        $test.WaitForExit()
        $details = Get-StructuredDiagnostics $TestName $stdout $stderr 80
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
      $details = Get-StructuredDiagnostics $TestName $stdout $stderr 120
      throw "$TestName failed:`n$($assertions -join "`n")`n$details"
    }
    Write-HarnessResult "PASS $TestName"
  } finally {
    Stop-OwnedProcess $test
  }
}

function Invoke-SimpleTest([string]$TestName) {
  $stdout = Join-Path $tempRoot "$TestName.out.log"
  $stderr = Join-Path $tempRoot "$TestName.err.log"
  $script:integrationTestStarted = $true
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
      $details = Get-StructuredDiagnostics $TestName $stdout $stderr 160
      throw "$TestName failed:`n$($assertions -join "`n")`n$details"
    }
    Write-HarnessResult "PASS $TestName"
  } finally {
    Stop-OwnedProcess $test
  }
}

function Measure-TcpConnectMilliseconds([int]$TcpPort, [int]$TimeoutMilliseconds) {
  $client = [Net.Sockets.TcpClient]::new()
  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  try {
    $connect = $client.ConnectAsync("127.0.0.1", $TcpPort)
    if (-not $connect.Wait($TimeoutMilliseconds) -or -not $client.Connected) {
      return -1
    }
    return [int]$stopwatch.ElapsedMilliseconds
  } catch {
    return -1
  } finally {
    $stopwatch.Stop()
    $client.Dispose()
  }
}

function Assert-NetworkFaultActive([string]$Profile, [int]$TcpPort) {
  switch ($Profile) {
    "Loss" {
      $samples = 1..20 | ForEach-Object {
        Measure-TcpConnectMilliseconds $TcpPort 400
      }
      $affected = @($samples | Where-Object { $_ -lt 0 -or $_ -ge 150 }).Count
      if ($affected -eq 0) {
        throw "clumsy loss profile did not affect any of 20 TCP probes"
      }
    }
    "Latency" {
      $elapsed = Measure-TcpConnectMilliseconds $TcpPort 2000
      if ($elapsed -lt 200) {
        throw "clumsy latency profile was not observed (connect time: $elapsed ms)"
      }
    }
    "Jitter" {
      $elapsed = Measure-TcpConnectMilliseconds $TcpPort 1500
      if ($elapsed -lt 50) {
        throw "clumsy low-latency jitter phase was not observed (connect time: $elapsed ms)"
      }
    }
    "Outage" {
      $elapsed = Measure-TcpConnectMilliseconds $TcpPort 1000
      if ($elapsed -ge 0) {
        throw "clumsy outage profile still allowed a TCP connection in $elapsed ms"
      }
    }
  }
}

function Invoke-WeakNetworkTest(
  [string]$Profile,
  [string[]]$ClumsyArguments,
  [int]$DurationSeconds
) {
  $readyFile = Join-Path $tempRoot "weak-network-$($Profile.ToLowerInvariant()).ready"
  $doneFile = Join-Path $tempRoot "weak-network-$($Profile.ToLowerInvariant()).done"
  Remove-Item -LiteralPath $readyFile -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $doneFile -Force -ErrorAction SilentlyContinue
  $env:LIVEKIT_NETWORK_FAULT_PROFILE = $Profile
  $env:LIVEKIT_NETWORK_FAULT_READY_FILE = $readyFile
  $env:LIVEKIT_NETWORK_FAULT_DONE_FILE = $doneFile

  $testName = "RecoversMediaAndDataAfterExternalNetworkFault"
  $stdout = Join-Path $tempRoot "weak-network-$($Profile.ToLowerInvariant()).out.log"
  $stderr = Join-Path $tempRoot "weak-network-$($Profile.ToLowerInvariant()).err.log"
  $script:integrationTestStarted = $true
  $test = Start-Process -FilePath $testExecutable `
    -ArgumentList "--gtest_filter=LiveKitServerTest.$testName" `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -WindowStyle Hidden -PassThru
  $clumsy = $null
  try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (-not (Test-Path -LiteralPath $readyFile)) {
      if ($test.HasExited) {
        $details = Get-StructuredDiagnostics "weak-network-$Profile" $stdout $stderr 120
        throw "$Profile test exited before reaching the fault point:`n$details"
      }
      if ([DateTime]::UtcNow -ge $deadline) {
        throw "Timed out waiting for the $Profile network-fault coordination point"
      }
      Start-Sleep -Milliseconds 50
    }

    $faultTimer = [Diagnostics.Stopwatch]::StartNew()
    $clumsyTimeout = $DurationSeconds + 5
    $argumentLine = ($ClumsyArguments + @("--timeout", [string]$clumsyTimeout)) -join " "
    $clumsy = Start-Process -FilePath $clumsyPath -ArgumentList $argumentLine `
      -WorkingDirectory (Split-Path -Parent $clumsyPath) -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    if ($clumsy.HasExited) {
      $clumsy.Refresh()
      throw "clumsy exited before applying the $Profile profile (exit code $($clumsy.ExitCode))"
    }
    Assert-NetworkFaultActive $Profile $Port
    if ($Profile -eq "Jitter") {
      $lowLatency = Measure-TcpConnectMilliseconds $Port 1500
      Stop-OwnedProcess $clumsy
      $highLatencyArguments = $argumentLine -replace '--lag-time 50', '--lag-time 300'
      $clumsy = Start-Process -FilePath $clumsyPath -ArgumentList $highLatencyArguments `
        -WorkingDirectory (Split-Path -Parent $clumsyPath) -WindowStyle Hidden -PassThru
      Start-Sleep -Seconds 1
      if ($clumsy.HasExited) {
        $clumsy.Refresh()
        throw "clumsy exited before applying the high-latency jitter phase " +
          "(exit code $($clumsy.ExitCode))"
      }
      $highLatency = Measure-TcpConnectMilliseconds $Port 2000
      if ($highLatency -lt 200 -or $highLatency - $lowLatency -lt 150) {
        throw "clumsy jitter phases were not observed " +
          "(low: $lowLatency ms, high: $highLatency ms)"
      }
    }
    Write-HarnessResult "Applied $Profile fault for $DurationSeconds seconds"
    $remainingMilliseconds = [int](($DurationSeconds - $faultTimer.Elapsed.TotalSeconds) * 1000)
    if ($remainingMilliseconds -gt 0) {
      Start-Sleep -Milliseconds $remainingMilliseconds
    }
    $faultTimer.Stop()
    Stop-OwnedProcess $clumsy
    $clumsy = $null
    New-Item -ItemType File -Path $doneFile -Force | Out-Null

    if (-not $test.WaitForExit(120000)) {
      Stop-OwnedProcess $test
      throw "$Profile network-fault test timed out"
    }
    $test.WaitForExit()
    $test.Refresh()
    $passed = Select-String -LiteralPath $stdout -Pattern '^\[  PASSED  \] 1 test\.$' -Quiet
    if (-not $passed) {
      $details = Get-StructuredDiagnostics "weak-network-$Profile" $stdout $stderr 160
      throw "$Profile network-fault test failed:`n$details"
    }
    Write-HarnessResult "PASS weak-network $Profile"
  } finally {
    Stop-OwnedProcess $clumsy
    if (-not (Test-Path -LiteralPath $doneFile)) {
      New-Item -ItemType File -Path $doneFile -Force | Out-Null
    }
    Stop-OwnedProcess $test
  }
}

function Invoke-ResourceSoakTest([string]$TestName, [string]$Label, [int]$DurationSeconds) {
  $readyFile = Join-Path $tempRoot "soak-$Label.ready"
  Remove-Item -LiteralPath $readyFile -Force -ErrorAction SilentlyContinue
  $env:LIVEKIT_CODEC_SOAK_READY_FILE = $readyFile
  $stdout = Join-Path $tempRoot "soak-$Label.out.log"
  $stderr = Join-Path $tempRoot "soak-$Label.err.log"
  $script:integrationTestStarted = $true
  $test = Start-Process -FilePath $testExecutable `
    -ArgumentList "--gtest_filter=LiveKitServerTest.$TestName" `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
    -WindowStyle Hidden -PassThru
  try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (-not (Test-Path -LiteralPath $readyFile)) {
      if ($test.HasExited) {
        $details = Get-StructuredDiagnostics "resource-soak-$Label" $stdout $stderr 120
        throw "$Label soak exited before reaching the sampling point:`n$details"
      }
      if ([DateTime]::UtcNow -ge $deadline) {
        throw "Timed out waiting for the $Label soak sampling point"
      }
      Start-Sleep -Milliseconds 50
    }

    $test.Refresh()
    $baselineHandles = $test.HandleCount
    $baselineThreads = $test.Threads.Count
    $baselinePrivateBytes = $test.PrivateMemorySize64
    $peakHandles = $baselineHandles
    $peakThreads = $baselineThreads
    $peakPrivateBytes = $baselinePrivateBytes
    $lastHandles = $baselineHandles
    $lastThreads = $baselineThreads
    $lastPrivateBytes = $baselinePrivateBytes
    $samplingTimer = [Diagnostics.Stopwatch]::StartNew()
    $nextProgressSeconds = 300

    while (-not $test.WaitForExit(1000)) {
      $test.Refresh()
      $lastHandles = $test.HandleCount
      $lastThreads = $test.Threads.Count
      $lastPrivateBytes = $test.PrivateMemorySize64
      $peakHandles = [Math]::Max($peakHandles, $lastHandles)
      $peakThreads = [Math]::Max($peakThreads, $lastThreads)
      $peakPrivateBytes = [Math]::Max($peakPrivateBytes, $lastPrivateBytes)
      if ($samplingTimer.Elapsed.TotalSeconds -ge $nextProgressSeconds) {
        $elapsedMinutes = [Math]::Round($samplingTimer.Elapsed.TotalMinutes, 1)
        $progressPrivateMb = [Math]::Round(
          ($peakPrivateBytes - $baselinePrivateBytes) / 1MB, 1)
        Write-HarnessResult (
          "PROGRESS resource-soak $Label ${elapsedMinutes}m; " +
          "peak-growth handles=$($peakHandles - $baselineHandles) " +
          "threads=$($peakThreads - $baselineThreads) private=${progressPrivateMb}MB"
        )
        $nextProgressSeconds += 300
      }
    }
    $samplingTimer.Stop()
    $test.WaitForExit()
    $test.Refresh()

    $passed = Select-String -LiteralPath $stdout -Pattern '^\[  PASSED  \] 1 test\.$' -Quiet
    if (-not $passed) {
      $details = Get-StructuredDiagnostics "resource-soak-$Label" $stdout $stderr 160
      throw "$Label resource soak failed:`n$details"
    }

    $handleGrowth = $peakHandles - $baselineHandles
    $threadGrowth = $peakThreads - $baselineThreads
    $privateGrowth = $peakPrivateBytes - $baselinePrivateBytes
    $privateGrowthMb = [Math]::Round($privateGrowth / 1MB, 1)
    if ($handleGrowth -gt $MaxHandleGrowth) {
      throw "$Label handle growth $handleGrowth exceeded $MaxHandleGrowth"
    }
    if ($threadGrowth -gt $MaxThreadGrowth) {
      throw "$Label thread growth $threadGrowth exceeded $MaxThreadGrowth"
    }
    if ($privateGrowth -gt $MaxPrivateMemoryGrowthMb * 1MB) {
      throw "$Label private-memory growth ${privateGrowthMb}MB exceeded " +
        "${MaxPrivateMemoryGrowthMb}MB"
    }
    Write-HarnessResult (
      "PASS resource-soak $Label ${DurationSeconds}s; " +
      "handles $baselineHandles/$lastHandles/$peakHandles; " +
      "threads $baselineThreads/$lastThreads/$peakThreads; " +
      "private-bytes $baselinePrivateBytes/$lastPrivateBytes/$peakPrivateBytes; " +
      "peak-growth handles=$handleGrowth threads=$threadGrowth private=${privateGrowthMb}MB"
    )
  } finally {
    Stop-OwnedProcess $test
    $env:LIVEKIT_CODEC_SOAK_READY_FILE = $null
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
        $details = Get-StructuredDiagnostics "official-cpp-peer" $stdout $stderr 100
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
      $details = Get-StructuredDiagnostics "official-cpp-peer" $stdout $stderr 120
      throw "Official C++ peer failed:`n$details"
    }
    Write-HarnessResult "PASS official C++ E2EE peer"
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
$clumsyPath = $null
if (-not [string]::IsNullOrEmpty($ClumsyExecutable)) {
  $clumsyPath = Resolve-RequiredPath $ClumsyExecutable "clumsy executable"
} elseif ($Scenario -eq "WeakNetwork") {
  throw "-ClumsyExecutable is required for -Scenario WeakNetwork"
}
if ($Scenario -eq "WeakNetwork" -or ($Scenario -eq "All" -and $null -ne $clumsyPath)) {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Weak-network tests must run from an Administrator PowerShell because clumsy uses " +
      "the WinDivert driver"
  }
}
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
  try {
    $defaultRoute = Get-NetRoute -AddressFamily IPv4 -DestinationPrefix "0.0.0.0/0" `
      -ErrorAction Stop |
      Sort-Object RouteMetric, InterfaceMetric |
      Select-Object -First 1
  } catch {
    $defaultRoute = $null
  }
  if ($null -eq $defaultRoute) {
    $NodeIp = "127.0.0.1"
    Write-Warning "Unable to inspect the default route; using loopback for the local matrix"
  } else {
    try {
      $NodeIp = Get-NetIPAddress -AddressFamily IPv4 `
        -InterfaceIndex $defaultRoute.InterfaceIndex -ErrorAction Stop |
        Where-Object { $_.IPAddress -notlike "169.254.*" } |
        Select-Object -ExpandProperty IPAddress -First 1
    } catch {
      $NodeIp = ""
    }
    if ([string]::IsNullOrEmpty($NodeIp)) {
      $NodeIp = "127.0.0.1"
      Write-Warning "Unable to resolve the default-route address; using loopback for the local matrix"
    }
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
  LIVEKIT_TOKEN_CAPI_RESTART = $env:LIVEKIT_TOKEN_CAPI_RESTART
  LIVEKIT_TOKEN_CAPI_RESTART_2 = $env:LIVEKIT_TOKEN_CAPI_RESTART_2
  LIVEKIT_TOKEN_REFRESH = $env:LIVEKIT_TOKEN_REFRESH
  LIVEKIT_TOKEN_SINGLE = $env:LIVEKIT_TOKEN_SINGLE
  LIVEKIT_TOKEN_PARTICIPANT_OBSERVER = $env:LIVEKIT_TOKEN_PARTICIPANT_OBSERVER
  LIVEKIT_TOKEN_PARTICIPANT_1 = $env:LIVEKIT_TOKEN_PARTICIPANT_1
  LIVEKIT_TOKEN_PARTICIPANT_2 = $env:LIVEKIT_TOKEN_PARTICIPANT_2
  LIVEKIT_TOKEN_PARTICIPANT_3 = $env:LIVEKIT_TOKEN_PARTICIPANT_3
  LIVEKIT_TOKEN_PARTICIPANT_4 = $env:LIVEKIT_TOKEN_PARTICIPANT_4
  LIVEKIT_TOKEN_DUPLICATE_OBSERVER = $env:LIVEKIT_TOKEN_DUPLICATE_OBSERVER
  LIVEKIT_TOKEN_DUPLICATE_1 = $env:LIVEKIT_TOKEN_DUPLICATE_1
  LIVEKIT_TOKEN_DUPLICATE_2 = $env:LIVEKIT_TOKEN_DUPLICATE_2
  LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN
  LIVEKIT_TOKEN_2 = $env:LIVEKIT_TOKEN_2
  LIVEKIT_VIDEO_CODEC = $env:LIVEKIT_VIDEO_CODEC
  LIVEKIT_CODEC_SOAK_SECONDS = $env:LIVEKIT_CODEC_SOAK_SECONDS
  LIVEKIT_CODEC_SOAK_READY_FILE = $env:LIVEKIT_CODEC_SOAK_READY_FILE
  LIVEKIT_AUDIO_QUALITY = $env:LIVEKIT_AUDIO_QUALITY
  LIVEKIT_AUDIO_QUALITY_SECONDS = $env:LIVEKIT_AUDIO_QUALITY_SECONDS
  LIVEKIT_AUDIO_QUALITY_MIN_ERLE_DB = $env:LIVEKIT_AUDIO_QUALITY_MIN_ERLE_DB
  LIVEKIT_AUDIO_QUALITY_MAX_RESIDUAL_ECHO = $env:LIVEKIT_AUDIO_QUALITY_MAX_RESIDUAL_ECHO
  LIVEKIT_AUDIO_QUALITY_SPEAKER_VOLUME = $env:LIVEKIT_AUDIO_QUALITY_SPEAKER_VOLUME
  LIVEKIT_AUDIO_QUALITY_INPUT_DEVICE_ID = $env:LIVEKIT_AUDIO_QUALITY_INPUT_DEVICE_ID
  LIVEKIT_AUDIO_QUALITY_OUTPUT_DEVICE_ID = $env:LIVEKIT_AUDIO_QUALITY_OUTPUT_DEVICE_ID
  LIVEKIT_AUDIO_FIXTURE_VOLUME = $env:LIVEKIT_AUDIO_FIXTURE_VOLUME
  LIVEKIT_AUDIO_DOUBLE_TALK_FAR_END_VOLUME = $env:LIVEKIT_AUDIO_DOUBLE_TALK_FAR_END_VOLUME
  LIVEKIT_AUDIO_NOISE_FIXTURE_VOLUME = $env:LIVEKIT_AUDIO_NOISE_FIXTURE_VOLUME
  LIVEKIT_AUDIO_DOUBLE_TALK_MIN_RETENTION = $env:LIVEKIT_AUDIO_DOUBLE_TALK_MIN_RETENTION
  LIVEKIT_AUDIO_NOISE_MAX_RATIO = $env:LIVEKIT_AUDIO_NOISE_MAX_RATIO
  LIVEKIT_AUDIO_HARDWARE_PHASE_SECONDS = $env:LIVEKIT_AUDIO_HARDWARE_PHASE_SECONDS
  LIVEKIT_OFFICIAL_CPP_PEER_IDENTITY = $env:LIVEKIT_OFFICIAL_CPP_PEER_IDENTITY
  LIVEKIT_SERVER_RESTART_READY_FILE = $env:LIVEKIT_SERVER_RESTART_READY_FILE
  LIVEKIT_CAPI_SERVER_RESTART_READY_FILE = $env:LIVEKIT_CAPI_SERVER_RESTART_READY_FILE
  LIVEKIT_TOKEN_REFRESH_READY_FILE = $env:LIVEKIT_TOKEN_REFRESH_READY_FILE
  LIVEKIT_NETWORK_FAULT_PROFILE = $env:LIVEKIT_NETWORK_FAULT_PROFILE
  LIVEKIT_NETWORK_FAULT_READY_FILE = $env:LIVEKIT_NETWORK_FAULT_READY_FILE
  LIVEKIT_NETWORK_FAULT_DONE_FILE = $env:LIVEKIT_NETWORK_FAULT_DONE_FILE
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
  $tokenValidFor = "15m"
  if ($Scenario -eq "Soak") {
    $tokenValidFor = "$([Math]::Ceiling($SoakSeconds / 60) + 15)m"
  }
  $room = "cpp-reconnect-matrix-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $restartIdentity = "restart-client"
  $refreshIdentity = "refresh-client"
  $env:LIVEKIT_TOKEN_RESTART = New-ParticipantToken $room $restartIdentity
  $env:LIVEKIT_TOKEN_REFRESH = New-ParticipantToken $room $refreshIdentity
  $singleRoom = "cpp-signal-resume-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $env:LIVEKIT_TOKEN_SINGLE = New-ParticipantToken $singleRoom "signal-resume-client"
  $participantRoom = "cpp-participant-matrix-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $env:LIVEKIT_TOKEN_PARTICIPANT_OBSERVER =
    New-ParticipantToken $participantRoom "participant-observer"
  $env:LIVEKIT_TOKEN_PARTICIPANT_1 = New-ParticipantToken $participantRoom "participant-1"
  $env:LIVEKIT_TOKEN_PARTICIPANT_2 = New-ParticipantToken $participantRoom "participant-2"
  $env:LIVEKIT_TOKEN_PARTICIPANT_3 = New-ParticipantToken $participantRoom "participant-3"
  $env:LIVEKIT_TOKEN_PARTICIPANT_4 = New-ParticipantToken $participantRoom "participant-4"
  $duplicateRoom = "cpp-duplicate-matrix-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $env:LIVEKIT_TOKEN_DUPLICATE_OBSERVER =
    New-ParticipantToken $duplicateRoom "duplicate-observer"
  $env:LIVEKIT_TOKEN_DUPLICATE_1 = New-ParticipantToken $duplicateRoom "duplicate-client"
  $env:LIVEKIT_TOKEN_DUPLICATE_2 = New-ParticipantToken $duplicateRoom "duplicate-client"
  $capiReconnectRoom = "cpp-capi-reconnect-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $env:LIVEKIT_TOKEN_CAPI_RESTART = New-ParticipantToken $capiReconnectRoom "capi-sender"
  $env:LIVEKIT_TOKEN_CAPI_RESTART_2 = New-ParticipantToken $capiReconnectRoom "capi-receiver"
  $e2eeRoom = "cpp-e2ee-matrix-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $env:LIVEKIT_TOKEN = New-ParticipantToken $e2eeRoom "e2ee-sender" -AllowUpdateMetadata
  $env:LIVEKIT_TOKEN_2 = New-ParticipantToken $e2eeRoom "e2ee-receiver"
  $env:LIVEKIT_VIDEO_CODEC = $VideoCodec
  $env:LIVEKIT_SERVER_RESTART_READY_FILE = Join-Path $tempRoot "restart.ready"
  $env:LIVEKIT_CAPI_SERVER_RESTART_READY_FILE = Join-Path $tempRoot "capi-restart.ready"
  $env:LIVEKIT_TOKEN_REFRESH_READY_FILE = Join-Path $tempRoot "refresh.ready"

  $server = Start-TestServer
  if ($Scenario -in @("All", "Participants")) {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
      Write-Host "Participant matrix iteration $iteration/$Iterations"
      Invoke-SimpleTest "HandlesConcurrentParticipantJoinAndLeave"
      Invoke-SimpleTest "ReplacesDuplicateIdentityAndAllowsRejoin"
    }
  }

  if ($Scenario -in @("All", "Restart")) {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
      Write-Host "Server restart iteration $iteration/$Iterations"
      Invoke-CoordinatedTest "RecoversAfterExplicitServerRestart" `
        $env:LIVEKIT_SERVER_RESTART_READY_FILE {
          Stop-OwnedProcess $server
          Start-Sleep -Seconds 2
          $script:server = Start-TestServer
        }
    }
  }

  if ($Scenario -in @("All", "Restart", "CAPI")) {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
      Write-Host "C API server restart iteration $iteration/$Iterations"
      Invoke-CoordinatedTest "CApiRecoversAfterExplicitServerRestart" `
        $env:LIVEKIT_CAPI_SERVER_RESTART_READY_FILE {
          Stop-OwnedProcess $server
          Start-Sleep -Seconds 2
          $script:server = Start-TestServer
        }
    }
  }

  if ($Scenario -in @("All", "TokenRefresh")) {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
      Write-Host "Resume and full reconnect iteration $iteration/$Iterations"
      Invoke-SimpleTest "RecoversAfterSignalTransportDisconnect"
      Invoke-CoordinatedTest "UsesRefreshedTokenForResumeAndFullReconnect" `
        $env:LIVEKIT_TOKEN_REFRESH_READY_FILE {
          # JoinResponse supplies a server-refreshed token. Reaching this point
          # confirms that the client retained it before recovery is triggered.
          Start-Sleep -Milliseconds 100
        }
    }
  }

  if ($Scenario -in @("All", "Media")) {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
      Write-Host "Media recovery iteration $iteration/$Iterations"
      Invoke-SimpleTest "RepublishesAudioAfterReconnect"
      Invoke-SimpleTest "PublishesAndReceivesSelectedVideoCodec"
      Invoke-SimpleTest "PublishesAndReceivesVideoFrameMetadata"
      Invoke-SimpleTest "PublishesBackupCodecWhenRequestedByServer"
      Invoke-SimpleTest "PublishesAndReceivesAudioAndVideo"
    }
  }

  if ($Scenario -eq "FrameMetadata") {
    Invoke-SimpleTest "PublishesAndReceivesVideoFrameMetadata"
  }

  if ($Scenario -eq "CodecMatrix") {
    $env:LIVEKIT_CODEC_SOAK_SECONDS = [string]$CodecSoakSeconds
    foreach ($codec in @("vp8", "vp9", "h264", "av1")) {
      $env:LIVEKIT_VIDEO_CODEC = $codec
      Write-HarnessResult "Codec soak $codec for $CodecSoakSeconds seconds"
      Invoke-SimpleTest "PublishesAndReceivesSelectedVideoCodec"
      Write-HarnessResult "PASS codec-soak $codec"
    }
  }

  if ($Scenario -eq "Soak") {
    $env:LIVEKIT_CODEC_SOAK_SECONDS = [string]$SoakSeconds
    $env:LIVEKIT_VIDEO_CODEC = $VideoCodec
    Write-HarnessResult "Resource soak $VideoCodec for $SoakSeconds seconds"
    Invoke-ResourceSoakTest "PublishesAndReceivesSelectedVideoCodec" $VideoCodec $SoakSeconds
  }

  if ($Scenario -eq "AudioQuality") {
    $env:LIVEKIT_AUDIO_QUALITY = "1"
    $env:LIVEKIT_AUDIO_QUALITY_SECONDS = [string]$AudioQualitySeconds
    $env:LIVEKIT_AUDIO_QUALITY_MIN_ERLE_DB =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioQualityMinErleDb)
    $env:LIVEKIT_AUDIO_QUALITY_MAX_RESIDUAL_ECHO =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioQualityMaxResidualEcho)
    $env:LIVEKIT_AUDIO_QUALITY_SPEAKER_VOLUME =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioQualitySpeakerVolume)
    $env:LIVEKIT_AUDIO_QUALITY_INPUT_DEVICE_ID = $AudioQualityInputDeviceId
    $env:LIVEKIT_AUDIO_QUALITY_OUTPUT_DEVICE_ID = $AudioQualityOutputDeviceId
    $env:LIVEKIT_AUDIO_FIXTURE_VOLUME =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioFixtureVolume)
    $env:LIVEKIT_AUDIO_DOUBLE_TALK_FAR_END_VOLUME =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioDoubleTalkFarEndVolume)
    $env:LIVEKIT_AUDIO_NOISE_FIXTURE_VOLUME =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioNoiseFixtureVolume)
    $env:LIVEKIT_AUDIO_DOUBLE_TALK_MIN_RETENTION =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioDoubleTalkMinRetention)
    $env:LIVEKIT_AUDIO_NOISE_MAX_RATIO =
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}",
        $AudioNoiseMaxRatio)
    $env:LIVEKIT_AUDIO_HARDWARE_PHASE_SECONDS = [string]$AudioHardwarePhaseSeconds
    $audioQualityFailures = @()
    try {
      Write-HarnessResult "Acoustic AEC quality measurement for $AudioQualitySeconds seconds"
      Invoke-SimpleTest "MeasuresHardwareAecQuality"
      $audioQualityOutput = Join-Path $tempRoot "MeasuresHardwareAecQuality.out.log"
      $audioQualityResult = Select-String -LiteralPath $audioQualityOutput `
        -Pattern '^AUDIO_QUALITY_RESULT ' | Select-Object -Last 1
      if ($null -eq $audioQualityResult) {
        throw "Audio quality test passed without emitting raw measurement evidence"
      }
      Write-HarnessResult $audioQualityResult.Line
    } catch {
      $audioQualityFailures += $_.Exception.Message
      Write-HarnessResult "FAIL acoustic AEC quality gate"
    }
    try {
      Write-HarnessResult (
        "Hardware double-talk and noise suppression measurement for " +
        "$AudioHardwarePhaseSeconds seconds per phase")
      Invoke-SimpleTest "MeasuresHardwareDoubleTalkAndNoiseSuppression"
      $hardwareProcessingOutput =
        Join-Path $tempRoot "MeasuresHardwareDoubleTalkAndNoiseSuppression.out.log"
      $hardwareProcessingResult = Select-String -LiteralPath $hardwareProcessingOutput `
        -Pattern '^AUDIO_HARDWARE_PROCESSING_RESULT ' | Select-Object -Last 1
      if ($null -eq $hardwareProcessingResult) {
        throw "Hardware processing test passed without emitting raw measurement evidence"
      }
      Write-HarnessResult $hardwareProcessingResult.Line
    } catch {
      $audioQualityFailures += $_.Exception.Message
      Write-HarnessResult "FAIL hardware double-talk/noise-suppression quality gate"
    }
    if ($audioQualityFailures.Count -gt 0) {
      throw "Audio quality gates failed:`n$($audioQualityFailures -join "`n")"
    }
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

  if ($Scenario -in @("All", "DataRecovery")) {
    Invoke-SimpleTest "PreservesDataStreamsAndRpcAcrossFullReconnect"
  }

  if ($Scenario -in @("All", "CAPI")) {
    Invoke-SimpleTest "CApiReportsParticipantProfileChanges"
    Invoke-SimpleTest "CApiPreservesDataStreamMetadata"
  }

  if ($Scenario -eq "WeakNetwork" -or ($Scenario -eq "All" -and $null -ne $clumsyPath)) {
    $networkFilter = '"outbound and loopback and ((tcp and ' +
      "(tcp.DstPort == $Port or tcp.SrcPort == $Port or " +
      "tcp.DstPort == $($Port + 1) or tcp.SrcPort == $($Port + 1))) or " +
      "(udp and (udp.DstPort == $($Port + 2) or udp.SrcPort == $($Port + 2))))" + '"'
    $profiles = @(
      @{
        Name = "Loss"
        Duration = 8
        Arguments = @(
          "--filter", $networkFilter, "--drop", "on", "--drop-inbound", "on",
          "--drop-outbound", "on", "--drop-chance", "15.0"
        )
      },
      @{
        Name = "Latency"
        Duration = 8
        Arguments = @(
          "--filter", $networkFilter, "--lag", "on", "--lag-inbound", "on",
          "--lag-outbound", "on", "--lag-chance", "100.0", "--lag-time", "250"
        )
      },
      @{
        Name = "Jitter"
        Duration = 8
        Arguments = @(
          "--filter", $networkFilter, "--lag", "on", "--lag-inbound", "on",
          "--lag-outbound", "on", "--lag-time", "50"
        )
      },
      @{
        Name = "Outage"
        Duration = 10
        Arguments = @(
          "--filter", $networkFilter, "--drop", "on", "--drop-inbound", "on",
          "--drop-outbound", "on", "--drop-chance", "100.0"
        )
      }
    )
    foreach ($profile in $profiles) {
      if ($WeakNetworkProfile -eq "All" -or $WeakNetworkProfile -eq $profile.Name) {
        Invoke-WeakNetworkTest $profile.Name $profile.Arguments $profile.Duration
      }
    }
  }

  if ($Scenario -eq "OfficialCpp" -or
      ($Scenario -eq "All" -and $null -ne $officialCppPeerPath)) {
    Invoke-OfficialCppTest
  }
} catch {
  Write-HarnessResult "FAIL $($_.Exception.Message)"
  $diagnostics = Get-HarnessFailureDiagnostics $tempRoot
  if (-not [string]::IsNullOrEmpty($diagnostics)) {
    Write-HarnessResult $diagnostics
  }
  throw
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
  $auditFailure = $null
  try {
    $capturedLogSources = @(Assert-LogsContainNoSensitiveData $tempRoot)
    if ($capturedLogSources.Count -gt 0) {
      Write-HarnessResult "PASS unified-log evidence sources=$($capturedLogSources -join ',')"
    } else {
      Write-HarnessResult "SKIP unified-log evidence: no SDK integration process started"
    }
    Write-HarnessResult "PASS sensitive-log audit"
  } catch {
    $auditFailure = $_
    Write-HarnessResult "FAIL sensitive-log audit: $($_.Exception.Message)"
  }
  foreach ($entry in $originalEnvironment.GetEnumerator()) {
    [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
  }
  if (Test-Path -LiteralPath $tempRoot) {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force
  }
  if ($null -ne $auditFailure) {
    throw $auditFailure
  }
}
