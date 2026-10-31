'use strict';

const { parentPort, workerData } = require('node:worker_threads');

let native;
let room;
let timer;
let awaitingEvents = false;
let closing;

function errorValue(error) {
  return { message: error.message, code: error.code, domain: error.domain, data: error.data, name: error.name };
}

function poll() {
  if (awaitingEvents || !room) return;
  const batch = native.call(room, 'poll', {});
  if (batch.events.length || batch.dropped) {
    awaitingEvents = true;
    parentPort.postMessage({ type: 'events', ...batch });
  }
}

function finishClose() {
  if (awaitingEvents) return;
  poll();
  if (awaitingEvents) return;
  room = null;
  parentPort.postMessage({ type: 'result', id: closing });
  parentPort.close();
}

try {
  native = require(workerData.addonPath);
  room = native.create(workerData.options);
  parentPort.on('message', ({ id, method, args }) => {
    if (method === '$ack') {
      awaitingEvents = false;
      if (closing !== undefined) finishClose();
      else poll();
      return;
    }
    try {
      if (method === 'close') {
        native.call(room, 'close', {});
        clearInterval(timer);
        closing = id;
        finishClose();
        return;
      }
      const value = method === 'version' ? native.version() : native.call(room, method, args);
      poll();
      parentPort.postMessage({ type: 'result', id, value: value === null ? undefined : value });
    } catch (error) {
      parentPort.postMessage({ type: 'result', id, error: errorValue(error) });
    }
  });
  timer = setInterval(poll, workerData.options.pollIntervalMs);
  parentPort.postMessage({ type: 'ready' });
} catch (error) {
  if (room) native.call(room, 'close', {});
  parentPort.postMessage({ type: 'fatal', error: errorValue(error) });
  parentPort.close();
}
