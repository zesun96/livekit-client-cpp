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
#include <utility>

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
};

enum class AsyncOperation { Connect, Disconnect, PublishData };

struct AsyncTask {
	uint64_t id = 0;
	AsyncOperation operation = AsyncOperation::Connect;
	std::string first;
	std::string second;
	std::string topic;
	lk_room_connect_options_t connect_options{};
	bool reliable = true;
	bool done = false;
	lk_status_t status = LK_STATUS_INVALID_STATE;
	std::string error;
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
				status = lk_room_connect_with_options(room->native, task->first.c_str(),
				                                      task->second.c_str(), &task->connect_options);
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
			}
			if (status != LK_STATUS_OK)
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

void read_connect_options(lua_State* L, lk_room_connect_options_t& options) {
	lk_room_connect_options_init(&options);
	if (lua_istable(L, 4)) {
		lua_getfield(L, 4, "auto_subscribe");
		if (!lua_isnil(L, -1))
			options.auto_subscribe = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 4, "adaptive_stream");
		if (!lua_isnil(L, -1))
			options.adaptive_stream = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 4, "dynacast");
		if (!lua_isnil(L, -1))
			options.dynacast = lua_toboolean(L, -1);
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
	lk_room_connect_options_t options;
	read_connect_options(L, options);
	return status_result(L, lk_room_connect_with_options(room->native, url, token, &options));
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
	lk_room_connect_options_t options;
	read_connect_options(L, options);
	try {
		auto task = std::make_shared<AsyncTask>();
		task->operation = AsyncOperation::Connect;
		task->first = url;
		task->second = token;
		task->connect_options = options;
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

const luaL_Reg room_methods[] = {{"on", on},
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
                                 {"dropped_events", dropped_events},
                                 {nullptr, nullptr}};
const luaL_Reg module_methods[] = {
    {"new_room", new_room}, {"version", version}, {nullptr, nullptr}};

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
