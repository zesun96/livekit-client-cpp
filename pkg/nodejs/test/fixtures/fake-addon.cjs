'use strict';
// Deterministic transport fixture: tests the public Worker protocol separately from the real ABI.
module.exports = {
  version() {
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 150);
    return '1.2.3';
  },
  create() { return { events: [] }; },
  call(room, method, args) {
    if (method === 'poll') return { events: room.events.splice(0, 32), dropped: 0 };
    if (method === 'publishData') {
      room.events.push({ type: 'dataReceived', data: args.data, backingByteLength: args.data.buffer.byteLength, topic: args.topic, participantIdentity: 'fixture', reliable: true });
    }
    if (method === 'snapshot') return { sequence: room.sequence || [] };
    if (method === 'setName') (room.sequence ||= []).push(args.name);
    return null;
  },
};
