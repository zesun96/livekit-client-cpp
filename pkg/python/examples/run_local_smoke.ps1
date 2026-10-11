param(
  [int]$Port = 17880,
  [string]$BuildDirectory = "out/build/sdk-dll-md-release/Release",
  [ValidateSet("two_peer_smoke", "room_echo", "publish_audio", "publish_video", "data_transfer", "stream_writer", "token_source", "data_track_schema", "receive_media", "receive_media_events", "record_remote", "rpc")]
  [string]$Example = "two_peer_smoke"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$workspaceRoot = (Resolve-Path (Join-Path $repoRoot "..")).Path
$serverExe = (Resolve-Path (Join-Path $workspaceRoot "others/livekit-server/bin/livekit-server.exe")).Path
$lkExe = (Resolve-Path (Join-Path $workspaceRoot "others/lk_2.18.2_windows_amd64/lk.exe")).Path
$sdkDirectory = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$sdkDll = (Resolve-Path (Join-Path $sdkDirectory "livekitclient.dll")).Path
$tempRoot = Join-Path $env:TEMP ("livekit-python-smoke-" + [guid]::NewGuid().ToString("N"))
$configPath = Join-Path $tempRoot "server.yaml"
$serverStdout = Join-Path $tempRoot "server.stdout.log"
$serverStderr = Join-Path $tempRoot "server.stderr.log"
$peerStdout = Join-Path $tempRoot "peer.stdout.log"
$peerStderr = Join-Path $tempRoot "peer.stderr.log"
$apiKey = "python-smoke"
$secretBytes = New-Object byte[] 32
$generator = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $generator.GetBytes($secretBytes) } finally { $generator.Dispose() }
$apiSecret = ([BitConverter]::ToString($secretBytes)).Replace("-", "").ToLowerInvariant()
$roomName = "python-smoke-" + [guid]::NewGuid().ToString("N")
$server = $null
$peer = $null

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
    -RedirectStandardOutput $serverStdout -RedirectStandardError $serverStderr `
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
    } catch {
      # Wait for the server to open its local port.
    } finally {
      $client.Dispose()
    }
    Start-Sleep -Milliseconds 100
  }
  if (-not $ready) { throw "Timed out waiting for LiveKit server" }

  $env:LIVEKIT_URL = "ws://127.0.0.1:$Port"
  $env:LIVEKIT_TOKEN_A = & $lkExe --quiet --url "http://127.0.0.1:$Port" `
    --api-key $apiKey --api-secret $apiSecret token create `
    --room $roomName --identity python-a --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Failed to generate participant A token" }
  $env:LIVEKIT_TOKEN_B = & $lkExe --quiet --url "http://127.0.0.1:$Port" `
    --api-key $apiKey --api-secret $apiSecret token create `
    --room $roomName --identity python-b --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Failed to generate participant B token" }
  $env:LIVEKIT_CLIENT_CPP_LIBRARY = $sdkDll
  $env:LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN_A
  $env:PYTHONPATH = Join-Path $repoRoot "pkg/python"
  $env:Path = "$sdkDirectory;$env:Path"
  if ($Example -in @("receive_media", "receive_media_events", "record_remote", "rpc")) {
    $peerExample = if ($Example -eq "rpc") { "rpc_receiver" } else { $Example }
    $peerArgs = @("-u", (Join-Path $PSScriptRoot "$peerExample.py"), "--seconds", "8")
    if ($Example -eq "record_remote") { $peerArgs += $tempRoot }
    $env:LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN_B
    $peer = Start-Process -FilePath (Get-Command python).Source -ArgumentList $peerArgs `
      -RedirectStandardOutput $peerStdout -RedirectStandardError $peerStderr `
      -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    $env:LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN_A
    if ($Example -eq "rpc") {
      & python -u (Join-Path $PSScriptRoot "rpc_caller.py") python-b
    } else {
      $publisher = if ($Example -eq "record_remote") { "publish_audio" } else { "publish_video" }
      & python -u (Join-Path $PSScriptRoot "$publisher.py") --seconds 3
    }
    if ($LASTEXITCODE -ne 0) { throw "Publisher or RPC caller failed" }
    if (-not $peer.WaitForExit(15000)) { throw "Receiver did not finish" }
    $peer.Refresh()
    Get-Content -LiteralPath $peerStdout
    if ([int]$peer.ExitCode -ne 0) {
      Get-Content -LiteralPath $peerStderr
      throw "Receiver failed"
    }
  } else {
    & python -u (Join-Path $PSScriptRoot "$Example.py")
    if ($LASTEXITCODE -ne 0) { throw "Python smoke test failed" }
  }
} finally {
  if ($null -ne $peer -and -not $peer.HasExited) {
    Stop-Process -Id $peer.Id
    $peer.WaitForExit(10000) | Out-Null
  }
  if ($null -ne $server -and -not $server.HasExited) {
    Stop-Process -Id $server.Id
    $server.WaitForExit(10000) | Out-Null
  }
  Remove-Item Env:LIVEKIT_TOKEN, Env:LIVEKIT_TOKEN_A, Env:LIVEKIT_TOKEN_B -ErrorAction SilentlyContinue
  foreach ($path in @($configPath, $serverStdout, $serverStderr, $peerStdout, $peerStderr)) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
  }
  if ($Example -eq "record_remote") {
    Get-ChildItem -LiteralPath $tempRoot -File | Remove-Item -Force
  }
  if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot }
}
