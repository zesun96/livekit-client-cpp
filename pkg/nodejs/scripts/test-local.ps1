param(
    [ValidateSet('Basic', 'Full', 'Audio')]
    [string]$Scenario = 'Full',
    [ValidateRange(1024, 65000)]
    [int]$Port = 17935,
    [int]$TimeoutSeconds = 180,
    [string]$AddonPath = '',
    [string]$PackagePath = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$workspaceRoot = Split-Path $repoRoot -Parent
$serverExe = (Resolve-Path (Join-Path $workspaceRoot 'others/livekit-server/bin/livekit-server.exe')).Path
$lkExe = (Resolve-Path (Join-Path $workspaceRoot 'others/lk_2.18.2_windows_amd64/lk.exe')).Path
$nodeExe = (Get-Command node).Source
$runDirectory = Join-Path $repoRoot ('out/node-validation/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $runDirectory | Out-Null
$configPath = Join-Path $runDirectory 'server.yaml'
$serverOut = Join-Path $runDirectory 'server.stdout.log'
$serverErr = Join-Path $runDirectory 'server.stderr.log'
$testOut = Join-Path $runDirectory 'tests.tap'
$testErr = Join-Path $runDirectory 'tests.stderr.log'
$savedEnvironment = @{}
$names = @('LIVEKIT_URL', 'LIVEKIT_TOKEN', 'LIVEKIT_SECOND_TOKEN', 'LIVEKIT_NODE_INTEGRATION',
    'LIVEKIT_NODE_AUDIO', 'LIVEKIT_NODE_E2EE_TOKEN', 'LIVEKIT_NODE_E2EE_SECOND_TOKEN',
    'LIVEKIT_NODE_E2EE_KEY', 'LIVEKIT_NODE_TEST_OUTPUT_DIR', 'LIVEKIT_NODE_ADDON', 'LIVEKIT_NODE_TEST_PACKAGE')
foreach ($name in $names) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$server = $null
$testProcess = $null
$secretBytes = New-Object byte[] 32
$generator = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $generator.GetBytes($secretBytes) } finally { $generator.Dispose() }
$apiSecret = ([BitConverter]::ToString($secretBytes)).Replace('-', '').ToLowerInvariant()
$apiKey = 'node-sdk-test'
$roomName = 'node-sdk-' + [guid]::NewGuid().ToString('N')
try {
    # Refuse to reuse a listener belonging to another task.
    foreach ($candidate in @($Port, ($Port + 1))) {
        $probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $candidate)
        try { $probe.Start() } finally { $probe.Stop() }
    }
    $config = @(
        "port: $Port", 'keys:', "  ${apiKey}: ${apiSecret}",
        'enable_participant_data_blob: true', 'rtc:', "  tcp_port: $($Port + 1)",
        "  udp_port: $($Port + 2)", '  use_external_ip: false', 'logging:', '  level: warn'
    ) -join [Environment]::NewLine
    [IO.File]::WriteAllText($configPath, $config, [Text.UTF8Encoding]::new($false))
    $server = Start-Process -FilePath $serverExe -ArgumentList @('--bind', '127.0.0.1', '--node-ip', '127.0.0.1', '--config', ('"' + $configPath + '"')) `
        -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -WindowStyle Hidden -PassThru
    $ready = $false
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($server.HasExited) { throw 'Local LiveKit server exited during startup' }
        $client = [Net.Sockets.TcpClient]::new()
        try {
            if ($client.ConnectAsync('127.0.0.1', $Port).Wait(100) -and $client.Connected) { $ready = $true; break }
        } catch { } finally { $client.Dispose() }
        Start-Sleep -Milliseconds 100
    }
    if (!$ready) { throw 'Timed out waiting for local LiveKit server' }
    $env:LIVEKIT_URL = "ws://127.0.0.1:$Port"
    $env:LIVEKIT_NODE_INTEGRATION = '1'
    $env:LIVEKIT_NODE_AUDIO = if ($Scenario -eq 'Audio') { '1' } else { '0' }
    $env:LIVEKIT_NODE_TEST_OUTPUT_DIR = $runDirectory
    if ($AddonPath) { $env:LIVEKIT_NODE_ADDON = (Resolve-Path -LiteralPath $AddonPath).Path }
    if ($PackagePath) { $env:LIVEKIT_NODE_TEST_PACKAGE = (Resolve-Path -LiteralPath $PackagePath).Path }
    function New-ParticipantToken([string]$Room, [string]$Identity) {
        # Windows PowerShell represents native stderr warnings as nonterminating ErrorRecords.
        $ErrorActionPreference = 'Continue'
        $token = & $lkExe --quiet --url "http://127.0.0.1:$Port" --api-key $apiKey --api-secret $apiSecret `
            token create --room $Room --identity $Identity --join --allow-update-metadata --valid-for 10m --token-only 2> (Join-Path $runDirectory 'cli.stderr.log')
        if ($LASTEXITCODE -ne 0) { throw 'Failed to generate temporary test participant token' }
        return ($token -join '').Trim()
    }
    $env:LIVEKIT_TOKEN = New-ParticipantToken $roomName 'node-a'
    $env:LIVEKIT_SECOND_TOKEN = New-ParticipantToken $roomName 'node-b'
    $env:LIVEKIT_NODE_E2EE_TOKEN = New-ParticipantToken ($roomName + '-e2ee') 'node-e2ee-a'
    $env:LIVEKIT_NODE_E2EE_SECOND_TOKEN = New-ParticipantToken ($roomName + '-e2ee') 'node-e2ee-b'
    $env:LIVEKIT_NODE_E2EE_KEY = [Convert]::ToBase64String($secretBytes)
    $suite = if ($Scenario -eq 'Basic') { 'two-peer.js' } elseif ($Scenario -eq 'Audio') { 'audio.js' } else { 'full.js' }
    $testFile = Join-Path $repoRoot "pkg/nodejs/test/integration/$suite"
    $testProcess = Start-Process -FilePath $nodeExe -ArgumentList @('--test', '--test-concurrency=1', ('"' + $testFile + '"')) `
        -WorkingDirectory $repoRoot -RedirectStandardOutput $testOut -RedirectStandardError $testErr -WindowStyle Hidden -PassThru
    $null = $testProcess.Handle
    if (!$testProcess.WaitForExit($TimeoutSeconds * 1000)) { throw "Node.js $Scenario integration test timed out" }
    $testProcess.WaitForExit()
    $testExitCode = $testProcess.ExitCode
    Get-Content -LiteralPath $testOut
    if ((Get-Item -LiteralPath $testErr).Length) { Get-Content -LiteralPath $testErr }
    if ($null -eq $testExitCode -or $testExitCode -ne 0) { throw "Node.js $Scenario integration test failed (exit $testExitCode)" }
} finally {
    foreach ($process in @($testProcess, $server)) {
        if ($null -ne $process -and !$process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null }
    }
    # Remove temporary signing credentials; keep credential-free test logs and generated media.
    if (Test-Path -LiteralPath $configPath) { Remove-Item -LiteralPath $configPath -Force }
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    Write-Output "Test artifacts: $runDirectory"
}
