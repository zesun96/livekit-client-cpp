#include <livekit/capi/livekit.h>
#include <node_api.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#ifdef LIVEKIT_NODE_TESTING
#include <thread>
#endif

namespace {
// This addon runs exclusively inside a Room's Worker. SDK callbacks never use Node-API.
using Bytes = std::vector<uint8_t>;
struct BigInteger {
	bool is_signed;
	uint64_t value;
};
struct Value {
	using Object = std::map<std::string, Value>;
	using Array = std::vector<Value>;
	std::variant<std::monostate, bool, double, BigInteger, std::string, Bytes, Object, Array> data;
	Value() = default;
	Value(bool v) : data(v) {}
	Value(int v) : data(double(v)) {}
	Value(uint32_t v) : data(double(v)) {}
	Value(double v) : data(v) {}
	Value(int64_t v) : data(BigInteger{true, static_cast<uint64_t>(v)}) {}
	Value(uint64_t v) : data(BigInteger{false, v}) {}
	Value(const char* v) : data(std::string(v ? v : "")) {}
	Value(std::string v) : data(std::move(v)) {}
	Value(Bytes v) : data(std::move(v)) {}
	Value(Object v) : data(std::move(v)) {}
	Value(Array v) : data(std::move(v)) {}
	size_t Size() const {
		return std::visit(
		    [](const auto& v) -> size_t {
			    using T = std::decay_t<decltype(v)>;
			    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, Bytes>)
				    return v.size() + 32;
			    else if constexpr (std::is_same_v<T, Object>) {
				    size_t n = 32;
				    for (const auto& [key, item] : v)
					    n += key.size() + item.Size() + 64;
				    return n;
			    } else if constexpr (std::is_same_v<T, Array>) {
				    size_t n = 32;
				    for (const auto& item : v)
					    n += item.Size();
				    return n;
			    } else
				    return 32;
		    },
		    data);
	}
};
using Object = Value::Object;
using Array = Value::Array;

void Napi(napi_status status) {
	if (status != napi_ok)
		throw std::runtime_error("Node-API operation failed");
}
struct Error : std::runtime_error {
	int domain;
	int code;
	std::string details;
	Error(std::string message, int d = LK_ERROR_DOMAIN_STATUS, int c = LK_STATUS_INVALID_ARGUMENT)
	    : std::runtime_error(std::move(message)), domain(d), code(c) {}
};
void Check(lk_status_t status) {
	if (status == LK_STATUS_OK)
		return;
	// Read the error on the calling thread, before another SDK call can replace it.
	const std::string message = lk_last_error();
	lk_error_info_t info{};
	lk_error_info_init(&info);
	lk_last_error_info(&info);
	throw Error(message, info.domain, info.code);
}
void CheckLastError() {
	lk_error_info_t info{};
	lk_error_info_init(&info);
	Check(lk_last_error_info(&info));
	if (info.domain != LK_ERROR_DOMAIN_NONE)
		throw Error(lk_last_error(), info.domain, info.code);
}
template <typename Getter> std::string Text(Getter get) {
	const auto size = get(nullptr, 0);
	if (!size)
		CheckLastError();
	std::vector<char> buffer(std::max<size_t>(1, size));
	const size_t required = get(buffer.data(), buffer.size());
	if (!required)
		CheckLastError();
	if (required > buffer.size()) {
		buffer.resize(required);
		get(buffer.data(), buffer.size());
	}
	return buffer.data();
}
Bytes Copy(const uint8_t* data, size_t size) { return size ? Bytes(data, data + size) : Bytes{}; }
#include "struct_pack.inc"
napi_value Js(napi_env env, const Value& value);
Value FromJs(napi_env env, napi_value input, size_t depth = 0);
#include "channel.inc"
Value Participant(const lk_participant_info_t* p) {
	if (!p)
		return {};
	return Object{{"sid", p->sid},
	              {"identity", p->identity},
	              {"name", p->name},
	              {"metadata", p->metadata},
	              {"audioLevel", double(p->audio_level)},
	              {"connectionQuality", int(p->connection_quality)},
	              {"isSpeaking", bool(p->is_speaking)},
	              {"isLocal", bool(p->is_local)}};
}
Value Track(const lk_track_publication_info_t* t) {
	if (!t)
		return {};
	return Object{{"sid", t->sid},        {"name", t->name},           {"mimeType", t->mime_type},
	              {"kind", int(t->kind)}, {"source", int(t->source)},  {"width", t->width},
	              {"height", t->height},  {"muted", bool(t->is_muted)}};
}

struct Media {
	std::string name;
	std::string capture_kind;
	lk_audio_source_t* audio = nullptr;
	lk_video_source_t* video = nullptr;
	lk_local_track_t* track = nullptr;
	uint32_t channels = 0;
	~Media() {
		if (track)
			lk_local_track_destroy(track);
		if (audio)
			lk_audio_source_destroy(audio);
		if (video)
			lk_video_source_destroy(video);
	}
};
#include "resource_types.inc"
struct Room {
	lk_room_t* room = nullptr;
	bool initialized = false;
	std::mutex mutex;
	std::deque<Value> events;
	size_t capacity;
	size_t byte_limit;
	size_t bytes = 0;
	size_t dropped = 0;
	std::map<uint32_t, std::unique_ptr<Media>> media;
	uint32_t next_id = 1;
	std::shared_ptr<Channel> channel;
	std::vector<std::unique_ptr<RpcContext>> rpc_contexts;
	std::vector<std::unique_ptr<WriterContext>> writer_contexts;
	std::map<uint32_t, Owned<lk_text_stream_writer_t, lk_text_stream_writer_destroy>> text_writers;
	std::map<uint32_t, Owned<lk_byte_stream_writer_t, lk_byte_stream_writer_destroy>> byte_writers;
	std::map<uint32_t, Owned<lk_local_data_track_t, lk_local_data_track_destroy>> data_tracks;
	std::map<uint32_t, Owned<lk_data_track_reader_t, lk_data_track_reader_destroy>> data_readers;
	std::map<uint32_t, Owned<lk_audio_stream_t, lk_audio_stream_destroy>> audio_readers;
	std::map<uint32_t, Owned<lk_video_stream_t, lk_video_stream_destroy>> video_readers;
	std::map<uint32_t, Owned<lk_track_recorder_t, lk_track_recorder_destroy>> recorders;
	uint32_t Id() {
		if (next_id == UINT32_MAX)
			throw Error("Resource identifier space exhausted");
		return next_id++;
	}
#ifdef LIVEKIT_NODE_TESTING
	lk_room_callbacks_t test_callbacks{};
#endif
	Room(size_t count, size_t limit) : capacity(count), byte_limit(limit) {
		Check(lk_init());
		initialized = true;
		try {
			Check(lk_room_create(&room));
		} catch (...) {
			lk_shutdown();
			initialized = false;
			throw;
		}
	}
	~Room() { Close(); }
	void Close() {
		if (channel)
			channel->Cancel();
		RemoveDiagnostics(this);
		if (room) {
			text_writers.clear();
			byte_writers.clear();
			data_readers.clear();
			audio_readers.clear();
			video_readers.clear();
			recorders.clear();
			// Disconnect before destroying published tracks; keep callback storage alive until
			// destroy.
			lk_room_disconnect(room);
			// Release RTC tracks while the room's transport threads still exist. A retained
			// sender can otherwise block room destruction waiting for those same threads.
			media.clear();
			data_tracks.clear();
			lk_room_destroy(room);
			room = nullptr;
			rpc_contexts.clear();
			writer_contexts.clear();
		}
		if (initialized) {
			lk_shutdown();
			initialized = false;
		}
	}
	void Push(Value event) noexcept {
		try {
			const size_t size = event.Size();
			std::lock_guard lock(mutex);
			if (size > byte_limit) {
				++dropped;
				return;
			}
			while (!events.empty() && (events.size() >= capacity || bytes + size > byte_limit)) {
				bytes -= events.front().Size();
				events.pop_front();
				++dropped;
			}
			events.push_back(std::move(event));
			bytes += size;
		} catch (...) { /* No exception may escape an SDK callback. */
		}
	}
};
template <typename F> void Callback(void* user, F function) noexcept {
	try {
		function(*static_cast<Room*>(user));
	} catch (...) {
	}
}
#include "native_callbacks.inc"
void SetCallbacks(Room& state) {
	lk_room_callbacks_t c{};
	lk_room_callbacks_init(&c);
	c.user_data = &state;
#define ROOM_EVENT(field, name)                                                                    \
	c.field = [](void* u, lk_room_t*) {                                                            \
		Callback(u, [](Room& r) { r.Push(Object{{"type", name}}); });                              \
	}
	ROOM_EVENT(on_connected, "connected");
	ROOM_EVENT(on_reconnecting, "reconnecting");
	ROOM_EVENT(on_reconnected, "reconnected");
	ROOM_EVENT(on_token_refreshed, "tokenRefreshed");
	ROOM_EVENT(on_room_eos, "roomEos");
#undef ROOM_EVENT
	c.on_disconnected_with_reason = [](void* u, lk_room_t*, lk_disconnect_reason_t reason) {
		Callback(
		    u, [&](Room& r) { r.Push(Object{{"type", "disconnected"}, {"reason", int(reason)}}); });
	};
	c.on_connection_state_changed = [](void* u, lk_room_t*, lk_room_state_t state) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "connectionStateChanged"}, {"state", int(state)}});
		});
	};
#define PARTICIPANT_EVENT(field, name)                                                             \
	c.field = [](void* u, lk_room_t*, const lk_participant_info_t* p) {                            \
		Callback(                                                                                  \
		    u, [&](Room& r) { r.Push(Object{{"type", name}, {"participant", Participant(p)}}); }); \
	}
	PARTICIPANT_EVENT(on_participant_connected, "participantConnected");
	PARTICIPANT_EVENT(on_participant_disconnected, "participantDisconnected");
#undef PARTICIPANT_EVENT
#define TRACK_EVENT(field, name)                                                                   \
	c.field = [](void* u, lk_room_t*, const lk_track_publication_info_t* t,                        \
	             const lk_participant_info_t* p) {                                                 \
		Callback(u, [&](Room& r) {                                                                 \
			r.Push(Object{{"type", name}, {"track", Track(t)}, {"participant", Participant(p)}});  \
		});                                                                                        \
	}
	TRACK_EVENT(on_track_published, "trackPublished");
	TRACK_EVENT(on_track_unpublished, "trackUnpublished");
	TRACK_EVENT(on_track_subscribed, "trackSubscribed");
	TRACK_EVENT(on_track_unsubscribed, "trackUnsubscribed");
	TRACK_EVENT(on_track_muted, "trackMuted");
	TRACK_EVENT(on_track_unmuted, "trackUnmuted");
	TRACK_EVENT(on_local_track_published, "localTrackPublished");
	TRACK_EVENT(on_local_track_unpublished, "localTrackUnpublished");
#undef TRACK_EVENT
	c.on_data_received = [](void* u, lk_room_t*, const lk_data_received_t* e) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "dataReceived"},
			              {"data", Copy(e->data, e->data_size)},
			              {"topic", e->topic},
			              {"participantIdentity", e->participant_identity},
			              {"reliable", bool(e->reliable)}});
		});
	};
	c.on_room_metadata_changed = [](void* u, lk_room_t*, const char* text) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "roomMetadataChanged"}, {"metadata", text}});
		});
	};
	c.on_text_received = [](void* u, lk_room_t*, const lk_text_received_t* e) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "textReceived"},
			              {"text", e->text},
			              {"topic", e->topic},
			              {"participantIdentity", e->participant_identity}});
		});
	};
	c.on_audio_frame = [](void* u, lk_room_t*, const lk_track_publication_info_t* t,
	                      const lk_participant_info_t* p, const lk_audio_frame_t* f) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "audioFrame"},
			              {"track", Track(t)},
			              {"participant", Participant(p)},
			              {"data", Copy(reinterpret_cast<const uint8_t*>(f->data),
			                            f->sample_count * sizeof(int16_t))},
			              {"sampleRate", f->sample_rate},
			              {"channels", f->num_channels},
			              {"samplesPerChannel", f->samples_per_channel}});
		});
	};
	c.on_video_frame = [](void* u, lk_room_t*, const lk_track_publication_info_t* t,
	                      const lk_participant_info_t* p, const lk_video_frame_t* f) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "videoFrame"},
			              {"track", Track(t)},
			              {"participant", Participant(p)},
			              {"data", Copy(f->data, f->data_size)},
			              {"width", f->width},
			              {"height", f->height},
			              {"timestampUs", std::to_string(f->timestamp_us)}});
		});
	};
	c.on_byte_received = [](void* u, lk_room_t*, const lk_file_received_t* e) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "bytesReceived"},
			              {"data", Copy(e->data, e->data_size)},
			              {"streamId", e->stream_id},
			              {"name", e->name},
			              {"mimeType", e->mime_type},
			              {"topic", e->topic},
			              {"participantIdentity", e->participant_identity}});
		});
	};
	c.on_file_received = [](void* u, lk_room_t*, const lk_file_received_t* e) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "fileReceived"},
			              {"data", Copy(e->data, e->data_size)},
			              {"streamId", e->stream_id},
			              {"name", e->name},
			              {"mimeType", e->mime_type},
			              {"topic", e->topic},
			              {"participantIdentity", e->participant_identity}});
		});
	};
	AdditionalCallbacks(c);
#ifdef LIVEKIT_NODE_TESTING
	state.test_callbacks = c;
#endif
	Check(lk_room_set_callbacks(state.room, &c));
}

napi_value Js(napi_env env, const Value& value) {
	return std::visit(
	    [&](const auto& v) -> napi_value {
		    using T = std::decay_t<decltype(v)>;
		    napi_value out;
		    if constexpr (std::is_same_v<T, std::monostate>)
			    Napi(napi_get_null(env, &out));
		    else if constexpr (std::is_same_v<T, bool>)
			    Napi(napi_get_boolean(env, v, &out));
		    else if constexpr (std::is_same_v<T, double>)
			    Napi(napi_create_double(env, v, &out));
		    else if constexpr (std::is_same_v<T, BigInteger>) {
			    if (v.is_signed)
				    Napi(napi_create_bigint_int64(env, static_cast<int64_t>(v.value), &out));
			    else
				    Napi(napi_create_bigint_uint64(env, v.value, &out));
		    } else if constexpr (std::is_same_v<T, std::string>)
			    Napi(napi_create_string_utf8(env, v.data(), v.size(), &out));
		    else if constexpr (std::is_same_v<T, Bytes>)
			    Napi(napi_create_buffer_copy(env, v.size(), v.data(), nullptr, &out));
		    else if constexpr (std::is_same_v<T, Object>) {
			    Napi(napi_create_object(env, &out));
			    for (const auto& [key, item] : v) {
				    napi_property_descriptor property{key.c_str(),
				                                      nullptr,
				                                      nullptr,
				                                      nullptr,
				                                      nullptr,
				                                      Js(env, item),
				                                      napi_default_jsproperty,
				                                      nullptr};
				    Napi(napi_define_properties(env, out, 1, &property));
			    }
		    } else {
			    Napi(napi_create_array_with_length(env, v.size(), &out));
			    for (uint32_t i = 0; i < v.size(); ++i)
				    Napi(napi_set_element(env, out, i, Js(env, v[i])));
		    }
		    return out;
	    },
	    value.data);
}
bool Has(napi_env env, napi_value object, const char* key) {
	bool has;
	Napi(napi_has_named_property(env, object, key, &has));
	return has;
}
napi_value Get(napi_env env, napi_value object, const char* key) {
	napi_value value;
	Napi(napi_get_named_property(env, object, key, &value));
	return value;
}
std::string String(napi_env env, napi_value value, bool allow_nul = false) {
	size_t size;
	Napi(napi_get_value_string_utf8(env, value, nullptr, 0, &size));
	std::vector<char> buffer(size + 1);
	Napi(napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &size));
	if (!allow_nul &&
	    std::find(buffer.begin(), buffer.begin() + size, '\0') != buffer.begin() + size)
		throw Error("Strings must not contain NUL");
	return std::string(buffer.data(), size);
}
std::string Str(napi_env env, napi_value object, const char* key, const char* fallback = "") {
	return Has(env, object, key) ? String(env, Get(env, object, key)) : fallback;
}
uint32_t Num(napi_env env, napi_value object, const char* key, uint32_t fallback) {
	if (!Has(env, object, key))
		return fallback;
	double n;
	Napi(napi_get_value_double(env, Get(env, object, key), &n));
	if (!std::isfinite(n) || n < 0 || n > UINT32_MAX || std::floor(n) != n)
		throw Error(std::string(key) + " must be a uint32");
	return static_cast<uint32_t>(n);
}
bool Bool(napi_env env, napi_value object, const char* key, bool fallback) {
	if (!Has(env, object, key))
		return fallback;
	bool v;
	Napi(napi_get_value_bool(env, Get(env, object, key), &v));
	return v;
}
Bytes Data(napi_env env, napi_value object, const char* key) {
	napi_typedarray_type type;
	size_t length, offset;
	void* data;
	napi_value buffer;
	Napi(napi_get_typedarray_info(env, Get(env, object, key), &type, &length, &data, &buffer,
	                              &offset));
	if (type != napi_uint8_array)
		throw Error("Expected Uint8Array");
	return Copy(static_cast<uint8_t*>(data), length);
}
struct Destinations {
	std::vector<std::string> strings;
	std::vector<const char*> pointers;
	Destinations(napi_env env, napi_value args) {
		if (!Has(env, args, "destinationIdentities"))
			return;
		auto values = Get(env, args, "destinationIdentities");
		uint32_t length;
		Napi(napi_get_array_length(env, values, &length));
		for (uint32_t i = 0; i < length; ++i) {
			napi_value value;
			Napi(napi_get_element(env, values, i, &value));
			strings.push_back(String(env, value));
		}
		for (const auto& text : strings)
			pointers.push_back(text.c_str());
	}
};
#include "marshal.inc"
constexpr napi_type_tag room_tag{0x527ba24e449df01aULL, 0xa8bce09e7edb0f62ULL};
Room& Unwrap(napi_env env, napi_value external) {
	bool valid = false;
	Napi(napi_check_object_type_tag(env, external, &room_tag, &valid));
	if (!valid)
		throw Error("Invalid room handle");
	void* ptr;
	Napi(napi_get_value_external(env, external, &ptr));
	return *static_cast<Room*>(ptr);
}
template <typename F> napi_value Guard(napi_env env, F function) noexcept {
	try {
		return function();
	} catch (const Error& e) {
		try {
			napi_value error;
			Napi(napi_create_error(env, nullptr, Js(env, e.what()), &error));
			Napi(napi_set_named_property(env, error, "domain", Js(env, e.domain)));
			Napi(napi_set_named_property(env, error, "code", Js(env, e.code)));
			Napi(napi_set_named_property(env, error, "data", Js(env, e.details)));
			napi_throw(env, error);
		} catch (...) {
			napi_throw_error(env, nullptr, e.what());
		}
	} catch (const std::exception& e) {
		napi_throw_error(env, nullptr, e.what());
	} catch (...) {
		napi_throw_error(env, nullptr, "Unknown native failure");
	}
	return nullptr;
}
napi_value Create(napi_env env, napi_callback_info info) {
	return Guard(env, [&] {
		size_t argc = 1;
		napi_value args;
		Napi(napi_get_cb_info(env, info, &argc, &args, nullptr, nullptr));
		if (argc != 1)
			throw Error("Expected room options");
		const auto capacity = Num(env, args, "eventCapacity", 256);
		const auto limit = Num(env, args, "eventByteLimit", 16 * 1024 * 1024);
		if (!capacity || !limit)
			throw Error("Queue limits must be positive");
		auto room = std::make_unique<Room>(capacity, limit);
		if (Has(env, args, "channelId"))
			room->channel = FindChannel(Str(env, args, "channelId"));
		SetCallbacks(*room);
		napi_value external;
		Napi(napi_create_external(
		    env, room.get(), [](napi_env, void* p, void*) { delete static_cast<Room*>(p); },
		    nullptr, &external));
		room.release();
		Napi(napi_type_tag_object(env, external, &room_tag));
		return external;
	});
}

#include "snapshots.inc"
Value Snapshot(Room& state) {
	auto* r = state.room;
	lk_remote_participant_list_t* raw = nullptr;
	Check(lk_room_create_remote_participant_snapshot(r, &raw));
	std::unique_ptr<lk_remote_participant_list_t, decltype(&lk_remote_participant_list_destroy)>
	    list(raw, lk_remote_participant_list_destroy);
	Array participants;
	for (size_t i = 0; i < lk_remote_participant_list_count(raw); ++i) {
		const lk_remote_participant_snapshot_t* p;
		Check(lk_remote_participant_list_at(raw, i, &p));
#define FIELD(name, getter) {name, Text([&](char* b, size_t n) { return getter(p, b, n); })}
		Object item{FIELD("sid", lk_remote_participant_snapshot_sid),
		            FIELD("identity", lk_remote_participant_snapshot_identity),
		            FIELD("name", lk_remote_participant_snapshot_name),
		            FIELD("metadata", lk_remote_participant_snapshot_metadata)};
#undef FIELD
		RemoteDetails(item, p);
		Array publications;
		for (size_t j = 0; j < lk_remote_participant_snapshot_publication_count(p); ++j) {
			const lk_remote_track_publication_snapshot_t* t;
			Check(lk_remote_participant_snapshot_publication_at(p, j, &t));
			lk_remote_track_publication_snapshot_info_t info{};
			lk_remote_track_publication_snapshot_info_init(&info);
			Check(lk_remote_track_publication_snapshot_info(t, &info));
			Value publication =
			    Object{{"sid", Text([&](char* b, size_t n) {
				            return lk_remote_track_publication_snapshot_sid(t, b, n);
			            })},
			           {"name", Text([&](char* b, size_t n) {
				            return lk_remote_track_publication_snapshot_name(t, b, n);
			            })},
			           {"mimeType", Text([&](char* b, size_t n) {
				            return lk_remote_track_publication_snapshot_mime_type(t, b, n);
			            })},
			           {"kind", int(info.kind)},
			           {"source", int(info.source)},
			           {"width", info.width},
			           {"height", info.height},
			           {"muted", bool(info.is_muted)}};
			PublicationDetails(std::get<Object>(publication.data), t, info);
			publications.push_back(std::move(publication));
		}
		item["publications"] = std::move(publications);
		participants.push_back(std::move(item));
	}
	return Object{{"state", int(lk_room_state(r))},
	              {"disconnectReason", int(lk_room_disconnect_reason(r))},
	              {"connected", bool(lk_room_is_connected(r))},
	              {"recording", bool(lk_room_is_recording(r))},
	              {"sid", Text([&](char* b, size_t n) { return lk_room_sid(r, b, n); })},
	              {"name", Text([&](char* b, size_t n) { return lk_room_name(r, b, n); })},
	              {"metadata", Text([&](char* b, size_t n) { return lk_room_metadata(r, b, n); })},
	              {"localParticipant", LocalSnapshot(r)},
	              {"remoteParticipants", std::move(participants)}};
}

#include "advanced.inc"
Value Invoke(napi_env env, Room& state, const std::string& method, napi_value args) {
	if (method == "close") {
		state.Close();
		return {};
	}
	if (method == "poll") {
		std::lock_guard lock(state.mutex);
		Array events;
		for (size_t i = 0; i < 32 && !state.events.empty(); ++i) {
			state.bytes -= state.events.front().Size();
			events.push_back(std::move(state.events.front()));
			state.events.pop_front();
		}
		const double dropped = double(state.dropped);
		state.dropped = 0;
		return Object{{"events", std::move(events)}, {"dropped", dropped}};
	}
	if (!state.room)
		throw Error("Room is closed", LK_ERROR_DOMAIN_STATUS, LK_STATUS_INVALID_STATE);
#ifdef LIVEKIT_NODE_TESTING
#include "test_events.inc"
	if (method == "_testOptions") {
		Arena arena;
		const auto type = Str(env, args, "type");
#include "test_structs.inc"
		throw Error("Unknown test structure");
	}
	if (method == "_testCallback") {
		if (!state.channel)
			throw Error("Missing test channel");
		return state.channel->Ask(Str(env, args, "kind", "test"),
		                          Object{{"value", "copied request"}});
	}
	if (method == "_testRpcCallback") {
		RpcContext context{&state, "testRpc"};
		lk_rpc_invocation_t invocation{"request", "caller", "payload", 10000};
		lk_rpc_handler_result_t result{};
		RpcHandler(&context, &invocation, &result);
		return Pack(result);
	}
	if (method == "_testReconnectCallback") {
		lk_reconnect_context_t context{};
		context.struct_size = sizeof(context);
		context.retry_count = 2;
		context.elapsed_ms = 9007199254740993ULL;
		context.server_url = "ws://unused.invalid";
		uint32_t delay = 0;
		const auto retry = ReconnectPolicy(&state, &context, &delay);
		return Object{{"retry", bool(retry)}, {"delayMs", delay}};
	}
	if (method == "_testEvents") {
		const auto count = Num(env, args, "count", 1);
		const auto size = Num(env, args, "size", 3);
		// Invoke the actual registered callback from another thread, then invalidate borrowed data.
		std::thread producer([&] {
			for (uint32_t i = 0; i < count; ++i) {
				Bytes bytes(size, static_cast<uint8_t>(i));
				lk_data_received_t e{bytes.data(), bytes.size(), "test-topic", "test-peer", 1};
				state.test_callbacks.on_data_received(&state, state.room, &e);
				std::fill(bytes.begin(), bytes.end(), uint8_t{255});
			}
		});
		producer.join();
		return {};
	}
#endif
	auto* r = state.room;
	if (method == "advanced")
		return Advanced(env, state, args);
	if (method == "connect")
		return ConnectRoom(env, state, args, false);
	if (method == "snapshot")
		return Snapshot(state);
	if (method == "devices") {
		lk_media_device_list_t* raw = nullptr;
		Check(lk_media_device_list_create(&raw));
		std::unique_ptr<lk_media_device_list_t, decltype(&lk_media_device_list_destroy)> list(
		    raw, lk_media_device_list_destroy);
		Array devices;
		for (size_t i = 0; i < lk_media_device_list_count(raw); ++i) {
			lk_media_device_info_t info{};
			info.struct_size = sizeof(info);
			Check(lk_media_device_list_info(raw, i, &info));
			devices.push_back(Object{{"kind", int(info.kind)},
			                         {"isDefault", bool(info.is_default)},
			                         {"id", Text([&](char* b, size_t n) {
				                          return lk_media_device_list_id(raw, i, b, n);
			                          })},
			                         {"label", Text([&](char* b, size_t n) {
				                          return lk_media_device_list_label(raw, i, b, n);
			                          })}});
		}
		return devices;
	}
	if (method == "disconnect")
		Check(lk_room_disconnect(r));
	else if (method == "setAttributes") {
		const auto object = Get(env, args, "attributes");
		napi_value keys;
		Napi(napi_get_property_names(env, object, &keys));
		uint32_t length;
		Napi(napi_get_array_length(env, keys, &length));
		std::vector<std::pair<std::string, std::string>> values;
		for (uint32_t i = 0; i < length; ++i) {
			napi_value key, value;
			Napi(napi_get_element(env, keys, i, &key));
			Napi(napi_get_property(env, object, key, &value));
			values.emplace_back(String(env, key), String(env, value));
		}
		std::vector<lk_attribute_t> attributes;
		for (const auto& [key, value] : values)
			attributes.push_back({key.c_str(), value.c_str()});
		Check(lk_local_participant_set_attributes(r, attributes.data(), attributes.size()));
	} else if (method == "setName")
		Check(lk_local_participant_set_name(r, Str(env, args, "name").c_str()));
	else if (method == "setMetadata")
		Check(lk_local_participant_set_metadata(r, Str(env, args, "metadata").c_str()));
	else if (method == "setTrackSubscribed")
		Check(lk_room_set_remote_track_subscribed(r, Str(env, args, "participantSid").c_str(),
		                                          Str(env, args, "trackSid").c_str(),
		                                          Bool(env, args, "subscribed", true)));
	else if (method == "publishData" || method == "sendText" || method == "sendBytes" ||
	         method == "sendFile") {
		Destinations destinations(env, args);
		const auto topic = Str(env, args, "topic"), mime = Str(env, args, "mimeType"),
		           name = Str(env, args, "name");
#define DEST(o)                                                                                    \
	o.topic = topic.c_str();                                                                       \
	o.destination_identities = destinations.pointers.data();                                       \
	o.destination_identity_count = destinations.pointers.size()
		if (method == "publishData") {
			const auto data = Data(env, args, "data");
			lk_data_publish_options_t o{};
			lk_data_publish_options_init(&o);
			DEST(o);
			o.reliable = Bool(env, args, "reliable", true);
			Check(lk_room_publish_data(r, data.data(), data.size(), &o));
		} else if (method == "sendText") {
			lk_text_send_options_t o{};
			Arena arena;
			Read(env, args, o, arena);
			DEST(o);
			Check(lk_room_send_text(r, Str(env, args, "text").c_str(), &o));
		} else if (method == "sendBytes") {
			const auto data = Data(env, args, "data");
			lk_byte_send_options_t o{};
			Arena arena;
			Read(env, args, o, arena);
			DEST(o);
			o.mime_type = mime.c_str();
			o.name = name.c_str();
			Check(lk_room_send_bytes(r, data.data(), data.size(), &o));
		} else {
			lk_file_send_options_t o{};
			Arena arena;
			Read(env, args, o, arena);
			DEST(o);
			o.mime_type = mime.c_str();
			Check(lk_room_send_file(r, Str(env, args, "path").c_str(), &o));
		}
#undef DEST
	} else if (method == "performRpc") {
		const auto destination = Str(env, args, "destinationIdentity"),
		           name = Str(env, args, "method"), payload = Str(env, args, "payload");
		lk_rpc_perform_options_t o{};
		lk_rpc_perform_options_init(&o);
		o.destination_identity = destination.c_str();
		o.method = name.c_str();
		o.payload = payload.c_str();
		o.response_timeout_ms = Num(env, args, "responseTimeoutMs", o.response_timeout_ms);
		lk_rpc_result_t* raw = nullptr;
		Check(lk_room_perform_rpc(r, &o, &raw));
		std::unique_ptr<lk_rpc_result_t, decltype(&lk_rpc_result_destroy)> result(
		    raw, lk_rpc_result_destroy);
		if (!lk_rpc_result_ok(raw)) {
			Error failure(
			    Text([&](char* b, size_t n) { return lk_rpc_result_error_message(raw, b, n); }), 3,
			    int(lk_rpc_result_error_code(raw)));
			failure.details =
			    Text([&](char* b, size_t n) { return lk_rpc_result_error_data(raw, b, n); });
			throw failure;
		}
		return Text([&](char* b, size_t n) { return lk_rpc_result_payload(raw, b, n); });
	} else if (method == "createAudioTrack" || method == "createVideoTrack") {
		auto media = std::make_unique<Media>();
		const auto name = Str(env, args, "name", "node-track");
		media->name = name;
		if (method == "createAudioTrack") {
			lk_audio_source_options_t o{};
			Arena arena;
			Read(env, args, o, arena);
			media->channels = o.num_channels;
			Check(lk_audio_source_create(&o, &media->audio));
		} else {
			lk_video_source_options_t o{};
			Arena arena;
			Read(env, args, o, arena);
			Check(lk_video_source_create(&o, &media->video));
		}
		if (state.next_id == UINT32_MAX)
			throw Error("Track identifier space exhausted");
		const auto id = state.next_id++;
		state.media.emplace(id, std::move(media));
		return id;
	} else if (method == "publishTrack" || method == "unpublishTrack" ||
	           method == "setTrackMuted" || method == "captureAudio" || method == "captureVideo") {
		const auto it = state.media.find(Num(env, args, "id", 0));
		if (it == state.media.end())
			throw Error("Unknown local track");
		auto& m = *it->second;
		if (method == "publishTrack") {
			// Sources can be populated offline; materialize the RTC track once connected.
			if (!m.track) {
				if (m.audio)
					Check(lk_room_create_audio_track(r, m.name.c_str(), m.audio, &m.track));
				else
					Check(lk_room_create_video_track(r, m.name.c_str(), m.video, &m.track));
			}
			lk_track_publish_options_t o{};
			Arena arena;
			Read(env, args, o, arena);
			o.source = static_cast<lk_track_source_t>(
			    Num(env, args, "source",
			        m.audio ? LK_TRACK_SOURCE_MICROPHONE : LK_TRACK_SOURCE_CAMERA));
			Check(lk_local_track_publish(r, m.track, &o));
		} else if (method == "unpublishTrack")
			Check(lk_local_track_unpublish(m.track, 0));
		else if (method == "setTrackMuted")
			Check(lk_local_track_set_muted(m.track, Bool(env, args, "muted", false)));
		else if (method == "captureAudio") {
			if (!m.audio)
				throw Error("Not an audio track");
			const auto data = Data(env, args, "data");
			const size_t sample_bytes = sizeof(int16_t) * size_t(m.channels);
			if (!sample_bytes || data.empty() || data.size() % sample_bytes ||
			    data.size() / sample_bytes > UINT32_MAX)
				throw Error("Expected complete interleaved PCM16 frames");
			std::vector<int16_t> samples(data.size() / 2);
			std::memcpy(samples.data(), data.data(), data.size());
			Check(lk_audio_source_capture_frame(
			    m.audio, samples.data(), static_cast<uint32_t>(samples.size() / m.channels)));
		} else {
			if (!m.video)
				throw Error("Not a video track");
			const auto data = Data(env, args, "data");
			const auto timestamp = Str(env, args, "timestampUs", "0");
			size_t used = 0;
			const auto ts = std::stoll(timestamp, &used);
			if (used != timestamp.size())
				throw Error("Invalid timestampUs");
			Check(lk_video_source_capture_i420(m.video, data.data(), data.size(),
			                                   Num(env, args, "width", 0),
			                                   Num(env, args, "height", 0), ts));
		}
	} else
		throw Error("Unknown native operation");
	return {};
}
napi_value Call(napi_env env, napi_callback_info info) {
	return Guard(env, [&] {
		size_t argc = 3;
		napi_value args[3];
		Napi(napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));
		if (argc != 3)
			throw Error("Expected room, method and arguments");
		return Js(env, Invoke(env, Unwrap(env, args[0]), String(env, args[1]), args[2]));
	});
}
napi_value Version(napi_env env, napi_callback_info) {
	return Guard(env,
	             [&] { return Js(env, Text([](char* b, size_t n) { return lk_version(b, n); })); });
}
#include "channel_exports.inc"
napi_value Init(napi_env env, napi_value exports) {
	return Guard(env, [&] {
		for (const auto& [name, callback] :
		     std::map<std::string, napi_callback>{{"create", Create},
		                                          {"call", Call},
		                                          {"version", Version},
		                                          {"createChannel", CreateChannel},
		                                          {"cancelChannel", CancelChannel},
		                                          {"releaseChannel", ReleaseChannel},
		                                          {"completeCallback", CompleteCallback}}) {
			napi_value function;
			Napi(napi_create_function(env, name.c_str(), NAPI_AUTO_LENGTH, callback, nullptr,
			                          &function));
			Napi(napi_set_named_property(env, exports, name.c_str(), function));
		}
		return exports;
	});
}
} // namespace
NAPI_MODULE(livekitclient_node, Init)
