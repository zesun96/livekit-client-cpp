param(
  [string]$ServerExecutable = "E:\workspace\cpp\lk-sdk\others\livekit-server\bin\livekit-server.exe",
  [string]$LkExecutable = "E:\workspace\cpp\lk-sdk\others\lk_2.18.2_windows_amd64\lk.exe",
  [int]$Port = 17891
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$native = Join-Path $root "out/build/sdk-dll-md-release/Release/livekitclient.dll"
$dotnet = "C:\Program Files\dotnet\dotnet.exe"
foreach ($path in @($ServerExecutable, $LkExecutable, $native, $dotnet)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing dependency: $path" }
}

$work = Join-Path $root "out/dotnet-example-smoke"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$config = Join-Path $work "livekit.yaml"
$apiKey = "dotnet-example"
$secretBytes = [byte[]]::new(32)
$random = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $random.GetBytes($secretBytes) } finally { $random.Dispose() }
$apiSecret = [BitConverter]::ToString($secretBytes).Replace("-", "")
$roomName = "dotnet-example-" + [guid]::NewGuid().ToString("N")
@(
  "port: $Port"
  "enable_participant_data_blob: true"
  "rtc:"
  "  tcp_port: $($Port + 1)"
  "  udp_port: $($Port + 2)"
  "  use_external_ip: false"
  "keys:"
  "  '$apiKey': '$apiSecret'"
  "logging:"
  "  level: warn"
) | Set-Content -LiteralPath $config -Encoding utf8

$server = $null
try {
  $server = Start-Process -FilePath $ServerExecutable `
    -ArgumentList @("--bind", "127.0.0.1", "--node-ip", "127.0.0.1", "--config", $config) `
    -RedirectStandardOutput (Join-Path $work "server.stdout.log") `
    -RedirectStandardError (Join-Path $work "server.stderr.log") `
    -WindowStyle Hidden -PassThru
  $ready = $false
  for ($attempt = 0; $attempt -lt 100; $attempt++) {
    if ($server.HasExited) { throw "LiveKit server exited during startup" }
    $client = [Net.Sockets.TcpClient]::new()
    try {
      $connect = $client.ConnectAsync("127.0.0.1", $Port)
      if ($connect.Wait(200) -and $client.Connected) { $ready = $true; break }
    } catch { } finally { $client.Dispose() }
    Start-Sleep -Milliseconds 100
  }
  if (-not $ready) { throw "LiveKit server did not start" }

  $env:LIVEKIT_URL = "ws://127.0.0.1:$Port"
  $env:LIVEKIT_CLIENT_CPP_LIBRARY = $native
  $env:LIVEKIT_E2EE_KEY = $apiSecret
  $env:DOTNET_CLI_HOME = Join-Path $root "out/dotnet-cli-home"
  $env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
  $env:LIVEKIT_TOKEN = & $LkExecutable token create --url $env:LIVEKIT_URL `
    --api-key $apiKey --api-secret $apiSecret --room $roomName --identity dotnet-example `
    --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Could not create example token" }
  $receiverToken = $env:LIVEKIT_TOKEN
  & $dotnet build (Join-Path $PSScriptRoot "RoomEcho/RoomEcho.csproj") --configuration Release
  if ($LASTEXITCODE -ne 0) { throw "Could not build RoomEcho" }
  & $dotnet build (Join-Path $PSScriptRoot "FeatureExamples/FeatureExamples.csproj") --configuration Release
  if ($LASTEXITCODE -ne 0) { throw "Could not build FeatureExamples" }

  $roomEcho = Join-Path $PSScriptRoot "RoomEcho/bin/Release/net8.0/RoomEcho.dll"
  $features = Join-Path $PSScriptRoot "FeatureExamples/bin/Release/net8.0/FeatureExamples.dll"
  $cases = @(
    @{ Name = "room-echo"; Assembly = $roomEcho; Arguments = @("--seconds", "0.5") }
    @{ Name = "data-transfer"; Assembly = $features; Arguments = @("data-transfer") }
    @{ Name = "stream-writer"; Assembly = $features; Arguments = @("stream-writer") }
    @{ Name = "token-source"; Assembly = $features; Arguments = @("token-source", "--seconds", "0.5") }
    @{ Name = "room-events"; Assembly = $features; Arguments = @("room-events", "--seconds", "0.5") }
    @{ Name = "rpc-receiver"; Assembly = $features; Arguments = @("rpc-receiver", "--seconds", "0.5") }
    @{ Name = "e2ee"; Assembly = $features; Arguments = @("e2ee", "--seconds", "0.5") }
    @{ Name = "data-track-schema"; Assembly = $features; Arguments = @("data-track-schema") }
    @{ Name = "publish-audio"; Assembly = $features; Arguments = @("publish-audio", "--seconds", "0.5") }
    @{ Name = "publish-video"; Assembly = $features; Arguments = @("publish-video", "--seconds", "0.5") }
    @{ Name = "receive-media"; Assembly = $features; Arguments = @("receive-media", "--seconds", "0.5") }
    @{ Name = "receive-media-events"; Assembly = $features; Arguments = @("receive-media-events", "--seconds", "0.5") }
    @{ Name = "record-remote"; Assembly = $features; Arguments = @("record-remote", "--output-dir", $work, "--seconds", "0.5") }
  )
  foreach ($case in $cases) {
    $stdout = Join-Path $work ($case.Name + ".stdout.log")
    $stderr = Join-Path $work ($case.Name + ".stderr.log")
    $arguments = @(('"' + $case.Assembly + '"')) + $case.Arguments
    $process = Start-Process -FilePath $dotnet -ArgumentList $arguments `
      -WorkingDirectory $root -RedirectStandardOutput $stdout `
      -RedirectStandardError $stderr -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
      Get-Content -LiteralPath $stderr -Tail 35 -ErrorAction SilentlyContinue
      throw "Example $($case.Name) failed with exit code $($process.ExitCode)"
    }
    Write-Host "PASS $($case.Name)"
  }

  $receiverStdout = Join-Path $work "rpc-pair-receiver.stdout.log"
  $receiverStderr = Join-Path $work "rpc-pair-receiver.stderr.log"
  $receiver = Start-Process -FilePath $dotnet `
    -ArgumentList @(('"' + $features + '"'), "rpc-receiver", "--seconds", "5") `
    -WorkingDirectory $root -RedirectStandardOutput $receiverStdout `
    -RedirectStandardError $receiverStderr -WindowStyle Hidden -PassThru
  try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
      if ($receiver.HasExited) { throw "RPC receiver exited before caller started" }
      if (Select-String -LiteralPath $receiverStdout -Pattern '^RPC receiver identity:' -Quiet `
          -ErrorAction SilentlyContinue) { $ready = $true; break }
      Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw "RPC receiver did not become ready" }
    $env:LIVEKIT_TOKEN = & $LkExecutable token create --url $env:LIVEKIT_URL `
      --api-key $apiKey --api-secret $apiSecret --room $roomName --identity dotnet-caller `
      --join --token-only
    if ($LASTEXITCODE -ne 0) { throw "Could not create caller token" }
    $callerStdout = Join-Path $work "rpc-pair-caller.stdout.log"
    $callerStderr = Join-Path $work "rpc-pair-caller.stderr.log"
    $caller = Start-Process -FilePath $dotnet `
      -ArgumentList @(('"' + $features + '"'), "rpc-caller", "--destination", "dotnet-example", "--payload", "hello") `
      -WorkingDirectory $root -RedirectStandardOutput $callerStdout `
      -RedirectStandardError $callerStderr -WindowStyle Hidden -Wait -PassThru
    if ($caller.ExitCode -ne 0 -or -not (Select-String -LiteralPath $callerStdout `
        -Pattern '^RPC response: hello$' -Quiet)) {
      Get-Content -LiteralPath $callerStderr -Tail 35 -ErrorAction SilentlyContinue
      throw "RPC caller example failed"
    }
    Write-Host "PASS rpc-caller"
  } finally {
    if (-not $receiver.HasExited) {
      Stop-Process -Id $receiver.Id
      $receiver.WaitForExit(10000) | Out-Null
    }
  }

  $callerToken = $env:LIVEKIT_TOKEN
  $env:LIVEKIT_TOKEN = $receiverToken
  $mediaStdout = Join-Path $work "media-pair-receiver.stdout.log"
  $mediaStderr = Join-Path $work "media-pair-receiver.stderr.log"
  $mediaReceiver = Start-Process -FilePath $dotnet `
    -ArgumentList @(('"' + $features + '"'), "receive-media", "--seconds", "5") `
    -WorkingDirectory $root -RedirectStandardOutput $mediaStdout `
    -RedirectStandardError $mediaStderr -WindowStyle Hidden -PassThru
  try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
      if ($mediaReceiver.HasExited) { throw "Media receiver exited before publisher started" }
      if (Select-String -LiteralPath $mediaStdout -Pattern '^Receiving media as ' -Quiet `
          -ErrorAction SilentlyContinue) { $ready = $true; break }
      Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw "Media receiver did not become ready" }
    $env:LIVEKIT_TOKEN = $callerToken
    $publisherStdout = Join-Path $work "media-pair-publisher.stdout.log"
    $publisherStderr = Join-Path $work "media-pair-publisher.stderr.log"
    $publisher = Start-Process -FilePath $dotnet `
      -ArgumentList @(('"' + $features + '"'), "publish-video", "--seconds", "2") `
      -WorkingDirectory $root -RedirectStandardOutput $publisherStdout `
      -RedirectStandardError $publisherStderr -WindowStyle Hidden -Wait -PassThru
    if ($publisher.ExitCode -ne 0) {
      Get-Content -LiteralPath $publisherStderr -Tail 35 -ErrorAction SilentlyContinue
      throw "Media publisher example failed"
    }
    $mediaReceiver.WaitForExit(10000) | Out-Null
    if (-not (Select-String -LiteralPath $mediaStdout -Pattern '^Video ' -Quiet)) {
      Get-Content -LiteralPath $mediaStderr -Tail 35 -ErrorAction SilentlyContinue
      throw "Media receiver did not read video frames"
    }
    Write-Host "PASS receive-media with publish-video"
  } finally {
    if (-not $mediaReceiver.HasExited) {
      Stop-Process -Id $mediaReceiver.Id
      $mediaReceiver.WaitForExit(10000) | Out-Null
    }
  }
} finally {
  if ($null -ne $server -and -not $server.HasExited) {
    Stop-Process -Id $server.Id
    $server.WaitForExit(10000) | Out-Null
  }
  Remove-Item Env:LIVEKIT_TOKEN -ErrorAction SilentlyContinue
  Remove-Item Env:LIVEKIT_E2EE_KEY -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $config -ErrorAction SilentlyContinue
  [Array]::Clear($secretBytes, 0, $secretBytes.Length)
}
