param(
    [int]$Port = 19080,
    [string]$BuildDirectory = 'out/build/sdk-dll-md-release/Release',
    [switch]$SkipBuild,
    [switch]$IncludeHardware,
    [int]$TestTimeoutSeconds = 120,
    [string]$TestFilter = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$workspaceRoot = (Resolve-Path (Join-Path $repoRoot '..')).Path
$native = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$serverExe = (Resolve-Path (Join-Path $workspaceRoot 'others/livekit-server/bin/livekit-server.exe')).Path
$manifest = Join-Path $PSScriptRoot '../Cargo.toml'
$saved = @{}
foreach ($name in @('Path', 'BUILD_INTEGRATION_TESTS', 'LIVEKIT_URL', 'LIVEKIT_TOKEN', 'LIVEKIT_TOKEN_B', 'LIVEKIT_E2EE_KEY', 'LIVEKIT_HARDWARE_TESTS', 'LIVEKIT_RUST_TEST_OUTPUT')) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$server = $null
$testProcess = $null
$runDirectory = Join-Path $repoRoot ('out/rust-smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$configPath = Join-Path $runDirectory 'server.yaml'
function ConvertTo-Base64Url([byte[]]$Bytes) { [Convert]::ToBase64String($Bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_') }
function New-TestToken([string]$Identity) {
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $header = ConvertTo-Base64Url ([Text.Encoding]::UTF8.GetBytes('{"alg":"HS256","typ":"JWT"}'))
    $payload = @{
        iss = $apiKey; sub = $Identity; nbf = $now - 10; exp = $now + 1800
        video = @{ roomJoin = $true; room = $roomName; canPublish = $true; canSubscribe = $true; canPublishData = $true; canUpdateOwnMetadata = $true }
    } | ConvertTo-Json -Depth 4 -Compress
    $body = $header + '.' + (ConvertTo-Base64Url ([Text.Encoding]::UTF8.GetBytes($payload)))
    $hmac = New-Object Security.Cryptography.HMACSHA256
    try {
        $hmac.Key = [Text.Encoding]::UTF8.GetBytes($apiSecret)
        $body + '.' + (ConvertTo-Base64Url ($hmac.ComputeHash([Text.Encoding]::UTF8.GetBytes($body))))
    } finally { $hmac.Dispose() }
}
try {
    $dependencyDirectory = Join-Path (Split-Path $native -Parent) 'Release'
    $env:Path = "$native;$dependencyDirectory;$($saved['Path'])"
    if (-not $SkipBuild) {
        & cargo test --manifest-path $manifest --locked --test two_peer --no-run
        if ($LASTEXITCODE -ne 0) { throw 'Rust integration test build failed' }
    }
    $testExe = Get-ChildItem (Join-Path $PSScriptRoot '../target/debug/deps') -Filter 'two_peer-*.exe' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $testExe) { throw 'Build the Rust two_peer test first' }
    $secretBytes = New-Object byte[] 32
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($secretBytes) } finally { $rng.Dispose() }
    $apiSecret = ([BitConverter]::ToString($secretBytes)).Replace('-', '').ToLowerInvariant()
    $apiKey = 'rust-smoke'
    $roomName = 'rust-smoke-' + [guid]::NewGuid().ToString('N')
    $config = @("port: $Port", 'keys:', "  ${apiKey}: ${apiSecret}", 'enable_participant_data_blob: true', 'rtc:', "  tcp_port: $($Port + 1)", "  udp_port: $($Port + 2)", '  use_external_ip: false') -join "`n"
    [IO.File]::WriteAllText($configPath, $config, [Text.UTF8Encoding]::new($false))
    $server = Start-Process -FilePath $serverExe -ArgumentList @('--bind', '127.0.0.1', '--node-ip', '127.0.0.1', '--config', $configPath) `
        -RedirectStandardOutput (Join-Path $runDirectory 'server.stdout.log') -RedirectStandardError (Join-Path $runDirectory 'server.stderr.log') `
        -WindowStyle Hidden -PassThru
    $ready = $false
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($server.HasExited) { throw 'LiveKit server exited during startup' }
        $client = New-Object Net.Sockets.TcpClient
        try { if ($client.ConnectAsync('127.0.0.1', $Port).Wait(200) -and $client.Connected) { $ready = $true; break } }
        finally { $client.Dispose() }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw 'Timed out waiting for LiveKit server' }
    $env:BUILD_INTEGRATION_TESTS = 'ON'
    $env:LIVEKIT_URL = "ws://127.0.0.1:$Port"
    $env:LIVEKIT_TOKEN = New-TestToken 'rust-a'
    $env:LIVEKIT_TOKEN_B = New-TestToken 'rust-b'
    $env:LIVEKIT_E2EE_KEY = $apiSecret
    $env:LIVEKIT_HARDWARE_TESTS = $(if ($IncludeHardware) { 'ON' } else { 'OFF' })
    $env:LIVEKIT_RUST_TEST_OUTPUT = $runDirectory
    $testStdout = Join-Path $runDirectory 'test.stdout.log'
    $testStderr = Join-Path $runDirectory 'test.stderr.log'
    $testArguments = @('--ignored', '--nocapture', '--test-threads=1')
    if ($TestFilter) { $testArguments += $TestFilter }
    $testProcess = Start-Process -FilePath $testExe.FullName -ArgumentList $testArguments `
        -RedirectStandardOutput $testStdout -RedirectStandardError $testStderr -WindowStyle Hidden -PassThru
    $testProcess.Handle | Out-Null
    $finished = $testProcess.WaitForExit($TestTimeoutSeconds * 1000)
    if (-not $finished) { Stop-Process -Id $testProcess.Id; $testProcess.WaitForExit(10000) | Out-Null }
    else { $testProcess.WaitForExit(); $testProcess.Refresh() }
    Get-Content -LiteralPath $testStdout
    Get-Content -LiteralPath $testStderr
    if (-not $finished) { throw "Rust integration tests timed out; logs: $runDirectory" }
    if ($testProcess.ExitCode -ne 0) { throw "Rust integration tests failed; logs: $runDirectory" }
    Write-Output "Rust integration tests passed; logs: $runDirectory"
} finally {
    if ($null -ne $testProcess -and -not $testProcess.HasExited) { Stop-Process -Id $testProcess.Id; $testProcess.WaitForExit(10000) | Out-Null }
    if ($null -ne $server -and -not $server.HasExited) { Stop-Process -Id $server.Id; $server.WaitForExit(10000) | Out-Null }
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    if (Test-Path -LiteralPath $configPath) { Remove-Item -LiteralPath $configPath -Force }
}
