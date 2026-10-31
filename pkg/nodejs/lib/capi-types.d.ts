// Generated from include/livekit/capi/livekit.h. Do not edit.
/// <reference types="node" />
export interface CErrorInfo {
  domain?: number;
  code?: number;
  messageSize?: number;
}
export interface CLogRecord {
  level?: number;
  source?: number;
  message?: string;
  file?: string;
  line?: number;
}
export interface CLogOptions {
  livekitLevel?: number;
  webrtcLevel?: number;
  websocketLevel?: number;
}
export interface CTraceRecord {
  phase?: number;
  category?: number;
  name?: string;
  timestampUs?: bigint;
  threadId?: bigint;
  correlationId?: bigint;
}
export interface CTraceOptions {
  enabled?: boolean;
  categoryMask?: bigint;
}
export interface CParticipantInfo {
  sid?: string;
  identity?: string;
  name?: string;
  metadata?: string;
  audioLevel?: number;
  connectionQuality?: number;
  isSpeaking?: boolean;
  isLocal?: boolean;
}
export interface CMediaDeviceInfo {
  kind?: number;
  isDefault?: boolean;
}
export interface CAudioPlaybackStats {
  queuedFrames?: bigint;
  playedFrames?: bigint;
  droppedFrames?: bigint;
  underrunFrames?: bigint;
  bufferedDurationMs?: number;
  deviceLatencyMs?: number;
  estimatedDelayMs?: number;
}
export interface CScreenSourceInfo {
  kind?: number;
  x?: number;
  y?: number;
  width?: number;
  height?: number;
}
export interface CParticipantPermissions {
  canSubscribe?: boolean;
  canPublish?: boolean;
  canPublishData?: boolean;
  canPublishSources?: number[];
  hidden?: boolean;
  recorder?: boolean;
  canUpdateMetadata?: boolean;
  agent?: boolean;
  canSubscribeMetrics?: boolean;
  canManageAgentSession?: boolean;
}
export interface CTrackPublicationInfo {
  sid?: string;
  name?: string;
  mimeType?: string;
  kind?: number;
  source?: number;
  width?: number;
  height?: number;
  isMuted?: boolean;
  isSimulcasted?: boolean;
  subscriptionAllowed?: boolean;
}
export interface CRemoteParticipantSnapshotInfo {
  audioLevel?: number;
  connectionQuality?: number;
  isSpeaking?: boolean;
}
export interface CLocalParticipantSnapshotInfo {
  audioLevel?: number;
  connectionQuality?: number;
  isSpeaking?: boolean;
}
export interface CDataStreamWriterInfo {
  kind?: number;
  hasTotalSize?: boolean;
  totalSize?: bigint;
  timestamp?: bigint;
  attributeCount?: number;
  attachedStreamIdCount?: number;
}
export interface CRemoteTrackPublicationSnapshotInfo {
  kind?: number;
  source?: number;
  width?: number;
  height?: number;
  isMuted?: boolean;
  isSimulcasted?: boolean;
  subscriptionAllowed?: boolean;
  subscriptionStatus?: number;
  hasSubscriptionError?: boolean;
  subscriptionError?: number;
  hasSubscribedTrack?: boolean;
  encryption?: number;
}
export interface CRemoteTrackSnapshotInfo {
  kind?: number;
  source?: number;
  streamState?: number;
  width?: number;
  height?: number;
  enabled?: boolean;
}
export interface CRtcTrackStats {
  direction?: number;
  timestampMs?: number;
  bytes?: bigint;
  packets?: bigint;
  packetsLost?: bigint;
  hasBitrateBps?: boolean;
  bitrateBps?: number;
  hasRoundTripTimeSeconds?: boolean;
  roundTripTimeSeconds?: number;
  hasJitterSeconds?: boolean;
  jitterSeconds?: number;
  hasAudioLevel?: boolean;
  audioLevel?: number;
  concealedSamples?: bigint;
  frameWidth?: number;
  frameHeight?: number;
  framesPerSecond?: number;
  frames?: bigint;
  framesDropped?: bigint;
  firCount?: bigint;
  pliCount?: bigint;
  nackCount?: bigint;
  qpSum?: bigint;
}
export interface CSubscribedQuality {
  quality?: number;
  enabled?: boolean;
}
export interface CSubscribedCodec {
  codec?: string;
  qualities?: CSubscribedQuality[];
}
export interface CSubscribedQualityUpdate {
  trackSid?: string;
  qualities?: CSubscribedQuality[];
  codecs?: CSubscribedCodec[];
}
export interface CEncryptionState {
  trackId?: string;
  participantIdentity?: string;
  kind?: number;
  direction?: number;
  enabled?: boolean;
  keyIndex?: number;
  state?: number;
}
export interface CFrameCryptorInfo {
  kind?: number;
  direction?: number;
  enabled?: boolean;
  keyIndex?: number;
  state?: number;
}
export interface CAudioFrame {
  data?: Uint8Array;
  sampleRate?: number;
  numChannels?: number;
  samplesPerChannel?: number;
}
export interface CVideoFrame {
  data?: Uint8Array;
  width?: number;
  height?: number;
  timestampUs?: bigint;
}
export interface CVideoPlane {
  offset?: number;
  size?: number;
  stride?: number;
}
export interface CVideoFrameMetadata {
  hasUserTimestampUs?: boolean;
  userTimestampUs?: bigint;
  hasFrameId?: boolean;
  frameId?: number;
  hasUserData?: boolean;
  userData?: Uint8Array;
}
export interface CVideoFrameInput {
  data?: Uint8Array;
  width?: number;
  height?: number;
  timestampUs?: bigint;
  rotation?: number;
  format?: number;
  planes?: CVideoPlane[];
  metadata?: CVideoFrameMetadata;
}
export interface CDataReceived {
  data?: Uint8Array;
  topic?: string;
  participantIdentity?: string;
  reliable?: boolean;
}
export interface CDataTrackInfo {
  publisherHandle?: number;
  sid?: string;
  name?: string;
  usesE2Ee?: number;
  hasFrameEncoding?: boolean;
  frameEncoding?: number;
  customFrameEncoding?: string;
  hasSchema?: boolean;
  schemaName?: string;
  schemaEncoding?: number;
  customSchemaEncoding?: string;
}
export interface CDataTrackFrameView {
  data?: Uint8Array;
  hasUserTimestamp?: boolean;
  userTimestamp?: bigint;
}
export interface CDataTrackSnapshotInfo {
  publisherHandle?: number;
  usesE2Ee?: number;
  hasFrameEncoding?: boolean;
  frameEncoding?: number;
  hasSchema?: boolean;
  schemaEncoding?: number;
  isPublished?: boolean;
}
export interface CSipDtmf {
  code?: number;
  digit?: string;
  participantIdentity?: string;
}
export interface CChatMessage {
  id?: string;
  timestamp?: bigint;
  hasEditTimestamp?: boolean;
  editTimestamp?: bigint;
  message?: string;
  deleted?: boolean;
  generated?: boolean;
  participantIdentity?: string;
}
export interface CTranscriptionSegment {
  id?: string;
  text?: string;
  language?: string;
  startTime?: bigint;
  endTime?: bigint;
  isFinal?: boolean;
  firstReceivedTime?: bigint;
  lastReceivedTime?: bigint;
}
export interface CTranscriptionReceived {
  transcribedParticipantIdentity?: string;
  trackId?: string;
  segments?: CTranscriptionSegment[];
}
export interface CMetricTimestamp {
  seconds?: bigint;
  nanos?: number;
}
export interface CMetricSample {
  timestampMs?: bigint;
  hasNormalizedTimestamp?: boolean;
  normalizedTimestamp?: CMetricTimestamp;
  value?: number;
}
export interface CTimeSeriesMetric {
  label?: number;
  participantIdentity?: number;
  trackSid?: number;
  rid?: number;
  samples?: CMetricSample[];
}
export interface CEventMetric {
  label?: number;
  participantIdentity?: number;
  trackSid?: number;
  rid?: number;
  startTimestampMs?: bigint;
  hasEndTimestampMs?: boolean;
  endTimestampMs?: bigint;
  hasNormalizedStartTimestamp?: boolean;
  normalizedStartTimestamp?: CMetricTimestamp;
  hasNormalizedEndTimestamp?: boolean;
  normalizedEndTimestamp?: CMetricTimestamp;
  metadata?: string;
}
export interface CMetricsReceived {
  timestampMs?: bigint;
  hasNormalizedTimestamp?: boolean;
  normalizedTimestamp?: CMetricTimestamp;
  stringData?: string[];
  timeSeries?: CTimeSeriesMetric[];
  events?: CEventMetric[];
  participantIdentity?: string;
}
export interface CAttribute {
  key?: string;
  value?: string;
}
export interface CTokenSourceFetchOptions {
  roomName?: string;
  participantName?: string;
  participantIdentity?: string;
  participantMetadata?: string;
  participantAttributes?: CAttribute[];
  agentName?: string;
  agentMetadata?: string;
  deployment?: string;
}
export interface CTokenSourceResponse {
  serverUrl?: string;
  participantToken?: string;
}
export interface CRoomSnapshot {
  sid?: string;
  name?: string;
  metadata?: string;
  isRecording?: boolean;
}
export interface CFileReceived {
  data?: Uint8Array;
  streamId?: string;
  name?: string;
  mimeType?: string;
  topic?: string;
  participantIdentity?: string;
  attributes?: CAttribute[];
  timestamp?: bigint;
}
export interface CTextReceived {
  streamId?: string;
  text?: string;
  topic?: string;
  participantIdentity?: string;
  replyToStreamId?: string;
  timestamp?: bigint;
  attachedStreamIds?: string[];
  attributes?: CAttribute[];
}
export interface CTextStreamEvent {
  type?: number;
  streamId?: string;
  mimeType?: string;
  topic?: string;
  participantIdentity?: string;
  content?: string;
  chunkIndex?: bigint;
  hasTotalSize?: boolean;
  totalSize?: bigint;
  reason?: string;
  attributes?: CAttribute[];
  timestamp?: bigint;
  replyToStreamId?: string;
  attachedStreamIds?: string[];
}
export interface CByteStreamEvent {
  type?: number;
  streamId?: string;
  name?: string;
  mimeType?: string;
  topic?: string;
  participantIdentity?: string;
  content?: Uint8Array;
  chunkIndex?: bigint;
  hasTotalSize?: boolean;
  totalSize?: bigint;
  reason?: string;
  attributes?: CAttribute[];
  timestamp?: bigint;
}
export interface CDataChannelBufferStatus {
  reliable?: boolean;
  bufferedAmount?: bigint;
  highWaterMark?: bigint;
  lowWaterMark?: bigint;
  backpressured?: number;
}
export interface CDataStreamCompletion {
  status?: number;
  streamId?: string;
  bytesSent?: bigint;
  hasTotalSize?: boolean;
  totalSize?: bigint;
  reason?: string;
  errorDomain?: number;
  errorCode?: number;
}
export interface CRpcInvocation {
  requestId?: string;
  callerIdentity?: string;
  payload?: string;
  responseTimeoutMs?: number;
}
export interface CRpcHandlerResult {
  payload?: string;
  errorCode?: number;
  errorMessage?: string;
  errorData?: string;
}
export interface CE2EeOptions {
  enabled?: boolean;
  sharedKey?: Uint8Array;
  ratchetSalt?: Uint8Array;
  unencryptedMagicBytes?: Uint8Array;
  ratchetWindowSize?: number;
  failureTolerance?: number;
  keyRingSize?: number;
  keyDerivation?: number;
}
export interface CIceServer {
  urls?: string[];
  username?: string;
  password?: string;
}
export interface CReconnectContext {
  retryCount?: number;
  elapsedMs?: bigint;
  reason?: number;
  serverUrl?: string;
}
export interface CRoomConnectOptions {
  autoSubscribe?: boolean;
  adaptiveStream?: boolean;
  dynacast?: boolean;
  joinRetries?: number;
  reconnectTimeoutMs?: number;
  iceServers?: CIceServer[];
  continualGatheringPolicy?: number;
  iceTransportType?: number;
  e2eeOptions?: CE2EeOptions;
}
export interface CAudioSourceOptions {
  sampleRate?: number;
  numChannels?: number;
  queueSizeMs?: number;
  echoCancellation?: boolean;
  autoGainControl?: boolean;
  noiseSuppression?: boolean;
}
export interface CMicrophoneCaptureOptions {
  deviceId?: string;
  queueSizeMs?: number;
  echoCancellation?: boolean;
  autoGainControl?: boolean;
  noiseSuppression?: boolean;
}
export interface CSystemAudioCaptureOptions {
  deviceId?: string;
  queueSizeMs?: number;
}
export interface CMicrophoneProcessingStats {
  captureFramesProcessed?: bigint;
  renderFramesProcessed?: bigint;
  captureProcessingErrors?: bigint;
  renderProcessingErrors?: bigint;
  framesDropped?: bigint;
  echoCancellationEnabled?: boolean;
  echoReturnLossAvailable?: boolean;
  echoReturnLossDb?: number;
  echoReturnLossEnhancementAvailable?: boolean;
  echoReturnLossEnhancementDb?: number;
  residualEchoLikelihoodAvailable?: boolean;
  residualEchoLikelihood?: number;
  residualEchoLikelihoodRecentMaxAvailable?: boolean;
  residualEchoLikelihoodRecentMax?: number;
  delayMedianAvailable?: boolean;
  delayMedianMs?: number;
  delayStandardDeviationAvailable?: boolean;
  delayStandardDeviationMs?: number;
  delayAvailable?: boolean;
  delayMs?: number;
}
export interface CVideoSourceOptions {
  isScreencast?: boolean;
}
export interface CCameraCaptureOptions {
  deviceId?: string;
  width?: number;
  height?: number;
  framesPerSecond?: number;
}
export interface CScreenCaptureOptions {
  sourceId?: string;
  framesPerSecond?: number;
  includeCursor?: boolean;
}
export interface CVideoEncoding {
  maxBitrate?: bigint;
  maxFramerate?: number;
}
export interface CFrameMetadataFeatures {
  userTimestamp?: boolean;
  frameId?: boolean;
  userData?: boolean;
}
export interface CTrackPublishOptions {
  source?: number;
  dtx?: boolean;
  red?: boolean;
  simulcast?: boolean;
  stream?: string;
  videoCodec?: number;
  scalabilityMode?: string;
  backupVideoCodecEnabled?: boolean;
  backupVideoCodec?: number;
  backupCodecPolicy?: number;
  videoEncoding?: CVideoEncoding;
  backupVideoEncoding?: CVideoEncoding;
  frameMetadataFeatures?: CFrameMetadataFeatures;
  preconnectBuffer?: boolean;
  hasDegradationPreference?: boolean;
  degradationPreference?: number;
}
export interface CDataPublishOptions {
  reliable?: boolean;
  topic?: string;
  destinationIdentities?: string[];
}
export interface CDataTrackSchemaId {
  name?: string;
  encoding?: number;
  customEncoding?: string;
}
export interface CDataTrackPublishOptions {
  name?: string;
  hasFrameEncoding?: boolean;
  frameEncoding?: number;
  customFrameEncoding?: string;
  hasSchema?: boolean;
  schema?: CDataTrackSchemaId;
}
export interface CDataTrackSubscriptionOptions {
  hasTargetFps?: boolean;
  targetFps?: number;
  bufferCapacity?: number;
  maxPartialFrames?: number;
}
export interface CMediaStreamOptions {
  capacity?: number;
}
export interface CTrackRecorderOptions {
  outputPath?: string;
  queueCapacity?: number;
}
export interface CTrackRecorderStats {
  state?: number;
  framesWritten?: bigint;
  bytesWritten?: bigint;
  framesDropped?: bigint;
}
export interface CFileSendOptions {
  topic?: string;
  mimeType?: string;
  destinationIdentities?: string[];
  chunkSize?: number;
  attributes?: CAttribute[];
  compress?: boolean;
}
export interface CTextSendOptions {
  topic?: string;
  destinationIdentities?: string[];
  attributes?: CAttribute[];
  replyToStreamId?: string;
  attachedStreamIds?: string[];
  chunkSize?: number;
  compress?: boolean;
}
export interface CByteSendOptions {
  topic?: string;
  mimeType?: string;
  name?: string;
  destinationIdentities?: string[];
  attributes?: CAttribute[];
  chunkSize?: number;
  compress?: boolean;
}
export interface CStreamTextOptions {
  topic?: string;
  destinationIdentities?: string[];
  attributes?: CAttribute[];
  replyToStreamId?: string;
  attachedStreamIds?: string[];
  streamId?: string;
  hasTotalSize?: boolean;
  totalSize?: bigint;
  chunkSize?: number;
  update?: boolean;
  version?: number;
  compress?: boolean;
}
export interface CStreamBytesOptions {
  topic?: string;
  mimeType?: string;
  name?: string;
  destinationIdentities?: string[];
  attributes?: CAttribute[];
  streamId?: string;
  hasTotalSize?: boolean;
  totalSize?: bigint;
  chunkSize?: number;
  compress?: boolean;
}
export interface CRpcPerformOptions {
  destinationIdentity?: string;
  method?: string;
  payload?: string;
  responseTimeoutMs?: number;
}
export interface CParticipantTrackPermission {
  participantSid?: string;
  participantIdentity?: string;
  allowAll?: boolean;
  allowedTrackSids?: string[];
}
export interface CRemoteTrackSettings {
  enabled?: boolean;
  hasVideoQuality?: boolean;
  videoQuality?: number;
  videoWidth?: number;
  videoHeight?: number;
  videoFps?: number;
  priority?: number;
}
