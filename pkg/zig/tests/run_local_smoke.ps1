param(
  [int]$Port = 18980,
  [string]$BuildDirectory = "out/build/sdk-dll-md-release/Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$workspaceRoot = (Resolve-Path (Join-Path $repoRoot "..")).Path
$serverExe = (Resolve-Path (Join-Path $workspaceRoot "others/livekit-server/bin/livekit-server.exe")).Path
$lkExe = (Resolve-Path (Join-Path $workspaceRoot "others/lk_2.18.2_windows_amd64/lk.exe")).Path
$sdkDirectory = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$smokeExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_zig_two_peer_smoke.exe")).Path
$roomExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_room_zig.exe")).Path
$transferExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_data_transfer_zig.exe")).Path
$streamExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_stream_writer_zig.exe")).Path
$dataTrackExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_data_track_schema_zig.exe")).Path
$audioExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_publish_audio_zig.exe")).Path
$videoExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_publish_video_zig.exe")).Path
$tokenSourceExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_token_source_zig.exe")).Path
$e2eeExe = (Resolve-Path (Join-Path $PSScriptRoot "../zig-out/bin/livekit_e2ee_zig.exe")).Path
$tempRoot = Join-Path $env:TEMP ("livekit-zig-smoke-" + [guid]::NewGuid().ToString("N"))
$configPath = Join-Path $tempRoot "server.yaml"
$stdoutPath = Join-Path $tempRoot "server.stdout.log"
$stderrPath = Join-Path $tempRoot "server.stderr.log"
$savedPath = $env:Path
$savedUrl = $env:LIVEKIT_URL
$savedTokenA = $env:LIVEKIT_TOKEN_A
$savedTokenB = $env:LIVEKIT_TOKEN_B
$savedToken = $env:LIVEKIT_TOKEN
$savedE2eeKey = $env:LIVEKIT_E2EE_KEY
$server = $null

$secretBytes = New-Object byte[] 32
$generator = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $generator.GetBytes($secretBytes) } finally { $generator.Dispose() }
$apiSecret = ([BitConverter]::ToString($secretBytes)).Replace("-", "").ToLowerInvariant()
$apiKey = "zig-smoke"
$roomName = "zig-smoke-" + [guid]::NewGuid().ToString("N")

New-Item -ItemType Directory -Path $tempRoot | Out-Null
try {
  $config = @(
    "port: $Port"
    "keys:"
    "  ${apiKey}: ${apiSecret}"
    "enable_participant_data_blob: true"
    "rtc:"
    "  tcp_port: $($Port + 1)"
    "  udp_port: $($Port + 2)"
    "  use_external_ip: false"
  ) -join [Environment]::NewLine
  [IO.File]::WriteAllText($configPath, $config, [Text.UTF8Encoding]::new($false))

  $server = Start-Process -FilePath $serverExe `
    -ArgumentList @("--bind", "127.0.0.1", "--node-ip", "127.0.0.1", "--config", $configPath) `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath `
    -WindowStyle Hidden -PassThru

  $ready = $false
  for ($attempt = 0; $attempt -lt 100; $attempt++) {
    if ($server.HasExited) { throw "LiveKit server exited during startup" }
    $client = [Net.Sockets.TcpClient]::new()
    try {
      if ($client.ConnectAsync("127.0.0.1", $Port).Wait(200) -and $client.Connected) {
        $ready = $true
        break
      }
    } finally {
      $client.Dispose()
    }
    Start-Sleep -Milliseconds 100
  }
  if (-not $ready) { throw "Timed out waiting for LiveKit server" }

  $env:LIVEKIT_URL = "ws://127.0.0.1:$Port"
  $env:LIVEKIT_TOKEN_A = & $lkExe --quiet --url "http://127.0.0.1:$Port" `
    --api-key $apiKey --api-secret $apiSecret token create `
    --room $roomName --identity zig-a --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Failed to generate participant A token" }
  $env:LIVEKIT_TOKEN_B = & $lkExe --quiet --url "http://127.0.0.1:$Port" `
    --api-key $apiKey --api-secret $apiSecret token create `
    --room $roomName --identity zig-b --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Failed to generate participant B token" }

  $env:Path = "$sdkDirectory;$savedPath"
  & $smokeExe
  if ($LASTEXITCODE -ne 0) { throw "Zig two-peer smoke test failed" }

  $env:LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN_A
  & $roomExe
  if ($LASTEXITCODE -ne 0) { throw "Zig room example failed" }
  & $transferExe
  if ($LASTEXITCODE -ne 0) { throw "Zig data-transfer example failed" }
  & $streamExe
  if ($LASTEXITCODE -ne 0) { throw "Zig stream-writer example failed" }
  & $dataTrackExe
  if ($LASTEXITCODE -ne 0) { throw "Zig DataTrack example failed" }
  & $audioExe
  if ($LASTEXITCODE -ne 0) { throw "Zig synthetic-audio example failed" }
  & $videoExe
  if ($LASTEXITCODE -ne 0) { throw "Zig synthetic-video example failed" }
  & $tokenSourceExe
  if ($LASTEXITCODE -ne 0) { throw "Zig token-source example failed" }
  $env:LIVEKIT_E2EE_KEY = $apiSecret
  & $e2eeExe
  if ($LASTEXITCODE -ne 0) { throw "Zig E2EE example failed" }
} catch {
  if (Test-Path -LiteralPath $stderrPath) {
    Get-Content -LiteralPath $stderrPath | Select-String -Pattern 'ERROR|error' | Select-Object -Last 5
  }
  throw
} finally {
  $env:Path = $savedPath
  $env:LIVEKIT_URL = $savedUrl
  $env:LIVEKIT_TOKEN_A = $savedTokenA
  $env:LIVEKIT_TOKEN_B = $savedTokenB
  $env:LIVEKIT_TOKEN = $savedToken
  $env:LIVEKIT_E2EE_KEY = $savedE2eeKey
  if ($null -ne $server -and -not $server.HasExited) {
    Stop-Process -Id $server.Id
    $server.WaitForExit(10000) | Out-Null
  }
  $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
  $resolvedTempBase = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
  if ($resolvedTempRoot.StartsWith($resolvedTempBase, [StringComparison]::OrdinalIgnoreCase)) {
    Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force
  }
}
