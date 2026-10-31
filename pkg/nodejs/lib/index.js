'use strict';

const { EventEmitter } = require('node:events');
const { Worker } = require('node:worker_threads');
const path = require('node:path');
const { serialize } = require('node:v8');
const { AsyncLocalStorage } = require('node:async_hooks');
const { advanced, trackControls, struct } = require('./advanced');
const CAPI = require('./constants');
const callbackContext = new AsyncLocalStorage();

class LiveKitError extends Error {
  constructor(message, { code, domain, data } = {}) {
    super(message);
    this.name = 'LiveKitError';
    this.code = code;
    this.domain = domain;
    this.data = data;
  }
}
class RpcError extends LiveKitError {
  constructor(code, message, data = '') {
    uint(code, 'RPC error code', 1);
    super(text(message, 'message'), { code, domain: 3, data: text(data, 'data') });
    this.name = 'RpcError';
  }
}

function text(value, name, nonempty = false) {
  if (typeof value !== 'string' || value.includes('\0') || (nonempty && !value.length)) {
    throw new TypeError(`${name} must be ${nonempty ? 'a nonempty' : 'a'} string without NUL`);
  }
  return value;
}
function uint(value, name, min = 0, max = 0xffffffff) {
  if (!Number.isInteger(value) || value < min || value > max) throw new RangeError(`${name} must be an integer in [${min}, ${max}]`);
  return value;
}
function options(value, schema) {
  if (value === undefined) return {};
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new TypeError('Expected an options object');
  const out = {};
  for (const [key, item] of Object.entries(value)) {
    if (!Object.hasOwn(schema, key)) throw new TypeError(`Unknown option: ${key}`);
    const kind = schema[key];
    if (kind === 'string') text(item, key);
    else if (kind === 'boolean') { if (typeof item !== 'boolean') throw new TypeError(`${key} must be boolean`); }
    else if (kind === 'strings') {
      if (!Array.isArray(item)) throw new TypeError(`${key} must be an array`);
      item.forEach(v => text(v, key, true));
    } else uint(item, key, kind === 'positive' ? 1 : 0);
    out[key] = item;
  }
  return out;
}
const destinations = { topic: 'string', destinationIdentities: 'strings' };
const fileOptions = { ...destinations, mimeType: 'string', name: 'string' };
function binary(value) {
  if (!(value instanceof Uint8Array)) throw new TypeError('data must be a Buffer or Uint8Array');
  return value;
}
function restoreBuffers(value) {
  if (value instanceof Uint8Array) return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  if (Array.isArray(value)) return value.map(restoreBuffers);
  if (value && typeof value === 'object') for (const key of Object.keys(value)) value[key] = restoreBuffers(value[key]);
  return value;
}
function copyRequest(value, depth = 0) {
  if (depth > 32) throw new TypeError('Operation arguments are nested too deeply');
  // structuredClone and postMessage preserve an entire typed-array backing buffer. Compact each
  // view first so a three-byte slice cannot enqueue a multi-megabyte backing allocation.
  if (value instanceof Uint8Array) return Uint8Array.from(value);
  if (Array.isArray(value)) return value.map(item => copyRequest(item, depth + 1));
  if (value && typeof value === 'object') return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, copyRequest(item, depth + 1)]));
  return value;
}

const RoomState = Object.freeze({ Connecting: 0, Connected: 1, Disconnecting: 2, Disconnected: 3, Failed: 4, Reconnecting: 5 });
const TrackKind = Object.freeze({ Unknown: 0, Audio: 1, Video: 2 });
const TrackSource = Object.freeze({ Unknown: 0, Camera: 1, Microphone: 2, ScreenShare: 3, ScreenShareAudio: 4 });

class Room extends EventEmitter {
  #worker;
  #ready;
  #pending = new Map();
  #nextId = 1;
  #closing = false;
  #closePromise;
  #failure;
  #pendingBytes = 0;
  #maxPending;
  #maxBytes;
  #exit;
  #native;
  #channel;
  #tokenSource;
  #reconnectPolicy;
  #rpc = new Map();
  #connecting = false;

  constructor(input = {}) {
    super();
    const config = options(input, { addonPath: 'string', eventCapacity: 'positive', eventByteLimit: 'positive',
      pollIntervalMs: 'positive', maxPendingOperations: 'positive', maxPendingBytes: 'positive',
      callbackCapacity: 'positive', callbackTimeoutMs: 'positive', callbackByteLimit: 'positive' });
    const addonPath = path.resolve(config.addonPath || process.env.LIVEKIT_NODE_ADDON || path.join(__dirname, '../native/livekitclient_node.node'));
    this.#maxPending = config.maxPendingOperations ?? 128;
    this.#maxBytes = config.maxPendingBytes ?? 32 * 1024 * 1024;
    let resolveReady, rejectReady;
    this.#ready = new Promise((resolve, reject) => { resolveReady = resolve; rejectReady = reject; });
    this.#ready.catch(() => {});
    // Loading the addon here does not initialize the SDK. Its callback channel is owned by this
    // application environment so it remains responsive while a Room Worker blocks in the C ABI.
    try { this.#native = require(addonPath); } catch { /* Worker reports loader errors asynchronously. */ }
    if (this.#native?.createChannel) this.#channel = this.#native.createChannel(value => { void this.#callback(value); }, {
      callbackCapacity: config.callbackCapacity ?? 128, callbackTimeoutMs: config.callbackTimeoutMs ?? 10000,
      callbackByteLimit: config.callbackByteLimit ?? 1024 * 1024,
    });
    this.#worker = new Worker(path.join(__dirname, 'worker.js'), { workerData: { addonPath, options: {
      eventCapacity: config.eventCapacity ?? 256, eventByteLimit: config.eventByteLimit ?? 16 * 1024 * 1024,
      pollIntervalMs: config.pollIntervalMs ?? 10,
      ...(this.#channel ? { channelId: this.#channel } : {}),
    } } });
    this.#exit = new Promise(resolve => this.#worker.once('exit', resolve));
    const fail = error => {
      if (this.#failure) return;
      this.#failure = error;
      rejectReady(error);
      for (const pending of this.#pending.values()) pending.reject(error);
      this.#pending.clear();
      this.#pendingBytes = 0;
      this.emit('workerError', error);
    };
    this.#worker.on('error', fail);
    this.#worker.on('exit', code => {
      if (this.#channel) {
        this.#native.releaseChannel(this.#channel);
        this.#channel = undefined;
      }
      if (!this.#closing || this.#pending.size) fail(new LiveKitError(`Room worker exited (${code})`));
    });
    this.#worker.on('message', message => {
      if (message.type === 'ready') resolveReady();
      else if (message.type === 'fatal') fail(new LiveKitError(message.error.message, message.error));
      else if (message.type === 'result') {
        const pending = this.#pending.get(message.id);
        if (!pending) return;
        this.#pending.delete(message.id);
        this.#pendingBytes -= pending.bytes;
        if (message.error) pending.reject(new LiveKitError(message.error.message, message.error));
        else pending.resolve(restoreBuffers(message.value));
      } else if (message.type === 'events') {
        try {
          if (message.dropped) this.emit('eventsDropped', message.dropped);
          for (const raw of message.events) {
            const event = restoreBuffers(raw);
            this.emit(event.type, event);
            this.emit('event', event);
          }
        } finally {
          this.#worker.postMessage({ method: '$ack' });
        }
      }
    });
    Object.defineProperties(this, Object.fromEntries(Object.entries(advanced(
      (method, args) => this.#request(method, args), (id, audio, kind) => this.#track(id, audio, kind), () => this.#closing,
    )).map(([key, value]) => [key, { value, enumerable: false }])));
  }

  async #callback({ id, kind, payload }) {
    const context = { room: this, active: true };
    let reply;
    try {
      reply = await callbackContext.run(context, async () => {
        if (this.#closing) throw new Error('Room is closing');
        if (kind === 'token') {
          if (!this.#tokenSource) throw new Error('Token source is not configured');
          const credentials = await this.#tokenSource(payload);
          return { ok: true, serverUrl: text(credentials?.serverUrl, 'serverUrl', true), participantToken: text(credentials?.participantToken, 'participantToken', true) };
        }
        if (kind === 'reconnect') {
          if (!this.#reconnectPolicy) return { ok: true, retry: false };
          const delay = await this.#reconnectPolicy(payload);
          return delay === null || delay === undefined ? { ok: true, retry: false } : { ok: true, retry: true, delayMs: uint(delay, 'retry delay') };
        }
        if (kind === 'rpc') {
          const handler = this.#rpc.get(payload.method);
          if (!handler) throw new RpcError(CAPI.RpcErrorCode.UnsupportedMethod, 'RPC method is not registered');
          return { ok: true, payload: text(await handler(payload.invocation), 'RPC payload') };
        }
        throw new Error('Unknown callback kind');
      });
    } catch (error) {
      reply = { ok: false, message: typeof error?.message === 'string' && !error.message.includes('\0') ? error.message : 'Callback failed',
        code: Number.isInteger(error?.code) && error.code > 0 && error.code <= 0xffffffff ? error.code : CAPI.RpcErrorCode.ApplicationError,
        data: typeof error?.data === 'string' && !error.data.includes('\0') ? error.data : '' };
    } finally { context.active = false; }
    if (this.#channel) {
      try { this.#native.completeCallback(this.#channel, id, reply); }
      catch { /* Room teardown may already have cancelled this request. */ }
    }
  }

  async #request(method, args = {}) {
    const context = callbackContext.getStore();
    if (context?.room === this && context.active && method !== 'close') throw new LiveKitError('Do not await operations on this room from its native response callback', { code: 'REENTRANT_CALLBACK' });
    if (this.#closing && method !== 'close') throw new LiveKitError('Room is closed', { code: 2, domain: 1 });
    if (this.#failure) throw this.#failure;
    // Snapshot user buffers/options before the first await, including during Worker startup.
    const bytes = serialize(args).byteLength;
    if (method !== 'close' && (this.#pending.size >= this.#maxPending || this.#pendingBytes + bytes > this.#maxBytes)) {
      throw new LiveKitError('Room operation queue limit exceeded', { code: 'QUEUE_FULL' });
    }
    const copy = copyRequest(args);
    const id = this.#nextId++;
    let resolve, reject;
    const result = new Promise((a, b) => { resolve = a; reject = b; });
    // Register before readiness so requests made during startup also count against queue limits.
    this.#pending.set(id, { resolve, reject, bytes });
    this.#pendingBytes += bytes;
    this.#ready.then(() => {
      if (this.#failure) return;
      try { this.#worker.postMessage({ id, method, args: copy }); }
      catch (error) {
        this.#pending.delete(id); this.#pendingBytes -= bytes; reject(error);
      }
    }, () => {});
    return result;
  }

  version() { return this.#request('version'); }
  snapshot() { return this.#request('snapshot'); }
  devices() { return this.#request('devices'); }
  connect(url, token, input) {
    text(url, 'url', true); text(token, 'token', true);
    const parsed = this.#connectOptions(input);
    return this.#startConnect('connect', { options: parsed.options, useReconnectPolicy: !!parsed.reconnectPolicy, url, token }, undefined, parsed.reconnectPolicy);
  }
  #connectOptions(input = {}) {
    const { reconnectPolicy, ...native } = input;
    if (reconnectPolicy !== undefined && typeof reconnectPolicy !== 'function') throw new TypeError('reconnectPolicy must be a function');
    const value = struct('CRoomConnectOptions', native);
    return { options: value, reconnectPolicy };
  }
  #startConnect(method, args, source, policy) {
    if (this.#connecting) return Promise.reject(new LiveKitError('A connection attempt is already in progress', { domain: 1, code: 2 }));
    const previousSource = this.#tokenSource, previousPolicy = this.#reconnectPolicy;
    this.#connecting = true;
    this.#tokenSource = source; this.#reconnectPolicy = policy;
    return this.#request(method, args).catch(error => {
      this.#tokenSource = previousSource; this.#reconnectPolicy = previousPolicy;
      throw error;
    }).finally(() => { this.#connecting = false; });
  }
  connectWithTokenSource(source, fetchOptions = {}, connectOptions = {}) {
    if (typeof source !== 'function') throw new TypeError('Token source must be a function');
    const fetch = struct('CTokenSourceFetchOptions', fetchOptions);
    const options = this.#connectOptions(connectOptions);
    return this.#startConnect('advanced', { op: 'connectTokenSource', fetchOptions: fetch, options: options.options,
      useReconnectPolicy: !!options.reconnectPolicy }, source, options.reconnectPolicy);
  }
  registerRpcMethod(method, handler) {
    text(method, 'method', true);
    if (typeof handler !== 'function') throw new TypeError('RPC handler must be a function');
    if (this.#rpc.has(method)) return Promise.reject(new LiveKitError('RPC method is already registered', { domain: 1, code: 2 }));
    const previous = this.#rpc.get(method);
    this.#rpc.set(method, handler);
    return this.#request('advanced', { op: 'registerRpc', method }).catch(error => {
      if (this.#rpc.get(method) === handler) {
        if (previous) this.#rpc.set(method, previous);
        else this.#rpc.delete(method);
      }
      throw error;
    });
  }
  unregisterRpcMethod(method) {
    text(method, 'method', true);
    return this.#request('advanced', { op: 'unregisterRpc', method }).then(() => { this.#rpc.delete(method); });
  }
  disconnect() { return this.#request('disconnect'); }
  close() {
    if (!this.#closePromise) {
      this.#closing = true;
      if (this.#channel) this.#native.cancelChannel(this.#channel);
      this.#closePromise = this.#request('close').then(async () => { await this.#exit; }, async error => {
        await this.#worker.terminate();
        await this.#exit;
        throw error;
      });
    }
    return this.#closePromise;
  }
  setName(name) { return this.#request('setName', { name: text(name, 'name') }); }
  setMetadata(metadata) { return this.#request('setMetadata', { metadata: text(metadata, 'metadata') }); }
  setAttributes(attributes) {
    if (!attributes || typeof attributes !== 'object' || Array.isArray(attributes)) throw new TypeError('attributes must be an object');
    const snapshot = Object.fromEntries(Object.entries(attributes).map(([key, value]) => [text(key, 'attribute key'), text(value, 'attribute value')]));
    return this.#request('setAttributes', { attributes: snapshot });
  }
  publishData(data, input) { return this.#request('publishData', { ...options(input, { ...destinations, reliable: 'boolean' }), data: binary(data) }); }
  sendText(value, input) { return this.#request('sendText', { ...struct('CTextSendOptions', input), text: text(value, 'text') }); }
  sendBytes(data, input) { return this.#request('sendBytes', { ...struct('CByteSendOptions', input), data: binary(data) }); }
  sendFile(file, input) { return this.#request('sendFile', { ...struct('CFileSendOptions', input), path: text(file, 'path', true) }); }
  performRpc(destinationIdentity, method, payload, input) {
    return this.#request('performRpc', { ...options(input, { responseTimeoutMs: 'positive' }),
      destinationIdentity: text(destinationIdentity, 'destinationIdentity', true), method: text(method, 'method', true), payload: text(payload, 'payload') });
  }
  setTrackSubscribed(participantSid, trackSid, subscribed) {
    if (typeof subscribed !== 'boolean') throw new TypeError('subscribed must be boolean');
    return this.#request('setTrackSubscribed', { participantSid: text(participantSid, 'participantSid', true), trackSid: text(trackSid, 'trackSid', true), subscribed });
  }
  async createAudioTrack(input) {
    const { name, channels, ...rest } = input ?? {};
    const config = struct('CAudioSourceOptions', { ...rest, ...(channels === undefined ? {} : { numChannels: channels }) });
    if (name !== undefined) config.name = text(name, 'name');
    const id = await this.#request('createAudioTrack', config);
    return this.#track(id, true);
  }
  async createVideoTrack(input) {
    const { name, ...rest } = input ?? {};
    const config = struct('CVideoSourceOptions', rest);
    if (name !== undefined) config.name = text(name, 'name');
    const id = await this.#request('createVideoTrack', config);
    return this.#track(id, false);
  }
  #track(id, audio, captureKind) {
    const invoke = (method, args) => this.#request(method, { ...args, id });
    const track = {
      kind: audio ? TrackKind.Audio : TrackKind.Video,
      publish: (input) => {
        const config = struct('CTrackPublishOptions', input);
        if (config.source === undefined && captureKind === 'screen') config.source = TrackSource.ScreenShare;
        if (config.source === undefined && captureKind === 'systemAudio') config.source = TrackSource.ScreenShareAudio;
        return invoke('publishTrack', config);
      },
      unpublish: () => invoke('unpublishTrack'),
      setMuted: muted => {
        if (typeof muted !== 'boolean') throw new TypeError('muted must be boolean');
        return invoke('setTrackMuted', { muted });
      },
      ...trackControls((method, args) => this.#request(method, args), id, audio),
    };
    if (audio) track.captureFrame = data => invoke('captureAudio', { data: binary(data) });
    else track.captureFrame = (data, width, height, timestampUs = 0n) => {
      if (typeof timestampUs !== 'bigint' || timestampUs < -(1n << 63n) || timestampUs >= (1n << 63n)) throw new RangeError('timestampUs must be a signed 64-bit bigint');
      return invoke('captureVideo', { data: binary(data), width: uint(width, 'width', 1), height: uint(height, 'height', 1), timestampUs: timestampUs.toString() });
    };
    return Object.freeze(track);
  }
}

module.exports = { Room, LiveKitError, RpcError, RoomState, TrackKind, TrackSource, CAPI };
