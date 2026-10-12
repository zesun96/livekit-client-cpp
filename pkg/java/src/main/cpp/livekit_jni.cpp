#include <jni.h>

#include <livekit/capi/livekit.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {
struct RoomHandle;
struct RpcContext {
	RoomHandle* room;
	std::string method;
};
struct RoomHandle {
	lk_room_t* room = nullptr;
	JavaVM* vm = nullptr;
	jobject owner = nullptr;
	jclass participant_class = nullptr;
	jclass packet_class = nullptr;
	jclass text_class = nullptr;
	jclass byte_class = nullptr;
	jclass chat_class = nullptr;
	std::vector<std::unique_ptr<RpcContext>> rpc_contexts;
};

RoomHandle* handle(jlong value) { return reinterpret_cast<RoomHandle*>(value); }
jstring string(JNIEnv* env, const char* value);

jclass global_class(JNIEnv* env, const char* name) {
	jclass local = env->FindClass(name);
	if (local == nullptr)
		return nullptr;
	jclass global = static_cast<jclass>(env->NewGlobalRef(local));
	env->DeleteLocalRef(local);
	return global;
}

void release_classes(JNIEnv* env, RoomHandle* h) {
	if (h->participant_class)
		env->DeleteGlobalRef(h->participant_class);
	if (h->packet_class)
		env->DeleteGlobalRef(h->packet_class);
	if (h->text_class)
		env->DeleteGlobalRef(h->text_class);
	if (h->byte_class)
		env->DeleteGlobalRef(h->byte_class);
	if (h->chat_class)
		env->DeleteGlobalRef(h->chat_class);
}

void fail(JNIEnv* env, lk_status_t status) {
	if (status == LK_STATUS_OK)
		return;
	const char* error = lk_last_error();
	jclass cls = env->FindClass("io/livekit/client/LiveKitException");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(ILjava/lang/String;)V");
	jstring message = string(env, error == nullptr ? "LiveKit operation failed" : error);
	jobject exception = env->NewObject(cls, ctor, static_cast<jint>(status), message);
	env->Throw(static_cast<jthrowable>(exception));
	env->DeleteLocalRef(exception);
	env->DeleteLocalRef(message);
	env->DeleteLocalRef(cls);
}

std::string bytes(JNIEnv* env, jbyteArray value) {
	if (value == nullptr)
		return {};
	const jsize size = env->GetArrayLength(value);
	std::string result(static_cast<size_t>(size), '\0');
	if (size != 0)
		env->GetByteArrayRegion(value, 0, size, reinterpret_cast<jbyte*>(result.data()));
	return result;
}

std::string java_string_bytes(JNIEnv* env, jstring value) {
	if (value == nullptr)
		return {};
	jclass charset = env->FindClass("java/nio/charset/StandardCharsets");
	jfieldID utf8 = env->GetStaticFieldID(charset, "UTF_8", "Ljava/nio/charset/Charset;");
	jobject encoding = env->GetStaticObjectField(charset, utf8);
	jclass cls = env->FindClass("java/lang/String");
	jmethodID get = env->GetMethodID(cls, "getBytes", "(Ljava/nio/charset/Charset;)[B");
	jbyteArray raw = static_cast<jbyteArray>(env->CallObjectMethod(value, get, encoding));
	std::string result = bytes(env, raw);
	env->DeleteLocalRef(raw);
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(encoding);
	env->DeleteLocalRef(charset);
	return result;
}

jbyteArray array(JNIEnv* env, const void* data, size_t size) {
	if (size > static_cast<size_t>(INT32_MAX))
		return nullptr;
	jbyteArray result = env->NewByteArray(static_cast<jsize>(size));
	if (result != nullptr && size != 0)
		env->SetByteArrayRegion(result, 0, static_cast<jsize>(size),
		                        static_cast<const jbyte*>(data));
	return result;
}

jstring string(JNIEnv* env, const char* value) {
	if (value == nullptr)
		value = "";
	jbyteArray data = array(env, value, strlen(value));
	jclass charset = env->FindClass("java/nio/charset/StandardCharsets");
	jfieldID utf8 = env->GetStaticFieldID(charset, "UTF_8", "Ljava/nio/charset/Charset;");
	jobject encoding = env->GetStaticObjectField(charset, utf8);
	jclass cls = env->FindClass("java/lang/String");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "([BLjava/nio/charset/Charset;)V");
	jstring result = static_cast<jstring>(env->NewObject(cls, ctor, data, encoding));
	env->DeleteLocalRef(data);
	env->DeleteLocalRef(encoding);
	env->DeleteLocalRef(charset);
	env->DeleteLocalRef(cls);
	return result;
}

jbyteArray getter(JNIEnv* env, const std::function<size_t(char*, size_t)>& get) {
	const size_t size = get(nullptr, 0);
	if (size == 0 || size > static_cast<size_t>(INT32_MAX))
		return array(env, nullptr, 0);
	std::string value(size, '\0');
	get(value.data(), value.size());
	return array(env, value.data(), value.size() - 1);
}

jobject participant(JNIEnv* env, RoomHandle* h, const lk_participant_info_t* value) {
	if (value == nullptr)
		return nullptr;
	jclass cls = h->participant_class;
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>",
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	jstring sid = string(env, value->sid);
	jstring identity = string(env, value->identity);
	jstring name = string(env, value->name);
	jstring metadata = string(env, value->metadata);
	jobject result = env->NewObject(cls, ctor, sid, identity, name, metadata);
	env->DeleteLocalRef(sid);
	env->DeleteLocalRef(identity);
	env->DeleteLocalRef(name);
	env->DeleteLocalRef(metadata);
	return result;
}

void event(RoomHandle* h, int kind, int value = 0, const lk_participant_info_t* who = nullptr,
           const lk_data_received_t* data = nullptr) {
	JNIEnv* env = nullptr;
	bool attached = false;
	if (h->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
		if (h->vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK)
			return;
		attached = true;
	}
	jobject p = participant(env, h, who);
	jobject packet = nullptr;
	if (data != nullptr) {
		jclass cls = h->packet_class;
		jmethodID ctor =
		    env->GetMethodID(cls, "<init>", "([BLjava/lang/String;Ljava/lang/String;Z)V");
		jbyteArray payload = array(env, data->data, data->data_size);
		jstring topic = string(env, data->topic);
		jstring identity = string(env, data->participant_identity);
		packet = env->NewObject(cls, ctor, payload, topic, identity,
		                        data->reliable ? JNI_TRUE : JNI_FALSE);
		env->DeleteLocalRef(payload);
		env->DeleteLocalRef(topic);
		env->DeleteLocalRef(identity);
	}
	jclass cls = env->GetObjectClass(h->owner);
	jmethodID method = env->GetMethodID(
	    cls, "onNativeEvent", "(IILio/livekit/client/Participant;Lio/livekit/client/DataPacket;)V");
	env->CallVoidMethod(h->owner, method, kind, value, p, packet);
	if (env->ExceptionCheck())
		env->ExceptionClear();
	if (p != nullptr)
		env->DeleteLocalRef(p);
	if (packet != nullptr)
		env->DeleteLocalRef(packet);
	env->DeleteLocalRef(cls);
	if (attached)
		h->vm->DetachCurrentThread();
}

void deliver(RoomHandle* h, const std::function<void(JNIEnv*)>& invoke) {
	JNIEnv* env = nullptr;
	bool attached = false;
	if (h->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
		if (h->vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK)
			return;
		attached = true;
	}
	invoke(env);
	if (env->ExceptionCheck())
		env->ExceptionClear();
	if (attached)
		h->vm->DetachCurrentThread();
}

void text_event(RoomHandle* h, const lk_text_received_t* value) {
	if (value == nullptr)
		return;
	deliver(h, [h, value](JNIEnv* env) {
		jclass cls = h->text_class;
		jmethodID ctor = env->GetMethodID(
		    cls, "<init>",
		    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V");
		jstring id = string(env, value->stream_id);
		jstring body = string(env, value->text);
		jstring topic = string(env, value->topic);
		jstring identity = string(env, value->participant_identity);
		jobject item = env->NewObject(cls, ctor, id, body, topic, identity, value->timestamp);
		jclass owner = env->GetObjectClass(h->owner);
		jmethodID callback =
		    env->GetMethodID(owner, "onNativeText", "(Lio/livekit/client/TextMessage;)V");
		env->CallVoidMethod(h->owner, callback, item);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(owner);
		env->DeleteLocalRef(id);
		env->DeleteLocalRef(body);
		env->DeleteLocalRef(topic);
		env->DeleteLocalRef(identity);
	});
}

void byte_event(RoomHandle* h, const lk_file_received_t* value, bool file) {
	if (value == nullptr)
		return;
	deliver(h, [h, value, file](JNIEnv* env) {
		jclass cls = h->byte_class;
		jmethodID ctor = env->GetMethodID(cls, "<init>",
		                                  "(Ljava/lang/String;[BLjava/lang/String;Ljava/lang/"
		                                  "String;Ljava/lang/String;Ljava/lang/String;J)V");
		jstring id = string(env, value->stream_id);
		jbyteArray body = array(env, value->data, value->data_size);
		jstring name = string(env, value->name);
		jstring mime = string(env, value->mime_type);
		jstring topic = string(env, value->topic);
		jstring identity = string(env, value->participant_identity);
		jobject item =
		    env->NewObject(cls, ctor, id, body, name, mime, topic, identity, value->timestamp);
		jclass owner = env->GetObjectClass(h->owner);
		jmethodID callback =
		    env->GetMethodID(owner, "onNativeBytes", "(Lio/livekit/client/ByteMessage;Z)V");
		env->CallVoidMethod(h->owner, callback, item, file ? JNI_TRUE : JNI_FALSE);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(owner);
		env->DeleteLocalRef(id);
		env->DeleteLocalRef(body);
		env->DeleteLocalRef(name);
		env->DeleteLocalRef(mime);
		env->DeleteLocalRef(topic);
		env->DeleteLocalRef(identity);
	});
}

void chat_event(RoomHandle* h, const lk_chat_message_t* value) {
	if (value == nullptr)
		return;
	deliver(h, [h, value](JNIEnv* env) {
		jclass cls = h->chat_class;
		jmethodID ctor = env->GetMethodID(
		    cls, "<init>", "(Ljava/lang/String;JLjava/lang/String;Ljava/lang/String;ZZ)V");
		jstring id = string(env, value->id);
		jstring body = string(env, value->message);
		jstring identity = string(env, value->participant_identity);
		jobject item = env->NewObject(cls, ctor, id, value->timestamp, body, identity,
		                              value->deleted ? JNI_TRUE : JNI_FALSE,
		                              value->generated ? JNI_TRUE : JNI_FALSE);
		jclass owner = env->GetObjectClass(h->owner);
		jmethodID callback =
		    env->GetMethodID(owner, "onNativeChat", "(Lio/livekit/client/ChatMessage;)V");
		env->CallVoidMethod(h->owner, callback, item);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(owner);
		env->DeleteLocalRef(id);
		env->DeleteLocalRef(body);
		env->DeleteLocalRef(identity);
	});
}

lk_rpc_handler_result_t rpc_handler(void* user_data, const lk_rpc_invocation_t* invocation) {
	auto* context = static_cast<RpcContext*>(user_data);
	auto* h = context->room;
	JNIEnv* env = nullptr;
	bool attached = false;
	if (h->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
		if (h->vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK)
			return {nullptr, LK_RPC_ERROR_APPLICATION_ERROR, "Failed to attach JVM thread",
			        nullptr};
		attached = true;
	}
	thread_local std::string response;
	response.clear();
	jclass cls = env->GetObjectClass(h->owner);
	jmethodID callback = env->GetMethodID(cls, "onNativeRpc",
	                                      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
	                                      "String;Ljava/lang/String;I)Ljava/lang/String;");
	jstring method = string(env, context->method.c_str());
	jstring request = string(env, invocation->request_id);
	jstring caller = string(env, invocation->caller_identity);
	jstring payload = string(env, invocation->payload);
	jstring output = static_cast<jstring>(
	    env->CallObjectMethod(h->owner, callback, method, request, caller, payload,
	                          static_cast<jint>(invocation->response_timeout_ms)));
	bool failed = env->ExceptionCheck();
	if (!failed)
		response = java_string_bytes(env, output);
	failed = failed || env->ExceptionCheck();
	if (failed)
		env->ExceptionClear();
	if (output != nullptr)
		env->DeleteLocalRef(output);
	env->DeleteLocalRef(method);
	env->DeleteLocalRef(request);
	env->DeleteLocalRef(caller);
	env->DeleteLocalRef(payload);
	env->DeleteLocalRef(cls);
	if (attached)
		h->vm->DetachCurrentThread();
	return failed ? lk_rpc_handler_result_t{nullptr, LK_RPC_ERROR_APPLICATION_ERROR,
	                                        "Java RPC handler failed", nullptr}
	              : lk_rpc_handler_result_t{response.c_str(), 0, nullptr, nullptr};
}
} // namespace

extern "C" {
JNIEXPORT jstring JNICALL Java_io_livekit_client_Native_version(JNIEnv* env, jclass) {
	jbyteArray value = getter(env, [](char* p, size_t n) { return lk_version(p, n); });
	std::string result = bytes(env, value);
	env->DeleteLocalRef(value);
	return string(env, result.c_str());
}

JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_create(JNIEnv* env, jclass, jobject owner) {
	auto h = std::make_unique<RoomHandle>();
	lk_status_t status = lk_room_create(&h->room);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	env->GetJavaVM(&h->vm);
	h->owner = env->NewGlobalRef(owner);
	h->participant_class = global_class(env, "io/livekit/client/Participant");
	h->packet_class = global_class(env, "io/livekit/client/DataPacket");
	h->text_class = global_class(env, "io/livekit/client/TextMessage");
	h->byte_class = global_class(env, "io/livekit/client/ByteMessage");
	h->chat_class = global_class(env, "io/livekit/client/ChatMessage");
	if (env->ExceptionCheck() || !h->owner || !h->participant_class || !h->packet_class ||
	    !h->text_class || !h->byte_class || !h->chat_class) {
		release_classes(env, h.get());
		if (h->owner)
			env->DeleteGlobalRef(h->owner);
		lk_room_destroy(h->room);
		return 0;
	}
	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	callbacks.user_data = h.get();
	callbacks.on_connected = [](void* p, lk_room_t*) { event(static_cast<RoomHandle*>(p), 0); };
	callbacks.on_reconnecting = [](void* p, lk_room_t*) { event(static_cast<RoomHandle*>(p), 2); };
	callbacks.on_reconnected = [](void* p, lk_room_t*) { event(static_cast<RoomHandle*>(p), 3); };
	callbacks.on_connection_state_changed = [](void* p, lk_room_t*, lk_room_state_t s) {
		event(static_cast<RoomHandle*>(p), 4, static_cast<int>(s));
	};
	callbacks.on_disconnected_with_reason = [](void* p, lk_room_t*, lk_disconnect_reason_t reason) {
		event(static_cast<RoomHandle*>(p), 1, static_cast<int>(reason));
	};
	callbacks.on_participant_connected = [](void* p, lk_room_t*, const lk_participant_info_t* v) {
		event(static_cast<RoomHandle*>(p), 5, 0, v);
	};
	callbacks.on_participant_disconnected = [](void* p, lk_room_t*,
	                                           const lk_participant_info_t* v) {
		event(static_cast<RoomHandle*>(p), 6, 0, v);
	};
	callbacks.on_data_received = [](void* p, lk_room_t*, const lk_data_received_t* v) {
		event(static_cast<RoomHandle*>(p), 7, 0, nullptr, v);
	};
	callbacks.on_text_received = [](void* p, lk_room_t*, const lk_text_received_t* v) {
		text_event(static_cast<RoomHandle*>(p), v);
	};
	callbacks.on_byte_received = [](void* p, lk_room_t*, const lk_file_received_t* v) {
		byte_event(static_cast<RoomHandle*>(p), v, false);
	};
	callbacks.on_file_received = [](void* p, lk_room_t*, const lk_file_received_t* v) {
		byte_event(static_cast<RoomHandle*>(p), v, true);
	};
	callbacks.on_chat_message_received = [](void* p, lk_room_t*, const lk_chat_message_t* v) {
		chat_event(static_cast<RoomHandle*>(p), v);
	};
	status = lk_room_set_callbacks(h->room, &callbacks);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		lk_room_destroy(h->room);
		env->DeleteGlobalRef(h->owner);
		release_classes(env, h.get());
		return 0;
	}
	return reinterpret_cast<jlong>(h.release());
}

JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroy(JNIEnv* env, jclass, jlong ptr) {
	std::unique_ptr<RoomHandle> h(handle(ptr));
	if (!h)
		return;
	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	lk_room_set_callbacks(h->room, &callbacks);
	lk_room_destroy(h->room);
	env->DeleteGlobalRef(h->owner);
	release_classes(env, h.get());
}

JNIEXPORT void JNICALL Java_io_livekit_client_Native_connect(JNIEnv* env, jclass, jlong ptr,
                                                             jbyteArray url, jbyteArray token) {
	std::string u = bytes(env, url), t = bytes(env, token);
	fail(env, lk_room_connect(handle(ptr)->room, u.c_str(), t.c_str()));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_connectWithOptions(JNIEnv* env, jclass,
                                                                        jlong ptr, jbyteArray url,
                                                                        jbyteArray token,
                                                                        jobject config) {
	std::string u = bytes(env, url), t = bytes(env, token);
	lk_room_connect_options_t options;
	lk_room_connect_options_init(&options);
	jclass cls = env->GetObjectClass(config);
	auto boolean = [env, config, cls](const char* name) {
		return env->CallBooleanMethod(config, env->GetMethodID(cls, name, "()Z")) == JNI_TRUE;
	};
	auto integer = [env, config, cls](const char* name) {
		return env->CallIntMethod(config, env->GetMethodID(cls, name, "()I"));
	};
	options.auto_subscribe = boolean("autoSubscribe") ? 1 : 0;
	options.adaptive_stream = boolean("adaptiveStream") ? 1 : 0;
	options.dynacast = boolean("dynacast") ? 1 : 0;
	options.join_retries = static_cast<uint32_t>(integer("joinRetries"));
	options.reconnect_timeout_ms = static_cast<uint32_t>(integer("reconnectTimeoutMs"));
	options.ice_transport_type = static_cast<lk_ice_transport_type_t>(integer("iceTransportType"));
	options.continual_gathering_policy =
	    static_cast<lk_continual_gathering_policy_t>(integer("continualGatheringPolicy"));
	struct OwnedIce {
		std::vector<std::string> urls;
		std::vector<const char*> url_pointers;
		std::string username;
		std::string password;
	};
	std::vector<OwnedIce> owned;
	std::vector<lk_ice_server_t> servers;
	jmethodID get_ice = env->GetMethodID(cls, "iceServers", "()[Lio/livekit/client/IceServer;");
	jobjectArray ice = static_cast<jobjectArray>(env->CallObjectMethod(config, get_ice));
	jsize count = env->GetArrayLength(ice);
	owned.reserve(static_cast<size_t>(count));
	servers.resize(static_cast<size_t>(count));
	for (jsize i = 0; i < count; ++i) {
		jobject server = env->GetObjectArrayElement(ice, i);
		jclass server_cls = env->GetObjectClass(server);
		jmethodID get_urls = env->GetMethodID(server_cls, "urls", "()[Ljava/lang/String;");
		jobjectArray urls = static_cast<jobjectArray>(env->CallObjectMethod(server, get_urls));
		OwnedIce item;
		for (jsize j = 0; j < env->GetArrayLength(urls); ++j) {
			jstring value = static_cast<jstring>(env->GetObjectArrayElement(urls, j));
			item.urls.push_back(java_string_bytes(env, value));
			env->DeleteLocalRef(value);
		}
		jstring username = static_cast<jstring>(env->CallObjectMethod(
		    server, env->GetMethodID(server_cls, "username", "()Ljava/lang/String;")));
		jstring password = static_cast<jstring>(env->CallObjectMethod(
		    server, env->GetMethodID(server_cls, "password", "()Ljava/lang/String;")));
		item.username = java_string_bytes(env, username);
		item.password = java_string_bytes(env, password);
		owned.push_back(std::move(item));
		env->DeleteLocalRef(username);
		env->DeleteLocalRef(password);
		env->DeleteLocalRef(urls);
		env->DeleteLocalRef(server_cls);
		env->DeleteLocalRef(server);
	}
	for (jsize i = 0; i < count; ++i) {
		auto& item = owned[static_cast<size_t>(i)];
		for (const auto& value : item.urls)
			item.url_pointers.push_back(value.c_str());
		auto& raw = servers[static_cast<size_t>(i)];
		lk_ice_server_init(&raw);
		raw.urls = item.url_pointers.data();
		raw.url_count = item.url_pointers.size();
		raw.username = item.username.c_str();
		raw.password = item.password.c_str();
	}
	options.ice_servers = servers.data();
	options.ice_server_count = servers.size();
	std::string key, salt;
	lk_e2ee_options_t e2ee;
	lk_e2ee_options_init(&e2ee);
	jmethodID get_e2ee = env->GetMethodID(cls, "e2ee", "()Lio/livekit/client/E2eeOptions;");
	jobject encryption = env->CallObjectMethod(config, get_e2ee);
	if (encryption != nullptr) {
		jclass e2ee_cls = env->GetObjectClass(encryption);
		jbyteArray raw_key = static_cast<jbyteArray>(
		    env->CallObjectMethod(encryption, env->GetMethodID(e2ee_cls, "sharedKey", "()[B")));
		jbyteArray raw_salt = static_cast<jbyteArray>(
		    env->CallObjectMethod(encryption, env->GetMethodID(e2ee_cls, "ratchetSalt", "()[B")));
		key = bytes(env, raw_key);
		salt = bytes(env, raw_salt);
		e2ee.enabled = env->CallBooleanMethod(
		                   encryption, env->GetMethodID(e2ee_cls, "enabled", "()Z")) == JNI_TRUE
		                   ? 1
		                   : 0;
		e2ee.key_derivation = static_cast<lk_e2ee_key_derivation_t>(
		    env->CallIntMethod(encryption, env->GetMethodID(e2ee_cls, "keyDerivation", "()I")));
		e2ee.shared_key = reinterpret_cast<const uint8_t*>(key.data());
		e2ee.shared_key_size = key.size();
		e2ee.ratchet_salt = reinterpret_cast<const uint8_t*>(salt.data());
		e2ee.ratchet_salt_size = salt.size();
		options.e2ee_options = &e2ee;
		env->DeleteLocalRef(raw_key);
		env->DeleteLocalRef(raw_salt);
		env->DeleteLocalRef(e2ee_cls);
		env->DeleteLocalRef(encryption);
	}
	env->DeleteLocalRef(ice);
	env->DeleteLocalRef(cls);
	if (env->ExceptionCheck())
		return;
	fail(env, lk_room_connect_with_options(handle(ptr)->room, u.c_str(), t.c_str(), &options));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_disconnect(JNIEnv* env, jclass, jlong ptr) {
	fail(env, lk_room_disconnect(handle(ptr)->room));
}
JNIEXPORT jint JNICALL Java_io_livekit_client_Native_state(JNIEnv*, jclass, jlong ptr) {
	return lk_room_state(handle(ptr)->room);
}
JNIEXPORT jint JNICALL Java_io_livekit_client_Native_disconnectReason(JNIEnv*, jclass, jlong ptr) {
	return lk_room_disconnect_reason(handle(ptr)->room);
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_connected(JNIEnv*, jclass, jlong ptr) {
	return lk_room_is_connected(handle(ptr)->room) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_audioPlaybackStats(JNIEnv* env, jclass,
                                                                           jlong ptr) {
	lk_audio_playback_stats_t stats;
	lk_audio_playback_stats_init(&stats);
	auto status = lk_room_audio_playback_stats(handle(ptr)->room, &stats);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/AudioPlaybackStats");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(JJJJIII)V");
	jobject result = env->NewObject(
	    cls, ctor, static_cast<jlong>(stats.queued_frames), static_cast<jlong>(stats.played_frames),
	    static_cast<jlong>(stats.dropped_frames), static_cast<jlong>(stats.underrun_frames),
	    static_cast<jint>(stats.buffered_duration_ms), static_cast<jint>(stats.device_latency_ms),
	    static_cast<jint>(stats.estimated_delay_ms));
	env->DeleteLocalRef(cls);
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setAudioOutputDevice(JNIEnv* env, jclass,
                                                                          jlong ptr,
                                                                          jbyteArray deviceId) {
	std::string id = bytes(env, deviceId);
	fail(env, lk_room_set_audio_output_device(handle(ptr)->room, id.c_str()));
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_audioOutputDevice(JNIEnv* env, jclass,
                                                                             jlong ptr) {
	auto* room = handle(ptr)->room;
	return getter(env,
	              [room](char* p, size_t n) { return lk_room_audio_output_device(room, p, n); });
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setSpeakerVolume(JNIEnv* env, jclass,
                                                                      jlong ptr, jfloat volume) {
	fail(env, lk_room_set_speaker_volume(handle(ptr)->room, volume));
}
JNIEXPORT jfloat JNICALL Java_io_livekit_client_Native_speakerVolume(JNIEnv*, jclass, jlong ptr) {
	return lk_room_speaker_volume(handle(ptr)->room);
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setSpeakerMuted(JNIEnv* env, jclass, jlong ptr,
                                                                     jboolean muted) {
	fail(env, lk_room_set_speaker_muted(handle(ptr)->room, muted ? 1 : 0));
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_speakerMuted(JNIEnv*, jclass, jlong ptr) {
	return lk_room_speaker_is_muted(handle(ptr)->room) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_republishAllTracks(JNIEnv* env, jclass,
                                                                        jlong ptr) {
	fail(env, lk_room_republish_all_tracks(handle(ptr)->room));
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_roomString(JNIEnv* env, jclass,
                                                                      jlong ptr, jint field) {
	lk_room_t* room = handle(ptr)->room;
	switch (field) {
	case 0:
		return getter(env, [room](char* p, size_t n) { return lk_room_sid(room, p, n); });
	case 1:
		return getter(env, [room](char* p, size_t n) { return lk_room_name(room, p, n); });
	case 2:
		return getter(env, [room](char* p, size_t n) { return lk_room_metadata(room, p, n); });
	default:
		return nullptr;
	}
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_localString(JNIEnv* env, jclass,
                                                                       jlong ptr, jint field) {
	lk_room_t* room = handle(ptr)->room;
	switch (field) {
	case 0:
		return getter(env,
		              [room](char* p, size_t n) { return lk_local_participant_sid(room, p, n); });
	case 1:
		return getter(
		    env, [room](char* p, size_t n) { return lk_local_participant_identity(room, p, n); });
	case 2:
		return getter(env,
		              [room](char* p, size_t n) { return lk_local_participant_name(room, p, n); });
	case 3:
		return getter(
		    env, [room](char* p, size_t n) { return lk_local_participant_metadata(room, p, n); });
	default:
		return nullptr;
	}
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setLocalString(JNIEnv* env, jclass, jlong ptr,
                                                                    jint field, jbyteArray value) {
	std::string v = bytes(env, value);
	lk_room_t* room = handle(ptr)->room;
	fail(env, field == 0 ? lk_local_participant_set_name(room, v.c_str())
	                     : lk_local_participant_set_metadata(room, v.c_str()));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setLocalAttributes(JNIEnv* env, jclass,
                                                                        jlong ptr,
                                                                        jobjectArray keys,
                                                                        jobjectArray values) {
	const jsize count = env->GetArrayLength(keys);
	if (count != env->GetArrayLength(values)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),
		              "Attribute keys and values have different lengths");
		return;
	}
	std::vector<std::string> names, contents;
	names.reserve(static_cast<size_t>(count));
	contents.reserve(static_cast<size_t>(count));
	for (jsize i = 0; i < count; ++i) {
		jbyteArray key = static_cast<jbyteArray>(env->GetObjectArrayElement(keys, i));
		jbyteArray value = static_cast<jbyteArray>(env->GetObjectArrayElement(values, i));
		names.push_back(bytes(env, key));
		contents.push_back(bytes(env, value));
		env->DeleteLocalRef(key);
		env->DeleteLocalRef(value);
	}
	std::vector<lk_attribute_t> attributes(static_cast<size_t>(count));
	for (jsize i = 0; i < count; ++i)
		attributes[static_cast<size_t>(i)] = {names[static_cast<size_t>(i)].c_str(),
		                                      contents[static_cast<size_t>(i)].c_str()};
	fail(env, lk_local_participant_set_attributes(handle(ptr)->room, attributes.data(),
	                                              attributes.size()));
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_isRecording(JNIEnv*, jclass, jlong ptr) {
	return lk_room_is_recording(handle(ptr)->room) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_publishData(JNIEnv* env, jclass, jlong ptr,
                                                                 jbyteArray data, jbyteArray topic,
                                                                 jboolean reliable,
                                                                 jobjectArray destinations) {
	std::string payload = bytes(env, data), t = bytes(env, topic);
	std::vector<std::string> ids;
	std::vector<const char*> pointers;
	if (destinations != nullptr) {
		for (jsize i = 0; i < env->GetArrayLength(destinations); ++i) {
			jbyteArray id = static_cast<jbyteArray>(env->GetObjectArrayElement(destinations, i));
			ids.emplace_back(bytes(env, id));
			env->DeleteLocalRef(id);
		}
		for (const auto& id : ids)
			pointers.push_back(id.c_str());
	}
	lk_data_publish_options_t options;
	lk_data_publish_options_init(&options);
	options.reliable = reliable ? 1 : 0;
	options.topic = topic == nullptr ? nullptr : t.c_str();
	options.destination_identities = pointers.data();
	options.destination_identity_count = pointers.size();
	fail(env,
	     lk_room_publish_data(handle(ptr)->room, reinterpret_cast<const uint8_t*>(payload.data()),
	                          payload.size(), &options));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_sendText(JNIEnv* env, jclass, jlong ptr,
                                                              jbyteArray value) {
	std::string v = bytes(env, value);
	fail(env, lk_room_send_text(handle(ptr)->room, v.c_str(), nullptr));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_sendBytes(JNIEnv* env, jclass, jlong ptr,
                                                               jbyteArray value) {
	std::string v = bytes(env, value);
	fail(env, lk_room_send_bytes(handle(ptr)->room, reinterpret_cast<const uint8_t*>(v.data()),
	                             v.size(), nullptr));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_sendFile(JNIEnv* env, jclass, jlong ptr,
                                                              jbyteArray value) {
	std::string v = bytes(env, value);
	fail(env, lk_room_send_file(handle(ptr)->room, v.c_str(), nullptr));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_sendDtmf(JNIEnv* env, jclass, jlong ptr,
                                                              jint code, jbyteArray digit) {
	std::string v = bytes(env, digit);
	fail(env, lk_room_publish_dtmf(handle(ptr)->room, static_cast<uint32_t>(code), v.c_str()));
}
JNIEXPORT jstring JNICALL Java_io_livekit_client_Native_sendChat(JNIEnv* env, jclass, jlong ptr,
                                                                 jbyteArray message) {
	std::string v = bytes(env, message);
	char id[256] = {};
	int64_t timestamp = 0;
	lk_status_t status =
	    lk_room_send_chat_message(handle(ptr)->room, v.c_str(), id, sizeof(id), &timestamp);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	return string(env, id);
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_remoteParticipants(JNIEnv* env, jclass,
                                                                                jlong ptr) {
	lk_remote_participant_list_t* list = nullptr;
	lk_status_t status = lk_room_create_remote_participant_snapshot(handle(ptr)->room, &list);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/Participant");
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>",
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	size_t count = lk_remote_participant_list_count(list);
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	for (size_t i = 0; i < count; ++i) {
		const lk_remote_participant_snapshot_t* p = nullptr;
		if (lk_remote_participant_list_at(list, i, &p) != LK_STATUS_OK)
			continue;
		auto read = [env, p](auto fn) {
			jbyteArray raw = getter(env, [p, fn](char* out, size_t n) { return fn(p, out, n); });
			std::string v = bytes(env, raw);
			env->DeleteLocalRef(raw);
			return string(env, v.c_str());
		};
		jstring sid = read(lk_remote_participant_snapshot_sid);
		jstring identity = read(lk_remote_participant_snapshot_identity);
		jstring name = read(lk_remote_participant_snapshot_name);
		jstring metadata = read(lk_remote_participant_snapshot_metadata);
		jobject item = env->NewObject(cls, ctor, sid, identity, name, metadata);
		env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(sid);
		env->DeleteLocalRef(identity);
		env->DeleteLocalRef(name);
		env->DeleteLocalRef(metadata);
	}
	env->DeleteLocalRef(cls);
	lk_remote_participant_list_destroy(list);
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setTrackSubscribed(JNIEnv* env, jclass,
                                                                        jlong ptr,
                                                                        jbyteArray participantSid,
                                                                        jbyteArray trackSid,
                                                                        jboolean subscribed) {
	std::string p = bytes(env, participantSid), t = bytes(env, trackSid);
	fail(env, lk_room_set_remote_track_subscribed(handle(ptr)->room, p.c_str(), t.c_str(),
	                                              subscribed ? 1 : 0));
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_performRpc(JNIEnv* env, jclass, jlong ptr,
                                                                   jbyteArray destination,
                                                                   jbyteArray method,
                                                                   jbyteArray payload,
                                                                   jint timeout) {
	std::string d = bytes(env, destination), m = bytes(env, method), p = bytes(env, payload);
	lk_rpc_perform_options_t options;
	lk_rpc_perform_options_init(&options);
	options.destination_identity = d.c_str();
	options.method = m.c_str();
	options.payload = p.c_str();
	options.response_timeout_ms = static_cast<uint32_t>(timeout);
	lk_rpc_result_t* result = nullptr;
	lk_status_t status = lk_room_perform_rpc(handle(ptr)->room, &options, &result);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	bool ok = lk_rpc_result_ok(result) != 0;
	auto read = [env, result](auto fn) {
		jbyteArray raw =
		    getter(env, [result, fn](char* out, size_t n) { return fn(result, out, n); });
		std::string v = bytes(env, raw);
		env->DeleteLocalRef(raw);
		return string(env, v.c_str());
	};
	jstring response = read(lk_rpc_result_payload);
	jint code = static_cast<jint>(lk_rpc_result_error_code(result));
	jstring error = read(lk_rpc_result_error_message);
	jstring errorData = read(lk_rpc_result_error_data);
	jclass cls = env->FindClass("io/livekit/client/RpcResult");
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>", "(ZLjava/lang/String;ILjava/lang/String;Ljava/lang/String;)V");
	jobject output =
	    env->NewObject(cls, ctor, ok ? JNI_TRUE : JNI_FALSE, response, code, error, errorData);
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(response);
	env->DeleteLocalRef(error);
	env->DeleteLocalRef(errorData);
	lk_rpc_result_destroy(result);
	return output;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_registerRpc(JNIEnv* env, jclass, jlong ptr,
                                                                 jbyteArray method) {
	auto* h = handle(ptr);
	auto context = std::make_unique<RpcContext>();
	context->room = h;
	context->method = bytes(env, method);
	lk_status_t status =
	    lk_room_register_rpc_method(h->room, context->method.c_str(), rpc_handler, context.get());
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return;
	}
	h->rpc_contexts.push_back(std::move(context));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_unregisterRpc(JNIEnv* env, jclass, jlong ptr,
                                                                   jbyteArray method) {
	std::string name = bytes(env, method);
	fail(env, lk_room_unregister_rpc_method(handle(ptr)->room, name.c_str()));
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_e2eeEnabled(JNIEnv*, jclass, jlong ptr) {
	return lk_room_e2ee_is_enabled(handle(ptr)->room) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setE2eeEnabled(JNIEnv* env, jclass, jlong ptr,
                                                                    jboolean enabled) {
	fail(env, lk_room_e2ee_set_enabled(handle(ptr)->room, enabled ? 1 : 0));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setSharedKey(JNIEnv* env, jclass, jlong ptr,
                                                                  jbyteArray key, jint index) {
	std::string v = bytes(env, key);
	fail(env,
	     lk_room_e2ee_set_shared_key(handle(ptr)->room, reinterpret_cast<const uint8_t*>(v.data()),
	                                 v.size(), static_cast<size_t>(index)));
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_exportSharedKey(JNIEnv* env, jclass,
                                                                           jlong ptr, jint index) {
	lk_room_t* room = handle(ptr)->room;
	size_t n = lk_room_e2ee_export_shared_key(room, static_cast<size_t>(index), nullptr, 0);
	std::vector<uint8_t> data(n);
	if (n != 0)
		lk_room_e2ee_export_shared_key(room, static_cast<size_t>(index), data.data(), data.size());
	return array(env, data.data(), data.size());
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_ratchetSharedKey(JNIEnv* env, jclass,
                                                                      jlong ptr, jint index) {
	fail(env, lk_room_e2ee_ratchet_shared_key(handle(ptr)->room, static_cast<size_t>(index)));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createAudioSource(JNIEnv* env, jclass,
                                                                        jint sampleRate,
                                                                        jint channels,
                                                                        jint queueMs) {
	lk_audio_source_options_t options;
	lk_audio_source_options_init(&options);
	options.sample_rate = static_cast<uint32_t>(sampleRate);
	options.num_channels = static_cast<uint32_t>(channels);
	options.queue_size_ms = static_cast<uint32_t>(queueMs);
	lk_audio_source_t* source = nullptr;
	lk_status_t status = lk_audio_source_create(&options, &source);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(source);
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createDeviceAudioSource(
    JNIEnv* env, jclass, jint kind, jbyteArray deviceId, jint queueMs, jboolean echoCancellation,
    jboolean autoGainControl, jboolean noiseSuppression) {
	std::string id = bytes(env, deviceId);
	lk_audio_source_t* source = nullptr;
	lk_status_t status;
	if (kind == 1) {
		lk_microphone_capture_options_t options;
		lk_microphone_capture_options_init(&options);
		options.device_id = deviceId == nullptr ? nullptr : id.c_str();
		options.queue_size_ms = static_cast<uint32_t>(queueMs);
		options.echo_cancellation = echoCancellation ? 1 : 0;
		options.auto_gain_control = autoGainControl ? 1 : 0;
		options.noise_suppression = noiseSuppression ? 1 : 0;
		status = lk_audio_source_create_microphone(&options, &source);
	} else if (kind == 2) {
		lk_system_audio_capture_options_t options;
		lk_system_audio_capture_options_init(&options);
		options.device_id = deviceId == nullptr ? nullptr : id.c_str();
		options.queue_size_ms = static_cast<uint32_t>(queueMs);
		status = lk_audio_source_create_system_audio(&options, &source);
	} else {
		env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),
		              "Invalid audio capture kind");
		return 0;
	}
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(source);
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_controlDeviceAudioSource(JNIEnv* env, jclass,
                                                                              jlong ptr, jint kind,
                                                                              jint command,
                                                                              jbyteArray deviceId) {
	auto* source = reinterpret_cast<lk_audio_source_t*>(ptr);
	std::string id = bytes(env, deviceId);
	lk_status_t status;
	if (kind == 1) {
		status = command == 0   ? lk_audio_source_microphone_start(source)
		         : command == 1 ? lk_audio_source_microphone_stop(source)
		                        : lk_audio_source_microphone_switch_device(source, id.c_str());
	} else {
		status = command == 0   ? lk_audio_source_system_audio_start(source)
		         : command == 1 ? lk_audio_source_system_audio_stop(source)
		                        : lk_audio_source_system_audio_switch_device(source, id.c_str());
	}
	fail(env, status);
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_deviceAudioSourceCapturing(JNIEnv*, jclass,
                                                                                    jlong ptr,
                                                                                    jint kind) {
	auto* source = reinterpret_cast<lk_audio_source_t*>(ptr);
	return (kind == 1 ? lk_audio_source_microphone_is_capturing(source)
	                  : lk_audio_source_system_audio_is_capturing(source))
	           ? JNI_TRUE
	           : JNI_FALSE;
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_deviceAudioSourceId(JNIEnv* env, jclass,
                                                                               jlong ptr,
                                                                               jint kind) {
	auto* source = reinterpret_cast<lk_audio_source_t*>(ptr);
	return getter(env, [source, kind](char* p, size_t n) {
		return kind == 1 ? lk_audio_source_microphone_device_id(source, p, n)
		                 : lk_audio_source_system_audio_device_id(source, p, n);
	});
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setMicrophoneMuted(JNIEnv* env, jclass,
                                                                        jlong ptr, jboolean muted) {
	fail(env, lk_audio_source_microphone_set_muted(reinterpret_cast<lk_audio_source_t*>(ptr),
	                                               muted ? 1 : 0));
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_microphoneMuted(JNIEnv*, jclass,
                                                                         jlong ptr) {
	return lk_audio_source_microphone_is_muted(reinterpret_cast<lk_audio_source_t*>(ptr))
	           ? JNI_TRUE
	           : JNI_FALSE;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setMicrophoneVolume(JNIEnv* env, jclass,
                                                                         jlong ptr, jfloat volume) {
	fail(env,
	     lk_audio_source_microphone_set_volume(reinterpret_cast<lk_audio_source_t*>(ptr), volume));
}
JNIEXPORT jfloat JNICALL Java_io_livekit_client_Native_microphoneVolume(JNIEnv*, jclass,
                                                                        jlong ptr) {
	return lk_audio_source_microphone_volume(reinterpret_cast<lk_audio_source_t*>(ptr));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyAudioSource(JNIEnv* env, jclass,
                                                                        jlong ptr) {
	fail(env, lk_audio_source_destroy(reinterpret_cast<lk_audio_source_t*>(ptr)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_captureAudio(JNIEnv* env, jclass, jlong ptr,
                                                                  jshortArray samples,
                                                                  jint samplesPerChannel) {
	std::vector<jshort> pcm(static_cast<size_t>(env->GetArrayLength(samples)));
	env->GetShortArrayRegion(samples, 0, static_cast<jsize>(pcm.size()), pcm.data());
	fail(env, lk_audio_source_capture_frame(reinterpret_cast<lk_audio_source_t*>(ptr),
	                                        reinterpret_cast<const int16_t*>(pcm.data()),
	                                        static_cast<uint32_t>(samplesPerChannel)));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createVideoSource(JNIEnv* env, jclass,
                                                                        jboolean screencast) {
	lk_video_source_options_t options;
	lk_video_source_options_init(&options);
	options.is_screencast = screencast ? 1 : 0;
	lk_video_source_t* source = nullptr;
	lk_status_t status = lk_video_source_create(&options, &source);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(source);
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createDeviceVideoSource(
    JNIEnv* env, jclass, jint kind, jbyteArray deviceId, jint width, jint height,
    jint framesPerSecond, jboolean includeCursor) {
	std::string id = bytes(env, deviceId);
	lk_video_source_t* source = nullptr;
	lk_status_t status;
	if (kind == 1) {
		lk_camera_capture_options_t options;
		lk_camera_capture_options_init(&options);
		options.device_id = deviceId == nullptr ? nullptr : id.c_str();
		options.width = static_cast<uint32_t>(width);
		options.height = static_cast<uint32_t>(height);
		options.frames_per_second = static_cast<uint32_t>(framesPerSecond);
		status = lk_video_source_create_camera(&options, &source);
	} else if (kind == 2) {
		lk_screen_capture_options_t options;
		lk_screen_capture_options_init(&options);
		options.source_id = id.c_str();
		options.frames_per_second = static_cast<uint32_t>(framesPerSecond);
		options.include_cursor = includeCursor ? 1 : 0;
		status = lk_video_source_create_screen(&options, &source);
	} else {
		env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),
		              "Invalid video capture kind");
		return 0;
	}
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(source);
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_controlDeviceVideoSource(JNIEnv* env, jclass,
                                                                              jlong ptr, jint kind,
                                                                              jint command,
                                                                              jbyteArray deviceId) {
	auto* source = reinterpret_cast<lk_video_source_t*>(ptr);
	std::string id = bytes(env, deviceId);
	lk_status_t status;
	if (kind == 1) {
		status = command == 0   ? lk_video_source_camera_start(source)
		         : command == 1 ? lk_video_source_camera_stop(source)
		                        : lk_video_source_camera_switch_device(source, id.c_str());
	} else {
		status = command == 0   ? lk_video_source_screen_start(source)
		         : command == 1 ? lk_video_source_screen_stop(source)
		                        : lk_video_source_screen_switch_source(source, id.c_str());
	}
	fail(env, status);
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_deviceVideoSourceCapturing(JNIEnv*, jclass,
                                                                                    jlong ptr,
                                                                                    jint kind) {
	auto* source = reinterpret_cast<lk_video_source_t*>(ptr);
	return (kind == 1 ? lk_video_source_camera_is_capturing(source)
	                  : lk_video_source_screen_is_capturing(source))
	           ? JNI_TRUE
	           : JNI_FALSE;
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_deviceVideoSourceId(JNIEnv* env, jclass,
                                                                               jlong ptr,
                                                                               jint kind) {
	auto* source = reinterpret_cast<lk_video_source_t*>(ptr);
	return getter(env, [source, kind](char* p, size_t n) {
		return kind == 1 ? lk_video_source_camera_device_id(source, p, n)
		                 : lk_video_source_screen_source_id(source, p, n);
	});
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyVideoSource(JNIEnv* env, jclass,
                                                                        jlong ptr) {
	fail(env, lk_video_source_destroy(reinterpret_cast<lk_video_source_t*>(ptr)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_captureI420(JNIEnv* env, jclass, jlong ptr,
                                                                 jbyteArray data, jint width,
                                                                 jint height, jlong timestampUs) {
	std::string frame = bytes(env, data);
	fail(env, lk_video_source_capture_i420(reinterpret_cast<lk_video_source_t*>(ptr),
	                                       reinterpret_cast<const uint8_t*>(frame.data()),
	                                       frame.size(), static_cast<uint32_t>(width),
	                                       static_cast<uint32_t>(height), timestampUs));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createTrack(JNIEnv* env, jclass, jlong room,
                                                                  jbyteArray label, jlong source,
                                                                  jboolean video) {
	std::string name = bytes(env, label);
	lk_local_track_t* track = nullptr;
	lk_status_t status =
	    video ? lk_room_create_video_track(handle(room)->room, name.c_str(),
	                                       reinterpret_cast<lk_video_source_t*>(source), &track)
	          : lk_room_create_audio_track(handle(room)->room, name.c_str(),
	                                       reinterpret_cast<lk_audio_source_t*>(source), &track);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(track);
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_publishTrack(JNIEnv* env, jclass, jlong room,
                                                                  jlong track, jint source) {
	lk_track_publish_options_t options;
	lk_track_publish_options_init(&options);
	options.source = static_cast<lk_track_source_t>(source);
	fail(env, lk_local_track_publish(handle(room)->room, reinterpret_cast<lk_local_track_t*>(track),
	                                 &options));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_publishScreenShareTrack(JNIEnv* env, jclass,
                                                                             jlong room,
                                                                             jlong track,
                                                                             jboolean video) {
	lk_track_publish_options_t options;
	lk_track_publish_options_init(&options);
	auto* native = reinterpret_cast<lk_local_track_t*>(track);
	fail(env,
	     video ? lk_local_track_publish_screen_share_video(handle(room)->room, native, &options)
	           : lk_local_track_publish_screen_share_audio(handle(room)->room, native, &options));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_unpublishTrack(JNIEnv* env, jclass,
                                                                    jlong ptr) {
	fail(env, lk_local_track_unpublish(reinterpret_cast<lk_local_track_t*>(ptr), 0));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyTrack(JNIEnv* env, jclass, jlong ptr) {
	fail(env, lk_local_track_destroy(reinterpret_cast<lk_local_track_t*>(ptr)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_muteTrack(JNIEnv* env, jclass, jlong ptr,
                                                               jboolean muted) {
	fail(env, lk_local_track_set_muted(reinterpret_cast<lk_local_track_t*>(ptr), muted ? 1 : 0));
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_trackStats(JNIEnv* env, jclass,
                                                                      jlong ptr) {
	auto* track = reinterpret_cast<lk_local_track_t*>(ptr);
	return getter(env,
	              [track](char* p, size_t n) { return lk_local_track_rtc_stats(track, p, n); });
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_mediaDevices(JNIEnv* env, jclass) {
	lk_media_device_list_t* devices = nullptr;
	lk_status_t status = lk_media_device_list_create(&devices);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/MediaDevice");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;IZ)V");
	size_t count = lk_media_device_list_count(devices);
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	for (size_t i = 0; i < count; ++i) {
		lk_media_device_info_t info{};
		info.struct_size = sizeof(info);
		status = lk_media_device_list_info(devices, i, &info);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			break;
		}
		auto read = [env, devices, i](auto fn) {
			jbyteArray raw =
			    getter(env, [devices, i, fn](char* p, size_t n) { return fn(devices, i, p, n); });
			std::string v = bytes(env, raw);
			env->DeleteLocalRef(raw);
			return string(env, v.c_str());
		};
		jstring id = read(lk_media_device_list_id);
		jstring label = read(lk_media_device_list_label);
		jobject item = env->NewObject(cls, ctor, id, label, static_cast<jint>(info.kind),
		                              info.is_default ? JNI_TRUE : JNI_FALSE);
		env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(id);
		env->DeleteLocalRef(label);
	}
	env->DeleteLocalRef(cls);
	lk_media_device_list_destroy(devices);
	return result;
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_screenSources(JNIEnv* env, jclass) {
	lk_screen_source_list_t* sources = nullptr;
	lk_status_t status = lk_screen_source_list_create(&sources);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/ScreenSource");
	jmethodID ctor =
	    env->GetMethodID(cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;IIIII)V");
	size_t count = lk_screen_source_list_count(sources);
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	for (size_t i = 0; i < count; ++i) {
		lk_screen_source_info_t info{};
		info.struct_size = sizeof(info);
		status = lk_screen_source_list_info(sources, i, &info);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			break;
		}
		auto read = [env, sources, i](auto fn) {
			jbyteArray raw =
			    getter(env, [sources, i, fn](char* p, size_t n) { return fn(sources, i, p, n); });
			std::string v = bytes(env, raw);
			env->DeleteLocalRef(raw);
			return string(env, v.c_str());
		};
		jstring id = read(lk_screen_source_list_id);
		jstring label = read(lk_screen_source_list_label);
		jobject item =
		    env->NewObject(cls, ctor, id, label, static_cast<jint>(info.kind),
		                   static_cast<jint>(info.x), static_cast<jint>(info.y),
		                   static_cast<jint>(info.width), static_cast<jint>(info.height));
		env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(id);
		env->DeleteLocalRef(label);
	}
	env->DeleteLocalRef(cls);
	lk_screen_source_list_destroy(sources);
	return result;
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_publishDataTrack(JNIEnv* env, jclass,
                                                                       jlong room, jbyteArray name,
                                                                       jint frameEncoding) {
	std::string name_value = bytes(env, name);
	lk_data_track_publish_options_t options;
	lk_data_track_publish_options_init(&options);
	options.name = name_value.c_str();
	options.has_frame_encoding = frameEncoding != LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED;
	options.frame_encoding = static_cast<lk_data_track_frame_encoding_t>(frameEncoding);
	lk_local_data_track_t* track = nullptr;
	auto status = lk_room_publish_data_track(handle(room)->room, &options, &track);
	if (status != LK_DATA_TRACK_ERROR_NONE) {
		fail(env, static_cast<lk_status_t>(status));
		return 0;
	}
	return reinterpret_cast<jlong>(track);
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_pushDataTrack(JNIEnv* env, jclass, jlong ptr,
                                                                   jbyteArray data,
                                                                   jboolean hasTimestamp,
                                                                   jlong timestamp) {
	std::string payload = bytes(env, data);
	auto status = lk_local_data_track_try_push(reinterpret_cast<lk_local_data_track_t*>(ptr),
	                                           reinterpret_cast<const uint8_t*>(payload.data()),
	                                           payload.size(), hasTimestamp == JNI_TRUE ? 1 : 0,
	                                           static_cast<uint64_t>(timestamp));
	fail(env, static_cast<lk_status_t>(status));
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_dataTrackSid(JNIEnv* env, jclass,
                                                                        jlong ptr) {
	auto* track = reinterpret_cast<lk_local_data_track_t*>(ptr);
	return getter(env, [track](char* p, size_t n) { return lk_local_data_track_sid(track, p, n); });
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_unpublishDataTrack(JNIEnv* env, jclass,
                                                                        jlong ptr) {
	auto* track = reinterpret_cast<lk_local_data_track_t*>(ptr);
	if (lk_local_data_track_is_published(track))
		fail(env, static_cast<lk_status_t>(lk_local_data_track_unpublish(track)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyDataTrack(JNIEnv* env, jclass,
                                                                      jlong ptr) {
	auto* track = reinterpret_cast<lk_local_data_track_t*>(ptr);
	if (lk_local_data_track_is_published(track)) {
		auto status = lk_local_data_track_unpublish(track);
		if (status != LK_DATA_TRACK_ERROR_NONE && status != LK_DATA_TRACK_ERROR_DISCONNECTED) {
			fail(env, static_cast<lk_status_t>(status));
			return;
		}
	}
	fail(env, static_cast<lk_status_t>(lk_local_data_track_destroy(track)));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_subscribeDataTrack(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray trackSid,
    jint bufferCapacity) {
	std::string identity = bytes(env, participantIdentity);
	std::string sid = bytes(env, trackSid);
	lk_data_track_subscription_options_t options;
	lk_data_track_subscription_options_init(&options);
	options.buffer_capacity = static_cast<size_t>(bufferCapacity);
	lk_data_track_reader_t* reader = nullptr;
	auto status = lk_room_subscribe_data_track(handle(room)->room, identity.c_str(), sid.c_str(),
	                                           &options, &reader);
	if (status != LK_DATA_TRACK_ERROR_NONE) {
		fail(env, static_cast<lk_status_t>(status));
		return 0;
	}
	return reinterpret_cast<jlong>(reader);
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_remoteDataTracks(JNIEnv* env, jclass,
                                                                              jlong room) {
	lk_remote_data_track_list_t* snapshot = nullptr;
	auto status = lk_room_create_remote_data_track_snapshot(handle(room)->room, &snapshot);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/RemoteDataTrack");
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IZ)V");
	const size_t count = lk_remote_data_track_list_count(snapshot);
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	for (size_t i = 0; i < count; ++i) {
		const lk_remote_data_track_snapshot_t* track = nullptr;
		status = lk_remote_data_track_list_at(snapshot, i, &track);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			break;
		}
		lk_data_track_snapshot_info_t info;
		lk_data_track_snapshot_info_init(&info);
		status = lk_remote_data_track_snapshot_info(track, &info);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			break;
		}
		auto read = [env, track](auto fn) {
			jbyteArray raw =
			    getter(env, [track, fn](char* p, size_t n) { return fn(track, p, n); });
			std::string value = bytes(env, raw);
			env->DeleteLocalRef(raw);
			return string(env, value.c_str());
		};
		jstring identity = read(lk_remote_data_track_snapshot_publisher_identity);
		jstring sid = read(lk_remote_data_track_snapshot_sid);
		jstring name = read(lk_remote_data_track_snapshot_name);
		jobject item =
		    env->NewObject(cls, ctor, identity, sid, name,
		                   info.has_frame_encoding ? static_cast<jint>(info.frame_encoding) : 0,
		                   info.is_published ? JNI_TRUE : JNI_FALSE);
		env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(identity);
		env->DeleteLocalRef(sid);
		env->DeleteLocalRef(name);
	}
	env->DeleteLocalRef(cls);
	lk_remote_data_track_list_destroy(snapshot);
	return result;
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_readDataTrack(JNIEnv* env, jclass,
                                                                      jlong ptr, jint timeoutMs) {
	auto* reader = reinterpret_cast<lk_data_track_reader_t*>(ptr);
	lk_data_track_frame_t* frame = nullptr;
	auto status = lk_data_track_reader_read_for(reader, static_cast<uint32_t>(timeoutMs), &frame);
	if (status == LK_DATA_TRACK_READ_EMPTY || status == LK_DATA_TRACK_READ_CLOSED)
		return nullptr;
	if (status != LK_DATA_TRACK_READ_FRAME) {
		fail(env, static_cast<lk_status_t>(LK_DATA_TRACK_ERROR_INVALID_ARGUMENT));
		return nullptr;
	}
	const size_t size = lk_data_track_frame_data(frame, nullptr, 0);
	std::vector<uint8_t> payload(size);
	if (size != 0)
		lk_data_track_frame_data(frame, payload.data(), size);
	jbyteArray data = array(env, payload.data(), size);
	jclass cls = env->FindClass("io/livekit/client/DataTrackFrame");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "([BJZ)V");
	jobject result = env->NewObject(
	    cls, ctor, data, static_cast<jlong>(lk_data_track_frame_user_timestamp(frame)),
	    lk_data_track_frame_has_user_timestamp(frame) ? JNI_TRUE : JNI_FALSE);
	env->DeleteLocalRef(data);
	env->DeleteLocalRef(cls);
	lk_data_track_frame_destroy(frame);
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyDataTrackReader(JNIEnv*, jclass,
                                                                            jlong ptr) {
	lk_data_track_reader_destroy(reinterpret_cast<lk_data_track_reader_t*>(ptr));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createAudioStream(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray trackSid,
    jint capacity) {
	std::string identity = bytes(env, participantIdentity), sid = bytes(env, trackSid);
	lk_media_stream_options_t options;
	lk_media_stream_options_init(&options);
	options.capacity = static_cast<size_t>(capacity);
	lk_audio_stream_t* stream = nullptr;
	auto status = lk_room_create_audio_stream(handle(room)->room, identity.c_str(), sid.c_str(),
	                                          &options, &stream);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(stream);
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createVideoStream(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray trackSid,
    jint capacity) {
	std::string identity = bytes(env, participantIdentity), sid = bytes(env, trackSid);
	lk_media_stream_options_t options;
	lk_media_stream_options_init(&options);
	options.capacity = static_cast<size_t>(capacity);
	lk_video_stream_t* stream = nullptr;
	auto status = lk_room_create_video_stream(handle(room)->room, identity.c_str(), sid.c_str(),
	                                          &options, &stream);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(stream);
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_readAudioStream(JNIEnv* env, jclass,
                                                                        jlong ptr, jint timeoutMs) {
	lk_owned_audio_frame_t* raw = nullptr;
	auto status = lk_audio_stream_read_for(reinterpret_cast<lk_audio_stream_t*>(ptr),
	                                       static_cast<uint32_t>(timeoutMs), &raw);
	if (status == LK_MEDIA_STREAM_READ_EMPTY || status == LK_MEDIA_STREAM_READ_CLOSED)
		return nullptr;
	if (status != LK_MEDIA_STREAM_READ_FRAME) {
		fail(env, LK_STATUS_INVALID_ARGUMENT);
		return nullptr;
	}
	std::unique_ptr<lk_owned_audio_frame_t, decltype(&lk_owned_audio_frame_destroy)> frame(
	    raw, lk_owned_audio_frame_destroy);
	lk_audio_frame_t view{};
	if (auto result = lk_owned_audio_frame_data(frame.get(), &view); result != LK_STATUS_OK) {
		fail(env, result);
		return nullptr;
	}
	if (view.sample_count > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
		              "Audio frame is too large");
		return nullptr;
	}
	jshortArray samples = env->NewShortArray(static_cast<jsize>(view.sample_count));
	if (samples == nullptr)
		return nullptr;
	if (view.sample_count != 0)
		env->SetShortArrayRegion(samples, 0, static_cast<jsize>(view.sample_count), view.data);
	jclass cls = env->FindClass("io/livekit/client/AudioFrame");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "([SIII)V");
	jobject result = env->NewObject(cls, ctor, samples, static_cast<jint>(view.sample_rate),
	                                static_cast<jint>(view.num_channels),
	                                static_cast<jint>(view.samples_per_channel));
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(samples);
	return result;
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_readVideoStream(JNIEnv* env, jclass,
                                                                        jlong ptr, jint timeoutMs) {
	lk_owned_video_frame_t* raw = nullptr;
	auto status = lk_video_stream_read_for(reinterpret_cast<lk_video_stream_t*>(ptr),
	                                       static_cast<uint32_t>(timeoutMs), &raw);
	if (status == LK_MEDIA_STREAM_READ_EMPTY || status == LK_MEDIA_STREAM_READ_CLOSED)
		return nullptr;
	if (status != LK_MEDIA_STREAM_READ_FRAME) {
		fail(env, LK_STATUS_INVALID_ARGUMENT);
		return nullptr;
	}
	std::unique_ptr<lk_owned_video_frame_t, decltype(&lk_owned_video_frame_destroy)> frame(
	    raw, lk_owned_video_frame_destroy);
	lk_video_frame_t view{};
	if (auto result = lk_owned_video_frame_data(frame.get(), &view); result != LK_STATUS_OK) {
		fail(env, result);
		return nullptr;
	}
	lk_video_frame_metadata_t metadata;
	lk_video_frame_metadata_init(&metadata);
	if (auto result = lk_owned_video_frame_metadata(frame.get(), &metadata);
	    result != LK_STATUS_OK) {
		fail(env, result);
		return nullptr;
	}
	if (view.data_size > static_cast<size_t>(INT32_MAX) ||
	    metadata.user_data_size > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
		              "Video frame is too large");
		return nullptr;
	}
	jbyteArray data = array(env, view.data, view.data_size);
	jbyteArray userData =
	    metadata.has_user_data ? array(env, metadata.user_data, metadata.user_data_size) : nullptr;
	jobject userTimestamp = nullptr, frameId = nullptr;
	if (metadata.has_user_timestamp_us) {
		jclass number = env->FindClass("java/lang/Long");
		userTimestamp = env->NewObject(number, env->GetMethodID(number, "<init>", "(J)V"),
		                               static_cast<jlong>(metadata.user_timestamp_us));
		env->DeleteLocalRef(number);
	}
	if (metadata.has_frame_id) {
		jclass number = env->FindClass("java/lang/Integer");
		frameId = env->NewObject(number, env->GetMethodID(number, "<init>", "(I)V"),
		                         static_cast<jint>(metadata.frame_id));
		env->DeleteLocalRef(number);
	}
	jclass cls = env->FindClass("io/livekit/client/VideoFrame");
	jmethodID ctor =
	    env->GetMethodID(cls, "<init>", "([BIIJLjava/lang/Long;Ljava/lang/Integer;[B)V");
	jobject result = env->NewObject(
	    cls, ctor, data, static_cast<jint>(view.width), static_cast<jint>(view.height),
	    static_cast<jlong>(view.timestamp_us), userTimestamp, frameId, userData);
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(data);
	if (userData)
		env->DeleteLocalRef(userData);
	if (userTimestamp)
		env->DeleteLocalRef(userTimestamp);
	if (frameId)
		env->DeleteLocalRef(frameId);
	return result;
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_audioStreamDroppedFrames(JNIEnv*, jclass,
                                                                               jlong ptr) {
	return static_cast<jlong>(
	    lk_audio_stream_dropped_frames(reinterpret_cast<lk_audio_stream_t*>(ptr)));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_videoStreamDroppedFrames(JNIEnv*, jclass,
                                                                               jlong ptr) {
	return static_cast<jlong>(
	    lk_video_stream_dropped_frames(reinterpret_cast<lk_video_stream_t*>(ptr)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyAudioStream(JNIEnv*, jclass,
                                                                        jlong ptr) {
	lk_audio_stream_destroy(reinterpret_cast<lk_audio_stream_t*>(ptr));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyVideoStream(JNIEnv*, jclass,
                                                                        jlong ptr) {
	lk_video_stream_destroy(reinterpret_cast<lk_video_stream_t*>(ptr));
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_remoteTrackPublications(JNIEnv* env,
                                                                                     jclass,
                                                                                     jlong room) {
	lk_remote_participant_list_t* raw = nullptr;
	auto status = lk_room_create_remote_participant_snapshot(handle(room)->room, &raw);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_remote_participant_list_t, decltype(&lk_remote_participant_list_destroy)>
	    snapshot(raw, lk_remote_participant_list_destroy);
	const size_t participant_count = lk_remote_participant_list_count(snapshot.get());
	size_t publication_count = 0;
	for (size_t i = 0; i < participant_count; ++i) {
		const lk_remote_participant_snapshot_t* participant = nullptr;
		status = lk_remote_participant_list_at(snapshot.get(), i, &participant);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			return nullptr;
		}
		publication_count += lk_remote_participant_snapshot_publication_count(participant);
	}
	if (publication_count > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
		              "Too many remote track publications");
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/RemoteTrackPublication");
	jmethodID ctor = env->GetMethodID(cls, "<init>",
	                                  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
	                                  "Ljava/lang/String;IIZII)V");
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(publication_count), cls, nullptr);
	jsize output_index = 0;
	for (size_t i = 0; i < participant_count; ++i) {
		const lk_remote_participant_snapshot_t* participant = nullptr;
		status = lk_remote_participant_list_at(snapshot.get(), i, &participant);
		if (status != LK_STATUS_OK)
			break;
		jbyteArray identity_bytes = getter(env, [participant](char* p, size_t n) {
			return lk_remote_participant_snapshot_identity(participant, p, n);
		});
		std::string identity_value = bytes(env, identity_bytes);
		env->DeleteLocalRef(identity_bytes);
		jstring identity = string(env, identity_value.c_str());
		const size_t count = lk_remote_participant_snapshot_publication_count(participant);
		for (size_t j = 0; j < count; ++j) {
			const lk_remote_track_publication_snapshot_t* publication = nullptr;
			status = lk_remote_participant_snapshot_publication_at(participant, j, &publication);
			if (status != LK_STATUS_OK)
				break;
			lk_remote_track_publication_snapshot_info_t info;
			lk_remote_track_publication_snapshot_info_init(&info);
			status = lk_remote_track_publication_snapshot_info(publication, &info);
			if (status != LK_STATUS_OK)
				break;
			auto read = [env, publication](auto fn) {
				jbyteArray raw_value = getter(
				    env, [publication, fn](char* p, size_t n) { return fn(publication, p, n); });
				std::string value = bytes(env, raw_value);
				env->DeleteLocalRef(raw_value);
				return string(env, value.c_str());
			};
			jstring sid = read(lk_remote_track_publication_snapshot_sid);
			jstring name = read(lk_remote_track_publication_snapshot_name);
			jstring mime = read(lk_remote_track_publication_snapshot_mime_type);
			jobject item =
			    env->NewObject(cls, ctor, identity, sid, name, mime, static_cast<jint>(info.kind),
			                   static_cast<jint>(info.source), info.is_muted ? JNI_TRUE : JNI_FALSE,
			                   static_cast<jint>(info.width), static_cast<jint>(info.height));
			env->SetObjectArrayElement(result, output_index++, item);
			env->DeleteLocalRef(item);
			env->DeleteLocalRef(sid);
			env->DeleteLocalRef(name);
			env->DeleteLocalRef(mime);
		}
		env->DeleteLocalRef(identity);
		if (status != LK_STATUS_OK)
			break;
	}
	env->DeleteLocalRef(cls);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	return result;
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_startTrackRecording(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray trackSid,
    jbyteArray outputPath, jint queueCapacity) {
	std::string identity = bytes(env, participantIdentity), sid = bytes(env, trackSid);
	std::string path = bytes(env, outputPath);
	lk_track_recorder_options_t options;
	lk_track_recorder_options_init(&options);
	options.output_path = path.c_str();
	options.queue_capacity = static_cast<size_t>(queueCapacity);
	lk_track_recorder_t* recorder = nullptr;
	auto status = lk_room_start_track_recording(handle(room)->room, identity.c_str(), sid.c_str(),
	                                            &options, &recorder);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(recorder);
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_trackRecorderStats(JNIEnv* env, jclass,
                                                                           jlong ptr) {
	lk_track_recorder_stats_t stats;
	lk_track_recorder_stats_init(&stats);
	auto status = lk_track_recorder_get_stats(reinterpret_cast<lk_track_recorder_t*>(ptr), &stats);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/TrackRecorderStats");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(IJJJ)V");
	jobject result = env->NewObject(
	    cls, ctor, static_cast<jint>(stats.state), static_cast<jlong>(stats.frames_written),
	    static_cast<jlong>(stats.bytes_written), static_cast<jlong>(stats.frames_dropped));
	env->DeleteLocalRef(cls);
	return result;
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_trackRecorderString(JNIEnv* env, jclass,
                                                                               jlong ptr,
                                                                               jboolean error) {
	auto* recorder = reinterpret_cast<lk_track_recorder_t*>(ptr);
	return getter(env, [recorder, error](char* p, size_t n) {
		return error ? lk_track_recorder_error(recorder, p, n)
		             : lk_track_recorder_output_path(recorder, p, n);
	});
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_stopTrackRecorder(JNIEnv*, jclass, jlong ptr) {
	lk_track_recorder_stop(reinterpret_cast<lk_track_recorder_t*>(ptr));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyTrackRecorder(JNIEnv*, jclass,
                                                                          jlong ptr) {
	lk_track_recorder_destroy(reinterpret_cast<lk_track_recorder_t*>(ptr));
}
}
