import crypto from "node:crypto";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import process from "node:process";
import { spawn } from "node:child_process";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "..", "..");

function argument(name, fallback) {
    const index = process.argv.indexOf(name);
    return index >= 0 && index + 1 < process.argv.length
        ? process.argv[index + 1]
        : fallback;
}

function base64Url(value) {
    return Buffer.from(value).toString("base64url");
}

function participantToken(apiKey, apiSecret, identity, room) {
    const now = Math.floor(Date.now() / 1000);
    const header = base64Url(JSON.stringify({ alg: "HS256", typ: "JWT" }));
    const payload = base64Url(
        JSON.stringify({
            exp: now + 900,
            iss: apiKey,
            nbf: now - 5,
            sub: identity,
            video: { roomJoin: true, room },
        }),
    );
    const unsigned = `${header}.${payload}`;
    const signature = crypto
        .createHmac("sha256", apiSecret)
        .update(unsigned)
        .digest("base64url");
    return `${unsigned}.${signature}`;
}

function credentialsFromConfig(configPath) {
    const config = fs.readFileSync(configPath, "utf8");
    const keys = config.match(/^keys:\s*\r?\n((?:[ \t].*(?:\r?\n|$))+)/m)?.[1];
    const credential = keys?.match(/^\s+([^:#]+):\s*([^#\r\n]+?)\s*$/m);
    if (!credential) {
        throw new Error(`No API credential found under keys: in ${configPath}`);
    }
    const clean = (value) => value.trim().replace(/^['"]|['"]$/g, "");
    return { apiKey: clean(credential[1]), apiSecret: clean(credential[2]) };
}

function deferred() {
    let resolve;
    let reject;
    const promise = new Promise((onResolve, onReject) => {
        resolve = onResolve;
        reject = onReject;
    });
    return { promise, resolve, reject };
}

function timeout(promise, milliseconds, description) {
    return Promise.race([
        promise,
        new Promise((_, reject) =>
            setTimeout(
                () =>
                    reject(
                        new Error(
                            `${description} timed out after ${milliseconds} ms`,
                        ),
                    ),
                milliseconds,
            ),
        ),
    ]);
}

async function main() {
    const officialSdkRoot = path.resolve(
        argument(
            "--official-js-sdk",
            path.resolve(repositoryRoot, "..", "client-sdk-js"),
        ),
    );
    const configPath = path.resolve(
        argument(
            "--config",
            "E:\\workspace\\livekit\\bin\\livekit_1.13.5_windows_amd64\\config-dev.yaml",
        ),
    );
    const testExecutable = path.resolve(
        argument(
            "--test-executable",
            path.join(
                repositoryRoot,
                "out",
                "build",
                "vs2022-x64-release",
                "test",
                "integration",
                "Release",
                "livekit_server_integration_tests.exe",
            ),
        ),
    );
    const chromeExecutable = path.resolve(
        argument(
            "--chrome",
            "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        ),
    );
    const cppUrl = argument("--cpp-url", "http://127.0.0.1:7980/rtc");
    const jsUrl = argument("--js-url", "ws://127.0.0.1:7980");
    const requiredFiles = [
        configPath,
        testExecutable,
        chromeExecutable,
        path.join(officialSdkRoot, "dist", "livekit-client.umd.js"),
        path.join(officialSdkRoot, "dist", "livekit-client.e2ee.worker.js"),
        path.join(officialSdkRoot, "node_modules", "playwright"),
    ];
    for (const required of requiredFiles) {
        if (!fs.existsSync(required)) {
            throw new Error(
                `Required interoperability input does not exist: ${required}`,
            );
        }
    }

    const require = createRequire(import.meta.url);
    const { chromium } = require(
        path.join(officialSdkRoot, "node_modules", "playwright"),
    );
    const { apiKey, apiSecret } = credentialsFromConfig(configPath);
    const room = `cpp-official-js-e2ee-${Date.now()}`;
    const cppIdentity = "cpp-e2ee-interop";
    const jsIdentity = "official-js-e2ee-interop";
    const cppToken = participantToken(apiKey, apiSecret, cppIdentity, room);
    const jsToken = participantToken(apiKey, apiSecret, jsIdentity, room);
    const ready = deferred();
    const result = deferred();
    const peerHtml = fs.readFileSync(
        path.join(scriptDirectory, "official_js_e2ee_peer.html"),
    );

    const server = http.createServer((request, response) => {
        const send = (status, contentType, body) => {
            response.writeHead(status, {
                "content-type": contentType,
                "cache-control": "no-store",
            });
            response.end(body);
        };
        if (request.method === "GET" && request.url === "/") {
            send(200, "text/html; charset=utf-8", peerHtml);
            return;
        }
        const assets = {
            "/official/livekit-client.umd.js": path.join(
                officialSdkRoot,
                "dist",
                "livekit-client.umd.js",
            ),
            "/official/livekit-client.e2ee.worker.js": path.join(
                officialSdkRoot,
                "dist",
                "livekit-client.e2ee.worker.js",
            ),
        };
        if (request.method === "GET" && assets[request.url]) {
            send(
                200,
                "text/javascript; charset=utf-8",
                fs.readFileSync(assets[request.url]),
            );
            return;
        }
        if (
            request.method === "POST" &&
            (request.url === "/ready" || request.url === "/result")
        ) {
            const chunks = [];
            request.on("data", (chunk) => chunks.push(chunk));
            request.on("end", () => {
                try {
                    const body = JSON.parse(
                        Buffer.concat(chunks).toString("utf8"),
                    );
                    if (request.url === "/ready") {
                        ready.resolve(body);
                    } else {
                        result.resolve(body);
                    }
                    send(204, "text/plain", "");
                } catch (error) {
                    send(400, "text/plain", String(error));
                }
            });
            return;
        }
        send(404, "text/plain", "not found");
    });
    await new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", resolve);
    });

    let browser;
    let testProcess;
    try {
        const port = server.address().port;
        browser = await chromium.launch({
            executablePath: chromeExecutable,
            headless: true,
            args: [
                "--autoplay-policy=no-user-gesture-required",
                "--disable-background-timer-throttling",
                "--disable-renderer-backgrounding",
            ],
        });
        const page = await browser.newPage();
        page.on("console", (message) => {
            if (message.type() === "error") {
                process.stderr.write(`browser: ${message.text()}\n`);
            }
        });
        const pageConfig = base64Url(
            JSON.stringify({ url: jsUrl, token: jsToken, cppIdentity }),
        );
        await page.goto(`http://127.0.0.1:${port}/#${pageConfig}`);
        await timeout(ready.promise, 15000, "official JS peer connection");

        testProcess = spawn(
            testExecutable,
            [
                "--gtest_filter=LiveKitServerTest.InteroperatesWithOfficialJsE2EEPeer",
            ],
            {
                cwd: repositoryRoot,
                env: {
                    ...process.env,
                    LIVEKIT_URL: cppUrl,
                    LIVEKIT_TOKEN: cppToken,
                    LIVEKIT_JS_PEER_IDENTITY: jsIdentity,
                },
                stdio: ["ignore", "pipe", "pipe"],
                windowsHide: true,
            },
        );
        let stdout = "";
        let stderr = "";
        testProcess.stdout.on("data", (chunk) => {
            stdout += chunk.toString();
        });
        testProcess.stderr.on("data", (chunk) => {
            stderr += chunk.toString();
        });
        const testExit = new Promise((resolve, reject) => {
            testProcess.once("error", reject);
            testProcess.once("exit", (code, signal) =>
                resolve({ code, signal }),
            );
        });

        const peerResult = await timeout(
            result.promise,
            35000,
            "official JS peer verification",
        );
        if (!peerResult.ok) {
            throw new Error(`Official JS peer failed: ${peerResult.message}`);
        }
        const exit = await timeout(
            testExit,
            30000,
            "C++ interoperability test exit",
        );
        if (exit.code !== 0) {
            throw new Error(
                `C++ interoperability test failed (${exit.code ?? exit.signal}):\n${stdout}\n${stderr}`,
            );
        }
        process.stdout.write(
            "PASS official JS 2.21.0 E2EE audio, VP8 video, and data interoperability\n",
        );
    } finally {
        if (testProcess && testProcess.exitCode === null) {
            testProcess.kill();
        }
        if (browser) {
            await browser.close();
        }
        await new Promise((resolve) => server.close(resolve));
    }
}

main().catch((error) => {
    process.stderr.write(
        `${error instanceof Error ? error.stack : String(error)}\n`,
    );
    process.exitCode = 1;
});
