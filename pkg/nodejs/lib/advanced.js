'use strict';
const schemas = require('./struct-schema.json');

function string(value, name, nonempty = false) {
  if (typeof value !== 'string' || value.includes('\0') || (nonempty && !value.length)) throw new TypeError(`${name} must be a ${nonempty ? 'nonempty ' : ''}string without NUL`);
  return value;
}
function integer(value, name, max = 0xffffffff) {
  if (!Number.isSafeInteger(value) || value < 0 || value > max) throw new RangeError(`${name} must be an unsigned integer`);
  return value;
}
function bool(value) { if (typeof value !== 'boolean') throw new TypeError('Expected boolean'); return value; }
function bytes(value) { if (!(value instanceof Uint8Array)) throw new TypeError('Expected Buffer or Uint8Array'); return value; }
function bigint(value, signed = false) {
  if (typeof value !== 'bigint' || value < (signed ? -(1n << 63n) : 0n) || value >= (signed ? 1n << 63n : 1n << 64n)) throw new RangeError('Expected a 64-bit bigint');
  return value;
}
function attributes(value) {
  if (Array.isArray(value)) return value;
  if (!value || typeof value !== 'object') throw new TypeError('Expected attributes object or array');
  return Object.entries(value).map(([key, item]) => ({ key: string(key, 'attribute key'), value: string(item, 'attribute value') }));
}
function struct(type, input = {}, depth = 0) {
  if (depth > 24 || !input || typeof input !== 'object' || Array.isArray(input)) throw new TypeError(`Expected ${type} options`);
  const schema = schemas[type];
  if (!schema) throw new Error(`Unknown native structure ${type}`);
  const out = {};
  for (const [key, raw] of Object.entries(input)) {
    const field = schema[key];
    if (!field) throw new TypeError(`Unknown ${type} option: ${key}`);
    const value = key === 'attributes' || key === 'participantAttributes' ? attributes(raw) : raw;
    function validate(kind, item) {
      if (kind.endsWith('[]')) {
        if (!Array.isArray(item)) throw new TypeError(`${key} must be an array`);
        return item.map(v => validate(kind.slice(0, -2), v));
      }
      if (kind.startsWith('C')) return struct(kind, item, depth + 1);
      if (kind === 'string') return string(item, key);
      if (kind === 'boolean') return bool(item);
      if (kind === 'Uint8Array') return bytes(item);
      if (kind === 'bigint') return bigint(item, ['timestamp', 'timestampUs', 'editTimestamp', 'firstReceivedTime', 'lastReceivedTime'].includes(key));
      if (typeof item !== 'number' || !Number.isFinite(item)) throw new TypeError(`${key} must be a finite number`);
      return item;
    }
    out[key] = validate(field, value);
  }
  for (const [value, flag] of [['totalSize', 'hasTotalSize'], ['frameEncoding', 'hasFrameEncoding'], ['schema', 'hasSchema'], ['targetFps', 'hasTargetFps'], ['videoQuality', 'hasVideoQuality'], ['userTimestampUs', 'hasUserTimestampUs'], ['frameId', 'hasFrameId'], ['userData', 'hasUserData'], ['degradationPreference', 'hasDegradationPreference']]) {
    if (Object.hasOwn(schema, flag) && Object.hasOwn(out, value) && !Object.hasOwn(out, flag)) out[flag] = true;
  }
  return out;
}

function trackControls(request, id, audio) {
  const call = (op, args = {}) => request('advanced', { op, ...args, id });
  let closePromise;
  const controls = {
    close() { return closePromise ||= call('trackRelease').catch(error => { closePromise = undefined; throw error; }); },
    rtcStats: () => call('trackStats'),
    rtcStatsJson: () => call('trackStatsJson'),
    startCapture: () => call('captureStart'), stopCapture: () => call('captureStop'),
    switchDevice: deviceId => call('captureSwitch', { deviceId: string(deviceId, 'deviceId', true) }),
    captureStatus: () => call('captureStatus'),
  };
  if (audio) Object.assign(controls, {
    queuedDurationMs: () => call('audioQueuedDuration'), clearQueue: () => call('audioClearQueue'),
    waitForPlayout: (timeoutMs = 1000) => call('audioWaitForPlayout', { timeoutMs: integer(timeoutMs, 'timeoutMs', 60000) }),
    setCaptureMuted: muted => call('microphoneMuted', { muted: bool(muted) }),
    setCaptureVolume: volume => {
      if (!Number.isFinite(volume) || volume < 0 || volume > 1) throw new RangeError('volume must be in [0, 1]');
      return call('microphoneVolume', { volume });
    },
    setProcessingOptions: options => call('microphoneProcessing', { options: struct('CAudioSourceOptions', options) }),
    processingStatus: () => call('microphoneStatus'),
  });
  else Object.assign(controls, {
    dimensions: () => call('videoDimensions'),
    captureVideoFrame: options => call('captureFrame', { options: struct('CVideoFrameInput', options) }),
    updateEncoding: (options, backupCodec = false) => call('trackEncoding', { options: struct('CVideoEncoding', options), backupCodec: bool(backupCodec) }),
    updateDegradationPreference: preference => call('trackDegradation', { preference: integer(preference, 'preference', 3) }),
  });
  return controls;
}

function advanced(request, makeTrack, isClosing) {
  const call = (op, args = {}) => request('advanced', { op, ...args });
  function writer(id, kind) {
    const invoke = (op, args = {}) => call(op, { ...args, id, kind });
    let disposed;
    return Object.freeze({
      id,
      write: value => {
        if (kind !== 'text') return invoke('writerWrite', { data: bytes(value) });
        if (typeof value !== 'string') throw new TypeError('text must be a string');
        return invoke('writerWrite', { text: value });
      },
      close: () => invoke('writerClose'), cancel: (reason = '') => invoke('writerCancel', { reason: string(reason, 'reason') }),
      dispose: () => disposed ||= invoke('writerDispose'),
      info: () => invoke('writerInfo'), isClosed: () => invoke('writerClosed'),
    });
  }
  function reader(id, kind) {
    const prefix = kind === 'data' ? 'dataReader' : 'reader';
    let closed;
    let terminal = false;
    let reading = false;
    const invoke = (op, args = {}) => call(prefix + op, { ...args, id, kind });
    const result = {
      async read(timeoutMs = 0) {
        integer(timeoutMs, 'timeoutMs');
        if (reading) throw new Error('Only one read may be pending per reader');
        if (terminal || closed || isClosing()) return null;
        reading = true;
        try {
          const deadline = performance.now() + timeoutMs;
          do {
            const batch = await invoke('Read', { timeoutMs: Math.min(100, Math.max(0, Math.ceil(deadline - performance.now()))) });
            if (batch.status === 0) return kind === 'video' ? { ...batch.frame, metadata: batch.metadata } : batch.frame;
            if (batch.status === 2) { terminal = true; return null; }
            if (!timeoutMs || performance.now() >= deadline || closed || isClosing()) return null;
          } while (true);
        } finally { reading = false; }
      },
      info: () => invoke('Info'),
      close() { terminal = true; return closed ||= invoke('Close'); },
      async *[Symbol.asyncIterator]() {
        try {
          while (!terminal && !closed && !isClosing()) {
            const frame = await result.read(100);
            if (frame) yield frame;
          }
        } finally { if (!isClosing()) await result.close(); }
      },
    };
    return Object.freeze(result);
  }
  const location = (participantIdentity, trackSid) => ({ participantIdentity: string(participantIdentity, 'participantIdentity', true), trackSid: string(trackSid, 'trackSid', true) });
  const e2ee = Object.freeze({
    status: () => call('e2eeStatus'), setEnabled: enabled => call('e2eeEnable', { enabled: bool(enabled) }),
    setSharedKey: (key, keyIndex = 0) => call('e2eeSetSharedKey', { key: bytes(key), keyIndex: integer(keyIndex, 'keyIndex') }),
    exportSharedKey: (keyIndex = 0) => call('e2eeExportSharedKey', { keyIndex: integer(keyIndex, 'keyIndex') }),
    ratchetSharedKey: (keyIndex = 0) => call('e2eeRatchetSharedKey', { keyIndex: integer(keyIndex, 'keyIndex') }),
    removeSharedKey: (keyIndex = 0) => call('e2eeRemoveSharedKey', { keyIndex: integer(keyIndex, 'keyIndex') }),
    setParticipantKey: (participantIdentity, key, keyIndex = 0) => call('e2eeSetParticipantKey', { participantIdentity: string(participantIdentity, 'identity', true), key: bytes(key), keyIndex: integer(keyIndex, 'keyIndex') }),
    exportParticipantKey: (participantIdentity, keyIndex = 0) => call('e2eeExportParticipantKey', { participantIdentity: string(participantIdentity, 'identity', true), keyIndex: integer(keyIndex, 'keyIndex') }),
    ratchetParticipantKey: (participantIdentity, keyIndex = 0) => call('e2eeRatchetParticipantKey', { participantIdentity: string(participantIdentity, 'identity', true), keyIndex: integer(keyIndex, 'keyIndex') }),
    removeParticipantKey: (participantIdentity, keyIndex = 0) => call('e2eeRemoveParticipantKey', { participantIdentity: string(participantIdentity, 'identity', true), keyIndex: integer(keyIndex, 'keyIndex') }),
    removeParticipantKeys: participantIdentity => call('e2eeRemoveParticipantKeys', { participantIdentity: string(participantIdentity, 'identity', true) }),
    clearKeys: () => call('e2eeClearKeys'), setDataKeyIndex: keyIndex => call('e2eeDataKeyIndex', { keyIndex: integer(keyIndex, 'keyIndex') }),
    cryptors: () => call('e2eeCryptors'),
    setCryptorEnabled: (trackId, direction, enabled) => call('e2eeCryptorEnabled', { trackId: string(trackId, 'trackId', true), direction: integer(direction, 'direction', 1), enabled: bool(enabled) }),
    setCryptorKeyIndex: (trackId, direction, keyIndex) => call('e2eeCryptorKeyIndex', { trackId: string(trackId, 'trackId', true), direction: integer(direction, 'direction', 1), keyIndex: integer(keyIndex, 'keyIndex') }),
    setParticipantEnabled: (participantIdentity, enabled) => call('e2eeParticipantEnabled', { participantIdentity: string(participantIdentity, 'identity', true), enabled: bool(enabled) }),
  });
  const diagnostics = Object.freeze({
    subscribe: (input = {}) => {
      if (!input || typeof input !== 'object' || Object.keys(input).some(key => !['logs', 'traces'].includes(key))) throw new TypeError('Expected logs/traces options');
      return call('diagnosticsSubscribe', { logs: bool(input.logs ?? false), traces: bool(input.traces ?? false) });
    },
    setLogOptions: input => call('logOptions', { options: struct('CLogOptions', input) }),
    getLogOptions: () => call('getLogOptions'),
    setTraceOptions: input => call('traceOptions', { options: struct('CTraceOptions', input) }),
    getTraceOptions: () => call('getTraceOptions'),
    startTraceFile: path => call('startTraceFile', { path: string(path, 'path', true) }), stopTrace: () => call('stopTrace'),
  });
  const output = Object.freeze({
    status: () => call('speakerStatus'),
    setMuted: muted => call('speakerMuted', { muted: bool(muted) }),
    setVolume: volume => { if (!Number.isFinite(volume) || volume < 0 || volume > 1) throw new RangeError('volume must be in [0, 1]'); return call('speakerVolume', { volume }); },
    setDevice: deviceId => call('speakerDevice', { deviceId: string(deviceId, 'deviceId', true) }),
  });
  const methods = { e2ee, diagnostics, output,
    screenSources: () => call('screenSources'),
    updateRemoteTrackSettings: (participantSid, trackSid, options) => call('remoteTrackSettings', { participantSid: string(participantSid, 'participantSid', true), trackSid: string(trackSid, 'trackSid', true), options: struct('CRemoteTrackSettings', options) }),
    setTrackSubscriptionPermissions: (allParticipantsAllowed, permissions = []) => {
      if (!Array.isArray(permissions)) throw new TypeError('permissions must be an array');
      return call('subscriptionPermissions', { allParticipantsAllowed: bool(allParticipantsAllowed), permissions: permissions.map(item => struct('CParticipantTrackPermission', item)) });
    },
    republishAllTracks: () => call('republishTracks'),
    sendChatMessage: message => call('sendChat', { message: string(message, 'message') }),
    editChatMessage: (messageId, timestamp, message) => call('editChat', { messageId: string(messageId, 'messageId', true), timestamp: bigint(timestamp, true), message: string(message, 'message') }),
    publishDtmf: (code, digit) => call('publishDtmf', { code: integer(code, 'code'), digit: string(digit, 'digit', true) }),
    streamText: async input => writer(await call('streamText', { options: struct('CStreamTextOptions', input) }), 'text'),
    streamBytes: async input => writer(await call('streamBytes', { options: struct('CStreamBytesOptions', input) }), 'bytes'),
    registerTextStreamHandler: topic => call('streamRegister', { kind: 'text', topic: string(topic, 'topic') }),
    unregisterTextStreamHandler: topic => call('streamUnregister', { kind: 'text', topic: string(topic, 'topic') }),
    registerByteStreamHandler: topic => call('streamRegister', { kind: 'bytes', topic: string(topic, 'topic') }),
    unregisterByteStreamHandler: topic => call('streamUnregister', { kind: 'bytes', topic: string(topic, 'topic') }),
    createAudioStream: async (participantIdentity, trackSid, input) => reader(await call('readerCreate', { kind: 'audio', ...location(participantIdentity, trackSid), options: struct('CMediaStreamOptions', input) }), 'audio'),
    createVideoStream: async (participantIdentity, trackSid, input) => reader(await call('readerCreate', { kind: 'video', ...location(participantIdentity, trackSid), options: struct('CMediaStreamOptions', input) }), 'video'),
    startTrackRecording: async (participantIdentity, trackSid, input) => {
      const id = await call('recorderStart', { ...location(participantIdentity, trackSid), options: struct('CTrackRecorderOptions', input) });
      let closed;
      return Object.freeze({ stop: () => call('recorderStop', { id }), stats: () => call('recorderStats', { id }), close: () => closed ||= call('recorderClose', { id }) });
    },
    storeDataTrackSchema: (schema, definition) => call('dataTrackStoreSchema', { options: struct('CDataTrackSchemaId', schema), definition: bytes(definition) }),
    getDataTrackSchema: (participantIdentity, schema) => call('dataTrackGetSchema', { participantIdentity: string(participantIdentity, 'participantIdentity'), options: struct('CDataTrackSchemaId', schema) }),
    remoteDataTracks: () => call('dataTrackSnapshot'),
    updateDataTrackSubscription: (participantIdentity, trackSid, input) => call('dataTrackUpdateSubscription', { ...location(participantIdentity, trackSid), options: struct('CDataTrackSubscriptionOptions', input) }),
    subscribeDataTrack: async (participantIdentity, trackSid, input) => reader(await call('dataReaderCreate', { ...location(participantIdentity, trackSid), options: struct('CDataTrackSubscriptionOptions', input) }), 'data'),
    publishDataTrack: async input => {
      const id = await call('dataTrackPublish', { options: struct('CDataTrackPublishOptions', input) });
      let closed;
      return Object.freeze({
        push: (data, userTimestamp) => call('dataTrackPush', { id, data: bytes(data), ...(userTimestamp === undefined ? {} : { userTimestamp: bigint(userTimestamp) }) }),
        info: () => call('dataTrackInfo', { id }), unpublish: () => call('dataTrackUnpublish', { id }),
        close: () => closed ||= call('dataTrackClose', { id }).catch(error => { closed = undefined; throw error; }),
      });
    },
  };
  for (const [method, kind, schema, audio] of [
    ['createMicrophoneTrack', 'microphone', 'CMicrophoneCaptureOptions', true],
    ['createSystemAudioTrack', 'systemAudio', 'CSystemAudioCaptureOptions', true],
    ['createCameraTrack', 'camera', 'CCameraCaptureOptions', false],
    ['createScreenTrack', 'screen', 'CScreenCaptureOptions', false],
  ]) methods[method] = async (input = {}, name = kind) => {
    const id = await call('createCapture', { kind, name: string(name, 'name'), options: struct(schema, input) });
    return makeTrack(id, audio, kind);
  };
  return methods;
}
module.exports = { advanced, trackControls, struct, string, integer };
