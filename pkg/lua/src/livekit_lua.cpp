#include <livekit/capi/livekit.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr const char* kRoomType = "livekit.room";
constexpr size_t kMaxQueuedEvents = 1024;

struct Event {
	std::string type;
	std::string identity;
	std::string name;
	std::string sid;
	std::string topic;
	std::string data;
	int value = 0;
	bool reliable = false;
	std::vector<std::pair<std::string, std::string>> strings;
	std::vector<std::pair<std::string, lua_Number>> numbers;
	std::vector<std::pair<std::string, bool>> booleans;
	std::vector<std::pair<std::string, std::string>> attributes;
	std::vector<std::string> speakers;
};

enum class CaptureKind { None, Microphone, SystemAudio, Camera, Screen };
enum class CaptureAction { Start, Stop, Switch };

struct CaptureConfig {
	CaptureKind kind = CaptureKind::None;
	std::string label;
	std::string source_id;
	uint32_t queue_ms = 200;
	uint32_t width = 1280;
	uint32_t height = 720;
	uint32_t fps = 30;
	bool echo_cancellation = true;
	bool auto_gain_control = true;
	bool noise_suppression = true;
	bool include_cursor = true;
};

enum class AsyncOperation {
	Connect,
	Disconnect,
	PublishData,
	Rpc,
	Chat,
	Text,
	Bytes,
	File,
	PublishAudioTrack,
	PublishVideoTrack,
	UnpublishLocalTrack,
	SetRemoteTrackSubscribed,
	PublishCaptureTrack,
	CaptureControl,
	OpenStreamWriter,
	WriteStreamWriter,
	CloseStreamWriter,
	CancelStreamWriter,
	StoreDataTrackSchema,
	GetDataTrackSchema,
	PublishDataTrack,
	SubscribeDataTrack,
	WaitAudioSource
};

struct SchemaIdConfig {
	std::string name;
	std::string custom_encoding;
	lk_data_track_schema_encoding_t encoding = LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED;

	lk_data_track_schema_id_t value() const {
		lk_data_track_schema_id_t result;
		lk_data_track_schema_id_init(&result);
		result.name = name.c_str();
		result.encoding = encoding;
		result.custom_encoding = custom_encoding.c_str();
		return result;
	}
};

struct DataTrackPublishConfig {
	std::string name;
	std::string custom_frame_encoding;
	SchemaIdConfig schema;
	bool has_frame_encoding = false;
	lk_data_track_frame_encoding_t frame_encoding = LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED;
	bool has_schema = false;
};

struct StreamWriterConfig {
	bool text = true;
	std::string topic;
	std::string mime_type;
	std::string name;
	std::string stream_id;
	std::string reply_to_stream_id;
	std::vector<std::string> destinations;
	std::vector<std::string> attached_stream_ids;
	std::map<std::string, std::string> attributes;
	uint64_t total_size = 0;
	bool has_total_size = false;
	size_t chunk_size = 0;
	bool compress = false;
	bool update = false;
	int32_t version = 0;
};

struct ConnectConfig {
	struct IceServer {
		std::vector<std::string> urls;
		std::string username;
		std::string password;
	};
	lk_room_connect_options_t options{};
	lk_e2ee_options_t e2ee{};
	bool has_e2ee = false;
	std::string shared_key;
	std::string ratchet_salt;
	std::string unencrypted_magic_bytes;
	std::vector<IceServer> ice_servers;
	std::vector<std::vector<const char*>> ice_urls;
	std::vector<lk_ice_server_t> native_ice_servers;

	void prepare() {
		if (has_e2ee) {
			e2ee.shared_key =
			    shared_key.empty() ? nullptr : reinterpret_cast<const uint8_t*>(shared_key.data());
			e2ee.shared_key_size = shared_key.size();
			e2ee.ratchet_salt = ratchet_salt.empty()
			                        ? nullptr
			                        : reinterpret_cast<const uint8_t*>(ratchet_salt.data());
			e2ee.ratchet_salt_size = ratchet_salt.size();
			e2ee.unencrypted_magic_bytes =
			    unencrypted_magic_bytes.empty()
			        ? nullptr
			        : reinterpret_cast<const uint8_t*>(unencrypted_magic_bytes.data());
			e2ee.unencrypted_magic_bytes_size = unencrypted_magic_bytes.size();
			options.e2ee_options = &e2ee;
		}
		ice_urls.clear();
		native_ice_servers.clear();
		ice_urls.resize(ice_servers.size());
		native_ice_servers.reserve(ice_servers.size());
		for (size_t i = 0; i < ice_servers.size(); ++i) {
			for (const auto& url : ice_servers[i].urls)
				ice_urls[i].push_back(url.c_str());
			lk_ice_server_t server{};
			server.struct_size = sizeof(server);
			server.urls = ice_urls[i].data();
			server.url_count = ice_urls[i].size();
			server.username = ice_servers[i].username.c_str();
			server.password = ice_servers[i].password.c_str();
			native_ice_servers.push_back(server);
		}
		options.ice_servers = native_ice_servers.data();
		options.ice_server_count = native_ice_servers.size();
	}
};

struct AsyncTask {
	uint64_t id = 0;
	AsyncOperation operation = AsyncOperation::Connect;
	std::string first;
	std::string second;
	std::string topic;
	std::string mime_type;
	std::string name;
	std::string chat_id;
	int64_t chat_timestamp = 0;
	uint64_t media_id = 0;
	uint32_t media_sample_rate = 48000;
	uint32_t media_channels = 1;
	uint32_t media_queue_ms = 200;
	uint32_t media_width = 0;
	uint32_t media_height = 0;
	bool media_screen = false;
	CaptureConfig capture;
	CaptureAction capture_action = CaptureAction::Start;
	StreamWriterConfig stream_config;
	SchemaIdConfig schema_id;
	DataTrackPublishConfig data_track_publish;
	lk_data_track_subscription_options_t data_track_subscription{};
	ConnectConfig connect;
	bool reliable = true;
	bool done = false;
	lk_status_t status = LK_STATUS_INVALID_STATE;
	std::string error;
	uint32_t timeout_ms = 0;
	bool rpc_ok = false;
	uint32_t rpc_error_code = 0;
	std::string rpc_payload;
	std::string rpc_error_message;
	std::string rpc_error_data;
};

struct RpcRequest {
	std::string method;
	std::string request_id;
	std::string caller_identity;
	std::string payload;
	uint32_t timeout_ms = 0;
	std::string response_payload;
	std::string error_message;
	std::string error_data;
	uint32_t error_code = 0;
	std::atomic_bool done{false};
};

struct Room;
struct RpcHandlerContext {
	Room* room = nullptr;
	std::string method;
};

struct LocalMediaTrack {
	lk_local_track_t* track = nullptr;
	lk_audio_source_t* audio = nullptr;
	lk_video_source_t* video = nullptr;
	uint32_t sample_rate = 0;
	uint32_t channels = 0;
	CaptureKind capture_kind = CaptureKind::None;
};

struct RemoteMediaStream {
	lk_audio_stream_t* audio = nullptr;
	lk_video_stream_t* video = nullptr;
};

struct StreamWriter {
	lk_text_stream_writer_t* text = nullptr;
	lk_byte_stream_writer_t* bytes = nullptr;
};

struct DataTrackHandles {
	std::map<uint64_t, lk_local_data_track_t*> local;
	std::map<uint64_t, lk_data_track_reader_t*> readers;
};

struct Room {
	lk_room_t* native = nullptr;
	int callback_ref = LUA_NOREF;
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<Event> events;
	std::deque<std::shared_ptr<RpcRequest>> rpc_pending;
	std::map<std::string, int> rpc_handlers;
	std::vector<std::unique_ptr<RpcHandlerContext>> rpc_contexts;
	std::map<uint64_t, LocalMediaTrack> local_tracks;
	std::map<uint64_t, RemoteMediaStream> remote_streams;
	std::map<uint64_t, StreamWriter> stream_writers;
	DataTrackHandles data_tracks;
	uint64_t next_media_id = 1;
	size_t dropped = 0;
	std::thread worker;
	std::deque<std::shared_ptr<AsyncTask>> pending;
	std::map<uint64_t, std::shared_ptr<AsyncTask>> tasks;
	uint64_t next_task_id = 1;
	size_t completed_tasks = 0;
	bool running = false;
	bool stopping = false;
};

bool publish_audio_native(Room* room, const char* label, uint32_t sample_rate, uint32_t channels,
                          uint32_t queue_ms, uint64_t& id, std::string& error);
bool publish_video_native(Room* room, const char* label, const lk_video_frame_input_t& first_frame,
                          bool screen, uint64_t& id, std::string& error);
bool unpublish_local_native(Room* room, uint64_t id, std::string& error);
bool publish_capture_native(Room* room, const CaptureConfig& config, uint64_t& id,
                            std::string& error);
lk_status_t capture_control_native(Room* room, uint64_t id, CaptureAction action,
                                   const char* source_id, std::string& error);
lk_status_t open_stream_writer_native(Room* room, const StreamWriterConfig& config, uint64_t& id);
lk_status_t stream_writer_operation_native(Room* room, AsyncOperation operation, uint64_t id,
                                           const std::string& data, std::string& error);
lk_data_track_error_code_t
publish_data_track_native(Room* room, const DataTrackPublishConfig& config, uint64_t& id);
lk_data_track_error_code_t
subscribe_data_track_native(Room* room, const char* identity, const char* sid,
                            const lk_data_track_subscription_options_t& options, uint64_t& id);
lk_status_t send_stream_one_shot_native(Room* room, AsyncOperation operation,
                                        const std::string& payload,
                                        const StreamWriterConfig& config);
const char* prepare_video_frame(lk_video_frame_input_t& frame, const char* pixels, size_t bytes,
                                lua_Integer width, lua_Integer height, const char* format,
                                int64_t timestamp_us);
int64_t current_timestamp_us();

const char* safe(const char* value) { return value != nullptr ? value : ""; }

Room* check_room(lua_State* L, int index) {
	return static_cast<Room*>(luaL_checkudata(L, index, kRoomType));
}

int status_result(lua_State* L, lk_status_t status) {
	if (status == LK_STATUS_OK) {
		lua_pushboolean(L, 1);
		return 1;
	}
	lua_pushnil(L);
	lua_pushstring(L, safe(lk_last_error()));
	return 2;
}
int media_error(lua_State* L, const char* message);
StreamWriterConfig read_stream_writer_config(lua_State* L, bool text, int index);

void string_field(lua_State* L, const char* key, const std::string& value);
void integer_field(lua_State* L, const char* key, lua_Integer value);
void number_field(lua_State* L, const char* key, lua_Number value);
void boolean_field(lua_State* L, const char* key, bool value);

std::string rpc_string(const lk_rpc_result_t* result,
                       size_t (*getter)(const lk_rpc_result_t*, char*, size_t)) {
	const size_t required = getter(result, nullptr, 0);
	if (required == 0)
		return {};
	std::string value(required, '\0');
	getter(result, value.data(), value.size());
	value.resize(required - 1);
	return value;
}

lk_status_t open_stream_writer_native(Room* room, const StreamWriterConfig& config, uint64_t& id) {
	std::vector<const char*> destinations;
	for (const auto& identity : config.destinations)
		destinations.push_back(identity.c_str());
	std::vector<const char*> attached;
	for (const auto& stream_id : config.attached_stream_ids)
		attached.push_back(stream_id.c_str());
	std::vector<lk_attribute_t> attributes;
	for (const auto& [key, value] : config.attributes)
		attributes.push_back({key.c_str(), value.c_str()});
	StreamWriter writer;
	lk_status_t status;
	if (config.text) {
		lk_stream_text_options_t options;
		lk_stream_text_options_init(&options);
		options.topic = config.topic.c_str();
		options.destination_identities = destinations.data();
		options.destination_identity_count = destinations.size();
		options.attributes = attributes.data();
		options.attribute_count = attributes.size();
		options.reply_to_stream_id = config.reply_to_stream_id.c_str();
		options.attached_stream_ids = attached.data();
		options.attached_stream_id_count = attached.size();
		options.stream_id = config.stream_id.empty() ? nullptr : config.stream_id.c_str();
		options.has_total_size = config.has_total_size;
		options.total_size = config.total_size;
		if (config.chunk_size != 0)
			options.chunk_size = config.chunk_size;
		options.update = config.update;
		options.version = config.version;
		options.compress = config.compress;
		status = lk_room_stream_text(room->native, &options, &writer.text);
	} else {
		lk_stream_bytes_options_t options;
		lk_stream_bytes_options_init(&options);
		options.topic = config.topic.c_str();
		options.mime_type = config.mime_type.c_str();
		options.name = config.name.c_str();
		options.destination_identities = destinations.data();
		options.destination_identity_count = destinations.size();
		options.attributes = attributes.data();
		options.attribute_count = attributes.size();
		options.stream_id = config.stream_id.empty() ? nullptr : config.stream_id.c_str();
		options.has_total_size = config.has_total_size;
		options.total_size = config.total_size;
		if (config.chunk_size != 0)
			options.chunk_size = config.chunk_size;
		options.compress = config.compress;
		status = lk_room_stream_bytes(room->native, &options, &writer.bytes);
	}
	if (status == LK_STATUS_OK) {
		id = room->next_media_id++;
		room->stream_writers.emplace(id, writer);
	}
	return status;
}

lk_status_t stream_writer_operation_native(Room* room, AsyncOperation operation, uint64_t id,
                                           const std::string& data, std::string& error) {
	auto found = room->stream_writers.find(id);
	if (found == room->stream_writers.end()) {
		error = "stream writer is unavailable";
		return LK_STATUS_INVALID_ARGUMENT;
	}
	auto& writer = found->second;
	if (writer.text != nullptr) {
		if (operation == AsyncOperation::WriteStreamWriter)
			return lk_text_stream_writer_write(writer.text, data.data(), data.size());
		if (operation == AsyncOperation::CloseStreamWriter)
			return lk_text_stream_writer_close(writer.text);
		return lk_text_stream_writer_cancel(writer.text, data.c_str());
	}
	if (operation == AsyncOperation::WriteStreamWriter)
		return lk_byte_stream_writer_write(
		    writer.bytes, reinterpret_cast<const uint8_t*>(data.data()), data.size());
	if (operation == AsyncOperation::CloseStreamWriter)
		return lk_byte_stream_writer_close(writer.bytes);
	return lk_byte_stream_writer_cancel(writer.bytes, data.c_str());
}

lk_data_track_error_code_t
publish_data_track_native(Room* room, const DataTrackPublishConfig& config, uint64_t& id) {
	lk_data_track_publish_options_t options;
	lk_data_track_publish_options_init(&options);
	options.name = config.name.c_str();
	options.has_frame_encoding = config.has_frame_encoding;
	options.frame_encoding = config.frame_encoding;
	options.custom_frame_encoding = config.custom_frame_encoding.c_str();
	options.has_schema = config.has_schema;
	if (config.has_schema)
		options.schema = config.schema.value();
	lk_local_data_track_t* track = nullptr;
	const auto code = lk_room_publish_data_track(room->native, &options, &track);
	if (code == LK_DATA_TRACK_ERROR_NONE) {
		id = room->next_media_id++;
		room->data_tracks.local.emplace(id, track);
	}
	return code;
}

lk_data_track_error_code_t
subscribe_data_track_native(Room* room, const char* identity, const char* sid,
                            const lk_data_track_subscription_options_t& options, uint64_t& id) {
	lk_data_track_reader_t* reader = nullptr;
	const auto code = lk_room_subscribe_data_track(room->native, identity, sid, &options, &reader);
	if (code == LK_DATA_TRACK_ERROR_NONE) {
		id = room->next_media_id++;
		room->data_tracks.readers.emplace(id, reader);
	}
	return code;
}

lk_status_t send_stream_one_shot_native(Room* room, AsyncOperation operation,
                                        const std::string& payload,
                                        const StreamWriterConfig& config) {
	std::vector<const char*> destinations;
	for (const auto& identity : config.destinations)
		destinations.push_back(identity.c_str());
	std::vector<const char*> attached;
	for (const auto& id : config.attached_stream_ids)
		attached.push_back(id.c_str());
	std::vector<lk_attribute_t> attributes;
	for (const auto& [key, value] : config.attributes)
		attributes.push_back({key.c_str(), value.c_str()});
	if (operation == AsyncOperation::Text) {
		lk_text_send_options_t options;
		lk_text_send_options_init(&options);
		options.topic = config.topic.c_str();
		options.destination_identities = destinations.data();
		options.destination_identity_count = destinations.size();
		options.attributes = attributes.data();
		options.attribute_count = attributes.size();
		options.reply_to_stream_id = config.reply_to_stream_id.c_str();
		options.attached_stream_ids = attached.data();
		options.attached_stream_id_count = attached.size();
		if (config.chunk_size != 0)
			options.chunk_size = config.chunk_size;
		options.compress = config.compress;
		return lk_room_send_text(room->native, payload.c_str(), &options);
	}
	if (operation == AsyncOperation::Bytes) {
		lk_byte_send_options_t options;
		lk_byte_send_options_init(&options);
		options.topic = config.topic.c_str();
		options.mime_type =
		    config.mime_type.empty() ? "application/octet-stream" : config.mime_type.c_str();
		options.name = config.name.c_str();
		options.destination_identities = destinations.data();
		options.destination_identity_count = destinations.size();
		options.attributes = attributes.data();
		options.attribute_count = attributes.size();
		if (config.chunk_size != 0)
			options.chunk_size = config.chunk_size;
		options.compress = config.compress;
		return lk_room_send_bytes(room->native, reinterpret_cast<const uint8_t*>(payload.data()),
		                          payload.size(), &options);
	}
	lk_file_send_options_t options;
	lk_file_send_options_init(&options);
	options.topic = config.topic.c_str();
	options.mime_type =
	    config.mime_type.empty() ? "application/octet-stream" : config.mime_type.c_str();
	options.destination_identities = destinations.data();
	options.destination_identity_count = destinations.size();
	options.attributes = attributes.data();
	options.attribute_count = attributes.size();
	if (config.chunk_size != 0)
		options.chunk_size = config.chunk_size;
	options.compress = config.compress;
	return lk_room_send_file(room->native, payload.c_str(), &options);
}

bool enqueue(Room* room, Event event) noexcept {
	try {
		std::lock_guard<std::mutex> lock(room->mutex);
		if (room->stopping)
			return false;
		if (room->events.size() == kMaxQueuedEvents) {
			room->events.pop_front();
			++room->dropped;
		}
		room->events.push_back(std::move(event));
		room->wake.notify_all();
		return true;
	} catch (...) {
		return false;
	}
}

lk_rpc_handler_result_t on_rpc_invocation(void* user_data,
                                          const lk_rpc_invocation_t* invocation) noexcept {
	thread_local std::string response_payload;
	thread_local std::string error_message;
	thread_local std::string error_data;
	try {
		auto* context = static_cast<RpcHandlerContext*>(user_data);
		if (context == nullptr || invocation == nullptr)
			return {nullptr, LK_RPC_ERROR_APPLICATION_ERROR, "invalid Lua RPC invocation", nullptr};
		Room* room = context->room;
		auto request = std::make_shared<RpcRequest>();
		request->method = context->method;
		request->request_id = safe(invocation->request_id);
		request->caller_identity = safe(invocation->caller_identity);
		request->payload = safe(invocation->payload);
		request->timeout_ms = invocation->response_timeout_ms;
		{
			std::unique_lock<std::mutex> lock(room->mutex);
			if (room->stopping)
				return {nullptr, LK_RPC_ERROR_RECIPIENT_DISCONNECTED, "room is closing", nullptr};
			if (room->rpc_pending.size() >= kMaxQueuedEvents)
				return {nullptr, LK_RPC_ERROR_APPLICATION_ERROR, "Lua RPC queue is full", nullptr};
			room->rpc_pending.push_back(request);
			room->wake.notify_all();
			const auto timeout =
			    std::chrono::milliseconds(request->timeout_ms == 0 ? 15000 : request->timeout_ms);
			if (!room->wake.wait_for(lock, timeout,
			                         [&] { return request->done || room->stopping; })) {
				request->done = true;
				return {nullptr, LK_RPC_ERROR_RESPONSE_TIMEOUT, "Lua RPC handler timed out",
				        nullptr};
			}
			if (room->stopping)
				return {nullptr, LK_RPC_ERROR_RECIPIENT_DISCONNECTED, "room is closing", nullptr};
			response_payload = request->response_payload;
			error_message = request->error_message;
			error_data = request->error_data;
			return {response_payload.c_str(), request->error_code,
			        error_message.empty() ? nullptr : error_message.c_str(),
			        error_data.empty() ? nullptr : error_data.c_str()};
		}
	} catch (...) {
		return {nullptr, LK_RPC_ERROR_APPLICATION_ERROR, "Lua RPC handler failed", nullptr};
	}
}

void worker_loop(Room* room) noexcept {
	for (;;) {
		std::shared_ptr<AsyncTask> task;
		{
			std::unique_lock<std::mutex> lock(room->mutex);
			room->wake.wait(lock, [&] { return room->stopping || !room->pending.empty(); });
			if (room->stopping)
				return;
			task = std::move(room->pending.front());
			room->pending.pop_front();
			room->running = true;
		}
		lk_status_t status = LK_STATUS_EXCEPTION;
		std::string error;
		try {
			switch (task->operation) {
			case AsyncOperation::Connect:
				task->connect.prepare();
				status = lk_room_connect_with_options(room->native, task->first.c_str(),
				                                      task->second.c_str(), &task->connect.options);
				break;
			case AsyncOperation::Disconnect:
				status = lk_room_disconnect(room->native);
				break;
			case AsyncOperation::PublishData: {
				lk_data_publish_options_t options;
				lk_data_publish_options_init(&options);
				options.reliable = task->reliable ? 1 : 0;
				options.topic = task->stream_config.topic.empty() ? task->topic.c_str()
				                                                  : task->stream_config.topic.c_str();
				std::vector<const char*> destinations;
				for (const auto& identity : task->stream_config.destinations)
					destinations.push_back(identity.c_str());
				options.destination_identities = destinations.data();
				options.destination_identity_count = destinations.size();
				status = lk_room_publish_data(room->native,
				                              reinterpret_cast<const uint8_t*>(task->first.data()),
				                              task->first.size(), &options);
				break;
			}
			case AsyncOperation::Rpc: {
				lk_rpc_perform_options_t options;
				lk_rpc_perform_options_init(&options);
				options.destination_identity = task->first.c_str();
				options.method = task->second.c_str();
				options.payload = task->topic.c_str();
				if (task->timeout_ms != 0)
					options.response_timeout_ms = task->timeout_ms;
				lk_rpc_result_t* raw = nullptr;
				status = lk_room_perform_rpc(room->native, &options, &raw);
				std::unique_ptr<lk_rpc_result_t, decltype(&lk_rpc_result_destroy)> result(
				    raw, lk_rpc_result_destroy);
				if (status == LK_STATUS_OK && raw != nullptr) {
					task->rpc_ok = lk_rpc_result_ok(raw) != 0;
					task->rpc_error_code = lk_rpc_result_error_code(raw);
					task->rpc_payload = rpc_string(raw, lk_rpc_result_payload);
					task->rpc_error_message = rpc_string(raw, lk_rpc_result_error_message);
					task->rpc_error_data = rpc_string(raw, lk_rpc_result_error_data);
				}
				if (status == LK_STATUS_OK && raw == nullptr) {
					status = LK_STATUS_OPERATION_FAILED;
					error = "RPC returned no result";
				}
				break;
			}
			case AsyncOperation::Chat: {
				char id[LK_CHAT_MESSAGE_ID_BUFFER_SIZE]{};
				status = lk_room_send_chat_message(room->native, task->first.c_str(), id,
				                                   sizeof(id), &task->chat_timestamp);
				if (status == LK_STATUS_OK)
					task->chat_id = id;
				break;
			}
			case AsyncOperation::Text:
			case AsyncOperation::Bytes:
			case AsyncOperation::File: {
				auto config = task->stream_config;
				if (config.topic.empty())
					config.topic = task->topic;
				if (config.mime_type.empty())
					config.mime_type = task->mime_type;
				if (config.name.empty())
					config.name = task->name;
				status = send_stream_one_shot_native(room, task->operation, task->first, config);
				break;
			}
			case AsyncOperation::PublishAudioTrack:
				status = publish_audio_native(room, task->second.c_str(), task->media_sample_rate,
				                              task->media_channels, task->media_queue_ms,
				                              task->media_id, error)
				             ? LK_STATUS_OK
				             : LK_STATUS_OPERATION_FAILED;
				break;
			case AsyncOperation::PublishVideoTrack: {
				lk_video_frame_input_t frame;
				const char* validation = prepare_video_frame(
				    frame, task->first.data(), task->first.size(), task->media_width,
				    task->media_height, task->topic.c_str(), current_timestamp_us());
				if (validation != nullptr) {
					status = LK_STATUS_INVALID_ARGUMENT;
					error = validation;
				} else {
					status = publish_video_native(room, task->second.c_str(), frame,
					                              task->media_screen, task->media_id, error)
					             ? LK_STATUS_OK
					             : LK_STATUS_OPERATION_FAILED;
				}
				break;
			}
			case AsyncOperation::UnpublishLocalTrack:
				status = unpublish_local_native(room, task->media_id, error)
				             ? LK_STATUS_OK
				             : LK_STATUS_OPERATION_FAILED;
				break;
			case AsyncOperation::SetRemoteTrackSubscribed:
				status = lk_room_set_remote_track_subscribed(room->native, task->first.c_str(),
				                                             task->second.c_str(), task->reliable);
				break;
			case AsyncOperation::PublishCaptureTrack:
				status = publish_capture_native(room, task->capture, task->media_id, error)
				             ? LK_STATUS_OK
				             : LK_STATUS_OPERATION_FAILED;
				break;
			case AsyncOperation::CaptureControl:
				status = capture_control_native(room, task->media_id, task->capture_action,
				                                task->first.c_str(), error);
				break;
			case AsyncOperation::OpenStreamWriter:
				status = open_stream_writer_native(room, task->stream_config, task->media_id);
				break;
			case AsyncOperation::WriteStreamWriter:
			case AsyncOperation::CloseStreamWriter:
			case AsyncOperation::CancelStreamWriter:
				status = stream_writer_operation_native(room, task->operation, task->media_id,
				                                        task->first, error);
				break;
			case AsyncOperation::StoreDataTrackSchema: {
				const auto schema = task->schema_id.value();
				const auto code = lk_room_store_data_track_schema(
				    room->native, &schema, reinterpret_cast<const uint8_t*>(task->first.data()),
				    task->first.size());
				status =
				    code == LK_DATA_TRACK_ERROR_NONE ? LK_STATUS_OK : LK_STATUS_OPERATION_FAILED;
				break;
			}
			case AsyncOperation::GetDataTrackSchema: {
				const auto schema = task->schema_id.value();
				lk_data_track_schema_t* raw = nullptr;
				const auto code =
				    lk_room_get_data_track_schema(room->native, task->first.c_str(), &schema, &raw);
				status =
				    code == LK_DATA_TRACK_ERROR_NONE ? LK_STATUS_OK : LK_STATUS_OPERATION_FAILED;
				std::unique_ptr<lk_data_track_schema_t, decltype(&lk_data_track_schema_destroy)>
				    result(raw, lk_data_track_schema_destroy);
				if (status == LK_STATUS_OK) {
					auto copy_string = [raw](auto getter) {
						const size_t size = getter(raw, nullptr, 0);
						std::string value(size, '\0');
						if (size != 0) {
							getter(raw, value.data(), value.size());
							value.resize(size - 1);
						}
						return value;
					};
					task->second = copy_string(lk_data_track_schema_name);
					task->mime_type = copy_string(lk_data_track_schema_custom_encoding);
					task->media_channels = lk_data_track_schema_encoding(raw);
					const size_t size = lk_data_track_schema_definition(raw, nullptr, 0);
					task->name.resize(size);
					if (size != 0)
						lk_data_track_schema_definition(
						    raw, reinterpret_cast<uint8_t*>(task->name.data()), size);
				}
				break;
			}
			case AsyncOperation::PublishDataTrack: {
				const auto code =
				    publish_data_track_native(room, task->data_track_publish, task->media_id);
				status =
				    code == LK_DATA_TRACK_ERROR_NONE ? LK_STATUS_OK : LK_STATUS_OPERATION_FAILED;
				break;
			}
			case AsyncOperation::SubscribeDataTrack: {
				const auto code =
				    subscribe_data_track_native(room, task->first.c_str(), task->second.c_str(),
				                                task->data_track_subscription, task->media_id);
				status =
				    code == LK_DATA_TRACK_ERROR_NONE ? LK_STATUS_OK : LK_STATUS_OPERATION_FAILED;
				break;
			}
			case AsyncOperation::WaitAudioSource: {
				const auto found = room->local_tracks.find(task->media_id);
				if (found == room->local_tracks.end() || found->second.audio == nullptr) {
					status = LK_STATUS_INVALID_ARGUMENT;
					error = "audio track is unavailable";
				} else {
					status =
					    lk_audio_source_wait_for_playout(found->second.audio, task->timeout_ms);
				}
				break;
			}
			}
			if (status != LK_STATUS_OK && error.empty())
				error = safe(lk_last_error());
		} catch (...) {
			status = LK_STATUS_EXCEPTION;
			try {
				error = "unexpected error in Lua worker";
			} catch (...) {
			}
		}
		{
			std::lock_guard<std::mutex> lock(room->mutex);
			task->status = status;
			task->error.swap(error);
			task->done = true;
			room->running = false;
			++room->completed_tasks;
			room->wake.notify_all();
		}
	}
}

int start_task(lua_State* L, Room* room, std::shared_ptr<AsyncTask> task) {
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	uint64_t id = 0;
	try {
		std::lock_guard<std::mutex> lock(room->mutex);
		if (room->stopping) {
			lua_pushnil(L);
			lua_pushliteral(L, "room is closing");
			return 2;
		}
		if (!room->worker.joinable())
			room->worker = std::thread(worker_loop, room);
		id = room->next_task_id;
		task->id = id;
		room->tasks.emplace(id, task);
		try {
			room->pending.push_back(std::move(task));
		} catch (...) {
			room->tasks.erase(id);
			throw;
		}
		++room->next_task_id;
		room->wake.notify_all();
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to start asynchronous operation");
		return 2;
	}
	lua_pushnumber(L, static_cast<lua_Number>(id));
	return 1;
}

void room_event(void* user_data, const char* type, int value = 0) noexcept {
	try {
		Event event{type};
		event.value = value;
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}

void on_connected(void* user_data, lk_room_t*) { room_event(user_data, "connected"); }
void on_reconnecting(void* user_data, lk_room_t*) { room_event(user_data, "reconnecting"); }
void on_reconnected(void* user_data, lk_room_t*) { room_event(user_data, "reconnected"); }
void on_disconnected(void* user_data, lk_room_t*, lk_disconnect_reason_t reason) {
	room_event(user_data, "disconnected", static_cast<int>(reason));
}
void on_state(void* user_data, lk_room_t*, lk_room_state_t state) {
	room_event(user_data, "connection_state_changed", static_cast<int>(state));
}
void on_token_refreshed(void* user_data, lk_room_t*) { room_event(user_data, "token_refreshed"); }
void on_room_eos(void* user_data, lk_room_t*) { room_event(user_data, "room_eos"); }
void snapshot_event(void* user_data, const lk_room_snapshot_t* snapshot,
                    const char* type) noexcept {
	try {
		Event event{type};
		if (snapshot != nullptr) {
			event.sid = safe(snapshot->sid);
			event.name = safe(snapshot->name);
			event.strings.emplace_back("metadata", safe(snapshot->metadata));
			event.booleans.emplace_back("is_recording", snapshot->is_recording != 0);
		}
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_room_updated(void* user_data, lk_room_t*, const lk_room_snapshot_t* snapshot) {
	snapshot_event(user_data, snapshot, "room_updated");
}
void on_room_moved(void* user_data, lk_room_t*, const lk_room_snapshot_t* snapshot) {
	snapshot_event(user_data, snapshot, "room_moved");
}
void on_participants_updated(void* user_data, lk_room_t*, const lk_participant_info_t* participants,
                             size_t count) {
	try {
		Event event{"participants_updated"};
		for (size_t i = 0; participants != nullptr && i < count; ++i)
			event.speakers.emplace_back(safe(participants[i].identity));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void participant_event(void* user_data, const lk_participant_info_t* participant,
                       const char* type) noexcept {
	try {
		Event event{type};
		if (participant != nullptr) {
			event.identity = safe(participant->identity);
			event.name = safe(participant->name);
			event.sid = safe(participant->sid);
		}
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_participant_connected(void* user_data, lk_room_t*,
                              const lk_participant_info_t* participant) {
	participant_event(user_data, participant, "participant_connected");
}
void on_participant_disconnected(void* user_data, lk_room_t*,
                                 const lk_participant_info_t* participant) {
	participant_event(user_data, participant, "participant_disconnected");
}
void on_data(void* user_data, lk_room_t*, const lk_data_received_t* received) {
	try {
		if (received == nullptr)
			return;
		Event event{"data_received"};
		event.identity = safe(received->participant_identity);
		event.topic = safe(received->topic);
		if (received->data != nullptr && received->data_size != 0)
			event.data.assign(reinterpret_cast<const char*>(received->data), received->data_size);
		event.reliable = received->reliable != 0;
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_sip_dtmf(void* user_data, lk_room_t*, const lk_sip_dtmf_t* received) {
	try {
		if (received == nullptr)
			return;
		Event event{"sip_dtmf_received"};
		event.identity = safe(received->participant_identity);
		event.strings.emplace_back("digit", safe(received->digit));
		event.numbers.emplace_back("code", received->code);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_data_channel_buffer_status(void* user_data, lk_room_t*,
                                   const lk_data_channel_buffer_status_t* status) {
	try {
		if (status == nullptr)
			return;
		Event event{"data_channel_buffer_status_changed"};
		event.booleans.emplace_back("reliable", status->reliable != 0);
		event.booleans.emplace_back("backpressured", status->backpressured != 0);
		event.numbers.emplace_back("buffered_amount",
		                           static_cast<lua_Number>(status->buffered_amount));
		event.numbers.emplace_back("high_water_mark",
		                           static_cast<lua_Number>(status->high_water_mark));
		event.numbers.emplace_back("low_water_mark",
		                           static_cast<lua_Number>(status->low_water_mark));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}

void on_chat(void* user_data, lk_room_t*, const lk_chat_message_t* message) {
	try {
		if (message == nullptr)
			return;
		Event event{"chat_message_received"};
		event.strings.emplace_back("id", safe(message->id));
		event.strings.emplace_back("message", safe(message->message));
		event.strings.emplace_back("participant_identity", safe(message->participant_identity));
		event.numbers.emplace_back("timestamp", static_cast<lua_Number>(message->timestamp));
		if (message->has_edit_timestamp)
			event.numbers.emplace_back("edit_timestamp",
			                           static_cast<lua_Number>(message->edit_timestamp));
		event.booleans.emplace_back("deleted", message->deleted != 0);
		event.booleans.emplace_back("generated", message->generated != 0);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}

void on_text(void* user_data, lk_room_t*, const lk_text_received_t* received) {
	try {
		if (received == nullptr)
			return;
		Event event{"text_received"};
		event.strings.emplace_back("stream_id", safe(received->stream_id));
		event.strings.emplace_back("text", safe(received->text));
		event.strings.emplace_back("topic", safe(received->topic));
		event.strings.emplace_back("participant_identity", safe(received->participant_identity));
		event.strings.emplace_back("reply_to_stream_id", safe(received->reply_to_stream_id));
		event.numbers.emplace_back("timestamp", static_cast<lua_Number>(received->timestamp));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}

void file_event(void* user_data, const lk_file_received_t* received, const char* type) noexcept {
	try {
		if (received == nullptr)
			return;
		Event event{type};
		if (received->data != nullptr && received->data_size != 0)
			event.strings.emplace_back(
			    "data",
			    std::string(reinterpret_cast<const char*>(received->data), received->data_size));
		else
			event.strings.emplace_back("data", "");
		event.strings.emplace_back("stream_id", safe(received->stream_id));
		event.strings.emplace_back("name", safe(received->name));
		event.strings.emplace_back("mime_type", safe(received->mime_type));
		event.strings.emplace_back("topic", safe(received->topic));
		event.strings.emplace_back("participant_identity", safe(received->participant_identity));
		event.numbers.emplace_back("timestamp", static_cast<lua_Number>(received->timestamp));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_file(void* user_data, lk_room_t*, const lk_file_received_t* received) {
	file_event(user_data, received, "file_received");
}
void on_byte(void* user_data, lk_room_t*, const lk_file_received_t* received) {
	file_event(user_data, received, "byte_received");
}
void on_text_stream_event(void* user_data, lk_room_t*, const lk_text_stream_event_t* stream) {
	try {
		if (stream == nullptr)
			return;
		Event event{"text_stream_event"};
		event.numbers.emplace_back("phase", stream->type);
		event.strings.emplace_back("stream_id", safe(stream->stream_id));
		event.strings.emplace_back("mime_type", safe(stream->mime_type));
		event.strings.emplace_back("topic", safe(stream->topic));
		event.strings.emplace_back("participant_identity", safe(stream->participant_identity));
		event.strings.emplace_back("reason", safe(stream->reason));
		event.strings.emplace_back("reply_to_stream_id", safe(stream->reply_to_stream_id));
		event.strings.emplace_back("content",
		                           stream->content != nullptr
		                               ? std::string(stream->content, stream->content_size)
		                               : std::string());
		event.numbers.emplace_back("chunk_index", static_cast<lua_Number>(stream->chunk_index));
		event.numbers.emplace_back("timestamp", static_cast<lua_Number>(stream->timestamp));
		event.booleans.emplace_back("has_total_size", stream->has_total_size != 0);
		if (stream->has_total_size)
			event.numbers.emplace_back("total_size", static_cast<lua_Number>(stream->total_size));
		for (size_t i = 0; stream->attributes != nullptr && i < stream->attribute_count; ++i)
			event.attributes.emplace_back(safe(stream->attributes[i].key),
			                              safe(stream->attributes[i].value));
		for (size_t i = 0;
		     stream->attached_stream_ids != nullptr && i < stream->attached_stream_id_count; ++i)
			event.speakers.emplace_back(safe(stream->attached_stream_ids[i]));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_byte_stream_event(void* user_data, lk_room_t*, const lk_byte_stream_event_t* stream) {
	try {
		if (stream == nullptr)
			return;
		Event event{"byte_stream_event"};
		event.numbers.emplace_back("phase", stream->type);
		event.strings.emplace_back("stream_id", safe(stream->stream_id));
		event.strings.emplace_back("name", safe(stream->name));
		event.strings.emplace_back("mime_type", safe(stream->mime_type));
		event.strings.emplace_back("topic", safe(stream->topic));
		event.strings.emplace_back("participant_identity", safe(stream->participant_identity));
		event.strings.emplace_back("reason", safe(stream->reason));
		event.strings.emplace_back(
		    "content",
		    stream->content != nullptr
		        ? std::string(reinterpret_cast<const char*>(stream->content), stream->content_size)
		        : std::string());
		event.numbers.emplace_back("chunk_index", static_cast<lua_Number>(stream->chunk_index));
		event.numbers.emplace_back("timestamp", static_cast<lua_Number>(stream->timestamp));
		event.booleans.emplace_back("has_total_size", stream->has_total_size != 0);
		if (stream->has_total_size)
			event.numbers.emplace_back("total_size", static_cast<lua_Number>(stream->total_size));
		for (size_t i = 0; stream->attributes != nullptr && i < stream->attribute_count; ++i)
			event.attributes.emplace_back(safe(stream->attributes[i].key),
			                              safe(stream->attributes[i].value));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_room_metadata(void* user_data, lk_room_t*, const char* metadata) {
	try {
		Event event{"room_metadata_changed"};
		event.strings.emplace_back("metadata", safe(metadata));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_recording_status(void* user_data, lk_room_t*, int recording) {
	try {
		Event event{"recording_status_changed"};
		event.booleans.emplace_back("recording", recording != 0);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_room_sid_changed(void* user_data, lk_room_t*, const char* previous_sid, const char* sid) {
	try {
		Event event{"room_sid_changed"};
		event.strings.emplace_back("previous_sid", safe(previous_sid));
		event.sid = safe(sid);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_connection_quality(void* user_data, lk_room_t*, lk_connection_quality_t quality,
                           const lk_participant_info_t* participant) {
	try {
		Event event{"connection_quality_changed"};
		event.identity = participant ? safe(participant->identity) : "";
		event.numbers.emplace_back("quality", quality);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_active_speakers(void* user_data, lk_room_t*, const lk_participant_info_t* participants,
                        size_t count) {
	try {
		Event event{"active_speakers_changed"};
		for (size_t i = 0; participants != nullptr && i < count; ++i)
			event.speakers.emplace_back(safe(participants[i].identity));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_encryption_state(void* user_data, lk_room_t*, const lk_encryption_state_t* state) {
	try {
		if (state == nullptr)
			return;
		Event event{"encryption_state_changed"};
		event.strings.emplace_back("track_sid", safe(state->track_id));
		event.strings.emplace_back("participant_identity", safe(state->participant_identity));
		event.numbers.emplace_back("kind", state->kind);
		event.numbers.emplace_back("direction", state->direction);
		event.numbers.emplace_back("key_index", static_cast<lua_Number>(state->key_index));
		event.numbers.emplace_back("state", state->state);
		event.booleans.emplace_back("enabled", state->enabled != 0);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_participant_metadata(void* user_data, lk_room_t*, const char* previous,
                             const lk_participant_info_t* participant) {
	try {
		Event event{"participant_metadata_changed"};
		event.identity = participant ? safe(participant->identity) : "";
		event.strings.emplace_back("metadata", participant ? safe(participant->metadata) : "");
		event.strings.emplace_back("previous_metadata", safe(previous));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_participant_name(void* user_data, lk_room_t*, const char* name,
                         const lk_participant_info_t* participant) {
	try {
		Event event{"participant_name_changed"};
		event.identity = participant ? safe(participant->identity) : "";
		event.name = safe(name);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_participant_attributes(void* user_data, lk_room_t*, const lk_attribute_t* changes,
                               size_t count, const lk_participant_info_t* participant) {
	try {
		Event event{"participant_attributes_changed"};
		event.identity = participant ? safe(participant->identity) : "";
		for (size_t i = 0; changes != nullptr && i < count; ++i)
			event.attributes.emplace_back(safe(changes[i].key), safe(changes[i].value));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void track_event(void* user_data, const lk_track_publication_info_t* track,
                 const lk_participant_info_t* participant, const char* type) noexcept {
	try {
		Event event{type};
		if (track != nullptr) {
			event.strings.emplace_back("sid", safe(track->sid));
			event.strings.emplace_back("name", safe(track->name));
			event.strings.emplace_back("mime_type", safe(track->mime_type));
			event.numbers.emplace_back("kind", track->kind);
			event.numbers.emplace_back("source", track->source);
			event.booleans.emplace_back("is_muted", track->is_muted != 0);
		}
		if (participant != nullptr) {
			event.strings.emplace_back("participant_identity", safe(participant->identity));
			event.strings.emplace_back("participant_sid", safe(participant->sid));
		}
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_track_published(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                        const lk_participant_info_t* p) {
	track_event(u, t, p, "track_published");
}
void on_track_unpublished(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                          const lk_participant_info_t* p) {
	track_event(u, t, p, "track_unpublished");
}
void on_track_subscribed(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                         const lk_participant_info_t* p) {
	track_event(u, t, p, "track_subscribed");
}
void on_track_unsubscribed(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                           const lk_participant_info_t* p) {
	track_event(u, t, p, "track_unsubscribed");
}
void on_track_muted(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                    const lk_participant_info_t* p) {
	track_event(u, t, p, "track_muted");
}
void on_track_unmuted(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                      const lk_participant_info_t* p) {
	track_event(u, t, p, "track_unmuted");
}
void subscription_event(void* user_data, const lk_track_publication_info_t* track,
                        const lk_participant_info_t* participant, const char* type,
                        const char* field, int value) noexcept {
	try {
		Event event{type};
		if (track != nullptr) {
			event.sid = safe(track->sid);
			event.name = safe(track->name);
		}
		if (participant != nullptr)
			event.identity = safe(participant->identity);
		event.numbers.emplace_back(field, value);
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_subscription_permission(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                                const lk_participant_info_t* p, int allowed) {
	try {
		Event event{"track_subscription_permission_changed"};
		if (t != nullptr)
			event.sid = safe(t->sid);
		if (p != nullptr)
			event.identity = safe(p->identity);
		event.booleans.emplace_back("allowed", allowed != 0);
		enqueue(static_cast<Room*>(u), std::move(event));
	} catch (...) {
	}
}
void on_subscription_failed(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                            const lk_participant_info_t* p, lk_subscription_error_t error) {
	subscription_event(u, t, p, "track_subscription_failed", "error", error);
}
void on_stream_state(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                     const lk_participant_info_t* p, lk_track_stream_state_t state) {
	subscription_event(u, t, p, "track_stream_state_changed", "state", state);
}
void on_subscription_status(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                            const lk_participant_info_t* p, lk_track_subscription_status_t status) {
	subscription_event(u, t, p, "track_subscription_status_changed", "status", status);
}
void on_local_track_published(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                              const lk_participant_info_t* p) {
	track_event(u, t, p, "local_track_published");
}
void on_local_track_unpublished(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                                const lk_participant_info_t* p) {
	track_event(u, t, p, "local_track_unpublished");
}
void data_track_event(void* user_data, const lk_data_track_info_t* track,
                      const lk_participant_info_t* participant, const char* type) noexcept {
	try {
		if (track == nullptr)
			return;
		Event event{type};
		event.identity = participant != nullptr ? safe(participant->identity) : "";
		event.sid = safe(track->sid);
		event.name = safe(track->name);
		event.numbers.emplace_back("publisher_handle", track->publisher_handle);
		event.booleans.emplace_back("uses_e2ee", track->uses_e2ee != 0);
		if (track->has_frame_encoding)
			event.numbers.emplace_back("frame_encoding", track->frame_encoding);
		event.strings.emplace_back("custom_frame_encoding", safe(track->custom_frame_encoding));
		if (track->has_schema) {
			event.strings.emplace_back("schema_name", safe(track->schema_name));
			event.numbers.emplace_back("schema_encoding", track->schema_encoding);
			event.strings.emplace_back("custom_schema_encoding",
			                           safe(track->custom_schema_encoding));
		}
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}
void on_data_track_published(void* u, lk_room_t*, const lk_data_track_info_t* t,
                             const lk_participant_info_t* p) {
	data_track_event(u, t, p, "data_track_published");
}
void on_data_track_unpublished(void* u, lk_room_t*, const lk_data_track_info_t* t,
                               const lk_participant_info_t* p) {
	data_track_event(u, t, p, "data_track_unpublished");
}
void on_local_data_track_published(void* u, lk_room_t*, const lk_data_track_info_t* t,
                                   const lk_participant_info_t* p) {
	data_track_event(u, t, p, "local_data_track_published");
}
void on_local_data_track_unpublished(void* u, lk_room_t*, const lk_data_track_info_t* t,
                                     const lk_participant_info_t* p) {
	data_track_event(u, t, p, "local_data_track_unpublished");
}
void on_data_track_frame(void* user_data, lk_room_t*, const lk_data_track_info_t* track,
                         const lk_participant_info_t* participant,
                         const lk_data_track_frame_view_t* frame) {
	try {
		if (frame == nullptr)
			return;
		Event event{"data_track_frame"};
		event.identity = participant != nullptr ? safe(participant->identity) : "";
		if (track != nullptr) {
			event.sid = safe(track->sid);
			event.name = safe(track->name);
		}
		if (frame->data != nullptr && frame->data_size != 0)
			event.strings.emplace_back(
			    "data", std::string(reinterpret_cast<const char*>(frame->data), frame->data_size));
		else
			event.strings.emplace_back("data", "");
		if (frame->has_user_timestamp)
			event.numbers.emplace_back("user_timestamp",
			                           static_cast<lua_Number>(frame->user_timestamp));
		enqueue(static_cast<Room*>(user_data), std::move(event));
	} catch (...) {
	}
}

bool destroy_local_media(LocalMediaTrack& media) noexcept {
	if (media.track != nullptr) {
		if (lk_local_track_destroy(media.track) != LK_STATUS_OK)
			return false;
		media.track = nullptr;
	}
	if (media.audio != nullptr) {
		if (lk_audio_source_destroy(media.audio) != LK_STATUS_OK)
			return false;
		media.audio = nullptr;
	}
	if (media.video != nullptr) {
		if (lk_video_source_destroy(media.video) != LK_STATUS_OK)
			return false;
		media.video = nullptr;
	}
	return true;
}
void destroy_remote_stream(RemoteMediaStream& stream) noexcept {
	if (stream.audio != nullptr) {
		lk_audio_stream_destroy(stream.audio);
		stream.audio = nullptr;
	}
	if (stream.video != nullptr) {
		lk_video_stream_destroy(stream.video);
		stream.video = nullptr;
	}
}

int close_room(lua_State* L) {
	Room* room = check_room(L, 1);
	if (room->native != nullptr) {
		{
			std::lock_guard<std::mutex> lock(room->mutex);
			room->stopping = true;
			for (auto& task : room->pending) {
				task->status = LK_STATUS_INVALID_STATE;
				task->done = true;
				++room->completed_tasks;
			}
			room->pending.clear();
			room->rpc_pending.clear();
			room->wake.notify_all();
		}
		if (room->worker.joinable())
			room->worker.join();
		for (auto& [id, stream] : room->remote_streams)
			destroy_remote_stream(stream);
		room->remote_streams.clear();
		for (auto& [id, writer] : room->stream_writers) {
			if (writer.text != nullptr)
				lk_text_stream_writer_destroy(writer.text);
			if (writer.bytes != nullptr)
				lk_byte_stream_writer_destroy(writer.bytes);
		}
		room->stream_writers.clear();
		for (auto& [id, reader] : room->data_tracks.readers)
			lk_data_track_reader_destroy(reader);
		room->data_tracks.readers.clear();
		for (auto& [id, track] : room->data_tracks.local) {
			if (lk_local_data_track_is_published(track))
				lk_local_data_track_unpublish(track);
			lk_local_data_track_destroy(track);
		}
		room->data_tracks.local.clear();
		if (lk_room_is_connected(room->native)) {
			for (auto& [id, media] : room->local_tracks)
				lk_local_track_unpublish(media.track, 1);
		}
		for (auto& [id, media] : room->local_tracks)
			destroy_local_media(media);
		room->local_tracks.clear();
		lk_room_destroy(room->native);
		room->native = nullptr;
	}
	if (room->callback_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, room->callback_ref);
		room->callback_ref = LUA_NOREF;
	}
	for (const auto& [method, ref] : room->rpc_handlers)
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
	room->rpc_handlers.clear();
	room->rpc_contexts.clear();
	{
		std::lock_guard<std::mutex> lock(room->mutex);
		room->events.clear();
		room->wake.notify_all();
	}
	lua_pushboolean(L, 1);
	return 1;
}

int gc_room(lua_State* L) {
	close_room(L);
	check_room(L, 1)->~Room();
	return 0;
}

int new_room(lua_State* L) {
	void* storage = lua_newuserdata(L, sizeof(Room));
	Room* room = new (storage) Room();
	luaL_getmetatable(L, kRoomType);
	lua_setmetatable(L, -2);
	lk_status_t status = lk_room_create(&room->native);
	if (status != LK_STATUS_OK) {
		status_result(L, status);
		lua_remove(L, -3);
		return 2;
	}
	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	callbacks.user_data = room;
	callbacks.on_connected = on_connected;
	callbacks.on_reconnecting = on_reconnecting;
	callbacks.on_reconnected = on_reconnected;
	callbacks.on_disconnected_with_reason = on_disconnected;
	callbacks.on_connection_state_changed = on_state;
	callbacks.on_token_refreshed = on_token_refreshed;
	callbacks.on_room_updated = on_room_updated;
	callbacks.on_room_moved = on_room_moved;
	callbacks.on_room_eos = on_room_eos;
	callbacks.on_participants_updated = on_participants_updated;
	callbacks.on_participant_connected = on_participant_connected;
	callbacks.on_participant_disconnected = on_participant_disconnected;
	callbacks.on_data_received = on_data;
	callbacks.on_sip_dtmf_received = on_sip_dtmf;
	callbacks.on_data_channel_buffer_status_changed = on_data_channel_buffer_status;
	callbacks.on_chat_message_received = on_chat;
	callbacks.on_text_received = on_text;
	callbacks.on_file_received = on_file;
	callbacks.on_byte_received = on_byte;
	callbacks.on_room_metadata_changed = on_room_metadata;
	callbacks.on_recording_status_changed = on_recording_status;
	callbacks.on_room_sid_changed = on_room_sid_changed;
	callbacks.on_connection_quality_changed = on_connection_quality;
	callbacks.on_active_speakers_changed = on_active_speakers;
	callbacks.on_encryption_state_changed = on_encryption_state;
	callbacks.on_participant_metadata_changed = on_participant_metadata;
	callbacks.on_participant_name_changed = on_participant_name;
	callbacks.on_participant_attributes_changed = on_participant_attributes;
	callbacks.on_track_published = on_track_published;
	callbacks.on_track_unpublished = on_track_unpublished;
	callbacks.on_track_subscribed = on_track_subscribed;
	callbacks.on_track_unsubscribed = on_track_unsubscribed;
	callbacks.on_track_muted = on_track_muted;
	callbacks.on_track_unmuted = on_track_unmuted;
	callbacks.on_track_subscription_permission_changed = on_subscription_permission;
	callbacks.on_track_subscription_failed = on_subscription_failed;
	callbacks.on_track_stream_state_changed = on_stream_state;
	callbacks.on_track_subscription_status_changed = on_subscription_status;
	callbacks.on_local_track_published = on_local_track_published;
	callbacks.on_local_track_unpublished = on_local_track_unpublished;
	callbacks.on_data_track_published = on_data_track_published;
	callbacks.on_data_track_unpublished = on_data_track_unpublished;
	callbacks.on_local_data_track_published = on_local_data_track_published;
	callbacks.on_local_data_track_unpublished = on_local_data_track_unpublished;
	callbacks.on_data_track_frame = on_data_track_frame;
	status = lk_room_set_callbacks(room->native, &callbacks);
	if (status != LK_STATUS_OK) {
		status_result(L, status);
		lua_remove(L, -3);
		return 2;
	}
	return 1;
}

int on(lua_State* L) {
	Room* room = check_room(L, 1);
	if (!lua_isnoneornil(L, 2))
		luaL_checktype(L, 2, LUA_TFUNCTION);
	if (room->callback_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, room->callback_ref);
	room->callback_ref = LUA_NOREF;
	if (!lua_isnoneornil(L, 2)) {
		lua_pushvalue(L, 2);
		room->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	lua_pushboolean(L, 1);
	return 1;
}

int register_rpc_method(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* method = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TFUNCTION);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (room->rpc_handlers.count(method) != 0) {
		lua_pushnil(L);
		lua_pushliteral(L, "RPC method is already registered");
		return 2;
	}
	try {
		auto context = std::make_unique<RpcHandlerContext>();
		context->room = room;
		context->method = method;
		room->rpc_contexts.push_back(std::move(context));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate RPC method");
		return 2;
	}
	const auto status = lk_room_register_rpc_method(room->native, method, on_rpc_invocation,
	                                                room->rpc_contexts.back().get());
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_pushvalue(L, 3);
	const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	try {
		room->rpc_handlers.emplace(method, ref);
	} catch (...) {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
		lk_room_unregister_rpc_method(room->native, method);
		lua_pushnil(L);
		lua_pushliteral(L, "failed to store RPC method");
		return 2;
	}
	lua_pushboolean(L, 1);
	return 1;
}
int unregister_rpc_method(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* method = luaL_checkstring(L, 2);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	const auto status = lk_room_unregister_rpc_method(room->native, method);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	const auto found = room->rpc_handlers.find(method);
	if (found != room->rpc_handlers.end()) {
		luaL_unref(L, LUA_REGISTRYINDEX, found->second);
		room->rpc_handlers.erase(found);
	}
	lua_pushboolean(L, 1);
	return 1;
}

void field(lua_State* L, const char* key, const std::string& value) {
	lua_pushlstring(L, value.data(), value.size());
	lua_setfield(L, -2, key);
}

void dispatch_rpc(lua_State* L, Room* room, const std::shared_ptr<RpcRequest>& request) {
	std::string payload;
	std::string message;
	std::string data;
	uint32_t error_code = 0;
	const auto handler = room->rpc_handlers.find(request->method);
	if (handler == room->rpc_handlers.end()) {
		error_code = LK_RPC_ERROR_UNSUPPORTED_METHOD;
		message = "RPC method is not registered";
	} else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, handler->second);
		lua_newtable(L);
		field(L, "request_id", request->request_id);
		field(L, "caller_identity", request->caller_identity);
		field(L, "payload", request->payload);
		lua_pushnumber(L, request->timeout_ms);
		lua_setfield(L, -2, "response_timeout_ms");
		if (lua_pcall(L, 1, 1, 0) != 0) {
			error_code = LK_RPC_ERROR_APPLICATION_ERROR;
			message = safe(lua_tostring(L, -1));
		} else if (lua_isstring(L, -1)) {
			payload = lua_tostring(L, -1);
		} else if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "payload");
			if (lua_isstring(L, -1))
				payload = lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, -1, "error_code");
			if (lua_isnumber(L, -1)) {
				const lua_Number value = lua_tonumber(L, -1);
				if (value >= 0 && value <= UINT32_MAX)
					error_code = static_cast<uint32_t>(value);
			}
			lua_pop(L, 1);
			lua_getfield(L, -1, "error_message");
			if (lua_isstring(L, -1))
				message = lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, -1, "error_data");
			if (lua_isstring(L, -1))
				data = lua_tostring(L, -1);
			lua_pop(L, 1);
		} else if (!lua_isnil(L, -1)) {
			error_code = LK_RPC_ERROR_APPLICATION_ERROR;
			message = "Lua RPC handler must return a string or result table";
		}
		lua_pop(L, 1);
	}
	std::lock_guard<std::mutex> lock(room->mutex);
	if (!request->done) {
		request->response_payload = std::move(payload);
		request->error_message = std::move(message);
		request->error_data = std::move(data);
		request->error_code = error_code;
		request->done = true;
		room->wake.notify_all();
	}
}

int poll(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_Integer requested = luaL_optinteger(L, 2, 100);
	if (requested < 0)
		return luaL_argerror(L, 2, "must be nonnegative");
	const int limit = static_cast<int>(requested > 1024 ? 1024 : requested);
	int delivered = 0;
	while (delivered < limit) {
		std::shared_ptr<RpcRequest> request;
		Event event;
		{
			std::lock_guard<std::mutex> lock(room->mutex);
			if (!room->rpc_pending.empty()) {
				request = std::move(room->rpc_pending.front());
				room->rpc_pending.pop_front();
			} else if (!room->events.empty()) {
				event = std::move(room->events.front());
				room->events.pop_front();
			} else {
				break;
			}
		}
		if (request) {
			if (!request->done) {
				try {
					dispatch_rpc(L, room, request);
				} catch (...) {
					std::lock_guard<std::mutex> lock(room->mutex);
					request->error_code = LK_RPC_ERROR_APPLICATION_ERROR;
					request->error_message = "Lua RPC handler failed";
					request->done = true;
					room->wake.notify_all();
				}
				++delivered;
			}
			continue;
		}
		if (room->callback_ref != LUA_NOREF) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, room->callback_ref);
			lua_newtable(L);
			field(L, "type", event.type);
			if (!event.identity.empty())
				field(L, "identity", event.identity);
			if (!event.name.empty())
				field(L, "name", event.name);
			if (!event.sid.empty())
				field(L, "sid", event.sid);
			if (event.type == "data_received") {
				field(L, "topic", event.topic);
				field(L, "data", event.data);
				lua_pushboolean(L, event.reliable);
				lua_setfield(L, -2, "reliable");
			}
			if (event.type == "disconnected" || event.type == "connection_state_changed") {
				lua_pushinteger(L, event.value);
				lua_setfield(L, -2, event.type == "disconnected" ? "reason" : "state");
			}
			for (const auto& [key, value] : event.strings)
				string_field(L, key.c_str(), value);
			for (const auto& [key, value] : event.numbers)
				number_field(L, key.c_str(), value);
			for (const auto& [key, value] : event.booleans)
				boolean_field(L, key.c_str(), value);
			if (event.type == "participant_attributes_changed" ||
			    event.type == "text_stream_event" || event.type == "byte_stream_event") {
				lua_newtable(L);
				for (const auto& [key, value] : event.attributes)
					string_field(L, key.c_str(), value);
				lua_setfield(L, -2,
				             event.type == "participant_attributes_changed" ? "changes"
				                                                            : "attributes");
			}
			if (event.type == "active_speakers_changed" || event.type == "participants_updated") {
				lua_newtable(L);
				for (size_t i = 0; i < event.speakers.size(); ++i) {
					lua_pushlstring(L, event.speakers[i].data(), event.speakers[i].size());
					lua_rawseti(L, -2, static_cast<int>(i + 1));
				}
				lua_setfield(L, -2, "identities");
			}
			if (event.type == "text_stream_event") {
				lua_newtable(L);
				for (size_t i = 0; i < event.speakers.size(); ++i) {
					lua_pushlstring(L, event.speakers[i].data(), event.speakers[i].size());
					lua_rawseti(L, -2, static_cast<int>(i + 1));
				}
				lua_setfield(L, -2, "attached_stream_ids");
			}
			if (lua_pcall(L, 1, 0, 0) != 0) {
				lua_pushnil(L);
				lua_insert(L, -2);
				return 2;
			}
		}
		++delivered;
	}
	lua_pushinteger(L, delivered);
	return 1;
}

void read_connect_options(lua_State* L, ConnectConfig& config) {
	lk_room_connect_options_init(&config.options);
	if (lua_istable(L, 4)) {
		lua_getfield(L, 4, "auto_subscribe");
		if (!lua_isnil(L, -1))
			config.options.auto_subscribe = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 4, "adaptive_stream");
		if (!lua_isnil(L, -1))
			config.options.adaptive_stream = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 4, "dynacast");
		if (!lua_isnil(L, -1))
			config.options.dynacast = lua_toboolean(L, -1);
		lua_pop(L, 1);
		for (const auto* key : {"join_retries", "reconnect_timeout_ms"}) {
			lua_getfield(L, 4, key);
			if (!lua_isnil(L, -1)) {
				const lua_Integer value = luaL_checkinteger(L, -1);
				luaL_argcheck(L, value > 0 && value <= UINT32_MAX, 4,
				              "connection retry or timeout is out of range");
				if (std::strcmp(key, "join_retries") == 0)
					config.options.join_retries = static_cast<uint32_t>(value);
				else
					config.options.reconnect_timeout_ms = static_cast<uint32_t>(value);
			}
			lua_pop(L, 1);
		}
		lua_getfield(L, 4, "continual_gathering_policy");
		if (!lua_isnil(L, -1)) {
			const lua_Integer value = luaL_checkinteger(L, -1);
			luaL_argcheck(L,
			              value >= LK_CONTINUAL_GATHERING_POLICY_GATHER_ONCE &&
			                  value <= LK_CONTINUAL_GATHERING_POLICY_GATHER_CONTINUALLY,
			              4, "invalid continual gathering policy");
			config.options.continual_gathering_policy =
			    static_cast<lk_continual_gathering_policy_t>(value);
		}
		lua_pop(L, 1);
		lua_getfield(L, 4, "ice_transport_type");
		if (!lua_isnil(L, -1)) {
			const lua_Integer value = luaL_checkinteger(L, -1);
			luaL_argcheck(L,
			              value >= LK_ICE_TRANSPORT_TYPE_NONE && value <= LK_ICE_TRANSPORT_TYPE_ALL,
			              4, "invalid ICE transport type");
			config.options.ice_transport_type = static_cast<lk_ice_transport_type_t>(value);
		}
		lua_pop(L, 1);
		lua_getfield(L, 4, "ice_servers");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
#if LUA_VERSION_NUM < 502
			const size_t server_count = lua_objlen(L, -1);
#else
			const size_t server_count = lua_rawlen(L, -1);
#endif
			for (size_t i = 1; i <= server_count; ++i) {
				lua_rawgeti(L, -1, static_cast<int>(i));
				luaL_checktype(L, -1, LUA_TTABLE);
				ConnectConfig::IceServer server;
				lua_getfield(L, -1, "urls");
				luaL_checktype(L, -1, LUA_TTABLE);
#if LUA_VERSION_NUM < 502
				const size_t url_count = lua_objlen(L, -1);
#else
				const size_t url_count = lua_rawlen(L, -1);
#endif
				for (size_t j = 1; j <= url_count; ++j) {
					lua_rawgeti(L, -1, static_cast<int>(j));
					server.urls.emplace_back(luaL_checkstring(L, -1));
					lua_pop(L, 1);
				}
				lua_pop(L, 1);
				lua_getfield(L, -1, "username");
				if (!lua_isnil(L, -1))
					server.username = luaL_checkstring(L, -1);
				lua_pop(L, 1);
				lua_getfield(L, -1, "password");
				if (!lua_isnil(L, -1))
					server.password = luaL_checkstring(L, -1);
				lua_pop(L, 1);
				config.ice_servers.push_back(std::move(server));
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
		lua_getfield(L, 4, "e2ee");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			config.has_e2ee = true;
			lk_e2ee_options_init(&config.e2ee);
			const int index = lua_gettop(L);
			lua_getfield(L, index, "enabled");
			if (!lua_isnil(L, -1))
				config.e2ee.enabled = lua_toboolean(L, -1);
			lua_pop(L, 1);
			auto bytes = [&](const char* key, std::string& output) {
				lua_getfield(L, index, key);
				if (!lua_isnil(L, -1)) {
					size_t size = 0;
					const char* data = luaL_checklstring(L, -1, &size);
					output.assign(data, size);
				}
				lua_pop(L, 1);
			};
			bytes("shared_key", config.shared_key);
			bytes("ratchet_salt", config.ratchet_salt);
			bytes("unencrypted_magic_bytes", config.unencrypted_magic_bytes);
			auto number = [&](const char* key, lua_Integer maximum, auto& output) {
				lua_getfield(L, index, key);
				if (!lua_isnil(L, -1)) {
					const lua_Integer value = luaL_checkinteger(L, -1);
					luaL_argcheck(L, value >= 0 && value <= maximum, 4,
					              "E2EE option is out of range");
					output = static_cast<std::remove_reference_t<decltype(output)>>(value);
				}
				lua_pop(L, 1);
			};
			number("ratchet_window_size", 1000000, config.e2ee.ratchet_window_size);
			number("failure_tolerance", 1000000, config.e2ee.failure_tolerance);
			number("key_ring_size", 1000000, config.e2ee.key_ring_size);
			lua_getfield(L, index, "key_derivation");
			if (!lua_isnil(L, -1)) {
				const lua_Integer value = luaL_checkinteger(L, -1);
				luaL_argcheck(L,
				              value >= LK_E2EE_KEY_DERIVATION_PBKDF2_SHA256 &&
				                  value <= LK_E2EE_KEY_DERIVATION_HKDF_SHA256,
				              4, "invalid E2EE key derivation");
				config.e2ee.key_derivation = static_cast<lk_e2ee_key_derivation_t>(value);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
}

bool async_busy(Room* room) {
	std::lock_guard<std::mutex> lock(room->mutex);
	return room->running || !room->pending.empty();
}

int busy_result(lua_State* L) {
	lua_pushnil(L);
	lua_pushliteral(L, "asynchronous operation in progress");
	return 2;
}

int connect_room(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* url = luaL_checkstring(L, 2);
	const char* token = luaL_checkstring(L, 3);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	ConnectConfig config;
	read_connect_options(L, config);
	config.prepare();
	return status_result(L,
	                     lk_room_connect_with_options(room->native, url, token, &config.options));
}

int disconnect_room(lua_State* L) {
	Room* room = check_room(L, 1);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	return status_result(L, lk_room_disconnect(room->native));
}

int publish_data(lua_State* L) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* data = luaL_checklstring(L, 2, &size);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	lk_data_publish_options_t options;
	lk_data_publish_options_init(&options);
	options.reliable = lua_isnoneornil(L, 3) ? 1 : lua_toboolean(L, 3);
	options.topic = luaL_optstring(L, 4, "");
	return status_result(
	    L,
	    lk_room_publish_data(room->native, reinterpret_cast<const uint8_t*>(data), size, &options));
}

int publish_data_with_options(lua_State* L) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* data = luaL_checklstring(L, 2, &size);
	const auto config = read_stream_writer_config(L, false, 3);
	lua_getfield(L, 3, "reliable");
	if (!lua_isnil(L, -1))
		luaL_checktype(L, -1, LUA_TBOOLEAN);
	const bool reliable = lua_isnil(L, -1) || lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (room->native == nullptr)
		return media_error(L, "room is closed");
	if (async_busy(room))
		return busy_result(L);
	std::vector<const char*> destinations;
	for (const auto& identity : config.destinations)
		destinations.push_back(identity.c_str());
	lk_data_publish_options_t options;
	lk_data_publish_options_init(&options);
	options.reliable = reliable;
	options.topic = config.topic.c_str();
	options.destination_identities = destinations.data();
	options.destination_identity_count = destinations.size();
	return status_result(
	    L,
	    lk_room_publish_data(room->native, reinterpret_cast<const uint8_t*>(data), size, &options));
}

int start_publish_data_with_options(lua_State* L) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* data = luaL_checklstring(L, 2, &size);
	auto config = read_stream_writer_config(L, false, 3);
	lua_getfield(L, 3, "reliable");
	if (!lua_isnil(L, -1))
		luaL_checktype(L, -1, LUA_TBOOLEAN);
	const bool reliable = lua_isnil(L, -1) || lua_toboolean(L, -1);
	lua_pop(L, 1);
	auto task = std::make_shared<AsyncTask>();
	task->operation = AsyncOperation::PublishData;
	task->first.assign(data, size);
	task->stream_config = std::move(config);
	task->reliable = reliable;
	return start_task(L, room, std::move(task));
}

int start_connect(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* url = luaL_checkstring(L, 2);
	const char* token = luaL_checkstring(L, 3);
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::Connect;
		task->first = url;
		task->second = token;
		read_connect_options(L, task->connect);
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}

int start_disconnect(lua_State* L) {
	Room* room = check_room(L, 1);
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::Disconnect;
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}

int start_publish_data(lua_State* L) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* data = luaL_checklstring(L, 2, &size);
	const bool reliable = lua_isnoneornil(L, 3) || lua_toboolean(L, 3) != 0;
	const char* topic = luaL_optstring(L, 4, "");
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::PublishData;
		task->first.assign(data, size);
		task->reliable = reliable;
		task->topic = topic;
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}

void push_rpc_result(lua_State* L, bool ok, uint32_t error_code, const std::string& payload,
                     const std::string& error_message, const std::string& error_data) {
	lua_newtable(L);
	boolean_field(L, "ok", ok);
	string_field(L, "payload", payload);
	integer_field(L, "error_code", error_code);
	string_field(L, "error_message", error_message);
	string_field(L, "error_data", error_data);
}

int perform_rpc(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* destination = luaL_checkstring(L, 2);
	const char* method = luaL_checkstring(L, 3);
	const char* payload = luaL_optstring(L, 4, "");
	const lua_Integer timeout = luaL_optinteger(L, 5, 0);
	if (timeout < 0 || timeout > UINT32_MAX) {
		lua_pushnil(L);
		lua_pushliteral(L, "RPC timeout is out of range");
		return 2;
	}
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	lk_rpc_perform_options_t options;
	lk_rpc_perform_options_init(&options);
	options.destination_identity = destination;
	options.method = method;
	options.payload = payload;
	if (timeout != 0)
		options.response_timeout_ms = static_cast<uint32_t>(timeout);
	lk_rpc_result_t* raw = nullptr;
	const auto status = lk_room_perform_rpc(room->native, &options, &raw);
	std::unique_ptr<lk_rpc_result_t, decltype(&lk_rpc_result_destroy)> result(
	    raw, lk_rpc_result_destroy);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	if (raw == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "RPC returned no result");
		return 2;
	}
	push_rpc_result(L, lk_rpc_result_ok(raw) != 0, lk_rpc_result_error_code(raw),
	                rpc_string(raw, lk_rpc_result_payload),
	                rpc_string(raw, lk_rpc_result_error_message),
	                rpc_string(raw, lk_rpc_result_error_data));
	return 1;
}

int start_rpc(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* destination = luaL_checkstring(L, 2);
	const char* method = luaL_checkstring(L, 3);
	const char* payload = luaL_optstring(L, 4, "");
	const lua_Integer timeout = luaL_optinteger(L, 5, 0);
	if (timeout < 0 || timeout > UINT32_MAX) {
		lua_pushnil(L);
		lua_pushliteral(L, "RPC timeout is out of range");
		return 2;
	}
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::Rpc;
		task->first = destination;
		task->second = method;
		task->topic = payload;
		task->timeout_ms = static_cast<uint32_t>(timeout);
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}

int async_result(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	std::shared_ptr<AsyncTask> task;
	{
		std::lock_guard<std::mutex> lock(room->mutex);
		auto found = room->tasks.find(id);
		if (found == room->tasks.end()) {
			lua_pushnil(L);
			lua_pushliteral(L, "unknown asynchronous operation");
			return 2;
		}
		if (!found->second->done) {
			lua_pushboolean(L, 0);
			return 1;
		}
		task = found->second;
		room->tasks.erase(found);
		--room->completed_tasks;
	}
	lua_pushboolean(L, 1);
	if (task->status == LK_STATUS_OK) {
		if (task->operation == AsyncOperation::Rpc)
			push_rpc_result(L, task->rpc_ok, task->rpc_error_code, task->rpc_payload,
			                task->rpc_error_message, task->rpc_error_data);
		else if (task->operation == AsyncOperation::GetDataTrackSchema) {
			lua_newtable(L);
			string_field(L, "name", task->second);
			integer_field(L, "encoding", task->media_channels);
			string_field(L, "custom_encoding", task->mime_type);
			string_field(L, "definition", task->name);
		} else if (task->operation == AsyncOperation::Chat) {
			lua_newtable(L);
			string_field(L, "id", task->chat_id);
			number_field(L, "timestamp", static_cast<lua_Number>(task->chat_timestamp));
		} else if (task->operation == AsyncOperation::PublishAudioTrack ||
		           task->operation == AsyncOperation::PublishVideoTrack ||
		           task->operation == AsyncOperation::PublishCaptureTrack ||
		           task->operation == AsyncOperation::OpenStreamWriter ||
		           task->operation == AsyncOperation::PublishDataTrack ||
		           task->operation == AsyncOperation::SubscribeDataTrack)
			lua_pushnumber(L, static_cast<lua_Number>(task->media_id));
		else
			lua_pushboolean(L, 1);
		return 2;
	}
	lua_pushnil(L);
	lua_pushstring(L, task->error.empty() ? "room closed" : task->error.c_str());
	return 3;
}

int wait_room(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_Integer timeout = luaL_optinteger(L, 2, 50);
	if (timeout < 0)
		return luaL_argerror(L, 2, "must be nonnegative");
	if (timeout > 60000)
		timeout = 60000;
	std::unique_lock<std::mutex> lock(room->mutex);
	const bool ready = room->wake.wait_for(lock, std::chrono::milliseconds(timeout), [&] {
		return !room->events.empty() || !room->rpc_pending.empty() || room->completed_tasks != 0 ||
		       room->stopping;
	});
	lock.unlock();
	lua_pushboolean(L, ready);
	return 1;
}

int state(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_pushinteger(L, room->native == nullptr ? LK_ROOM_STATE_DISCONNECTED
	                                           : lk_room_state(room->native));
	return 1;
}
int is_connected(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_pushboolean(L, room->native != nullptr && lk_room_is_connected(room->native));
	return 1;
}
int room_string(lua_State* L, size_t (*getter)(const lk_room_t*, char*, size_t)) {
	Room* room = check_room(L, 1);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	const size_t required = getter(room->native, nullptr, 0);
	std::string value(required, '\0');
	if (required != 0)
		getter(room->native, &value[0], value.size());
	lua_pushlstring(L, value.data(), required > 0 ? required - 1 : 0);
	return 1;
}
int sid(lua_State* L) { return room_string(L, lk_room_sid); }
int name(lua_State* L) { return room_string(L, lk_room_name); }
int metadata(lua_State* L) { return room_string(L, lk_room_metadata); }
int disconnect_reason(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_pushinteger(L, room->native == nullptr ? LK_DISCONNECT_REASON_UNKNOWN
	                                           : lk_room_disconnect_reason(room->native));
	return 1;
}
int is_recording(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_pushboolean(L, room->native != nullptr && lk_room_is_recording(room->native));
	return 1;
}

size_t key_index(lua_State* L, int argument) {
	const lua_Integer value = luaL_optinteger(L, argument, 0);
	luaL_argcheck(L, value >= 0, argument, "key index must be nonnegative");
	return static_cast<size_t>(value);
}

int e2ee_is_configured(lua_State* L) {
	lua_pushboolean(L, lk_room_e2ee_is_configured(check_room(L, 1)->native));
	return 1;
}
int e2ee_is_enabled(lua_State* L) {
	lua_pushboolean(L, lk_room_e2ee_is_enabled(check_room(L, 1)->native));
	return 1;
}
int e2ee_set_enabled(lua_State* L) {
	Room* room = check_room(L, 1);
	luaL_checktype(L, 2, LUA_TBOOLEAN);
	return status_result(L, lk_room_e2ee_set_enabled(room->native, lua_toboolean(L, 2)));
}
int e2ee_set_shared_key(lua_State* L) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* bytes = luaL_checklstring(L, 2, &size);
	return status_result(L, lk_room_e2ee_set_shared_key(room->native,
	                                                    reinterpret_cast<const uint8_t*>(bytes),
	                                                    size, key_index(L, 3)));
}
int e2ee_export_shared_key(lua_State* L) {
	Room* room = check_room(L, 1);
	const size_t index = key_index(L, 2);
	const size_t required = lk_room_e2ee_export_shared_key(room->native, index, nullptr, 0);
	if (required == 0) {
		lua_pushnil(L);
		lua_pushliteral(L, "shared key is unavailable");
		return 2;
	}
	std::string bytes(required, '\0');
	const size_t written = lk_room_e2ee_export_shared_key(
	    room->native, index, reinterpret_cast<uint8_t*>(bytes.data()), bytes.size());
	if (written != required) {
		lua_pushnil(L);
		lua_pushliteral(L, "shared key changed while exporting");
		return 2;
	}
	lua_pushlstring(L, bytes.data(), bytes.size());
	return 1;
}
int e2ee_ratchet_shared_key(lua_State* L) {
	Room* room = check_room(L, 1);
	return status_result(L, lk_room_e2ee_ratchet_shared_key(room->native, key_index(L, 2)));
}
int e2ee_remove_shared_key(lua_State* L) {
	Room* room = check_room(L, 1);
	return status_result(L, lk_room_e2ee_remove_shared_key(room->native, key_index(L, 2)));
}
int e2ee_set_participant_key(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	size_t size = 0;
	const char* bytes = luaL_checklstring(L, 3, &size);
	return status_result(L, lk_room_e2ee_set_participant_key(
	                            room->native, identity, reinterpret_cast<const uint8_t*>(bytes),
	                            size, key_index(L, 4)));
}
int e2ee_export_participant_key(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	const size_t index = key_index(L, 3);
	const size_t required =
	    lk_room_e2ee_export_participant_key(room->native, identity, index, nullptr, 0);
	if (required == 0) {
		lua_pushnil(L);
		lua_pushliteral(L, "participant key is unavailable");
		return 2;
	}
	std::string bytes(required, '\0');
	const size_t written = lk_room_e2ee_export_participant_key(
	    room->native, identity, index, reinterpret_cast<uint8_t*>(bytes.data()), bytes.size());
	if (written != required) {
		lua_pushnil(L);
		lua_pushliteral(L, "participant key changed while exporting");
		return 2;
	}
	lua_pushlstring(L, bytes.data(), bytes.size());
	return 1;
}
int e2ee_ratchet_participant_key(lua_State* L) {
	Room* room = check_room(L, 1);
	return status_result(L, lk_room_e2ee_ratchet_participant_key(
	                            room->native, luaL_checkstring(L, 2), key_index(L, 3)));
}
int e2ee_remove_participant_key(lua_State* L) {
	Room* room = check_room(L, 1);
	return status_result(L, lk_room_e2ee_remove_participant_key(
	                            room->native, luaL_checkstring(L, 2), key_index(L, 3)));
}
int e2ee_remove_participant_keys(lua_State* L) {
	Room* room = check_room(L, 1);
	return status_result(
	    L, lk_room_e2ee_remove_participant_keys(room->native, luaL_checkstring(L, 2)));
}
int e2ee_clear_keys(lua_State* L) {
	return status_result(L, lk_room_e2ee_clear_keys(check_room(L, 1)->native));
}
int e2ee_data_key_index(lua_State* L) {
	lua_pushnumber(L,
	               static_cast<lua_Number>(lk_room_e2ee_data_key_index(check_room(L, 1)->native)));
	return 1;
}
int e2ee_set_data_key_index(lua_State* L) {
	Room* room = check_room(L, 1);
	return status_result(L, lk_room_e2ee_set_data_key_index(room->native, key_index(L, 2)));
}

template <typename Getter, typename... Args> std::string owned_string(Getter getter, Args... args) {
	const size_t required = getter(args..., nullptr, 0);
	if (required == 0)
		return {};
	std::string value(required, '\0');
	getter(args..., value.data(), value.size());
	value.resize(required - 1);
	return value;
}

void string_field(lua_State* L, const char* key, const std::string& value) {
	lua_pushlstring(L, value.data(), value.size());
	lua_setfield(L, -2, key);
}
void integer_field(lua_State* L, const char* key, lua_Integer value) {
	lua_pushinteger(L, value);
	lua_setfield(L, -2, key);
}
void number_field(lua_State* L, const char* key, lua_Number value) {
	lua_pushnumber(L, value);
	lua_setfield(L, -2, key);
}
void boolean_field(lua_State* L, const char* key, bool value) {
	lua_pushboolean(L, value);
	lua_setfield(L, -2, key);
}

int local_participant(lua_State* L) {
	Room* room = check_room(L, 1);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	lk_local_participant_snapshot_t* raw = nullptr;
	const auto status = lk_room_create_local_participant_snapshot(room->native, &raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_local_participant_snapshot_t,
	                decltype(&lk_local_participant_snapshot_destroy)>
	    snapshot(raw, lk_local_participant_snapshot_destroy);
	lk_local_participant_snapshot_info_t info;
	lk_local_participant_snapshot_info_init(&info);
	if (lk_local_participant_snapshot_info(raw, &info) != LK_STATUS_OK)
		return status_result(L, LK_STATUS_OPERATION_FAILED);
	lua_newtable(L);
	string_field(L, "sid", owned_string(lk_local_participant_snapshot_sid, raw));
	string_field(L, "identity", owned_string(lk_local_participant_snapshot_identity, raw));
	string_field(L, "name", owned_string(lk_local_participant_snapshot_name, raw));
	string_field(L, "metadata", owned_string(lk_local_participant_snapshot_metadata, raw));
	number_field(L, "audio_level", info.audio_level);
	integer_field(L, "connection_quality", info.connection_quality);
	boolean_field(L, "is_speaking", info.is_speaking != 0);
	lua_newtable(L);
	const size_t count = lk_local_participant_snapshot_attribute_count(raw);
	for (size_t i = 0; i < count; ++i) {
		const std::string key = owned_string(lk_local_participant_snapshot_attribute_key, raw, i);
		const std::string value =
		    owned_string(lk_local_participant_snapshot_attribute_value, raw, i);
		lua_pushlstring(L, value.data(), value.size());
		lua_setfield(L, -2, key.c_str());
	}
	lua_setfield(L, -2, "attributes");
	return 1;
}

int remote_participants(lua_State* L) {
	Room* room = check_room(L, 1);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	lk_remote_participant_list_t* raw = nullptr;
	const auto status = lk_room_create_remote_participant_snapshot(room->native, &raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_remote_participant_list_t, decltype(&lk_remote_participant_list_destroy)>
	    snapshot(raw, lk_remote_participant_list_destroy);
	lua_newtable(L);
	const size_t count = lk_remote_participant_list_count(raw);
	for (size_t i = 0; i < count; ++i) {
		const lk_remote_participant_snapshot_t* participant = nullptr;
		if (lk_remote_participant_list_at(raw, i, &participant) != LK_STATUS_OK)
			return status_result(L, LK_STATUS_OPERATION_FAILED);
		lk_remote_participant_snapshot_info_t info;
		lk_remote_participant_snapshot_info_init(&info);
		if (lk_remote_participant_snapshot_info(participant, &info) != LK_STATUS_OK)
			return status_result(L, LK_STATUS_OPERATION_FAILED);
		lua_newtable(L);
		string_field(L, "sid", owned_string(lk_remote_participant_snapshot_sid, participant));
		string_field(L, "identity",
		             owned_string(lk_remote_participant_snapshot_identity, participant));
		string_field(L, "name", owned_string(lk_remote_participant_snapshot_name, participant));
		string_field(L, "metadata",
		             owned_string(lk_remote_participant_snapshot_metadata, participant));
		number_field(L, "audio_level", info.audio_level);
		integer_field(L, "connection_quality", info.connection_quality);
		boolean_field(L, "is_speaking", info.is_speaking != 0);
		lua_newtable(L);
		const size_t attr_count = lk_remote_participant_snapshot_attribute_count(participant);
		for (size_t j = 0; j < attr_count; ++j) {
			const std::string key =
			    owned_string(lk_remote_participant_snapshot_attribute_key, participant, j);
			const std::string value =
			    owned_string(lk_remote_participant_snapshot_attribute_value, participant, j);
			lua_pushlstring(L, value.data(), value.size());
			lua_setfield(L, -2, key.c_str());
		}
		lua_setfield(L, -2, "attributes");
		lua_newtable(L);
		const size_t publication_count =
		    lk_remote_participant_snapshot_publication_count(participant);
		for (size_t j = 0; j < publication_count; ++j) {
			const lk_remote_track_publication_snapshot_t* publication = nullptr;
			if (lk_remote_participant_snapshot_publication_at(participant, j, &publication) !=
			    LK_STATUS_OK)
				return status_result(L, LK_STATUS_OPERATION_FAILED);
			lk_remote_track_publication_snapshot_info_t track_info;
			lk_remote_track_publication_snapshot_info_init(&track_info);
			if (lk_remote_track_publication_snapshot_info(publication, &track_info) != LK_STATUS_OK)
				return status_result(L, LK_STATUS_OPERATION_FAILED);
			lua_newtable(L);
			string_field(L, "sid",
			             owned_string(lk_remote_track_publication_snapshot_sid, publication));
			string_field(L, "name",
			             owned_string(lk_remote_track_publication_snapshot_name, publication));
			string_field(L, "mime_type",
			             owned_string(lk_remote_track_publication_snapshot_mime_type, publication));
			integer_field(L, "kind", track_info.kind);
			integer_field(L, "source", track_info.source);
			integer_field(L, "width", track_info.width);
			integer_field(L, "height", track_info.height);
			boolean_field(L, "is_muted", track_info.is_muted != 0);
			boolean_field(L, "subscription_allowed", track_info.subscription_allowed != 0);
			integer_field(L, "subscription_status", track_info.subscription_status);
			boolean_field(L, "has_subscribed_track", track_info.has_subscribed_track != 0);
			lua_rawseti(L, -2, static_cast<int>(j + 1));
		}
		lua_setfield(L, -2, "publications");
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}

int local_string(lua_State* L, size_t (*getter)(const lk_room_t*, char*, size_t)) {
	return room_string(L, getter);
}
int local_sid(lua_State* L) { return local_string(L, lk_local_participant_sid); }
int local_identity(lua_State* L) { return local_string(L, lk_local_participant_identity); }
int local_name(lua_State* L) { return local_string(L, lk_local_participant_name); }
int local_metadata(lua_State* L) { return local_string(L, lk_local_participant_metadata); }

int set_local_metadata(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* value = luaL_checkstring(L, 2);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	return status_result(L, lk_local_participant_set_metadata(room->native, value));
}
int set_local_name(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* value = luaL_checkstring(L, 2);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	return status_result(L, lk_local_participant_set_name(room->native, value));
}
int set_local_attributes(lua_State* L) {
	Room* room = check_room(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	std::vector<std::pair<std::string, std::string>> values;
	lua_pushnil(L);
	while (lua_next(L, 2) != 0) {
		if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING) {
			lua_pop(L, 2);
			lua_pushnil(L);
			lua_pushliteral(L, "attribute keys and values must be strings");
			return 2;
		}
		values.emplace_back(lua_tostring(L, -2), lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	std::vector<lk_attribute_t> attributes;
	attributes.reserve(values.size());
	for (const auto& [key, value] : values)
		attributes.push_back({key.c_str(), value.c_str()});
	return status_result(
	    L, lk_local_participant_set_attributes(room->native, attributes.data(), attributes.size()));
}

int audio_output_device(lua_State* L) { return room_string(L, lk_room_audio_output_device); }
int set_audio_output_device(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* id = luaL_checkstring(L, 2);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	return status_result(L, lk_room_set_audio_output_device(room->native, id));
}
int speaker_volume(lua_State* L) {
	Room* room = check_room(L, 1);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	lua_pushnumber(L, lk_room_speaker_volume(room->native));
	return 1;
}
int set_speaker_volume(lua_State* L) {
	Room* room = check_room(L, 1);
	const float volume = static_cast<float>(luaL_checknumber(L, 2));
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	return status_result(L, lk_room_set_speaker_volume(room->native, volume));
}
int speaker_is_muted(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_pushboolean(L, room->native != nullptr && lk_room_speaker_is_muted(room->native));
	return 1;
}
int set_speaker_muted(lua_State* L) {
	Room* room = check_room(L, 1);
	const int muted = lua_toboolean(L, 2);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	return status_result(L, lk_room_set_speaker_muted(room->native, muted));
}
int audio_playback_stats(lua_State* L) {
	Room* room = check_room(L, 1);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	lk_audio_playback_stats_t stats;
	lk_audio_playback_stats_init(&stats);
	const auto status = lk_room_audio_playback_stats(room->native, &stats);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_newtable(L);
	number_field(L, "queued_frames", static_cast<lua_Number>(stats.queued_frames));
	number_field(L, "played_frames", static_cast<lua_Number>(stats.played_frames));
	number_field(L, "dropped_frames", static_cast<lua_Number>(stats.dropped_frames));
	number_field(L, "underrun_frames", static_cast<lua_Number>(stats.underrun_frames));
	integer_field(L, "buffered_duration_ms", stats.buffered_duration_ms);
	integer_field(L, "device_latency_ms", stats.device_latency_ms);
	integer_field(L, "estimated_delay_ms", stats.estimated_delay_ms);
	return 1;
}

uint64_t media_id(lua_State* L, int index) {
	const lua_Number value = luaL_checknumber(L, index);
	luaL_argcheck(L, value >= 1 && value < 9007199254740992.0 && value == std::floor(value), index,
	              "invalid media ID");
	return static_cast<uint64_t>(value);
}
int media_error(lua_State* L, const char* message) {
	lua_pushnil(L);
	lua_pushstring(L, message);
	return 2;
}
int push_rtc_stats(lua_State* L, const lk_rtc_stats_snapshot_t* snapshot) {
	lua_newtable(L);
	const size_t count = lk_rtc_stats_snapshot_count(snapshot);
	for (size_t i = 0; i < count; ++i) {
		lk_rtc_track_stats_t stats;
		lk_rtc_track_stats_init(&stats);
		const auto status = lk_rtc_stats_snapshot_info(snapshot, i, &stats);
		if (status != LK_STATUS_OK)
			return status_result(L, status);
		lua_newtable(L);
		string_field(L, "id", owned_string(lk_rtc_stats_snapshot_id, snapshot, i));
		string_field(L, "kind", owned_string(lk_rtc_stats_snapshot_kind, snapshot, i));
		string_field(L, "rid", owned_string(lk_rtc_stats_snapshot_rid, snapshot, i));
		string_field(L, "codec_mime_type",
		             owned_string(lk_rtc_stats_snapshot_codec_mime_type, snapshot, i));
		string_field(L, "codec_implementation",
		             owned_string(lk_rtc_stats_snapshot_codec_implementation, snapshot, i));
		string_field(L, "quality_limitation_reason",
		             owned_string(lk_rtc_stats_snapshot_quality_limitation_reason, snapshot, i));
		integer_field(L, "direction", stats.direction);
		number_field(L, "timestamp_ms", stats.timestamp_ms);
		number_field(L, "bytes", static_cast<lua_Number>(stats.bytes));
		number_field(L, "packets", static_cast<lua_Number>(stats.packets));
		number_field(L, "packets_lost", static_cast<lua_Number>(stats.packets_lost));
		if (stats.has_bitrate_bps)
			number_field(L, "bitrate_bps", stats.bitrate_bps);
		if (stats.has_round_trip_time_seconds)
			number_field(L, "round_trip_time_seconds", stats.round_trip_time_seconds);
		if (stats.has_jitter_seconds)
			number_field(L, "jitter_seconds", stats.jitter_seconds);
		if (stats.has_audio_level)
			number_field(L, "audio_level", stats.audio_level);
		number_field(L, "concealed_samples", static_cast<lua_Number>(stats.concealed_samples));
		integer_field(L, "frame_width", stats.frame_width);
		integer_field(L, "frame_height", stats.frame_height);
		number_field(L, "frames_per_second", stats.frames_per_second);
		number_field(L, "frames", static_cast<lua_Number>(stats.frames));
		number_field(L, "frames_dropped", static_cast<lua_Number>(stats.frames_dropped));
		number_field(L, "fir_count", static_cast<lua_Number>(stats.fir_count));
		number_field(L, "pli_count", static_cast<lua_Number>(stats.pli_count));
		number_field(L, "nack_count", static_cast<lua_Number>(stats.nack_count));
		number_field(L, "qp_sum", static_cast<lua_Number>(stats.qp_sum));
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}
int local_track_rtc_stats(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end())
		return media_error(L, "local track is unavailable");
	lk_rtc_stats_snapshot_t* raw = nullptr;
	const auto status = lk_local_track_create_rtc_stats_snapshot(found->second.track, &raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_rtc_stats_snapshot_t, decltype(&lk_rtc_stats_snapshot_destroy)> snapshot(
	    raw, lk_rtc_stats_snapshot_destroy);
	return push_rtc_stats(L, raw);
}
int remote_track_rtc_stats(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	const char* track_sid = luaL_checkstring(L, 3);
	if (room->native == nullptr)
		return media_error(L, "room is closed");
	lk_remote_participant_list_t* raw_list = nullptr;
	const auto status = lk_room_create_remote_participant_snapshot(room->native, &raw_list);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_remote_participant_list_t, decltype(&lk_remote_participant_list_destroy)>
	    participants(raw_list, lk_remote_participant_list_destroy);
	for (size_t i = 0; i < lk_remote_participant_list_count(raw_list); ++i) {
		const lk_remote_participant_snapshot_t* participant = nullptr;
		if (lk_remote_participant_list_at(raw_list, i, &participant) != LK_STATUS_OK)
			return media_error(L, "failed to read remote participant snapshot");
		if (owned_string(lk_remote_participant_snapshot_identity, participant) != identity)
			continue;
		for (size_t j = 0; j < lk_remote_participant_snapshot_publication_count(participant); ++j) {
			const lk_remote_track_publication_snapshot_t* publication = nullptr;
			if (lk_remote_participant_snapshot_publication_at(participant, j, &publication) !=
			    LK_STATUS_OK)
				return media_error(L, "failed to read remote track publication");
			if (owned_string(lk_remote_track_publication_snapshot_sid, publication) != track_sid)
				continue;
			const lk_remote_track_snapshot_t* track = nullptr;
			if (lk_remote_track_publication_snapshot_track(publication, &track) != LK_STATUS_OK ||
			    track == nullptr)
				return media_error(L, "remote track is not subscribed");
			lk_rtc_stats_snapshot_t* raw_stats = nullptr;
			const auto stats_status =
			    lk_remote_track_snapshot_create_rtc_stats_snapshot(track, &raw_stats);
			if (stats_status != LK_STATUS_OK)
				return status_result(L, stats_status);
			std::unique_ptr<lk_rtc_stats_snapshot_t, decltype(&lk_rtc_stats_snapshot_destroy)>
			    stats(raw_stats, lk_rtc_stats_snapshot_destroy);
			return push_rtc_stats(L, raw_stats);
		}
		break;
	}
	return media_error(L, "remote track is unavailable");
}
bool publish_audio_native(Room* room, const char* label, uint32_t sample_rate, uint32_t channels,
                          uint32_t queue_ms, uint64_t& id, std::string& error) {
	if (room->native == nullptr || !lk_room_is_connected(room->native)) {
		error = "room is not connected";
		return false;
	}
	lk_audio_source_options_t source_options;
	lk_audio_source_options_init(&source_options);
	source_options.sample_rate = sample_rate;
	source_options.num_channels = channels;
	source_options.queue_size_ms = queue_ms;
	LocalMediaTrack media;
	media.sample_rate = source_options.sample_rate;
	media.channels = source_options.num_channels;
	auto status = lk_audio_source_create(&source_options, &media.audio);
	if (status == LK_STATUS_OK)
		status = lk_room_create_audio_track(room->native, label, media.audio, &media.track);
	if (status == LK_STATUS_OK) {
		lk_track_publish_options_t options;
		lk_track_publish_options_init(&options);
		options.source = LK_TRACK_SOURCE_MICROPHONE;
		status = lk_local_track_publish(room->native, media.track, &options);
	}
	if (status != LK_STATUS_OK) {
		error = safe(lk_last_error());
		destroy_local_media(media);
		return false;
	}
	try {
		std::lock_guard<std::mutex> lock(room->mutex);
		id = room->next_media_id++;
		room->local_tracks.emplace(id, media);
		return true;
	} catch (...) {
		lk_local_track_unpublish(media.track, 1);
		destroy_local_media(media);
		error = "failed to store local audio track";
		return false;
	}
}
int publish_audio_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const lua_Integer sample_rate = luaL_optinteger(L, 3, 48000);
	const lua_Integer channels = luaL_optinteger(L, 4, 1);
	const lua_Integer queue_ms = luaL_optinteger(L, 5, 200);
	if (sample_rate <= 0 || sample_rate > INT32_MAX || sample_rate % 100 != 0 || channels <= 0 ||
	    channels > 8 || queue_ms <= 0 || queue_ms > INT32_MAX || queue_ms % 10 != 0)
		return media_error(L, "invalid audio source options");
	if (async_busy(room))
		return busy_result(L);
	uint64_t id = 0;
	std::string error;
	if (!publish_audio_native(room, label, static_cast<uint32_t>(sample_rate),
	                          static_cast<uint32_t>(channels), static_cast<uint32_t>(queue_ms), id,
	                          error))
		return media_error(L, error.c_str());
	lua_pushnumber(L, static_cast<lua_Number>(id));
	return 1;
}
const char* prepare_video_frame(lk_video_frame_input_t& frame, const char* pixels, size_t bytes,
                                lua_Integer width, lua_Integer height, const char* format,
                                int64_t timestamp_us) {
	if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
		return "invalid video dimensions";
	const size_t area = static_cast<size_t>(width) * static_cast<size_t>(height);
	lk_video_buffer_type_t buffer_type;
	if (std::strcmp(format, "RGBA") == 0) {
		buffer_type = LK_VIDEO_BUFFER_RGBA;
		if (bytes != area * 4)
			return "RGBA frame size does not match dimensions";
	} else if (std::strcmp(format, "I420") == 0) {
		buffer_type = LK_VIDEO_BUFFER_I420;
		if ((width % 2) != 0 || (height % 2) != 0 || bytes != area * 3 / 2)
			return "I420 frame size does not match dimensions";
	} else {
		return "video format must be RGBA or I420";
	}
	lk_video_frame_input_init(&frame);
	frame.data = reinterpret_cast<const uint8_t*>(pixels);
	frame.data_size = bytes;
	frame.width = static_cast<uint32_t>(width);
	frame.height = static_cast<uint32_t>(height);
	frame.format = buffer_type;
	frame.timestamp_us = timestamp_us;
	return nullptr;
}
int64_t current_timestamp_us() {
	return std::chrono::duration_cast<std::chrono::microseconds>(
	           std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}
bool publish_video_native(Room* room, const char* label, const lk_video_frame_input_t& first_frame,
                          bool screen, uint64_t& id, std::string& error) {
	if (room->native == nullptr || !lk_room_is_connected(room->native)) {
		error = "room is not connected";
		return false;
	}
	lk_video_source_options_t source_options;
	lk_video_source_options_init(&source_options);
	source_options.is_screencast = screen ? 1 : 0;
	LocalMediaTrack media;
	auto status = lk_video_source_create(&source_options, &media.video);
	if (status == LK_STATUS_OK)
		status = lk_video_source_capture_frame(media.video, &first_frame);
	if (status == LK_STATUS_OK)
		status = lk_room_create_video_track(room->native, label, media.video, &media.track);
	if (status == LK_STATUS_OK) {
		lk_track_publish_options_t options;
		lk_track_publish_options_init(&options);
		options.source = screen ? LK_TRACK_SOURCE_SCREEN_SHARE : LK_TRACK_SOURCE_CAMERA;
		options.simulcast = 0;
		status = lk_local_track_publish(room->native, media.track, &options);
	}
	if (status != LK_STATUS_OK) {
		error = safe(lk_last_error());
		destroy_local_media(media);
		return false;
	}
	try {
		std::lock_guard<std::mutex> lock(room->mutex);
		id = room->next_media_id++;
		room->local_tracks.emplace(id, media);
		return true;
	} catch (...) {
		lk_local_track_unpublish(media.track, 1);
		destroy_local_media(media);
		error = "failed to store local video track";
		return false;
	}
}
int publish_video_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* label = luaL_checkstring(L, 2);
	size_t bytes = 0;
	const char* pixels = luaL_checklstring(L, 3, &bytes);
	const lua_Integer width = luaL_checkinteger(L, 4);
	const lua_Integer height = luaL_checkinteger(L, 5);
	const char* format = luaL_optstring(L, 6, "RGBA");
	const bool screen = lua_toboolean(L, 7) != 0;
	lk_video_frame_input_t first_frame;
	if (const char* error = prepare_video_frame(first_frame, pixels, bytes, width, height, format,
	                                            current_timestamp_us()))
		return media_error(L, error);
	if (async_busy(room))
		return busy_result(L);
	uint64_t id = 0;
	std::string error;
	if (!publish_video_native(room, label, first_frame, screen, id, error))
		return media_error(L, error.c_str());
	lua_pushnumber(L, static_cast<lua_Number>(id));
	return 1;
}
bool read_capture_config(lua_State* L, CaptureConfig& config, std::string& error) {
	if (lua_type(L, 2) != LUA_TSTRING || lua_type(L, 3) != LUA_TSTRING) {
		error = "capture kind and track label must be strings";
		return false;
	}
	const char* kind = lua_tostring(L, 2);
	config.label = lua_tostring(L, 3);
	if (std::strcmp(kind, "microphone") == 0)
		config.kind = CaptureKind::Microphone;
	else if (std::strcmp(kind, "system_audio") == 0)
		config.kind = CaptureKind::SystemAudio;
	else if (std::strcmp(kind, "camera") == 0)
		config.kind = CaptureKind::Camera;
	else if (std::strcmp(kind, "screen") == 0) {
		config.kind = CaptureKind::Screen;
		config.fps = 15;
	} else {
		error = "capture kind must be microphone, system_audio, camera, or screen";
		return false;
	}
	if (config.label.empty()) {
		error = "capture track label is required";
		return false;
	}
	if (lua_isnoneornil(L, 4)) {
		if (config.kind == CaptureKind::Screen) {
			error = "screen source_id is required";
			return false;
		}
		return true;
	}
	if (lua_type(L, 4) != LUA_TTABLE) {
		error = "capture options must be a table";
		return false;
	}
	auto string_option = [&](const char* key, std::string& output) {
		lua_getfield(L, 4, key);
		const bool valid = lua_isnil(L, -1) || lua_type(L, -1) == LUA_TSTRING;
		if (lua_type(L, -1) == LUA_TSTRING)
			output = lua_tostring(L, -1);
		lua_pop(L, 1);
		if (!valid)
			error = std::string(key) + " must be a string";
		return valid;
	};
	auto uint_option = [&](const char* key, uint32_t& output, uint32_t max) {
		lua_getfield(L, 4, key);
		bool valid = true;
		if (!lua_isnil(L, -1)) {
			valid = lua_type(L, -1) == LUA_TNUMBER;
			if (valid) {
				const lua_Number value = lua_tonumber(L, -1);
				valid = value >= 1 && value <= max && value == std::floor(value);
				if (valid)
					output = static_cast<uint32_t>(value);
			}
		}
		lua_pop(L, 1);
		if (!valid)
			error = std::string(key) + " must be a positive integer in range";
		return valid;
	};
	auto bool_option = [&](const char* key, bool& output) {
		lua_getfield(L, 4, key);
		const bool valid = lua_isnil(L, -1) || lua_type(L, -1) == LUA_TBOOLEAN;
		if (lua_type(L, -1) == LUA_TBOOLEAN)
			output = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
		if (!valid)
			error = std::string(key) + " must be a boolean";
		return valid;
	};
	if (config.kind == CaptureKind::Screen) {
		if (!string_option("source_id", config.source_id) || !uint_option("fps", config.fps, 60) ||
		    !bool_option("include_cursor", config.include_cursor))
			return false;
		if (config.source_id.empty()) {
			error = "screen source_id is required";
			return false;
		}
		return true;
	}
	if (!string_option("device_id", config.source_id))
		return false;
	if (config.kind == CaptureKind::Camera)
		return uint_option("width", config.width, 8192) &&
		       uint_option("height", config.height, 8192) && uint_option("fps", config.fps, 120);
	if (!uint_option("queue_ms", config.queue_ms, INT32_MAX))
		return false;
	if (config.queue_ms % 10 != 0) {
		error = "queue_ms must be a multiple of 10";
		return false;
	}
	if (config.kind == CaptureKind::Microphone)
		return bool_option("echo_cancellation", config.echo_cancellation) &&
		       bool_option("auto_gain_control", config.auto_gain_control) &&
		       bool_option("noise_suppression", config.noise_suppression);
	return true;
}
bool publish_capture_native(Room* room, const CaptureConfig& config, uint64_t& id,
                            std::string& error) {
	if (room->native == nullptr || !lk_room_is_connected(room->native)) {
		error = "room is not connected";
		return false;
	}
	LocalMediaTrack media;
	media.capture_kind = config.kind;
	const char* source_id = config.source_id.empty() ? nullptr : config.source_id.c_str();
	lk_status_t status = LK_STATUS_INVALID_ARGUMENT;
	switch (config.kind) {
	case CaptureKind::Microphone: {
		lk_microphone_capture_options_t options;
		lk_microphone_capture_options_init(&options);
		options.device_id = source_id;
		options.queue_size_ms = config.queue_ms;
		options.echo_cancellation = config.echo_cancellation;
		options.auto_gain_control = config.auto_gain_control;
		options.noise_suppression = config.noise_suppression;
		status = lk_audio_source_create_microphone(&options, &media.audio);
		break;
	}
	case CaptureKind::SystemAudio: {
		lk_system_audio_capture_options_t options;
		lk_system_audio_capture_options_init(&options);
		options.device_id = source_id;
		options.queue_size_ms = config.queue_ms;
		status = lk_audio_source_create_system_audio(&options, &media.audio);
		break;
	}
	case CaptureKind::Camera: {
		lk_camera_capture_options_t options;
		lk_camera_capture_options_init(&options);
		options.device_id = source_id;
		options.width = config.width;
		options.height = config.height;
		options.frames_per_second = config.fps;
		status = lk_video_source_create_camera(&options, &media.video);
		break;
	}
	case CaptureKind::Screen: {
		lk_screen_capture_options_t options;
		lk_screen_capture_options_init(&options);
		options.source_id = source_id;
		options.frames_per_second = config.fps;
		options.include_cursor = config.include_cursor;
		status = lk_video_source_create_screen(&options, &media.video);
		break;
	}
	case CaptureKind::None:
		break;
	}
	if (status == LK_STATUS_OK && media.video != nullptr) {
		bool ready = false;
		for (int attempt = 0; attempt < 100; ++attempt) {
			uint32_t width = 0;
			uint32_t height = 0;
			status = lk_video_source_dimensions(media.video, &width, &height);
			if (status != LK_STATUS_OK)
				break;
			if (width != 0 && height != 0) {
				ready = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if (status == LK_STATUS_OK && !ready) {
			status = LK_STATUS_OPERATION_FAILED;
			error = "capture source did not produce a video frame";
		}
	}
	if (status == LK_STATUS_OK)
		status = media.audio != nullptr
		             ? lk_room_create_audio_track(room->native, config.label.c_str(), media.audio,
		                                          &media.track)
		             : lk_room_create_video_track(room->native, config.label.c_str(), media.video,
		                                          &media.track);
	if (status == LK_STATUS_OK) {
		lk_track_publish_options_t options;
		lk_track_publish_options_init(&options);
		switch (config.kind) {
		case CaptureKind::Microphone:
			options.source = LK_TRACK_SOURCE_MICROPHONE;
			status = lk_local_track_publish(room->native, media.track, &options);
			break;
		case CaptureKind::SystemAudio:
			options.source = LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO;
			status = lk_local_track_publish_screen_share_audio(room->native, media.track, &options);
			break;
		case CaptureKind::Camera:
			options.source = LK_TRACK_SOURCE_CAMERA;
			status = lk_local_track_publish(room->native, media.track, &options);
			break;
		case CaptureKind::Screen:
			options.source = LK_TRACK_SOURCE_SCREEN_SHARE;
			status = lk_local_track_publish_screen_share_video(room->native, media.track, &options);
			break;
		case CaptureKind::None:
			break;
		}
	}
	if (status != LK_STATUS_OK) {
		if (error.empty())
			error = safe(lk_last_error());
		destroy_local_media(media);
		return false;
	}
	try {
		std::lock_guard<std::mutex> lock(room->mutex);
		id = room->next_media_id++;
		room->local_tracks.emplace(id, media);
		return true;
	} catch (...) {
		lk_local_track_unpublish(media.track, 1);
		destroy_local_media(media);
		error = "failed to store capture track";
		return false;
	}
}
int publish_capture_track(lua_State* L) {
	Room* room = check_room(L, 1);
	CaptureConfig config;
	std::string error;
	if (!read_capture_config(L, config, error))
		return media_error(L, error.c_str());
	if (async_busy(room))
		return busy_result(L);
	uint64_t id = 0;
	if (!publish_capture_native(room, config, id, error))
		return media_error(L, error.c_str());
	lua_pushnumber(L, static_cast<lua_Number>(id));
	return 1;
}
int start_publish_capture_track(lua_State* L) {
	Room* room = check_room(L, 1);
	try {
		auto task = std::make_shared<AsyncTask>();
		std::string error;
		if (!read_capture_config(L, task->capture, error))
			return media_error(L, error.c_str());
		task->operation = AsyncOperation::PublishCaptureTrack;
		return start_task(L, room, std::move(task));
	} catch (...) {
		return media_error(L, "failed to allocate asynchronous capture operation");
	}
}
int push_audio_frame(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	size_t bytes = 0;
	const char* pcm = luaL_checklstring(L, 3, &bytes);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.audio == nullptr ||
	    found->second.capture_kind != CaptureKind::None)
		return media_error(L, "audio track is unavailable");
	const auto& media = found->second;
	const size_t samples_per_channel = media.sample_rate / 100;
	if (samples_per_channel == 0 || bytes != samples_per_channel * media.channels * 2)
		return media_error(L, "PCM frame must contain exactly 10 ms of interleaved 16-bit samples");
	std::vector<int16_t> samples(bytes / sizeof(int16_t));
	std::memcpy(samples.data(), pcm, bytes);
	return status_result(L,
	                     lk_audio_source_capture_frame(media.audio, samples.data(),
	                                                   static_cast<uint32_t>(samples_per_channel)));
}
int push_video_frame(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	size_t bytes = 0;
	const char* pixels = luaL_checklstring(L, 3, &bytes);
	const lua_Integer width = luaL_checkinteger(L, 4);
	const lua_Integer height = luaL_checkinteger(L, 5);
	const char* format = luaL_optstring(L, 6, "RGBA");
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.video == nullptr ||
	    found->second.capture_kind != CaptureKind::None)
		return media_error(L, "video track is unavailable");
	lk_video_frame_input_t frame;
	const int64_t timestamp = static_cast<int64_t>(luaL_optnumber(L, 7, current_timestamp_us()));
	if (const char* error =
	        prepare_video_frame(frame, pixels, bytes, width, height, format, timestamp))
		return media_error(L, error);
	return status_result(L, lk_video_source_capture_frame(found->second.video, &frame));
}
int audio_source_queued_duration_ms(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.audio == nullptr)
		return media_error(L, "audio track is unavailable");
	uint32_t duration = 0;
	const auto status = lk_audio_source_queued_duration_ms(found->second.audio, &duration);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_pushinteger(L, duration);
	return 1;
}
int clear_audio_source_queue(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.audio == nullptr)
		return media_error(L, "audio track is unavailable");
	return status_result(L, lk_audio_source_clear_queue(found->second.audio));
}
int wait_audio_source_playout(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const lua_Integer timeout = luaL_optinteger(L, 3, 0);
	luaL_argcheck(L, timeout >= 0 && timeout <= UINT32_MAX, 3, "timeout is out of range");
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.audio == nullptr)
		return media_error(L, "audio track is unavailable");
	return status_result(
	    L, lk_audio_source_wait_for_playout(found->second.audio, static_cast<uint32_t>(timeout)));
}
int start_wait_audio_source_playout(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const lua_Integer timeout = luaL_optinteger(L, 3, 0);
	luaL_argcheck(L, timeout >= 0 && timeout <= UINT32_MAX, 3, "timeout is out of range");
	auto task = std::make_shared<AsyncTask>();
	task->operation = AsyncOperation::WaitAudioSource;
	task->media_id = id;
	task->timeout_ms = static_cast<uint32_t>(timeout);
	return start_task(L, room, std::move(task));
}
int update_video_encoding(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	const bool backup = lua_toboolean(L, 4);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.video == nullptr)
		return media_error(L, "video track is unavailable");
	lk_video_encoding_t encoding;
	lk_video_encoding_init(&encoding);
	lua_getfield(L, 3, "max_bitrate");
	if (!lua_isnil(L, -1)) {
		const lua_Number value = luaL_checknumber(L, -1);
		luaL_argcheck(L,
		              std::isfinite(value) && value >= 0 && std::floor(value) == value &&
		                  value <= 9007199254740991.0,
		              3, "max_bitrate must be a nonnegative exact integer");
		encoding.max_bitrate = static_cast<uint64_t>(value);
	}
	lua_pop(L, 1);
	lua_getfield(L, 3, "max_framerate");
	if (!lua_isnil(L, -1)) {
		const lua_Number value = luaL_checknumber(L, -1);
		luaL_argcheck(L, std::isfinite(value) && value >= 0 && value <= 1000, 3,
		              "max_framerate is out of range");
		encoding.max_framerate = static_cast<float>(value);
	}
	lua_pop(L, 1);
	return status_result(
	    L, lk_local_video_track_update_encoding(found->second.track, &encoding, backup));
}
int update_video_degradation_preference(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const lua_Integer value = luaL_checkinteger(L, 3);
	luaL_argcheck(L,
	              value >= LK_VIDEO_DEGRADATION_PREFERENCE_MAINTAIN_FRAMERATE &&
	                  value <= LK_VIDEO_DEGRADATION_PREFERENCE_DISABLED,
	              3, "invalid degradation preference");
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.video == nullptr)
		return media_error(L, "video track is unavailable");
	return status_result(
	    L, lk_local_video_track_update_degradation_preference(
	           found->second.track, static_cast<lk_video_degradation_preference_t>(value)));
}
int set_local_track_muted(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	luaL_checktype(L, 3, LUA_TBOOLEAN);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end())
		return media_error(L, "local track is unavailable");
	return status_result(L, lk_local_track_set_muted(found->second.track, lua_toboolean(L, 3)));
}
lk_status_t capture_control_native(Room* room, uint64_t id, CaptureAction action,
                                   const char* source_id, std::string& error) {
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.capture_kind == CaptureKind::None) {
		error = "capture track is unavailable";
		return LK_STATUS_INVALID_ARGUMENT;
	}
	if (action == CaptureAction::Switch && (source_id == nullptr || *source_id == '\0')) {
		error = "capture source ID is required";
		return LK_STATUS_INVALID_ARGUMENT;
	}
	lk_status_t status = LK_STATUS_INVALID_ARGUMENT;
	const auto& media = found->second;
	switch (media.capture_kind) {
	case CaptureKind::Microphone:
		status = action == CaptureAction::Start ? lk_audio_source_microphone_start(media.audio)
		         : action == CaptureAction::Stop
		             ? lk_audio_source_microphone_stop(media.audio)
		             : lk_audio_source_microphone_switch_device(media.audio, source_id);
		break;
	case CaptureKind::SystemAudio:
		status = action == CaptureAction::Start ? lk_audio_source_system_audio_start(media.audio)
		         : action == CaptureAction::Stop
		             ? lk_audio_source_system_audio_stop(media.audio)
		             : lk_audio_source_system_audio_switch_device(media.audio, source_id);
		break;
	case CaptureKind::Camera:
		status = action == CaptureAction::Start ? lk_video_source_camera_start(media.video)
		         : action == CaptureAction::Stop
		             ? lk_video_source_camera_stop(media.video)
		             : lk_video_source_camera_switch_device(media.video, source_id);
		break;
	case CaptureKind::Screen:
		status = action == CaptureAction::Start ? lk_video_source_screen_start(media.video)
		         : action == CaptureAction::Stop
		             ? lk_video_source_screen_stop(media.video)
		             : lk_video_source_screen_switch_source(media.video, source_id);
		break;
	case CaptureKind::None:
		break;
	}
	if (status != LK_STATUS_OK)
		error = safe(lk_last_error());
	return status;
}
CaptureAction capture_action(lua_State* L, int index) {
	const char* action = luaL_checkstring(L, index);
	if (std::strcmp(action, "start") == 0)
		return CaptureAction::Start;
	if (std::strcmp(action, "stop") == 0)
		return CaptureAction::Stop;
	if (std::strcmp(action, "switch") == 0)
		return CaptureAction::Switch;
	luaL_argerror(L, index, "action must be start, stop, or switch");
	return CaptureAction::Start;
}
int control_capture(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const CaptureAction action = capture_action(L, 3);
	const char* source_id = luaL_optstring(L, 4, "");
	if (async_busy(room))
		return busy_result(L);
	std::string error;
	const auto status = capture_control_native(room, id, action, source_id, error);
	return status == LK_STATUS_OK ? status_result(L, status) : media_error(L, error.c_str());
}
int start_control_capture(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const CaptureAction action = capture_action(L, 3);
	const char* source_id = luaL_optstring(L, 4, "");
	if (action == CaptureAction::Switch && *source_id == '\0')
		return media_error(L, "capture source ID is required");
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::CaptureControl;
		task->media_id = id;
		task->capture_action = action;
		task->first = source_id;
		return start_task(L, room, std::move(task));
	} catch (...) {
		return media_error(L, "failed to allocate asynchronous capture operation");
	}
}
int capture_is_running(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.capture_kind == CaptureKind::None)
		return media_error(L, "capture track is unavailable");
	const auto& media = found->second;
	bool running = false;
	switch (media.capture_kind) {
	case CaptureKind::Microphone:
		running = lk_audio_source_microphone_is_capturing(media.audio) != 0;
		break;
	case CaptureKind::SystemAudio:
		running = lk_audio_source_system_audio_is_capturing(media.audio) != 0;
		break;
	case CaptureKind::Camera:
		running = lk_video_source_camera_is_capturing(media.video) != 0;
		break;
	case CaptureKind::Screen:
		running = lk_video_source_screen_is_capturing(media.video) != 0;
		break;
	case CaptureKind::None:
		break;
	}
	lua_pushboolean(L, running);
	return 1;
}
int capture_source_id(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end() || found->second.capture_kind == CaptureKind::None)
		return media_error(L, "capture track is unavailable");
	const auto& media = found->second;
	std::string value;
	switch (media.capture_kind) {
	case CaptureKind::Microphone:
		value = owned_string(lk_audio_source_microphone_device_id, media.audio);
		break;
	case CaptureKind::SystemAudio:
		value = owned_string(lk_audio_source_system_audio_device_id, media.audio);
		break;
	case CaptureKind::Camera:
		value = owned_string(lk_video_source_camera_device_id, media.video);
		break;
	case CaptureKind::Screen:
		value = owned_string(lk_video_source_screen_source_id, media.video);
		break;
	case CaptureKind::None:
		break;
	}
	lua_pushlstring(L, value.data(), value.size());
	return 1;
}
LocalMediaTrack* microphone_media(Room* room, uint64_t id) {
	const auto found = room->local_tracks.find(id);
	return found != room->local_tracks.end() &&
	               found->second.capture_kind == CaptureKind::Microphone
	           ? &found->second
	           : nullptr;
}
int microphone_is_muted(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto* media = microphone_media(room, id);
	if (media == nullptr)
		return media_error(L, "microphone track is unavailable");
	lua_pushboolean(L, lk_audio_source_microphone_is_muted(media->audio));
	return 1;
}
int microphone_set_muted(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	luaL_checktype(L, 3, LUA_TBOOLEAN);
	if (async_busy(room))
		return busy_result(L);
	const auto* media = microphone_media(room, id);
	if (media == nullptr)
		return media_error(L, "microphone track is unavailable");
	return status_result(L,
	                     lk_audio_source_microphone_set_muted(media->audio, lua_toboolean(L, 3)));
}
int microphone_volume(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto* media = microphone_media(room, id);
	if (media == nullptr)
		return media_error(L, "microphone track is unavailable");
	lua_pushnumber(L, lk_audio_source_microphone_volume(media->audio));
	return 1;
}
int microphone_set_volume(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const lua_Number volume = luaL_checknumber(L, 3);
	if (!std::isfinite(volume) || volume < 0 || volume > 1)
		return media_error(L, "microphone volume must be between 0 and 1");
	if (async_busy(room))
		return busy_result(L);
	const auto* media = microphone_media(room, id);
	if (media == nullptr)
		return media_error(L, "microphone track is unavailable");
	return status_result(
	    L, lk_audio_source_microphone_set_volume(media->audio, static_cast<float>(volume)));
}
int microphone_processing_options(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto* media = microphone_media(room, id);
	if (media == nullptr)
		return media_error(L, "microphone track is unavailable");
	lk_audio_source_options_t options;
	lk_audio_source_options_init(&options);
	const auto status = lk_audio_source_microphone_processing_options(media->audio, &options);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_newtable(L);
	boolean_field(L, "echo_cancellation", options.echo_cancellation != 0);
	boolean_field(L, "auto_gain_control", options.auto_gain_control != 0);
	boolean_field(L, "noise_suppression", options.noise_suppression != 0);
	return 1;
}
int microphone_set_processing_options(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	if (async_busy(room))
		return busy_result(L);
	const auto* media = microphone_media(room, id);
	if (media == nullptr)
		return media_error(L, "microphone track is unavailable");
	lk_audio_source_options_t options;
	lk_audio_source_options_init(&options);
	const auto current = lk_audio_source_microphone_processing_options(media->audio, &options);
	if (current != LK_STATUS_OK)
		return status_result(L, current);
	auto update = [&](const char* key, int& target) {
		lua_getfield(L, 3, key);
		const bool valid = lua_isnil(L, -1) || lua_type(L, -1) == LUA_TBOOLEAN;
		if (lua_type(L, -1) == LUA_TBOOLEAN)
			target = lua_toboolean(L, -1);
		lua_pop(L, 1);
		return valid;
	};
	if (!update("echo_cancellation", options.echo_cancellation) ||
	    !update("auto_gain_control", options.auto_gain_control) ||
	    !update("noise_suppression", options.noise_suppression))
		return media_error(L, "microphone processing options must be boolean");
	return status_result(L,
	                     lk_audio_source_microphone_set_processing_options(media->audio, &options));
}
int microphone_processing_stats(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	const auto* media = microphone_media(room, id);
	if (media == nullptr)
		return media_error(L, "microphone track is unavailable");
	lk_microphone_processing_stats_t stats;
	lk_microphone_processing_stats_init(&stats);
	const auto status = lk_audio_source_microphone_processing_stats(media->audio, &stats);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_newtable(L);
	number_field(L, "capture_frames_processed",
	             static_cast<lua_Number>(stats.capture_frames_processed));
	number_field(L, "render_frames_processed",
	             static_cast<lua_Number>(stats.render_frames_processed));
	number_field(L, "capture_processing_errors",
	             static_cast<lua_Number>(stats.capture_processing_errors));
	number_field(L, "render_processing_errors",
	             static_cast<lua_Number>(stats.render_processing_errors));
	number_field(L, "frames_dropped", static_cast<lua_Number>(stats.frames_dropped));
	boolean_field(L, "echo_cancellation_enabled", stats.echo_cancellation_enabled != 0);
	if (stats.echo_return_loss_available)
		number_field(L, "echo_return_loss_db", stats.echo_return_loss_db);
	if (stats.echo_return_loss_enhancement_available)
		number_field(L, "echo_return_loss_enhancement_db", stats.echo_return_loss_enhancement_db);
	if (stats.residual_echo_likelihood_available)
		number_field(L, "residual_echo_likelihood", stats.residual_echo_likelihood);
	if (stats.residual_echo_likelihood_recent_max_available)
		number_field(L, "residual_echo_likelihood_recent_max",
		             stats.residual_echo_likelihood_recent_max);
	if (stats.delay_median_available)
		integer_field(L, "delay_median_ms", stats.delay_median_ms);
	if (stats.delay_standard_deviation_available)
		integer_field(L, "delay_standard_deviation_ms", stats.delay_standard_deviation_ms);
	if (stats.delay_available)
		integer_field(L, "delay_ms", stats.delay_ms);
	return 1;
}
bool unpublish_local_native(Room* room, uint64_t id, std::string& error) {
	const auto found = room->local_tracks.find(id);
	if (found == room->local_tracks.end()) {
		error = "local track is unavailable";
		return false;
	}
	if (room->native != nullptr && lk_room_is_connected(room->native)) {
		const auto status = lk_local_track_unpublish(found->second.track, 1);
		if (status != LK_STATUS_OK) {
			error = safe(lk_last_error());
			return false;
		}
	}
	if (!destroy_local_media(found->second)) {
		error = safe(lk_last_error());
		return false;
	}
	room->local_tracks.erase(found);
	return true;
}
int unpublish_local_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	if (async_busy(room))
		return busy_result(L);
	std::string error;
	if (!unpublish_local_native(room, id, error))
		return media_error(L, error.c_str());
	lua_pushboolean(L, 1);
	return 1;
}
int start_publish_audio_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const lua_Integer sample_rate = luaL_optinteger(L, 3, 48000);
	const lua_Integer channels = luaL_optinteger(L, 4, 1);
	const lua_Integer queue_ms = luaL_optinteger(L, 5, 200);
	if (sample_rate <= 0 || sample_rate > INT32_MAX || sample_rate % 100 != 0 || channels <= 0 ||
	    channels > 8 || queue_ms <= 0 || queue_ms > INT32_MAX || queue_ms % 10 != 0)
		return media_error(L, "invalid audio source options");
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::PublishAudioTrack;
		task->second = label;
		task->media_sample_rate = static_cast<uint32_t>(sample_rate);
		task->media_channels = static_cast<uint32_t>(channels);
		task->media_queue_ms = static_cast<uint32_t>(queue_ms);
		return start_task(L, room, std::move(task));
	} catch (...) {
		return media_error(L, "failed to allocate asynchronous media operation");
	}
}
int start_publish_video_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* label = luaL_checkstring(L, 2);
	size_t bytes = 0;
	const char* pixels = luaL_checklstring(L, 3, &bytes);
	const lua_Integer width = luaL_checkinteger(L, 4);
	const lua_Integer height = luaL_checkinteger(L, 5);
	const char* format = luaL_optstring(L, 6, "RGBA");
	const bool screen = lua_toboolean(L, 7) != 0;
	lk_video_frame_input_t check;
	if (const char* error = prepare_video_frame(check, pixels, bytes, width, height, format,
	                                            current_timestamp_us()))
		return media_error(L, error);
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::PublishVideoTrack;
		task->first.assign(pixels, bytes);
		task->second = label;
		task->topic = format;
		task->media_width = static_cast<uint32_t>(width);
		task->media_height = static_cast<uint32_t>(height);
		task->media_screen = screen;
		return start_task(L, room, std::move(task));
	} catch (...) {
		return media_error(L, "failed to allocate asynchronous media operation");
	}
}
int start_unpublish_local_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::UnpublishLocalTrack;
		task->media_id = id;
		return start_task(L, room, std::move(task));
	} catch (...) {
		return media_error(L, "failed to allocate asynchronous media operation");
	}
}
int open_remote_stream(lua_State* L, bool audio) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	const char* track_sid = luaL_checkstring(L, 3);
	const lua_Integer capacity = luaL_optinteger(L, 4, 8);
	if (capacity <= 0 || capacity > 1024)
		return media_error(L, "stream capacity must be between 1 and 1024");
	if (room->native == nullptr)
		return media_error(L, "room is closed");
	lk_media_stream_options_t options;
	lk_media_stream_options_init(&options);
	options.capacity = static_cast<size_t>(capacity);
	RemoteMediaStream stream;
	const auto status = audio ? lk_room_create_audio_stream(room->native, identity, track_sid,
	                                                        &options, &stream.audio)
	                          : lk_room_create_video_stream(room->native, identity, track_sid,
	                                                        &options, &stream.video);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	try {
		std::lock_guard<std::mutex> lock(room->mutex);
		const uint64_t id = room->next_media_id++;
		room->remote_streams.emplace(id, stream);
		lua_pushnumber(L, static_cast<lua_Number>(id));
		return 1;
	} catch (...) {
		destroy_remote_stream(stream);
		return media_error(L, "failed to store remote stream");
	}
}
int open_audio_stream(lua_State* L) { return open_remote_stream(L, true); }
int open_video_stream(lua_State* L) { return open_remote_stream(L, false); }
int close_remote_stream(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const auto found = room->remote_streams.find(id);
	if (found == room->remote_streams.end())
		return media_error(L, "remote stream is unavailable");
	destroy_remote_stream(found->second);
	room->remote_streams.erase(found);
	lua_pushboolean(L, 1);
	return 1;
}
int remote_stream_is_closed(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const auto found = room->remote_streams.find(id);
	if (found == room->remote_streams.end())
		return media_error(L, "remote stream is unavailable");
	lua_pushboolean(L, found->second.audio != nullptr
	                       ? lk_audio_stream_is_closed(found->second.audio)
	                       : lk_video_stream_is_closed(found->second.video));
	return 1;
}
int remote_stream_dropped_frames(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const auto found = room->remote_streams.find(id);
	if (found == room->remote_streams.end())
		return media_error(L, "remote stream is unavailable");
	const size_t dropped = found->second.audio != nullptr
	                           ? lk_audio_stream_dropped_frames(found->second.audio)
	                           : lk_video_stream_dropped_frames(found->second.video);
	lua_pushnumber(L, static_cast<lua_Number>(dropped));
	return 1;
}
int read_remote_frame(lua_State* L, bool audio) {
	Room* room = check_room(L, 1);
	const uint64_t id = media_id(L, 2);
	const lua_Integer timeout = luaL_optinteger(L, 3, 0);
	if (timeout < 0 || timeout > 60000)
		return media_error(L, "read timeout must be between 0 and 60000 ms");
	const auto found = room->remote_streams.find(id);
	if (found == room->remote_streams.end() ||
	    (audio ? found->second.audio == nullptr : found->second.video == nullptr))
		return media_error(L, "remote stream is unavailable");
	if (audio) {
		lk_owned_audio_frame_t* raw = nullptr;
		const auto status = timeout == 0
		                        ? lk_audio_stream_try_read(found->second.audio, &raw)
		                        : lk_audio_stream_read_for(found->second.audio,
		                                                   static_cast<uint32_t>(timeout), &raw);
		std::unique_ptr<lk_owned_audio_frame_t, decltype(&lk_owned_audio_frame_destroy)> frame(
		    raw, lk_owned_audio_frame_destroy);
		if (status == LK_MEDIA_STREAM_READ_EMPTY)
			return media_error(L, "empty");
		if (status == LK_MEDIA_STREAM_READ_CLOSED)
			return media_error(L, "closed");
		if (status != LK_MEDIA_STREAM_READ_FRAME)
			return media_error(L, safe(lk_last_error()));
		lk_audio_frame_t data{};
		const auto frame_status = lk_owned_audio_frame_data(raw, &data);
		if (frame_status != LK_STATUS_OK)
			return status_result(L, frame_status);
		lua_newtable(L);
		lua_pushlstring(L, data.data != nullptr ? reinterpret_cast<const char*>(data.data) : "",
		                data.sample_count * sizeof(int16_t));
		lua_setfield(L, -2, "data");
		integer_field(L, "sample_rate", data.sample_rate);
		integer_field(L, "channels", data.num_channels);
		integer_field(L, "samples_per_channel", data.samples_per_channel);
		return 1;
	}
	lk_owned_video_frame_t* raw = nullptr;
	const auto status =
	    timeout == 0
	        ? lk_video_stream_try_read(found->second.video, &raw)
	        : lk_video_stream_read_for(found->second.video, static_cast<uint32_t>(timeout), &raw);
	std::unique_ptr<lk_owned_video_frame_t, decltype(&lk_owned_video_frame_destroy)> frame(
	    raw, lk_owned_video_frame_destroy);
	if (status == LK_MEDIA_STREAM_READ_EMPTY)
		return media_error(L, "empty");
	if (status == LK_MEDIA_STREAM_READ_CLOSED)
		return media_error(L, "closed");
	if (status != LK_MEDIA_STREAM_READ_FRAME)
		return media_error(L, safe(lk_last_error()));
	lk_video_frame_t data{};
	const auto frame_status = lk_owned_video_frame_data(raw, &data);
	if (frame_status != LK_STATUS_OK)
		return status_result(L, frame_status);
	lua_newtable(L);
	lua_pushlstring(L, data.data != nullptr ? reinterpret_cast<const char*>(data.data) : "",
	                data.data_size);
	lua_setfield(L, -2, "data");
	integer_field(L, "width", data.width);
	integer_field(L, "height", data.height);
	number_field(L, "timestamp_us", static_cast<lua_Number>(data.timestamp_us));
	lua_pushliteral(L, "I420");
	lua_setfield(L, -2, "format");
	return 1;
}
int read_audio_frame(lua_State* L) { return read_remote_frame(L, true); }
int read_video_frame(lua_State* L) { return read_remote_frame(L, false); }

int set_remote_track_subscribed(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* participant_sid = luaL_checkstring(L, 2);
	const char* track_sid = luaL_checkstring(L, 3);
	luaL_checktype(L, 4, LUA_TBOOLEAN);
	return status_result(L, lk_room_set_remote_track_subscribed(room->native, participant_sid,
	                                                            track_sid, lua_toboolean(L, 4)));
}
int start_set_remote_track_subscribed(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* participant_sid = luaL_checkstring(L, 2);
	const char* track_sid = luaL_checkstring(L, 3);
	luaL_checktype(L, 4, LUA_TBOOLEAN);
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::SetRemoteTrackSubscribed;
		task->first = participant_sid;
		task->second = track_sid;
		task->reliable = lua_toboolean(L, 4) != 0;
		return start_task(L, room, std::move(task));
	} catch (...) {
		return media_error(L, "failed to allocate asynchronous media operation");
	}
}
int update_remote_track_settings(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* participant_sid = luaL_checkstring(L, 2);
	const char* track_sid = luaL_checkstring(L, 3);
	luaL_checktype(L, 4, LUA_TTABLE);
	lk_remote_track_settings_t settings;
	lk_remote_track_settings_init(&settings);
	const int index = 4;
	lua_getfield(L, index, "enabled");
	if (!lua_isnil(L, -1))
		settings.enabled = lua_toboolean(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "video_quality");
	if (!lua_isnil(L, -1)) {
		settings.has_video_quality = 1;
		settings.video_quality = static_cast<lk_video_quality_t>(luaL_checkinteger(L, -1));
	}
	lua_pop(L, 1);
	auto number = [&](const char* key, uint32_t& output) {
		lua_getfield(L, index, key);
		if (!lua_isnil(L, -1)) {
			const lua_Integer value = luaL_checkinteger(L, -1);
			luaL_argcheck(L, value >= 0 && value <= UINT32_MAX, 4,
			              "remote track setting is out of range");
			output = static_cast<uint32_t>(value);
		}
		lua_pop(L, 1);
	};
	number("video_width", settings.video_width);
	number("video_height", settings.video_height);
	number("video_fps", settings.video_fps);
	number("priority", settings.priority);
	return status_result(L, lk_room_update_remote_track_settings(room->native, participant_sid,
	                                                             track_sid, &settings));
}

size_t lua_array_length(lua_State* L, int index) {
#if LUA_VERSION_NUM < 502
	return lua_objlen(L, index);
#else
	return lua_rawlen(L, index);
#endif
}
int set_track_subscription_permissions(lua_State* L) {
	Room* room = check_room(L, 1);
	luaL_checktype(L, 2, LUA_TBOOLEAN);
	luaL_checktype(L, 3, LUA_TTABLE);
	const size_t count = lua_array_length(L, 3);
	std::vector<lk_participant_track_permission_t> permissions(count);
	std::vector<std::vector<const char*>> track_sids(count);
	for (size_t i = 0; i < count; ++i) {
		lua_rawgeti(L, 3, static_cast<int>(i + 1));
		luaL_checktype(L, -1, LUA_TTABLE);
		const int item = lua_gettop(L);
		auto& permission = permissions[i];
		lk_participant_track_permission_init(&permission);
		lua_getfield(L, item, "participant_sid");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TSTRING);
			permission.participant_sid = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
		lua_getfield(L, item, "participant_identity");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TSTRING);
			permission.participant_identity = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
		lua_getfield(L, item, "allow_all");
		if (!lua_isnil(L, -1))
			permission.allow_all = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, item, "allowed_track_sids");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			const int tracks = lua_gettop(L);
			const size_t track_count = lua_array_length(L, tracks);
			track_sids[i].reserve(track_count);
			for (size_t j = 0; j < track_count; ++j) {
				lua_rawgeti(L, tracks, static_cast<int>(j + 1));
				luaL_checktype(L, -1, LUA_TSTRING);
				track_sids[i].push_back(lua_tostring(L, -1));
				lua_pop(L, 1);
			}
			permission.allowed_track_sids = track_sids[i].data();
			permission.allowed_track_sid_count = track_sids[i].size();
		}
		lua_pop(L, 2);
	}
	return status_result(L, lk_room_set_track_subscription_permissions(
	                            room->native, lua_toboolean(L, 2),
	                            permissions.empty() ? nullptr : permissions.data(), count));
}

lk_frame_cryptor_direction_t cryptor_direction(lua_State* L, int argument) {
	const lua_Integer value = luaL_checkinteger(L, argument);
	luaL_argcheck(L,
	              value == LK_FRAME_CRYPTOR_DIRECTION_SENDER ||
	                  value == LK_FRAME_CRYPTOR_DIRECTION_RECEIVER,
	              argument, "invalid frame cryptor direction");
	return static_cast<lk_frame_cryptor_direction_t>(value);
}
int e2ee_set_frame_cryptor_enabled(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* track_sid = luaL_checkstring(L, 2);
	const auto direction = cryptor_direction(L, 3);
	luaL_checktype(L, 4, LUA_TBOOLEAN);
	return status_result(L, lk_room_e2ee_set_frame_cryptor_enabled(room->native, track_sid,
	                                                               direction, lua_toboolean(L, 4)));
}
int e2ee_set_frame_cryptor_key_index(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* track_sid = luaL_checkstring(L, 2);
	const auto direction = cryptor_direction(L, 3);
	return status_result(L, lk_room_e2ee_set_frame_cryptor_key_index(room->native, track_sid,
	                                                                 direction, key_index(L, 4)));
}
int e2ee_set_participant_enabled(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TBOOLEAN);
	size_t count = 0;
	const auto status =
	    lk_room_e2ee_set_participant_enabled(room->native, identity, lua_toboolean(L, 3), &count);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_pushnumber(L, static_cast<lua_Number>(count));
	return 1;
}
int e2ee_frame_cryptors(lua_State* L) {
	Room* room = check_room(L, 1);
	lk_frame_cryptor_list_t* raw = nullptr;
	const auto status = lk_frame_cryptor_list_create(room->native, &raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_frame_cryptor_list_t, decltype(&lk_frame_cryptor_list_destroy)> cryptors(
	    raw, lk_frame_cryptor_list_destroy);
	lua_newtable(L);
	for (size_t i = 0; i < lk_frame_cryptor_list_count(raw); ++i) {
		lk_frame_cryptor_info_t info{};
		info.struct_size = sizeof(info);
		const auto item_status = lk_frame_cryptor_list_info(raw, i, &info);
		if (item_status != LK_STATUS_OK)
			return status_result(L, item_status);
		lua_newtable(L);
		string_field(L, "track_sid", owned_string(lk_frame_cryptor_list_track_id, raw, i));
		string_field(L, "participant_identity",
		             owned_string(lk_frame_cryptor_list_participant_identity, raw, i));
		integer_field(L, "kind", info.kind);
		integer_field(L, "direction", info.direction);
		boolean_field(L, "enabled", info.enabled != 0);
		number_field(L, "key_index", static_cast<lua_Number>(info.key_index));
		integer_field(L, "state", info.state);
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}

int send_chat_message(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* message = luaL_checkstring(L, 2);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	char id[LK_CHAT_MESSAGE_ID_BUFFER_SIZE]{};
	int64_t timestamp = 0;
	const auto status =
	    lk_room_send_chat_message(room->native, message, id, sizeof(id), &timestamp);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_pushstring(L, id);
	lua_pushnumber(L, static_cast<lua_Number>(timestamp));
	return 2;
}
int edit_chat_message(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* id = luaL_checkstring(L, 2);
	const int64_t timestamp = static_cast<int64_t>(luaL_checknumber(L, 3));
	const char* message = luaL_checkstring(L, 4);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	return status_result(L, lk_room_edit_chat_message(room->native, id, timestamp, message));
}

StreamWriterConfig read_stream_writer_config(lua_State* L, bool text, int index = 2) {
	luaL_checktype(L, index, LUA_TTABLE);
	StreamWriterConfig config;
	config.text = text;
	auto string_option = [&](const char* key, std::string& value) {
		lua_getfield(L, index, key);
		if (!lua_isnil(L, -1))
			value = luaL_checkstring(L, -1);
		lua_pop(L, 1);
	};
	string_option("topic", config.topic);
	string_option("mime_type", config.mime_type);
	string_option("name", config.name);
	string_option("stream_id", config.stream_id);
	string_option("reply_to_stream_id", config.reply_to_stream_id);
	auto string_array = [&](const char* key, std::vector<std::string>& values) {
		lua_getfield(L, index, key);
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
#if LUA_VERSION_NUM < 502
			const size_t count = lua_objlen(L, -1);
#else
			const size_t count = lua_rawlen(L, -1);
#endif
			for (size_t i = 1; i <= count; ++i) {
				lua_rawgeti(L, -1, static_cast<int>(i));
				values.emplace_back(luaL_checkstring(L, -1));
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	};
	string_array("destination_identities", config.destinations);
	string_array("attached_stream_ids", config.attached_stream_ids);
	lua_getfield(L, index, "attributes");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TTABLE);
		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			luaL_argcheck(L, lua_type(L, -2) == LUA_TSTRING, index,
			              "attribute keys must be strings");
			const char* key = lua_tostring(L, -2);
			const char* value = luaL_checkstring(L, -1);
			config.attributes.emplace(key, value);
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	lua_getfield(L, index, "total_size");
	if (!lua_isnil(L, -1)) {
		const lua_Integer value = luaL_checkinteger(L, -1);
		luaL_argcheck(L, value >= 0, index, "total_size must be nonnegative");
		config.has_total_size = true;
		config.total_size = static_cast<uint64_t>(value);
	}
	lua_pop(L, 1);
	lua_getfield(L, index, "chunk_size");
	if (!lua_isnil(L, -1)) {
		const lua_Integer value = luaL_checkinteger(L, -1);
		luaL_argcheck(L, value > 0 && value <= 1024 * 1024, index, "chunk_size is out of range");
		config.chunk_size = static_cast<size_t>(value);
	}
	lua_pop(L, 1);
	lua_getfield(L, index, "compress");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TBOOLEAN);
		config.compress = lua_toboolean(L, -1);
	}
	lua_pop(L, 1);
	if (text) {
		lua_getfield(L, index, "update");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TBOOLEAN);
			config.update = lua_toboolean(L, -1);
		}
		lua_pop(L, 1);
		lua_getfield(L, index, "version");
		if (!lua_isnil(L, -1)) {
			const lua_Integer value = luaL_checkinteger(L, -1);
			luaL_argcheck(L, value >= 0 && value <= INT32_MAX, index, "version is out of range");
			config.version = static_cast<int32_t>(value);
		}
		lua_pop(L, 1);
	}
	return config;
}

int open_stream_writer(lua_State* L) {
	Room* room = check_room(L, 1);
	const bool is_text = lua_toboolean(L, 3);
	const auto config = read_stream_writer_config(L, is_text);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	uint64_t id = 0;
	const auto status = open_stream_writer_native(room, config, id);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_pushnumber(L, static_cast<lua_Number>(id));
	return 1;
}

int start_open_stream_writer(lua_State* L) {
	Room* room = check_room(L, 1);
	const bool is_text = lua_toboolean(L, 3);
	auto config = read_stream_writer_config(L, is_text);
	auto task = std::make_shared<AsyncTask>();
	task->operation = AsyncOperation::OpenStreamWriter;
	task->stream_config = std::move(config);
	return start_task(L, room, std::move(task));
}

int stream_writer_operation(lua_State* L, AsyncOperation operation) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	size_t size = 0;
	const char* data =
	    operation == AsyncOperation::CloseStreamWriter ? "" : luaL_checklstring(L, 3, &size);
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	std::string error;
	const auto status =
	    stream_writer_operation_native(room, operation, id, std::string(data, size), error);
	if (!error.empty()) {
		lua_pushnil(L);
		lua_pushlstring(L, error.data(), error.size());
		return 2;
	}
	return status_result(L, status);
}

int start_stream_writer_operation(lua_State* L, AsyncOperation operation) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	size_t size = 0;
	const char* data =
	    operation == AsyncOperation::CloseStreamWriter ? "" : luaL_checklstring(L, 3, &size);
	auto task = std::make_shared<AsyncTask>();
	task->operation = operation;
	task->media_id = id;
	task->first.assign(data, size);
	return start_task(L, room, std::move(task));
}

int stream_writer_write(lua_State* L) {
	return stream_writer_operation(L, AsyncOperation::WriteStreamWriter);
}
int stream_writer_close(lua_State* L) {
	return stream_writer_operation(L, AsyncOperation::CloseStreamWriter);
}
int stream_writer_cancel(lua_State* L) {
	return stream_writer_operation(L, AsyncOperation::CancelStreamWriter);
}
int start_stream_writer_write(lua_State* L) {
	return start_stream_writer_operation(L, AsyncOperation::WriteStreamWriter);
}
int start_stream_writer_close(lua_State* L) {
	return start_stream_writer_operation(L, AsyncOperation::CloseStreamWriter);
}
int start_stream_writer_cancel(lua_State* L) {
	return start_stream_writer_operation(L, AsyncOperation::CancelStreamWriter);
}

int stream_writer_release(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	if (async_busy(room))
		return busy_result(L);
	auto found = room->stream_writers.find(id);
	if (found == room->stream_writers.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "stream writer is unavailable");
		return 2;
	}
	if (found->second.text != nullptr)
		lk_text_stream_writer_destroy(found->second.text);
	if (found->second.bytes != nullptr)
		lk_byte_stream_writer_destroy(found->second.bytes);
	room->stream_writers.erase(found);
	lua_pushboolean(L, 1);
	return 1;
}

int stream_writer_info(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	if (async_busy(room))
		return busy_result(L);
	auto found = room->stream_writers.find(id);
	if (found == room->stream_writers.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "stream writer is unavailable");
		return 2;
	}
	lk_data_stream_writer_info_snapshot_t* raw = nullptr;
	const auto status = found->second.text != nullptr
	                        ? lk_text_stream_writer_create_info_snapshot(found->second.text, &raw)
	                        : lk_byte_stream_writer_create_info_snapshot(found->second.bytes, &raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_data_stream_writer_info_snapshot_t,
	                decltype(&lk_data_stream_writer_info_snapshot_destroy)>
	    snapshot(raw, lk_data_stream_writer_info_snapshot_destroy);
	lk_data_stream_writer_info_t info;
	lk_data_stream_writer_info_init(&info);
	const auto info_status = lk_data_stream_writer_info_snapshot_info(raw, &info);
	if (info_status != LK_STATUS_OK)
		return status_result(L, info_status);
	lua_newtable(L);
	integer_field(L, "kind", info.kind);
	boolean_field(L, "is_closed",
	              found->second.text != nullptr
	                  ? lk_text_stream_writer_is_closed(found->second.text)
	                  : lk_byte_stream_writer_is_closed(found->second.bytes));
	boolean_field(L, "has_total_size", info.has_total_size != 0);
	if (info.has_total_size)
		number_field(L, "total_size", static_cast<lua_Number>(info.total_size));
	number_field(L, "timestamp", static_cast<lua_Number>(info.timestamp));
	string_field(L, "stream_id", owned_string(lk_data_stream_writer_info_snapshot_stream_id, raw));
	string_field(L, "topic", owned_string(lk_data_stream_writer_info_snapshot_topic, raw));
	string_field(L, "mime_type", owned_string(lk_data_stream_writer_info_snapshot_mime_type, raw));
	string_field(L, "participant_identity",
	             owned_string(lk_data_stream_writer_info_snapshot_participant_identity, raw));
	string_field(L, "name", owned_string(lk_data_stream_writer_info_snapshot_name, raw));
	string_field(L, "reply_to_stream_id",
	             owned_string(lk_data_stream_writer_info_snapshot_reply_to_stream_id, raw));
	lua_newtable(L);
	for (size_t i = 0; i < info.attribute_count; ++i) {
		const auto key = owned_string(lk_data_stream_writer_info_snapshot_attribute_key, raw, i);
		const auto value =
		    owned_string(lk_data_stream_writer_info_snapshot_attribute_value, raw, i);
		string_field(L, key.c_str(), value);
	}
	lua_setfield(L, -2, "attributes");
	lua_newtable(L);
	for (size_t i = 0; i < info.attached_stream_id_count; ++i) {
		const auto value =
		    owned_string(lk_data_stream_writer_info_snapshot_attached_stream_id, raw, i);
		lua_pushlstring(L, value.data(), value.size());
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	lua_setfield(L, -2, "attached_stream_ids");
	return 1;
}

int register_text_stream_handler(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* topic = luaL_checkstring(L, 2);
	if (async_busy(room))
		return busy_result(L);
	return status_result(
	    L, lk_room_register_text_stream_handler(room->native, topic, on_text_stream_event, room));
}
int unregister_text_stream_handler(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* topic = luaL_checkstring(L, 2);
	if (async_busy(room))
		return busy_result(L);
	return status_result(L, lk_room_unregister_text_stream_handler(room->native, topic));
}
int register_byte_stream_handler(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* topic = luaL_checkstring(L, 2);
	if (async_busy(room))
		return busy_result(L);
	return status_result(
	    L, lk_room_register_byte_stream_handler(room->native, topic, on_byte_stream_event, room));
}
int unregister_byte_stream_handler(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* topic = luaL_checkstring(L, 2);
	if (async_busy(room))
		return busy_result(L);
	return status_result(L, lk_room_unregister_byte_stream_handler(room->native, topic));
}

int data_track_error(lua_State* L, lk_data_track_error_code_t code) {
	if (code == LK_DATA_TRACK_ERROR_NONE) {
		lua_pushboolean(L, 1);
		return 1;
	}
	lua_pushnil(L);
	lua_pushstring(L, safe(lk_last_error()));
	lua_pushinteger(L, code);
	return 3;
}

SchemaIdConfig read_schema_id(lua_State* L, int index) {
	luaL_checktype(L, index, LUA_TTABLE);
	SchemaIdConfig config;
	lua_getfield(L, index, "name");
	config.name = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "encoding");
	if (!lua_isnil(L, -1))
		config.encoding = static_cast<lk_data_track_schema_encoding_t>(luaL_checkinteger(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, index, "custom_encoding");
	if (!lua_isnil(L, -1))
		config.custom_encoding = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	return config;
}

int store_data_track_schema(lua_State* L) {
	Room* room = check_room(L, 1);
	auto config = read_schema_id(L, 2);
	size_t size = 0;
	const char* definition = luaL_checklstring(L, 3, &size);
	if (async_busy(room))
		return busy_result(L);
	const auto schema = config.value();
	return data_track_error(
	    L, lk_room_store_data_track_schema(room->native, &schema,
	                                       reinterpret_cast<const uint8_t*>(definition), size));
}

int start_store_data_track_schema(lua_State* L) {
	Room* room = check_room(L, 1);
	auto task = std::make_shared<AsyncTask>();
	task->operation = AsyncOperation::StoreDataTrackSchema;
	task->schema_id = read_schema_id(L, 2);
	size_t size = 0;
	const char* definition = luaL_checklstring(L, 3, &size);
	task->first.assign(definition, size);
	return start_task(L, room, std::move(task));
}

int get_data_track_schema(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	auto config = read_schema_id(L, 3);
	if (async_busy(room))
		return busy_result(L);
	const auto schema_id = config.value();
	lk_data_track_schema_t* raw = nullptr;
	const auto code = lk_room_get_data_track_schema(room->native, identity, &schema_id, &raw);
	if (code != LK_DATA_TRACK_ERROR_NONE)
		return data_track_error(L, code);
	std::unique_ptr<lk_data_track_schema_t, decltype(&lk_data_track_schema_destroy)> schema(
	    raw, lk_data_track_schema_destroy);
	lua_newtable(L);
	string_field(L, "name", owned_string(lk_data_track_schema_name, raw));
	integer_field(L, "encoding", lk_data_track_schema_encoding(raw));
	string_field(L, "custom_encoding", owned_string(lk_data_track_schema_custom_encoding, raw));
	const size_t size = lk_data_track_schema_definition(raw, nullptr, 0);
	std::string definition(size, '\0');
	if (size != 0)
		lk_data_track_schema_definition(raw, reinterpret_cast<uint8_t*>(definition.data()), size);
	string_field(L, "definition", definition);
	return 1;
}

int start_get_data_track_schema(lua_State* L) {
	Room* room = check_room(L, 1);
	auto task = std::make_shared<AsyncTask>();
	task->operation = AsyncOperation::GetDataTrackSchema;
	task->first = luaL_checkstring(L, 2);
	task->schema_id = read_schema_id(L, 3);
	return start_task(L, room, std::move(task));
}

DataTrackPublishConfig read_data_track_publish_config(lua_State* L) {
	DataTrackPublishConfig config;
	config.name = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	lua_getfield(L, 3, "frame_encoding");
	if (!lua_isnil(L, -1)) {
		config.has_frame_encoding = true;
		config.frame_encoding =
		    static_cast<lk_data_track_frame_encoding_t>(luaL_checkinteger(L, -1));
	}
	lua_pop(L, 1);
	lua_getfield(L, 3, "custom_frame_encoding");
	if (!lua_isnil(L, -1))
		config.custom_frame_encoding = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, 3, "schema");
	if (!lua_isnil(L, -1)) {
		config.schema = read_schema_id(L, lua_gettop(L));
		config.has_schema = true;
	}
	lua_pop(L, 1);
	return config;
}

int publish_data_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const auto config = read_data_track_publish_config(L);
	if (async_busy(room))
		return busy_result(L);
	uint64_t id = 0;
	const auto code = publish_data_track_native(room, config, id);
	if (code != LK_DATA_TRACK_ERROR_NONE)
		return data_track_error(L, code);
	lua_pushnumber(L, static_cast<lua_Number>(id));
	return 1;
}

int start_publish_data_track(lua_State* L) {
	Room* room = check_room(L, 1);
	auto task = std::make_shared<AsyncTask>();
	task->operation = AsyncOperation::PublishDataTrack;
	task->data_track_publish = read_data_track_publish_config(L);
	return start_task(L, room, std::move(task));
}

int data_track_info(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	if (async_busy(room))
		return busy_result(L);
	auto found = room->data_tracks.local.find(id);
	if (found == room->data_tracks.local.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "local DataTrack is unavailable");
		return 2;
	}
	lk_data_track_snapshot_info_t info;
	lk_data_track_snapshot_info_init(&info);
	const auto status = lk_local_data_track_info(found->second, &info);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_newtable(L);
	string_field(L, "sid", owned_string(lk_local_data_track_sid, found->second));
	string_field(L, "name", owned_string(lk_local_data_track_name, found->second));
	string_field(L, "custom_frame_encoding",
	             owned_string(lk_local_data_track_custom_frame_encoding, found->second));
	string_field(L, "schema_name", owned_string(lk_local_data_track_schema_name, found->second));
	string_field(L, "custom_schema_encoding",
	             owned_string(lk_local_data_track_custom_schema_encoding, found->second));
	boolean_field(L, "is_published", lk_local_data_track_is_published(found->second));
	boolean_field(L, "uses_e2ee", info.uses_e2ee != 0);
	integer_field(L, "publisher_handle", info.publisher_handle);
	if (info.has_frame_encoding)
		integer_field(L, "frame_encoding", info.frame_encoding);
	if (info.has_schema)
		integer_field(L, "schema_encoding", info.schema_encoding);
	return 1;
}

int data_track_push(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	size_t size = 0;
	const char* data = luaL_checklstring(L, 3, &size);
	const bool has_timestamp = !lua_isnoneornil(L, 4);
	uint64_t timestamp = 0;
	if (has_timestamp) {
		const lua_Number value = luaL_checknumber(L, 4);
		luaL_argcheck(L,
		              std::isfinite(value) && value >= 0 && std::floor(value) == value &&
		                  value <= 9007199254740991.0,
		              4, "user_timestamp must be a nonnegative exact integer");
		timestamp = static_cast<uint64_t>(value);
	}
	if (async_busy(room))
		return busy_result(L);
	auto found = room->data_tracks.local.find(id);
	if (found == room->data_tracks.local.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "local DataTrack is unavailable");
		return 2;
	}
	return data_track_error(L, lk_local_data_track_try_push(found->second,
	                                                        reinterpret_cast<const uint8_t*>(data),
	                                                        size, has_timestamp, timestamp));
}

int unpublish_data_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	if (async_busy(room))
		return busy_result(L);
	auto found = room->data_tracks.local.find(id);
	if (found == room->data_tracks.local.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "local DataTrack is unavailable");
		return 2;
	}
	const auto code = lk_local_data_track_unpublish(found->second);
	if (code != LK_DATA_TRACK_ERROR_NONE)
		return data_track_error(L, code);
	const auto destroy_code = lk_local_data_track_destroy(found->second);
	if (destroy_code != LK_DATA_TRACK_ERROR_NONE)
		return data_track_error(L, destroy_code);
	room->data_tracks.local.erase(found);
	lua_pushboolean(L, 1);
	return 1;
}

int remote_data_tracks(lua_State* L) {
	Room* room = check_room(L, 1);
	if (async_busy(room))
		return busy_result(L);
	lk_remote_data_track_list_t* raw = nullptr;
	const auto status = lk_room_create_remote_data_track_snapshot(room->native, &raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_remote_data_track_list_t, decltype(&lk_remote_data_track_list_destroy)> list(
	    raw, lk_remote_data_track_list_destroy);
	lua_newtable(L);
	for (size_t i = 0; i < lk_remote_data_track_list_count(raw); ++i) {
		const lk_remote_data_track_snapshot_t* track = nullptr;
		const auto item_status = lk_remote_data_track_list_at(raw, i, &track);
		if (item_status != LK_STATUS_OK)
			return status_result(L, item_status);
		lk_data_track_snapshot_info_t info;
		lk_data_track_snapshot_info_init(&info);
		const auto info_status = lk_remote_data_track_snapshot_info(track, &info);
		if (info_status != LK_STATUS_OK)
			return status_result(L, info_status);
		lua_newtable(L);
		string_field(L, "participant_identity",
		             owned_string(lk_remote_data_track_snapshot_publisher_identity, track));
		string_field(L, "sid", owned_string(lk_remote_data_track_snapshot_sid, track));
		string_field(L, "name", owned_string(lk_remote_data_track_snapshot_name, track));
		string_field(L, "custom_frame_encoding",
		             owned_string(lk_remote_data_track_snapshot_custom_frame_encoding, track));
		string_field(L, "schema_name",
		             owned_string(lk_remote_data_track_snapshot_schema_name, track));
		string_field(L, "custom_schema_encoding",
		             owned_string(lk_remote_data_track_snapshot_custom_schema_encoding, track));
		integer_field(L, "publisher_handle", info.publisher_handle);
		boolean_field(L, "uses_e2ee", info.uses_e2ee != 0);
		boolean_field(L, "is_published", info.is_published != 0);
		if (info.has_frame_encoding)
			integer_field(L, "frame_encoding", info.frame_encoding);
		if (info.has_schema)
			integer_field(L, "schema_encoding", info.schema_encoding);
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}

lk_data_track_subscription_options_t read_data_track_subscription_options(lua_State* L, int index) {
	lk_data_track_subscription_options_t options;
	lk_data_track_subscription_options_init(&options);
	if (lua_isnoneornil(L, index))
		return options;
	luaL_checktype(L, index, LUA_TTABLE);
	lua_getfield(L, index, "target_fps");
	if (!lua_isnil(L, -1)) {
		const lua_Integer fps = luaL_checkinteger(L, -1);
		luaL_argcheck(L, fps > 0 && fps <= UINT32_MAX, index, "target_fps is out of range");
		options.has_target_fps = 1;
		options.target_fps = static_cast<uint32_t>(fps);
	}
	lua_pop(L, 1);
	for (const auto* key : {"buffer_capacity", "max_partial_frames"}) {
		lua_getfield(L, index, key);
		if (!lua_isnil(L, -1)) {
			const lua_Integer value = luaL_checkinteger(L, -1);
			luaL_argcheck(L, value > 0, index, "subscription capacity must be positive");
			if (std::strcmp(key, "buffer_capacity") == 0)
				options.buffer_capacity = static_cast<size_t>(value);
			else
				options.max_partial_frames = static_cast<size_t>(value);
		}
		lua_pop(L, 1);
	}
	return options;
}

int subscribe_data_track(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	const char* sid = luaL_checkstring(L, 3);
	const auto options = read_data_track_subscription_options(L, 4);
	if (async_busy(room))
		return busy_result(L);
	uint64_t id = 0;
	const auto code = subscribe_data_track_native(room, identity, sid, options, id);
	if (code != LK_DATA_TRACK_ERROR_NONE)
		return data_track_error(L, code);
	lua_pushnumber(L, static_cast<lua_Number>(id));
	return 1;
}

int start_subscribe_data_track(lua_State* L) {
	Room* room = check_room(L, 1);
	auto task = std::make_shared<AsyncTask>();
	task->operation = AsyncOperation::SubscribeDataTrack;
	task->first = luaL_checkstring(L, 2);
	task->second = luaL_checkstring(L, 3);
	task->data_track_subscription = read_data_track_subscription_options(L, 4);
	return start_task(L, room, std::move(task));
}

int update_data_track_subscription(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* identity = luaL_checkstring(L, 2);
	const char* sid = luaL_checkstring(L, 3);
	const auto options = read_data_track_subscription_options(L, 4);
	if (async_busy(room))
		return busy_result(L);
	return data_track_error(
	    L, lk_room_update_data_track_subscription_options(room->native, identity, sid, &options));
}

int read_data_track_frame(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	const lua_Integer timeout = luaL_optinteger(L, 3, 0);
	luaL_argcheck(L, timeout >= 0 && timeout <= UINT32_MAX, 3, "timeout is out of range");
	if (async_busy(room))
		return busy_result(L);
	auto found = room->data_tracks.readers.find(id);
	if (found == room->data_tracks.readers.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "DataTrack reader is unavailable");
		return 2;
	}
	lk_data_track_frame_t* raw = nullptr;
	const auto status =
	    timeout == 0
	        ? lk_data_track_reader_try_read(found->second, &raw)
	        : lk_data_track_reader_read_for(found->second, static_cast<uint32_t>(timeout), &raw);
	if (status != LK_DATA_TRACK_READ_FRAME) {
		lua_pushnil(L);
		lua_pushstring(L, status == LK_DATA_TRACK_READ_EMPTY    ? "empty"
		                  : status == LK_DATA_TRACK_READ_CLOSED ? "closed"
		                                                        : "invalid DataTrack reader");
		return 2;
	}
	std::unique_ptr<lk_data_track_frame_t, decltype(&lk_data_track_frame_destroy)> frame(
	    raw, lk_data_track_frame_destroy);
	const size_t size = lk_data_track_frame_data(raw, nullptr, 0);
	std::string data(size, '\0');
	if (size != 0)
		lk_data_track_frame_data(raw, reinterpret_cast<uint8_t*>(data.data()), size);
	lua_newtable(L);
	string_field(L, "data", data);
	if (lk_data_track_frame_has_user_timestamp(raw))
		number_field(L, "user_timestamp",
		             static_cast<lua_Number>(lk_data_track_frame_user_timestamp(raw)));
	return 1;
}

int close_data_track_reader(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	if (async_busy(room))
		return busy_result(L);
	auto found = room->data_tracks.readers.find(id);
	if (found == room->data_tracks.readers.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "DataTrack reader is unavailable");
		return 2;
	}
	lk_data_track_reader_destroy(found->second);
	room->data_tracks.readers.erase(found);
	lua_pushboolean(L, 1);
	return 1;
}

int data_track_reader_stats(lua_State* L) {
	Room* room = check_room(L, 1);
	const uint64_t id = static_cast<uint64_t>(luaL_checknumber(L, 2));
	if (async_busy(room))
		return busy_result(L);
	auto found = room->data_tracks.readers.find(id);
	if (found == room->data_tracks.readers.end()) {
		lua_pushnil(L);
		lua_pushliteral(L, "DataTrack reader is unavailable");
		return 2;
	}
	lua_newtable(L);
	boolean_field(L, "is_closed", lk_data_track_reader_is_closed(found->second));
	number_field(L, "dropped_frames",
	             static_cast<lua_Number>(lk_data_track_reader_dropped_frames(found->second)));
	return 1;
}

int send_text(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* text = luaL_checkstring(L, 2);
	const char* topic = luaL_optstring(L, 3, "");
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	lk_text_send_options_t options;
	lk_text_send_options_init(&options);
	options.topic = topic;
	return status_result(L, lk_room_send_text(room->native, text, &options));
}
int send_bytes(lua_State* L) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* data = luaL_checklstring(L, 2, &size);
	const char* topic = luaL_optstring(L, 3, "");
	const char* mime_type = luaL_optstring(L, 4, "application/octet-stream");
	const char* name = luaL_optstring(L, 5, "");
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	lk_byte_send_options_t options;
	lk_byte_send_options_init(&options);
	options.topic = topic;
	options.mime_type = mime_type;
	options.name = name;
	return status_result(L, lk_room_send_bytes(room->native, reinterpret_cast<const uint8_t*>(data),
	                                           size, &options));
}
int send_file(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* path = luaL_checkstring(L, 2);
	const char* topic = luaL_optstring(L, 3, "");
	const char* mime_type = luaL_optstring(L, 4, "application/octet-stream");
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	lk_file_send_options_t options;
	lk_file_send_options_init(&options);
	options.topic = topic;
	options.mime_type = mime_type;
	return status_result(L, lk_room_send_file(room->native, path, &options));
}
int send_with_options(lua_State* L, AsyncOperation operation, bool async) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* data = luaL_checklstring(L, 2, &size);
	const auto config = read_stream_writer_config(L, operation == AsyncOperation::Text, 3);
	if (async) {
		try {
			auto task = std::make_shared<AsyncTask>();
			task->operation = operation;
			task->first.assign(data, size);
			task->stream_config = config;
			return start_task(L, room, std::move(task));
		} catch (...) {
			return media_error(L, "failed to allocate asynchronous stream operation");
		}
	}
	if (room->native == nullptr)
		return media_error(L, "room is closed");
	if (async_busy(room))
		return busy_result(L);
	return status_result(
	    L, send_stream_one_shot_native(room, operation, std::string(data, size), config));
}
int send_text_with_options(lua_State* L) {
	return send_with_options(L, AsyncOperation::Text, false);
}
int start_text_with_options(lua_State* L) {
	return send_with_options(L, AsyncOperation::Text, true);
}
int send_bytes_with_options(lua_State* L) {
	return send_with_options(L, AsyncOperation::Bytes, false);
}
int start_bytes_with_options(lua_State* L) {
	return send_with_options(L, AsyncOperation::Bytes, true);
}
int send_file_with_options(lua_State* L) {
	return send_with_options(L, AsyncOperation::File, false);
}
int start_file_with_options(lua_State* L) {
	return send_with_options(L, AsyncOperation::File, true);
}
int start_chat(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* message = luaL_checkstring(L, 2);
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::Chat;
		task->first = message;
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}
int start_text(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* value = luaL_checkstring(L, 2);
	const char* topic = luaL_optstring(L, 3, "");
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::Text;
		task->first = value;
		task->topic = topic;
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}
int start_bytes(lua_State* L) {
	Room* room = check_room(L, 1);
	size_t size = 0;
	const char* data = luaL_checklstring(L, 2, &size);
	const char* topic = luaL_optstring(L, 3, "");
	const char* mime_type = luaL_optstring(L, 4, "application/octet-stream");
	const char* name = luaL_optstring(L, 5, "");
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::Bytes;
		task->first.assign(data, size);
		task->topic = topic;
		task->mime_type = mime_type;
		task->name = name;
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}
int start_file(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* path = luaL_checkstring(L, 2);
	const char* topic = luaL_optstring(L, 3, "");
	const char* mime_type = luaL_optstring(L, 4, "application/octet-stream");
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::File;
		task->first = path;
		task->topic = topic;
		task->mime_type = mime_type;
		return start_task(L, room, std::move(task));
	} catch (...) {
		lua_pushnil(L);
		lua_pushliteral(L, "failed to allocate asynchronous operation");
		return 2;
	}
}
int publish_dtmf(lua_State* L) {
	Room* room = check_room(L, 1);
	const lua_Integer code = luaL_checkinteger(L, 2);
	const char* digit = luaL_checkstring(L, 3);
	if (code < 0 || code > UINT32_MAX) {
		lua_pushnil(L);
		lua_pushliteral(L, "DTMF code is out of range");
		return 2;
	}
	if (room->native == nullptr) {
		lua_pushnil(L);
		lua_pushliteral(L, "room is closed");
		return 2;
	}
	if (async_busy(room))
		return busy_result(L);
	return status_result(L, lk_room_publish_dtmf(room->native, static_cast<uint32_t>(code), digit));
}

int list_media_devices(lua_State* L) {
	lk_media_device_list_t* raw = nullptr;
	const auto status = lk_media_device_list_create(&raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_media_device_list_t, decltype(&lk_media_device_list_destroy)> devices(
	    raw, lk_media_device_list_destroy);
	lua_newtable(L);
	const size_t count = lk_media_device_list_count(raw);
	for (size_t i = 0; i < count; ++i) {
		lk_media_device_info_t info{};
		info.struct_size = sizeof(info);
		if (lk_media_device_list_info(raw, i, &info) != LK_STATUS_OK)
			return status_result(L, LK_STATUS_OPERATION_FAILED);
		lua_newtable(L);
		string_field(L, "id", owned_string(lk_media_device_list_id, raw, i));
		string_field(L, "label", owned_string(lk_media_device_list_label, raw, i));
		integer_field(L, "kind", info.kind);
		boolean_field(L, "is_default", info.is_default != 0);
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}
int list_screen_sources(lua_State* L) {
	lk_screen_source_list_t* raw = nullptr;
	const auto status = lk_screen_source_list_create(&raw);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	std::unique_ptr<lk_screen_source_list_t, decltype(&lk_screen_source_list_destroy)> sources(
	    raw, lk_screen_source_list_destroy);
	lua_newtable(L);
	const size_t count = lk_screen_source_list_count(raw);
	for (size_t i = 0; i < count; ++i) {
		lk_screen_source_info_t info{};
		info.struct_size = sizeof(info);
		if (lk_screen_source_list_info(raw, i, &info) != LK_STATUS_OK)
			return status_result(L, LK_STATUS_OPERATION_FAILED);
		lua_newtable(L);
		string_field(L, "id", owned_string(lk_screen_source_list_id, raw, i));
		string_field(L, "label", owned_string(lk_screen_source_list_label, raw, i));
		integer_field(L, "kind", info.kind);
		integer_field(L, "x", info.x);
		integer_field(L, "y", info.y);
		integer_field(L, "width", info.width);
		integer_field(L, "height", info.height);
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}
int dropped_events(lua_State* L) {
	Room* room = check_room(L, 1);
	std::lock_guard<std::mutex> lock(room->mutex);
	lua_pushnumber(L, static_cast<lua_Number>(room->dropped));
	return 1;
}
int version(lua_State* L) {
	size_t required = lk_version(nullptr, 0);
	std::string value(required, '\0');
	if (required != 0)
		lk_version(&value[0], value.size());
	lua_pushlstring(L, value.data(), required > 0 ? required - 1 : 0);
	return 1;
}

int log_options(lua_State* L) {
	lk_log_options_t options;
	lk_log_options_init(&options);
	const lk_status_t status = lk_log_get_options(&options);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_newtable(L);
	integer_field(L, "livekit_level", options.livekit_level);
	integer_field(L, "webrtc_level", options.webrtc_level);
	integer_field(L, "websocket_level", options.websocket_level);
	return 1;
}

int set_log_options(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lk_log_options_t options;
	lk_log_options_init(&options);
	const lk_status_t status = lk_log_get_options(&options);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	for (const auto* field : {"livekit_level", "webrtc_level", "websocket_level"}) {
		lua_getfield(L, 1, field);
		if (!lua_isnil(L, -1)) {
			const auto level = static_cast<lk_log_level_t>(luaL_checkinteger(L, -1));
			if (std::strcmp(field, "livekit_level") == 0)
				options.livekit_level = level;
			else if (std::strcmp(field, "webrtc_level") == 0)
				options.webrtc_level = level;
			else
				options.websocket_level = level;
		}
		lua_pop(L, 1);
	}
	return status_result(L, lk_log_set_options(&options));
}

int trace_options(lua_State* L) {
	lk_trace_options_t options;
	lk_trace_options_init(&options);
	const lk_status_t status = lk_trace_get_options(&options);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_newtable(L);
	boolean_field(L, "enabled", options.enabled != 0);
	integer_field(L, "category_mask", static_cast<lua_Integer>(options.category_mask));
	return 1;
}

int set_trace_options(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lk_trace_options_t options;
	lk_trace_options_init(&options);
	const lk_status_t status = lk_trace_get_options(&options);
	if (status != LK_STATUS_OK)
		return status_result(L, status);
	lua_getfield(L, 1, "enabled");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TBOOLEAN);
		options.enabled = lua_toboolean(L, -1);
	}
	lua_pop(L, 1);
	lua_getfield(L, 1, "category_mask");
	if (!lua_isnil(L, -1)) {
		const lua_Integer mask = luaL_checkinteger(L, -1);
		if (mask < 0)
			return luaL_argerror(L, 1, "category_mask must be nonnegative");
		options.category_mask = static_cast<uint64_t>(mask);
	}
	lua_pop(L, 1);
	return status_result(L, lk_trace_set_options(&options));
}

int trace_start_json_file(lua_State* L) {
	return status_result(L, lk_trace_start_json_file(luaL_checkstring(L, 1)));
}

int trace_stop(lua_State* L) { return status_result(L, lk_trace_stop()); }

const luaL_Reg room_methods[] = {
    {"on", on},
    {"register_rpc_method", register_rpc_method},
    {"unregister_rpc_method", unregister_rpc_method},
    {"poll", poll},
    {"wait", wait_room},
    {"connect", connect_room},
    {"_start_connect", start_connect},
    {"disconnect", disconnect_room},
    {"_start_disconnect", start_disconnect},
    {"close", close_room},
    {"publish_data", publish_data},
    {"publish_data_with_options", publish_data_with_options},
    {"_start_publish_data_with_options", start_publish_data_with_options},
    {"publish_audio_track", publish_audio_track},
    {"_start_publish_audio_track", start_publish_audio_track},
    {"publish_video_track", publish_video_track},
    {"_start_publish_video_track", start_publish_video_track},
    {"publish_capture_track", publish_capture_track},
    {"_start_publish_capture_track", start_publish_capture_track},
    {"control_capture", control_capture},
    {"_start_control_capture", start_control_capture},
    {"capture_is_running", capture_is_running},
    {"capture_source_id", capture_source_id},
    {"microphone_is_muted", microphone_is_muted},
    {"microphone_set_muted", microphone_set_muted},
    {"microphone_volume", microphone_volume},
    {"microphone_set_volume", microphone_set_volume},
    {"microphone_processing_options", microphone_processing_options},
    {"microphone_set_processing_options", microphone_set_processing_options},
    {"microphone_processing_stats", microphone_processing_stats},
    {"push_audio_frame", push_audio_frame},
    {"push_video_frame", push_video_frame},
    {"audio_source_queued_duration_ms", audio_source_queued_duration_ms},
    {"clear_audio_source_queue", clear_audio_source_queue},
    {"wait_audio_source_playout", wait_audio_source_playout},
    {"_start_wait_audio_source_playout", start_wait_audio_source_playout},
    {"update_video_encoding", update_video_encoding},
    {"update_video_degradation_preference", update_video_degradation_preference},
    {"set_local_track_muted", set_local_track_muted},
    {"unpublish_local_track", unpublish_local_track},
    {"_start_unpublish_local_track", start_unpublish_local_track},
    {"open_audio_stream", open_audio_stream},
    {"open_video_stream", open_video_stream},
    {"read_audio_frame", read_audio_frame},
    {"read_video_frame", read_video_frame},
    {"close_remote_stream", close_remote_stream},
    {"remote_stream_is_closed", remote_stream_is_closed},
    {"remote_stream_dropped_frames", remote_stream_dropped_frames},
    {"local_track_rtc_stats", local_track_rtc_stats},
    {"remote_track_rtc_stats", remote_track_rtc_stats},
    {"_start_set_remote_track_subscribed", start_set_remote_track_subscribed},
    {"_start_publish_data", start_publish_data},
    {"_async_result", async_result},
    {"state", state},
    {"is_connected", is_connected},
    {"sid", sid},
    {"name", name},
    {"metadata", metadata},
    {"disconnect_reason", disconnect_reason},
    {"is_recording", is_recording},
    {"e2ee_is_configured", e2ee_is_configured},
    {"e2ee_is_enabled", e2ee_is_enabled},
    {"e2ee_set_enabled", e2ee_set_enabled},
    {"e2ee_set_shared_key", e2ee_set_shared_key},
    {"e2ee_export_shared_key", e2ee_export_shared_key},
    {"e2ee_ratchet_shared_key", e2ee_ratchet_shared_key},
    {"e2ee_remove_shared_key", e2ee_remove_shared_key},
    {"e2ee_set_participant_key", e2ee_set_participant_key},
    {"e2ee_export_participant_key", e2ee_export_participant_key},
    {"e2ee_ratchet_participant_key", e2ee_ratchet_participant_key},
    {"e2ee_remove_participant_key", e2ee_remove_participant_key},
    {"e2ee_remove_participant_keys", e2ee_remove_participant_keys},
    {"e2ee_clear_keys", e2ee_clear_keys},
    {"e2ee_data_key_index", e2ee_data_key_index},
    {"e2ee_set_data_key_index", e2ee_set_data_key_index},
    {"e2ee_set_frame_cryptor_enabled", e2ee_set_frame_cryptor_enabled},
    {"e2ee_set_frame_cryptor_key_index", e2ee_set_frame_cryptor_key_index},
    {"e2ee_set_participant_enabled", e2ee_set_participant_enabled},
    {"e2ee_frame_cryptors", e2ee_frame_cryptors},
    {"local_participant", local_participant},
    {"remote_participants", remote_participants},
    {"local_sid", local_sid},
    {"local_identity", local_identity},
    {"local_name", local_name},
    {"local_metadata", local_metadata},
    {"set_local_name", set_local_name},
    {"set_local_metadata", set_local_metadata},
    {"set_local_attributes", set_local_attributes},
    {"audio_output_device", audio_output_device},
    {"set_audio_output_device", set_audio_output_device},
    {"speaker_volume", speaker_volume},
    {"set_speaker_volume", set_speaker_volume},
    {"speaker_is_muted", speaker_is_muted},
    {"set_speaker_muted", set_speaker_muted},
    {"audio_playback_stats", audio_playback_stats},
    {"set_remote_track_subscribed", set_remote_track_subscribed},
    {"update_remote_track_settings", update_remote_track_settings},
    {"set_track_subscription_permissions", set_track_subscription_permissions},
    {"send_chat_message", send_chat_message},
    {"_start_chat", start_chat},
    {"edit_chat_message", edit_chat_message},
    {"_open_stream_writer", open_stream_writer},
    {"_start_open_stream_writer", start_open_stream_writer},
    {"stream_writer_write", stream_writer_write},
    {"stream_writer_close", stream_writer_close},
    {"stream_writer_cancel", stream_writer_cancel},
    {"_start_stream_writer_write", start_stream_writer_write},
    {"_start_stream_writer_close", start_stream_writer_close},
    {"_start_stream_writer_cancel", start_stream_writer_cancel},
    {"stream_writer_release", stream_writer_release},
    {"stream_writer_info", stream_writer_info},
    {"register_text_stream_handler", register_text_stream_handler},
    {"unregister_text_stream_handler", unregister_text_stream_handler},
    {"register_byte_stream_handler", register_byte_stream_handler},
    {"unregister_byte_stream_handler", unregister_byte_stream_handler},
    {"store_data_track_schema", store_data_track_schema},
    {"_start_store_data_track_schema", start_store_data_track_schema},
    {"get_data_track_schema", get_data_track_schema},
    {"_start_get_data_track_schema", start_get_data_track_schema},
    {"publish_data_track", publish_data_track},
    {"_start_publish_data_track", start_publish_data_track},
    {"data_track_info", data_track_info},
    {"data_track_push", data_track_push},
    {"unpublish_data_track", unpublish_data_track},
    {"remote_data_tracks", remote_data_tracks},
    {"subscribe_data_track", subscribe_data_track},
    {"_start_subscribe_data_track", start_subscribe_data_track},
    {"update_data_track_subscription", update_data_track_subscription},
    {"read_data_track_frame", read_data_track_frame},
    {"close_data_track_reader", close_data_track_reader},
    {"data_track_reader_stats", data_track_reader_stats},
    {"send_text", send_text},
    {"_start_text", start_text},
    {"send_text_with_options", send_text_with_options},
    {"_start_text_with_options", start_text_with_options},
    {"send_bytes", send_bytes},
    {"_start_bytes", start_bytes},
    {"send_bytes_with_options", send_bytes_with_options},
    {"_start_bytes_with_options", start_bytes_with_options},
    {"send_file", send_file},
    {"_start_file", start_file},
    {"send_file_with_options", send_file_with_options},
    {"_start_file_with_options", start_file_with_options},
    {"publish_dtmf", publish_dtmf},
    {"perform_rpc", perform_rpc},
    {"_start_rpc", start_rpc},
    {"dropped_events", dropped_events},
    {nullptr, nullptr}};
const luaL_Reg module_methods[] = {{"new_room", new_room},
                                   {"version", version},
                                   {"log_options", log_options},
                                   {"set_log_options", set_log_options},
                                   {"trace_options", trace_options},
                                   {"set_trace_options", set_trace_options},
                                   {"trace_start_json_file", trace_start_json_file},
                                   {"trace_stop", trace_stop},
                                   {"list_media_devices", list_media_devices},
                                   {"list_screen_sources", list_screen_sources},
                                   {nullptr, nullptr}};

void register_functions(lua_State* L, const luaL_Reg* methods) {
#if LUA_VERSION_NUM < 502
	luaL_register(L, nullptr, methods);
#else
	luaL_setfuncs(L, methods, 0);
#endif
}

} // namespace

#if defined(_WIN32)
#define LIVEKIT_LUA_EXPORT __declspec(dllexport)
#else
#define LIVEKIT_LUA_EXPORT
#endif

extern "C" LIVEKIT_LUA_EXPORT int luaopen_livekit_client_native(lua_State* L) {
	lk_status_t status = lk_init();
	if (status != LK_STATUS_OK)
		return luaL_error(L, "LiveKit initialization failed: %s", safe(lk_last_error()));
	luaL_newmetatable(L, kRoomType);
	lua_pushcfunction(L, gc_room);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	register_functions(L, room_methods);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	lua_newtable(L);
	register_functions(L, module_methods);
	luaL_getmetatable(L, kRoomType);
	lua_getfield(L, -1, "__index");
	lua_setfield(L, -3, "_room_methods");
	lua_pop(L, 1);
	lua_pushinteger(L, LK_ROOM_STATE_CONNECTING);
	lua_setfield(L, -2, "CONNECTING");
	lua_pushinteger(L, LK_ROOM_STATE_CONNECTED);
	lua_setfield(L, -2, "CONNECTED");
	lua_pushinteger(L, LK_ROOM_STATE_DISCONNECTING);
	lua_setfield(L, -2, "DISCONNECTING");
	lua_pushinteger(L, LK_ROOM_STATE_DISCONNECTED);
	lua_setfield(L, -2, "DISCONNECTED");
	lua_pushinteger(L, LK_ROOM_STATE_FAILED);
	lua_setfield(L, -2, "FAILED");
	lua_pushinteger(L, LK_ROOM_STATE_RECONNECTING);
	lua_setfield(L, -2, "RECONNECTING");
	for (const auto& level : {std::pair{"LOG_TRACE", LK_LOG_LEVEL_TRACE},
	                          {"LOG_DEBUG", LK_LOG_LEVEL_DEBUG},
	                          {"LOG_INFO", LK_LOG_LEVEL_INFO},
	                          {"LOG_WARNING", LK_LOG_LEVEL_WARNING},
	                          {"LOG_ERROR", LK_LOG_LEVEL_ERROR},
	                          {"LOG_OFF", LK_LOG_LEVEL_OFF}}) {
		lua_pushinteger(L, level.second);
		lua_setfield(L, -2, level.first);
	}
	for (const auto& category : {std::pair{"TRACE_LIFECYCLE", LK_TRACE_CATEGORY_LIFECYCLE},
	                             {"TRACE_SIGNALING", LK_TRACE_CATEGORY_SIGNALING},
	                             {"TRACE_TRANSPORT", LK_TRACE_CATEGORY_TRANSPORT},
	                             {"TRACE_TRACK", LK_TRACE_CATEGORY_TRACK},
	                             {"TRACE_DATA", LK_TRACE_CATEGORY_DATA},
	                             {"TRACE_RPC", LK_TRACE_CATEGORY_RPC},
	                             {"TRACE_E2EE", LK_TRACE_CATEGORY_E2EE},
	                             {"TRACE_ALL", LK_TRACE_CATEGORY_ALL}}) {
		lua_pushinteger(L, category.second);
		lua_setfield(L, -2, category.first);
	}
	lua_newtable(L);
	for (const auto& encoding : {std::pair{"UNSPECIFIED", LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED},
	                             {"ROS1", LK_DATA_TRACK_FRAME_ENCODING_ROS1},
	                             {"CDR", LK_DATA_TRACK_FRAME_ENCODING_CDR},
	                             {"PROTOBUF", LK_DATA_TRACK_FRAME_ENCODING_PROTOBUF},
	                             {"FLATBUFFER", LK_DATA_TRACK_FRAME_ENCODING_FLATBUFFER},
	                             {"CBOR", LK_DATA_TRACK_FRAME_ENCODING_CBOR},
	                             {"MSGPACK", LK_DATA_TRACK_FRAME_ENCODING_MSGPACK},
	                             {"JSON", LK_DATA_TRACK_FRAME_ENCODING_JSON},
	                             {"CUSTOM", LK_DATA_TRACK_FRAME_ENCODING_CUSTOM}}) {
		lua_pushinteger(L, encoding.second);
		lua_setfield(L, -2, encoding.first);
	}
	lua_setfield(L, -2, "DATA_TRACK_FRAME_ENCODING");
	lua_newtable(L);
	for (const auto& encoding :
	     {std::pair{"UNSPECIFIED", LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED},
	      {"PROTOBUF", LK_DATA_TRACK_SCHEMA_ENCODING_PROTOBUF},
	      {"FLATBUFFER", LK_DATA_TRACK_SCHEMA_ENCODING_FLATBUFFER},
	      {"ROS1_MESSAGE", LK_DATA_TRACK_SCHEMA_ENCODING_ROS1_MESSAGE},
	      {"ROS2_MESSAGE", LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_MESSAGE},
	      {"ROS2_IDL", LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_IDL},
	      {"OMG_IDL", LK_DATA_TRACK_SCHEMA_ENCODING_OMG_IDL},
	      {"JSON_SCHEMA", LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA},
	      {"CUSTOM", LK_DATA_TRACK_SCHEMA_ENCODING_CUSTOM}}) {
		lua_pushinteger(L, encoding.second);
		lua_setfield(L, -2, encoding.first);
	}
	lua_setfield(L, -2, "DATA_TRACK_SCHEMA_ENCODING");
	lua_newtable(L);
	lua_pushinteger(L, LK_CONTINUAL_GATHERING_POLICY_GATHER_ONCE);
	lua_setfield(L, -2, "GATHER_ONCE");
	lua_pushinteger(L, LK_CONTINUAL_GATHERING_POLICY_GATHER_CONTINUALLY);
	lua_setfield(L, -2, "GATHER_CONTINUALLY");
	lua_setfield(L, -2, "CONTINUAL_GATHERING_POLICY");
	lua_newtable(L);
	for (const auto& transport : {std::pair{"NONE", LK_ICE_TRANSPORT_TYPE_NONE},
	                              {"RELAY", LK_ICE_TRANSPORT_TYPE_RELAY},
	                              {"NO_HOST", LK_ICE_TRANSPORT_TYPE_NO_HOST},
	                              {"ALL", LK_ICE_TRANSPORT_TYPE_ALL}}) {
		lua_pushinteger(L, transport.second);
		lua_setfield(L, -2, transport.first);
	}
	lua_setfield(L, -2, "ICE_TRANSPORT_TYPE");
	lua_newtable(L);
	for (const auto& preference :
	     {std::pair{"MAINTAIN_FRAMERATE", LK_VIDEO_DEGRADATION_PREFERENCE_MAINTAIN_FRAMERATE},
	      {"MAINTAIN_RESOLUTION", LK_VIDEO_DEGRADATION_PREFERENCE_MAINTAIN_RESOLUTION},
	      {"BALANCED", LK_VIDEO_DEGRADATION_PREFERENCE_BALANCED},
	      {"DISABLED", LK_VIDEO_DEGRADATION_PREFERENCE_DISABLED}}) {
		lua_pushinteger(L, preference.second);
		lua_setfield(L, -2, preference.first);
	}
	lua_setfield(L, -2, "VIDEO_DEGRADATION_PREFERENCE");
	return 1;
}
