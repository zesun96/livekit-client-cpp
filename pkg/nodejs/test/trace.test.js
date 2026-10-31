'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { Room } = require('..');

test('trace file stays active across unrelated room shutdown and flushes on owner close', async () => {
  const outputRoot = path.resolve(__dirname, '../../../out');
  fs.mkdirSync(outputRoot, { recursive: true });
  const directory = fs.mkdtempSync(path.join(outputRoot, 'node-trace-test-'));
  const file = path.join(directory, 'trace.json');
  const owner = new Room();
  const other = new Room();
  try {
    await owner.diagnostics.setTraceOptions({ enabled: true, categoryMask: 127n });
    await owner.diagnostics.startTraceFile(file);
    await other.close();
    const last = new Room();
    try { await last.version(); } finally { await last.close(); }
    await owner.close();
    const contents = JSON.parse(fs.readFileSync(file, 'utf8'));
    assert(Array.isArray(contents.traceEvents));
    assert(contents.traceEvents.some(event => event.name === 'runtime.init'));
    assert(contents.traceEvents.some(event => event.name === 'runtime.shutdown'));
  } finally {
    await Promise.all([owner.close(), other.close()]);
    if (fs.existsSync(file)) fs.unlinkSync(file);
    fs.rmdirSync(directory);
  }
});
