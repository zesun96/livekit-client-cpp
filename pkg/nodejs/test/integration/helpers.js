'use strict';
const { setTimeout: delay } = require('node:timers/promises');
const sdk = require(process.env.LIVEKIT_NODE_TEST_PACKAGE || '../..');
const { Room } = sdk;

const enabled = process.env.LIVEKIT_NODE_INTEGRATION === '1' && !!process.env.LIVEKIT_URL &&
  !!process.env.LIVEKIT_TOKEN && !!process.env.LIVEKIT_SECOND_TOKEN;
async function until(label, predicate, timeout = 15000) {
  const deadline = performance.now() + timeout;
  do {
    const result = await predicate();
    if (result) return result;
    await delay(25);
  } while (performance.now() < deadline);
  throw new Error(`Timed out waiting for ${label}`);
}
function watch(room, name, predicate = () => true, timeout = 15000) {
  let listener, timer;
  const promise = new Promise((resolve, reject) => {
    listener = event => {
      if (!predicate(event)) return;
      clearTimeout(timer); room.off(name, listener); resolve(event);
    };
    room.on(name, listener);
    timer = setTimeout(() => { room.off(name, listener); reject(new Error(`Timed out waiting for ${name}`)); }, timeout);
  });
  promise.catch(() => {});
  return { promise, cancel() { clearTimeout(timer); room.off(name, listener); } };
}
async function peers(t, connectOptions = {}, e2ee = false) {
  const first = new Room({ eventCapacity: 512, eventByteLimit: 32 * 1024 * 1024 });
  const second = new Room({ eventCapacity: 512, eventByteLimit: 32 * 1024 * 1024 });
  t.after(async () => { await Promise.all([first.close(), second.close()]); });
  const tokenA = e2ee ? process.env.LIVEKIT_NODE_E2EE_TOKEN : process.env.LIVEKIT_TOKEN;
  const tokenB = e2ee ? process.env.LIVEKIT_NODE_E2EE_SECOND_TOKEN : process.env.LIVEKIT_SECOND_TOKEN;
  await first.connect(process.env.LIVEKIT_URL, tokenA, { autoSubscribe: false, ...connectOptions });
  await second.connect(process.env.LIVEKIT_URL, tokenB, { autoSubscribe: false, ...connectOptions });
  try {
    await until('both rooms connected', async () => (await first.snapshot()).connected && (await second.snapshot()).connected);
  } catch (error) {
    for (const room of [first, second]) {
      const snapshot = await room.snapshot();
      t.diagnostic(JSON.stringify({ state: snapshot.state, connected: snapshot.connected,
        identity: snapshot.localParticipant?.identity, peers: snapshot.remoteParticipants.map(p => p.identity) }));
    }
    throw error;
  }
  const a = (await first.snapshot()).localParticipant;
  const b = (await second.snapshot()).localParticipant;
  await until('peer snapshot propagation', async () => {
    const one = await first.snapshot(), two = await second.snapshot();
    return one.remoteParticipants.some(p => p.identity === b.identity) && two.remoteParticipants.some(p => p.identity === a.identity);
  });
  return { first, second, a, b };
}
module.exports = { enabled, until, watch, peers, delay, sdk };
