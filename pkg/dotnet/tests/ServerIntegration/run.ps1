param(
  [string]$ServerExecutable = "E:\workspace\cpp\lk-sdk\others\livekit-server\bin\livekit-server.exe",
  [string]$LkExecutable = "E:\workspace\cpp\lk-sdk\others\lk_2.18.2_windows_amd64\lk.exe",
  [int]$Port = 17890
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../../../..")).Path
$native = Join-Path $root "out/build/sdk-dll-md-release/Release/livekitclient.dll"
$dotnet = "C:\Program Files\dotnet\dotnet.exe"
foreach ($path in @($ServerExecutable, $LkExecutable, $native, $dotnet)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing test dependency: $path" }
}

$work = Join-Path $root "out/dotnet-server-integration"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$config = Join-Path $work "livekit.yaml"
$stdout = Join-Path $work "server.stdout.log"
$stderr = Join-Path $work "server.stderr.log"
$testStdout = Join-Path $work "test.stdout.log"
$testStderr = Join-Path $work "test.stderr.log"
$apiKey = "dotnet-test"
$secretBytes = [byte[]]::new(32)
$random = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $random.GetBytes($secretBytes) } finally { $random.Dispose() }
$apiSecret = [BitConverter]::ToString($secretBytes).Replace("-", "")
$roomName = "dotnet-" + [guid]::NewGuid().ToString("N")
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
$testProcess = $null
try {
  $server = Start-Process -FilePath $ServerExecutable `
    -ArgumentList @("--bind", "127.0.0.1", "--node-ip", "127.0.0.1", "--config", $config) `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
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
  if (-not $ready) { throw "LiveKit server did not start on port $Port" }

  $env:LIVEKIT_URL = "ws://127.0.0.1:$Port"
  $env:LIVEKIT_CLIENT_CPP_LIBRARY = $native
  $env:DOTNET_CLI_HOME = Join-Path $root "out/dotnet-cli-home"
  $env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
  $env:LIVEKIT_TOKEN_1 = & $LkExecutable token create --url $env:LIVEKIT_URL `
    --api-key $apiKey --api-secret $apiSecret --room $roomName --identity dotnet-first `
    --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Could not create first participant token" }
  $env:LIVEKIT_TOKEN_2 = & $LkExecutable token create --url $env:LIVEKIT_URL `
    --api-key $apiKey --api-secret $apiSecret --room $roomName --identity dotnet-second `
    --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Could not create second participant token" }
  & $dotnet build (Join-Path $PSScriptRoot "ServerIntegration.csproj") `
    --configuration Release
  if ($LASTEXITCODE -ne 0) { throw "Could not build .NET integration test" }
  $assembly = Join-Path $PSScriptRoot "bin/Release/net8.0/ServerIntegration.dll"
  $testProcess = Start-Process -FilePath $dotnet -ArgumentList @(('"' + $assembly + '"')) `
    -WorkingDirectory $root -RedirectStandardOutput $testStdout `
    -RedirectStandardError $testStderr -WindowStyle Hidden -PassThru
  if (-not $testProcess.WaitForExit(90000)) {
    Stop-Process -Id $testProcess.Id
    throw "Server .NET binding integration timed out after 90 seconds"
  }
  $testProcess.Refresh()
  $passed = Select-String -LiteralPath $testStdout `
    -Pattern '^Server .NET binding integration passed$' -Quiet
  if ([int]$testProcess.ExitCode -ne 0 -or -not $passed) {
    Get-Content -LiteralPath $testStdout -ErrorAction SilentlyContinue
    Get-Content -LiteralPath $testStderr -Tail 80 -ErrorAction SilentlyContinue
    throw "Server .NET binding integration failed"
  }
  Get-Content -LiteralPath $testStdout
} finally {
  if ($null -ne $testProcess -and -not $testProcess.HasExited) {
    Stop-Process -Id $testProcess.Id
    $testProcess.WaitForExit(10000) | Out-Null
  }
  if ($null -ne $server -and -not $server.HasExited) {
    Stop-Process -Id $server.Id
    $server.WaitForExit(10000) | Out-Null
  }
  Remove-Item Env:LIVEKIT_TOKEN_1, Env:LIVEKIT_TOKEN_2 -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $config -ErrorAction SilentlyContinue
}
