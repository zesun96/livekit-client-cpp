#include <livekit/capi/livekit.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
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
};

enum class AsyncOperation { Connect, Disconnect, PublishData, Rpc, Chat, Text, Bytes, File };

struct ConnectConfig {
	lk_room_connect_options_t options{};
	lk_e2ee_options_t e2ee{};
	bool has_e2ee = false;
	std::string shared_key;
	std::string ratchet_salt;
	std::string unencrypted_magic_bytes;

	void prepare() {
		if (!has_e2ee)
			return;
		e2ee.shared_key =
		    shared_key.empty() ? nullptr : reinterpret_cast<const uint8_t*>(shared_key.data());
		e2ee.shared_key_size = shared_key.size();
		e2ee.ratchet_salt =
		    ratchet_salt.empty() ? nullptr : reinterpret_cast<const uint8_t*>(ratchet_salt.data());
		e2ee.ratchet_salt_size = ratchet_salt.size();
		e2ee.unencrypted_magic_bytes =
		    unencrypted_magic_bytes.empty()
		        ? nullptr
		        : reinterpret_cast<const uint8_t*>(unencrypted_magic_bytes.data());
		e2ee.unencrypted_magic_bytes_size = unencrypted_magic_bytes.size();
		options.e2ee_options = &e2ee;
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

struct Room {
	lk_room_t* native = nullptr;
	int callback_ref = LUA_NOREF;
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<Event> events;
	size_t dropped = 0;
	std::thread worker;
	std::deque<std::shared_ptr<AsyncTask>> pending;
	std::map<uint64_t, std::shared_ptr<AsyncTask>> tasks;
	uint64_t next_task_id = 1;
	size_t completed_tasks = 0;
	bool running = false;
	bool stopping = false;
};

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
				options.topic = task->topic.c_str();
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
			case AsyncOperation::Text: {
				lk_text_send_options_t options;
				lk_text_send_options_init(&options);
				options.topic = task->topic.c_str();
				status = lk_room_send_text(room->native, task->first.c_str(), &options);
				break;
			}
			case AsyncOperation::Bytes: {
				lk_byte_send_options_t options;
				lk_byte_send_options_init(&options);
				options.topic = task->topic.c_str();
				options.mime_type = task->mime_type.c_str();
				options.name = task->name.c_str();
				status = lk_room_send_bytes(room->native,
				                            reinterpret_cast<const uint8_t*>(task->first.data()),
				                            task->first.size(), &options);
				break;
			}
			case AsyncOperation::File: {
				lk_file_send_options_t options;
				lk_file_send_options_init(&options);
				options.topic = task->topic.c_str();
				options.mime_type = task->mime_type.c_str();
				status = lk_room_send_file(room->native, task->first.c_str(), &options);
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
		if (participant != nullptr)
			event.strings.emplace_back("participant_identity", safe(participant->identity));
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
void on_local_track_published(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                              const lk_participant_info_t* p) {
	track_event(u, t, p, "local_track_published");
}
void on_local_track_unpublished(void* u, lk_room_t*, const lk_track_publication_info_t* t,
                                const lk_participant_info_t* p) {
	track_event(u, t, p, "local_track_unpublished");
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
			room->wake.notify_all();
		}
		if (room->worker.joinable())
			room->worker.join();
		lk_room_destroy(room->native);
		room->native = nullptr;
	}
	if (room->callback_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, room->callback_ref);
		room->callback_ref = LUA_NOREF;
	}
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
	callbacks.on_participant_connected = on_participant_connected;
	callbacks.on_participant_disconnected = on_participant_disconnected;
	callbacks.on_data_received = on_data;
	callbacks.on_chat_message_received = on_chat;
	callbacks.on_text_received = on_text;
	callbacks.on_file_received = on_file;
	callbacks.on_byte_received = on_byte;
	callbacks.on_room_metadata_changed = on_room_metadata;
	callbacks.on_recording_status_changed = on_recording_status;
	callbacks.on_encryption_state_changed = on_encryption_state;
	callbacks.on_participant_metadata_changed = on_participant_metadata;
	callbacks.on_participant_name_changed = on_participant_name;
	callbacks.on_participant_attributes_changed = on_participant_attributes;
	callbacks.on_track_published = on_track_published;
	callbacks.on_track_unpublished = on_track_unpublished;
	callbacks.on_track_subscribed = on_track_subscribed;
	callbacks.on_track_unsubscribed = on_track_unsubscribed;
	callbacks.on_local_track_published = on_local_track_published;
	callbacks.on_local_track_unpublished = on_local_track_unpublished;
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

void field(lua_State* L, const char* key, const std::string& value) {
	lua_pushlstring(L, value.data(), value.size());
	lua_setfield(L, -2, key);
}

int poll(lua_State* L) {
	Room* room = check_room(L, 1);
	lua_Integer requested = luaL_optinteger(L, 2, 100);
	if (requested < 0)
		return luaL_argerror(L, 2, "must be nonnegative");
	const int limit = static_cast<int>(requested > 1024 ? 1024 : requested);
	int delivered = 0;
	while (delivered < limit) {
		Event event;
		{
			std::lock_guard<std::mutex> lock(room->mutex);
			if (room->events.empty())
				break;
			event = std::move(room->events.front());
			room->events.pop_front();
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
			if (event.type == "participant_attributes_changed") {
				lua_newtable(L);
				for (const auto& [key, value] : event.attributes)
					string_field(L, key.c_str(), value);
				lua_setfield(L, -2, "changes");
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
		else if (task->operation == AsyncOperation::Chat) {
			lua_newtable(L);
			string_field(L, "id", task->chat_id);
			number_field(L, "timestamp", static_cast<lua_Number>(task->chat_timestamp));
		} else
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
		return !room->events.empty() || room->completed_tasks != 0 || room->stopping;
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

int set_remote_track_subscribed(lua_State* L) {
	Room* room = check_room(L, 1);
	const char* participant_sid = luaL_checkstring(L, 2);
	const char* track_sid = luaL_checkstring(L, 3);
	luaL_checktype(L, 4, LUA_TBOOLEAN);
	return status_result(L, lk_room_set_remote_track_subscribed(room->native, participant_sid,
	                                                            track_sid, lua_toboolean(L, 4)));
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

const luaL_Reg room_methods[] = {
    {"on", on},
    {"poll", poll},
    {"wait", wait_room},
    {"connect", connect_room},
    {"_start_connect", start_connect},
    {"disconnect", disconnect_room},
    {"_start_disconnect", start_disconnect},
    {"close", close_room},
    {"publish_data", publish_data},
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
    {"send_chat_message", send_chat_message},
    {"_start_chat", start_chat},
    {"edit_chat_message", edit_chat_message},
    {"send_text", send_text},
    {"_start_text", start_text},
    {"send_bytes", send_bytes},
    {"_start_bytes", start_bytes},
    {"send_file", send_file},
    {"_start_file", start_file},
    {"publish_dtmf", publish_dtmf},
    {"perform_rpc", perform_rpc},
    {"_start_rpc", start_rpc},
    {"dropped_events", dropped_events},
    {nullptr, nullptr}};
const luaL_Reg module_methods[] = {{"new_room", new_room},
                                   {"version", version},
                                   {"list_media_devices", list_media_devices},
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
	return 1;
}
