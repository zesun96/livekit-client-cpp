param(
  [int]$Port = 17885,
  [string]$NativeDirectory = "out/build/java-sdk-static-crt/Release",
  [string]$BridgeDirectory = "out/build/java-binding/Release",
  [ValidateSet("two-peer-smoke", "media-devices", "room-echo", "room-events",
    "data-transfer", "stream-writer", "data-track-schema", "token-source", "e2ee",
    "publish-audio", "publish-video", "receive-media", "receive-media-events",
    "record-remote", "rpc")]
  [string]$Example = "two-peer-smoke"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$workspaceRoot = (Resolve-Path (Join-Path $repoRoot "..")).Path
$serverExe = (Resolve-Path (Join-Path $workspaceRoot "others/livekit-server/bin/livekit-server.exe")).Path
$lkExe = (Resolve-Path (Join-Path $workspaceRoot "others/lk_2.18.2_windows_amd64/lk.exe")).Path
$sdkDirectory = (Resolve-Path (Join-Path $repoRoot $NativeDirectory)).Path
$bridgeDirectory = (Resolve-Path (Join-Path $repoRoot $BridgeDirectory)).Path
$sdkDll = (Resolve-Path (Join-Path $sdkDirectory "livekitclient.dll")).Path
$bridgeDll = (Resolve-Path (Join-Path $bridgeDirectory "livekitclient_java.dll")).Path
$classes = Join-Path $repoRoot "out/build/java-example-classes"
$temporary = Join-Path $env:TEMP ("livekit-java-smoke-" + [guid]::NewGuid().ToString("N"))
$configPath = Join-Path $temporary "server.yaml"
$stdoutPath = Join-Path $temporary "server.stdout.log"
$stderrPath = Join-Path $temporary "server.stderr.log"
$peerStdout = Join-Path $temporary "peer.stdout.log"
$peerStderr = Join-Path $temporary "peer.stderr.log"
$apiKey = "java-smoke"
$secretBytes = New-Object byte[] 32
$generator = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $generator.GetBytes($secretBytes) } finally { $generator.Dispose() }
$apiSecret = ([BitConverter]::ToString($secretBytes)).Replace("-", "").ToLowerInvariant()
$roomName = "java-smoke-" + [guid]::NewGuid().ToString("N")
$server = $null
$peer = $null

New-Item -ItemType Directory -Path $temporary | Out-Null
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
    } catch {
      # The local server is still starting.
    } finally {
      $client.Dispose()
    }
    Start-Sleep -Milliseconds 100
  }
  if (-not $ready) { throw "Timed out waiting for LiveKit server" }

  $env:LIVEKIT_URL = "ws://127.0.0.1:$Port"
  $env:LIVEKIT_TOKEN_A = & $lkExe --quiet --url "http://127.0.0.1:$Port" `
    --api-key $apiKey --api-secret $apiSecret token create `
    --room $roomName --identity java-a --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Failed to generate participant A token" }
  $env:LIVEKIT_TOKEN_B = & $lkExe --quiet --url "http://127.0.0.1:$Port" `
    --api-key $apiKey --api-secret $apiSecret token create `
    --room $roomName --identity java-b --join --token-only
  if ($LASTEXITCODE -ne 0) { throw "Failed to generate participant B token" }
  $env:LIVEKIT_CLIENT_CPP_LIBRARY = $sdkDll
  $env:LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN_A
  $env:LIVEKIT_E2EE_KEY = $apiSecret
  $env:LIVEKIT_EXAMPLE_SECONDS = "2"
  $env:Path = "$sdkDirectory;$env:Path"
  $sources = rg --files (Join-Path $repoRoot "pkg/java/src/main/java") `
    (Join-Path $repoRoot "pkg/java/examples/java") | Where-Object { $_ -like "*.java" }
  if (-not $sources) { throw "No Java sources found" }
  New-Item -ItemType Directory -Path $classes -Force | Out-Null
  & javac -d $classes $sources
  if ($LASTEXITCODE -ne 0) { throw "Java example compilation failed" }
  if ($Example -eq "two-peer-smoke") {
    & java -Xcheck:jni "-Dlivekit.java.library=$bridgeDll" -cp $classes `
      io.livekit.client.examples.TwoPeerSmoke
  } elseif ($Example -in @("receive-media", "receive-media-events", "record-remote", "rpc")) {
    $peerExample = if ($Example -eq "rpc") { "rpc-receiver" } else { $Example }
    $peerArgs = @("-Xcheck:jni", "-Dlivekit.java.library=$bridgeDll", "-cp", $classes,
      "io.livekit.client.examples.Examples", $peerExample)
    if ($Example -eq "record-remote") { $peerArgs += $temporary }
    $env:LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN_B
    $env:LIVEKIT_EXAMPLE_SECONDS = "6"
    $peer = Start-Process -FilePath (Get-Command java).Source -ArgumentList $peerArgs `
      -RedirectStandardOutput $peerStdout -RedirectStandardError $peerStderr `
      -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    $env:LIVEKIT_TOKEN = $env:LIVEKIT_TOKEN_A
    $env:LIVEKIT_EXAMPLE_SECONDS = "3"
    if ($Example -eq "rpc") {
      & java -Xcheck:jni "-Dlivekit.java.library=$bridgeDll" -cp $classes `
        io.livekit.client.examples.Examples rpc-caller java-b hello
    } else {
      $publisher = if ($Example -eq "record-remote") { "publish-audio" } else { "publish-video" }
      & java -Xcheck:jni "-Dlivekit.java.library=$bridgeDll" -cp $classes `
        io.livekit.client.examples.Examples $publisher
    }
    if ($LASTEXITCODE -ne 0) { throw "Java publisher or RPC caller failed" }
    if (-not $peer.WaitForExit(15000)) { throw "Java receiver timed out" }
    $peer.Refresh()
    Get-Content -LiteralPath $peerStdout
    if ([int]$peer.ExitCode -ne 0) {
      Get-Content -LiteralPath $peerStderr
      throw "Java receiver failed"
    }
  } else {
    & java -Xcheck:jni "-Dlivekit.java.library=$bridgeDll" -cp $classes `
      io.livekit.client.examples.Examples $Example
  }
  if ($LASTEXITCODE -ne 0) { throw "Java example failed: $Example" }
} catch {
  if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Tail 30 }
  throw
} finally {
  if ($null -ne $peer -and -not $peer.HasExited) {
    Stop-Process -Id $peer.Id
    $peer.WaitForExit(10000) | Out-Null
  }
  if ($null -ne $server -and -not $server.HasExited) {
    Stop-Process -Id $server.Id
    $server.WaitForExit(10000) | Out-Null
  }
  Remove-Item Env:LIVEKIT_TOKEN, Env:LIVEKIT_TOKEN_A, Env:LIVEKIT_TOKEN_B, `
    Env:LIVEKIT_E2EE_KEY -ErrorAction SilentlyContinue
  foreach ($path in @($configPath, $stdoutPath, $stderrPath, $peerStdout, $peerStderr)) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
  }
  if ($Example -eq "record-remote") {
    Get-ChildItem -LiteralPath $temporary -File | Remove-Item -Force
  }
  if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
