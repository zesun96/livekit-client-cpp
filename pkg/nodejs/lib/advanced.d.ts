import type * as C from './capi-types';
import type { LocalAudioTrack, LocalVideoTrack, Publication } from './index';

export type AttributeInput = Record<string, string> | C.CAttribute[];
export type WithAttributes<T> = Omit<T, 'attributes' | 'participantAttributes'> & { attributes?: AttributeInput };
export type TokenFetchOptions = Omit<C.CTokenSourceFetchOptions, 'participantAttributes'> & { participantAttributes?: AttributeInput };
export interface TokenFetchRequest { options: Required<C.CTokenSourceFetchOptions>; forceRefresh: boolean }
export interface TokenCredentials { serverUrl: string; participantToken: string }
export type TokenSource = (request: TokenFetchRequest) => TokenCredentials | Promise<TokenCredentials>;
export type ReconnectPolicy = (context: Required<C.CReconnectContext>) => number | null | undefined | Promise<number | null | undefined>;
export type RpcHandler = (invocation: Required<C.CRpcInvocation>) => string | Promise<string>;
export type RtcStats = Required<C.CRtcTrackStats> & { id: string; kind: string; rid: string; mimeType: string; implementation: string; qualityLimitationReason: string };
export interface TrackControls {
  close(): Promise<void>;
  rtcStats(): Promise<RtcStats[]>;
  rtcStatsJson(): Promise<string>;
  startCapture(): Promise<void>;
  stopCapture(): Promise<void>;
  switchDevice(deviceId: string): Promise<void>;
  captureStatus(): Promise<{ capturing: boolean; deviceId: string }>;
}
export interface AudioTrackControls {
  queuedDurationMs(): Promise<number>;
  clearQueue(): Promise<void>;
  waitForPlayout(timeoutMs?: number): Promise<void>;
  setCaptureMuted(muted: boolean): Promise<void>;
  setCaptureVolume(volume: number): Promise<void>;
  setProcessingOptions(options: C.CAudioSourceOptions): Promise<void>;
  processingStatus(): Promise<{ muted: boolean; volume: number; options: Required<C.CAudioSourceOptions>; stats: Required<C.CMicrophoneProcessingStats> }>;
}
export interface VideoTrackControls {
  dimensions(): Promise<{ width: number; height: number }>;
  captureVideoFrame(frame: C.CVideoFrameInput): Promise<void>;
  updateEncoding(encoding: C.CVideoEncoding, backupCodec?: boolean): Promise<void>;
  updateDegradationPreference(preference: number): Promise<void>;
}
export type CryptorInfo = Required<C.CFrameCryptorInfo> & { trackId: string; participantIdentity: string };
export interface E2EE {
  status(): Promise<{ configured: boolean; enabled: boolean; dataKeyIndex: number }>;
  setEnabled(enabled: boolean): Promise<void>;
  setSharedKey(key: Uint8Array, keyIndex?: number): Promise<void>;
  exportSharedKey(keyIndex?: number): Promise<Buffer>;
  ratchetSharedKey(keyIndex?: number): Promise<void>;
  removeSharedKey(keyIndex?: number): Promise<void>;
  setParticipantKey(identity: string, key: Uint8Array, keyIndex?: number): Promise<void>;
  exportParticipantKey(identity: string, keyIndex?: number): Promise<Buffer>;
  ratchetParticipantKey(identity: string, keyIndex?: number): Promise<void>;
  removeParticipantKey(identity: string, keyIndex?: number): Promise<void>;
  removeParticipantKeys(identity: string): Promise<void>;
  clearKeys(): Promise<void>;
  setDataKeyIndex(keyIndex: number): Promise<void>;
  cryptors(): Promise<CryptorInfo[]>;
  setCryptorEnabled(trackId: string, direction: number, enabled: boolean): Promise<void>;
  setCryptorKeyIndex(trackId: string, direction: number, keyIndex: number): Promise<void>;
  setParticipantEnabled(identity: string, enabled: boolean): Promise<number>;
}
export interface Diagnostics {
  subscribe(options?: { logs?: boolean; traces?: boolean }): Promise<void>;
  setLogOptions(options: C.CLogOptions): Promise<void>;
  getLogOptions(): Promise<Required<C.CLogOptions>>;
  setTraceOptions(options: C.CTraceOptions): Promise<void>;
  getTraceOptions(): Promise<Required<C.CTraceOptions>>;
  startTraceFile(path: string): Promise<void>;
  stopTrace(): Promise<void>;
}
export interface OutputControls {
  status(): Promise<{ volume: number; muted: boolean; deviceId: string; stats: Required<C.CAudioPlaybackStats> }>;
  setMuted(muted: boolean): Promise<void>;
  setVolume(volume: number): Promise<void>;
  setDevice(deviceId: string): Promise<void>;
}
export type WriterInfo = Required<C.CDataStreamWriterInfo> & {
  streamId: string; mimeType: string; topic: string; participantIdentity: string; name: string;
  replyToStreamId: string; attributes: Record<string, string>; attachedStreamIds: string[];
};
export interface StreamWriter<T> {
  readonly id: number;
  write(value: T): Promise<void>;
  /** Completes the stream; info remains available until dispose or Room.close. */
  close(): Promise<void>;
  cancel(reason?: string): Promise<void>;
  /** Releases the native writer, cancelling it if still open. */
  dispose(): Promise<void>;
  info(): Promise<WriterInfo>;
  isClosed(): Promise<boolean>;
}
export interface FrameReader<T> extends AsyncIterable<T> {
  /** Null means timeout or terminal reader; use info to distinguish. */
  read(timeoutMs?: number): Promise<T | null>;
  info(): Promise<{ closed: boolean; droppedFrames: number }>;
  close(): Promise<void>;
}
export type AudioFrame = Required<Omit<C.CAudioFrame, 'data'>> & { data: Buffer };
export type VideoFrame = Required<Omit<C.CVideoFrame, 'data'>> & { data: Buffer; metadata: Required<C.CVideoFrameMetadata> };
export interface DataTrackFrame { data: Buffer; hasUserTimestamp: boolean; userTimestamp: bigint }
export type DataTrackInfo = Required<C.CDataTrackSnapshotInfo> & {
  sid: string; name: string; customFrameEncoding: string; schemaName: string; customSchemaEncoding: string;
};
export interface DataTrack {
  push(data: Uint8Array, userTimestamp?: bigint): Promise<void>;
  info(): Promise<DataTrackInfo & { published: boolean }>;
  unpublish(): Promise<void>;
  close(): Promise<void>;
}
export interface Recorder {
  stop(): Promise<void>;
  stats(): Promise<{ stats: Required<C.CTrackRecorderStats>; outputPath: string; error: string }>;
  close(): Promise<void>;
}
export type AdvancedEventMap = {
  textStream: [event: { type: 'textStream'; event: Required<C.CTextStreamEvent> }];
  byteStream: [event: { type: 'byteStream'; event: Required<C.CByteStreamEvent> }];
  streamProgress: [event: { type: 'streamProgress'; id: number; bytesSent: bigint; hasTotalSize: boolean; totalSize: bigint }];
  streamComplete: [event: { type: 'streamComplete'; id: number; completion: Required<C.CDataStreamCompletion> }];
  log: [event: { type: 'log'; record: Required<C.CLogRecord> }];
  trace: [event: { type: 'trace'; record: Required<C.CTraceRecord> }];
  sipDtmfReceived: [event: { type: 'sipDtmfReceived'; event: Required<C.CSipDtmf> }];
  chatMessageReceived: [event: { type: 'chatMessageReceived'; event: Required<C.CChatMessage> }];
  transcriptionReceived: [event: { type: 'transcriptionReceived'; event: Required<C.CTranscriptionReceived> }];
  metricsReceived: [event: { type: 'metricsReceived'; event: Required<C.CMetricsReceived> }];
  dataChannelBufferStatusChanged: [event: { type: 'dataChannelBufferStatusChanged'; event: Required<C.CDataChannelBufferStatus> }];
  encryptionStateChanged: [event: { type: 'encryptionStateChanged'; event: Required<C.CEncryptionState> }];
  roomUpdated: [event: { type: 'roomUpdated'; event: Required<C.CRoomSnapshot> }];
  roomMoved: [event: { type: 'roomMoved'; event: Required<C.CRoomSnapshot> }];
  roomSidChanged: [event: { type: 'roomSidChanged'; previousSid: string; sid: string }];
  recordingStatusChanged: [event: { type: 'recordingStatusChanged'; recording: boolean }];
  connectionQualityChanged: [event: { type: 'connectionQualityChanged'; quality: number; participant: C.CParticipantInfo | null }];
  activeSpeakersChanged: [event: { type: 'activeSpeakersChanged'; participants: C.CParticipantInfo[] }];
  participantsUpdated: [event: { type: 'participantsUpdated'; participants: C.CParticipantInfo[] }];
  participantMetadataChanged: [event: { type: 'participantMetadataChanged'; previousMetadata: string; participant: C.CParticipantInfo | null }];
  participantNameChanged: [event: { type: 'participantNameChanged'; name: string; participant: C.CParticipantInfo | null }];
  participantAttributesChanged: [event: { type: 'participantAttributesChanged'; changes: C.CAttribute[]; participant: C.CParticipantInfo | null }];
  participantPermissionsChanged: [event: { type: 'participantPermissionsChanged'; previousPermissions: C.CParticipantPermissions | null; permissions: C.CParticipantPermissions | null; participant: C.CParticipantInfo | null }];
  subscribedQualityUpdate: [event: { type: 'subscribedQualityUpdate'; track: Publication | null; participant: C.CParticipantInfo | null; update: Required<C.CSubscribedQualityUpdate> }];
  videoFrameWithMetadata: [event: { type: 'videoFrameWithMetadata'; track: Publication | null; participant: C.CParticipantInfo | null; frame: Required<C.CVideoFrame>; metadata: Required<C.CVideoFrameMetadata> | null }];
} & {
  [K in 'dataTrackPublished' | 'dataTrackUnpublished' | 'localDataTrackPublished' | 'localDataTrackUnpublished']:
    [event: { type: K; track: Required<C.CDataTrackInfo>; participant: C.CParticipantInfo | null }]
} & {
  dataTrackFrame: [event: { type: 'dataTrackFrame'; track: Required<C.CDataTrackInfo>; participant: C.CParticipantInfo | null; frame: Required<C.CDataTrackFrameView> }];
  localTrackSubscribed: [event: { type: 'localTrackSubscribed'; track: Publication | null; participant: C.CParticipantInfo | null }];
  trackSubscriptionPermissionChanged: [event: { type: 'trackSubscriptionPermissionChanged'; track: Publication | null; participant: C.CParticipantInfo | null; allowed: number }];
  trackSubscriptionFailed: [event: { type: 'trackSubscriptionFailed'; track: Publication | null; participant: C.CParticipantInfo | null; error: number }];
  trackStreamStateChanged: [event: { type: 'trackStreamStateChanged'; track: Publication | null; participant: C.CParticipantInfo | null; state: number }];
  trackSubscriptionStatusChanged: [event: { type: 'trackSubscriptionStatusChanged'; track: Publication | null; participant: C.CParticipantInfo | null; status: number }];
};
export type AdvancedEvent = AdvancedEventMap[keyof AdvancedEventMap][0];

export interface AdvancedRoom {
  readonly e2ee: E2EE;
  readonly diagnostics: Diagnostics;
  readonly output: OutputControls;
  screenSources(): Promise<Array<Required<C.CScreenSourceInfo> & { id: string; label: string }>>;
  updateRemoteTrackSettings(participantSid: string, trackSid: string, options: C.CRemoteTrackSettings): Promise<void>;
  setTrackSubscriptionPermissions(allParticipantsAllowed: boolean, permissions?: C.CParticipantTrackPermission[]): Promise<void>;
  republishAllTracks(): Promise<void>;
  sendChatMessage(message: string): Promise<{ id: string; timestamp: bigint }>;
  editChatMessage(messageId: string, originalTimestamp: bigint, message: string): Promise<void>;
  publishDtmf(code: number, digit: string): Promise<void>;
  createMicrophoneTrack(options?: C.CMicrophoneCaptureOptions, name?: string): Promise<LocalAudioTrack>;
  createSystemAudioTrack(options?: C.CSystemAudioCaptureOptions, name?: string): Promise<LocalAudioTrack>;
  createCameraTrack(options?: C.CCameraCaptureOptions, name?: string): Promise<LocalVideoTrack>;
  createScreenTrack(options?: C.CScreenCaptureOptions, name?: string): Promise<LocalVideoTrack>;
  streamText(options?: WithAttributes<C.CStreamTextOptions>): Promise<StreamWriter<string>>;
  streamBytes(options?: WithAttributes<C.CStreamBytesOptions>): Promise<StreamWriter<Uint8Array>>;
  registerTextStreamHandler(topic: string): Promise<void>;
  unregisterTextStreamHandler(topic: string): Promise<void>;
  registerByteStreamHandler(topic: string): Promise<void>;
  unregisterByteStreamHandler(topic: string): Promise<void>;
  createAudioStream(identity: string, trackSid: string, options?: C.CMediaStreamOptions): Promise<FrameReader<AudioFrame>>;
  createVideoStream(identity: string, trackSid: string, options?: C.CMediaStreamOptions): Promise<FrameReader<VideoFrame>>;
  startTrackRecording(identity: string, trackSid: string, options: C.CTrackRecorderOptions): Promise<Recorder>;
  storeDataTrackSchema(schema: C.CDataTrackSchemaId, definition: Uint8Array): Promise<void>;
  getDataTrackSchema(identity: string, schema: C.CDataTrackSchemaId): Promise<{ name: string; encoding: number; customEncoding: string; definition: Buffer }>;
  remoteDataTracks(): Promise<Array<DataTrackInfo & { publisherIdentity: string }>>;
  updateDataTrackSubscription(identity: string, trackSid: string, options?: C.CDataTrackSubscriptionOptions): Promise<void>;
  subscribeDataTrack(identity: string, trackSid: string, options?: C.CDataTrackSubscriptionOptions): Promise<FrameReader<DataTrackFrame>>;
  publishDataTrack(options: C.CDataTrackPublishOptions): Promise<DataTrack>;
}
