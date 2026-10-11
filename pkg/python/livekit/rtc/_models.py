"""Detached public values shared by room events and snapshots."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Callable

from ._native import LiveKitError, ParticipantInfo, TrackPublicationInfo, decode
from .e2ee import E2EEOptions


class ConnectionState(IntEnum):
    CONNECTING = 0
    CONNECTED = 1
    DISCONNECTING = 2
    DISCONNECTED = 3
    FAILED = 4
    RECONNECTING = 5


class ContinualGatheringPolicy(IntEnum):
    GATHER_ONCE = 0
    GATHER_CONTINUALLY = 1


class IceTransportType(IntEnum):
    NONE = 0
    RELAY = 1
    NO_HOST = 2
    ALL = 3


class ReconnectReason(IntEnum):
    UNKNOWN = 0
    SIGNAL_DISCONNECTED = 1
    MEDIA_FAILURE = 2


class SubscriptionError(IntEnum):
    UNKNOWN = 0
    CODEC_UNSUPPORTED = 1
    TRACK_NOT_FOUND = 2


class TrackStreamState(IntEnum):
    UNKNOWN = 0
    ACTIVE = 1
    PAUSED = 2


class TrackSubscriptionStatus(IntEnum):
    UNSUBSCRIBED = 0
    DESIRED = 1
    SUBSCRIBED = 2


@dataclass(frozen=True)
class IceServer:
    urls: tuple[str, ...] | list[str]
    username: str = ""
    password: str = ""


@dataclass(frozen=True)
class RtcConfiguration:
    ice_servers: tuple[IceServer, ...] | list[IceServer] = ()
    continual_gathering_policy: ContinualGatheringPolicy = ContinualGatheringPolicy.GATHER_CONTINUALLY
    ice_transport_type: IceTransportType = IceTransportType.ALL


@dataclass(frozen=True)
class ReconnectContext:
    retry_count: int
    elapsed_ms: int
    reason: ReconnectReason
    server_url: str


@dataclass(frozen=True)
class TokenSourceFetchOptions:
    room_name: str = ""
    participant_name: str = ""
    participant_identity: str = ""
    participant_metadata: str = ""
    participant_attributes: dict[str, str] = field(default_factory=dict)
    agent_name: str = ""
    agent_metadata: str = ""
    deployment: str = ""
    force_refresh: bool = False


@dataclass(frozen=True)
class TokenSourceResponse:
    server_url: str
    participant_token: str


@dataclass(frozen=True)
class AudioPlaybackStats:
    queued_frames: int
    played_frames: int
    dropped_frames: int
    underrun_frames: int
    buffered_duration_ms: int
    device_latency_ms: int
    estimated_delay_ms: int


@dataclass(frozen=True)
class ParticipantTrackPermission:
    participant_sid: str = ""
    participant_identity: str = ""
    allow_all: bool = False
    allowed_track_sids: tuple[str, ...] | list[str] = ()


@dataclass(frozen=True)
class RoomOptions:
    auto_subscribe: bool = True
    adaptive_stream: bool = False
    dynacast: bool = False
    join_retries: int | None = None
    reconnect_timeout_ms: int | None = None
    rtc_config: RtcConfiguration | None = None
    reconnect_policy: Callable[[ReconnectContext], int | None] | None = None
    e2ee: E2EEOptions | None = None
    encryption: E2EEOptions | None = None


@dataclass(frozen=True)
class ParticipantPermissions:
    can_subscribe: bool
    can_publish: bool
    can_publish_data: bool
    can_publish_sources: tuple[int, ...]
    hidden: bool
    recorder: bool
    can_update_metadata: bool
    agent: bool
    can_subscribe_metrics: bool
    can_manage_agent_session: bool

    @classmethod
    def from_native(cls, value) -> ParticipantPermissions:
        return cls(
            can_subscribe=bool(value.can_subscribe),
            can_publish=bool(value.can_publish),
            can_publish_data=bool(value.can_publish_data),
            can_publish_sources=tuple(value.can_publish_sources[index]
                for index in range(value.can_publish_source_count)),
            hidden=bool(value.hidden), recorder=bool(value.recorder),
            can_update_metadata=bool(value.can_update_metadata),
            agent=bool(value.agent),
            can_subscribe_metrics=bool(value.can_subscribe_metrics),
            can_manage_agent_session=bool(value.can_manage_agent_session),
        )


@dataclass(frozen=True)
class RoomSnapshot:
    sid: str
    name: str
    metadata: str
    is_recording: bool


@dataclass(frozen=True)
class DataChannelBufferStatus:
    reliable: bool
    buffered_amount: int
    high_water_mark: int
    low_water_mark: int
    backpressured: bool


@dataclass(frozen=True)
class SipDtmf:
    code: int
    digit: str
    participant_identity: str


@dataclass(frozen=True)
class TranscriptionSegment:
    id: str
    text: str
    language: str
    start_time: int
    end_time: int
    is_final: bool
    first_received_time: int
    last_received_time: int


@dataclass(frozen=True)
class Transcription:
    participant_identity: str
    track_id: str
    segments: tuple[TranscriptionSegment, ...]


@dataclass(frozen=True)
class MetricTimestamp:
    seconds: int
    nanos: int

    @classmethod
    def from_native(cls, value) -> MetricTimestamp:
        return cls(value.seconds, value.nanos)


@dataclass(frozen=True)
class MetricSample:
    timestamp_ms: int
    normalized_timestamp: MetricTimestamp | None
    value: float


@dataclass(frozen=True)
class TimeSeriesMetric:
    label: int
    participant_identity: int
    track_sid: int
    rid: int
    samples: tuple[MetricSample, ...]


@dataclass(frozen=True)
class EventMetric:
    label: int
    participant_identity: int
    track_sid: int
    rid: int
    start_timestamp_ms: int
    end_timestamp_ms: int | None
    normalized_start_timestamp: MetricTimestamp | None
    normalized_end_timestamp: MetricTimestamp | None
    metadata: str


@dataclass(frozen=True)
class Metrics:
    timestamp_ms: int
    normalized_timestamp: MetricTimestamp | None
    string_data: tuple[str, ...]
    time_series: tuple[TimeSeriesMetric, ...]
    events: tuple[EventMetric, ...]
    participant_identity: str


@dataclass(frozen=True)
class SubscribedQuality:
    quality: int
    enabled: bool


@dataclass(frozen=True)
class SubscribedCodec:
    codec: str
    qualities: tuple[SubscribedQuality, ...]


@dataclass(frozen=True)
class SubscribedQualityUpdate:
    track_sid: str
    qualities: tuple[SubscribedQuality, ...]
    codecs: tuple[SubscribedCodec, ...]


@dataclass(frozen=True)
class Participant:
    sid: str
    identity: str
    name: str
    metadata: str
    audio_level: float = 0.0
    connection_quality: int = 0
    is_speaking: bool = False
    is_local: bool = False
    attributes: dict[str, str] = field(default_factory=dict)
    track_publications: dict[str, TrackPublication] = field(default_factory=dict)
    permissions: ParticipantPermissions | None = None

    @classmethod
    def from_native(cls, info: ParticipantInfo) -> Participant:
        return cls(
            sid=decode(info.sid), identity=decode(info.identity),
            name=decode(info.name), metadata=decode(info.metadata),
            audio_level=info.audio_level, connection_quality=info.connection_quality,
            is_speaking=bool(info.is_speaking), is_local=bool(info.is_local),
        )


@dataclass(frozen=True)
class TrackPublication:
    sid: str
    name: str
    mime_type: str
    kind: int
    source: int
    width: int
    height: int
    is_muted: bool
    is_simulcasted: bool
    subscription_allowed: bool
    subscription_status: int = 0
    has_subscribed_track: bool = False

    @classmethod
    def from_native(cls, info: TrackPublicationInfo) -> TrackPublication:
        return cls(
            sid=decode(info.sid), name=decode(info.name),
            mime_type=decode(info.mime_type), kind=info.kind, source=info.source,
            width=info.width, height=info.height, is_muted=bool(info.is_muted),
            is_simulcasted=bool(info.is_simulcasted),
            subscription_allowed=bool(info.subscription_allowed),
        )


@dataclass(frozen=True)
class DataPacket:
    data: bytes
    topic: str
    participant_identity: str
    reliable: bool


@dataclass(frozen=True)
class ChatMessage:
    id: str
    timestamp: int
    message: str
    participant_identity: str
    edit_timestamp: int | None = None
    deleted: bool = False
    generated: bool = False


class StreamEventType(IntEnum):
    OPEN = 0
    CHUNK = 1
    CLOSED = 2
    FAILED = 3


@dataclass(frozen=True)
class TextMessage:
    stream_id: str
    text: str
    topic: str
    participant_identity: str
    reply_to_stream_id: str
    timestamp: int
    attached_stream_ids: tuple[str, ...]
    attributes: dict[str, str]


@dataclass(frozen=True)
class ByteMessage:
    data: bytes
    stream_id: str
    name: str
    mime_type: str
    topic: str
    participant_identity: str
    timestamp: int
    attributes: dict[str, str]


@dataclass(frozen=True)
class TextStreamEvent:
    type: StreamEventType
    stream_id: str
    mime_type: str
    topic: str
    participant_identity: str
    content: str
    chunk_index: int
    total_size: int | None
    reason: str
    attributes: dict[str, str]
    timestamp: int
    reply_to_stream_id: str
    attached_stream_ids: tuple[str, ...]


@dataclass(frozen=True)
class ByteStreamEvent:
    type: StreamEventType
    stream_id: str
    name: str
    mime_type: str
    topic: str
    participant_identity: str
    content: bytes
    chunk_index: int
    total_size: int | None
    reason: str
    attributes: dict[str, str]
    timestamp: int


class RpcError(LiveKitError):
    def __init__(self, message: str, code: int, data: str = ""):
        super().__init__(message, code)
        self.data = data


@dataclass(frozen=True)
class RpcInvocationData:
    request_id: str
    caller_identity: str
    payload: str
    response_timeout_ms: int


def copy_attributes(values, count: int) -> dict[str, str]:
    return {decode(values[index].key): decode(values[index].value)
            for index in range(count)} if values else {}


def copy_strings(values, count: int) -> tuple[str, ...]:
    return tuple(decode(values[index]) for index in range(count)) if values else ()
