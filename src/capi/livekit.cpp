#include "livekit/capi/livekit.h"

#include "livekit/core/data_track.h"
#include "livekit/core/e2ee/e2ee_manager.h"
#include "livekit/core/livekit_client.h"
#include "livekit/core/logging.h"
#include "livekit/core/rpc.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/local_track_interface.h"
#include "livekit/core/track/remote_track_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace core = livekit::core;

static lk_track_source_t SnapshotTrackSource(core::TrackSource source) {
	switch (source) {
	case core::TrackSource::Camera:
		return LK_TRACK_SOURCE_CAMERA;
	case core::TrackSource::Microphone:
		return LK_TRACK_SOURCE_MICROPHONE;
	case core::TrackSource::ScreenShare:
		return LK_TRACK_SOURCE_SCREEN_SHARE;
	case core::TrackSource::ScreenShareAudio:
		return LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO;
	default:
		return LK_TRACK_SOURCE_UNKNOWN;
	}
}

class CRoomEvents;

struct AsyncRpcTask {
	std::atomic_bool completed{false};
	std::jthread worker;
};

struct RoomHandleState {
	std::atomic_bool alive{true};
	std::atomic_bool connected{false};
};

struct lk_room {
	std::unique_ptr<core::RoomInterface> room;
	std::unique_ptr<CRoomEvents> events;
	std::mutex callbacks_mutex;
	std::mutex callback_lifetime_mutex;
	std::mutex local_tracks_mutex;
	std::mutex local_data_tracks_mutex;
	std::mutex async_tasks_mutex;
	std::vector<lk_local_track_t*> local_tracks;
	std::vector<lk_local_data_track_t*> local_data_tracks;
	std::vector<std::shared_ptr<AsyncRpcTask>> async_rpc_tasks;
	lk_room_callbacks_t callbacks{};
	std::shared_ptr<RoomHandleState> state = std::make_shared<RoomHandleState>();
};

struct lk_audio_source {
	std::unique_ptr<core::AudioSourceInterface> source;
	std::atomic_size_t track_references{0};
	uint32_t sample_rate = 0;
	uint32_t num_channels = 0;
};

struct lk_video_source {
	std::unique_ptr<core::VideoSourceInterface> source;
	std::atomic_size_t track_references{0};
};

struct lk_local_track {
	std::unique_ptr<core::LocalTrackInterface> track;
	lk_room_t* owner = nullptr;
	lk_audio_source_t* audio_source = nullptr;
	lk_video_source_t* video_source = nullptr;
	std::shared_ptr<RoomHandleState> room_state;
	std::atomic_bool published{false};
};

struct lk_local_data_track {
	std::shared_ptr<core::LocalDataTrackInterface> track;
	lk_room_t* owner = nullptr;
	std::shared_ptr<RoomHandleState> room_state;
};

struct lk_remote_data_track_snapshot {
	core::RemoteDataTrackSnapshot value;
};

struct lk_remote_data_track_list {
	std::vector<lk_remote_data_track_snapshot_t> tracks;
};

struct lk_data_track_reader {
	std::shared_ptr<core::DataTrackReader> reader;
};

struct lk_data_track_frame {
	core::DataTrackFrame frame;
};

struct lk_data_track_schema {
	core::DataTrackSchema schema;
};

struct lk_rpc_result {
	core::RpcResult result;
};

struct CDataStreamCompletionState {
	std::mutex mutex;
	lk_data_stream_completion_callback callback = nullptr;
	void* user_data = nullptr;
	std::string stream_id;
	uint64_t bytes_sent = 0;
	std::optional<uint64_t> total_size;
	bool notified = false;
};

struct lk_text_stream_writer {
	std::unique_ptr<core::TextStreamWriterInterface> writer;
	std::shared_ptr<CDataStreamCompletionState> completion;
};

struct lk_byte_stream_writer {
	std::unique_ptr<core::ByteStreamWriterInterface> writer;
	std::shared_ptr<CDataStreamCompletionState> completion;
};

struct lk_remote_track_snapshot {
	explicit lk_remote_track_snapshot(core::RemoteTrackSnapshot source)
	    : value(std::move(source)) {}
	core::RemoteTrackSnapshot value;
};

struct lk_remote_track_publication_snapshot {
	explicit lk_remote_track_publication_snapshot(core::RemoteTrackPublicationSnapshot source)
	    : value(std::move(source)) {
		if (value.subscribed_track.has_value()) {
			track = std::make_unique<lk_remote_track_snapshot_t>(
			    std::move(value.subscribed_track.value()));
		}
	}
	lk_remote_track_publication_snapshot(lk_remote_track_publication_snapshot&&) noexcept = default;
	lk_remote_track_publication_snapshot&
	operator=(lk_remote_track_publication_snapshot&&) noexcept = default;
	lk_remote_track_publication_snapshot(const lk_remote_track_publication_snapshot&) = delete;
	lk_remote_track_publication_snapshot&
	operator=(const lk_remote_track_publication_snapshot&) = delete;
	core::RemoteTrackPublicationSnapshot value;
	std::unique_ptr<lk_remote_track_snapshot_t> track;
};

struct lk_remote_participant_snapshot {
	explicit lk_remote_participant_snapshot(core::RemoteParticipantSnapshot source)
	    : value(std::move(source)) {
		publish_sources.reserve(value.permissions.can_publish_sources.size());
		for (const auto source : value.permissions.can_publish_sources) {
			publish_sources.push_back(SnapshotTrackSource(source));
		}
		publications.reserve(value.publications.size());
		for (auto& publication : value.publications) {
			publications.emplace_back(std::move(publication));
		}
		value.publications.clear();
	}
	lk_remote_participant_snapshot(lk_remote_participant_snapshot&&) noexcept = default;
	lk_remote_participant_snapshot& operator=(lk_remote_participant_snapshot&&) noexcept = default;
	lk_remote_participant_snapshot(const lk_remote_participant_snapshot&) = delete;
	lk_remote_participant_snapshot& operator=(const lk_remote_participant_snapshot&) = delete;
	core::RemoteParticipantSnapshot value;
	std::vector<lk_track_source_t> publish_sources;
	std::vector<lk_remote_track_publication_snapshot_t> publications;
};

struct lk_remote_participant_list {
	std::vector<lk_remote_participant_snapshot_t> participants;
};

struct lk_media_device_list {
	std::vector<core::MediaDeviceInfo> devices;
};

struct lk_screen_source_list {
	std::vector<core::ScreenCaptureSourceInfo> sources;
};

struct lk_frame_cryptor_list {
	std::vector<core::FrameCryptorInfo> cryptors;
};

namespace {

struct CErrorState {
	lk_error_domain_t domain = LK_ERROR_DOMAIN_NONE;
	int32_t code = 0;
	std::string message;
};

thread_local CErrorState last_error;

lk_log_level_t FromCoreLogLevel(core::LogLevel level) {
	switch (level) {
	case core::LogLevel::Trace:
		return LK_LOG_LEVEL_TRACE;
	case core::LogLevel::Debug:
		return LK_LOG_LEVEL_DEBUG;
	case core::LogLevel::Info:
		return LK_LOG_LEVEL_INFO;
	case core::LogLevel::Warning:
		return LK_LOG_LEVEL_WARNING;
	case core::LogLevel::Error:
		return LK_LOG_LEVEL_ERROR;
	case core::LogLevel::Off:
		return LK_LOG_LEVEL_OFF;
	}
	return LK_LOG_LEVEL_OFF;
}

lk_log_source_t FromCoreLogSource(core::LogSource source) {
	switch (source) {
	case core::LogSource::LiveKit:
		return LK_LOG_SOURCE_LIVEKIT;
	case core::LogSource::WebRTC:
		return LK_LOG_SOURCE_WEBRTC;
	case core::LogSource::WebSocket:
		return LK_LOG_SOURCE_WEBSOCKET;
	}
	return LK_LOG_SOURCE_LIVEKIT;
}

class CLogSink final : public core::LogSinkInterface {
public:
	CLogSink(lk_log_callback callback, void* user_data)
	    : callback_(callback), user_data_(user_data) {}

	void OnLog(const core::LogRecord& record) override {
		lk_log_callback callback = nullptr;
		void* user_data = nullptr;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (callback_ == nullptr) {
				return;
			}
			++callbacks_in_progress_;
			callback = callback_;
			user_data = user_data_;
		}

		const lk_log_record_t c_record{sizeof(lk_log_record_t),
		                               FromCoreLogLevel(record.level),
		                               FromCoreLogSource(record.source),
		                               record.message.c_str(),
		                               record.file.c_str(),
		                               record.line};
		try {
			callback(user_data, &c_record);
		} catch (...) {
			// Exceptions must never escape a C ABI callback boundary.
		}

		{
			std::lock_guard<std::mutex> guard(mutex_);
			--callbacks_in_progress_;
		}
		callbacks_finished_.notify_all();
	}

	void Disable() {
		std::unique_lock<std::mutex> lock(mutex_);
		callback_ = nullptr;
		user_data_ = nullptr;
		callbacks_finished_.wait(lock, [this] { return callbacks_in_progress_ == 0; });
	}

private:
	std::mutex mutex_;
	std::condition_variable callbacks_finished_;
	lk_log_callback callback_ = nullptr;
	void* user_data_ = nullptr;
	size_t callbacks_in_progress_ = 0;
};

std::mutex c_log_sink_mutex;
std::shared_ptr<CLogSink> c_log_sink;

void ClearError() { last_error = {}; }

void SetError(lk_error_domain_t domain, int32_t code, std::string message) {
	last_error = {domain, code, std::move(message)};
}

void SetStatusError(lk_status_t status, std::string message) {
	SetError(LK_ERROR_DOMAIN_STATUS, static_cast<int32_t>(status), std::move(message));
}

void SetDataTrackError(lk_data_track_error_code_t code, std::string message) {
	SetError(LK_ERROR_DOMAIN_DATA_TRACK, static_cast<int32_t>(code), std::move(message));
}

lk_status_t Failure(lk_status_t status, const char* message) {
	SetStatusError(status, message);
	return status;
}

bool ToCoreLogLevel(lk_log_level_t level, core::LogLevel& result) {
	switch (level) {
	case LK_LOG_LEVEL_TRACE:
		result = core::LogLevel::Trace;
		return true;
	case LK_LOG_LEVEL_DEBUG:
		result = core::LogLevel::Debug;
		return true;
	case LK_LOG_LEVEL_INFO:
		result = core::LogLevel::Info;
		return true;
	case LK_LOG_LEVEL_WARNING:
		result = core::LogLevel::Warning;
		return true;
	case LK_LOG_LEVEL_ERROR:
		result = core::LogLevel::Error;
		return true;
	case LK_LOG_LEVEL_OFF:
		result = core::LogLevel::Off;
		return true;
	}
	return false;
}

template <typename Function> lk_status_t Guard(Function&& function) noexcept {
	try {
		ClearError();
		return function();
	} catch (const std::exception& exception) {
		SetStatusError(LK_STATUS_EXCEPTION, exception.what());
		return LK_STATUS_EXCEPTION;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "unknown C++ exception");
		return LK_STATUS_EXCEPTION;
	}
}

template <typename Function> size_t SizeGuard(Function&& function) noexcept {
	try {
		ClearError();
		return function();
	} catch (const std::exception& exception) {
		SetStatusError(LK_STATUS_EXCEPTION, exception.what());
		return 0;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "unknown C++ exception");
		return 0;
	}
}

lk_data_track_error_code_t DataTrackFailure(lk_data_track_error_code_t code, const char* message) {
	SetDataTrackError(code, message);
	return code;
}

template <typename Function>
lk_data_track_error_code_t DataTrackGuard(Function&& function) noexcept {
	try {
		ClearError();
		return function();
	} catch (const std::exception& exception) {
		SetDataTrackError(LK_DATA_TRACK_ERROR_SEND_FAILED, exception.what());
		return LK_DATA_TRACK_ERROR_SEND_FAILED;
	} catch (...) {
		SetDataTrackError(LK_DATA_TRACK_ERROR_SEND_FAILED, "unknown C++ exception");
		return LK_DATA_TRACK_ERROR_SEND_FAILED;
	}
}

size_t CopyString(const std::string& value, char* buffer, size_t buffer_size) noexcept {
	const size_t required = value.size() + 1;
	if (buffer != nullptr && buffer_size != 0) {
		const size_t count = std::min(value.size(), buffer_size - 1);
		std::memcpy(buffer, value.data(), count);
		buffer[count] = '\0';
	}
	return required;
}

size_t CopyBytes(const core::E2eeKey& value, uint8_t* buffer, size_t buffer_size) noexcept {
	if (buffer != nullptr && buffer_size != 0 && !value.empty()) {
		std::memcpy(buffer, value.data(), std::min(value.size(), buffer_size));
	}
	return value.size();
}

bool HasField(size_t struct_size, size_t offset, size_t field_size) {
	return struct_size >= offset + field_size;
}

template <typename Value>
lk_status_t CopyOutputStruct(const Value& value, Value* output, const char* description) {
	if (output == nullptr || output->struct_size < sizeof(output->struct_size)) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, description);
	}
	const auto output_size = output->struct_size;
	std::memcpy(output, &value, std::min(output_size, sizeof(value)));
	return LK_STATUS_OK;
}

size_t InvalidSizeResult(const char* message) {
	SetStatusError(LK_STATUS_INVALID_ARGUMENT, message);
	return 0;
}

void UpdateDataStreamProgress(const std::shared_ptr<CDataStreamCompletionState>& state,
                              uint64_t bytes_sent, std::optional<uint64_t> total_size) {
	if (!state) {
		return;
	}
	std::lock_guard<std::mutex> guard(state->mutex);
	state->bytes_sent = bytes_sent;
	state->total_size = total_size;
}

void NotifyDataStreamCompletion(const std::shared_ptr<CDataStreamCompletionState>& state,
                                lk_data_stream_completion_status_t status, std::string reason,
                                lk_status_t error_code = LK_STATUS_OPERATION_FAILED) {
	if (!state || state->callback == nullptr) {
		return;
	}
	lk_data_stream_completion_callback callback = nullptr;
	void* user_data = nullptr;
	std::string stream_id;
	uint64_t bytes_sent = 0;
	std::optional<uint64_t> total_size;
	{
		std::lock_guard<std::mutex> guard(state->mutex);
		if (state->notified) {
			return;
		}
		state->notified = true;
		callback = state->callback;
		user_data = state->user_data;
		stream_id = state->stream_id;
		bytes_sent = state->bytes_sent;
		total_size = state->total_size;
	}
	const lk_data_stream_completion_t completion{
	    status,
	    stream_id.c_str(),
	    bytes_sent,
	    total_size.has_value() ? 1 : 0,
	    total_size.value_or(0),
	    reason.c_str(),
	    status == LK_DATA_STREAM_COMPLETION_FAILED ? LK_ERROR_DOMAIN_STATUS : LK_ERROR_DOMAIN_NONE,
	    status == LK_DATA_STREAM_COMPLETION_FAILED ? error_code : 0};
	try {
		callback(user_data, &completion);
	} catch (...) {
		// Exceptions must never escape a C ABI callback boundary.
	}
}

#define LKC_HAS_FIELD(value, type, field)                                                          \
	HasField((value)->struct_size, offsetof(type, field), sizeof((value)->field))

bool ToCoreDataTrackFrameEncoding(lk_data_track_frame_encoding_t encoding,
                                  core::DataTrackFrameEncodingKind& result) {
	switch (encoding) {
	case LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED:
		result = core::DataTrackFrameEncodingKind::Unspecified;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_ROS1:
		result = core::DataTrackFrameEncodingKind::Ros1;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_CDR:
		result = core::DataTrackFrameEncodingKind::Cdr;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_PROTOBUF:
		result = core::DataTrackFrameEncodingKind::Protobuf;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_FLATBUFFER:
		result = core::DataTrackFrameEncodingKind::Flatbuffer;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_CBOR:
		result = core::DataTrackFrameEncodingKind::Cbor;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_MSGPACK:
		result = core::DataTrackFrameEncodingKind::Msgpack;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_JSON:
		result = core::DataTrackFrameEncodingKind::Json;
		return true;
	case LK_DATA_TRACK_FRAME_ENCODING_CUSTOM:
		result = core::DataTrackFrameEncodingKind::Custom;
		return true;
	default:
		return false;
	}
}

lk_data_track_frame_encoding_t
ToCDataTrackFrameEncoding(core::DataTrackFrameEncodingKind encoding) {
	switch (encoding) {
	case core::DataTrackFrameEncodingKind::Ros1:
		return LK_DATA_TRACK_FRAME_ENCODING_ROS1;
	case core::DataTrackFrameEncodingKind::Cdr:
		return LK_DATA_TRACK_FRAME_ENCODING_CDR;
	case core::DataTrackFrameEncodingKind::Protobuf:
		return LK_DATA_TRACK_FRAME_ENCODING_PROTOBUF;
	case core::DataTrackFrameEncodingKind::Flatbuffer:
		return LK_DATA_TRACK_FRAME_ENCODING_FLATBUFFER;
	case core::DataTrackFrameEncodingKind::Cbor:
		return LK_DATA_TRACK_FRAME_ENCODING_CBOR;
	case core::DataTrackFrameEncodingKind::Msgpack:
		return LK_DATA_TRACK_FRAME_ENCODING_MSGPACK;
	case core::DataTrackFrameEncodingKind::Json:
		return LK_DATA_TRACK_FRAME_ENCODING_JSON;
	case core::DataTrackFrameEncodingKind::Custom:
		return LK_DATA_TRACK_FRAME_ENCODING_CUSTOM;
	case core::DataTrackFrameEncodingKind::Unspecified:
	default:
		return LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED;
	}
}

bool ToCoreDataTrackSchemaEncoding(lk_data_track_schema_encoding_t encoding,
                                   core::DataTrackSchemaEncodingKind& result) {
	switch (encoding) {
	case LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED:
		result = core::DataTrackSchemaEncodingKind::Unspecified;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_PROTOBUF:
		result = core::DataTrackSchemaEncodingKind::Protobuf;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_FLATBUFFER:
		result = core::DataTrackSchemaEncodingKind::Flatbuffer;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_ROS1_MESSAGE:
		result = core::DataTrackSchemaEncodingKind::Ros1Message;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_MESSAGE:
		result = core::DataTrackSchemaEncodingKind::Ros2Message;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_IDL:
		result = core::DataTrackSchemaEncodingKind::Ros2Idl;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_OMG_IDL:
		result = core::DataTrackSchemaEncodingKind::OmgIdl;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA:
		result = core::DataTrackSchemaEncodingKind::JsonSchema;
		return true;
	case LK_DATA_TRACK_SCHEMA_ENCODING_CUSTOM:
		result = core::DataTrackSchemaEncodingKind::Custom;
		return true;
	default:
		return false;
	}
}

lk_data_track_schema_encoding_t
ToCDataTrackSchemaEncoding(core::DataTrackSchemaEncodingKind encoding) {
	switch (encoding) {
	case core::DataTrackSchemaEncodingKind::Protobuf:
		return LK_DATA_TRACK_SCHEMA_ENCODING_PROTOBUF;
	case core::DataTrackSchemaEncodingKind::Flatbuffer:
		return LK_DATA_TRACK_SCHEMA_ENCODING_FLATBUFFER;
	case core::DataTrackSchemaEncodingKind::Ros1Message:
		return LK_DATA_TRACK_SCHEMA_ENCODING_ROS1_MESSAGE;
	case core::DataTrackSchemaEncodingKind::Ros2Message:
		return LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_MESSAGE;
	case core::DataTrackSchemaEncodingKind::Ros2Idl:
		return LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_IDL;
	case core::DataTrackSchemaEncodingKind::OmgIdl:
		return LK_DATA_TRACK_SCHEMA_ENCODING_OMG_IDL;
	case core::DataTrackSchemaEncodingKind::JsonSchema:
		return LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA;
	case core::DataTrackSchemaEncodingKind::Custom:
		return LK_DATA_TRACK_SCHEMA_ENCODING_CUSTOM;
	case core::DataTrackSchemaEncodingKind::Unspecified:
	default:
		return LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED;
	}
}

lk_data_track_snapshot_info_t ToCDataTrackSnapshotInfo(const core::DataTrackInfo& source,
                                                       bool published) {
	return {sizeof(lk_data_track_snapshot_info_t),
	        source.publisher_handle,
	        source.uses_e2ee ? 1 : 0,
	        source.frame_encoding.has_value() ? 1 : 0,
	        source.frame_encoding ? ToCDataTrackFrameEncoding(source.frame_encoding->kind)
	                              : LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED,
	        source.schema.has_value() ? 1 : 0,
	        source.schema ? ToCDataTrackSchemaEncoding(source.schema->encoding.kind)
	                      : LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED,
	        published ? 1 : 0};
}

lk_data_track_error_code_t ToCDataTrackError(core::DataTrackError error) {
	if (!error) {
		return LK_DATA_TRACK_ERROR_NONE;
	}
	lk_data_track_error_code_t converted = LK_DATA_TRACK_ERROR_SEND_FAILED;
	switch (error.code) {
	case core::DataTrackErrorCode::InvalidName:
		converted = LK_DATA_TRACK_ERROR_INVALID_NAME;
		break;
	case core::DataTrackErrorCode::InvalidSchema:
		converted = LK_DATA_TRACK_ERROR_INVALID_SCHEMA;
		break;
	case core::DataTrackErrorCode::DuplicateName:
		converted = LK_DATA_TRACK_ERROR_DUPLICATE_NAME;
		break;
	case core::DataTrackErrorCode::HandleLimitReached:
		converted = LK_DATA_TRACK_ERROR_HANDLE_LIMIT_REACHED;
		break;
	case core::DataTrackErrorCode::NotAllowed:
		converted = LK_DATA_TRACK_ERROR_NOT_ALLOWED;
		break;
	case core::DataTrackErrorCode::Disconnected:
		converted = LK_DATA_TRACK_ERROR_DISCONNECTED;
		break;
	case core::DataTrackErrorCode::Timeout:
		converted = LK_DATA_TRACK_ERROR_TIMEOUT;
		break;
	case core::DataTrackErrorCode::Unpublished:
		converted = LK_DATA_TRACK_ERROR_UNPUBLISHED;
		break;
	case core::DataTrackErrorCode::QueueFull:
		converted = LK_DATA_TRACK_ERROR_QUEUE_FULL;
		break;
	case core::DataTrackErrorCode::InvalidFrame:
		converted = LK_DATA_TRACK_ERROR_INVALID_FRAME;
		break;
	case core::DataTrackErrorCode::ProtocolError:
		converted = LK_DATA_TRACK_ERROR_PROTOCOL;
		break;
	case core::DataTrackErrorCode::SendFailed:
		converted = LK_DATA_TRACK_ERROR_SEND_FAILED;
		break;
	case core::DataTrackErrorCode::NotFound:
		converted = LK_DATA_TRACK_ERROR_NOT_FOUND;
		break;
	case core::DataTrackErrorCode::None:
		converted = LK_DATA_TRACK_ERROR_NONE;
		break;
	default:
		break;
	}
	SetDataTrackError(converted, std::move(error.message));
	return converted;
}

lk_data_track_error_code_t ToCoreDataTrackSchemaId(const lk_data_track_schema_id_t* source,
                                                   core::DataTrackSchemaId& result) {
	if (source == nullptr || source->struct_size < sizeof(source->struct_size) ||
	    !LKC_HAS_FIELD(source, lk_data_track_schema_id_t, name) || source->name == nullptr ||
	    *source->name == '\0') {
		return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
		                        "initialized DataTrack schema ID and name are required");
	}
	result.name = source->name;
	if (LKC_HAS_FIELD(source, lk_data_track_schema_id_t, encoding) &&
	    !ToCoreDataTrackSchemaEncoding(source->encoding, result.encoding.kind)) {
		return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
		                        "invalid DataTrack schema encoding");
	}
	if (LKC_HAS_FIELD(source, lk_data_track_schema_id_t, custom_encoding) &&
	    source->custom_encoding != nullptr) {
		result.encoding.custom = source->custom_encoding;
	}
	if (result.encoding.kind == core::DataTrackSchemaEncodingKind::Custom &&
	    result.encoding.custom.empty()) {
		return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_SCHEMA,
		                        "custom DataTrack schema encoding is required");
	}
	return LK_DATA_TRACK_ERROR_NONE;
}

lk_data_track_error_code_t
ToCoreDataTrackPublishOptions(const lk_data_track_publish_options_t* source,
                              core::DataTrackPublishOptions& result) {
	if (source == nullptr || source->struct_size < sizeof(source->struct_size) ||
	    !LKC_HAS_FIELD(source, lk_data_track_publish_options_t, name) || source->name == nullptr ||
	    *source->name == '\0') {
		return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
		                        "initialized DataTrack publish options and name are required");
	}
	result.name = source->name;
	if (LKC_HAS_FIELD(source, lk_data_track_publish_options_t, has_frame_encoding) &&
	    source->has_frame_encoding) {
		core::DataTrackFrameEncoding encoding;
		if (!LKC_HAS_FIELD(source, lk_data_track_publish_options_t, frame_encoding) ||
		    !ToCoreDataTrackFrameEncoding(source->frame_encoding, encoding.kind)) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "invalid DataTrack frame encoding");
		}
		if (LKC_HAS_FIELD(source, lk_data_track_publish_options_t, custom_frame_encoding) &&
		    source->custom_frame_encoding != nullptr) {
			encoding.custom = source->custom_frame_encoding;
		}
		if (encoding.kind == core::DataTrackFrameEncodingKind::Custom && encoding.custom.empty()) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "custom DataTrack frame encoding is required");
		}
		result.frame_encoding = std::move(encoding);
	}
	if (LKC_HAS_FIELD(source, lk_data_track_publish_options_t, has_schema) && source->has_schema) {
		if (!LKC_HAS_FIELD(source, lk_data_track_publish_options_t, schema)) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "DataTrack schema is missing from publish options");
		}
		core::DataTrackSchemaId schema;
		const auto error = ToCoreDataTrackSchemaId(&source->schema, schema);
		if (error != LK_DATA_TRACK_ERROR_NONE) {
			return error;
		}
		result.schema = std::move(schema);
	}
	return LK_DATA_TRACK_ERROR_NONE;
}

lk_data_track_error_code_t
ToCoreDataTrackSubscriptionOptions(const lk_data_track_subscription_options_t* source,
                                   core::DataTrackSubscriptionOptions& result) {
	if (source == nullptr) {
		return LK_DATA_TRACK_ERROR_NONE;
	}
	if (source->struct_size < sizeof(source->struct_size)) {
		return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
		                        "invalid DataTrack subscription options struct size");
	}
	if (LKC_HAS_FIELD(source, lk_data_track_subscription_options_t, has_target_fps) &&
	    source->has_target_fps) {
		if (!LKC_HAS_FIELD(source, lk_data_track_subscription_options_t, target_fps) ||
		    source->target_fps == 0) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "DataTrack target FPS must be greater than zero");
		}
		result.target_fps = source->target_fps;
	}
	if (LKC_HAS_FIELD(source, lk_data_track_subscription_options_t, buffer_capacity)) {
		result.buffer_capacity = source->buffer_capacity;
	}
	if (LKC_HAS_FIELD(source, lk_data_track_subscription_options_t, max_partial_frames)) {
		result.max_partial_frames = source->max_partial_frames;
	}
	if (result.buffer_capacity == 0 || result.max_partial_frames == 0) {
		return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
		                        "DataTrack reader capacities must be greater than zero");
	}
	return LK_DATA_TRACK_ERROR_NONE;
}

core::TrackSource ToCoreTrackSource(lk_track_source_t source) {
	switch (source) {
	case LK_TRACK_SOURCE_CAMERA:
		return core::TrackSource::Camera;
	case LK_TRACK_SOURCE_MICROPHONE:
		return core::TrackSource::Microphone;
	case LK_TRACK_SOURCE_SCREEN_SHARE:
		return core::TrackSource::ScreenShare;
	case LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO:
		return core::TrackSource::ScreenShareAudio;
	default:
		return core::TrackSource::Unknown;
	}
}

bool ToCoreVideoCodec(lk_video_codec_t codec, core::VideoCodec& result) {
	switch (codec) {
	case LK_VIDEO_CODEC_VP8:
		result = core::VideoCodec::VP8;
		return true;
	case LK_VIDEO_CODEC_H264:
		result = core::VideoCodec::H264;
		return true;
	case LK_VIDEO_CODEC_VP9:
		result = core::VideoCodec::VP9;
		return true;
	case LK_VIDEO_CODEC_AV1:
		result = core::VideoCodec::AV1;
		return true;
	default:
		return false;
	}
}

bool ToCoreBackupCodecPolicy(lk_backup_codec_policy_t policy, core::BackupCodecPolicy& result) {
	switch (policy) {
	case LK_BACKUP_CODEC_POLICY_PREFER_REGRESSION:
		result = core::BackupCodecPolicy::PreferRegression;
		return true;
	case LK_BACKUP_CODEC_POLICY_SIMULCAST:
		result = core::BackupCodecPolicy::Simulcast;
		return true;
	case LK_BACKUP_CODEC_POLICY_REGRESSION:
		result = core::BackupCodecPolicy::Regression;
		return true;
	default:
		return false;
	}
}

lk_status_t ToCoreVideoEncoding(const lk_video_encoding_t* encoding, core::VideoEncoding& result) {
	if (encoding == nullptr || encoding->struct_size < sizeof(encoding->struct_size)) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video encoding struct size");
	}
	if (LKC_HAS_FIELD(encoding, lk_video_encoding_t, max_bitrate)) {
		result.max_bitrate = encoding->max_bitrate;
	}
	if (LKC_HAS_FIELD(encoding, lk_video_encoding_t, max_framerate)) {
		if (!std::isfinite(encoding->max_framerate) || encoding->max_framerate < 0.0F) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video encoding frame rate");
		}
		result.max_framerate = encoding->max_framerate;
	}
	return LK_STATUS_OK;
}

lk_status_t ToCoreE2eeOptions(const lk_e2ee_options_t* options, core::E2eeOptions& result) {
	if (options == nullptr || options->struct_size < sizeof(options->struct_size)) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid E2EE options struct size");
	}
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, enabled)) {
		result.enabled = options->enabled != 0;
	}
	auto copy_bytes = [&](const uint8_t* data, size_t size, core::E2eeKey& destination,
	                      const char* description) -> lk_status_t {
		if (size != 0 && data == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, description);
		}
		if (size != 0) {
			destination.assign(data, data + size);
		}
		return LK_STATUS_OK;
	};
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, shared_key_size)) {
		if (options->shared_key != nullptr || options->shared_key_size != 0) {
			core::E2eeKey key;
			const auto status = copy_bytes(options->shared_key, options->shared_key_size, key,
			                               "E2EE shared key data is invalid");
			if (status != LK_STATUS_OK) {
				return status;
			}
			if (key.empty()) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "E2EE shared key must not be empty");
			}
			result.shared_key = std::move(key);
		}
	}
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, ratchet_salt_size) &&
	    (options->ratchet_salt != nullptr || options->ratchet_salt_size != 0)) {
		result.key_provider.ratchet_salt.clear();
		const auto status =
		    copy_bytes(options->ratchet_salt, options->ratchet_salt_size,
		               result.key_provider.ratchet_salt, "E2EE ratchet salt data is invalid");
		if (status != LK_STATUS_OK) {
			return status;
		}
	}
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, unencrypted_magic_bytes_size) &&
	    (options->unencrypted_magic_bytes != nullptr ||
	     options->unencrypted_magic_bytes_size != 0)) {
		result.key_provider.unencrypted_magic_bytes.clear();
		const auto status =
		    copy_bytes(options->unencrypted_magic_bytes, options->unencrypted_magic_bytes_size,
		               result.key_provider.unencrypted_magic_bytes,
		               "E2EE unencrypted magic bytes are invalid");
		if (status != LK_STATUS_OK) {
			return status;
		}
	}
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, ratchet_window_size)) {
		result.key_provider.ratchet_window_size = options->ratchet_window_size;
	}
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, failure_tolerance)) {
		result.key_provider.failure_tolerance = options->failure_tolerance;
	}
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, key_ring_size)) {
		result.key_provider.key_ring_size = options->key_ring_size;
	}
	if (LKC_HAS_FIELD(options, lk_e2ee_options_t, key_derivation)) {
		switch (options->key_derivation) {
		case LK_E2EE_KEY_DERIVATION_PBKDF2_SHA256:
			result.key_provider.key_derivation = core::KeyDerivationFunction::Pbkdf2Sha256;
			break;
		case LK_E2EE_KEY_DERIVATION_HKDF_SHA256:
			result.key_provider.key_derivation = core::KeyDerivationFunction::HkdfSha256;
			break;
		default:
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid E2EE key derivation function");
		}
	}
	if (result.key_provider.ratchet_window_size > 256) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "E2EE ratchet window size must not exceed 256");
	}
	if (result.key_provider.failure_tolerance < -1) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "E2EE failure tolerance must be -1 or greater");
	}
	if (result.key_provider.key_ring_size == 0 || result.key_provider.key_ring_size > 256) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "E2EE key ring size must be between 1 and 256");
	}
	return LK_STATUS_OK;
}

lk_status_t ToCoreTrackPublishOptions(const lk_track_publish_options_t* options,
                                      core::TrackPublishOptions& result) {
	if (options == nullptr) {
		return LK_STATUS_OK;
	}
	if (options->struct_size < sizeof(options->struct_size)) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid publish options struct size");
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, source)) {
		result.source = ToCoreTrackSource(options->source);
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, dtx)) {
		result.dtx = options->dtx != 0;
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, red)) {
		result.red = options->red != 0;
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, simulcast)) {
		result.simulcast = options->simulcast != 0;
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, stream) && options->stream != nullptr) {
		result.stream = options->stream;
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, video_codec) &&
	    !ToCoreVideoCodec(options->video_codec, result.video_codec)) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video codec");
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, scalability_mode) &&
	    options->scalability_mode != nullptr) {
		result.scalability_mode = options->scalability_mode;
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, backup_video_codec_enabled) &&
	    options->backup_video_codec_enabled != 0) {
		core::VideoCodec backup_codec;
		if (!LKC_HAS_FIELD(options, lk_track_publish_options_t, backup_video_codec) ||
		    !ToCoreVideoCodec(options->backup_video_codec, backup_codec)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid backup video codec");
		}
		result.backup_video_codec = backup_codec;
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, backup_codec_policy) &&
	    !ToCoreBackupCodecPolicy(options->backup_codec_policy, result.backup_codec_policy)) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid backup codec policy");
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, video_encoding)) {
		const auto status = ToCoreVideoEncoding(&options->video_encoding, result.video_encoding);
		if (status != LK_STATUS_OK) {
			return status;
		}
	}
	if (LKC_HAS_FIELD(options, lk_track_publish_options_t, backup_video_encoding)) {
		const auto status =
		    ToCoreVideoEncoding(&options->backup_video_encoding, result.backup_video_encoding);
		if (status != LK_STATUS_OK) {
			return status;
		}
	}
	return LK_STATUS_OK;
}

lk_track_kind_t ToCTrackKind(core::TrackKind kind) {
	switch (kind) {
	case core::TrackKind::Audio:
		return LK_TRACK_KIND_AUDIO;
	case core::TrackKind::Video:
		return LK_TRACK_KIND_VIDEO;
	default:
		return LK_TRACK_KIND_UNKNOWN;
	}
}

lk_frame_cryptor_direction_t ToCFrameCryptorDirection(core::FrameCryptorDirection direction) {
	return direction == core::FrameCryptorDirection::Receiver ? LK_FRAME_CRYPTOR_DIRECTION_RECEIVER
	                                                          : LK_FRAME_CRYPTOR_DIRECTION_SENDER;
}

bool ToCoreFrameCryptorDirection(lk_frame_cryptor_direction_t direction,
                                 core::FrameCryptorDirection& result) {
	switch (direction) {
	case LK_FRAME_CRYPTOR_DIRECTION_SENDER:
		result = core::FrameCryptorDirection::Sender;
		return true;
	case LK_FRAME_CRYPTOR_DIRECTION_RECEIVER:
		result = core::FrameCryptorDirection::Receiver;
		return true;
	default:
		return false;
	}
}

lk_frame_cryptor_state_t ToCFrameCryptorState(core::FrameCryptorState state) {
	switch (state) {
	case core::FrameCryptorState::Ok:
		return LK_FRAME_CRYPTOR_STATE_OK;
	case core::FrameCryptorState::EncryptionFailed:
		return LK_FRAME_CRYPTOR_STATE_ENCRYPTION_FAILED;
	case core::FrameCryptorState::DecryptionFailed:
		return LK_FRAME_CRYPTOR_STATE_DECRYPTION_FAILED;
	case core::FrameCryptorState::MissingKey:
		return LK_FRAME_CRYPTOR_STATE_MISSING_KEY;
	case core::FrameCryptorState::KeyRatcheted:
		return LK_FRAME_CRYPTOR_STATE_KEY_RATCHETED;
	case core::FrameCryptorState::InternalError:
		return LK_FRAME_CRYPTOR_STATE_INTERNAL_ERROR;
	case core::FrameCryptorState::New:
	default:
		return LK_FRAME_CRYPTOR_STATE_NEW;
	}
}

lk_track_source_t ToCTrackSource(core::TrackSource source) { return SnapshotTrackSource(source); }

lk_connection_quality_t ToCConnectionQuality(core::ConnectionQuality quality) {
	switch (quality) {
	case core::ConnectionQuality::Poor:
		return LK_CONNECTION_QUALITY_POOR;
	case core::ConnectionQuality::Good:
		return LK_CONNECTION_QUALITY_GOOD;
	case core::ConnectionQuality::Excellent:
		return LK_CONNECTION_QUALITY_EXCELLENT;
	case core::ConnectionQuality::Lost:
		return LK_CONNECTION_QUALITY_LOST;
	default:
		return LK_CONNECTION_QUALITY_UNKNOWN;
	}
}

lk_subscription_error_t ToCSubscriptionError(core::SubscriptionError error) {
	switch (error) {
	case core::SubscriptionError::CodecUnsupported:
		return LK_SUBSCRIPTION_ERROR_CODEC_UNSUPPORTED;
	case core::SubscriptionError::TrackNotFound:
		return LK_SUBSCRIPTION_ERROR_TRACK_NOT_FOUND;
	default:
		return LK_SUBSCRIPTION_ERROR_UNKNOWN;
	}
}

lk_track_stream_state_t ToCTrackStreamState(core::TrackStreamState state) {
	switch (state) {
	case core::TrackStreamState::Active:
		return LK_TRACK_STREAM_STATE_ACTIVE;
	case core::TrackStreamState::Paused:
		return LK_TRACK_STREAM_STATE_PAUSED;
	default:
		return LK_TRACK_STREAM_STATE_UNKNOWN;
	}
}

lk_track_subscription_status_t ToCTrackSubscriptionStatus(core::TrackSubscriptionStatus status) {
	switch (status) {
	case core::TrackSubscriptionStatus::Desired:
		return LK_TRACK_SUBSCRIPTION_STATUS_DESIRED;
	case core::TrackSubscriptionStatus::Subscribed:
		return LK_TRACK_SUBSCRIPTION_STATUS_SUBSCRIBED;
	default:
		return LK_TRACK_SUBSCRIPTION_STATUS_UNSUBSCRIBED;
	}
}

lk_data_stream_event_type_t ToCDataStreamEventType(core::DataStreamEventType type) {
	switch (type) {
	case core::DataStreamEventType::Chunk:
		return LK_DATA_STREAM_EVENT_CHUNK;
	case core::DataStreamEventType::Closed:
		return LK_DATA_STREAM_EVENT_CLOSED;
	case core::DataStreamEventType::Failed:
		return LK_DATA_STREAM_EVENT_FAILED;
	default:
		return LK_DATA_STREAM_EVENT_OPEN;
	}
}

bool ToCoreVideoQuality(lk_video_quality_t quality, core::VideoQuality& result) {
	switch (quality) {
	case LK_VIDEO_QUALITY_LOW:
		result = core::VideoQuality::Low;
		return true;
	case LK_VIDEO_QUALITY_MEDIUM:
		result = core::VideoQuality::Medium;
		return true;
	case LK_VIDEO_QUALITY_HIGH:
		result = core::VideoQuality::High;
		return true;
	case LK_VIDEO_QUALITY_OFF:
		result = core::VideoQuality::Off;
		return true;
	default:
		return false;
	}
}

lk_video_quality_t ToCVideoQuality(core::VideoQuality quality) {
	return static_cast<lk_video_quality_t>(quality);
}

lk_room_state_t ToCRoomState(core::RoomInterface::RoomState state) {
	switch (state) {
	case core::RoomInterface::RoomState::Connecting:
		return LK_ROOM_STATE_CONNECTING;
	case core::RoomInterface::RoomState::Connected:
		return LK_ROOM_STATE_CONNECTED;
	case core::RoomInterface::RoomState::Disconnecting:
		return LK_ROOM_STATE_DISCONNECTING;
	case core::RoomInterface::RoomState::Failed:
		return LK_ROOM_STATE_FAILED;
	case core::RoomInterface::RoomState::Reconnecting:
		return LK_ROOM_STATE_RECONNECTING;
	default:
		return LK_ROOM_STATE_DISCONNECTED;
	}
}

lk_disconnect_reason_t ToCDisconnectReason(core::DisconnectReason reason) {
	return static_cast<lk_disconnect_reason_t>(reason);
}

struct OwnedParticipantInfo {
	explicit OwnedParticipantInfo(core::ParticipantInterface* participant) {
		if (participant == nullptr) {
			return;
		}
		sid = participant->Sid();
		identity = participant->Identity();
		name = participant->Name();
		metadata = participant->Metadata();
		info = {sid.c_str(),
		        identity.c_str(),
		        name.c_str(),
		        metadata.c_str(),
		        participant->AudioLevel(),
		        ToCConnectionQuality(participant->GetConnectionQuality()),
		        participant->IsSpeaking() ? 1 : 0,
		        participant->IsLocalParticipant() ? 1 : 0};
	}

	std::string sid;
	std::string identity;
	std::string name;
	std::string metadata;
	lk_participant_info_t info{};
};

struct OwnedDataTrackInfo {
	explicit OwnedDataTrackInfo(core::DataTrackInterface* track) {
		if (track == nullptr) {
			return;
		}
		value = track->Info();
		const auto* frame_encoding = value.frame_encoding ? &*value.frame_encoding : nullptr;
		const auto* schema = value.schema ? &*value.schema : nullptr;
		info = {value.publisher_handle,
		        value.sid.c_str(),
		        value.name.c_str(),
		        value.uses_e2ee ? 1 : 0,
		        frame_encoding != nullptr ? 1 : 0,
		        frame_encoding != nullptr ? ToCDataTrackFrameEncoding(frame_encoding->kind)
		                                  : LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED,
		        frame_encoding != nullptr ? frame_encoding->custom.c_str() : "",
		        schema != nullptr ? 1 : 0,
		        schema != nullptr ? schema->name.c_str() : "",
		        schema != nullptr ? ToCDataTrackSchemaEncoding(schema->encoding.kind)
		                          : LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED,
		        schema != nullptr ? schema->encoding.custom.c_str() : ""};
	}

	core::DataTrackInfo value;
	lk_data_track_info_t info{};
};

struct OwnedParticipantPermissions {
	explicit OwnedParticipantPermissions(const core::ParticipantPermissions& source) {
		publish_sources.reserve(source.can_publish_sources.size());
		for (const auto value : source.can_publish_sources) {
			publish_sources.push_back(ToCTrackSource(value));
		}
		permissions = {source.can_subscribe ? 1 : 0,
		               source.can_publish ? 1 : 0,
		               source.can_publish_data ? 1 : 0,
		               publish_sources.data(),
		               publish_sources.size(),
		               source.hidden ? 1 : 0,
		               source.recorder ? 1 : 0,
		               source.can_update_metadata ? 1 : 0,
		               source.agent ? 1 : 0,
		               source.can_subscribe_metrics ? 1 : 0,
		               source.can_manage_agent_session ? 1 : 0};
	}

	std::vector<lk_track_source_t> publish_sources;
	lk_participant_permissions_t permissions{};
};

struct OwnedTrackInfo {
	explicit OwnedTrackInfo(core::TrackPublicationInterface* publication) {
		if (publication == nullptr) {
			return;
		}
		sid = publication->Sid();
		name = publication->Name();
		mime_type = publication->MimeType();
		const auto dimensions = publication->Dimensions();
		info = {sid.c_str(),
		        name.c_str(),
		        mime_type.c_str(),
		        ToCTrackKind(publication->Kind()),
		        ToCTrackSource(publication->Source()),
		        dimensions.width,
		        dimensions.height,
		        publication->IsMuted() ? 1 : 0,
		        publication->IsSimulcasted() ? 1 : 0,
		        publication->IsSubscriptionAllowed() ? 1 : 0};
	}

	OwnedTrackInfo(core::RemoteTrackInterface* track,
	               core::RemoteParticipantInterface* participant) {
		core::TrackPublicationInterface* publication = nullptr;
		if (track != nullptr && participant != nullptr) {
			for (auto* candidate : participant->GetTrackPublications()) {
				if (candidate->Sid() == track->Sid()) {
					publication = candidate;
					break;
				}
			}
		}
		if (publication != nullptr) {
			sid = publication->Sid();
			name = publication->Name();
			mime_type = publication->MimeType();
			const auto dimensions = publication->Dimensions();
			info = {sid.c_str(),
			        name.c_str(),
			        mime_type.c_str(),
			        ToCTrackKind(publication->Kind()),
			        ToCTrackSource(publication->Source()),
			        dimensions.width,
			        dimensions.height,
			        publication->IsMuted() ? 1 : 0,
			        publication->IsSimulcasted() ? 1 : 0,
			        publication->IsSubscriptionAllowed() ? 1 : 0};
			return;
		}
		if (track == nullptr) {
			return;
		}
		sid = track->Sid();
		name = track->Name();
		const auto dimensions = track->Dimensions();
		info = {sid.c_str(),
		        name.c_str(),
		        "",
		        ToCTrackKind(track->Kind()),
		        ToCTrackSource(track->Source()),
		        dimensions.width,
		        dimensions.height,
		        0,
		        0,
		        1};
	}

	std::string sid;
	std::string name;
	std::string mime_type;
	lk_track_publication_info_t info{};
};

core::LocalParticipantInterface* LocalParticipant(lk_room_t* room) {
	return room != nullptr && room->room != nullptr ? room->room->GetLocalParticipant() : nullptr;
}

core::LocalParticipantInterface* LocalParticipant(const lk_room_t* room) {
	return room != nullptr && room->room != nullptr ? room->room->GetLocalParticipant() : nullptr;
}

core::E2EEManager* E2eeManager(const lk_room_t* room) {
	return room != nullptr && room->room != nullptr ? room->room->GetE2EEManager() : nullptr;
}

lk_status_t KeyOperationStatus(const core::KeyOperationResult& result) {
	if (result.Ok()) {
		return LK_STATUS_OK;
	}
	const auto& error = *result.error;
	switch (error.code) {
	case core::KeyProviderErrorCode::InvalidKeyIndex:
	case core::KeyProviderErrorCode::EmptyKey:
	case core::KeyProviderErrorCode::EmptyParticipantIdentity:
		return Failure(LK_STATUS_INVALID_ARGUMENT, error.message.c_str());
	case core::KeyProviderErrorCode::KeyNotFound:
		return Failure(LK_STATUS_INVALID_STATE, error.message.c_str());
	case core::KeyProviderErrorCode::CryptoFailure:
	default:
		return Failure(LK_STATUS_OPERATION_FAILED, error.message.c_str());
	}
}

template <typename Function> void InvokeRoomCallback(lk_room_t* owner, Function&& function) {
	if (owner == nullptr) {
		return;
	}
	std::lock_guard<std::mutex> lifetime_guard(owner->callback_lifetime_mutex);
	lk_room_callbacks_t callbacks{};
	{
		std::lock_guard<std::mutex> guard(owner->callbacks_mutex);
		callbacks = owner->callbacks;
	}
	function(callbacks);
}

std::vector<std::string> DestinationIdentities(const char* const* identities, size_t count) {
	std::vector<std::string> result;
	result.reserve(count);
	for (size_t index = 0; index < count; ++index) {
		if (identities[index] != nullptr) {
			result.emplace_back(identities[index]);
		}
	}
	return result;
}

std::vector<const char*> StringViews(const std::vector<std::string>& values) {
	std::vector<const char*> result;
	result.reserve(values.size());
	for (const auto& value : values) {
		result.push_back(value.c_str());
	}
	return result;
}

std::vector<lk_attribute_t> AttributeViews(const std::map<std::string, std::string>& attributes) {
	std::vector<lk_attribute_t> result;
	result.reserve(attributes.size());
	for (const auto& [key, value] : attributes) {
		result.push_back({key.c_str(), value.c_str()});
	}
	return result;
}

bool CopyAttributes(const lk_attribute_t* attributes, size_t count,
                    std::map<std::string, std::string>& result) {
	if (count != 0 && attributes == nullptr) {
		return false;
	}
	for (size_t index = 0; index < count; ++index) {
		if (attributes[index].key == nullptr || attributes[index].value == nullptr) {
			return false;
		}
		result[attributes[index].key] = attributes[index].value;
	}
	return true;
}

} // namespace

class CRoomEvents final : public core::RoomEventInterface {
public:
	explicit CRoomEvents(lk_room_t* owner) : owner_(owner) {}

	void OnConnected() override {
		InvokeRoomCallback(owner_, [this](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_connected != nullptr) {
				callbacks.on_connected(callbacks.user_data, owner_);
			}
		});
	}

	void OnDisconnected() override { OnDisconnected(core::DisconnectReason::Unknown); }

	void OnDisconnected(core::DisconnectReason reason) override {
		owner_->state->connected.store(false);
		InvokeRoomCallback(owner_, [this, reason](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_disconnected != nullptr) {
				callbacks.on_disconnected(callbacks.user_data, owner_);
			}
			if (callbacks.on_disconnected_with_reason != nullptr) {
				callbacks.on_disconnected_with_reason(callbacks.user_data, owner_,
				                                      ToCDisconnectReason(reason));
			}
		});
	}

	void OnReconnecting() override {
		owner_->state->connected.store(false);
		InvokeRoomCallback(owner_, [this](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_reconnecting != nullptr) {
				callbacks.on_reconnecting(callbacks.user_data, owner_);
			}
		});
	}

	void OnReconnected() override {
		owner_->state->connected.store(true);
		InvokeRoomCallback(owner_, [this](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_reconnected != nullptr) {
				callbacks.on_reconnected(callbacks.user_data, owner_);
			}
		});
	}

	void OnConnectionStateChanged(core::RoomState state) override {
		InvokeRoomCallback(owner_, [this, state](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_connection_state_changed != nullptr) {
				callbacks.on_connection_state_changed(callbacks.user_data, owner_,
				                                      ToCRoomState(state));
			}
		});
	}

	void OnParticipantConnected(core::RemoteParticipantInterface* participant) override {
		Participant(callbacks_member(&lk_room_callbacks_t::on_participant_connected), participant);
	}

	void OnParticipantDisconnected(core::RemoteParticipantInterface* participant) override {
		Participant(callbacks_member(&lk_room_callbacks_t::on_participant_disconnected),
		            participant);
	}

	void OnParticipantMetadataChanged(const std::string& previous_metadata,
	                                  core::ParticipantInterface* participant) override {
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_participant_metadata_changed != nullptr) {
				callbacks.on_participant_metadata_changed(callbacks.user_data, owner_,
				                                          previous_metadata.c_str(),
				                                          &owned_participant.info);
			}
		});
	}

	void OnParticipantNameChanged(const std::string& name,
	                              core::ParticipantInterface* participant) override {
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_participant_name_changed != nullptr) {
				callbacks.on_participant_name_changed(callbacks.user_data, owner_, name.c_str(),
				                                      &owned_participant.info);
			}
		});
	}

	void OnParticipantAttributesChanged(const std::map<std::string, std::string>& changes,
	                                    core::ParticipantInterface* participant) override {
		auto converted = AttributeViews(changes);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_participant_attributes_changed != nullptr) {
				callbacks.on_participant_attributes_changed(callbacks.user_data, owner_,
				                                            converted.data(), converted.size(),
				                                            &owned_participant.info);
			}
		});
	}

	void OnParticipantPermissionsChanged(const core::ParticipantPermissions& previous_permissions,
	                                     core::ParticipantInterface* participant) override {
		OwnedParticipantPermissions previous(previous_permissions);
		OwnedParticipantPermissions current(
		    participant != nullptr ? participant->Permissions() : core::ParticipantPermissions{});
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_participant_permissions_changed != nullptr) {
				callbacks.on_participant_permissions_changed(
				    callbacks.user_data, owner_, &previous.permissions, &current.permissions,
				    &owned_participant.info);
			}
		});
	}

	void OnTrackPublished(core::TrackPublicationInterface* track,
	                      core::RemoteParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_published), track, participant);
	}

	void OnTrackUnpublished(core::TrackPublicationInterface* track,
	                        core::RemoteParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_unpublished), track, participant);
	}

	void OnLocalTrackPublished(core::TrackPublicationInterface* track,
	                           core::ParticipantInterface* participant) override {
		MarkLocalTrackPublished(track, true);
		Track(callbacks_member(&lk_room_callbacks_t::on_local_track_published), track, participant);
	}

	void OnLocalTrackUnpublished(core::TrackPublicationInterface* track,
	                             core::ParticipantInterface* participant) override {
		MarkLocalTrackPublished(track, false);
		Track(callbacks_member(&lk_room_callbacks_t::on_local_track_unpublished), track,
		      participant);
	}

	void OnLocalTrackSubscribed(core::TrackPublicationInterface* track,
	                            core::ParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_local_track_subscribed), track,
		      participant);
	}

	void OnSubscribedQualityUpdate(core::TrackPublicationInterface* track,
	                               core::ParticipantInterface* participant,
	                               const core::SubscribedQualityUpdate& update) override {
		std::vector<lk_subscribed_quality_t> legacy_qualities;
		legacy_qualities.reserve(update.qualities.size());
		for (const auto& quality : update.qualities) {
			legacy_qualities.push_back({ToCVideoQuality(quality.quality), quality.enabled ? 1 : 0});
		}
		std::vector<std::vector<lk_subscribed_quality_t>> codec_quality_groups;
		codec_quality_groups.reserve(update.codecs.size());
		for (const auto& codec : update.codecs) {
			auto& qualities = codec_quality_groups.emplace_back();
			qualities.reserve(codec.qualities.size());
			for (const auto& quality : codec.qualities) {
				qualities.push_back({ToCVideoQuality(quality.quality), quality.enabled ? 1 : 0});
			}
		}
		std::vector<lk_subscribed_codec_t> codecs;
		codecs.reserve(update.codecs.size());
		for (std::size_t index = 0; index < update.codecs.size(); ++index) {
			const auto& codec = update.codecs[index];
			const auto& qualities = codec_quality_groups[index];
			codecs.push_back({codec.codec.c_str(), qualities.data(), qualities.size()});
		}
		const lk_subscribed_quality_update_t c_update{
		    update.track_sid.c_str(), legacy_qualities.data(), legacy_qualities.size(),
		    codecs.data(), codecs.size()};
		OwnedTrackInfo owned_track(track);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_subscribed_quality_update != nullptr) {
				callbacks.on_subscribed_quality_update(callbacks.user_data, owner_,
				                                       &owned_track.info, &owned_participant.info,
				                                       &c_update);
			}
		});
	}

	void OnEncryptionStateChanged(const core::EncryptionStateEvent& event) override {
		const lk_encryption_state_t state{event.cryptor.track_id.c_str(),
		                                  event.cryptor.participant_identity.c_str(),
		                                  ToCTrackKind(event.cryptor.kind),
		                                  ToCFrameCryptorDirection(event.cryptor.direction),
		                                  event.cryptor.enabled ? 1 : 0,
		                                  event.cryptor.key_index,
		                                  ToCFrameCryptorState(event.cryptor.state)};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_encryption_state_changed != nullptr) {
				callbacks.on_encryption_state_changed(callbacks.user_data, owner_, &state);
			}
		});
	}

	void OnDataTrackPublished(core::RemoteDataTrackInterface* track,
	                          core::RemoteParticipantInterface* participant) override {
		DataTrack(callbacks_member(&lk_room_callbacks_t::on_data_track_published), track,
		          participant);
	}

	void OnDataTrackUnpublished(core::DataTrackInterface* track,
	                            core::RemoteParticipantInterface* participant) override {
		DataTrack(callbacks_member(&lk_room_callbacks_t::on_data_track_unpublished), track,
		          participant);
	}

	void OnLocalDataTrackPublished(core::LocalDataTrackInterface* track,
	                               core::ParticipantInterface* participant) override {
		DataTrack(callbacks_member(&lk_room_callbacks_t::on_local_data_track_published), track,
		          participant);
	}

	void OnLocalDataTrackUnpublished(core::LocalDataTrackInterface* track,
	                                 core::ParticipantInterface* participant) override {
		DataTrack(callbacks_member(&lk_room_callbacks_t::on_local_data_track_unpublished), track,
		          participant);
	}

	void OnDataTrackFrame(core::RemoteDataTrackInterface* track,
	                      core::RemoteParticipantInterface* participant,
	                      const core::DataTrackFrame& frame) override {
		OwnedDataTrackInfo owned_track(track);
		OwnedParticipantInfo owned_participant(participant);
		const lk_data_track_frame_view_t view{frame.payload.data(), frame.payload.size(),
		                                      frame.user_timestamp.has_value() ? 1 : 0,
		                                      frame.user_timestamp.value_or(0)};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_data_track_frame != nullptr) {
				callbacks.on_data_track_frame(callbacks.user_data, owner_, &owned_track.info,
				                              &owned_participant.info, &view);
			}
		});
	}

	void OnTrackMuted(core::TrackPublicationInterface* track,
	                  core::ParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_muted), track, participant);
	}

	void OnTrackUnmuted(core::TrackPublicationInterface* track,
	                    core::ParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_unmuted), track, participant);
	}

	void OnTrackSubscribed(core::RemoteTrackInterface* track,
	                       core::RemoteParticipantInterface* participant) override {
		OwnedTrackInfo owned_track(track, participant);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_subscribed != nullptr) {
				callbacks.on_track_subscribed(callbacks.user_data, owner_, &owned_track.info,
				                              &owned_participant.info);
			}
		});
	}

	void OnTrackSubscriptionFailed(const std::string& track_sid,
	                               core::RemoteParticipantInterface* participant,
	                               core::SubscriptionError error) override {
		core::TrackPublicationInterface* publication = nullptr;
		if (participant != nullptr) {
			for (auto* candidate : participant->GetTrackPublications()) {
				if (candidate->Sid() == track_sid) {
					publication = candidate;
					break;
				}
			}
		}
		OwnedTrackInfo owned_track(publication);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_subscription_failed != nullptr) {
				callbacks.on_track_subscription_failed(callbacks.user_data, owner_,
				                                       &owned_track.info, &owned_participant.info,
				                                       ToCSubscriptionError(error));
			}
		});
	}

	void OnTrackUnsubscribed(core::RemoteTrackInterface*,
	                         core::TrackPublicationInterface* publication,
	                         core::RemoteParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_unsubscribed), publication,
		      participant);
	}

	void OnTrackStreamStateChanged(core::TrackPublicationInterface* publication,
	                               core::RemoteParticipantInterface* participant,
	                               core::TrackStreamState state) override {
		OwnedTrackInfo owned_track(publication);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_stream_state_changed != nullptr) {
				callbacks.on_track_stream_state_changed(callbacks.user_data, owner_,
				                                        &owned_track.info, &owned_participant.info,
				                                        ToCTrackStreamState(state));
			}
		});
	}

	void OnTrackSubscriptionStatusChanged(core::TrackPublicationInterface* publication,
	                                      core::RemoteParticipantInterface* participant,
	                                      core::TrackSubscriptionStatus status) override {
		OwnedTrackInfo owned_track(publication);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_subscription_status_changed != nullptr) {
				callbacks.on_track_subscription_status_changed(
				    callbacks.user_data, owner_, &owned_track.info, &owned_participant.info,
				    ToCTrackSubscriptionStatus(status));
			}
		});
	}

	void OnDataChannelBufferStatusChanged(const core::DataChannelBufferStatus& status) override {
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_data_channel_buffer_status_changed != nullptr) {
				const lk_data_channel_buffer_status_t converted{
				    status.reliable, status.buffered_amount, status.high_water_mark,
				    status.low_water_mark, status.backpressured};
				callbacks.on_data_channel_buffer_status_changed(callbacks.user_data, owner_,
				                                                &converted);
			}
		});
	}

	void OnTrackSubscriptionPermissionChanged(core::TrackPublicationInterface* track,
	                                          core::RemoteParticipantInterface* participant,
	                                          bool allowed) override {
		OwnedTrackInfo owned_track(track);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_subscription_permission_changed != nullptr) {
				callbacks.on_track_subscription_permission_changed(
				    callbacks.user_data, owner_, &owned_track.info, &owned_participant.info,
				    allowed ? 1 : 0);
			}
		});
	}

	void OnAudioFrame(core::RemoteTrackInterface* track,
	                  core::RemoteParticipantInterface* participant,
	                  const core::AudioFrame& frame) override {
		OwnedTrackInfo owned_track(track, participant);
		OwnedParticipantInfo owned_participant(participant);
		const lk_audio_frame_t c_frame{frame.data.data(), frame.data.size(), frame.sample_rate,
		                               frame.num_channels, frame.samples_per_channel};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_audio_frame != nullptr) {
				callbacks.on_audio_frame(callbacks.user_data, owner_, &owned_track.info,
				                         &owned_participant.info, &c_frame);
			}
		});
	}

	void OnVideoFrame(core::RemoteTrackInterface* track,
	                  core::RemoteParticipantInterface* participant,
	                  const core::VideoFrame& frame) override {
		OwnedTrackInfo owned_track(track, participant);
		OwnedParticipantInfo owned_participant(participant);
		const lk_video_frame_t c_frame{frame.data.data(), frame.data.size(), frame.width,
		                               frame.height, frame.timestamp_us};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_video_frame != nullptr) {
				callbacks.on_video_frame(callbacks.user_data, owner_, &owned_track.info,
				                         &owned_participant.info, &c_frame);
			}
		});
	}

	void OnDataReceived(const core::DataReceivedEvent& event) override {
		const lk_data_received_t c_event{event.payload.data(), event.payload.size(),
		                                 event.topic.c_str(), event.participant_identity.c_str(),
		                                 event.reliable ? 1 : 0};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_data_received != nullptr) {
				callbacks.on_data_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnSipDtmfReceived(const core::SipDtmfEvent& event) override {
		const lk_sip_dtmf_t c_event{event.code, event.digit.c_str(),
		                            event.participant_identity.c_str()};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_sip_dtmf_received != nullptr) {
				callbacks.on_sip_dtmf_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnChatMessageReceived(const core::ChatMessage& event) override {
		const lk_chat_message_t c_event{
		    event.id.c_str(),
		    event.timestamp,
		    event.edit_timestamp.has_value() ? 1 : 0,
		    event.edit_timestamp.value_or(0),
		    event.message.c_str(),
		    event.deleted ? 1 : 0,
		    event.generated ? 1 : 0,
		    event.participant_identity.c_str(),
		};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_chat_message_received != nullptr) {
				callbacks.on_chat_message_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnTranscriptionReceived(const core::TranscriptionReceivedEvent& event) override {
		std::vector<lk_transcription_segment_t> segments;
		segments.reserve(event.segments.size());
		for (const auto& segment : event.segments) {
			segments.push_back({segment.id.c_str(), segment.text.c_str(), segment.language.c_str(),
			                    segment.start_time, segment.end_time, segment.final ? 1 : 0,
			                    segment.first_received_time, segment.last_received_time});
		}
		const lk_transcription_received_t c_event{event.transcribed_participant_identity.c_str(),
		                                          event.track_id.c_str(), segments.data(),
		                                          segments.size()};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_transcription_received != nullptr) {
				callbacks.on_transcription_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnMetricsReceived(const core::MetricsReceivedEvent& event) override {
		auto timestamp = [](const std::optional<core::MetricTimestamp>& value) {
			return value.has_value() ? lk_metric_timestamp_t{value->seconds, value->nanos}
			                         : lk_metric_timestamp_t{};
		};
		std::vector<const char*> string_data;
		string_data.reserve(event.string_data.size());
		for (const auto& value : event.string_data) {
			string_data.push_back(value.c_str());
		}
		std::vector<std::vector<lk_metric_sample_t>> sample_groups;
		sample_groups.reserve(event.time_series.size());
		for (const auto& series : event.time_series) {
			auto& samples = sample_groups.emplace_back();
			samples.reserve(series.samples.size());
			for (const auto& sample : series.samples) {
				samples.push_back({sample.timestamp_ms,
				                   sample.normalized_timestamp.has_value() ? 1 : 0,
				                   timestamp(sample.normalized_timestamp), sample.value});
			}
		}
		std::vector<lk_time_series_metric_t> time_series;
		time_series.reserve(event.time_series.size());
		for (std::size_t index = 0; index < event.time_series.size(); ++index) {
			const auto& series = event.time_series[index];
			const auto& samples = sample_groups[index];
			time_series.push_back({series.label, series.participant_identity, series.track_sid,
			                       series.rid, samples.data(), samples.size()});
		}
		std::vector<lk_event_metric_t> metric_events;
		metric_events.reserve(event.events.size());
		for (const auto& metric : event.events) {
			metric_events.push_back(
			    {metric.label, metric.participant_identity, metric.track_sid, metric.rid,
			     metric.start_timestamp_ms, metric.end_timestamp_ms.has_value() ? 1 : 0,
			     metric.end_timestamp_ms.value_or(0),
			     metric.normalized_start_timestamp.has_value() ? 1 : 0,
			     timestamp(metric.normalized_start_timestamp),
			     metric.normalized_end_timestamp.has_value() ? 1 : 0,
			     timestamp(metric.normalized_end_timestamp), metric.metadata.c_str()});
		}
		const lk_metrics_received_t c_event{
		    event.timestamp_ms,
		    event.normalized_timestamp.has_value() ? 1 : 0,
		    timestamp(event.normalized_timestamp),
		    string_data.data(),
		    string_data.size(),
		    time_series.data(),
		    time_series.size(),
		    metric_events.data(),
		    metric_events.size(),
		    event.participant_identity.c_str(),
		};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_metrics_received != nullptr) {
				callbacks.on_metrics_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnTextReceived(const core::TextReceivedEvent& event) override {
		const auto attached_stream_ids = StringViews(event.attached_stream_ids);
		const auto attributes = AttributeViews(event.attributes);
		const lk_text_received_t c_event{event.stream_id.c_str(),
		                                 event.text.c_str(),
		                                 event.topic.c_str(),
		                                 event.participant_identity.c_str(),
		                                 event.reply_to_stream_id.c_str(),
		                                 event.timestamp,
		                                 attached_stream_ids.data(),
		                                 attached_stream_ids.size(),
		                                 attributes.data(),
		                                 attributes.size()};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_text_received != nullptr) {
				callbacks.on_text_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnByteReceived(const core::ByteReceivedEvent& event) override {
		File(callbacks_member(&lk_room_callbacks_t::on_byte_received), event);
	}

	void OnFileReceived(const core::FileReceivedEvent& event) override {
		File(callbacks_member(&lk_room_callbacks_t::on_file_received), event);
	}

private:
	void File(lk_file_received_callback lk_room_callbacks_t::*callback,
	          const core::FileReceivedEvent& event) {
		const auto attributes = AttributeViews(event.attributes);
		const lk_file_received_t c_event{event.data.data(),
		                                 event.data.size(),
		                                 event.stream_id.c_str(),
		                                 event.name.c_str(),
		                                 event.mime_type.c_str(),
		                                 event.topic.c_str(),
		                                 event.participant_identity.c_str(),
		                                 attributes.data(),
		                                 attributes.size(),
		                                 event.timestamp};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.*callback != nullptr) {
				(callbacks.*callback)(callbacks.user_data, owner_, &c_event);
			}
		});
	}

public:
	void OnRoomMetadataChanged(const std::string& metadata) override {
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_room_metadata_changed != nullptr) {
				callbacks.on_room_metadata_changed(callbacks.user_data, owner_, metadata.c_str());
			}
		});
	}

	void OnTokenRefreshed() override {
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_token_refreshed != nullptr) {
				callbacks.on_token_refreshed(callbacks.user_data, owner_);
			}
		});
	}

	void OnRoomUpdated(const core::RoomSnapshot& snapshot) override {
		const lk_room_snapshot_t value{sizeof(lk_room_snapshot_t), snapshot.sid.c_str(),
		                               snapshot.name.c_str(), snapshot.metadata.c_str(),
		                               snapshot.is_recording ? 1 : 0};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_room_updated != nullptr) {
				callbacks.on_room_updated(callbacks.user_data, owner_, &value);
			}
		});
	}

	void OnRoomMoved(const core::RoomSnapshot& snapshot) override {
		const lk_room_snapshot_t value{sizeof(lk_room_snapshot_t), snapshot.sid.c_str(),
		                               snapshot.name.c_str(), snapshot.metadata.c_str(),
		                               snapshot.is_recording ? 1 : 0};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_room_moved != nullptr) {
				callbacks.on_room_moved(callbacks.user_data, owner_, &value);
			}
		});
	}

	void OnRoomSidChanged(const std::string& previous_sid, const std::string& sid) override {
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_room_sid_changed != nullptr) {
				callbacks.on_room_sid_changed(callbacks.user_data, owner_, previous_sid.c_str(),
				                              sid.c_str());
			}
		});
	}

	void OnRoomEos() override {
		owner_->state->connected.store(false);
		InvokeRoomCallback(owner_, [this](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_room_eos != nullptr) {
				callbacks.on_room_eos(callbacks.user_data, owner_);
			}
		});
	}

	void
	OnParticipantsUpdated(const std::vector<core::ParticipantInterface*>& participants) override {
		std::vector<OwnedParticipantInfo> owned;
		owned.reserve(participants.size());
		for (auto* participant : participants) {
			owned.emplace_back(participant);
		}
		std::vector<lk_participant_info_t> values;
		values.reserve(owned.size());
		for (const auto& participant : owned) {
			values.push_back(participant.info);
		}
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_participants_updated != nullptr) {
				callbacks.on_participants_updated(callbacks.user_data, owner_, values.data(),
				                                  values.size());
			}
		});
	}

	void OnRecordingStatusChanged(bool recording) override {
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_recording_status_changed != nullptr) {
				callbacks.on_recording_status_changed(callbacks.user_data, owner_,
				                                      recording ? 1 : 0);
			}
		});
	}

	void OnConnectionQualityChanged(core::ConnectionQuality quality,
	                                core::ParticipantInterface* participant) override {
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_connection_quality_changed != nullptr) {
				callbacks.on_connection_quality_changed(callbacks.user_data, owner_,
				                                        ToCConnectionQuality(quality),
				                                        &owned_participant.info);
			}
		});
	}

	void
	OnActiveSpeakersChanged(const std::vector<core::ParticipantInterface*>& participants) override {
		std::vector<OwnedParticipantInfo> owned;
		owned.reserve(participants.size());
		for (auto* participant : participants) {
			owned.emplace_back(participant);
		}
		std::vector<lk_participant_info_t> infos;
		infos.reserve(owned.size());
		for (const auto& participant : owned) {
			infos.push_back(participant.info);
		}
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_active_speakers_changed != nullptr) {
				callbacks.on_active_speakers_changed(callbacks.user_data, owner_, infos.data(),
				                                     infos.size());
			}
		});
	}

private:
	template <typename Member> static Member callbacks_member(Member member) { return member; }

	void MarkLocalTrackPublished(core::TrackPublicationInterface* publication, bool published) {
		if (owner_ == nullptr || publication == nullptr) {
			return;
		}
		auto* local_track = publication->Track();
		std::lock_guard<std::mutex> guard(owner_->local_tracks_mutex);
		for (auto* handle : owner_->local_tracks) {
			if (handle != nullptr && handle->track.get() == local_track) {
				handle->published.store(published);
				break;
			}
		}
	}

	void Participant(lk_participant_event_callback lk_room_callbacks_t::*callback,
	                 core::ParticipantInterface* participant) {
		OwnedParticipantInfo owned(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.*callback != nullptr) {
				(callbacks.*callback)(callbacks.user_data, owner_, &owned.info);
			}
		});
	}

	void Track(lk_track_event_callback lk_room_callbacks_t::*callback,
	           core::TrackPublicationInterface* track, core::ParticipantInterface* participant) {
		OwnedTrackInfo owned_track(track);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.*callback != nullptr) {
				(callbacks.*callback)(callbacks.user_data, owner_, &owned_track.info,
				                      &owned_participant.info);
			}
		});
	}

	void DataTrack(lk_data_track_event_callback lk_room_callbacks_t::*callback,
	               core::DataTrackInterface* track, core::ParticipantInterface* participant) {
		OwnedDataTrackInfo owned_track(track);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.*callback != nullptr) {
				(callbacks.*callback)(callbacks.user_data, owner_, &owned_track.info,
				                      &owned_participant.info);
			}
		});
	}

	lk_room_t* owner_;
};

namespace {

enum class PublishMode { Track, ScreenShareVideo, ScreenShareAudio };

lk_status_t PublishLocalTrack(lk_room_t* room, lk_local_track_t* track,
                              const lk_track_publish_options_t* options, PublishMode mode) {
	auto* participant = LocalParticipant(room);
	if (participant == nullptr || track == nullptr || track->track == nullptr) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "room and track are required");
	}
	if (track->owner != room) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "track belongs to a different room");
	}
	if (track->published.load()) {
		return Failure(LK_STATUS_INVALID_STATE, "track is already published");
	}
	core::TrackPublishOptions publish_options;
	const auto options_status = ToCoreTrackPublishOptions(options, publish_options);
	if (options_status != LK_STATUS_OK) {
		return options_status;
	}

	bool published = false;
	switch (mode) {
	case PublishMode::ScreenShareVideo:
		published = participant->PublishScreenShareVideoTrack(track->track.get(), publish_options);
		break;
	case PublishMode::ScreenShareAudio:
		published = participant->PublishScreenShareAudioTrack(track->track.get(), publish_options);
		break;
	case PublishMode::Track:
		published = participant->PublishTrack(track->track.get(), publish_options);
		break;
	}
	if (!published) {
		return Failure(LK_STATUS_OPERATION_FAILED, "failed to publish track");
	}
	track->published.store(true);
	return LK_STATUS_OK;
}

lk_status_t ToCorePerformRpcParams(const lk_rpc_perform_options_t* options,
                                   core::PerformRpcParams& params) {
	if (options == nullptr || options->struct_size < sizeof(options->struct_size)) {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "valid RPC options are required");
	}
	if (!LKC_HAS_FIELD(options, lk_rpc_perform_options_t, destination_identity) ||
	    options->destination_identity == nullptr || *options->destination_identity == '\0' ||
	    !LKC_HAS_FIELD(options, lk_rpc_perform_options_t, method) || options->method == nullptr ||
	    *options->method == '\0') {
		return Failure(LK_STATUS_INVALID_ARGUMENT, "RPC destination and method are required");
	}
	params.destination_identity = options->destination_identity;
	params.method = options->method;
	if (LKC_HAS_FIELD(options, lk_rpc_perform_options_t, payload) && options->payload != nullptr) {
		params.payload = options->payload;
	}
	if (LKC_HAS_FIELD(options, lk_rpc_perform_options_t, response_timeout_ms) &&
	    options->response_timeout_ms != 0) {
		params.response_timeout = std::chrono::milliseconds(options->response_timeout_ms);
	}
	return LK_STATUS_OK;
}

} // namespace

extern "C" {

lk_status_t lk_init(void) {
	return Guard([] {
		return core::Init() ? LK_STATUS_OK
		                    : Failure(LK_STATUS_OPERATION_FAILED, "failed to initialize LiveKit");
	});
}

lk_status_t lk_shutdown(void) {
	return Guard([] {
		return core::Destroy() ? LK_STATUS_OK
		                       : Failure(LK_STATUS_OPERATION_FAILED, "failed to shut down LiveKit");
	});
}

size_t lk_version(char* buffer, size_t buffer_size) {
	try {
		ClearError();
		return CopyString(core::Version(), buffer, buffer_size);
	} catch (const std::exception& exception) {
		SetStatusError(LK_STATUS_EXCEPTION, exception.what());
		return 0;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "unknown C++ exception");
		return 0;
	}
}

void lk_error_info_init(lk_error_info_t* info) {
	if (info != nullptr) {
		*info = {};
		info->struct_size = sizeof(*info);
	}
}

const char* lk_last_error(void) { return last_error.message.c_str(); }

lk_status_t lk_last_error_info(lk_error_info_t* info) {
	try {
		if (info == nullptr || info->struct_size < sizeof(info->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "initialized error info is required");
		}
		const lk_error_info_t value{sizeof(value), last_error.domain, last_error.code,
		                            last_error.message.size() + 1};
		const auto output_size = info->struct_size;
		std::memcpy(info, &value, std::min(output_size, sizeof(value)));
		return LK_STATUS_OK;
	} catch (const std::exception& exception) {
		SetStatusError(LK_STATUS_EXCEPTION, exception.what());
		return LK_STATUS_EXCEPTION;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "unknown C++ exception");
		return LK_STATUS_EXCEPTION;
	}
}

size_t lk_last_error_message(char* buffer, size_t buffer_size) {
	return CopyString(last_error.message, buffer, buffer_size);
}

void lk_log_options_init(lk_log_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->livekit_level = LK_LOG_LEVEL_INFO;
		options->webrtc_level = LK_LOG_LEVEL_WARNING;
		options->websocket_level = LK_LOG_LEVEL_WARNING;
	}
}

lk_status_t lk_log_set_options(const lk_log_options_t* options) {
	return Guard([&] {
		if (options == nullptr || options->struct_size < sizeof(options->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "valid log options are required");
		}
		const auto current = core::GetLogOptions();
		core::LogOptions converted = current;
		if (LKC_HAS_FIELD(options, lk_log_options_t, livekit_level) &&
		    !ToCoreLogLevel(options->livekit_level, converted.livekit_level)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid LiveKit log level");
		}
		if (LKC_HAS_FIELD(options, lk_log_options_t, webrtc_level) &&
		    !ToCoreLogLevel(options->webrtc_level, converted.webrtc_level)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid WebRTC log level");
		}
		if (LKC_HAS_FIELD(options, lk_log_options_t, websocket_level) &&
		    !ToCoreLogLevel(options->websocket_level, converted.websocket_level)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid WebSocket log level");
		}
		core::SetLogOptions(converted);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_log_get_options(lk_log_options_t* options) {
	return Guard([&] {
		const auto current = core::GetLogOptions();
		const lk_log_options_t result{
		    sizeof(lk_log_options_t), FromCoreLogLevel(current.livekit_level),
		    FromCoreLogLevel(current.webrtc_level), FromCoreLogLevel(current.websocket_level)};
		return CopyOutputStruct(result, options, "log options output is invalid");
	});
}

lk_status_t lk_log_set_callback(lk_log_callback callback, void* user_data) {
	return Guard([&] {
		std::shared_ptr<CLogSink> replacement;
		if (callback != nullptr) {
			replacement = std::make_shared<CLogSink>(callback, user_data);
		}

		std::lock_guard<std::mutex> guard(c_log_sink_mutex);
		auto previous = std::move(c_log_sink);
		core::SetLogSink(replacement);
		if (previous) {
			previous->Disable();
		}
		c_log_sink = std::move(replacement);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_media_device_list_create(lk_media_device_list_t** devices) {
	return Guard([&] {
		if (devices == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "devices output must not be null");
		}
		auto result = std::make_unique<lk_media_device_list_t>();
		result->devices = core::EnumerateMediaDevices();
		*devices = result.release();
		return LK_STATUS_OK;
	});
}

void lk_media_device_list_destroy(lk_media_device_list_t* devices) { delete devices; }

size_t lk_media_device_list_count(const lk_media_device_list_t* devices) {
	return SizeGuard([&] { return devices != nullptr ? devices->devices.size() : 0; });
}

lk_status_t lk_media_device_list_info(const lk_media_device_list_t* devices, size_t index,
                                      lk_media_device_info_t* info) {
	return Guard([&] {
		if (devices == nullptr || index >= devices->devices.size()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "media device index is out of range");
		}
		const auto& device = devices->devices[index];
		lk_media_device_kind_t kind = LK_MEDIA_DEVICE_KIND_AUDIO_INPUT;
		if (device.kind == core::MediaDeviceKind::AudioOutput) {
			kind = LK_MEDIA_DEVICE_KIND_AUDIO_OUTPUT;
		} else if (device.kind == core::MediaDeviceKind::VideoInput) {
			kind = LK_MEDIA_DEVICE_KIND_VIDEO_INPUT;
		}
		return CopyOutputStruct(
		    lk_media_device_info_t{sizeof(lk_media_device_info_t), kind, device.is_default ? 1 : 0},
		    info, "media device info is invalid");
	});
}

size_t lk_media_device_list_id(const lk_media_device_list_t* devices, size_t index, char* buffer,
                               size_t buffer_size) {
	return SizeGuard([&] {
		if (devices == nullptr || index >= devices->devices.size()) {
			return InvalidSizeResult("media device index is out of range");
		}
		return CopyString(devices->devices[index].id, buffer, buffer_size);
	});
}

size_t lk_media_device_list_label(const lk_media_device_list_t* devices, size_t index, char* buffer,
                                  size_t buffer_size) {
	return SizeGuard([&] {
		if (devices == nullptr || index >= devices->devices.size()) {
			return InvalidSizeResult("media device index is out of range");
		}
		return CopyString(devices->devices[index].label, buffer, buffer_size);
	});
}

lk_status_t lk_screen_source_list_create(lk_screen_source_list_t** sources) {
	return Guard([&] {
		if (sources == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "sources output must not be null");
		}
		*sources = nullptr;
		auto result = std::make_unique<lk_screen_source_list_t>();
		result->sources = core::EnumerateScreenCaptureSources();
		*sources = result.release();
		return LK_STATUS_OK;
	});
}

void lk_screen_source_list_destroy(lk_screen_source_list_t* sources) { delete sources; }

size_t lk_screen_source_list_count(const lk_screen_source_list_t* sources) {
	return SizeGuard([&] { return sources != nullptr ? sources->sources.size() : 0; });
}

lk_status_t lk_screen_source_list_info(const lk_screen_source_list_t* sources, size_t index,
                                       lk_screen_source_info_t* info) {
	return Guard([&] {
		if (sources == nullptr || index >= sources->sources.size()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "screen source index is out of range");
		}
		const auto& source = sources->sources[index];
		const auto kind = source.kind == core::ScreenCaptureSourceKind::Monitor
		                      ? LK_SCREEN_SOURCE_KIND_MONITOR
		                      : LK_SCREEN_SOURCE_KIND_WINDOW;
		return CopyOutputStruct(lk_screen_source_info_t{sizeof(lk_screen_source_info_t), kind,
		                                                source.x, source.y, source.width,
		                                                source.height},
		                        info, "screen source info is invalid");
	});
}

size_t lk_screen_source_list_id(const lk_screen_source_list_t* sources, size_t index, char* buffer,
                                size_t buffer_size) {
	return SizeGuard([&] {
		if (sources == nullptr || index >= sources->sources.size()) {
			return InvalidSizeResult("screen source index is out of range");
		}
		return CopyString(sources->sources[index].id, buffer, buffer_size);
	});
}

size_t lk_screen_source_list_label(const lk_screen_source_list_t* sources, size_t index,
                                   char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		if (sources == nullptr || index >= sources->sources.size()) {
			return InvalidSizeResult("screen source index is out of range");
		}
		return CopyString(sources->sources[index].label, buffer, buffer_size);
	});
}

void lk_room_callbacks_init(lk_room_callbacks_t* callbacks) {
	if (callbacks != nullptr) {
		*callbacks = {};
		callbacks->struct_size = sizeof(*callbacks);
	}
}

void lk_token_source_fetch_options_init(lk_token_source_fetch_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
	}
}

void lk_token_source_response_init(lk_token_source_response_t* response) {
	if (response != nullptr) {
		*response = {};
		response->struct_size = sizeof(*response);
	}
}

void lk_room_snapshot_init(lk_room_snapshot_t* snapshot) {
	if (snapshot != nullptr) {
		*snapshot = {};
		snapshot->struct_size = sizeof(*snapshot);
	}
}

void lk_e2ee_options_init(lk_e2ee_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->enabled = 1;
		options->ratchet_window_size = core::kDefaultE2eeRatchetWindowSize;
		options->failure_tolerance = core::kDefaultE2eeFailureTolerance;
		options->key_ring_size = core::kDefaultE2eeKeyRingSize;
		options->key_derivation = LK_E2EE_KEY_DERIVATION_PBKDF2_SHA256;
	}
}

void lk_frame_cryptor_info_init(lk_frame_cryptor_info_t* info) {
	if (info != nullptr) {
		*info = {};
		info->struct_size = sizeof(*info);
	}
}

void lk_audio_playback_stats_init(lk_audio_playback_stats_t* stats) {
	if (stats != nullptr) {
		*stats = {};
		stats->struct_size = sizeof(*stats);
	}
}

void lk_audio_source_options_init(lk_audio_source_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->sample_rate = 48000;
		options->num_channels = 1;
		options->queue_size_ms = 200;
	}
}

void lk_microphone_capture_options_init(lk_microphone_capture_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->queue_size_ms = 200;
		options->echo_cancellation = 1;
		options->auto_gain_control = 1;
		options->noise_suppression = 1;
	}
}

void lk_system_audio_capture_options_init(lk_system_audio_capture_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->queue_size_ms = 200;
	}
}

void lk_microphone_processing_stats_init(lk_microphone_processing_stats_t* stats) {
	if (stats != nullptr) {
		*stats = {};
		stats->struct_size = sizeof(*stats);
	}
}

void lk_video_source_options_init(lk_video_source_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
	}
}

void lk_camera_capture_options_init(lk_camera_capture_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->width = 1280;
		options->height = 720;
		options->frames_per_second = 30;
	}
}

void lk_screen_capture_options_init(lk_screen_capture_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->frames_per_second = 15;
		options->include_cursor = 1;
	}
}

void lk_video_encoding_init(lk_video_encoding_t* encoding) {
	if (encoding != nullptr) {
		*encoding = {};
		encoding->struct_size = sizeof(*encoding);
	}
}

void lk_track_publish_options_init(lk_track_publish_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->dtx = 1;
		options->red = 1;
		options->simulcast = 1;
		options->video_codec = LK_VIDEO_CODEC_VP8;
		options->scalability_mode = "L3T3_KEY";
		options->backup_video_codec = LK_VIDEO_CODEC_VP8;
		options->backup_codec_policy = LK_BACKUP_CODEC_POLICY_PREFER_REGRESSION;
		lk_video_encoding_init(&options->video_encoding);
		lk_video_encoding_init(&options->backup_video_encoding);
	}
}

void lk_data_publish_options_init(lk_data_publish_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->reliable = 1;
	}
}

void lk_data_track_schema_id_init(lk_data_track_schema_id_t* schema_id) {
	if (schema_id != nullptr) {
		*schema_id = {};
		schema_id->struct_size = sizeof(*schema_id);
	}
}

void lk_data_track_publish_options_init(lk_data_track_publish_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		lk_data_track_schema_id_init(&options->schema);
	}
}

void lk_data_track_subscription_options_init(lk_data_track_subscription_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->buffer_capacity = 16;
		options->max_partial_frames = 1;
	}
}

void lk_data_track_snapshot_info_init(lk_data_track_snapshot_info_t* info) {
	if (info != nullptr) {
		*info = {};
		info->struct_size = sizeof(*info);
	}
}

void lk_file_send_options_init(lk_file_send_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->topic = "files";
		options->mime_type = "application/octet-stream";
		options->chunk_size = 15000;
	}
}

void lk_text_send_options_init(lk_text_send_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->chunk_size = 15000;
	}
}

void lk_byte_send_options_init(lk_byte_send_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->mime_type = "application/octet-stream";
		options->chunk_size = 15000;
	}
}

void lk_stream_text_options_init(lk_stream_text_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->chunk_size = 15000;
	}
}

void lk_stream_bytes_options_init(lk_stream_bytes_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->mime_type = "application/octet-stream";
		options->name = "unknown";
		options->chunk_size = 15000;
	}
}

void lk_rpc_perform_options_init(lk_rpc_perform_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->response_timeout_ms = 15000;
	}
}

void lk_participant_track_permission_init(lk_participant_track_permission_t* permission) {
	if (permission != nullptr) {
		*permission = {};
		permission->struct_size = sizeof(*permission);
	}
}

void lk_remote_track_settings_init(lk_remote_track_settings_t* settings) {
	if (settings != nullptr) {
		*settings = {};
		settings->struct_size = sizeof(*settings);
		settings->enabled = 1;
	}
}

void lk_remote_participant_snapshot_info_init(lk_remote_participant_snapshot_info_t* info) {
	if (info != nullptr) {
		*info = {};
		info->struct_size = sizeof(*info);
	}
}

void lk_remote_track_publication_snapshot_info_init(
    lk_remote_track_publication_snapshot_info_t* info) {
	if (info != nullptr) {
		*info = {};
		info->struct_size = sizeof(*info);
	}
}

void lk_remote_track_snapshot_info_init(lk_remote_track_snapshot_info_t* info) {
	if (info != nullptr) {
		*info = {};
		info->struct_size = sizeof(*info);
	}
}

lk_status_t lk_room_create(lk_room_t** room) {
	return Guard([&] {
		if (room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room output is null");
		}
		*room = nullptr;
		auto result = std::make_unique<lk_room_t>();
		result->room = core::CreateRoomUnique();
		if (!result->room) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create room");
		}
		result->events = std::make_unique<CRoomEvents>(result.get());
		result->room->AddEventListener(result->events.get());
		*room = result.release();
		return LK_STATUS_OK;
	});
}

void lk_room_destroy(lk_room_t* room) {
	if (room == nullptr) {
		return;
	}
	try {
		room->state->alive.store(false);
		room->room->RemoveEventListener();
		if (room->room->IsConnected()) {
			room->room->Disconnect();
		}
		room->state->connected.store(false);
		std::vector<std::shared_ptr<AsyncRpcTask>> async_rpc_tasks;
		{
			std::lock_guard<std::mutex> guard(room->async_tasks_mutex);
			async_rpc_tasks.swap(room->async_rpc_tasks);
		}
		async_rpc_tasks.clear();
		{
			std::lock_guard<std::mutex> guard(room->local_tracks_mutex);
			for (auto* track : room->local_tracks) {
				if (track != nullptr) {
					track->owner = nullptr;
				}
			}
			room->local_tracks.clear();
		}
		{
			std::lock_guard<std::mutex> guard(room->local_data_tracks_mutex);
			for (auto* track : room->local_data_tracks) {
				if (track != nullptr) {
					track->owner = nullptr;
				}
			}
			room->local_data_tracks.clear();
		}
		{ std::lock_guard<std::mutex> guard(room->callback_lifetime_mutex); }
		delete room;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "exception while destroying room");
	}
}

lk_status_t lk_room_set_callbacks(lk_room_t* room, const lk_room_callbacks_t* callbacks) {
	return Guard([&] {
		if (room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room is null");
		}
		lk_room_callbacks_t copy{};
		if (callbacks != nullptr) {
			if (callbacks->struct_size < sizeof(callbacks->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid callbacks struct size");
			}
			std::memcpy(&copy, callbacks, std::min(callbacks->struct_size, sizeof(copy)));
			copy.struct_size = sizeof(copy);
		}
		std::lock_guard<std::mutex> guard(room->callbacks_mutex);
		room->callbacks = copy;
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_connect(lk_room_t* room, const char* url, const char* token) {
	return Guard([&] {
		if (room == nullptr || url == nullptr || token == nullptr || *url == '\0' ||
		    *token == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, URL, and token are required");
		}
		if (!room->room->Connect(url, token)) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to connect room");
		}
		room->state->connected.store(true);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_connect_with_token_source(lk_room_t* room, lk_token_source_callback callback,
                                              void* user_data,
                                              const lk_token_source_fetch_options_t* options,
                                              const lk_e2ee_options_t* e2ee_options) {
	return Guard([&] {
		if (room == nullptr || callback == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room and token source callback are required");
		}
		core::TokenSourceFetchOptions source_options;
		core::RoomConnectOptions connect_options;
		if (e2ee_options != nullptr) {
			core::E2eeOptions e2ee;
			const auto status = ToCoreE2eeOptions(e2ee_options, e2ee);
			if (status != LK_STATUS_OK) {
				return status;
			}
			connect_options.e2ee = std::move(e2ee);
		}
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid token source options size");
			}
			auto copy = [](const char* value) { return value == nullptr ? std::string{} : value; };
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t, room_name)) {
				source_options.room_name = copy(options->room_name);
			}
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t, participant_name)) {
				source_options.participant_name = copy(options->participant_name);
			}
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t, participant_identity)) {
				source_options.participant_identity = copy(options->participant_identity);
			}
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t, participant_metadata)) {
				source_options.participant_metadata = copy(options->participant_metadata);
			}
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t,
			                  participant_attribute_count)) {
				if (options->participant_attribute_count != 0 &&
				    (!LKC_HAS_FIELD(options, lk_token_source_fetch_options_t,
				                    participant_attributes) ||
				     options->participant_attributes == nullptr)) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "token source attributes are null");
				}
				for (size_t index = 0; index < options->participant_attribute_count; ++index) {
					const auto& attribute = options->participant_attributes[index];
					if (attribute.key == nullptr) {
						return Failure(LK_STATUS_INVALID_ARGUMENT,
						               "token source attribute key is null");
					}
					source_options.participant_attributes[attribute.key] = copy(attribute.value);
				}
			}
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t, agent_name)) {
				source_options.agent_name = copy(options->agent_name);
			}
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t, agent_metadata)) {
				source_options.agent_metadata = copy(options->agent_metadata);
			}
			if (LKC_HAS_FIELD(options, lk_token_source_fetch_options_t, deployment)) {
				source_options.deployment = copy(options->deployment);
			}
		}
		auto source = core::CreateCallbackTokenSource(
		    [callback, user_data](const core::TokenSourceFetchOptions& requested, bool force) {
			    const auto attributes = AttributeViews(requested.participant_attributes);
			    const lk_token_source_fetch_options_t request{
			        sizeof(lk_token_source_fetch_options_t),
			        requested.room_name.c_str(),
			        requested.participant_name.c_str(),
			        requested.participant_identity.c_str(),
			        requested.participant_metadata.c_str(),
			        attributes.data(),
			        attributes.size(),
			        requested.agent_name.c_str(),
			        requested.agent_metadata.c_str(),
			        requested.deployment.c_str()};
			    lk_token_source_response_t response{};
			    lk_token_source_response_init(&response);
			    const auto status = callback(user_data, &request, force ? 1 : 0, &response);
			    if (status != LK_STATUS_OK) {
				    return core::TokenSourceResult{
				        {}, "C token source callback failed with status " + std::to_string(status)};
			    }
			    return core::TokenSourceResult{
			        {response.server_url == nullptr ? "" : response.server_url,
			         response.participant_token == nullptr ? "" : response.participant_token},
			        {}};
		    });
		if (!room->room->Connect(std::move(source), std::move(source_options),
		                         std::move(connect_options))) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to connect room with token source");
		}
		room->state->connected.store(true);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_connect_e2ee(lk_room_t* room, const char* url, const char* token,
                                 const lk_e2ee_options_t* options) {
	return Guard([&] {
		if (room == nullptr || url == nullptr || token == nullptr || *url == '\0' ||
		    *token == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, URL, and token are required");
		}
		core::E2eeOptions e2ee;
		const auto status = ToCoreE2eeOptions(options, e2ee);
		if (status != LK_STATUS_OK) {
			return status;
		}
		core::RoomConnectOptions connect_options;
		connect_options.e2ee = std::move(e2ee);
		if (!room->room->Connect(url, token, std::move(connect_options))) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to connect E2EE room");
		}
		room->state->connected.store(true);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_disconnect(lk_room_t* room) {
	return Guard([&] {
		if (room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room is null");
		}
		if (!room->room->Disconnect()) {
			return Failure(LK_STATUS_INVALID_STATE, "room is already disconnected");
		}
		room->state->connected.store(false);
		return LK_STATUS_OK;
	});
}

lk_room_state_t lk_room_state(const lk_room_t* room) {
	if (room == nullptr || room->room == nullptr) {
		return LK_ROOM_STATE_DISCONNECTED;
	}
	return ToCRoomState(room->room->State());
}

lk_disconnect_reason_t lk_room_disconnect_reason(const lk_room_t* room) {
	if (room == nullptr || room->room == nullptr) {
		return LK_DISCONNECT_REASON_UNKNOWN;
	}
	return ToCDisconnectReason(room->room->LastDisconnectReason());
}

int lk_room_is_connected(const lk_room_t* room) {
	return room != nullptr && room->room != nullptr && room->room->IsConnected() ? 1 : 0;
}

size_t lk_room_sid(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return room != nullptr && room->room != nullptr
		           ? CopyString(room->room->Sid(), buffer, buffer_size)
		           : InvalidSizeResult("room is required");
	});
}

size_t lk_room_name(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return room != nullptr && room->room != nullptr
		           ? CopyString(room->room->Name(), buffer, buffer_size)
		           : InvalidSizeResult("room is required");
	});
}

size_t lk_room_metadata(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return room != nullptr && room->room != nullptr
		           ? CopyString(room->room->Metadata(), buffer, buffer_size)
		           : InvalidSizeResult("room is required");
	});
}

int lk_room_is_recording(const lk_room_t* room) {
	return room != nullptr && room->room->IsRecording() ? 1 : 0;
}

lk_status_t lk_room_set_audio_output_device(lk_room_t* room, const char* device_id) {
	return Guard([&] {
		if (room == nullptr || room->room == nullptr || device_id == nullptr ||
		    *device_id == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room and audio output device ID are required");
		}
		return room->room->SetAudioOutputDevice(device_id)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to select audio output device");
	});
}

size_t lk_room_audio_output_device(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return room != nullptr && room->room != nullptr
		           ? CopyString(room->room->AudioOutputDevice(), buffer, buffer_size)
		           : InvalidSizeResult("room is required");
	});
}

lk_status_t lk_room_set_speaker_volume(lk_room_t* room, float volume) {
	return Guard([&] {
		if (room == nullptr || room->room == nullptr || !std::isfinite(volume) || volume < 0.0F ||
		    volume > 1.0F) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room and a volume between 0 and 1 are required");
		}
		return room->room->SetSpeakerVolume(volume)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to set speaker volume");
	});
}

float lk_room_speaker_volume(const lk_room_t* room) {
	return room != nullptr && room->room != nullptr ? room->room->SpeakerVolume() : 0.0F;
}

lk_status_t lk_room_set_speaker_muted(lk_room_t* room, int muted) {
	return Guard([&] {
		if (room == nullptr || room->room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room is required");
		}
		return room->room->SetSpeakerMuted(muted != 0)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to set speaker mute state");
	});
}

int lk_room_speaker_is_muted(const lk_room_t* room) {
	return room != nullptr && room->room != nullptr && room->room->SpeakerMuted() ? 1 : 0;
}

lk_status_t lk_room_audio_playback_stats(const lk_room_t* room, lk_audio_playback_stats_t* stats) {
	return Guard([&] {
		if (room == nullptr || room->room == nullptr || stats == nullptr ||
		    stats->struct_size < sizeof(stats->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and initialized stats are required");
		}
		const auto source = room->room->GetAudioPlaybackStats();
		lk_audio_playback_stats_t copy{};
		copy.struct_size = sizeof(copy);
		copy.queued_frames = source.queued_frames;
		copy.played_frames = source.played_frames;
		copy.dropped_frames = source.dropped_frames;
		copy.underrun_frames = source.underrun_frames;
		copy.buffered_duration_ms = source.buffered_duration_ms;
		copy.device_latency_ms = source.device_latency_ms;
		copy.estimated_delay_ms = source.estimated_delay_ms;
		std::memcpy(stats, &copy, std::min(stats->struct_size, sizeof(copy)));
		return LK_STATUS_OK;
	});
}

int lk_room_e2ee_is_configured(const lk_room_t* room) {
	return E2eeManager(room) != nullptr ? 1 : 0;
}

int lk_room_e2ee_is_enabled(const lk_room_t* room) {
	auto* manager = E2eeManager(room);
	return manager != nullptr && manager->Enabled() ? 1 : 0;
}

lk_status_t lk_room_e2ee_set_enabled(lk_room_t* room, int enabled) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		return manager->SetEnabled(enabled != 0)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update E2EE state");
	});
}

lk_status_t lk_room_e2ee_set_shared_key(lk_room_t* room, const uint8_t* key, size_t key_size,
                                        size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (key == nullptr || key_size == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "E2EE shared key must not be empty");
		}
		return KeyOperationStatus(
		    manager->Keys().SetSharedKey(core::E2eeKey(key, key + key_size), key_index));
	});
}

size_t lk_room_e2ee_export_shared_key(const lk_room_t* room, size_t key_index, uint8_t* buffer,
                                      size_t buffer_size) {
	return SizeGuard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return InvalidSizeResult("E2EE is not configured for this room");
		}
		const auto result = manager->Keys().ExportSharedKey(key_index);
		if (!result.Ok()) {
			KeyOperationStatus(result);
			return size_t{0};
		}
		return CopyBytes(result.key, buffer, buffer_size);
	});
}

lk_status_t lk_room_e2ee_ratchet_shared_key(lk_room_t* room, size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		return manager != nullptr
		           ? KeyOperationStatus(manager->Keys().RatchetSharedKey(key_index))
		           : Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
	});
}

lk_status_t lk_room_e2ee_remove_shared_key(lk_room_t* room, size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		return manager != nullptr
		           ? KeyOperationStatus(manager->Keys().RemoveSharedKey(key_index))
		           : Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
	});
}

lk_status_t lk_room_e2ee_set_participant_key(lk_room_t* room, const char* participant_identity,
                                             const uint8_t* key, size_t key_size,
                                             size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (participant_identity == nullptr || *participant_identity == '\0' || key == nullptr ||
		    key_size == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "participant identity and E2EE key are required");
		}
		return KeyOperationStatus(manager->Keys().SetKey(
		    participant_identity, core::E2eeKey(key, key + key_size), key_index));
	});
}

size_t lk_room_e2ee_export_participant_key(const lk_room_t* room, const char* participant_identity,
                                           size_t key_index, uint8_t* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return InvalidSizeResult("E2EE is not configured for this room");
		}
		if (participant_identity == nullptr || *participant_identity == '\0') {
			return InvalidSizeResult("participant identity is required");
		}
		const auto result = manager->Keys().ExportKey(participant_identity, key_index);
		if (!result.Ok()) {
			KeyOperationStatus(result);
			return size_t{0};
		}
		return CopyBytes(result.key, buffer, buffer_size);
	});
}

lk_status_t lk_room_e2ee_ratchet_participant_key(lk_room_t* room, const char* participant_identity,
                                                 size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (participant_identity == nullptr || *participant_identity == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "participant identity is required");
		}
		return KeyOperationStatus(manager->Keys().RatchetKey(participant_identity, key_index));
	});
}

lk_status_t lk_room_e2ee_remove_participant_key(lk_room_t* room, const char* participant_identity,
                                                size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (participant_identity == nullptr || *participant_identity == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "participant identity is required");
		}
		return KeyOperationStatus(manager->Keys().RemoveKey(participant_identity, key_index));
	});
}

lk_status_t lk_room_e2ee_remove_participant_keys(lk_room_t* room,
                                                 const char* participant_identity) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (participant_identity == nullptr || *participant_identity == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "participant identity is required");
		}
		return KeyOperationStatus(manager->Keys().RemoveParticipantKeys(participant_identity));
	});
}

lk_status_t lk_room_e2ee_clear_keys(lk_room_t* room) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		manager->Keys().Clear();
		return LK_STATUS_OK;
	});
}

size_t lk_room_e2ee_data_key_index(const lk_room_t* room) {
	auto* manager = E2eeManager(room);
	return manager != nullptr ? manager->DataKeyIndex() : 0;
}

lk_status_t lk_room_e2ee_set_data_key_index(lk_room_t* room, size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		return manager->SetDataKeyIndex(key_index)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_INVALID_ARGUMENT, "invalid E2EE data key index");
	});
}

lk_status_t lk_room_e2ee_set_frame_cryptor_enabled(lk_room_t* room, const char* track_id,
                                                   lk_frame_cryptor_direction_t direction,
                                                   int enabled) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		core::FrameCryptorDirection core_direction;
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (track_id == nullptr || *track_id == '\0' ||
		    !ToCoreFrameCryptorDirection(direction, core_direction)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "track ID and direction are required");
		}
		return manager->SetFrameCryptorEnabled(track_id, core_direction, enabled != 0)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update frame cryptor");
	});
}

lk_status_t lk_room_e2ee_set_frame_cryptor_key_index(lk_room_t* room, const char* track_id,
                                                     lk_frame_cryptor_direction_t direction,
                                                     size_t key_index) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		core::FrameCryptorDirection core_direction;
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (track_id == nullptr || *track_id == '\0' ||
		    !ToCoreFrameCryptorDirection(direction, core_direction)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "track ID and direction are required");
		}
		return manager->SetFrameCryptorKeyIndex(track_id, core_direction, key_index)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED,
		                     "failed to update frame cryptor key index");
	});
}

lk_status_t lk_room_e2ee_set_participant_enabled(lk_room_t* room, const char* participant_identity,
                                                 int enabled, size_t* updated_count) {
	return Guard([&] {
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		if (participant_identity == nullptr || *participant_identity == '\0' ||
		    updated_count == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "participant identity and updated count are required");
		}
		*updated_count = manager->SetParticipantEnabled(participant_identity, enabled != 0);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_frame_cryptor_list_create(const lk_room_t* room,
                                         lk_frame_cryptor_list_t** cryptors) {
	return Guard([&] {
		if (cryptors == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "frame cryptor output is required");
		}
		*cryptors = nullptr;
		auto* manager = E2eeManager(room);
		if (manager == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "E2EE is not configured for this room");
		}
		auto result = std::make_unique<lk_frame_cryptor_list_t>();
		result->cryptors = manager->FrameCryptors();
		*cryptors = result.release();
		return LK_STATUS_OK;
	});
}

void lk_frame_cryptor_list_destroy(lk_frame_cryptor_list_t* cryptors) { delete cryptors; }

size_t lk_frame_cryptor_list_count(const lk_frame_cryptor_list_t* cryptors) {
	return cryptors != nullptr ? cryptors->cryptors.size() : 0;
}

lk_status_t lk_frame_cryptor_list_info(const lk_frame_cryptor_list_t* cryptors, size_t index,
                                       lk_frame_cryptor_info_t* info) {
	return Guard([&] {
		if (cryptors == nullptr || index >= cryptors->cryptors.size()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "frame cryptor index is out of range");
		}
		const auto& source = cryptors->cryptors[index];
		const lk_frame_cryptor_info_t converted{sizeof(converted),
		                                        ToCTrackKind(source.kind),
		                                        ToCFrameCryptorDirection(source.direction),
		                                        source.enabled ? 1 : 0,
		                                        source.key_index,
		                                        ToCFrameCryptorState(source.state)};
		return CopyOutputStruct(converted, info, "initialized frame cryptor info is required");
	});
}

size_t lk_frame_cryptor_list_track_id(const lk_frame_cryptor_list_t* cryptors, size_t index,
                                      char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return cryptors != nullptr && index < cryptors->cryptors.size()
		           ? CopyString(cryptors->cryptors[index].track_id, buffer, buffer_size)
		           : InvalidSizeResult("frame cryptor index is out of range");
	});
}

size_t lk_frame_cryptor_list_participant_identity(const lk_frame_cryptor_list_t* cryptors,
                                                  size_t index, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return cryptors != nullptr && index < cryptors->cryptors.size()
		           ? CopyString(cryptors->cryptors[index].participant_identity, buffer, buffer_size)
		           : InvalidSizeResult("frame cryptor index is out of range");
	});
}

lk_status_t lk_room_create_remote_participant_snapshot(const lk_room_t* room,
                                                       lk_remote_participant_list_t** snapshot) {
	return Guard([&] {
		if (room == nullptr || room->room == nullptr || snapshot == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and snapshot output are required");
		}
		*snapshot = nullptr;
		auto result = std::make_unique<lk_remote_participant_list_t>();
		auto participants = room->room->GetRemoteParticipantSnapshots();
		result->participants.reserve(participants.size());
		for (auto& participant : participants) {
			result->participants.emplace_back(std::move(participant));
		}
		*snapshot = result.release();
		return LK_STATUS_OK;
	});
}

void lk_remote_participant_list_destroy(lk_remote_participant_list_t* snapshot) { delete snapshot; }

size_t lk_remote_participant_list_count(const lk_remote_participant_list_t* snapshot) {
	return snapshot != nullptr ? snapshot->participants.size() : 0;
}

lk_status_t lk_remote_participant_list_at(const lk_remote_participant_list_t* snapshot,
                                          size_t index,
                                          const lk_remote_participant_snapshot_t** participant) {
	return Guard([&] {
		if (participant == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "participant output is null");
		}
		*participant = nullptr;
		if (snapshot == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "participant snapshot is null");
		}
		if (index >= snapshot->participants.size()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "participant snapshot index is out of range");
		}
		*participant = &snapshot->participants[index];
		return LK_STATUS_OK;
	});
}

lk_status_t lk_remote_participant_snapshot_info(const lk_remote_participant_snapshot_t* participant,
                                                lk_remote_participant_snapshot_info_t* info) {
	return Guard([&] {
		if (participant == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "participant snapshot is null");
		}
		const lk_remote_participant_snapshot_info_t value{
		    sizeof(value), participant->value.audio_level,
		    ToCConnectionQuality(participant->value.connection_quality),
		    participant->value.speaking ? 1 : 0};
		return CopyOutputStruct(value, info, "invalid participant snapshot info output");
	});
}

lk_status_t
lk_remote_participant_snapshot_permissions(const lk_remote_participant_snapshot_t* participant,
                                           lk_participant_permissions_t* permissions) {
	return Guard([&] {
		if (participant == nullptr || permissions == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "participant snapshot and permissions output are required");
		}
		const auto& source = participant->value.permissions;
		*permissions = {source.can_subscribe ? 1 : 0,
		                source.can_publish ? 1 : 0,
		                source.can_publish_data ? 1 : 0,
		                participant->publish_sources.data(),
		                participant->publish_sources.size(),
		                source.hidden ? 1 : 0,
		                source.recorder ? 1 : 0,
		                source.can_update_metadata ? 1 : 0,
		                source.agent ? 1 : 0,
		                source.can_subscribe_metrics ? 1 : 0,
		                source.can_manage_agent_session ? 1 : 0};
		return LK_STATUS_OK;
	});
}

size_t lk_remote_participant_snapshot_sid(const lk_remote_participant_snapshot_t* participant,
                                          char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return participant != nullptr ? CopyString(participant->value.sid, buffer, buffer_size)
		                              : InvalidSizeResult("participant snapshot is null");
	});
}

size_t lk_remote_participant_snapshot_identity(const lk_remote_participant_snapshot_t* participant,
                                               char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return participant != nullptr ? CopyString(participant->value.identity, buffer, buffer_size)
		                              : InvalidSizeResult("participant snapshot is null");
	});
}

size_t lk_remote_participant_snapshot_name(const lk_remote_participant_snapshot_t* participant,
                                           char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return participant != nullptr ? CopyString(participant->value.name, buffer, buffer_size)
		                              : InvalidSizeResult("participant snapshot is null");
	});
}

size_t lk_remote_participant_snapshot_metadata(const lk_remote_participant_snapshot_t* participant,
                                               char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return participant != nullptr ? CopyString(participant->value.metadata, buffer, buffer_size)
		                              : InvalidSizeResult("participant snapshot is null");
	});
}

size_t lk_remote_participant_snapshot_attribute_count(
    const lk_remote_participant_snapshot_t* participant) {
	return participant != nullptr ? participant->value.attributes.size() : 0;
}

size_t
lk_remote_participant_snapshot_attribute_key(const lk_remote_participant_snapshot_t* participant,
                                             size_t index, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		if (participant == nullptr || index >= participant->value.attributes.size()) {
			return InvalidSizeResult("participant attribute index is out of range");
		}
		auto attribute = participant->value.attributes.begin();
		std::advance(attribute, static_cast<std::ptrdiff_t>(index));
		return CopyString(attribute->first, buffer, buffer_size);
	});
}

size_t
lk_remote_participant_snapshot_attribute_value(const lk_remote_participant_snapshot_t* participant,
                                               size_t index, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		if (participant == nullptr || index >= participant->value.attributes.size()) {
			return InvalidSizeResult("participant attribute index is out of range");
		}
		auto attribute = participant->value.attributes.begin();
		std::advance(attribute, static_cast<std::ptrdiff_t>(index));
		return CopyString(attribute->second, buffer, buffer_size);
	});
}

size_t lk_remote_participant_snapshot_publication_count(
    const lk_remote_participant_snapshot_t* participant) {
	return participant != nullptr ? participant->publications.size() : 0;
}

lk_status_t lk_remote_participant_snapshot_publication_at(
    const lk_remote_participant_snapshot_t* participant, size_t index,
    const lk_remote_track_publication_snapshot_t** publication) {
	return Guard([&] {
		if (publication == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "publication output is null");
		}
		*publication = nullptr;
		if (participant == nullptr || index >= participant->publications.size()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "publication snapshot index is out of range");
		}
		*publication = &participant->publications[index];
		return LK_STATUS_OK;
	});
}

lk_status_t
lk_remote_track_publication_snapshot_info(const lk_remote_track_publication_snapshot_t* publication,
                                          lk_remote_track_publication_snapshot_info_t* info) {
	return Guard([&] {
		if (publication == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "publication snapshot is null");
		}
		const auto& source = publication->value;
		const lk_remote_track_publication_snapshot_info_t value{
		    sizeof(value),
		    ToCTrackKind(source.kind),
		    ToCTrackSource(source.source),
		    source.dimensions.width,
		    source.dimensions.height,
		    source.muted ? 1 : 0,
		    source.simulcasted ? 1 : 0,
		    source.subscription_allowed ? 1 : 0,
		    ToCTrackSubscriptionStatus(source.subscription_status),
		    source.subscription_error.has_value() ? 1 : 0,
		    ToCSubscriptionError(
		        source.subscription_error.value_or(core::SubscriptionError::Unknown)),
		    publication->track != nullptr ? 1 : 0};
		return CopyOutputStruct(value, info, "invalid publication snapshot info output");
	});
}

size_t
lk_remote_track_publication_snapshot_sid(const lk_remote_track_publication_snapshot_t* publication,
                                         char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return publication != nullptr ? CopyString(publication->value.sid, buffer, buffer_size)
		                              : InvalidSizeResult("publication snapshot is null");
	});
}

size_t
lk_remote_track_publication_snapshot_name(const lk_remote_track_publication_snapshot_t* publication,
                                          char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return publication != nullptr ? CopyString(publication->value.name, buffer, buffer_size)
		                              : InvalidSizeResult("publication snapshot is null");
	});
}

size_t lk_remote_track_publication_snapshot_mime_type(
    const lk_remote_track_publication_snapshot_t* publication, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return publication != nullptr
		           ? CopyString(publication->value.mime_type, buffer, buffer_size)
		           : InvalidSizeResult("publication snapshot is null");
	});
}

lk_status_t lk_remote_track_publication_snapshot_track(
    const lk_remote_track_publication_snapshot_t* publication,
    const lk_remote_track_snapshot_t** track) {
	return Guard([&] {
		if (publication == nullptr || track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "publication snapshot and track output are required");
		}
		*track = publication->track.get();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_remote_track_snapshot_info(const lk_remote_track_snapshot_t* track,
                                          lk_remote_track_snapshot_info_t* info) {
	return Guard([&] {
		if (track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "remote track snapshot is null");
		}
		const auto& source = track->value;
		const lk_remote_track_snapshot_info_t value{sizeof(value),
		                                            ToCTrackKind(source.kind),
		                                            ToCTrackSource(source.source),
		                                            ToCTrackStreamState(source.stream_state),
		                                            source.dimensions.width,
		                                            source.dimensions.height,
		                                            source.enabled ? 1 : 0};
		return CopyOutputStruct(value, info, "invalid remote track snapshot info output");
	});
}

size_t lk_remote_track_snapshot_sid(const lk_remote_track_snapshot_t* track, char* buffer,
                                    size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr ? CopyString(track->value.sid, buffer, buffer_size)
		                        : InvalidSizeResult("remote track snapshot is null");
	});
}

size_t lk_remote_track_snapshot_name(const lk_remote_track_snapshot_t* track, char* buffer,
                                     size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr ? CopyString(track->value.name, buffer, buffer_size)
		                        : InvalidSizeResult("remote track snapshot is null");
	});
}

size_t lk_local_participant_sid(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Sid(), buffer, buffer_size)
		                              : InvalidSizeResult("connected room is required");
	});
}

size_t lk_local_participant_identity(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Identity(), buffer, buffer_size)
		                              : InvalidSizeResult("connected room is required");
	});
}

size_t lk_local_participant_name(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Name(), buffer, buffer_size)
		                              : InvalidSizeResult("connected room is required");
	});
}

size_t lk_local_participant_metadata(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Metadata(), buffer, buffer_size)
		                              : InvalidSizeResult("connected room is required");
	});
}

lk_status_t lk_local_participant_set_metadata(lk_room_t* room, const char* metadata) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || metadata == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and metadata are required");
		}
		return participant->SetMetadata(metadata)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update metadata");
	});
}

lk_status_t lk_local_participant_set_name(lk_room_t* room, const char* name) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || name == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and name are required");
		}
		return participant->SetName(name)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update name");
	});
}

lk_status_t lk_local_participant_set_attributes(lk_room_t* room, const lk_attribute_t* attributes,
                                                size_t attribute_count) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (attributes == nullptr && attribute_count != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or attributes");
		}
		std::map<std::string, std::string> values;
		for (size_t index = 0; index < attribute_count; ++index) {
			if (attributes[index].key == nullptr || attributes[index].value == nullptr) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "attribute key or value is null");
			}
			values[attributes[index].key] = attributes[index].value;
		}
		return participant->SetAttributes(values)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update attributes");
	});
}

lk_status_t lk_audio_source_create(const lk_audio_source_options_t* options,
                                   lk_audio_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		lk_audio_source_options_t values;
		lk_audio_source_options_init(&values);
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid audio options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, sample_rate)) {
				values.sample_rate = options->sample_rate;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, num_channels)) {
				values.num_channels = options->num_channels;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, queue_size_ms)) {
				values.queue_size_ms = options->queue_size_ms;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, echo_cancellation)) {
				values.echo_cancellation = options->echo_cancellation;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, auto_gain_control)) {
				values.auto_gain_control = options->auto_gain_control;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, noise_suppression)) {
				values.noise_suppression = options->noise_suppression;
			}
		}
		if (values.sample_rate == 0 || values.num_channels == 0 || values.queue_size_ms == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid audio source dimensions");
		}
		core::AudioSourceOptions core_options;
		core_options.echo_cancellation = values.echo_cancellation != 0;
		core_options.auto_gain_control = values.auto_gain_control != 0;
		core_options.noise_suppression = values.noise_suppression != 0;
		auto result = std::make_unique<lk_audio_source_t>();
		result->source.reset(core::CreateAudioSource(core_options, values.sample_rate,
		                                             values.num_channels, values.queue_size_ms));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create audio source");
		}
		result->sample_rate = values.sample_rate;
		result->num_channels = values.num_channels;
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_audio_source_create_microphone(const lk_microphone_capture_options_t* options,
                                              lk_audio_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		lk_microphone_capture_options_t values;
		lk_microphone_capture_options_init(&values);
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT,
				               "invalid microphone options struct size");
			}
			std::memcpy(&values, options, std::min(options->struct_size, sizeof(values)));
		}
		if (values.queue_size_ms == 0 || values.queue_size_ms % 10 != 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "microphone queue must be a multiple of 10 ms");
		}
		core::MicrophoneCaptureOptions core_options;
		if (values.device_id != nullptr) {
			core_options.device_id = values.device_id;
		}
		core_options.queue_size_ms = values.queue_size_ms;
		core_options.processing.echo_cancellation = values.echo_cancellation != 0;
		core_options.processing.auto_gain_control = values.auto_gain_control != 0;
		core_options.processing.noise_suppression = values.noise_suppression != 0;
		auto result = std::make_unique<lk_audio_source_t>();
		result->source.reset(core::CreateMicrophoneAudioSource(std::move(core_options)));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to open microphone device");
		}
		result->sample_rate = 48000;
		result->num_channels = 1;
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_audio_source_create_system_audio(const lk_system_audio_capture_options_t* options,
                                                lk_audio_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		lk_system_audio_capture_options_t values;
		lk_system_audio_capture_options_init(&values);
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT,
				               "invalid system audio options struct size");
			}
			std::memcpy(&values, options, std::min(options->struct_size, sizeof(values)));
		}
		if (values.queue_size_ms == 0 || values.queue_size_ms % 10 != 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "system audio queue must be a multiple of 10 ms");
		}
		core::SystemAudioCaptureOptions core_options;
		if (values.device_id != nullptr) {
			core_options.device_id = values.device_id;
		}
		core_options.queue_size_ms = values.queue_size_ms;
		auto result = std::make_unique<lk_audio_source_t>();
		result->source.reset(core::CreateSystemAudioSource(std::move(core_options)));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to open system audio output device");
		}
		result->sample_rate = 48000;
		result->num_channels = 2;
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_audio_source_destroy(lk_audio_source_t* source) {
	if (source == nullptr) {
		return LK_STATUS_OK;
	}
	if (source->track_references.load() != 0) {
		return Failure(LK_STATUS_INVALID_STATE, "audio source is still used by a track");
	}
	delete source;
	return LK_STATUS_OK;
}

lk_status_t lk_audio_source_capture_frame(lk_audio_source_t* source, const int16_t* data,
                                          uint32_t samples_per_channel) {
	return Guard([&] {
		if (source == nullptr || data == nullptr || samples_per_channel == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid audio frame");
		}
		return source->source->CaptureFrame(const_cast<int16_t*>(data), source->sample_rate,
		                                    source->num_channels, samples_per_channel)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to capture audio frame");
	});
}

lk_status_t lk_audio_source_microphone_start(lk_audio_source_t* source) {
	return Guard([&] {
		auto* microphone =
		    source != nullptr
		        ? dynamic_cast<core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "audio source is not a microphone source");
		}
		return microphone->Start()
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to start microphone capture");
	});
}

lk_status_t lk_audio_source_microphone_stop(lk_audio_source_t* source) {
	return Guard([&] {
		auto* microphone =
		    source != nullptr
		        ? dynamic_cast<core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "audio source is not a microphone source");
		}
		microphone->Stop();
		return LK_STATUS_OK;
	});
}

int lk_audio_source_microphone_is_capturing(const lk_audio_source_t* source) {
	try {
		ClearError();
		const auto* microphone =
		    source != nullptr
		        ? dynamic_cast<const core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			SetStatusError(LK_STATUS_INVALID_ARGUMENT, "audio source is not a microphone source");
			return 0;
		}
		return microphone != nullptr && microphone->IsCapturing() ? 1 : 0;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "failed to query microphone capture state");
		return 0;
	}
}

size_t lk_audio_source_microphone_device_id(const lk_audio_source_t* source, char* buffer,
                                            size_t buffer_size) {
	return SizeGuard([&] {
		const auto* microphone =
		    source != nullptr
		        ? dynamic_cast<const core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			return InvalidSizeResult("audio source is not a microphone source");
		}
		return CopyString(microphone->DeviceId(), buffer, buffer_size);
	});
}

lk_status_t lk_audio_source_microphone_switch_device(lk_audio_source_t* source,
                                                     const char* device_id) {
	return Guard([&] {
		auto* microphone =
		    source != nullptr
		        ? dynamic_cast<core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr || device_id == nullptr || *device_id == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid microphone source or device ID");
		}
		return microphone->SwitchDevice(device_id)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to switch microphone device");
	});
}

lk_status_t lk_audio_source_microphone_set_muted(lk_audio_source_t* source, int muted) {
	return Guard([&] {
		auto* microphone =
		    source != nullptr
		        ? dynamic_cast<core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "audio source is not a microphone source");
		}
		microphone->SetMuted(muted != 0);
		return LK_STATUS_OK;
	});
}

int lk_audio_source_microphone_is_muted(const lk_audio_source_t* source) {
	try {
		ClearError();
		const auto* microphone =
		    source != nullptr
		        ? dynamic_cast<const core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			SetStatusError(LK_STATUS_INVALID_ARGUMENT, "audio source is not a microphone source");
			return 0;
		}
		return microphone != nullptr && microphone->IsMuted() ? 1 : 0;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "failed to query microphone mute state");
		return 0;
	}
}

lk_status_t lk_audio_source_microphone_set_volume(lk_audio_source_t* source, float volume) {
	return Guard([&] {
		auto* microphone =
		    source != nullptr
		        ? dynamic_cast<core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "audio source is not a microphone source");
		}
		return microphone->SetVolume(volume) ? LK_STATUS_OK
		                                     : Failure(LK_STATUS_INVALID_ARGUMENT,
		                                               "microphone volume must be between 0 and 1");
	});
}

float lk_audio_source_microphone_volume(const lk_audio_source_t* source) {
	try {
		ClearError();
		const auto* microphone =
		    source != nullptr
		        ? dynamic_cast<const core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr) {
			SetStatusError(LK_STATUS_INVALID_ARGUMENT, "audio source is not a microphone source");
			return 0.0F;
		}
		return microphone->Volume();
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "failed to query microphone volume");
		return 0.0F;
	}
}

lk_status_t
lk_audio_source_microphone_set_processing_options(lk_audio_source_t* source,
                                                  const lk_audio_source_options_t* options) {
	return Guard([&] {
		auto* microphone =
		    source != nullptr
		        ? dynamic_cast<core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr || options == nullptr ||
		    options->struct_size < sizeof(options->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "microphone source and initialized processing options are required");
		}
		auto values = microphone->ProcessingOptions();
		if (LKC_HAS_FIELD(options, lk_audio_source_options_t, echo_cancellation)) {
			values.echo_cancellation = options->echo_cancellation != 0;
		}
		if (LKC_HAS_FIELD(options, lk_audio_source_options_t, auto_gain_control)) {
			values.auto_gain_control = options->auto_gain_control != 0;
		}
		if (LKC_HAS_FIELD(options, lk_audio_source_options_t, noise_suppression)) {
			values.noise_suppression = options->noise_suppression != 0;
		}
		return microphone->SetProcessingOptions(values)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED,
		                     "failed to reconfigure microphone audio processing");
	});
}

lk_status_t lk_audio_source_microphone_processing_options(const lk_audio_source_t* source,
                                                          lk_audio_source_options_t* options) {
	return Guard([&] {
		const auto* microphone =
		    source != nullptr
		        ? dynamic_cast<const core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr || options == nullptr ||
		    options->struct_size < sizeof(options->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "microphone source and initialized processing options are required");
		}
		const auto values = microphone->ProcessingOptions();
		lk_audio_source_options_t result;
		lk_audio_source_options_init(&result);
		result.echo_cancellation = values.echo_cancellation ? 1 : 0;
		result.auto_gain_control = values.auto_gain_control ? 1 : 0;
		result.noise_suppression = values.noise_suppression ? 1 : 0;
		std::memcpy(options, &result, std::min(options->struct_size, sizeof(result)));
		return LK_STATUS_OK;
	});
}

lk_status_t lk_audio_source_microphone_processing_stats(const lk_audio_source_t* source,
                                                        lk_microphone_processing_stats_t* stats) {
	return Guard([&] {
		const auto* microphone =
		    source != nullptr
		        ? dynamic_cast<const core::MicrophoneAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (microphone == nullptr || stats == nullptr ||
		    stats->struct_size < sizeof(stats->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "invalid microphone source or processing stats output");
		}
		const auto values = microphone->ProcessingStats();
		lk_microphone_processing_stats_t result;
		lk_microphone_processing_stats_init(&result);
		result.capture_frames_processed = values.capture_frames_processed;
		result.render_frames_processed = values.render_frames_processed;
		result.capture_processing_errors = values.capture_processing_errors;
		result.render_processing_errors = values.render_processing_errors;
		result.frames_dropped = values.frames_dropped;
		result.echo_cancellation_enabled = values.echo_cancellation_enabled ? 1 : 0;
		result.echo_return_loss_available = values.echo_return_loss_available ? 1 : 0;
		result.echo_return_loss_db = values.echo_return_loss_db;
		result.echo_return_loss_enhancement_available =
		    values.echo_return_loss_enhancement_available ? 1 : 0;
		result.echo_return_loss_enhancement_db = values.echo_return_loss_enhancement_db;
		result.residual_echo_likelihood_available =
		    values.residual_echo_likelihood_available ? 1 : 0;
		result.residual_echo_likelihood = values.residual_echo_likelihood;
		result.residual_echo_likelihood_recent_max_available =
		    values.residual_echo_likelihood_recent_max_available ? 1 : 0;
		result.residual_echo_likelihood_recent_max = values.residual_echo_likelihood_recent_max;
		result.delay_median_available = values.delay_median_available ? 1 : 0;
		result.delay_median_ms = values.delay_median_ms;
		result.delay_standard_deviation_available =
		    values.delay_standard_deviation_available ? 1 : 0;
		result.delay_standard_deviation_ms = values.delay_standard_deviation_ms;
		result.delay_available = values.delay_available ? 1 : 0;
		result.delay_ms = values.delay_ms;
		std::memcpy(stats, &result, std::min(stats->struct_size, sizeof(result)));
		return LK_STATUS_OK;
	});
}

lk_status_t lk_audio_source_system_audio_start(lk_audio_source_t* source) {
	return Guard([&] {
		auto* system_audio =
		    source != nullptr
		        ? dynamic_cast<core::SystemAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (system_audio == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "audio source is not a system audio source");
		}
		return system_audio->Start()
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to start system audio capture");
	});
}

lk_status_t lk_audio_source_system_audio_stop(lk_audio_source_t* source) {
	return Guard([&] {
		auto* system_audio =
		    source != nullptr
		        ? dynamic_cast<core::SystemAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (system_audio == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "audio source is not a system audio source");
		}
		system_audio->Stop();
		return LK_STATUS_OK;
	});
}

int lk_audio_source_system_audio_is_capturing(const lk_audio_source_t* source) {
	const auto* system_audio =
	    source != nullptr
	        ? dynamic_cast<const core::SystemAudioSourceInterface*>(source->source.get())
	        : nullptr;
	return system_audio != nullptr && system_audio->IsCapturing() ? 1 : 0;
}

size_t lk_audio_source_system_audio_device_id(const lk_audio_source_t* source, char* buffer,
                                              size_t buffer_size) {
	return SizeGuard([&] {
		const auto* system_audio =
		    source != nullptr
		        ? dynamic_cast<const core::SystemAudioSourceInterface*>(source->source.get())
		        : nullptr;
		return system_audio != nullptr
		           ? CopyString(system_audio->DeviceId(), buffer, buffer_size)
		           : InvalidSizeResult("audio source is not a system audio source");
	});
}

lk_status_t lk_audio_source_system_audio_switch_device(lk_audio_source_t* source,
                                                       const char* device_id) {
	return Guard([&] {
		auto* system_audio =
		    source != nullptr
		        ? dynamic_cast<core::SystemAudioSourceInterface*>(source->source.get())
		        : nullptr;
		if (system_audio == nullptr || device_id == nullptr || *device_id == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "system audio source and output device ID are required");
		}
		return system_audio->SwitchDevice(device_id)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to switch system audio device");
	});
}

lk_status_t lk_video_source_create(const lk_video_source_options_t* options,
                                   lk_video_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		core::VideoSourceOptions core_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_video_source_options_t, is_screencast)) {
				core_options.is_screencast = options->is_screencast != 0;
			}
		}
		auto result = std::make_unique<lk_video_source_t>();
		result->source.reset(core::CreateVideoSource(core_options));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create video source");
		}
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_video_source_create_camera(const lk_camera_capture_options_t* options,
                                          lk_video_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		lk_camera_capture_options_t values;
		lk_camera_capture_options_init(&values);
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid camera options struct size");
			}
			std::memcpy(&values, options, std::min(options->struct_size, sizeof(values)));
		}
		if (values.width == 0 || values.height == 0 || values.frames_per_second == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid camera capture dimensions or rate");
		}
		core::CameraCaptureOptions core_options;
		if (values.device_id != nullptr) {
			core_options.device_id = values.device_id;
		}
		core_options.width = values.width;
		core_options.height = values.height;
		core_options.frames_per_second = values.frames_per_second;
		auto result = std::make_unique<lk_video_source_t>();
		result->source.reset(core::CreateCameraVideoSource(std::move(core_options)));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to open camera device");
		}
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_video_source_create_screen(const lk_screen_capture_options_t* options,
                                          lk_video_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		lk_screen_capture_options_t values;
		lk_screen_capture_options_init(&values);
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid screen options struct size");
			}
			std::memcpy(&values, options, std::min(options->struct_size, sizeof(values)));
		}
		if (values.source_id == nullptr || *values.source_id == '\0' ||
		    values.frames_per_second == 0 || values.frames_per_second > 60) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid screen source ID or frame rate");
		}
		core::ScreenCaptureOptions core_options;
		core_options.source_id = values.source_id;
		core_options.frames_per_second = values.frames_per_second;
		core_options.include_cursor = values.include_cursor != 0;
		auto result = std::make_unique<lk_video_source_t>();
		result->source.reset(core::CreateScreenVideoSource(std::move(core_options)));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to open screen source");
		}
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_video_source_destroy(lk_video_source_t* source) {
	if (source == nullptr) {
		return LK_STATUS_OK;
	}
	if (source->track_references.load() != 0) {
		return Failure(LK_STATUS_INVALID_STATE, "video source is still used by a track");
	}
	delete source;
	return LK_STATUS_OK;
}

lk_status_t lk_video_source_capture_i420(lk_video_source_t* source, const uint8_t* data,
                                         size_t data_size, uint32_t width, uint32_t height,
                                         int64_t timestamp_us) {
	return Guard([&] {
		if (source == nullptr || data == nullptr || data_size == 0 || width == 0 || height == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video frame");
		}
		core::VideoFrame frame;
		frame.data.assign(data, data + data_size);
		frame.width = width;
		frame.height = height;
		frame.timestamp_us = timestamp_us;
		return source->source->CaptureFrame(frame)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to capture video frame");
	});
}

lk_status_t lk_video_source_camera_start(lk_video_source_t* source) {
	return Guard([&] {
		auto* camera = source != nullptr
		                   ? dynamic_cast<core::CameraVideoSourceInterface*>(source->source.get())
		                   : nullptr;
		if (camera == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "video source is not a camera source");
		}
		return camera->Start()
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to start camera capture");
	});
}

lk_status_t lk_video_source_camera_stop(lk_video_source_t* source) {
	return Guard([&] {
		auto* camera = source != nullptr
		                   ? dynamic_cast<core::CameraVideoSourceInterface*>(source->source.get())
		                   : nullptr;
		if (camera == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "video source is not a camera source");
		}
		camera->Stop();
		return LK_STATUS_OK;
	});
}

int lk_video_source_camera_is_capturing(const lk_video_source_t* source) {
	try {
		ClearError();
		const auto* camera =
		    source != nullptr
		        ? dynamic_cast<const core::CameraVideoSourceInterface*>(source->source.get())
		        : nullptr;
		if (camera == nullptr) {
			SetStatusError(LK_STATUS_INVALID_ARGUMENT, "video source is not a camera source");
			return 0;
		}
		return camera != nullptr && camera->IsCapturing() ? 1 : 0;
	} catch (const std::exception& exception) {
		SetStatusError(LK_STATUS_EXCEPTION, exception.what());
		return 0;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "unknown C++ exception");
		return 0;
	}
}

size_t lk_video_source_camera_device_id(const lk_video_source_t* source, char* buffer,
                                        size_t buffer_size) {
	return SizeGuard([&] {
		const auto* camera =
		    source != nullptr
		        ? dynamic_cast<const core::CameraVideoSourceInterface*>(source->source.get())
		        : nullptr;
		if (camera == nullptr) {
			return InvalidSizeResult("video source is not a camera source");
		}
		return CopyString(camera->DeviceId(), buffer, buffer_size);
	});
}

lk_status_t lk_video_source_camera_switch_device(lk_video_source_t* source, const char* device_id) {
	return Guard([&] {
		auto* camera = source != nullptr
		                   ? dynamic_cast<core::CameraVideoSourceInterface*>(source->source.get())
		                   : nullptr;
		if (camera == nullptr || device_id == nullptr || *device_id == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid camera source or device ID");
		}
		return camera->SwitchDevice(device_id)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to switch camera device");
	});
}

lk_status_t lk_video_source_screen_start(lk_video_source_t* source) {
	return Guard([&] {
		auto* screen = source != nullptr
		                   ? dynamic_cast<core::ScreenVideoSourceInterface*>(source->source.get())
		                   : nullptr;
		if (screen == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "video source is not a screen source");
		}
		return screen->Start()
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to start screen capture");
	});
}

lk_status_t lk_video_source_screen_stop(lk_video_source_t* source) {
	return Guard([&] {
		auto* screen = source != nullptr
		                   ? dynamic_cast<core::ScreenVideoSourceInterface*>(source->source.get())
		                   : nullptr;
		if (screen == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "video source is not a screen source");
		}
		screen->Stop();
		return LK_STATUS_OK;
	});
}

int lk_video_source_screen_is_capturing(const lk_video_source_t* source) {
	try {
		ClearError();
		const auto* screen =
		    source != nullptr
		        ? dynamic_cast<const core::ScreenVideoSourceInterface*>(source->source.get())
		        : nullptr;
		if (screen == nullptr) {
			SetStatusError(LK_STATUS_INVALID_ARGUMENT, "video source is not a screen source");
			return 0;
		}
		return screen != nullptr && screen->IsCapturing() ? 1 : 0;
	} catch (const std::exception& exception) {
		SetStatusError(LK_STATUS_EXCEPTION, exception.what());
		return 0;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "unknown C++ exception");
		return 0;
	}
}

size_t lk_video_source_screen_source_id(const lk_video_source_t* source, char* buffer,
                                        size_t buffer_size) {
	return SizeGuard([&] {
		const auto* screen =
		    source != nullptr
		        ? dynamic_cast<const core::ScreenVideoSourceInterface*>(source->source.get())
		        : nullptr;
		if (screen == nullptr) {
			return InvalidSizeResult("video source is not a screen source");
		}
		return CopyString(screen->SourceId(), buffer, buffer_size);
	});
}

lk_status_t lk_video_source_screen_switch_source(lk_video_source_t* source, const char* source_id) {
	return Guard([&] {
		auto* screen = source != nullptr
		                   ? dynamic_cast<core::ScreenVideoSourceInterface*>(source->source.get())
		                   : nullptr;
		if (screen == nullptr || source_id == nullptr || *source_id == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid screen source or source ID");
		}
		return screen->SwitchSource(source_id)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to switch screen source");
	});
}

lk_status_t lk_room_create_audio_track(lk_room_t* room, const char* label,
                                       lk_audio_source_t* source, lk_local_track_t** track) {
	return Guard([&] {
		if (track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "track output is null");
		}
		*track = nullptr;
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || label == nullptr || source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, label, and source are required");
		}
		auto result = std::make_unique<lk_local_track_t>();
		result->track.reset(participant->CreateLocalAudioTrack(label, source->source.get()));
		if (!result->track) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create audio track");
		}
		result->owner = room;
		result->room_state = room->state;
		result->audio_source = source;
		source->track_references.fetch_add(1);
		{
			std::lock_guard<std::mutex> guard(room->local_tracks_mutex);
			room->local_tracks.push_back(result.get());
		}
		*track = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_create_video_track(lk_room_t* room, const char* label,
                                       lk_video_source_t* source, lk_local_track_t** track) {
	return Guard([&] {
		if (track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "track output is null");
		}
		*track = nullptr;
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || label == nullptr || source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, label, and source are required");
		}
		auto result = std::make_unique<lk_local_track_t>();
		result->track.reset(participant->CreateLocalVideoTrack(label, source->source.get()));
		if (!result->track) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create video track");
		}
		result->owner = room;
		result->room_state = room->state;
		result->video_source = source;
		source->track_references.fetch_add(1);
		{
			std::lock_guard<std::mutex> guard(room->local_tracks_mutex);
			room->local_tracks.push_back(result.get());
		}
		*track = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_local_track_publish(lk_room_t* room, lk_local_track_t* track,
                                   const lk_track_publish_options_t* options) {
	return Guard([&] { return PublishLocalTrack(room, track, options, PublishMode::Track); });
}

lk_status_t lk_local_track_publish_screen_share_video(lk_room_t* room, lk_local_track_t* track,
                                                      const lk_track_publish_options_t* options) {
	return Guard(
	    [&] { return PublishLocalTrack(room, track, options, PublishMode::ScreenShareVideo); });
}

lk_status_t lk_local_track_publish_screen_share_audio(lk_room_t* room, lk_local_track_t* track,
                                                      const lk_track_publish_options_t* options) {
	return Guard(
	    [&] { return PublishLocalTrack(room, track, options, PublishMode::ScreenShareAudio); });
}

lk_status_t lk_local_track_unpublish(lk_local_track_t* track, int stop_on_unpublish) {
	return Guard([&] {
		if (track == nullptr || track->track == nullptr || track->owner == nullptr ||
		    track->room_state == nullptr || !track->room_state->alive.load()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "live local track is required");
		}
		if (!track->published.load()) {
			return Failure(LK_STATUS_INVALID_STATE, "track is not published");
		}
		auto* participant = LocalParticipant(track->owner);
		if (participant == nullptr ||
		    !participant->UnpublishTrack(track->track.get(), stop_on_unpublish != 0)) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to unpublish track");
		}
		track->published.store(false);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_republish_all_tracks(lk_room_t* room) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "live room is required");
		}
		return participant->RepublishAllTracks()
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to republish all tracks");
	});
}

lk_status_t lk_local_track_set_muted(lk_local_track_t* track, int muted) {
	return Guard([&] {
		if (track == nullptr || track->track == nullptr || track->owner == nullptr ||
		    track->room_state == nullptr || !track->room_state->alive.load()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "live local track is required");
		}
		if (!track->published.load()) {
			return Failure(LK_STATUS_INVALID_STATE, "track is not published");
		}
		return track->owner->room->SetLocalTrackMuted(track->track->Sid(), muted != 0)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update track mute state");
	});
}

lk_status_t lk_local_video_track_update_encoding(lk_local_track_t* track,
                                                 const lk_video_encoding_t* encoding,
                                                 int backup_codec) {
	return Guard([&] {
		if (track == nullptr || track->track == nullptr || track->owner == nullptr ||
		    track->room_state == nullptr || !track->room_state->alive.load() ||
		    track->track->Kind() != core::TrackKind::Video) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "live local video track is required");
		}
		if (!track->published.load()) {
			return Failure(LK_STATUS_INVALID_STATE, "track is not published");
		}
		core::VideoEncoding core_encoding;
		const auto status = ToCoreVideoEncoding(encoding, core_encoding);
		if (status != LK_STATUS_OK) {
			return status;
		}
		auto* participant = LocalParticipant(track->owner);
		if (participant == nullptr || !participant->UpdateVideoEncoding(
		                                  track->track.get(), core_encoding, backup_codec != 0)) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to update video encoding");
		}
		return LK_STATUS_OK;
	});
}

size_t lk_local_track_rtc_stats(const lk_local_track_t* track, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr && track->track != nullptr
		           ? CopyString(track->track->GetRTCStats(), buffer, buffer_size)
		           : InvalidSizeResult("local track is required");
	});
}

lk_status_t lk_local_track_destroy(lk_local_track_t* track) {
	if (track == nullptr) {
		return LK_STATUS_OK;
	}
	if (track->published.load() && track->room_state != nullptr &&
	    track->room_state->alive.load() && track->room_state->connected.load()) {
		return Failure(LK_STATUS_INVALID_STATE,
		               "disconnect the room before destroying a published track");
	}
	if (track->owner != nullptr && track->room_state != nullptr &&
	    track->room_state->alive.load()) {
		std::lock_guard<std::mutex> guard(track->owner->local_tracks_mutex);
		auto& tracks = track->owner->local_tracks;
		tracks.erase(std::remove(tracks.begin(), tracks.end(), track), tracks.end());
	}
	if (track->audio_source != nullptr) {
		track->audio_source->track_references.fetch_sub(1);
	}
	if (track->video_source != nullptr) {
		track->video_source->track_references.fetch_sub(1);
	}
	delete track;
	return LK_STATUS_OK;
}

lk_status_t lk_room_set_remote_track_subscribed(lk_room_t* room, const char* participant_sid,
                                                const char* track_sid, int subscribed) {
	return Guard([&] {
		if (room == nullptr || participant_sid == nullptr || track_sid == nullptr ||
		    *participant_sid == '\0' || *track_sid == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room, participant SID, and track SID are required");
		}
		return room->room->SetRemoteTrackSubscribed(participant_sid, track_sid, subscribed != 0)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update track subscription");
	});
}

lk_status_t lk_room_update_remote_track_settings(lk_room_t* room, const char* participant_sid,
                                                 const char* track_sid,
                                                 const lk_remote_track_settings_t* settings) {
	return Guard([&] {
		if (room == nullptr || room->room == nullptr || participant_sid == nullptr ||
		    *participant_sid == '\0' || track_sid == nullptr || *track_sid == '\0' ||
		    settings == nullptr || settings->struct_size < sizeof(settings->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room, participant SID, track SID, and settings are required");
		}
		core::RemoteTrackSettings converted;
		if (LKC_HAS_FIELD(settings, lk_remote_track_settings_t, enabled)) {
			converted.enabled = settings->enabled != 0;
		}
		if (LKC_HAS_FIELD(settings, lk_remote_track_settings_t, has_video_quality) &&
		    settings->has_video_quality != 0) {
			if (!LKC_HAS_FIELD(settings, lk_remote_track_settings_t, video_quality) ||
			    !ToCoreVideoQuality(settings->video_quality, converted.video_quality.emplace())) {
				converted.video_quality.reset();
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video quality");
			}
		}
		if (LKC_HAS_FIELD(settings, lk_remote_track_settings_t, video_width) ||
		    LKC_HAS_FIELD(settings, lk_remote_track_settings_t, video_height)) {
			const uint32_t width = LKC_HAS_FIELD(settings, lk_remote_track_settings_t, video_width)
			                           ? settings->video_width
			                           : 0;
			const uint32_t height =
			    LKC_HAS_FIELD(settings, lk_remote_track_settings_t, video_height)
			        ? settings->video_height
			        : 0;
			if ((width == 0) != (height == 0)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT,
				               "video width and height must both be zero or non-zero");
			}
			if (width != 0) {
				converted.video_dimensions = core::TrackDimensions{width, height};
			}
		}
		if (converted.video_quality.has_value() && converted.video_dimensions.has_value()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "video quality and dimensions are mutually exclusive");
		}
		if (LKC_HAS_FIELD(settings, lk_remote_track_settings_t, video_fps)) {
			converted.video_fps = settings->video_fps;
		}
		if (LKC_HAS_FIELD(settings, lk_remote_track_settings_t, priority)) {
			converted.priority = settings->priority;
		}
		return room->room->UpdateRemoteTrackSettings(participant_sid, track_sid, converted)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "remote track settings update failed");
	});
}

lk_status_t
lk_room_set_track_subscription_permissions(lk_room_t* room, int all_participants_allowed,
                                           const lk_participant_track_permission_t* permissions,
                                           size_t permission_count) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (permission_count != 0 && permissions == nullptr)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or permission array");
		}
		std::vector<core::ParticipantTrackPermission> converted;
		converted.reserve(permission_count);
		for (size_t index = 0; index < permission_count; ++index) {
			const auto& permission = permissions[index];
			if (permission.struct_size < sizeof(permission.struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid permission struct size");
			}
			core::ParticipantTrackPermission value;
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t, participant_sid) &&
			    permission.participant_sid != nullptr) {
				value.participant_sid = permission.participant_sid;
			}
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t,
			                  participant_identity) &&
			    permission.participant_identity != nullptr) {
				value.participant_identity = permission.participant_identity;
			}
			if (value.participant_sid.empty() && value.participant_identity.empty()) {
				return Failure(LK_STATUS_INVALID_ARGUMENT,
				               "permission participant SID or identity is required");
			}
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t, allow_all)) {
				value.allow_all = permission.allow_all != 0;
			}
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t,
			                  allowed_track_sid_count)) {
				if (permission.allowed_track_sid_count != 0 &&
				    permission.allowed_track_sids == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "allowed track SIDs are null");
				}
				value.allowed_track_sids = DestinationIdentities(
				    permission.allowed_track_sids, permission.allowed_track_sid_count);
				if (value.allowed_track_sids.size() != permission.allowed_track_sid_count) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "allowed track SID is null");
				}
			}
			converted.push_back(std::move(value));
		}
		return participant->SetTrackSubscriptionPermissions(all_participants_allowed != 0,
		                                                    converted)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED,
		                     "failed to update track subscription permissions");
	});
}

lk_status_t lk_room_publish_data(lk_room_t* room, const uint8_t* data, size_t data_size,
                                 const lk_data_publish_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (data == nullptr && data_size != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or data buffer");
		}
		core::DataPublishOptions publish_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid data options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_data_publish_options_t, reliable)) {
				publish_options.reliable = options->reliable != 0;
			}
			if (LKC_HAS_FIELD(options, lk_data_publish_options_t, topic) &&
			    options->topic != nullptr) {
				publish_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_data_publish_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				publish_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
		}
		std::vector<uint8_t> payload;
		if (data_size != 0) {
			payload.assign(data, data + data_size);
		}
		return participant->PublishData(payload, std::move(publish_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to publish data");
	});
}

lk_data_track_error_code_t
lk_room_store_data_track_schema(lk_room_t* room, const lk_data_track_schema_id_t* schema_id,
                                const uint8_t* definition, size_t definition_size) {
	return DataTrackGuard([&] {
		if (room == nullptr || room->room == nullptr ||
		    (definition == nullptr && definition_size != 0)) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "room and valid schema definition are required");
		}
		core::DataTrackSchema schema;
		const auto id_error = ToCoreDataTrackSchemaId(schema_id, schema.id);
		if (id_error != LK_DATA_TRACK_ERROR_NONE) {
			return id_error;
		}
		if (definition_size != 0) {
			schema.definition.assign(definition, definition + definition_size);
		}
		return ToCDataTrackError(room->room->StoreDataTrackSchema(std::move(schema)));
	});
}

lk_data_track_error_code_t lk_room_get_data_track_schema(lk_room_t* room,
                                                         const char* participant_identity,
                                                         const lk_data_track_schema_id_t* schema_id,
                                                         lk_data_track_schema_t** schema) {
	return DataTrackGuard([&] {
		if (schema == nullptr) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "DataTrack schema output is required");
		}
		*schema = nullptr;
		if (room == nullptr || room->room == nullptr || participant_identity == nullptr ||
		    *participant_identity == '\0') {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "room and participant identity are required");
		}
		core::DataTrackSchemaId core_id;
		const auto id_error = ToCoreDataTrackSchemaId(schema_id, core_id);
		if (id_error != LK_DATA_TRACK_ERROR_NONE) {
			return id_error;
		}
		auto result = room->room->GetDataTrackSchema(participant_identity, std::move(core_id));
		if (!result) {
			return ToCDataTrackError(std::move(result.error));
		}
		auto output = std::make_unique<lk_data_track_schema_t>();
		output->schema = std::move(*result.schema);
		*schema = output.release();
		return LK_DATA_TRACK_ERROR_NONE;
	});
}

void lk_data_track_schema_destroy(lk_data_track_schema_t* schema) { delete schema; }

size_t lk_data_track_schema_name(const lk_data_track_schema_t* schema, char* buffer,
                                 size_t buffer_size) {
	return SizeGuard([&] {
		return schema != nullptr ? CopyString(schema->schema.id.name, buffer, buffer_size)
		                         : InvalidSizeResult("DataTrack schema is required");
	});
}

lk_data_track_schema_encoding_t
lk_data_track_schema_encoding(const lk_data_track_schema_t* schema) {
	return schema != nullptr ? ToCDataTrackSchemaEncoding(schema->schema.id.encoding.kind)
	                         : LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED;
}

size_t lk_data_track_schema_custom_encoding(const lk_data_track_schema_t* schema, char* buffer,
                                            size_t buffer_size) {
	return SizeGuard([&] {
		return schema != nullptr
		           ? CopyString(schema->schema.id.encoding.custom, buffer, buffer_size)
		           : InvalidSizeResult("DataTrack schema is required");
	});
}

size_t lk_data_track_schema_definition(const lk_data_track_schema_t* schema, uint8_t* buffer,
                                       size_t buffer_size) {
	return SizeGuard([&] {
		return schema != nullptr ? CopyBytes(schema->schema.definition, buffer, buffer_size)
		                         : InvalidSizeResult("DataTrack schema is required");
	});
}

lk_data_track_error_code_t
lk_room_publish_data_track(lk_room_t* room, const lk_data_track_publish_options_t* options,
                           lk_local_data_track_t** track) {
	return DataTrackGuard([&] {
		if (track == nullptr) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "local DataTrack output is required");
		}
		*track = nullptr;
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || room == nullptr) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_DISCONNECTED,
			                        "room is unavailable or disconnected");
		}
		core::DataTrackPublishOptions publish_options;
		const auto options_error = ToCoreDataTrackPublishOptions(options, publish_options);
		if (options_error != LK_DATA_TRACK_ERROR_NONE) {
			return options_error;
		}
		auto published = participant->PublishDataTrack(std::move(publish_options));
		if (!published) {
			return ToCDataTrackError(std::move(published.error));
		}
		auto output = std::make_unique<lk_local_data_track_t>();
		output->track = std::move(published.track);
		output->owner = room;
		output->room_state = room->state;
		{
			std::lock_guard<std::mutex> guard(room->local_data_tracks_mutex);
			room->local_data_tracks.push_back(output.get());
		}
		*track = output.release();
		return LK_DATA_TRACK_ERROR_NONE;
	});
}

lk_data_track_error_code_t lk_local_data_track_try_push(lk_local_data_track_t* track,
                                                        const uint8_t* data, size_t data_size,
                                                        int has_user_timestamp,
                                                        uint64_t user_timestamp) {
	return DataTrackGuard([&] {
		if (track == nullptr || track->track == nullptr || (data == nullptr && data_size != 0)) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "local DataTrack and valid frame data are required");
		}
		if (track->room_state == nullptr || !track->room_state->alive.load()) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_DISCONNECTED,
			                        "DataTrack room has been destroyed");
		}
		core::DataTrackFrame frame;
		if (data_size != 0) {
			frame.payload.assign(data, data + data_size);
		}
		if (has_user_timestamp) {
			frame.user_timestamp = user_timestamp;
		}
		return ToCDataTrackError(track->track->TryPush(frame));
	});
}

lk_data_track_error_code_t lk_local_data_track_unpublish(lk_local_data_track_t* track) {
	return DataTrackGuard([&] {
		if (track == nullptr || track->track == nullptr) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "local DataTrack is required");
		}
		if (track->room_state == nullptr || !track->room_state->alive.load()) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_DISCONNECTED,
			                        "DataTrack room has been destroyed");
		}
		return ToCDataTrackError(track->track->Unpublish());
	});
}

lk_data_track_error_code_t lk_local_data_track_destroy(lk_local_data_track_t* track) {
	return DataTrackGuard([&] {
		if (track == nullptr) {
			return LK_DATA_TRACK_ERROR_NONE;
		}
		if (track->track != nullptr && track->track->IsPublished() &&
		    track->room_state != nullptr && track->room_state->alive.load() &&
		    track->room_state->connected.load()) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_NOT_ALLOWED,
			                        "unpublish the DataTrack before destroying its handle");
		}
		if (track->owner != nullptr && track->room_state != nullptr &&
		    track->room_state->alive.load()) {
			std::lock_guard<std::mutex> guard(track->owner->local_data_tracks_mutex);
			auto& tracks = track->owner->local_data_tracks;
			tracks.erase(std::remove(tracks.begin(), tracks.end(), track), tracks.end());
		}
		delete track;
		return LK_DATA_TRACK_ERROR_NONE;
	});
}

int lk_local_data_track_is_published(const lk_local_data_track_t* track) {
	return track != nullptr && track->track != nullptr && track->track->IsPublished();
}

lk_status_t lk_local_data_track_info(const lk_local_data_track_t* track,
                                     lk_data_track_snapshot_info_t* info) {
	return Guard([&] {
		if (track == nullptr || track->track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "local DataTrack is required");
		}
		const auto converted =
		    ToCDataTrackSnapshotInfo(track->track->Info(), track->track->IsPublished());
		return CopyOutputStruct(converted, info, "initialized DataTrack info is required");
	});
}

size_t lk_local_data_track_sid(const lk_local_data_track_t* track, char* buffer,
                               size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr && track->track != nullptr
		           ? CopyString(track->track->Info().sid, buffer, buffer_size)
		           : InvalidSizeResult("local DataTrack is required");
	});
}

size_t lk_local_data_track_name(const lk_local_data_track_t* track, char* buffer,
                                size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr && track->track != nullptr
		           ? CopyString(track->track->Info().name, buffer, buffer_size)
		           : InvalidSizeResult("local DataTrack is required");
	});
}

size_t lk_local_data_track_custom_frame_encoding(const lk_local_data_track_t* track, char* buffer,
                                                 size_t buffer_size) {
	return SizeGuard([&] {
		if (track == nullptr || track->track == nullptr) {
			return InvalidSizeResult("local DataTrack is required");
		}
		const auto info = track->track->Info();
		return CopyString(info.frame_encoding ? info.frame_encoding->custom : std::string{}, buffer,
		                  buffer_size);
	});
}

size_t lk_local_data_track_schema_name(const lk_local_data_track_t* track, char* buffer,
                                       size_t buffer_size) {
	return SizeGuard([&] {
		if (track == nullptr || track->track == nullptr) {
			return InvalidSizeResult("local DataTrack is required");
		}
		const auto info = track->track->Info();
		return CopyString(info.schema ? info.schema->name : std::string{}, buffer, buffer_size);
	});
}

size_t lk_local_data_track_custom_schema_encoding(const lk_local_data_track_t* track, char* buffer,
                                                  size_t buffer_size) {
	return SizeGuard([&] {
		if (track == nullptr || track->track == nullptr) {
			return InvalidSizeResult("local DataTrack is required");
		}
		const auto info = track->track->Info();
		return CopyString(info.schema ? info.schema->encoding.custom : std::string{}, buffer,
		                  buffer_size);
	});
}

lk_status_t lk_room_create_remote_data_track_snapshot(const lk_room_t* room,
                                                      lk_remote_data_track_list_t** snapshot) {
	return Guard([&] {
		if (room == nullptr || room->room == nullptr || snapshot == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room and remote DataTrack snapshot output are required");
		}
		*snapshot = nullptr;
		auto result = std::make_unique<lk_remote_data_track_list_t>();
		auto tracks = room->room->GetRemoteDataTrackSnapshots();
		result->tracks.reserve(tracks.size());
		for (auto& track : tracks) {
			result->tracks.push_back({std::move(track)});
		}
		*snapshot = result.release();
		return LK_STATUS_OK;
	});
}

void lk_remote_data_track_list_destroy(lk_remote_data_track_list_t* snapshot) { delete snapshot; }

size_t lk_remote_data_track_list_count(const lk_remote_data_track_list_t* snapshot) {
	return snapshot != nullptr ? snapshot->tracks.size() : 0;
}

lk_status_t lk_remote_data_track_list_at(const lk_remote_data_track_list_t* snapshot, size_t index,
                                         const lk_remote_data_track_snapshot_t** track) {
	return Guard([&] {
		if (track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "remote DataTrack snapshot output is required");
		}
		*track = nullptr;
		if (snapshot == nullptr || index >= snapshot->tracks.size()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "remote DataTrack snapshot index is out of range");
		}
		*track = &snapshot->tracks[index];
		return LK_STATUS_OK;
	});
}

lk_status_t lk_remote_data_track_snapshot_info(const lk_remote_data_track_snapshot_t* track,
                                               lk_data_track_snapshot_info_t* info) {
	return Guard([&] {
		if (track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "remote DataTrack snapshot is required");
		}
		return CopyOutputStruct(ToCDataTrackSnapshotInfo(track->value.info, track->value.published),
		                        info, "initialized remote DataTrack info is required");
	});
}

size_t
lk_remote_data_track_snapshot_publisher_identity(const lk_remote_data_track_snapshot_t* track,
                                                 char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr ? CopyString(track->value.publisher_identity, buffer, buffer_size)
		                        : InvalidSizeResult("remote DataTrack snapshot is required");
	});
}

size_t lk_remote_data_track_snapshot_sid(const lk_remote_data_track_snapshot_t* track, char* buffer,
                                         size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr ? CopyString(track->value.info.sid, buffer, buffer_size)
		                        : InvalidSizeResult("remote DataTrack snapshot is required");
	});
}

size_t lk_remote_data_track_snapshot_name(const lk_remote_data_track_snapshot_t* track,
                                          char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return track != nullptr ? CopyString(track->value.info.name, buffer, buffer_size)
		                        : InvalidSizeResult("remote DataTrack snapshot is required");
	});
}

size_t
lk_remote_data_track_snapshot_custom_frame_encoding(const lk_remote_data_track_snapshot_t* track,
                                                    char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		if (track == nullptr) {
			return InvalidSizeResult("remote DataTrack snapshot is required");
		}
		const auto& encoding = track->value.info.frame_encoding;
		return CopyString(encoding ? encoding->custom : std::string{}, buffer, buffer_size);
	});
}

size_t lk_remote_data_track_snapshot_schema_name(const lk_remote_data_track_snapshot_t* track,
                                                 char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		if (track == nullptr) {
			return InvalidSizeResult("remote DataTrack snapshot is required");
		}
		const auto& schema = track->value.info.schema;
		return CopyString(schema ? schema->name : std::string{}, buffer, buffer_size);
	});
}

size_t
lk_remote_data_track_snapshot_custom_schema_encoding(const lk_remote_data_track_snapshot_t* track,
                                                     char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		if (track == nullptr) {
			return InvalidSizeResult("remote DataTrack snapshot is required");
		}
		const auto& schema = track->value.info.schema;
		return CopyString(schema ? schema->encoding.custom : std::string{}, buffer, buffer_size);
	});
}

lk_data_track_error_code_t lk_room_subscribe_data_track(
    lk_room_t* room, const char* participant_identity, const char* track_sid,
    const lk_data_track_subscription_options_t* options, lk_data_track_reader_t** reader) {
	return DataTrackGuard([&] {
		if (reader == nullptr) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "DataTrack reader output is required");
		}
		*reader = nullptr;
		if (room == nullptr || room->room == nullptr || participant_identity == nullptr ||
		    *participant_identity == '\0' || track_sid == nullptr || *track_sid == '\0') {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "room, participant identity, and DataTrack SID are required");
		}
		if (!room->room->IsConnected()) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_DISCONNECTED, "room is disconnected");
		}
		core::DataTrackSubscriptionOptions subscription_options;
		const auto options_error =
		    ToCoreDataTrackSubscriptionOptions(options, subscription_options);
		if (options_error != LK_DATA_TRACK_ERROR_NONE) {
			return options_error;
		}
		auto track = room->room->GetRemoteDataTrack(participant_identity, track_sid);
		if (track == nullptr) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_NOT_FOUND,
			                        "remote participant or DataTrack was not found");
		}
		auto subscribed = track->Subscribe(subscription_options);
		if (!subscribed) {
			return DataTrackFailure(track->IsPublished() ? LK_DATA_TRACK_ERROR_SEND_FAILED
			                                             : LK_DATA_TRACK_ERROR_UNPUBLISHED,
			                        "failed to subscribe to remote DataTrack");
		}
		auto output = std::make_unique<lk_data_track_reader_t>();
		output->reader = std::move(subscribed);
		*reader = output.release();
		return LK_DATA_TRACK_ERROR_NONE;
	});
}

lk_data_track_error_code_t lk_room_update_data_track_subscription_options(
    lk_room_t* room, const char* participant_identity, const char* track_sid,
    const lk_data_track_subscription_options_t* options) {
	return DataTrackGuard([&] {
		if (room == nullptr || room->room == nullptr || participant_identity == nullptr ||
		    *participant_identity == '\0' || track_sid == nullptr || *track_sid == '\0') {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                        "room, participant identity, and DataTrack SID are required");
		}
		if (!room->room->IsConnected()) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_DISCONNECTED, "room is disconnected");
		}
		core::DataTrackSubscriptionOptions subscription_options;
		const auto options_error =
		    ToCoreDataTrackSubscriptionOptions(options, subscription_options);
		if (options_error != LK_DATA_TRACK_ERROR_NONE) {
			return options_error;
		}
		auto track = room->room->GetRemoteDataTrack(participant_identity, track_sid);
		if (track == nullptr) {
			return DataTrackFailure(LK_DATA_TRACK_ERROR_NOT_FOUND,
			                        "remote participant or DataTrack was not found");
		}
		if (!track->SetSubscriptionOptions(subscription_options)) {
			return DataTrackFailure(track->IsPublished() ? LK_DATA_TRACK_ERROR_SEND_FAILED
			                                             : LK_DATA_TRACK_ERROR_UNPUBLISHED,
			                        "failed to update remote DataTrack subscription options");
		}
		return LK_DATA_TRACK_ERROR_NONE;
	});
}

void lk_data_track_reader_destroy(lk_data_track_reader_t* reader) { delete reader; }

void lk_data_track_reader_close(lk_data_track_reader_t* reader) {
	if (reader != nullptr && reader->reader != nullptr) {
		reader->reader->Close();
	}
}

int lk_data_track_reader_is_closed(const lk_data_track_reader_t* reader) {
	return reader == nullptr || reader->reader == nullptr || reader->reader->IsClosed();
}

size_t lk_data_track_reader_dropped_frames(const lk_data_track_reader_t* reader) {
	return reader != nullptr && reader->reader != nullptr ? reader->reader->DroppedFrames() : 0;
}

static lk_data_track_read_status_t ReadDataTrackFrame(lk_data_track_reader_t* reader,
                                                      lk_data_track_frame_t** frame, bool wait,
                                                      uint32_t timeout_ms) {
	try {
		ClearError();
		if (frame == nullptr || reader == nullptr || reader->reader == nullptr) {
			SetDataTrackError(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT,
			                  "DataTrack reader and frame output are required");
			return LK_DATA_TRACK_READ_INVALID_ARGUMENT;
		}
		*frame = nullptr;
		core::DataTrackFrame value;
		const bool received =
		    wait ? reader->reader->ReadFor(value, std::chrono::milliseconds(timeout_ms))
		         : reader->reader->TryRead(value);
		if (!received) {
			return reader->reader->IsClosed() ? LK_DATA_TRACK_READ_CLOSED
			                                  : LK_DATA_TRACK_READ_EMPTY;
		}
		auto output = std::make_unique<lk_data_track_frame_t>();
		output->frame = std::move(value);
		*frame = output.release();
		return LK_DATA_TRACK_READ_FRAME;
	} catch (const std::exception& exception) {
		SetDataTrackError(LK_DATA_TRACK_ERROR_SEND_FAILED, exception.what());
		return LK_DATA_TRACK_READ_INVALID_ARGUMENT;
	} catch (...) {
		SetDataTrackError(LK_DATA_TRACK_ERROR_SEND_FAILED, "unknown C++ exception");
		return LK_DATA_TRACK_READ_INVALID_ARGUMENT;
	}
}

lk_data_track_read_status_t lk_data_track_reader_try_read(lk_data_track_reader_t* reader,
                                                          lk_data_track_frame_t** frame) {
	return ReadDataTrackFrame(reader, frame, false, 0);
}

lk_data_track_read_status_t lk_data_track_reader_read_for(lk_data_track_reader_t* reader,
                                                          uint32_t timeout_ms,
                                                          lk_data_track_frame_t** frame) {
	return ReadDataTrackFrame(reader, frame, true, timeout_ms);
}

void lk_data_track_frame_destroy(lk_data_track_frame_t* frame) { delete frame; }

size_t lk_data_track_frame_data(const lk_data_track_frame_t* frame, uint8_t* buffer,
                                size_t buffer_size) {
	return SizeGuard([&] {
		return frame != nullptr ? CopyBytes(frame->frame.payload, buffer, buffer_size)
		                        : InvalidSizeResult("DataTrack frame is required");
	});
}

int lk_data_track_frame_has_user_timestamp(const lk_data_track_frame_t* frame) {
	return frame != nullptr && frame->frame.user_timestamp.has_value();
}

uint64_t lk_data_track_frame_user_timestamp(const lk_data_track_frame_t* frame) {
	return frame != nullptr ? frame->frame.user_timestamp.value_or(0) : 0;
}

lk_status_t lk_room_publish_dtmf(lk_room_t* room, uint32_t code, const char* digit) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || digit == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and DTMF digit are required");
		}
		return participant->PublishDtmf(code, digit)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to publish SIP DTMF");
	});
}

lk_status_t lk_room_send_chat_message(lk_room_t* room, const char* message, char* message_id,
                                      size_t message_id_size, int64_t* timestamp) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || message == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and chat message are required");
		}
		if (message_id != nullptr && message_id_size < LK_CHAT_MESSAGE_ID_BUFFER_SIZE) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "chat message ID buffer is too small");
		}
		auto sent = participant->SendChatMessage(message);
		if (!sent.has_value()) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to send chat message");
		}
		if (message_id != nullptr) {
			CopyString(sent->id, message_id, message_id_size);
		}
		if (timestamp != nullptr) {
			*timestamp = sent->timestamp;
		}
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_edit_chat_message(lk_room_t* room, const char* message_id,
                                      int64_t original_timestamp, const char* message) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || message_id == nullptr || *message_id == '\0' ||
		    message == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room, message ID, and chat message are required");
		}
		core::ChatMessage original;
		original.id = message_id;
		original.timestamp = original_timestamp;
		return participant->EditChatMessage(message, original).has_value()
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to edit chat message");
	});
}

lk_status_t lk_room_send_text(lk_room_t* room, const char* text,
                              const lk_text_send_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || text == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and text are required");
		}
		core::TextSendOptions send_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid text options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, topic) &&
			    options->topic != nullptr) {
				send_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				send_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    send_options.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid text attributes");
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, reply_to_stream_id) &&
			    options->reply_to_stream_id != nullptr) {
				send_options.reply_to_stream_id = options->reply_to_stream_id;
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, attached_stream_id_count)) {
				if (options->attached_stream_id_count != 0 &&
				    options->attached_stream_ids == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "attached stream IDs are null");
				}
				send_options.attached_stream_ids = DestinationIdentities(
				    options->attached_stream_ids, options->attached_stream_id_count);
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, chunk_size)) {
				send_options.chunk_size = options->chunk_size;
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, compress)) {
				send_options.compress = options->compress != 0;
			}
		}
		return participant->SendText(text, std::move(send_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to send text stream");
	});
}

lk_status_t lk_room_send_bytes(lk_room_t* room, const uint8_t* data, size_t data_size,
                               const lk_byte_send_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (data == nullptr && data_size != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or byte buffer");
		}
		core::ByteSendOptions send_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid byte options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, topic) &&
			    options->topic != nullptr) {
				send_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, mime_type) &&
			    options->mime_type != nullptr) {
				send_options.mime_type = options->mime_type;
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, name) && options->name != nullptr) {
				send_options.name = options->name;
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				send_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    send_options.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid byte attributes");
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, chunk_size)) {
				send_options.chunk_size = options->chunk_size;
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, compress)) {
				send_options.compress = options->compress != 0;
			}
		}
		std::vector<uint8_t> payload;
		if (data_size != 0) {
			payload.assign(data, data + data_size);
		}
		return participant->SendBytes(payload, std::move(send_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to send byte stream");
	});
}

lk_status_t lk_room_send_file(lk_room_t* room, const char* path,
                              const lk_file_send_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || path == nullptr || *path == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and file path are required");
		}
		core::FileSendOptions send_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid file options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, topic) &&
			    options->topic != nullptr) {
				send_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, mime_type) &&
			    options->mime_type != nullptr) {
				send_options.mime_type = options->mime_type;
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				send_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, chunk_size)) {
				send_options.chunk_size = options->chunk_size;
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    send_options.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid file attributes");
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, compress)) {
				send_options.compress = options->compress != 0;
			}
		}
		return participant->SendFile(path, std::move(send_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to send file");
	});
}

lk_status_t lk_room_stream_text(lk_room_t* room, const lk_stream_text_options_t* options,
                                lk_text_stream_writer_t** writer) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || writer == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and writer output are required");
		}
		*writer = nullptr;
		core::StreamTextOptions converted;
		std::shared_ptr<CDataStreamCompletionState> completion;
		lk_data_stream_progress_callback progress_callback = nullptr;
		void* progress_user_data = nullptr;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid stream text options size");
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, topic) && options->topic) {
				converted.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 && !options->destination_identities) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				converted.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    converted.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid stream text attributes");
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, reply_to_stream_id) &&
			    options->reply_to_stream_id) {
				converted.reply_to_stream_id = options->reply_to_stream_id;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, attached_stream_id_count)) {
				if (options->attached_stream_id_count != 0 && !options->attached_stream_ids) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "attached stream IDs are null");
				}
				converted.attached_stream_ids = DestinationIdentities(
				    options->attached_stream_ids, options->attached_stream_id_count);
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, stream_id) && options->stream_id) {
				converted.stream_id = options->stream_id;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, has_total_size) &&
			    options->has_total_size) {
				if (!LKC_HAS_FIELD(options, lk_stream_text_options_t, total_size)) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "total size is missing");
				}
				converted.total_size = options->total_size;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, chunk_size)) {
				converted.chunk_size = options->chunk_size;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, update)) {
				converted.update = options->update != 0;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, version)) {
				converted.version = options->version;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, on_progress) &&
			    options->on_progress) {
				progress_callback = options->on_progress;
				progress_user_data =
				    LKC_HAS_FIELD(options, lk_stream_text_options_t, progress_user_data)
				        ? options->progress_user_data
				        : nullptr;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, compress)) {
				converted.compress = options->compress != 0;
			}
			if (LKC_HAS_FIELD(options, lk_stream_text_options_t, on_complete) &&
			    options->on_complete) {
				completion = std::make_shared<CDataStreamCompletionState>();
				completion->callback = options->on_complete;
				completion->user_data =
				    LKC_HAS_FIELD(options, lk_stream_text_options_t, completion_user_data)
				        ? options->completion_user_data
				        : nullptr;
				completion->total_size = converted.total_size;
			}
		}
		if (progress_callback != nullptr || completion) {
			converted.on_progress = [progress_callback, progress_user_data,
			                         completion](uint64_t sent, std::optional<uint64_t> total) {
				UpdateDataStreamProgress(completion, sent, total);
				if (progress_callback != nullptr) {
					progress_callback(progress_user_data, sent, total.has_value(),
					                  total.value_or(0));
				}
			};
		}
		auto core_writer = participant->StreamText(std::move(converted));
		if (!core_writer) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to open text stream");
		}
		auto handle = std::make_unique<lk_text_stream_writer>();
		handle->writer = std::move(core_writer);
		handle->completion = completion;
		if (completion) {
			std::lock_guard<std::mutex> guard(completion->mutex);
			completion->stream_id = handle->writer->Info().stream_id;
		}
		*writer = handle.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_text_stream_writer_write(lk_text_stream_writer_t* writer, const char* text,
                                        size_t text_size) {
	return Guard([&] {
		if (!writer || !writer->writer || (!text && text_size != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "writer and text are required");
		}
		const std::string value = text_size == 0 ? std::string{} : std::string(text, text_size);
		if (writer->writer->Write(value)) {
			return LK_STATUS_OK;
		}
		NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
		                           "failed to write text stream");
		return Failure(LK_STATUS_OPERATION_FAILED, "failed to write text stream");
	});
}

lk_status_t lk_text_stream_writer_close(lk_text_stream_writer_t* writer) {
	return Guard([&] {
		if (writer != nullptr && writer->writer != nullptr && writer->writer->Close()) {
			NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_COMPLETED, {});
			return LK_STATUS_OK;
		}
		if (writer != nullptr) {
			NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
			                           "text stream is already closed or incomplete",
			                           LK_STATUS_INVALID_STATE);
		}
		return Failure(LK_STATUS_INVALID_STATE, "text stream is already closed or incomplete");
	});
}

lk_status_t lk_text_stream_writer_cancel(lk_text_stream_writer_t* writer, const char* reason) {
	return Guard([&] {
		if (!writer || !writer->writer) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "writer is required");
		}
		const std::string cancellation_reason = reason ? reason : "cancelled";
		if (writer->writer->Cancel(cancellation_reason)) {
			NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_CANCELLED,
			                           cancellation_reason);
			return LK_STATUS_OK;
		}
		NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
		                           "failed to cancel text stream", LK_STATUS_INVALID_STATE);
		return Failure(LK_STATUS_INVALID_STATE, "text stream is already closed");
	});
}

size_t lk_text_stream_writer_id(const lk_text_stream_writer_t* writer, char* buffer,
                                size_t buffer_size) {
	return SizeGuard([&] {
		return writer && writer->writer
		           ? CopyString(writer->writer->Info().stream_id, buffer, buffer_size)
		           : InvalidSizeResult("text stream writer is required");
	});
}

int lk_text_stream_writer_is_closed(const lk_text_stream_writer_t* writer) {
	return writer && writer->writer && writer->writer->IsClosed();
}

void lk_text_stream_writer_destroy(lk_text_stream_writer_t* writer) {
	try {
		if (writer != nullptr && writer->writer != nullptr && !writer->writer->IsClosed()) {
			const std::string reason = "writer destroyed before close";
			if (writer->writer->Cancel(reason)) {
				NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_CANCELLED,
				                           reason);
			} else {
				NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
				                           "failed to cancel text stream");
			}
		}
		delete writer;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "exception while destroying text stream writer");
	}
}

lk_status_t lk_room_stream_bytes(lk_room_t* room, const lk_stream_bytes_options_t* options,
                                 lk_byte_stream_writer_t** writer) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || writer == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and writer output are required");
		}
		*writer = nullptr;
		core::StreamBytesOptions converted;
		std::shared_ptr<CDataStreamCompletionState> completion;
		lk_data_stream_progress_callback progress_callback = nullptr;
		void* progress_user_data = nullptr;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid stream byte options size");
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, topic) && options->topic) {
				converted.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, mime_type) &&
			    options->mime_type) {
				converted.mime_type = options->mime_type;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, name) && options->name) {
				converted.name = options->name;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 && !options->destination_identities) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				converted.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    converted.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid stream byte attributes");
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, stream_id) &&
			    options->stream_id) {
				converted.stream_id = options->stream_id;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, has_total_size) &&
			    options->has_total_size) {
				if (!LKC_HAS_FIELD(options, lk_stream_bytes_options_t, total_size)) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "total size is missing");
				}
				converted.total_size = options->total_size;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, chunk_size)) {
				converted.chunk_size = options->chunk_size;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, on_progress) &&
			    options->on_progress) {
				progress_callback = options->on_progress;
				progress_user_data =
				    LKC_HAS_FIELD(options, lk_stream_bytes_options_t, progress_user_data)
				        ? options->progress_user_data
				        : nullptr;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, compress)) {
				converted.compress = options->compress != 0;
			}
			if (LKC_HAS_FIELD(options, lk_stream_bytes_options_t, on_complete) &&
			    options->on_complete) {
				completion = std::make_shared<CDataStreamCompletionState>();
				completion->callback = options->on_complete;
				completion->user_data =
				    LKC_HAS_FIELD(options, lk_stream_bytes_options_t, completion_user_data)
				        ? options->completion_user_data
				        : nullptr;
				completion->total_size = converted.total_size;
			}
		}
		if (progress_callback != nullptr || completion) {
			converted.on_progress = [progress_callback, progress_user_data,
			                         completion](uint64_t sent, std::optional<uint64_t> total) {
				UpdateDataStreamProgress(completion, sent, total);
				if (progress_callback != nullptr) {
					progress_callback(progress_user_data, sent, total.has_value(),
					                  total.value_or(0));
				}
			};
		}
		auto core_writer = participant->StreamBytes(std::move(converted));
		if (!core_writer) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to open byte stream");
		}
		auto handle = std::make_unique<lk_byte_stream_writer>();
		handle->writer = std::move(core_writer);
		handle->completion = completion;
		if (completion) {
			std::lock_guard<std::mutex> guard(completion->mutex);
			completion->stream_id = handle->writer->Info().stream_id;
		}
		*writer = handle.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_byte_stream_writer_write(lk_byte_stream_writer_t* writer, const uint8_t* data,
                                        size_t data_size) {
	return Guard([&] {
		if (!writer || !writer->writer || (!data && data_size != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "writer and data are required");
		}
		std::vector<uint8_t> value;
		if (data_size != 0) {
			value.assign(data, data + data_size);
		}
		if (writer->writer->Write(value)) {
			return LK_STATUS_OK;
		}
		NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
		                           "failed to write byte stream");
		return Failure(LK_STATUS_OPERATION_FAILED, "failed to write byte stream");
	});
}

lk_status_t lk_byte_stream_writer_close(lk_byte_stream_writer_t* writer) {
	return Guard([&] {
		if (writer != nullptr && writer->writer != nullptr && writer->writer->Close()) {
			NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_COMPLETED, {});
			return LK_STATUS_OK;
		}
		if (writer != nullptr) {
			NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
			                           "byte stream is already closed or incomplete",
			                           LK_STATUS_INVALID_STATE);
		}
		return Failure(LK_STATUS_INVALID_STATE, "byte stream is already closed or incomplete");
	});
}

lk_status_t lk_byte_stream_writer_cancel(lk_byte_stream_writer_t* writer, const char* reason) {
	return Guard([&] {
		if (!writer || !writer->writer) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "writer is required");
		}
		const std::string cancellation_reason = reason ? reason : "cancelled";
		if (writer->writer->Cancel(cancellation_reason)) {
			NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_CANCELLED,
			                           cancellation_reason);
			return LK_STATUS_OK;
		}
		NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
		                           "failed to cancel byte stream", LK_STATUS_INVALID_STATE);
		return Failure(LK_STATUS_INVALID_STATE, "byte stream is already closed");
	});
}

size_t lk_byte_stream_writer_id(const lk_byte_stream_writer_t* writer, char* buffer,
                                size_t buffer_size) {
	return SizeGuard([&] {
		return writer && writer->writer
		           ? CopyString(writer->writer->Info().stream_id, buffer, buffer_size)
		           : InvalidSizeResult("byte stream writer is required");
	});
}

int lk_byte_stream_writer_is_closed(const lk_byte_stream_writer_t* writer) {
	return writer && writer->writer && writer->writer->IsClosed();
}

void lk_byte_stream_writer_destroy(lk_byte_stream_writer_t* writer) {
	try {
		if (writer != nullptr && writer->writer != nullptr && !writer->writer->IsClosed()) {
			const std::string reason = "writer destroyed before close";
			if (writer->writer->Cancel(reason)) {
				NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_CANCELLED,
				                           reason);
			} else {
				NotifyDataStreamCompletion(writer->completion, LK_DATA_STREAM_COMPLETION_FAILED,
				                           "failed to cancel byte stream");
			}
		}
		delete writer;
	} catch (...) {
		SetStatusError(LK_STATUS_EXCEPTION, "exception while destroying byte stream writer");
	}
}

lk_status_t lk_room_register_text_stream_handler(lk_room_t* room, const char* topic,
                                                 lk_text_stream_handler handler, void* user_data) {
	return Guard([&] {
		if (!room || !room->room || !topic || !handler) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, topic, and handler are required");
		}
		const bool registered = room->room->RegisterTextStreamHandler(
		    topic, [room, handler, user_data](const core::TextStreamEvent& event) {
			    std::lock_guard<std::mutex> guard(room->callback_lifetime_mutex);
			    const auto attributes = AttributeViews(event.info.attributes);
			    const auto attached_stream_ids = StringViews(event.info.attached_stream_ids);
			    const lk_text_stream_event_t converted{ToCDataStreamEventType(event.type),
			                                           event.info.stream_id.c_str(),
			                                           event.info.mime_type.c_str(),
			                                           event.info.topic.c_str(),
			                                           event.info.participant_identity.c_str(),
			                                           event.content.data(),
			                                           event.content.size(),
			                                           event.chunk_index,
			                                           event.info.total_size.has_value(),
			                                           event.info.total_size.value_or(0),
			                                           event.reason.c_str(),
			                                           attributes.data(),
			                                           attributes.size(),
			                                           event.info.timestamp,
			                                           event.info.reply_to_stream_id.c_str(),
			                                           attached_stream_ids.data(),
			                                           attached_stream_ids.size()};
			    handler(user_data, room, &converted);
		    });
		return registered
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_INVALID_STATE, "text stream topic is already registered");
	});
}

lk_status_t lk_room_unregister_text_stream_handler(lk_room_t* room, const char* topic) {
	return Guard([&] {
		if (!room || !room->room || !topic) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and topic are required");
		}
		return room->room->UnregisterTextStreamHandler(topic)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_INVALID_STATE, "text stream topic is not registered");
	});
}

lk_status_t lk_room_register_byte_stream_handler(lk_room_t* room, const char* topic,
                                                 lk_byte_stream_handler handler, void* user_data) {
	return Guard([&] {
		if (!room || !room->room || !topic || !handler) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, topic, and handler are required");
		}
		const bool registered = room->room->RegisterByteStreamHandler(
		    topic, [room, handler, user_data](const core::ByteStreamEvent& event) {
			    std::lock_guard<std::mutex> guard(room->callback_lifetime_mutex);
			    const auto attributes = AttributeViews(event.info.attributes);
			    const lk_byte_stream_event_t converted{ToCDataStreamEventType(event.type),
			                                           event.info.stream_id.c_str(),
			                                           event.info.name.c_str(),
			                                           event.info.mime_type.c_str(),
			                                           event.info.topic.c_str(),
			                                           event.info.participant_identity.c_str(),
			                                           event.content.data(),
			                                           event.content.size(),
			                                           event.chunk_index,
			                                           event.info.total_size.has_value(),
			                                           event.info.total_size.value_or(0),
			                                           event.reason.c_str(),
			                                           attributes.data(),
			                                           attributes.size(),
			                                           event.info.timestamp};
			    handler(user_data, room, &converted);
		    });
		return registered
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_INVALID_STATE, "byte stream topic is already registered");
	});
}

lk_status_t lk_room_unregister_byte_stream_handler(lk_room_t* room, const char* topic) {
	return Guard([&] {
		if (!room || !room->room || !topic) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and topic are required");
		}
		return room->room->UnregisterByteStreamHandler(topic)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_INVALID_STATE, "byte stream topic is not registered");
	});
}

lk_status_t lk_room_register_rpc_method(lk_room_t* room, const char* method, lk_rpc_handler handler,
                                        void* user_data) {
	return Guard([&] {
		if (room == nullptr || method == nullptr || *method == '\0' || handler == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, method, and handler are required");
		}
		const bool registered = room->room->RegisterRpcMethod(
		    method, [handler, user_data](const core::RpcInvocationData& invocation) {
			    const lk_rpc_invocation_t c_invocation{
			        invocation.request_id.c_str(), invocation.caller_identity.c_str(),
			        invocation.payload.c_str(),
			        static_cast<uint32_t>(invocation.response_timeout.count())};
			    const auto response = handler(user_data, &c_invocation);
			    if (response.error_code == 0) {
				    return core::RpcResult::Success(response.payload != nullptr ? response.payload
				                                                                : "");
			    }
			    auto error =
			        core::RpcError::BuiltIn(static_cast<core::RpcErrorCode>(response.error_code));
			    if (response.error_message != nullptr) {
				    error.message = response.error_message;
			    }
			    if (response.error_data != nullptr) {
				    error.data = response.error_data;
			    }
			    return core::RpcResult::Failure(std::move(error));
		    });
		return registered ? LK_STATUS_OK
		                  : Failure(LK_STATUS_INVALID_STATE, "RPC method is already registered");
	});
}

lk_status_t lk_room_unregister_rpc_method(lk_room_t* room, const char* method) {
	return Guard([&] {
		if (room == nullptr || method == nullptr || *method == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and method are required");
		}
		return room->room->UnregisterRpcMethod(method)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_INVALID_STATE, "RPC method is not registered");
	});
}

lk_status_t lk_room_perform_rpc(lk_room_t* room, const lk_rpc_perform_options_t* options,
                                lk_rpc_result_t** result) {
	return Guard([&] {
		if (result == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "RPC result output is null");
		}
		*result = nullptr;
		if (room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room is required");
		}

		core::PerformRpcParams params;
		const auto options_status = ToCorePerformRpcParams(options, params);
		if (options_status != LK_STATUS_OK) {
			return options_status;
		}
		auto* participant = LocalParticipant(room);
		if (participant == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "local participant is unavailable");
		}
		auto owned = std::make_unique<lk_rpc_result_t>();
		owned->result = participant->PerformRpc(params);
		*result = owned.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_perform_rpc_async(lk_room_t* room, const lk_rpc_perform_options_t* options,
                                      lk_rpc_completion_callback callback, void* user_data) {
	return Guard([&] {
		if (room == nullptr || callback == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room and RPC completion callback are required");
		}
		core::PerformRpcParams params;
		const auto options_status = ToCorePerformRpcParams(options, params);
		if (options_status != LK_STATUS_OK) {
			return options_status;
		}
		auto* participant = LocalParticipant(room);
		if (participant == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "local participant is unavailable");
		}

		auto task = std::make_shared<AsyncRpcTask>();
		auto* task_pointer = task.get();
		auto state = room->state;
		task->worker =
		    std::jthread([task_pointer, state = std::move(state), participant,
		                  params = std::move(params), callback, user_data, room]() mutable {
			    lk_rpc_result_t result;
			    result.result = participant->PerformRpc(params);
			    if (state->alive.load()) {
				    try {
					    callback(user_data, room, &result);
				    } catch (...) {
					    // Exceptions must never escape a C ABI callback boundary.
				    }
			    }
			    task_pointer->completed.store(true);
		    });
		{
			std::lock_guard<std::mutex> guard(room->async_tasks_mutex);
			std::erase_if(room->async_rpc_tasks,
			              [](const auto& existing) { return existing->completed.load(); });
			room->async_rpc_tasks.push_back(std::move(task));
		}
		return LK_STATUS_OK;
	});
}

lk_status_t lk_rpc_result_clone(const lk_rpc_result_t* result, lk_rpc_result_t** cloned_result) {
	return Guard([&] {
		if (cloned_result != nullptr) {
			*cloned_result = nullptr;
		}
		if (result == nullptr || cloned_result == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "RPC result and cloned result output are required");
		}
		auto clone = std::make_unique<lk_rpc_result_t>();
		clone->result = result->result;
		*cloned_result = clone.release();
		return LK_STATUS_OK;
	});
}

void lk_rpc_result_destroy(lk_rpc_result_t* result) { delete result; }

int lk_rpc_result_ok(const lk_rpc_result_t* result) {
	return result != nullptr && result->result.Ok() ? 1 : 0;
}

size_t lk_rpc_result_payload(const lk_rpc_result_t* result, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return result != nullptr ? CopyString(result->result.payload, buffer, buffer_size)
		                         : InvalidSizeResult("RPC result is required");
	});
}

uint32_t lk_rpc_result_error_code(const lk_rpc_result_t* result) {
	return result != nullptr && result->result.error
	           ? static_cast<uint32_t>(result->result.error->code)
	           : 0;
}

size_t lk_rpc_result_error_message(const lk_rpc_result_t* result, char* buffer,
                                   size_t buffer_size) {
	return SizeGuard([&] {
		if (result == nullptr) {
			return InvalidSizeResult("RPC result is required");
		}
		return CopyString(result->result.error ? result->result.error->message : std::string{},
		                  buffer, buffer_size);
	});
}

size_t lk_rpc_result_error_data(const lk_rpc_result_t* result, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		if (result == nullptr) {
			return InvalidSizeResult("RPC result is required");
		}
		return CopyString(result->result.error ? result->result.error->data : std::string{}, buffer,
		                  buffer_size);
	});
}

} // extern "C"
