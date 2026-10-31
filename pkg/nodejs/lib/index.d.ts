/// <reference types="node" />
import { EventEmitter } from 'node:events';
import type * as C from './capi-types';
import type { AdvancedRoom, AdvancedEventMap, AdvancedEvent, TrackControls, AudioTrackControls, VideoTrackControls, WithAttributes, TokenFetchOptions, TokenSource, ReconnectPolicy, RpcHandler } from './advanced';
export * from './advanced';
export * from './capi-types';
export * as CAPI from './constants';

export const RoomState: Readonly<{ Connecting: 0; Connected: 1; Disconnecting: 2; Disconnected: 3; Failed: 4; Reconnecting: 5 }>;
export const TrackKind: Readonly<{ Unknown: 0; Audio: 1; Video: 2 }>;
export const TrackSource: Readonly<{ Unknown: 0; Camera: 1; Microphone: 2; ScreenShare: 3; ScreenShareAudio: 4 }>;

export class LiveKitError extends Error {
  readonly code?: number | string;
  /** Native error domain; 3 denotes an RPC application error in this binding. */
  readonly domain?: number;
  readonly data?: string;
  constructor(message: string, details?: { code?: number | string; domain?: number; data?: string });
}
export class RpcError extends LiveKitError { constructor(code: number, message: string, data?: string) }
export interface RoomOptions {
  addonPath?: string;
  /** Maximum retained native events (default 256, drops oldest on overflow). */
  eventCapacity?: number;
  /** Maximum retained native event bytes including estimated object overhead (default 16 MiB). */
  eventByteLimit?: number;
  /** Worker polling interval (default 10 ms). */
  pollIntervalMs?: number;
  /** Maximum queued operations (default 128); close always remains available. */
  maxPendingOperations?: number;
  /** Maximum serialized operation bytes (default 32 MiB). */
  maxPendingBytes?: number;
  callbackCapacity?: number;
  callbackTimeoutMs?: number;
  callbackByteLimit?: number;
}
export interface ConnectOptions extends C.CRoomConnectOptions {
  autoSubscribe?: boolean;
  adaptiveStream?: boolean;
  dynacast?: boolean;
  joinRetries?: number;
  reconnectTimeoutMs?: number;
  reconnectPolicy?: ReconnectPolicy;
}
export interface SendOptions { topic?: string; destinationIdentities?: string[] }
export interface DataPublishOptions extends SendOptions { reliable?: boolean }
export interface TextSendOptions extends WithAttributes<C.CTextSendOptions> {}
export interface ByteSendOptions extends WithAttributes<C.CByteSendOptions> {}
export interface FileSendOptions extends WithAttributes<C.CFileSendOptions> {}
export interface ParticipantSnapshot {
  sid: string; identity: string; name: string; metadata: string;
  attributes: Record<string, string>; permissions: Required<C.CParticipantPermissions>;
  audioLevel: number; connectionQuality: number; isSpeaking: boolean;
}
export interface ParticipantEventValue extends Pick<ParticipantSnapshot, 'sid' | 'identity' | 'name' | 'metadata'> {
  audioLevel: number;
  connectionQuality: number;
  isSpeaking: boolean;
  isLocal: boolean;
}
export interface Publication {
  sid: string; name: string; mimeType: string; kind: number; source: number;
  width: number; height: number; muted: boolean;
}
export interface PublicationSnapshot extends Publication, Required<C.CRemoteTrackPublicationSnapshotInfo> {
  track: (Required<C.CRemoteTrackSnapshotInfo> & { sid: string; name: string; rtcStats: import('./advanced').RtcStats[] }) | null;
}
export interface RoomSnapshot {
  state: number; disconnectReason: number; connected: boolean; recording: boolean;
  sid: string; name: string; metadata: string;
  localParticipant: ParticipantSnapshot;
  remoteParticipants: Array<ParticipantSnapshot & { publications: PublicationSnapshot[] }>;
}
export interface MediaDevice { id: string; label: string; kind: number; isDefault: boolean }
export interface LocalTrack extends TrackControls {
  readonly kind: number;
  publish(options?: C.CTrackPublishOptions): Promise<void>;
  unpublish(): Promise<void>;
  setMuted(muted: boolean): Promise<void>;
}
export interface LocalAudioTrack extends LocalTrack, AudioTrackControls {
  /** Interleaved signed PCM16 little endian, copied before returning. */
  captureFrame(data: Uint8Array): Promise<void>;
}
export interface LocalVideoTrack extends LocalTrack, VideoTrackControls {
  /** Tightly packed I420; timestamp uses signed 64-bit microseconds. */
  captureFrame(data: Uint8Array, width: number, height: number, timestampUs?: bigint): Promise<void>;
}
type LifecycleType = 'connected' | 'reconnecting' | 'reconnected' | 'tokenRefreshed' | 'roomEos';
type ParticipantType = 'participantConnected' | 'participantDisconnected';
type TrackType = 'trackPublished' | 'trackUnpublished' | 'trackSubscribed' | 'trackUnsubscribed' |
  'trackMuted' | 'trackUnmuted' | 'localTrackPublished' | 'localTrackUnpublished';
type LifecycleEvent = { type: LifecycleType };
type ParticipantEvent = { type: ParticipantType; participant: ParticipantEventValue | null };
type TrackEvent = { type: TrackType; participant: ParticipantEventValue | null; track: Publication | null };
export interface DataReceivedEvent {
  type: 'dataReceived'; data: Buffer; topic: string; participantIdentity: string; reliable: boolean;
}
export interface TextReceivedEvent { type: 'textReceived'; text: string; topic: string; participantIdentity: string }
export interface BytesReceivedEvent {
  type: 'bytesReceived' | 'fileReceived'; data: Buffer; streamId: string; name: string;
  mimeType: string; topic: string; participantIdentity: string;
}
export interface AudioFrameEvent {
  type: 'audioFrame'; track: Publication | null; participant: ParticipantEventValue | null;
  data: Buffer; sampleRate: number; channels: number; samplesPerChannel: number;
}
export interface VideoFrameEvent {
  type: 'videoFrame'; track: Publication | null; participant: ParticipantEventValue | null;
  data: Buffer; width: number; height: number;
  /** Decimal string to preserve all signed 64-bit timestamp values. */
  timestampUs: string;
}
export type RoomEvent = AdvancedEvent | LifecycleEvent | ParticipantEvent | TrackEvent | DataReceivedEvent | TextReceivedEvent |
  BytesReceivedEvent | AudioFrameEvent | VideoFrameEvent | { type: 'disconnected'; reason: number } |
  { type: 'connectionStateChanged'; state: number } | { type: 'roomMetadataChanged'; metadata: string };
export type RoomEventMap = AdvancedEventMap & {
  [K in LifecycleType]: [event: LifecycleEvent]
} & { [K in ParticipantType]: [event: ParticipantEvent]
} & { [K in TrackType]: [event: TrackEvent]
} & {
  dataReceived: [event: DataReceivedEvent]; textReceived: [event: TextReceivedEvent];
  bytesReceived: [event: BytesReceivedEvent]; fileReceived: [event: BytesReceivedEvent];
  audioFrame: [event: AudioFrameEvent]; videoFrame: [event: VideoFrameEvent];
  disconnected: [event: { type: 'disconnected'; reason: number }];
  connectionStateChanged: [event: { type: 'connectionStateChanged'; state: number }];
  roomMetadataChanged: [event: { type: 'roomMetadataChanged'; metadata: string }];
  event: [event: RoomEvent]; eventsDropped: [count: number]; workerError: [error: Error];
};

export class Room extends EventEmitter<RoomEventMap> {
  constructor(options?: RoomOptions);
  version(): Promise<string>;
  snapshot(): Promise<RoomSnapshot>;
  /** Enumerates device metadata without opening physical capture or playback. */
  devices(): Promise<MediaDevice[]>;
  connect(url: string, token: string, options?: ConnectOptions): Promise<void>;
  connectWithTokenSource(source: TokenSource, fetchOptions?: TokenFetchOptions, connectOptions?: ConnectOptions): Promise<void>;
  registerRpcMethod(method: string, handler: RpcHandler): Promise<void>;
  unregisterRpcMethod(method: string): Promise<void>;
  disconnect(): Promise<void>;
  /** Idempotent; waits for queued operations, native teardown and Worker exit. */
  close(): Promise<void>;
  setName(name: string): Promise<void>;
  setMetadata(metadata: string): Promise<void>;
  setAttributes(attributes: Record<string, string>): Promise<void>;
  publishData(data: Uint8Array, options?: DataPublishOptions): Promise<void>;
  sendText(text: string, options?: TextSendOptions): Promise<void>;
  sendBytes(data: Uint8Array, options?: ByteSendOptions): Promise<void>;
  sendFile(path: string, options?: FileSendOptions): Promise<void>;
  performRpc(destinationIdentity: string, method: string, payload: string, options?: { responseTimeoutMs?: number }): Promise<string>;
  setTrackSubscribed(participantSid: string, trackSid: string, subscribed: boolean): Promise<void>;
  createAudioTrack(options?: C.CAudioSourceOptions & { name?: string; channels?: number }): Promise<LocalAudioTrack>;
  createVideoTrack(options?: C.CVideoSourceOptions & { name?: string }): Promise<LocalVideoTrack>;
}
export interface Room extends AdvancedRoom {}
