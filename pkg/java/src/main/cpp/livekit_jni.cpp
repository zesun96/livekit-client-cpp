#include <jni.h>

#include <livekit/capi/livekit.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
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
	jclass text_stream_class = nullptr;
	jclass byte_stream_class = nullptr;
	jclass token_request_class = nullptr;
	jclass remote_track_class = nullptr;
	jclass permissions_class = nullptr;
	std::atomic<bool> frame_callbacks_enabled{false};
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
	if (h->text_stream_class)
		env->DeleteGlobalRef(h->text_stream_class);
	if (h->byte_stream_class)
		env->DeleteGlobalRef(h->byte_stream_class);
	if (h->token_request_class)
		env->DeleteGlobalRef(h->token_request_class);
	if (h->remote_track_class)
		env->DeleteGlobalRef(h->remote_track_class);
	if (h->permissions_class)
		env->DeleteGlobalRef(h->permissions_class);
}

void throw_livekit_error(JNIEnv* env, lk_status_t status, lk_error_domain_t domain, int code) {
	const char* error = lk_last_error();
	jclass cls = env->FindClass("io/livekit/client/LiveKitException");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(IIILjava/lang/String;)V");
	jstring message = string(env, error == nullptr ? "LiveKit operation failed" : error);
	jobject exception = env->NewObject(cls, ctor, static_cast<jint>(status),
	                                   static_cast<jint>(domain), static_cast<jint>(code), message);
	env->Throw(static_cast<jthrowable>(exception));
	env->DeleteLocalRef(exception);
	env->DeleteLocalRef(message);
	env->DeleteLocalRef(cls);
}

void fail(JNIEnv* env, lk_status_t status) {
	if (status == LK_STATUS_OK)
		return;
	lk_error_info_t info;
	lk_error_info_init(&info);
	lk_last_error_info(&info);
	const auto domain = info.domain == LK_ERROR_DOMAIN_NONE ? LK_ERROR_DOMAIN_STATUS : info.domain;
	const int code = info.domain == LK_ERROR_DOMAIN_NONE ? static_cast<int>(status) : info.code;
	throw_livekit_error(env, status, domain, code);
}

void fail_data_track(JNIEnv* env, lk_data_track_error_code_t code) {
	if (code != LK_DATA_TRACK_ERROR_NONE)
		throw_livekit_error(env, LK_STATUS_OPERATION_FAILED, LK_ERROR_DOMAIN_DATA_TRACK,
		                    static_cast<int>(code));
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
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;FIZZ)V");
	jstring sid = string(env, value->sid);
	jstring identity = string(env, value->identity);
	jstring name = string(env, value->name);
	jstring metadata = string(env, value->metadata);
	jobject result = env->NewObject(cls, ctor, sid, identity, name, metadata, value->audio_level,
	                                static_cast<jint>(value->connection_quality),
	                                value->is_speaking == 0 ? JNI_FALSE : JNI_TRUE,
	                                value->is_local == 0 ? JNI_FALSE : JNI_TRUE);
	env->DeleteLocalRef(sid);
	env->DeleteLocalRef(identity);
	env->DeleteLocalRef(name);
	env->DeleteLocalRef(metadata);
	return result;
}

jobject permissions(JNIEnv* env, RoomHandle* h, const lk_participant_permissions_t* value) {
	if (value == nullptr)
		return nullptr;
	if (value->can_publish_source_count > static_cast<size_t>(INT32_MAX) ||
	    (value->can_publish_source_count != 0 && value->can_publish_sources == nullptr))
		return nullptr;
	jintArray sources = env->NewIntArray(static_cast<jsize>(value->can_publish_source_count));
	if (sources == nullptr)
		return nullptr;
	std::vector<jint> copied;
	copied.reserve(value->can_publish_source_count);
	for (size_t i = 0; i < value->can_publish_source_count; ++i)
		copied.push_back(static_cast<jint>(value->can_publish_sources[i]));
	if (!copied.empty())
		env->SetIntArrayRegion(sources, 0, static_cast<jsize>(copied.size()), copied.data());
	if (env->ExceptionCheck()) {
		env->DeleteLocalRef(sources);
		return nullptr;
	}
	jmethodID ctor = env->GetMethodID(h->permissions_class, "<init>", "(ZZZ[IZZZZZZ)V");
	if (ctor == nullptr) {
		env->DeleteLocalRef(sources);
		return nullptr;
	}
	jobject result = env->NewObject(
	    h->permissions_class, ctor, value->can_subscribe ? JNI_TRUE : JNI_FALSE,
	    value->can_publish ? JNI_TRUE : JNI_FALSE, value->can_publish_data ? JNI_TRUE : JNI_FALSE,
	    sources, value->hidden ? JNI_TRUE : JNI_FALSE, value->recorder ? JNI_TRUE : JNI_FALSE,
	    value->can_update_metadata ? JNI_TRUE : JNI_FALSE, value->agent ? JNI_TRUE : JNI_FALSE,
	    value->can_subscribe_metrics ? JNI_TRUE : JNI_FALSE,
	    value->can_manage_agent_session ? JNI_TRUE : JNI_FALSE);
	env->DeleteLocalRef(sources);
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

void room_string_event(RoomHandle* h, int kind, const char* previous, const char* current) {
	deliver(h, [h, kind, previous, current](JNIEnv* env) {
		jstring old_value = string(env, previous);
		jstring new_value = string(env, current);
		jclass cls = env->GetObjectClass(h->owner);
		jmethodID callback =
		    env->GetMethodID(cls, "onNativeRoomString", "(ILjava/lang/String;Ljava/lang/String;)V");
		env->CallVoidMethod(h->owner, callback, static_cast<jint>(kind), old_value, new_value);
		env->DeleteLocalRef(cls);
		env->DeleteLocalRef(old_value);
		env->DeleteLocalRef(new_value);
	});
}

void room_snapshot_event(RoomHandle* h, int kind, const lk_room_snapshot_t* value) {
	if (value == nullptr)
		return;
	deliver(h, [h, kind, value](JNIEnv* env) {
		if (env->PushLocalFrame(16) != JNI_OK)
			return;
		jstring sid = string(env, value->sid);
		jstring name = env->ExceptionCheck() ? nullptr : string(env, value->name);
		jstring metadata = env->ExceptionCheck() ? nullptr : string(env, value->metadata);
		if (!env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(
			              owner, "onNativeRoomSnapshot",
			              "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, static_cast<jint>(kind), sid, name,
				                    metadata, value->is_recording ? JNI_TRUE : JNI_FALSE);
		}
		env->PopLocalFrame(nullptr);
	});
}

jlong bounded_long(uint64_t value) {
	return static_cast<jlong>(
	    std::min(value, static_cast<uint64_t>(std::numeric_limits<jlong>::max())));
}

void sip_dtmf_event(RoomHandle* h, const lk_sip_dtmf_t* value) {
	if (value == nullptr)
		return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(16) != JNI_OK)
			return;
		jstring digit = string(env, value->digit);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		if (!env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback = owner == nullptr
			                         ? nullptr
			                         : env->GetMethodID(owner, "onNativeSipDtmf",
			                                            "(JLjava/lang/String;Ljava/lang/String;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, static_cast<jlong>(value->code), digit,
				                    identity);
		}
		env->PopLocalFrame(nullptr);
	});
}

void data_channel_event(RoomHandle* h, const lk_data_channel_buffer_status_t* value) {
	if (value == nullptr)
		return;
	deliver(h, [h, value](JNIEnv* env) {
		jclass owner = env->GetObjectClass(h->owner);
		if (owner == nullptr)
			return;
		jmethodID callback = env->GetMethodID(owner, "onNativeDataChannelBufferStatus", "(ZJJJZ)V");
		if (callback != nullptr)
			env->CallVoidMethod(
			    h->owner, callback, value->reliable ? JNI_TRUE : JNI_FALSE,
			    bounded_long(value->buffered_amount), bounded_long(value->high_water_mark),
			    bounded_long(value->low_water_mark), value->backpressured ? JNI_TRUE : JNI_FALSE);
		env->DeleteLocalRef(owner);
	});
}

void encryption_event(RoomHandle* h, const lk_encryption_state_t* value) {
	if (value == nullptr)
		return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(16) != JNI_OK)
			return;
		jstring track = string(env, value->track_id);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		if (!env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr ? nullptr
			                     : env->GetMethodID(owner, "onNativeEncryptionState",
			                                        "(Ljava/lang/String;Ljava/lang/String;IIZJI)V");
			if (callback != nullptr)
				env->CallVoidMethod(
				    h->owner, callback, track, identity, static_cast<jint>(value->kind),
				    static_cast<jint>(value->direction), value->enabled ? JNI_TRUE : JNI_FALSE,
				    bounded_long(value->key_index), static_cast<jint>(value->state));
		}
		env->PopLocalFrame(nullptr);
	});
}

void data_track_event(RoomHandle* h, int kind, const lk_data_track_info_t* track,
                      const lk_participant_info_t* who,
                      const lk_data_track_frame_view_t* frame = nullptr) {
	if (track == nullptr ||
	    (frame != nullptr && (frame->data_size > static_cast<size_t>(INT32_MAX) ||
	                          (frame->data_size != 0 && frame->data == nullptr))))
		return;
	deliver(h, [h, kind, track, who, frame](JNIEnv* env) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jstring identity = string(env, who == nullptr ? nullptr : who->identity);
		jstring sid = env->ExceptionCheck() ? nullptr : string(env, track->sid);
		jstring name = env->ExceptionCheck() ? nullptr : string(env, track->name);
		jstring frame_encoding =
		    env->ExceptionCheck() ? nullptr : string(env, track->custom_frame_encoding);
		jstring schema_name = env->ExceptionCheck() ? nullptr : string(env, track->schema_name);
		jstring schema_encoding =
		    env->ExceptionCheck() ? nullptr : string(env, track->custom_schema_encoding);
		jbyteArray data = frame == nullptr || env->ExceptionCheck()
		                      ? nullptr
		                      : array(env, frame->data, frame->data_size);
		if (!env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(
			              owner, "onNativeDataTrack",
			              "(ILjava/lang/String;ILjava/lang/String;Ljava/lang/String;ZZI"
			              "Ljava/lang/String;ZLjava/lang/String;ILjava/lang/String;[BZJ)V");
			if (callback != nullptr)
				env->CallVoidMethod(
				    h->owner, callback, static_cast<jint>(kind), identity,
				    static_cast<jint>(track->publisher_handle), sid, name,
				    track->uses_e2ee ? JNI_TRUE : JNI_FALSE,
				    track->has_frame_encoding ? JNI_TRUE : JNI_FALSE,
				    static_cast<jint>(track->frame_encoding), frame_encoding,
				    track->has_schema ? JNI_TRUE : JNI_FALSE, schema_name,
				    static_cast<jint>(track->schema_encoding), schema_encoding, data,
				    frame != nullptr && frame->has_user_timestamp ? JNI_TRUE : JNI_FALSE,
				    frame == nullptr ? 0 : bounded_long(frame->user_timestamp));
		}
		env->PopLocalFrame(nullptr);
	});
}

void participant_change_event(RoomHandle* h, int kind, const lk_participant_info_t* who,
                              const char* detail, const lk_attribute_t* changes = nullptr,
                              size_t change_count = 0) {
	deliver(h, [h, kind, who, detail, changes, change_count](JNIEnv* env) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jobject value = participant(env, h, who);
		jstring text = env->ExceptionCheck() ? nullptr : string(env, detail);
		jclass map_class = env->ExceptionCheck() ? nullptr : env->FindClass("java/util/HashMap");
		jmethodID map_ctor =
		    map_class == nullptr ? nullptr : env->GetMethodID(map_class, "<init>", "()V");
		jobject attributes = map_ctor == nullptr ? nullptr : env->NewObject(map_class, map_ctor);
		jmethodID put =
		    attributes == nullptr
		        ? nullptr
		        : env->GetMethodID(map_class, "put",
		                           "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
		if (put != nullptr && changes != nullptr) {
			for (size_t i = 0; i < change_count && !env->ExceptionCheck(); ++i) {
				jstring key = string(env, changes[i].key);
				jstring item = env->ExceptionCheck() ? nullptr : string(env, changes[i].value);
				if (!env->ExceptionCheck()) {
					jobject old = env->CallObjectMethod(attributes, put, key, item);
					if (old != nullptr)
						env->DeleteLocalRef(old);
				}
				env->DeleteLocalRef(key);
				if (item != nullptr)
					env->DeleteLocalRef(item);
			}
		}
		if (!env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(owner, "onNativeParticipantChange",
			                           "(ILio/livekit/client/Participant;Ljava/lang/String;"
			                           "Ljava/util/Map;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, static_cast<jint>(kind), value, text,
				                    attributes);
		}
		env->PopLocalFrame(nullptr);
	});
}

void permissions_event(RoomHandle* h, const lk_participant_info_t* who,
                       const lk_participant_permissions_t* before,
                       const lk_participant_permissions_t* after) {
	deliver(h, [h, who, before, after](JNIEnv* env) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jobject p = participant(env, h, who);
		jobject previous = env->ExceptionCheck() ? nullptr : permissions(env, h, before);
		jobject current = env->ExceptionCheck() ? nullptr : permissions(env, h, after);
		if (!env->ExceptionCheck() && (before == nullptr || previous != nullptr) &&
		    (after == nullptr || current != nullptr)) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(
			              owner, "onNativePermissionsChanged",
			              "(Lio/livekit/client/Participant;Lio/livekit/client/"
			              "ParticipantPermissions;Lio/livekit/client/ParticipantPermissions;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, p, previous, current);
		}
		env->PopLocalFrame(nullptr);
	});
}

void participant_list_event(RoomHandle* h, int kind, const lk_participant_info_t* values,
                            size_t count) {
	if (count > static_cast<size_t>(INT32_MAX) || (count != 0 && values == nullptr))
		return;
	deliver(h, [h, kind, values, count](JNIEnv* env) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jobjectArray items =
		    env->NewObjectArray(static_cast<jsize>(count), h->participant_class, nullptr);
		for (size_t i = 0; items != nullptr && i < count && !env->ExceptionCheck(); ++i) {
			jobject value = participant(env, h, &values[i]);
			if (!env->ExceptionCheck())
				env->SetObjectArrayElement(items, static_cast<jsize>(i), value);
			if (value != nullptr)
				env->DeleteLocalRef(value);
		}
		if (items != nullptr && !env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback = owner == nullptr
			                         ? nullptr
			                         : env->GetMethodID(owner, "onNativeParticipantList",
			                                            "(I[Lio/livekit/client/Participant;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, static_cast<jint>(kind), items);
		}
		env->PopLocalFrame(nullptr);
	});
}

jobject attribute_map(JNIEnv* env, const lk_attribute_t* values, size_t count) {
	jclass cls = env->FindClass("java/util/HashMap");
	jmethodID ctor = cls == nullptr ? nullptr : env->GetMethodID(cls, "<init>", "()V");
	jobject result = ctor == nullptr ? nullptr : env->NewObject(cls, ctor);
	jmethodID put =
	    result == nullptr
	        ? nullptr
	        : env->GetMethodID(cls, "put",
	                           "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
	for (size_t i = 0; put != nullptr && i < count && !env->ExceptionCheck(); ++i) {
		jstring key = string(env, values[i].key);
		jstring value = env->ExceptionCheck() ? nullptr : string(env, values[i].value);
		if (!env->ExceptionCheck()) {
			jobject old = env->CallObjectMethod(result, put, key, value);
			if (old != nullptr)
				env->DeleteLocalRef(old);
		}
		if (key != nullptr)
			env->DeleteLocalRef(key);
		if (value != nullptr)
			env->DeleteLocalRef(value);
	}
	if (cls != nullptr)
		env->DeleteLocalRef(cls);
	return result;
}

template <typename Snapshot, typename KeyGetter, typename ValueGetter>
jobject snapshot_attribute_map(JNIEnv* env, const Snapshot* snapshot, size_t count,
                               KeyGetter key_getter, ValueGetter value_getter) {
	std::vector<std::string> keys, values;
	keys.reserve(count);
	values.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		auto read = [env, snapshot, i](auto fn) {
			jbyteArray raw = getter(env, [snapshot, i, fn](char* out, size_t size) {
				return fn(snapshot, i, out, size);
			});
			std::string result = bytes(env, raw);
			env->DeleteLocalRef(raw);
			return result;
		};
		keys.push_back(read(key_getter));
		values.push_back(read(value_getter));
	}
	std::vector<lk_attribute_t> attributes;
	attributes.reserve(count);
	for (size_t i = 0; i < count; ++i)
		attributes.push_back({keys[i].c_str(), values[i].c_str()});
	return attribute_map(env, attributes.data(), attributes.size());
}

template <typename Snapshot, typename Getter>
std::string snapshot_string(JNIEnv* env, const Snapshot* snapshot, Getter fn) {
	jbyteArray raw =
	    getter(env, [snapshot, fn](char* out, size_t size) { return fn(snapshot, out, size); });
	std::string result = bytes(env, raw);
	env->DeleteLocalRef(raw);
	return result;
}

jobjectArray rtc_stats_array(JNIEnv* env, const lk_rtc_stats_snapshot_t* snapshot) {
	const size_t count = lk_rtc_stats_snapshot_count(snapshot);
	if (count > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
		              "Too many RTC stats streams");
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/RtcTrackStats");
	jclass string_class = env->FindClass("java/lang/String");
	if (cls == nullptr || string_class == nullptr)
		return nullptr;
	jmethodID ctor = env->GetMethodID(cls, "<init>", "([Ljava/lang/String;[J[D[Z)V");
	if (ctor == nullptr)
		return nullptr;
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	using StringGetter = size_t (*)(const lk_rtc_stats_snapshot_t*, size_t, char*, size_t);
	const std::array<StringGetter, 6> fields = {lk_rtc_stats_snapshot_id,
	                                            lk_rtc_stats_snapshot_kind,
	                                            lk_rtc_stats_snapshot_rid,
	                                            lk_rtc_stats_snapshot_codec_mime_type,
	                                            lk_rtc_stats_snapshot_codec_implementation,
	                                            lk_rtc_stats_snapshot_quality_limitation_reason};
	for (size_t i = 0; i < count && !env->ExceptionCheck(); ++i) {
		if (env->PushLocalFrame(24) != JNI_OK)
			break;
		lk_rtc_track_stats_t stats;
		lk_rtc_track_stats_init(&stats);
		auto status = lk_rtc_stats_snapshot_info(snapshot, i, &stats);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			env->PopLocalFrame(nullptr);
			break;
		}
		jobjectArray text = env->NewObjectArray(6, string_class, nullptr);
		for (size_t j = 0; j < fields.size() && !env->ExceptionCheck(); ++j) {
			jbyteArray raw = getter(env, [snapshot, i, field = fields[j]](char* p, size_t n) {
				return field(snapshot, i, p, n);
			});
			if (raw == nullptr)
				break;
			std::string value = bytes(env, raw);
			jstring decoded = string(env, value.c_str());
			if (decoded != nullptr) {
				env->SetObjectArrayElement(text, static_cast<jsize>(j), decoded);
				env->DeleteLocalRef(decoded);
			}
			env->DeleteLocalRef(raw);
		}
		const jlong counters[] = {static_cast<jlong>(stats.direction),
		                          static_cast<jlong>(stats.bytes),
		                          static_cast<jlong>(stats.packets),
		                          static_cast<jlong>(stats.packets_lost),
		                          static_cast<jlong>(stats.concealed_samples),
		                          static_cast<jlong>(stats.frame_width),
		                          static_cast<jlong>(stats.frame_height),
		                          static_cast<jlong>(stats.frames),
		                          static_cast<jlong>(stats.frames_dropped),
		                          static_cast<jlong>(stats.fir_count),
		                          static_cast<jlong>(stats.pli_count),
		                          static_cast<jlong>(stats.nack_count),
		                          static_cast<jlong>(stats.qp_sum)};
		const jdouble measurements[] = {
		    stats.timestamp_ms,   stats.bitrate_bps, stats.round_trip_time_seconds,
		    stats.jitter_seconds, stats.audio_level, stats.frames_per_second};
		const jboolean available[] = {static_cast<jboolean>(stats.has_bitrate_bps != 0),
		                              static_cast<jboolean>(stats.has_round_trip_time_seconds != 0),
		                              static_cast<jboolean>(stats.has_jitter_seconds != 0),
		                              static_cast<jboolean>(stats.has_audio_level != 0)};
		jlongArray counts = env->NewLongArray(13);
		jdoubleArray doubles = env->NewDoubleArray(6);
		jbooleanArray flags = env->NewBooleanArray(4);
		if (!env->ExceptionCheck()) {
			env->SetLongArrayRegion(counts, 0, 13, counters);
			env->SetDoubleArrayRegion(doubles, 0, 6, measurements);
			env->SetBooleanArrayRegion(flags, 0, 4, available);
		}
		if (!env->ExceptionCheck()) {
			jobject item = env->NewObject(cls, ctor, text, counts, doubles, flags);
			if (item != nullptr)
				env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		}
		env->PopLocalFrame(nullptr);
	}
	env->DeleteLocalRef(string_class);
	env->DeleteLocalRef(cls);
	return env->ExceptionCheck() ? nullptr : result;
}

jobject publication_snapshot(JNIEnv* env, const std::string& participant_identity,
                             const lk_remote_track_publication_snapshot_t* publication) {
	lk_remote_track_publication_snapshot_info_t info;
	lk_remote_track_publication_snapshot_info_init(&info);
	lk_status_t status = lk_remote_track_publication_snapshot_info(publication, &info);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	auto read = [env, publication](auto fn) {
		jbyteArray raw = getter(
		    env, [publication, fn](char* out, size_t size) { return fn(publication, out, size); });
		std::string value = bytes(env, raw);
		env->DeleteLocalRef(raw);
		return string(env, value.c_str());
	};
	jstring identity = string(env, participant_identity.c_str());
	jstring sid = read(lk_remote_track_publication_snapshot_sid);
	jstring name = read(lk_remote_track_publication_snapshot_name);
	jstring mime = read(lk_remote_track_publication_snapshot_mime_type);
	jclass cls = env->FindClass("io/livekit/client/RemoteTrackPublication");
	jmethodID ctor = env->GetMethodID(cls, "<init>",
	                                  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
	                                  "Ljava/lang/String;IIZII)V");
	jobject result =
	    env->NewObject(cls, ctor, identity, sid, name, mime, static_cast<jint>(info.kind),
	                   static_cast<jint>(info.source), info.is_muted ? JNI_TRUE : JNI_FALSE,
	                   static_cast<jint>(info.width), static_cast<jint>(info.height));
	for (jobject value :
	     {static_cast<jobject>(identity), static_cast<jobject>(sid), static_cast<jobject>(name),
	      static_cast<jobject>(mime), static_cast<jobject>(cls)})
		env->DeleteLocalRef(value);
	return result;
}

jobject publication_details(JNIEnv* env, const std::string& participant_identity,
                            const lk_remote_track_publication_snapshot_t* publication) {
	lk_remote_track_publication_snapshot_info_t info;
	lk_remote_track_publication_snapshot_info_init(&info);
	lk_status_t status = lk_remote_track_publication_snapshot_info(publication, &info);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jobject basic = publication_snapshot(env, participant_identity, publication);
	if (basic == nullptr)
		return nullptr;
	jobject track_state = nullptr;
	if (info.has_subscribed_track) {
		const lk_remote_track_snapshot_t* track = nullptr;
		status = lk_remote_track_publication_snapshot_track(publication, &track);
		lk_remote_track_snapshot_info_t track_info;
		lk_remote_track_snapshot_info_init(&track_info);
		if (status == LK_STATUS_OK && track != nullptr)
			status = lk_remote_track_snapshot_info(track, &track_info);
		if (status != LK_STATUS_OK || track == nullptr) {
			env->DeleteLocalRef(basic);
			fail(env, status == LK_STATUS_OK ? LK_STATUS_INVALID_STATE : status);
			return nullptr;
		}
		std::string sid = snapshot_string(env, track, lk_remote_track_snapshot_sid);
		std::string name = snapshot_string(env, track, lk_remote_track_snapshot_name);
		jstring java_sid = string(env, sid.c_str());
		jstring java_name = string(env, name.c_str());
		jclass cls = env->FindClass("io/livekit/client/RemoteTrackState");
		jmethodID ctor =
		    env->GetMethodID(cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;IIIIIZ)V");
		track_state = env->NewObject(
		    cls, ctor, java_sid, java_name, static_cast<jint>(track_info.kind),
		    static_cast<jint>(track_info.source), static_cast<jint>(track_info.stream_state),
		    static_cast<jint>(track_info.width), static_cast<jint>(track_info.height),
		    track_info.enabled ? JNI_TRUE : JNI_FALSE);
		env->DeleteLocalRef(java_sid);
		env->DeleteLocalRef(java_name);
		env->DeleteLocalRef(cls);
	}
	jclass cls = env->FindClass("io/livekit/client/RemoteTrackPublicationDetails");
	jmethodID ctor = env->GetMethodID(cls, "<init>",
	                                  "(Lio/livekit/client/RemoteTrackPublication;ZZIZII"
	                                  "Lio/livekit/client/RemoteTrackState;)V");
	jobject result = env->NewObject(cls, ctor, basic, info.is_simulcasted ? JNI_TRUE : JNI_FALSE,
	                                info.subscription_allowed ? JNI_TRUE : JNI_FALSE,
	                                static_cast<jint>(info.subscription_status),
	                                info.has_subscription_error ? JNI_TRUE : JNI_FALSE,
	                                static_cast<jint>(info.subscription_error),
	                                static_cast<jint>(info.encryption), track_state);
	env->DeleteLocalRef(basic);
	if (track_state != nullptr)
		env->DeleteLocalRef(track_state);
	env->DeleteLocalRef(cls);
	return result;
}

jobject participant_details(JNIEnv* env, RoomHandle* h, const lk_participant_info_t& info,
                            const lk_participant_permissions_t& access, jobject attributes,
                            jobjectArray publications) {
	jobject who = participant(env, h, &info);
	jobject allowed = permissions(env, h, &access);
	jclass arrays = env->FindClass("java/util/Arrays");
	jmethodID as_list =
	    env->GetStaticMethodID(arrays, "asList", "([Ljava/lang/Object;)Ljava/util/List;");
	jobject publication_list = env->CallStaticObjectMethod(arrays, as_list, publications);
	jclass cls = env->FindClass("io/livekit/client/ParticipantDetails");
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>",
	    "(Lio/livekit/client/Participant;Lio/livekit/client/ParticipantPermissions;"
	    "Ljava/util/Map;Ljava/util/List;)V");
	jobject result = env->NewObject(cls, ctor, who, allowed, attributes, publication_list);
	for (jobject value :
	     {who, allowed, publication_list, static_cast<jobject>(arrays), static_cast<jobject>(cls)})
		env->DeleteLocalRef(value);
	return result;
}

void text_event(RoomHandle* h, const lk_text_received_t* value) {
	if (value == nullptr || value->attached_stream_id_count > static_cast<size_t>(INT32_MAX) ||
	    value->attribute_count > static_cast<size_t>(INT32_MAX) ||
	    (value->attached_stream_id_count != 0 && value->attached_stream_ids == nullptr) ||
	    (value->attribute_count != 0 && value->attributes == nullptr))
		return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jclass strings = env->FindClass("java/lang/String");
		jobjectArray attachments =
		    strings == nullptr
		        ? nullptr
		        : env->NewObjectArray(static_cast<jsize>(value->attached_stream_id_count), strings,
		                              nullptr);
		for (size_t i = 0; attachments != nullptr && i < value->attached_stream_id_count &&
		                   !env->ExceptionCheck();
		     ++i) {
			jstring id = string(env, value->attached_stream_ids[i]);
			if (!env->ExceptionCheck())
				env->SetObjectArrayElement(attachments, static_cast<jsize>(i), id);
			if (id != nullptr)
				env->DeleteLocalRef(id);
		}
		jclass arrays = env->ExceptionCheck() ? nullptr : env->FindClass("java/util/Arrays");
		jmethodID as_list =
		    arrays == nullptr
		        ? nullptr
		        : env->GetStaticMethodID(arrays, "asList", "([Ljava/lang/Object;)Ljava/util/List;");
		jobject list = as_list == nullptr
		                   ? nullptr
		                   : env->CallStaticObjectMethod(arrays, as_list, attachments);
		jobject attributes = env->ExceptionCheck()
		                         ? nullptr
		                         : attribute_map(env, value->attributes, value->attribute_count);
		jmethodID ctor =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->GetMethodID(h->text_class, "<init>",
		                           "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
		                           "Ljava/lang/String;Ljava/lang/String;JLjava/util/List;"
		                           "Ljava/util/Map;)V");
		jstring id = env->ExceptionCheck() ? nullptr : string(env, value->stream_id);
		jstring body = env->ExceptionCheck() ? nullptr : string(env, value->text);
		jstring topic = env->ExceptionCheck() ? nullptr : string(env, value->topic);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		jstring reply = env->ExceptionCheck() ? nullptr : string(env, value->reply_to_stream_id);
		jobject item = env->ExceptionCheck()
		                   ? nullptr
		                   : env->NewObject(h->text_class, ctor, id, body, topic, identity, reply,
		                                    static_cast<jlong>(value->timestamp), list, attributes);
		if (item != nullptr && !env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(owner, "onNativeText", "(Lio/livekit/client/TextMessage;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, item);
		}
		env->PopLocalFrame(nullptr);
	});
}

void byte_event(RoomHandle* h, const lk_file_received_t* value, bool file) {
	if (value == nullptr || value->data_size > static_cast<size_t>(INT32_MAX) ||
	    value->attribute_count > static_cast<size_t>(INT32_MAX) ||
	    (value->data_size != 0 && value->data == nullptr) ||
	    (value->attribute_count != 0 && value->attributes == nullptr))
		return;
	deliver(h, [h, value, file](JNIEnv* env) {
		if (env->PushLocalFrame(24) != JNI_OK)
			return;
		jobject attributes = attribute_map(env, value->attributes, value->attribute_count);
		jmethodID ctor =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->GetMethodID(h->byte_class, "<init>",
		                           "(Ljava/lang/String;[BLjava/lang/String;Ljava/lang/String;"
		                           "Ljava/lang/String;Ljava/lang/String;JLjava/util/Map;)V");
		jstring id = env->ExceptionCheck() ? nullptr : string(env, value->stream_id);
		jbyteArray body =
		    env->ExceptionCheck() ? nullptr : array(env, value->data, value->data_size);
		jstring name = env->ExceptionCheck() ? nullptr : string(env, value->name);
		jstring mime = env->ExceptionCheck() ? nullptr : string(env, value->mime_type);
		jstring topic = env->ExceptionCheck() ? nullptr : string(env, value->topic);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		jobject item =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->NewObject(h->byte_class, ctor, id, body, name, mime, topic, identity,
		                         static_cast<jlong>(value->timestamp), attributes);
		if (item != nullptr && !env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback = owner == nullptr
			                         ? nullptr
			                         : env->GetMethodID(owner, "onNativeBytes",
			                                            "(Lio/livekit/client/ByteMessage;Z)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, item, file ? JNI_TRUE : JNI_FALSE);
		}
		env->PopLocalFrame(nullptr);
	});
}

void chat_event(RoomHandle* h, const lk_chat_message_t* value) {
	if (value == nullptr)
		return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(16) != JNI_OK)
			return;
		jclass cls = h->chat_class;
		jmethodID ctor = env->GetMethodID(cls, "<init>",
		                                  "(Ljava/lang/String;JLjava/lang/Long;Ljava/lang/String;"
		                                  "Ljava/lang/String;ZZ)V");
		jstring id = env->ExceptionCheck() ? nullptr : string(env, value->id);
		jobject edit = nullptr;
		if (!env->ExceptionCheck() && value->has_edit_timestamp) {
			jclass number = env->FindClass("java/lang/Long");
			jmethodID long_ctor =
			    number == nullptr ? nullptr : env->GetMethodID(number, "<init>", "(J)V");
			if (long_ctor != nullptr)
				edit = env->NewObject(number, long_ctor, static_cast<jlong>(value->edit_timestamp));
		}
		jstring body = env->ExceptionCheck() ? nullptr : string(env, value->message);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		jobject item =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->NewObject(cls, ctor, id, static_cast<jlong>(value->timestamp), edit, body,
		                         identity, value->deleted ? JNI_TRUE : JNI_FALSE,
		                         value->generated ? JNI_TRUE : JNI_FALSE);
		if (item != nullptr && !env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(owner, "onNativeChat", "(Lio/livekit/client/ChatMessage;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, item);
		}
		env->PopLocalFrame(nullptr);
	});
}

void transcription_event(RoomHandle* h, const lk_transcription_received_t* value) {
	if (value == nullptr || value->segment_count > static_cast<size_t>(INT32_MAX) ||
	    (value->segment_count != 0 && value->segments == nullptr))
		return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(24) != JNI_OK)
			return;
		jclass segment_class = env->FindClass("io/livekit/client/TranscriptionSegment");
		jmethodID constructor =
		    segment_class == nullptr
		        ? nullptr
		        : env->GetMethodID(
		              segment_class, "<init>",
		              "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JJZJJ)V");
		jobjectArray segments = constructor == nullptr
		                            ? nullptr
		                            : env->NewObjectArray(static_cast<jsize>(value->segment_count),
		                                                  segment_class, nullptr);
		for (size_t i = 0;
		     segments != nullptr && i < value->segment_count && !env->ExceptionCheck(); ++i) {
			const auto& source = value->segments[i];
			jstring id = string(env, source.id);
			jstring body = env->ExceptionCheck() ? nullptr : string(env, source.text);
			jstring language = env->ExceptionCheck() ? nullptr : string(env, source.language);
			if (!env->ExceptionCheck()) {
				jobject item = env->NewObject(
				    segment_class, constructor, id, body, language, bounded_long(source.start_time),
				    bounded_long(source.end_time), source.is_final ? JNI_TRUE : JNI_FALSE,
				    static_cast<jlong>(source.first_received_time),
				    static_cast<jlong>(source.last_received_time));
				if (item != nullptr && !env->ExceptionCheck())
					env->SetObjectArrayElement(segments, static_cast<jsize>(i), item);
				if (item != nullptr)
					env->DeleteLocalRef(item);
			}
			if (id != nullptr)
				env->DeleteLocalRef(id);
			if (body != nullptr)
				env->DeleteLocalRef(body);
			if (language != nullptr)
				env->DeleteLocalRef(language);
		}
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->transcribed_participant_identity);
		jstring track = env->ExceptionCheck() ? nullptr : string(env, value->track_id);
		if (!env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr ? nullptr
			                     : env->GetMethodID(owner, "onNativeTranscription",
			                                        "(Ljava/lang/String;Ljava/lang/String;"
			                                        "[Lio/livekit/client/TranscriptionSegment;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, identity, track, segments);
		}
		env->PopLocalFrame(nullptr);
	});
}

jobjectArray quality_array(JNIEnv* env, jclass cls, jmethodID constructor,
                           const lk_subscribed_quality_t* values, size_t count) {
	if (count > static_cast<size_t>(INT32_MAX) || (count != 0 && values == nullptr))
		return nullptr;
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	for (size_t i = 0; result != nullptr && i < count && !env->ExceptionCheck(); ++i) {
		jobject item = env->NewObject(cls, constructor, static_cast<jint>(values[i].quality),
		                              values[i].enabled ? JNI_TRUE : JNI_FALSE);
		if (item != nullptr && !env->ExceptionCheck())
			env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		if (item != nullptr)
			env->DeleteLocalRef(item);
	}
	return result;
}

void quality_event(RoomHandle* h, const lk_track_publication_info_t* track,
                   const lk_participant_info_t* who, const lk_subscribed_quality_update_t* update) {
	if (track == nullptr || update == nullptr ||
	    update->quality_count > static_cast<size_t>(INT32_MAX) ||
	    (update->quality_count != 0 && update->qualities == nullptr) ||
	    update->codec_count > static_cast<size_t>(INT32_MAX) ||
	    (update->codec_count != 0 && update->codecs == nullptr))
		return;
	for (size_t i = 0; i < update->codec_count; ++i)
		if (update->codecs[i].quality_count > static_cast<size_t>(INT32_MAX) ||
		    (update->codecs[i].quality_count != 0 && update->codecs[i].qualities == nullptr))
			return;
	deliver(h, [h, track, who, update](JNIEnv* env) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jclass quality_class = env->FindClass("io/livekit/client/SubscribedQuality");
		jclass codec_class =
		    env->ExceptionCheck() ? nullptr : env->FindClass("io/livekit/client/SubscribedCodec");
		jmethodID quality_ctor =
		    quality_class == nullptr ? nullptr : env->GetMethodID(quality_class, "<init>", "(IZ)V");
		jmethodID codec_ctor =
		    codec_class == nullptr
		        ? nullptr
		        : env->GetMethodID(codec_class, "<init>", "(Ljava/lang/String;Ljava/util/List;)V");
		jobjectArray qualities = quality_ctor == nullptr
		                             ? nullptr
		                             : quality_array(env, quality_class, quality_ctor,
		                                             update->qualities, update->quality_count);
		jobjectArray codecs = codec_ctor == nullptr || qualities == nullptr
		                          ? nullptr
		                          : env->NewObjectArray(static_cast<jsize>(update->codec_count),
		                                                codec_class, nullptr);
		jclass arrays = env->ExceptionCheck() ? nullptr : env->FindClass("java/util/Arrays");
		jmethodID as_list =
		    arrays == nullptr
		        ? nullptr
		        : env->GetStaticMethodID(arrays, "asList", "([Ljava/lang/Object;)Ljava/util/List;");
		for (size_t i = 0; codecs != nullptr && as_list != nullptr && i < update->codec_count &&
		                   !env->ExceptionCheck();
		     ++i) {
			const auto& source = update->codecs[i];
			jobjectArray entries = quality_array(env, quality_class, quality_ctor, source.qualities,
			                                     source.quality_count);
			jobject list = entries == nullptr
			                   ? nullptr
			                   : env->CallStaticObjectMethod(arrays, as_list, entries);
			jstring name = env->ExceptionCheck() ? nullptr : string(env, source.codec);
			jobject item = env->ExceptionCheck()
			                   ? nullptr
			                   : env->NewObject(codec_class, codec_ctor, name, list);
			if (item != nullptr && !env->ExceptionCheck())
				env->SetObjectArrayElement(codecs, static_cast<jsize>(i), item);
			if (item != nullptr)
				env->DeleteLocalRef(item);
			if (name != nullptr)
				env->DeleteLocalRef(name);
			if (list != nullptr)
				env->DeleteLocalRef(list);
			if (entries != nullptr)
				env->DeleteLocalRef(entries);
		}
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, who == nullptr ? nullptr : who->identity);
		jstring sid = env->ExceptionCheck() ? nullptr : string(env, track->sid);
		jstring name = env->ExceptionCheck() ? nullptr : string(env, track->name);
		jstring mime = env->ExceptionCheck() ? nullptr : string(env, track->mime_type);
		jstring changed_sid = env->ExceptionCheck() ? nullptr : string(env, update->track_sid);
		jmethodID track_ctor = env->ExceptionCheck()
		                           ? nullptr
		                           : env->GetMethodID(h->remote_track_class, "<init>",
		                                              "(Ljava/lang/String;Ljava/lang/String;Ljava/"
		                                              "lang/String;Ljava/lang/String;IIZII)V");
		jobject publication =
		    track_ctor == nullptr
		        ? nullptr
		        : env->NewObject(h->remote_track_class, track_ctor, identity, sid, name, mime,
		                         static_cast<jint>(track->kind), static_cast<jint>(track->source),
		                         track->is_muted ? JNI_TRUE : JNI_FALSE,
		                         static_cast<jint>(track->width), static_cast<jint>(track->height));
		jobject participant_value = env->ExceptionCheck() ? nullptr : participant(env, h, who);
		if (!env->ExceptionCheck() && publication != nullptr && codecs != nullptr &&
		    as_list != nullptr) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(owner, "onNativeSubscribedQuality",
			                           "(Lio/livekit/client/RemoteTrackPublication;"
			                           "Lio/livekit/client/Participant;Ljava/lang/"
			                           "String;[Lio/livekit/client/SubscribedQuality;[Lio/livekit/"
			                           "client/SubscribedCodec;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, publication, participant_value, changed_sid,
				                    qualities, codecs);
		}
		env->PopLocalFrame(nullptr);
	});
}

jobject metric_timestamp(JNIEnv* env, jclass cls, jmethodID constructor,
                         const lk_metric_timestamp_t& value) {
	return env->NewObject(cls, constructor, static_cast<jlong>(value.seconds),
	                      static_cast<jint>(value.nanos));
}

void metrics_event(RoomHandle* h, const lk_metrics_received_t* value) {
	if (value == nullptr || value->string_data_count > static_cast<size_t>(INT32_MAX) ||
	    value->time_series_count > static_cast<size_t>(INT32_MAX) ||
	    value->event_count > static_cast<size_t>(INT32_MAX) ||
	    (value->string_data_count != 0 && value->string_data == nullptr) ||
	    (value->time_series_count != 0 && value->time_series == nullptr) ||
	    (value->event_count != 0 && value->events == nullptr))
		return;
	for (size_t i = 0; i < value->time_series_count; ++i)
		if (value->time_series[i].sample_count > static_cast<size_t>(INT32_MAX) ||
		    (value->time_series[i].sample_count != 0 && value->time_series[i].samples == nullptr))
			return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(64) != JNI_OK)
			return;
		jclass timestamp_class = env->FindClass("io/livekit/client/MetricTimestamp");
		jclass sample_class =
		    env->ExceptionCheck() ? nullptr : env->FindClass("io/livekit/client/MetricSample");
		jclass series_class =
		    env->ExceptionCheck() ? nullptr : env->FindClass("io/livekit/client/TimeSeriesMetric");
		jclass event_class =
		    env->ExceptionCheck() ? nullptr : env->FindClass("io/livekit/client/EventMetric");
		jclass message_class =
		    env->ExceptionCheck() ? nullptr : env->FindClass("io/livekit/client/MetricsMessage");
		jclass string_class = env->ExceptionCheck() ? nullptr : env->FindClass("java/lang/String");
		jclass long_class = env->ExceptionCheck() ? nullptr : env->FindClass("java/lang/Long");
		jclass arrays_class = env->ExceptionCheck() ? nullptr : env->FindClass("java/util/Arrays");
		if (env->ExceptionCheck()) {
			env->PopLocalFrame(nullptr);
			return;
		}
		jmethodID timestamp_ctor = env->GetMethodID(timestamp_class, "<init>", "(JI)V");
		jmethodID sample_ctor =
		    env->GetMethodID(sample_class, "<init>", "(JLio/livekit/client/MetricTimestamp;F)V");
		jmethodID series_ctor = env->GetMethodID(series_class, "<init>", "(JJJJLjava/util/List;)V");
		jmethodID event_ctor =
		    env->GetMethodID(event_class, "<init>",
		                     "(JJJJJLjava/lang/Long;Lio/livekit/client/MetricTimestamp;Lio/livekit/"
		                     "client/MetricTimestamp;Ljava/lang/String;)V");
		jmethodID message_ctor =
		    env->GetMethodID(message_class, "<init>",
		                     "(JLio/livekit/client/MetricTimestamp;Ljava/lang/String;Ljava/util/"
		                     "List;Ljava/util/List;Ljava/util/List;)V");
		jmethodID long_ctor = env->GetMethodID(long_class, "<init>", "(J)V");
		jmethodID as_list =
		    env->GetStaticMethodID(arrays_class, "asList", "([Ljava/lang/Object;)Ljava/util/List;");
		if (env->ExceptionCheck()) {
			env->PopLocalFrame(nullptr);
			return;
		}
		jobjectArray strings = env->NewObjectArray(static_cast<jsize>(value->string_data_count),
		                                           string_class, nullptr);
		for (size_t i = 0;
		     strings != nullptr && i < value->string_data_count && !env->ExceptionCheck(); ++i) {
			jstring item = string(env, value->string_data[i]);
			if (!env->ExceptionCheck())
				env->SetObjectArrayElement(strings, static_cast<jsize>(i), item);
			if (item != nullptr)
				env->DeleteLocalRef(item);
		}
		jobjectArray series =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->NewObjectArray(static_cast<jsize>(value->time_series_count), series_class,
		                              nullptr);
		for (size_t i = 0;
		     series != nullptr && i < value->time_series_count && !env->ExceptionCheck(); ++i) {
			const auto& source = value->time_series[i];
			jobjectArray samples =
			    env->NewObjectArray(static_cast<jsize>(source.sample_count), sample_class, nullptr);
			for (size_t j = 0;
			     samples != nullptr && j < source.sample_count && !env->ExceptionCheck(); ++j) {
				const auto& sample = source.samples[j];
				jobject normalized = sample.has_normalized_timestamp
				                         ? metric_timestamp(env, timestamp_class, timestamp_ctor,
				                                            sample.normalized_timestamp)
				                         : nullptr;
				jobject item = env->ExceptionCheck()
				                   ? nullptr
				                   : env->NewObject(sample_class, sample_ctor,
				                                    static_cast<jlong>(sample.timestamp_ms),
				                                    normalized, static_cast<jfloat>(sample.value));
				if (item != nullptr && !env->ExceptionCheck())
					env->SetObjectArrayElement(samples, static_cast<jsize>(j), item);
				if (item != nullptr)
					env->DeleteLocalRef(item);
				if (normalized != nullptr)
					env->DeleteLocalRef(normalized);
			}
			jobject list = env->ExceptionCheck()
			                   ? nullptr
			                   : env->CallStaticObjectMethod(arrays_class, as_list, samples);
			jobject item =
			    env->ExceptionCheck()
			        ? nullptr
			        : env->NewObject(series_class, series_ctor, static_cast<jlong>(source.label),
			                         static_cast<jlong>(source.participant_identity),
			                         static_cast<jlong>(source.track_sid),
			                         static_cast<jlong>(source.rid), list);
			if (item != nullptr && !env->ExceptionCheck())
				env->SetObjectArrayElement(series, static_cast<jsize>(i), item);
			if (item != nullptr)
				env->DeleteLocalRef(item);
			if (list != nullptr)
				env->DeleteLocalRef(list);
			if (samples != nullptr)
				env->DeleteLocalRef(samples);
		}
		jobjectArray events =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->NewObjectArray(static_cast<jsize>(value->event_count), event_class, nullptr);
		for (size_t i = 0; events != nullptr && i < value->event_count && !env->ExceptionCheck();
		     ++i) {
			const auto& source = value->events[i];
			jobject end = source.has_end_timestamp_ms
			                  ? env->NewObject(long_class, long_ctor,
			                                   static_cast<jlong>(source.end_timestamp_ms))
			                  : nullptr;
			jobject start_normalized =
			    env->ExceptionCheck() || !source.has_normalized_start_timestamp
			        ? nullptr
			        : metric_timestamp(env, timestamp_class, timestamp_ctor,
			                           source.normalized_start_timestamp);
			jobject end_normalized = env->ExceptionCheck() || !source.has_normalized_end_timestamp
			                             ? nullptr
			                             : metric_timestamp(env, timestamp_class, timestamp_ctor,
			                                                source.normalized_end_timestamp);
			jstring metadata = env->ExceptionCheck() ? nullptr : string(env, source.metadata);
			jobject item =
			    env->ExceptionCheck()
			        ? nullptr
			        : env->NewObject(event_class, event_ctor, static_cast<jlong>(source.label),
			                         static_cast<jlong>(source.participant_identity),
			                         static_cast<jlong>(source.track_sid),
			                         static_cast<jlong>(source.rid),
			                         static_cast<jlong>(source.start_timestamp_ms), end,
			                         start_normalized, end_normalized, metadata);
			if (item != nullptr && !env->ExceptionCheck())
				env->SetObjectArrayElement(events, static_cast<jsize>(i), item);
			if (item != nullptr)
				env->DeleteLocalRef(item);
			if (end != nullptr)
				env->DeleteLocalRef(end);
			if (start_normalized != nullptr)
				env->DeleteLocalRef(start_normalized);
			if (end_normalized != nullptr)
				env->DeleteLocalRef(end_normalized);
			if (metadata != nullptr)
				env->DeleteLocalRef(metadata);
		}
		jobject normalized = env->ExceptionCheck() || !value->has_normalized_timestamp
		                         ? nullptr
		                         : metric_timestamp(env, timestamp_class, timestamp_ctor,
		                                            value->normalized_timestamp);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		jobject string_list = env->ExceptionCheck()
		                          ? nullptr
		                          : env->CallStaticObjectMethod(arrays_class, as_list, strings);
		jobject series_list = env->ExceptionCheck()
		                          ? nullptr
		                          : env->CallStaticObjectMethod(arrays_class, as_list, series);
		jobject event_list = env->ExceptionCheck()
		                         ? nullptr
		                         : env->CallStaticObjectMethod(arrays_class, as_list, events);
		jobject message = env->ExceptionCheck()
		                      ? nullptr
		                      : env->NewObject(message_class, message_ctor,
		                                       static_cast<jlong>(value->timestamp_ms), normalized,
		                                       identity, string_list, series_list, event_list);
		if (message != nullptr && !env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback = owner == nullptr
			                         ? nullptr
			                         : env->GetMethodID(owner, "onNativeMetrics",
			                                            "(Lio/livekit/client/MetricsMessage;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, message);
		}
		env->PopLocalFrame(nullptr);
	});
}

jobject publication(JNIEnv* env, RoomHandle* h, const lk_track_publication_info_t* track,
                    const lk_participant_info_t* who) {
	if (env->PushLocalFrame(8) != JNI_OK)
		return nullptr;
	jmethodID ctor = env->GetMethodID(
	    h->remote_track_class, "<init>",
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZII)V");
	jstring identity =
	    ctor == nullptr ? nullptr : string(env, who == nullptr ? nullptr : who->identity);
	jstring sid = env->ExceptionCheck() ? nullptr : string(env, track->sid);
	jstring name = env->ExceptionCheck() ? nullptr : string(env, track->name);
	jstring mime = env->ExceptionCheck() ? nullptr : string(env, track->mime_type);
	jobject item =
	    env->ExceptionCheck()
	        ? nullptr
	        : env->NewObject(h->remote_track_class, ctor, identity, sid, name, mime,
	                         static_cast<jint>(track->kind), static_cast<jint>(track->source),
	                         track->is_muted ? JNI_TRUE : JNI_FALSE,
	                         static_cast<jint>(track->width), static_cast<jint>(track->height));
	return env->PopLocalFrame(item);
}

void track_event(RoomHandle* h, int kind, const lk_track_publication_info_t* track,
                 const lk_participant_info_t* who, int value = 0) {
	if (track == nullptr)
		return;
	deliver(h, [h, kind, track, who, value](JNIEnv* env) {
		jobject item = publication(env, h, track, who);
		if (item == nullptr || env->ExceptionCheck())
			return;
		jclass owner = env->GetObjectClass(h->owner);
		jmethodID callback = env->GetMethodID(owner, "onNativeTrack",
		                                      "(ILio/livekit/client/RemoteTrackPublication;I)V");
		env->CallVoidMethod(h->owner, callback, static_cast<jint>(kind), item,
		                    static_cast<jint>(value));
		env->DeleteLocalRef(owner);
		env->DeleteLocalRef(item);
	});
}

void audio_frame_event(RoomHandle* h, const lk_track_publication_info_t* track,
                       const lk_participant_info_t* who, const lk_audio_frame_t* frame) {
	if (!h->frame_callbacks_enabled.load(std::memory_order_relaxed) || track == nullptr ||
	    frame == nullptr || frame->sample_count > static_cast<size_t>(INT32_MAX) ||
	    (frame->sample_count != 0 && frame->data == nullptr))
		return;
	deliver(h, [h, track, who, frame](JNIEnv* env) {
		if (env->PushLocalFrame(24) != JNI_OK)
			return;
		jobject item = publication(env, h, track, who);
		jobject p = env->ExceptionCheck() ? nullptr : participant(env, h, who);
		jshortArray samples = env->ExceptionCheck()
		                          ? nullptr
		                          : env->NewShortArray(static_cast<jsize>(frame->sample_count));
		if (samples != nullptr && frame->sample_count != 0)
			env->SetShortArrayRegion(samples, 0, static_cast<jsize>(frame->sample_count),
			                         frame->data);
		if (!env->ExceptionCheck() && item != nullptr && samples != nullptr) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr ? nullptr
			                     : env->GetMethodID(owner, "onNativeAudioFrame",
			                                        "(Lio/livekit/client/RemoteTrackPublication;"
			                                        "Lio/livekit/client/Participant;[SIII)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, item, p, samples,
				                    static_cast<jint>(frame->sample_rate),
				                    static_cast<jint>(frame->num_channels),
				                    static_cast<jint>(frame->samples_per_channel));
		}
		env->PopLocalFrame(nullptr);
	});
}

void video_frame_event(RoomHandle* h, const lk_track_publication_info_t* track,
                       const lk_participant_info_t* who, const lk_video_frame_t* frame,
                       const lk_video_frame_metadata_t* metadata) {
	if (!h->frame_callbacks_enabled.load(std::memory_order_relaxed) || track == nullptr ||
	    frame == nullptr || frame->data_size > static_cast<size_t>(INT32_MAX) ||
	    (frame->data_size != 0 && frame->data == nullptr) ||
	    (metadata != nullptr && (metadata->user_data_size > static_cast<size_t>(INT32_MAX) ||
	                             (metadata->has_user_data && metadata->user_data_size != 0 &&
	                              metadata->user_data == nullptr))))
		return;
	deliver(h, [h, track, who, frame, metadata](JNIEnv* env) {
		if (env->PushLocalFrame(24) != JNI_OK)
			return;
		jobject item = publication(env, h, track, who);
		jobject p = env->ExceptionCheck() ? nullptr : participant(env, h, who);
		jbyteArray data =
		    env->ExceptionCheck() ? nullptr : array(env, frame->data, frame->data_size);
		jbyteArray user_data =
		    env->ExceptionCheck() || metadata == nullptr || !metadata->has_user_data
		        ? nullptr
		        : array(env, metadata->user_data, metadata->user_data_size);
		jobject user_timestamp = nullptr, frame_id = nullptr;
		if (!env->ExceptionCheck() && metadata != nullptr && metadata->has_user_timestamp_us) {
			jclass cls = env->FindClass("java/lang/Long");
			jmethodID ctor = cls == nullptr ? nullptr : env->GetMethodID(cls, "<init>", "(J)V");
			if (ctor != nullptr)
				user_timestamp =
				    env->NewObject(cls, ctor, bounded_long(metadata->user_timestamp_us));
		}
		if (!env->ExceptionCheck() && metadata != nullptr && metadata->has_frame_id) {
			jclass cls = env->FindClass("java/lang/Integer");
			jmethodID ctor = cls == nullptr ? nullptr : env->GetMethodID(cls, "<init>", "(I)V");
			if (ctor != nullptr)
				frame_id = env->NewObject(cls, ctor, static_cast<jint>(metadata->frame_id));
		}
		if (!env->ExceptionCheck() && item != nullptr && data != nullptr) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback =
			    owner == nullptr
			        ? nullptr
			        : env->GetMethodID(owner, "onNativeVideoFrame",
			                           "(Lio/livekit/client/RemoteTrackPublication;"
			                           "Lio/livekit/client/Participant;[BIIJLjava/lang/Long;"
			                           "Ljava/lang/Integer;[B)V");
			if (callback != nullptr)
				env->CallVoidMethod(
				    h->owner, callback, item, p, data, static_cast<jint>(frame->width),
				    static_cast<jint>(frame->height), static_cast<jlong>(frame->timestamp_us),
				    user_timestamp, frame_id, user_data);
		}
		env->PopLocalFrame(nullptr);
	});
}

jobject stream_total_size(JNIEnv* env, int has_total_size, uint64_t total_size) {
	if (!has_total_size)
		return nullptr;
	jclass cls = env->FindClass("java/lang/Long");
	jmethodID ctor = cls == nullptr ? nullptr : env->GetMethodID(cls, "<init>", "(J)V");
	return ctor == nullptr ? nullptr : env->NewObject(cls, ctor, bounded_long(total_size));
}

jobject stream_attachments(JNIEnv* env, const char* const* values, size_t count) {
	jclass strings = env->FindClass("java/lang/String");
	jobjectArray items = strings == nullptr
	                         ? nullptr
	                         : env->NewObjectArray(static_cast<jsize>(count), strings, nullptr);
	for (size_t i = 0; items != nullptr && i < count && !env->ExceptionCheck(); ++i) {
		jstring item = string(env, values[i]);
		if (!env->ExceptionCheck())
			env->SetObjectArrayElement(items, static_cast<jsize>(i), item);
		if (item != nullptr)
			env->DeleteLocalRef(item);
	}
	jclass arrays = env->ExceptionCheck() ? nullptr : env->FindClass("java/util/Arrays");
	jmethodID as_list =
	    arrays == nullptr
	        ? nullptr
	        : env->GetStaticMethodID(arrays, "asList", "([Ljava/lang/Object;)Ljava/util/List;");
	return as_list == nullptr ? nullptr : env->CallStaticObjectMethod(arrays, as_list, items);
}

void text_stream_event(RoomHandle* h, const lk_text_stream_event_t* value) {
	if (value == nullptr || value->content_size > static_cast<size_t>(INT32_MAX) ||
	    (value->content_size != 0 && value->content == nullptr) ||
	    value->attached_stream_id_count > static_cast<size_t>(INT32_MAX) ||
	    (value->attached_stream_id_count != 0 && value->attached_stream_ids == nullptr) ||
	    value->attribute_count > static_cast<size_t>(INT32_MAX) ||
	    (value->attribute_count != 0 && value->attributes == nullptr))
		return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jclass cls = h->text_stream_class;
		jmethodID ctor = env->GetMethodID(
		    cls, "<init>",
		    "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;[BJLjava/lang/String;"
		    "Ljava/lang/String;Ljava/lang/Long;Ljava/util/Map;JLjava/lang/String;"
		    "Ljava/util/List;)V");
		jobject attributes = env->ExceptionCheck()
		                         ? nullptr
		                         : attribute_map(env, value->attributes, value->attribute_count);
		jobject attachments = env->ExceptionCheck()
		                          ? nullptr
		                          : stream_attachments(env, value->attached_stream_ids,
		                                               value->attached_stream_id_count);
		jobject total = env->ExceptionCheck()
		                    ? nullptr
		                    : stream_total_size(env, value->has_total_size, value->total_size);
		jstring id = env->ExceptionCheck() ? nullptr : string(env, value->stream_id);
		jstring topic = env->ExceptionCheck() ? nullptr : string(env, value->topic);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		jbyteArray content =
		    env->ExceptionCheck() ? nullptr : array(env, value->content, value->content_size);
		jstring reason = env->ExceptionCheck() ? nullptr : string(env, value->reason);
		jstring mime = env->ExceptionCheck() ? nullptr : string(env, value->mime_type);
		jstring reply = env->ExceptionCheck() ? nullptr : string(env, value->reply_to_stream_id);
		jobject event =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->NewObject(cls, ctor, static_cast<jint>(value->type), id, topic, identity,
		                         content, bounded_long(value->chunk_index), reason, mime, total,
		                         attributes, static_cast<jlong>(value->timestamp), reply,
		                         attachments);
		if (event != nullptr && !env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback = owner == nullptr
			                         ? nullptr
			                         : env->GetMethodID(owner, "onNativeTextStream",
			                                            "(Lio/livekit/client/TextStreamEvent;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, event);
		}
		env->PopLocalFrame(nullptr);
	});
}

void byte_stream_event(RoomHandle* h, const lk_byte_stream_event_t* value) {
	if (value == nullptr || value->content_size > static_cast<size_t>(INT32_MAX) ||
	    (value->content_size != 0 && value->content == nullptr) ||
	    value->attribute_count > static_cast<size_t>(INT32_MAX) ||
	    (value->attribute_count != 0 && value->attributes == nullptr))
		return;
	deliver(h, [h, value](JNIEnv* env) {
		if (env->PushLocalFrame(24) != JNI_OK)
			return;
		jclass cls = h->byte_stream_class;
		jmethodID ctor = env->GetMethodID(
		    cls, "<init>",
		    "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
		    "Ljava/lang/String;[BJLjava/lang/String;Ljava/lang/Long;Ljava/util/Map;J)V");
		jobject attributes = env->ExceptionCheck()
		                         ? nullptr
		                         : attribute_map(env, value->attributes, value->attribute_count);
		jobject total = env->ExceptionCheck()
		                    ? nullptr
		                    : stream_total_size(env, value->has_total_size, value->total_size);
		jstring id = env->ExceptionCheck() ? nullptr : string(env, value->stream_id);
		jstring topic = env->ExceptionCheck() ? nullptr : string(env, value->topic);
		jstring identity =
		    env->ExceptionCheck() ? nullptr : string(env, value->participant_identity);
		jstring name = env->ExceptionCheck() ? nullptr : string(env, value->name);
		jstring mime = env->ExceptionCheck() ? nullptr : string(env, value->mime_type);
		jbyteArray content =
		    env->ExceptionCheck() ? nullptr : array(env, value->content, value->content_size);
		jstring reason = env->ExceptionCheck() ? nullptr : string(env, value->reason);
		jobject event =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->NewObject(cls, ctor, static_cast<jint>(value->type), id, topic, identity,
		                         name, mime, content, bounded_long(value->chunk_index), reason,
		                         total, attributes, static_cast<jlong>(value->timestamp));
		if (event != nullptr && !env->ExceptionCheck()) {
			jclass owner = env->GetObjectClass(h->owner);
			jmethodID callback = owner == nullptr
			                         ? nullptr
			                         : env->GetMethodID(owner, "onNativeByteStream",
			                                            "(Lio/livekit/client/ByteStreamEvent;)V");
			if (callback != nullptr)
				env->CallVoidMethod(h->owner, callback, event);
		}
		env->PopLocalFrame(nullptr);
	});
}

int reconnect_policy_callback(void* user_data, const lk_reconnect_context_t* context,
                              uint32_t* retry_delay_ms) {
	if (context == nullptr || retry_delay_ms == nullptr)
		return 0;
	auto* h = static_cast<RoomHandle*>(user_data);
	JNIEnv* env = nullptr;
	bool attached = false;
	if (h->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
		if (h->vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK)
			return 0;
		attached = true;
	}
	bool retry = false;
	if (env->PushLocalFrame(16) == JNI_OK) {
		jclass cls = env->FindClass("io/livekit/client/ReconnectContext");
		jmethodID ctor =
		    cls == nullptr ? nullptr : env->GetMethodID(cls, "<init>", "(JJILjava/lang/String;)V");
		jstring url = env->ExceptionCheck() ? nullptr : string(env, context->server_url);
		jobject value = env->ExceptionCheck()
		                    ? nullptr
		                    : env->NewObject(cls, ctor, static_cast<jlong>(context->retry_count),
		                                     bounded_long(context->elapsed_ms),
		                                     static_cast<jint>(context->reason), url);
		jclass owner = env->ExceptionCheck() ? nullptr : env->GetObjectClass(h->owner);
		jmethodID callback = owner == nullptr
		                         ? nullptr
		                         : env->GetMethodID(owner, "onNativeReconnectPolicy",
		                                            "(Lio/livekit/client/ReconnectContext;)"
		                                            "Ljava/lang/Long;");
		jobject delay =
		    callback == nullptr ? nullptr : env->CallObjectMethod(h->owner, callback, value);
		if (!env->ExceptionCheck() && delay != nullptr) {
			jclass number = env->FindClass("java/lang/Long");
			jmethodID as_long =
			    number == nullptr ? nullptr : env->GetMethodID(number, "longValue", "()J");
			if (as_long != nullptr) {
				jlong milliseconds = env->CallLongMethod(delay, as_long);
				if (!env->ExceptionCheck() && milliseconds >= 0 &&
				    static_cast<uint64_t>(milliseconds) <= UINT32_MAX) {
					*retry_delay_ms = static_cast<uint32_t>(milliseconds);
					retry = true;
				}
			}
		}
		if (env->ExceptionCheck())
			env->ExceptionClear();
		env->PopLocalFrame(nullptr);
	} else if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
	if (attached)
		h->vm->DetachCurrentThread();
	return retry ? 1 : 0;
}

lk_status_t token_source_callback(void* user_data, const lk_token_source_fetch_options_t* options,
                                  int force_refresh, lk_token_source_response_t* response) {
	auto* h = static_cast<RoomHandle*>(user_data);
	JNIEnv* env = nullptr;
	bool attached = false;
	jint env_status = h->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
	if (env_status == JNI_EDETACHED) {
		if (h->vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK)
			return LK_STATUS_EXCEPTION;
		attached = true;
	} else if (env_status != JNI_OK)
		return LK_STATUS_EXCEPTION;
	if (env->PushLocalFrame(32) != JNI_OK) {
		if (env->ExceptionCheck())
			env->ExceptionClear();
		if (attached)
			h->vm->DetachCurrentThread();
		return LK_STATUS_OPERATION_FAILED;
	}
	auto abort = [&] {
		if (env->ExceptionCheck())
			env->ExceptionClear();
		env->PopLocalFrame(nullptr);
		if (attached)
			h->vm->DetachCurrentThread();
		return LK_STATUS_OPERATION_FAILED;
	};
	jclass map_class = env->FindClass("java/util/HashMap");
	if (map_class == nullptr)
		return abort();
	jmethodID map_ctor = env->GetMethodID(map_class, "<init>", "()V");
	if (map_ctor == nullptr)
		return abort();
	jobject attributes = env->NewObject(map_class, map_ctor);
	if (attributes == nullptr || env->ExceptionCheck())
		return abort();
	jmethodID put = env->GetMethodID(map_class, "put",
	                                 "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
	if (put == nullptr)
		return abort();
	if (options != nullptr) {
		for (size_t i = 0; i < options->participant_attribute_count; ++i) {
			jstring key = string(env, options->participant_attributes[i].key);
			jstring value = string(env, options->participant_attributes[i].value);
			if (env->ExceptionCheck())
				return abort();
			jobject old = env->CallObjectMethod(attributes, put, key, value);
			if (env->ExceptionCheck())
				return abort();
			if (old != nullptr)
				env->DeleteLocalRef(old);
			env->DeleteLocalRef(key);
			env->DeleteLocalRef(value);
		}
	}
	std::array<jstring, 7> fields{string(env, options ? options->room_name : nullptr),
	                              string(env, options ? options->participant_name : nullptr),
	                              string(env, options ? options->participant_identity : nullptr),
	                              string(env, options ? options->participant_metadata : nullptr),
	                              string(env, options ? options->agent_name : nullptr),
	                              string(env, options ? options->agent_metadata : nullptr),
	                              string(env, options ? options->deployment : nullptr)};
	if (env->ExceptionCheck())
		return abort();
	jmethodID ctor = env->GetMethodID(
	    h->token_request_class, "<init>",
	    "(ZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
	    "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/util/Map;)V");
	if (ctor == nullptr)
		return abort();
	jobject request = env->NewObject(
	    h->token_request_class, ctor, force_refresh ? JNI_TRUE : JNI_FALSE, fields[0], fields[1],
	    fields[2], fields[3], fields[4], fields[5], fields[6], attributes);
	if (request == nullptr || env->ExceptionCheck())
		return abort();
	jclass owner = env->GetObjectClass(h->owner);
	if (owner == nullptr)
		return abort();
	jmethodID callback = env->GetMethodID(
	    owner, "onNativeTokenFetch",
	    "(Lio/livekit/client/TokenFetchRequest;)Lio/livekit/client/TokenCredentials;");
	if (callback == nullptr)
		return abort();
	jobject credentials = env->CallObjectMethod(h->owner, callback, request);
	thread_local std::string url, token;
	url.clear();
	token.clear();
	bool failed = env->ExceptionCheck() || credentials == nullptr;
	if (!failed) {
		jclass cls = env->GetObjectClass(credentials);
		jstring server = nullptr;
		jstring participant = nullptr;
		if (cls != nullptr && !env->ExceptionCheck()) {
			jmethodID get_server = env->GetMethodID(cls, "serverUrl", "()Ljava/lang/String;");
			jmethodID get_token =
			    get_server != nullptr && !env->ExceptionCheck()
			        ? env->GetMethodID(cls, "participantToken", "()Ljava/lang/String;")
			        : nullptr;
			if (get_token != nullptr && !env->ExceptionCheck()) {
				server = static_cast<jstring>(env->CallObjectMethod(credentials, get_server));
				if (!env->ExceptionCheck())
					participant =
					    static_cast<jstring>(env->CallObjectMethod(credentials, get_token));
			}
		}
		if (!env->ExceptionCheck()) {
			url = java_string_bytes(env, server);
			if (!env->ExceptionCheck())
				token = java_string_bytes(env, participant);
		}
		if (server != nullptr)
			env->DeleteLocalRef(server);
		if (participant != nullptr)
			env->DeleteLocalRef(participant);
		if (cls != nullptr)
			env->DeleteLocalRef(cls);
		failed = env->ExceptionCheck() || url.empty() || token.empty();
	}
	if (failed && env->ExceptionCheck())
		env->ExceptionClear();
	if (credentials != nullptr)
		env->DeleteLocalRef(credentials);
	env->DeleteLocalRef(owner);
	env->DeleteLocalRef(request);
	for (jstring field : fields)
		env->DeleteLocalRef(field);
	env->DeleteLocalRef(attributes);
	env->DeleteLocalRef(map_class);
	if (!failed) {
		response->server_url = url.c_str();
		response->participant_token = token.c_str();
	}
	env->PopLocalFrame(nullptr);
	if (attached)
		h->vm->DetachCurrentThread();
	return failed ? LK_STATUS_OPERATION_FAILED : LK_STATUS_OK;
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
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_logOptions(JNIEnv* env, jclass) {
	lk_log_options_t options;
	lk_log_options_init(&options);
	auto status = lk_log_get_options(&options);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/LogOptions");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(III)V");
	jobject result = env->NewObject(cls, ctor, static_cast<jint>(options.livekit_level),
	                                static_cast<jint>(options.webrtc_level),
	                                static_cast<jint>(options.websocket_level));
	env->DeleteLocalRef(cls);
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setLogOptions(JNIEnv* env, jclass,
                                                                   jint liveKitLevel,
                                                                   jint webRtcLevel,
                                                                   jint webSocketLevel) {
	lk_log_options_t options;
	lk_log_options_init(&options);
	options.livekit_level = static_cast<lk_log_level_t>(liveKitLevel);
	options.webrtc_level = static_cast<lk_log_level_t>(webRtcLevel);
	options.websocket_level = static_cast<lk_log_level_t>(webSocketLevel);
	fail(env, lk_log_set_options(&options));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setTraceOptions(JNIEnv* env, jclass,
                                                                     jboolean enabled,
                                                                     jlong categoryMask) {
	lk_trace_options_t options;
	lk_trace_options_init(&options);
	options.enabled = enabled ? 1 : 0;
	options.category_mask = static_cast<uint64_t>(categoryMask);
	fail(env, lk_trace_set_options(&options));
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_traceOptions(JNIEnv* env, jclass) {
	lk_trace_options_t options;
	lk_trace_options_init(&options);
	auto status = lk_trace_get_options(&options);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/TraceOptions");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZJ)V");
	jobject result = env->NewObject(cls, ctor, options.enabled ? JNI_TRUE : JNI_FALSE,
	                                static_cast<jlong>(options.category_mask));
	env->DeleteLocalRef(cls);
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_startTraceJson(JNIEnv* env, jclass,
                                                                    jbyteArray path) {
	std::string value = bytes(env, path);
	fail(env, lk_trace_start_json_file(value.c_str()));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_stopTrace(JNIEnv* env, jclass) {
	fail(env, lk_trace_stop());
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
	h->text_stream_class = global_class(env, "io/livekit/client/TextStreamEvent");
	h->byte_stream_class = global_class(env, "io/livekit/client/ByteStreamEvent");
	h->token_request_class = global_class(env, "io/livekit/client/TokenFetchRequest");
	h->remote_track_class = global_class(env, "io/livekit/client/RemoteTrackPublication");
	h->permissions_class = global_class(env, "io/livekit/client/ParticipantPermissions");
	if (env->ExceptionCheck() || !h->owner || !h->participant_class || !h->packet_class ||
	    !h->text_class || !h->byte_class || !h->chat_class || !h->text_stream_class ||
	    !h->byte_stream_class || !h->token_request_class || !h->remote_track_class ||
	    !h->permissions_class) {
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
	callbacks.on_room_metadata_changed = [](void* p, lk_room_t*, const char* metadata) {
		room_string_event(static_cast<RoomHandle*>(p), 0, nullptr, metadata);
	};
	callbacks.on_room_sid_changed = [](void* p, lk_room_t*, const char* previous, const char* sid) {
		room_string_event(static_cast<RoomHandle*>(p), 1, previous, sid);
	};
	callbacks.on_recording_status_changed = [](void* p, lk_room_t*, int recording) {
		event(static_cast<RoomHandle*>(p), 8, recording);
	};
	callbacks.on_token_refreshed = [](void* p, lk_room_t*) {
		event(static_cast<RoomHandle*>(p), 9);
	};
	callbacks.on_room_eos = [](void* p, lk_room_t*) { event(static_cast<RoomHandle*>(p), 10); };
	callbacks.on_room_updated = [](void* p, lk_room_t*, const lk_room_snapshot_t* value) {
		room_snapshot_event(static_cast<RoomHandle*>(p), 0, value);
	};
	callbacks.on_room_moved = [](void* p, lk_room_t*, const lk_room_snapshot_t* value) {
		room_snapshot_event(static_cast<RoomHandle*>(p), 1, value);
	};
	callbacks.on_participant_connected = [](void* p, lk_room_t*, const lk_participant_info_t* v) {
		event(static_cast<RoomHandle*>(p), 5, 0, v);
	};
	callbacks.on_participant_disconnected = [](void* p, lk_room_t*,
	                                           const lk_participant_info_t* v) {
		event(static_cast<RoomHandle*>(p), 6, 0, v);
	};
	callbacks.on_participant_metadata_changed = [](void* p, lk_room_t*, const char* previous,
	                                               const lk_participant_info_t* who) {
		participant_change_event(static_cast<RoomHandle*>(p), 0, who, previous);
	};
	callbacks.on_participant_name_changed = [](void* p, lk_room_t*, const char* name,
	                                           const lk_participant_info_t* who) {
		participant_change_event(static_cast<RoomHandle*>(p), 1, who, name);
	};
	callbacks.on_participant_attributes_changed = [](void* p, lk_room_t*,
	                                                 const lk_attribute_t* changes, size_t count,
	                                                 const lk_participant_info_t* who) {
		participant_change_event(static_cast<RoomHandle*>(p), 2, who, nullptr, changes, count);
	};
	callbacks.on_participant_permissions_changed =
	    [](void* p, lk_room_t*, const lk_participant_permissions_t* previous,
	       const lk_participant_permissions_t* current, const lk_participant_info_t* who) {
		    permissions_event(static_cast<RoomHandle*>(p), who, previous, current);
	    };
	callbacks.on_connection_quality_changed =
	    [](void* p, lk_room_t*, lk_connection_quality_t quality, const lk_participant_info_t* who) {
		    event(static_cast<RoomHandle*>(p), 11, static_cast<int>(quality), who);
	    };
	callbacks.on_active_speakers_changed = [](void* p, lk_room_t*,
	                                          const lk_participant_info_t* speakers, size_t count) {
		participant_list_event(static_cast<RoomHandle*>(p), 0, speakers, count);
	};
	callbacks.on_participants_updated =
	    [](void* p, lk_room_t*, const lk_participant_info_t* participants, size_t count) {
		    participant_list_event(static_cast<RoomHandle*>(p), 1, participants, count);
	    };
	callbacks.on_track_published = [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	                                  const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 0, track, who);
	};
	callbacks.on_track_unpublished = [](void* p, lk_room_t*,
	                                    const lk_track_publication_info_t* track,
	                                    const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 1, track, who);
	};
	callbacks.on_track_muted = [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	                              const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 2, track, who);
	};
	callbacks.on_track_unmuted = [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	                                const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 3, track, who);
	};
	callbacks.on_track_subscribed = [](void* p, lk_room_t*,
	                                   const lk_track_publication_info_t* track,
	                                   const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 4, track, who);
	};
	callbacks.on_track_unsubscribed = [](void* p, lk_room_t*,
	                                     const lk_track_publication_info_t* track,
	                                     const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 5, track, who);
	};
	callbacks.on_track_subscription_permission_changed =
	    [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	       const lk_participant_info_t* who,
	       int allowed) { track_event(static_cast<RoomHandle*>(p), 6, track, who, allowed); };
	callbacks.on_track_subscription_failed =
	    [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	       const lk_participant_info_t* who, lk_subscription_error_t error) {
		    track_event(static_cast<RoomHandle*>(p), 7, track, who, static_cast<int>(error));
	    };
	callbacks.on_track_stream_state_changed =
	    [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	       const lk_participant_info_t* who, lk_track_stream_state_t state) {
		    track_event(static_cast<RoomHandle*>(p), 8, track, who, static_cast<int>(state));
	    };
	callbacks.on_track_subscription_status_changed =
	    [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	       const lk_participant_info_t* who, lk_track_subscription_status_t status) {
		    track_event(static_cast<RoomHandle*>(p), 9, track, who, static_cast<int>(status));
	    };
	callbacks.on_local_track_published = [](void* p, lk_room_t*,
	                                        const lk_track_publication_info_t* track,
	                                        const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 10, track, who);
	};
	callbacks.on_local_track_unpublished = [](void* p, lk_room_t*,
	                                          const lk_track_publication_info_t* track,
	                                          const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 11, track, who);
	};
	callbacks.on_local_track_subscribed = [](void* p, lk_room_t*,
	                                         const lk_track_publication_info_t* track,
	                                         const lk_participant_info_t* who) {
		track_event(static_cast<RoomHandle*>(p), 12, track, who);
	};
	callbacks.on_subscribed_quality_update =
	    [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	       const lk_participant_info_t* who, const lk_subscribed_quality_update_t* update) {
		    quality_event(static_cast<RoomHandle*>(p), track, who, update);
	    };
	callbacks.on_data_received = [](void* p, lk_room_t*, const lk_data_received_t* v) {
		event(static_cast<RoomHandle*>(p), 7, 0, nullptr, v);
	};
	callbacks.on_audio_frame = [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	                              const lk_participant_info_t* who, const lk_audio_frame_t* frame) {
		audio_frame_event(static_cast<RoomHandle*>(p), track, who, frame);
	};
	callbacks.on_video_frame_with_metadata =
	    [](void* p, lk_room_t*, const lk_track_publication_info_t* track,
	       const lk_participant_info_t* who, const lk_video_frame_t* frame,
	       const lk_video_frame_metadata_t* metadata) {
		    video_frame_event(static_cast<RoomHandle*>(p), track, who, frame, metadata);
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
	callbacks.on_transcription_received = [](void* p, lk_room_t*,
	                                         const lk_transcription_received_t* value) {
		transcription_event(static_cast<RoomHandle*>(p), value);
	};
	callbacks.on_metrics_received = [](void* p, lk_room_t*, const lk_metrics_received_t* value) {
		metrics_event(static_cast<RoomHandle*>(p), value);
	};
	callbacks.on_sip_dtmf_received = [](void* p, lk_room_t*, const lk_sip_dtmf_t* value) {
		sip_dtmf_event(static_cast<RoomHandle*>(p), value);
	};
	callbacks.on_data_channel_buffer_status_changed =
	    [](void* p, lk_room_t*, const lk_data_channel_buffer_status_t* value) {
		    data_channel_event(static_cast<RoomHandle*>(p), value);
	    };
	callbacks.on_encryption_state_changed = [](void* p, lk_room_t*,
	                                           const lk_encryption_state_t* value) {
		encryption_event(static_cast<RoomHandle*>(p), value);
	};
	callbacks.on_data_track_published = [](void* p, lk_room_t*, const lk_data_track_info_t* track,
	                                       const lk_participant_info_t* who) {
		data_track_event(static_cast<RoomHandle*>(p), 0, track, who);
	};
	callbacks.on_data_track_unpublished = [](void* p, lk_room_t*, const lk_data_track_info_t* track,
	                                         const lk_participant_info_t* who) {
		data_track_event(static_cast<RoomHandle*>(p), 1, track, who);
	};
	callbacks.on_local_data_track_published = [](void* p, lk_room_t*,
	                                             const lk_data_track_info_t* track,
	                                             const lk_participant_info_t* who) {
		data_track_event(static_cast<RoomHandle*>(p), 2, track, who);
	};
	callbacks.on_local_data_track_unpublished = [](void* p, lk_room_t*,
	                                               const lk_data_track_info_t* track,
	                                               const lk_participant_info_t* who) {
		data_track_event(static_cast<RoomHandle*>(p), 3, track, who);
	};
	callbacks.on_data_track_frame = [](void* p, lk_room_t*, const lk_data_track_info_t* track,
	                                   const lk_participant_info_t* who,
	                                   const lk_data_track_frame_view_t* frame) {
		data_track_event(static_cast<RoomHandle*>(p), 4, track, who, frame);
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

JNIEXPORT void JNICALL Java_io_livekit_client_Native_setFrameCallbacksEnabled(JNIEnv*, jclass,
                                                                              jlong ptr,
                                                                              jboolean enabled) {
	handle(ptr)->frame_callbacks_enabled.store(enabled == JNI_TRUE, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL Java_io_livekit_client_Native_connect(JNIEnv* env, jclass, jlong ptr,
                                                             jbyteArray url, jbyteArray token) {
	std::string u = bytes(env, url), t = bytes(env, token);
	fail(env, lk_room_connect(handle(ptr)->room, u.c_str(), t.c_str()));
}
void with_connect_options(
    JNIEnv* env, RoomHandle* h, jobject config,
    const std::function<lk_status_t(const lk_room_connect_options_t*)>& connect) {
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
	jmethodID get_policy =
	    env->GetMethodID(cls, "reconnectPolicy", "()Lio/livekit/client/ReconnectPolicy;");
	jobject policy = get_policy == nullptr ? nullptr : env->CallObjectMethod(config, get_policy);
	if (policy != nullptr && !env->ExceptionCheck()) {
		options.reconnect_policy = reconnect_policy_callback;
		options.reconnect_policy_user_data = h;
	}
	if (policy != nullptr)
		env->DeleteLocalRef(policy);
	if (env->ExceptionCheck()) {
		env->DeleteLocalRef(cls);
		return;
	}
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
	std::string key, salt, magic;
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
		jbyteArray raw_magic = static_cast<jbyteArray>(env->CallObjectMethod(
		    encryption, env->GetMethodID(e2ee_cls, "unencryptedMagicBytes", "()[B")));
		key = bytes(env, raw_key);
		salt = bytes(env, raw_salt);
		magic = bytes(env, raw_magic);
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
		e2ee.unencrypted_magic_bytes = reinterpret_cast<const uint8_t*>(magic.data());
		e2ee.unencrypted_magic_bytes_size = magic.size();
		e2ee.ratchet_window_size = static_cast<size_t>(
		    env->CallIntMethod(encryption, env->GetMethodID(e2ee_cls, "ratchetWindowSize", "()I")));
		e2ee.failure_tolerance =
		    env->CallIntMethod(encryption, env->GetMethodID(e2ee_cls, "failureTolerance", "()I"));
		e2ee.key_ring_size = static_cast<size_t>(
		    env->CallIntMethod(encryption, env->GetMethodID(e2ee_cls, "keyRingSize", "()I")));
		options.e2ee_options = &e2ee;
		env->DeleteLocalRef(raw_key);
		env->DeleteLocalRef(raw_salt);
		env->DeleteLocalRef(raw_magic);
		env->DeleteLocalRef(e2ee_cls);
		env->DeleteLocalRef(encryption);
	}
	env->DeleteLocalRef(ice);
	env->DeleteLocalRef(cls);
	if (env->ExceptionCheck())
		return;
	fail(env, connect(&options));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_connectWithOptions(JNIEnv* env, jclass,
                                                                        jlong ptr, jbyteArray url,
                                                                        jbyteArray token,
                                                                        jobject config) {
	std::string u = bytes(env, url), t = bytes(env, token);
	if (env->ExceptionCheck())
		return;
	with_connect_options(
	    env, handle(ptr), config, [ptr, &u, &t](const lk_room_connect_options_t* options) {
		    return lk_room_connect_with_options(handle(ptr)->room, u.c_str(), t.c_str(), options);
	    });
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_connectWithTokenSource(
    JNIEnv* env, jclass, jlong ptr, jobjectArray fields, jobjectArray attributeKeys,
    jobjectArray attributeValues, jobject config) {
	if (fields == nullptr || attributeKeys == nullptr || attributeValues == nullptr ||
	    env->GetArrayLength(fields) != 7 ||
	    env->GetArrayLength(attributeKeys) != env->GetArrayLength(attributeValues)) {
		jclass cls = env->FindClass("java/lang/IllegalArgumentException");
		if (cls != nullptr)
			env->ThrowNew(cls, "Invalid token fetch options");
		return;
	}
	std::array<std::string, 7> values;
	for (jsize i = 0; i < 7; ++i) {
		jbyteArray field = static_cast<jbyteArray>(env->GetObjectArrayElement(fields, i));
		if (env->ExceptionCheck())
			return;
		values[static_cast<size_t>(i)] = bytes(env, field);
		env->DeleteLocalRef(field);
		if (env->ExceptionCheck())
			return;
	}
	const jsize count = env->GetArrayLength(attributeKeys);
	std::vector<std::string> keys, attributes;
	keys.reserve(static_cast<size_t>(count));
	attributes.reserve(static_cast<size_t>(count));
	for (jsize i = 0; i < count; ++i) {
		jbyteArray key = static_cast<jbyteArray>(env->GetObjectArrayElement(attributeKeys, i));
		jbyteArray value =
		    env->ExceptionCheck()
		        ? nullptr
		        : static_cast<jbyteArray>(env->GetObjectArrayElement(attributeValues, i));
		if (env->ExceptionCheck())
			return;
		keys.push_back(bytes(env, key));
		if (!env->ExceptionCheck())
			attributes.push_back(bytes(env, value));
		env->DeleteLocalRef(key);
		env->DeleteLocalRef(value);
		if (env->ExceptionCheck())
			return;
	}
	std::vector<lk_attribute_t> copied(static_cast<size_t>(count));
	for (jsize i = 0; i < count; ++i) {
		copied[static_cast<size_t>(i)].key = keys[static_cast<size_t>(i)].c_str();
		copied[static_cast<size_t>(i)].value = attributes[static_cast<size_t>(i)].c_str();
	}
	lk_token_source_fetch_options_t options;
	lk_token_source_fetch_options_init(&options);
	options.room_name = values[0].c_str();
	options.participant_name = values[1].c_str();
	options.participant_identity = values[2].c_str();
	options.participant_metadata = values[3].c_str();
	options.agent_name = values[4].c_str();
	options.agent_metadata = values[5].c_str();
	options.deployment = values[6].c_str();
	options.participant_attributes = copied.data();
	options.participant_attribute_count = copied.size();
	auto* h = handle(ptr);
	if (config == nullptr) {
		fail(env, lk_room_connect_with_token_source(h->room, token_source_callback, h, &options,
		                                            nullptr));
		return;
	}
	with_connect_options(env, h, config, [h, &options](const lk_room_connect_options_t* connect) {
		return lk_room_connect_with_token_source_and_options(h->room, token_source_callback, h,
		                                                     &options, connect);
	});
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
JNIEXPORT void JNICALL Java_io_livekit_client_Native_sendWithOptions(
    JNIEnv* env, jclass, jlong ptr, jint kind, jbyteArray value, jobjectArray fields,
    jobjectArray destinations, jobjectArray attributeKeys, jobjectArray attributeValues,
    jobjectArray attachedStreamIds, jint chunkSize, jboolean compress) {
	if (kind < 0 || kind > 2 || fields == nullptr || env->GetArrayLength(fields) != 4 ||
	    destinations == nullptr || attributeKeys == nullptr || attributeValues == nullptr ||
	    attachedStreamIds == nullptr ||
	    env->GetArrayLength(attributeKeys) != env->GetArrayLength(attributeValues)) {
		fail(env, LK_STATUS_INVALID_ARGUMENT);
		return;
	}
	auto read = [env](jobjectArray source) {
		std::vector<std::string> result;
		for (jsize i = 0; i < env->GetArrayLength(source); ++i) {
			auto* part = static_cast<jbyteArray>(env->GetObjectArrayElement(source, i));
			result.push_back(bytes(env, part));
			if (part != nullptr)
				env->DeleteLocalRef(part);
		}
		return result;
	};
	auto pointers = [](const std::vector<std::string>& source) {
		std::vector<const char*> result;
		for (const auto& part : source)
			result.push_back(part.c_str());
		return result;
	};
	auto field_values = read(fields);
	auto identities = read(destinations);
	auto destination_pointers = pointers(identities);
	auto attachment_ids = read(attachedStreamIds);
	auto attachment_pointers = pointers(attachment_ids);
	auto keys = read(attributeKeys);
	auto values = read(attributeValues);
	std::vector<lk_attribute_t> attributes;
	for (size_t i = 0; i < keys.size(); ++i)
		attributes.push_back({keys[i].c_str(), values[i].c_str()});
	auto field = [&](jsize index) {
		auto* item = env->GetObjectArrayElement(fields, index);
		bool present = item != nullptr;
		if (item != nullptr)
			env->DeleteLocalRef(item);
		return present ? field_values[static_cast<size_t>(index)].c_str() : nullptr;
	};
	const char* topic = field(0);
	const char* mime_type = field(1);
	const char* name = field(2);
	const char* reply_to = field(3);
	std::string payload = bytes(env, value);
	if (kind == 0) {
		lk_text_send_options_t options;
		lk_text_send_options_init(&options);
		options.topic = topic;
		options.destination_identities = destination_pointers.data();
		options.destination_identity_count = destination_pointers.size();
		options.attributes = attributes.data();
		options.attribute_count = attributes.size();
		options.reply_to_stream_id = reply_to;
		options.attached_stream_ids = attachment_pointers.data();
		options.attached_stream_id_count = attachment_pointers.size();
		options.chunk_size = static_cast<size_t>(chunkSize);
		options.compress = compress == JNI_TRUE;
		fail(env, lk_room_send_text(handle(ptr)->room, payload.c_str(), &options));
		return;
	}
	if (kind == 1) {
		lk_byte_send_options_t options;
		lk_byte_send_options_init(&options);
		options.topic = topic;
		if (mime_type != nullptr)
			options.mime_type = mime_type;
		options.name = name;
		options.destination_identities = destination_pointers.data();
		options.destination_identity_count = destination_pointers.size();
		options.attributes = attributes.data();
		options.attribute_count = attributes.size();
		options.chunk_size = static_cast<size_t>(chunkSize);
		options.compress = compress == JNI_TRUE;
		fail(env,
		     lk_room_send_bytes(handle(ptr)->room, reinterpret_cast<const uint8_t*>(payload.data()),
		                        payload.size(), &options));
		return;
	}
	lk_file_send_options_t options;
	lk_file_send_options_init(&options);
	if (topic != nullptr)
		options.topic = topic;
	if (mime_type != nullptr)
		options.mime_type = mime_type;
	options.destination_identities = destination_pointers.data();
	options.destination_identity_count = destination_pointers.size();
	options.attributes = attributes.data();
	options.attribute_count = attributes.size();
	options.chunk_size = static_cast<size_t>(chunkSize);
	options.compress = compress == JNI_TRUE;
	fail(env, lk_room_send_file(handle(ptr)->room, payload.c_str(), &options));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_createStreamWriter(
    JNIEnv* env, jclass, jlong room, jboolean text, jobjectArray fields, jobjectArray destinations,
    jobjectArray attributeKeys, jobjectArray attributeValues, jobjectArray attachedStreamIds,
    jlong totalSize, jboolean hasTotalSize, jint chunkSize, jboolean compress, jboolean update,
    jint version) {
	if (fields == nullptr || env->GetArrayLength(fields) != 5 || destinations == nullptr ||
	    attributeKeys == nullptr || attributeValues == nullptr || attachedStreamIds == nullptr ||
	    env->GetArrayLength(attributeKeys) != env->GetArrayLength(attributeValues)) {
		fail(env, LK_STATUS_INVALID_ARGUMENT);
		return 0;
	}
	auto read = [env](jobjectArray values) {
		std::vector<std::string> result;
		for (jsize i = 0; i < env->GetArrayLength(values); ++i) {
			auto* value = static_cast<jbyteArray>(env->GetObjectArrayElement(values, i));
			result.push_back(bytes(env, value));
			if (value != nullptr)
				env->DeleteLocalRef(value);
		}
		return result;
	};
	auto pointers = [](const std::vector<std::string>& values) {
		std::vector<const char*> result;
		for (const auto& value : values)
			result.push_back(value.c_str());
		return result;
	};
	auto field_values = read(fields);
	auto identities = read(destinations);
	auto destination_pointers = pointers(identities);
	auto attached_ids = read(attachedStreamIds);
	auto attached_pointers = pointers(attached_ids);
	auto keys = read(attributeKeys);
	auto values = read(attributeValues);
	std::vector<lk_attribute_t> attributes;
	for (size_t i = 0; i < keys.size(); ++i)
		attributes.push_back({keys[i].c_str(), values[i].c_str()});
	auto optional_field = [&](jsize index) {
		auto* value = env->GetObjectArrayElement(fields, index);
		bool present = value != nullptr;
		if (value != nullptr)
			env->DeleteLocalRef(value);
		return present ? field_values[static_cast<size_t>(index)].c_str() : nullptr;
	};
	const char* topic = optional_field(0);
	const char* mime_type = optional_field(1);
	const char* name = optional_field(2);
	const char* reply_to = optional_field(3);
	const char* stream_id = optional_field(4);
	if (text == JNI_TRUE) {
		lk_stream_text_options_t options;
		lk_stream_text_options_init(&options);
		options.topic = topic;
		options.destination_identities = destination_pointers.data();
		options.destination_identity_count = destination_pointers.size();
		options.attributes = attributes.data();
		options.attribute_count = attributes.size();
		options.reply_to_stream_id = reply_to;
		options.attached_stream_ids = attached_pointers.data();
		options.attached_stream_id_count = attached_pointers.size();
		options.stream_id = stream_id;
		options.has_total_size = hasTotalSize == JNI_TRUE;
		options.total_size = static_cast<uint64_t>(totalSize);
		options.chunk_size = static_cast<size_t>(chunkSize);
		options.compress = compress == JNI_TRUE;
		options.update = update == JNI_TRUE;
		options.version = version;
		lk_text_stream_writer_t* writer = nullptr;
		auto status = lk_room_stream_text(handle(room)->room, &options, &writer);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			return 0;
		}
		return reinterpret_cast<jlong>(writer);
	}
	lk_stream_bytes_options_t options;
	lk_stream_bytes_options_init(&options);
	options.topic = topic;
	if (mime_type != nullptr)
		options.mime_type = mime_type;
	if (name != nullptr)
		options.name = name;
	options.destination_identities = destination_pointers.data();
	options.destination_identity_count = destination_pointers.size();
	options.attributes = attributes.data();
	options.attribute_count = attributes.size();
	options.stream_id = stream_id;
	options.has_total_size = hasTotalSize == JNI_TRUE;
	options.total_size = static_cast<uint64_t>(totalSize);
	options.chunk_size = static_cast<size_t>(chunkSize);
	options.compress = compress == JNI_TRUE;
	lk_byte_stream_writer_t* writer = nullptr;
	auto status = lk_room_stream_bytes(handle(room)->room, &options, &writer);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(writer);
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_streamWriterId(JNIEnv* env, jclass,
                                                                          jlong ptr,
                                                                          jboolean text) {
	if (text == JNI_TRUE) {
		auto* writer = reinterpret_cast<lk_text_stream_writer_t*>(ptr);
		return getter(
		    env, [writer](char* p, size_t n) { return lk_text_stream_writer_id(writer, p, n); });
	}
	auto* writer = reinterpret_cast<lk_byte_stream_writer_t*>(ptr);
	return getter(env,
	              [writer](char* p, size_t n) { return lk_byte_stream_writer_id(writer, p, n); });
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_streamWriterClosed(JNIEnv*, jclass,
                                                                            jlong ptr,
                                                                            jboolean text) {
	return (text == JNI_TRUE
	            ? lk_text_stream_writer_is_closed(reinterpret_cast<lk_text_stream_writer_t*>(ptr))
	            : lk_byte_stream_writer_is_closed(reinterpret_cast<lk_byte_stream_writer_t*>(ptr)))
	           ? JNI_TRUE
	           : JNI_FALSE;
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_streamWriterInfo(JNIEnv* env, jclass,
                                                                         jlong ptr, jboolean text) {
	lk_data_stream_writer_info_snapshot_t* raw = nullptr;
	lk_status_t status = text == JNI_TRUE
	                         ? lk_text_stream_writer_create_info_snapshot(
	                               reinterpret_cast<lk_text_stream_writer_t*>(ptr), &raw)
	                         : lk_byte_stream_writer_create_info_snapshot(
	                               reinterpret_cast<lk_byte_stream_writer_t*>(ptr), &raw);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_data_stream_writer_info_snapshot_t,
	                decltype(&lk_data_stream_writer_info_snapshot_destroy)>
	    snapshot(raw, lk_data_stream_writer_info_snapshot_destroy);
	lk_data_stream_writer_info_t info;
	lk_data_stream_writer_info_init(&info);
	status = lk_data_stream_writer_info_snapshot_info(snapshot.get(), &info);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	if (info.attribute_count > static_cast<size_t>(INT32_MAX) ||
	    info.attached_stream_id_count > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
		              "Too much stream metadata");
		return nullptr;
	}
	auto read = [env, &snapshot](auto fn) {
		return getter(
		    env, [&snapshot, fn](char* out, size_t size) { return fn(snapshot.get(), out, size); });
	};
	auto indexed = [env, &snapshot](auto fn, size_t index) {
		jbyteArray raw_value = getter(env, [&snapshot, fn, index](char* out, size_t size) {
			return fn(snapshot.get(), index, out, size);
		});
		std::string value = bytes(env, raw_value);
		env->DeleteLocalRef(raw_value);
		return value;
	};
	std::vector<std::string> keys, values, attached;
	keys.reserve(info.attribute_count);
	values.reserve(info.attribute_count);
	attached.reserve(info.attached_stream_id_count);
	for (size_t i = 0; i < info.attribute_count; ++i) {
		keys.push_back(indexed(lk_data_stream_writer_info_snapshot_attribute_key, i));
		values.push_back(indexed(lk_data_stream_writer_info_snapshot_attribute_value, i));
	}
	for (size_t i = 0; i < info.attached_stream_id_count; ++i)
		attached.push_back(indexed(lk_data_stream_writer_info_snapshot_attached_stream_id, i));
	std::vector<lk_attribute_t> attributes;
	for (size_t i = 0; i < keys.size(); ++i)
		attributes.push_back({keys[i].c_str(), values[i].c_str()});
	std::vector<const char*> attachment_pointers;
	for (const auto& item : attached)
		attachment_pointers.push_back(item.c_str());
	jobject attribute_values = attribute_map(env, attributes.data(), attributes.size());
	jobject attachments =
	    stream_attachments(env, attachment_pointers.data(), attachment_pointers.size());
	jbyteArray raw_id = read(lk_data_stream_writer_info_snapshot_stream_id);
	jbyteArray raw_mime = read(lk_data_stream_writer_info_snapshot_mime_type);
	jbyteArray raw_topic = read(lk_data_stream_writer_info_snapshot_topic);
	jbyteArray raw_identity = read(lk_data_stream_writer_info_snapshot_participant_identity);
	jbyteArray raw_name = read(lk_data_stream_writer_info_snapshot_name);
	jbyteArray raw_reply = read(lk_data_stream_writer_info_snapshot_reply_to_stream_id);
	jstring id = string(env, bytes(env, raw_id).c_str());
	jstring mime = string(env, bytes(env, raw_mime).c_str());
	jstring topic = string(env, bytes(env, raw_topic).c_str());
	jstring identity = string(env, bytes(env, raw_identity).c_str());
	jstring name = string(env, bytes(env, raw_name).c_str());
	jstring reply = string(env, bytes(env, raw_reply).c_str());
	jobject total_size = stream_total_size(env, info.has_total_size, info.total_size);
	jclass cls = env->FindClass("io/livekit/client/StreamWriterInfo");
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>",
	    "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
	    "Ljava/lang/String;Ljava/lang/String;Ljava/lang/Long;JLjava/util/Map;Ljava/util/List;)V");
	jobject result = env->NewObject(
	    cls, ctor, static_cast<jint>(info.kind), id, mime, topic, identity, name, reply, total_size,
	    static_cast<jlong>(info.timestamp), attribute_values, attachments);
	for (jobject value :
	     {static_cast<jobject>(raw_id), static_cast<jobject>(raw_mime),
	      static_cast<jobject>(raw_topic), static_cast<jobject>(raw_identity),
	      static_cast<jobject>(raw_name), static_cast<jobject>(raw_reply), static_cast<jobject>(id),
	      static_cast<jobject>(mime), static_cast<jobject>(topic), static_cast<jobject>(identity),
	      static_cast<jobject>(name), static_cast<jobject>(reply), total_size, attribute_values,
	      attachments, static_cast<jobject>(cls)}) {
		if (value != nullptr)
			env->DeleteLocalRef(value);
	}
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_writeStream(JNIEnv* env, jclass, jlong ptr,
                                                                 jboolean text, jbyteArray data) {
	std::string payload = bytes(env, data);
	fail(env, text == JNI_TRUE
	              ? lk_text_stream_writer_write(reinterpret_cast<lk_text_stream_writer_t*>(ptr),
	                                            payload.data(), payload.size())
	              : lk_byte_stream_writer_write(reinterpret_cast<lk_byte_stream_writer_t*>(ptr),
	                                            reinterpret_cast<const uint8_t*>(payload.data()),
	                                            payload.size()));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_closeStreamWriter(JNIEnv* env, jclass,
                                                                       jlong ptr, jboolean text) {
	fail(env, text == JNI_TRUE
	              ? lk_text_stream_writer_close(reinterpret_cast<lk_text_stream_writer_t*>(ptr))
	              : lk_byte_stream_writer_close(reinterpret_cast<lk_byte_stream_writer_t*>(ptr)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_cancelStream(JNIEnv* env, jclass, jlong ptr,
                                                                  jboolean text,
                                                                  jbyteArray reason) {
	std::string value = bytes(env, reason);
	fail(env, text == JNI_TRUE
	              ? lk_text_stream_writer_cancel(reinterpret_cast<lk_text_stream_writer_t*>(ptr),
	                                             value.c_str())
	              : lk_byte_stream_writer_cancel(reinterpret_cast<lk_byte_stream_writer_t*>(ptr),
	                                             value.c_str()));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyStreamWriter(JNIEnv*, jclass, jlong ptr,
                                                                         jboolean text) {
	if (text == JNI_TRUE)
		lk_text_stream_writer_destroy(reinterpret_cast<lk_text_stream_writer_t*>(ptr));
	else
		lk_byte_stream_writer_destroy(reinterpret_cast<lk_byte_stream_writer_t*>(ptr));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_registerStreamHandler(JNIEnv* env, jclass,
                                                                           jlong ptr, jboolean text,
                                                                           jbyteArray topic) {
	auto* h = handle(ptr);
	std::string name = bytes(env, topic);
	if (text == JNI_TRUE) {
		fail(env, lk_room_register_text_stream_handler(
		              h->room, name.c_str(),
		              [](void* context, lk_room_t*, const lk_text_stream_event_t* event) {
			              text_stream_event(static_cast<RoomHandle*>(context), event);
		              },
		              h));
	} else {
		fail(env, lk_room_register_byte_stream_handler(
		              h->room, name.c_str(),
		              [](void* context, lk_room_t*, const lk_byte_stream_event_t* event) {
			              byte_stream_event(static_cast<RoomHandle*>(context), event);
		              },
		              h));
	}
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_unregisterStreamHandler(JNIEnv* env, jclass,
                                                                             jlong ptr,
                                                                             jboolean text,
                                                                             jbyteArray topic) {
	std::string name = bytes(env, topic);
	fail(env, text == JNI_TRUE
	              ? lk_room_unregister_text_stream_handler(handle(ptr)->room, name.c_str())
	              : lk_room_unregister_byte_stream_handler(handle(ptr)->room, name.c_str()));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_sendDtmf(JNIEnv* env, jclass, jlong ptr,
                                                              jint code, jbyteArray digit) {
	std::string v = bytes(env, digit);
	fail(env, lk_room_publish_dtmf(handle(ptr)->room, static_cast<uint32_t>(code), v.c_str()));
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_sendChatMessage(JNIEnv* env, jclass,
                                                                        jlong ptr,
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
	jclass cls = env->FindClass("io/livekit/client/SentChatMessage");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;J)V");
	jstring message_id = string(env, id);
	jobject result = env->NewObject(cls, ctor, message_id, static_cast<jlong>(timestamp));
	env->DeleteLocalRef(message_id);
	env->DeleteLocalRef(cls);
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_editChat(JNIEnv* env, jclass, jlong ptr,
                                                              jbyteArray messageId,
                                                              jlong originalTimestamp,
                                                              jbyteArray message) {
	std::string id = bytes(env, messageId);
	std::string value = bytes(env, message);
	fail(env, lk_room_edit_chat_message(handle(ptr)->room, id.c_str(), originalTimestamp,
	                                    value.c_str()));
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
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;FIZZ)V");
	size_t count = lk_remote_participant_list_count(list);
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	for (size_t i = 0; i < count; ++i) {
		const lk_remote_participant_snapshot_t* p = nullptr;
		status = lk_remote_participant_list_at(list, i, &p);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			break;
		}
		lk_remote_participant_snapshot_info_t info;
		lk_remote_participant_snapshot_info_init(&info);
		status = lk_remote_participant_snapshot_info(p, &info);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			break;
		}
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
		jobject item = env->NewObject(cls, ctor, sid, identity, name, metadata, info.audio_level,
		                              static_cast<jint>(info.connection_quality),
		                              info.is_speaking == 0 ? JNI_FALSE : JNI_TRUE, JNI_FALSE);
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
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_localParticipant(JNIEnv* env, jclass,
                                                                         jlong ptr) {
	lk_local_participant_snapshot_t* snapshot = nullptr;
	lk_status_t status = lk_room_create_local_participant_snapshot(handle(ptr)->room, &snapshot);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	lk_local_participant_snapshot_info_t info;
	lk_local_participant_snapshot_info_init(&info);
	status = lk_local_participant_snapshot_info(snapshot, &info);
	if (status != LK_STATUS_OK) {
		lk_local_participant_snapshot_destroy(snapshot);
		fail(env, status);
		return nullptr;
	}
	auto read = [env, snapshot](auto fn) {
		jbyteArray raw =
		    getter(env, [snapshot, fn](char* out, size_t n) { return fn(snapshot, out, n); });
		std::string value = bytes(env, raw);
		env->DeleteLocalRef(raw);
		return string(env, value.c_str());
	};
	jstring sid = read(lk_local_participant_snapshot_sid);
	jstring identity = read(lk_local_participant_snapshot_identity);
	jstring name = read(lk_local_participant_snapshot_name);
	jstring metadata = read(lk_local_participant_snapshot_metadata);
	jclass cls = env->FindClass("io/livekit/client/Participant");
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>",
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;FIZZ)V");
	jobject result = env->NewObject(cls, ctor, sid, identity, name, metadata, info.audio_level,
	                                static_cast<jint>(info.connection_quality),
	                                info.is_speaking == 0 ? JNI_FALSE : JNI_TRUE, JNI_TRUE);
	env->DeleteLocalRef(sid);
	env->DeleteLocalRef(identity);
	env->DeleteLocalRef(name);
	env->DeleteLocalRef(metadata);
	env->DeleteLocalRef(cls);
	lk_local_participant_snapshot_destroy(snapshot);
	return result;
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_localParticipantDetails(JNIEnv* env, jclass,
                                                                                jlong ptr) {
	lk_local_participant_snapshot_t* raw = nullptr;
	lk_status_t status = lk_room_create_local_participant_snapshot(handle(ptr)->room, &raw);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_local_participant_snapshot_t,
	                decltype(&lk_local_participant_snapshot_destroy)>
	    snapshot(raw, lk_local_participant_snapshot_destroy);
	lk_local_participant_snapshot_info_t info;
	lk_local_participant_snapshot_info_init(&info);
	status = lk_local_participant_snapshot_info(snapshot.get(), &info);
	lk_participant_permissions_t access{};
	if (status == LK_STATUS_OK)
		status = lk_local_participant_snapshot_permissions(snapshot.get(), &access);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	const size_t attribute_count = lk_local_participant_snapshot_attribute_count(snapshot.get());
	if (attribute_count > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
		              "Too many participant attributes");
		return nullptr;
	}
	std::string sid = snapshot_string(env, snapshot.get(), lk_local_participant_snapshot_sid);
	std::string identity =
	    snapshot_string(env, snapshot.get(), lk_local_participant_snapshot_identity);
	std::string name = snapshot_string(env, snapshot.get(), lk_local_participant_snapshot_name);
	std::string metadata =
	    snapshot_string(env, snapshot.get(), lk_local_participant_snapshot_metadata);
	lk_participant_info_t who{
	    sid.c_str(),      identity.c_str(),        name.c_str(),     metadata.c_str(),
	    info.audio_level, info.connection_quality, info.is_speaking, 1};
	jobject attributes = snapshot_attribute_map(env, snapshot.get(), attribute_count,
	                                            lk_local_participant_snapshot_attribute_key,
	                                            lk_local_participant_snapshot_attribute_value);
	jclass publication_class = env->FindClass("io/livekit/client/RemoteTrackPublication");
	jobjectArray publications = env->NewObjectArray(0, publication_class, nullptr);
	jobject result = participant_details(env, handle(ptr), who, access, attributes, publications);
	env->DeleteLocalRef(attributes);
	env->DeleteLocalRef(publications);
	env->DeleteLocalRef(publication_class);
	return result;
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_remoteParticipantDetails(JNIEnv* env,
                                                                                      jclass,
                                                                                      jlong ptr) {
	lk_remote_participant_list_t* raw = nullptr;
	lk_status_t status = lk_room_create_remote_participant_snapshot(handle(ptr)->room, &raw);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_remote_participant_list_t, decltype(&lk_remote_participant_list_destroy)>
	    snapshot(raw, lk_remote_participant_list_destroy);
	const size_t count = lk_remote_participant_list_count(snapshot.get());
	if (count > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Too many participants");
		return nullptr;
	}
	jclass details_class = env->FindClass("io/livekit/client/ParticipantDetails");
	jclass publication_class = env->FindClass("io/livekit/client/RemoteTrackPublication");
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), details_class, nullptr);
	for (size_t i = 0; i < count; ++i) {
		const lk_remote_participant_snapshot_t* participant = nullptr;
		status = lk_remote_participant_list_at(snapshot.get(), i, &participant);
		if (status != LK_STATUS_OK)
			break;
		lk_remote_participant_snapshot_info_t info;
		lk_remote_participant_snapshot_info_init(&info);
		status = lk_remote_participant_snapshot_info(participant, &info);
		lk_participant_permissions_t access{};
		if (status == LK_STATUS_OK)
			status = lk_remote_participant_snapshot_permissions(participant, &access);
		if (status != LK_STATUS_OK)
			break;
		const size_t attribute_count = lk_remote_participant_snapshot_attribute_count(participant);
		const size_t publication_count =
		    lk_remote_participant_snapshot_publication_count(participant);
		if (attribute_count > static_cast<size_t>(INT32_MAX) ||
		    publication_count > static_cast<size_t>(INT32_MAX)) {
			env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
			              "Too much participant metadata");
			break;
		}
		std::string sid = snapshot_string(env, participant, lk_remote_participant_snapshot_sid);
		std::string identity =
		    snapshot_string(env, participant, lk_remote_participant_snapshot_identity);
		std::string name = snapshot_string(env, participant, lk_remote_participant_snapshot_name);
		std::string metadata =
		    snapshot_string(env, participant, lk_remote_participant_snapshot_metadata);
		lk_participant_info_t who{
		    sid.c_str(),      identity.c_str(),        name.c_str(),     metadata.c_str(),
		    info.audio_level, info.connection_quality, info.is_speaking, 0};
		jobject attributes = snapshot_attribute_map(env, participant, attribute_count,
		                                            lk_remote_participant_snapshot_attribute_key,
		                                            lk_remote_participant_snapshot_attribute_value);
		jobjectArray publications =
		    env->NewObjectArray(static_cast<jsize>(publication_count), publication_class, nullptr);
		for (size_t j = 0; j < publication_count; ++j) {
			const lk_remote_track_publication_snapshot_t* publication = nullptr;
			status = lk_remote_participant_snapshot_publication_at(participant, j, &publication);
			if (status != LK_STATUS_OK)
				break;
			jobject item = publication_snapshot(env, identity, publication);
			if (item == nullptr)
				break;
			env->SetObjectArrayElement(publications, static_cast<jsize>(j), item);
			env->DeleteLocalRef(item);
		}
		if (status == LK_STATUS_OK && !env->ExceptionCheck()) {
			jobject details =
			    participant_details(env, handle(ptr), who, access, attributes, publications);
			env->SetObjectArrayElement(result, static_cast<jsize>(i), details);
			env->DeleteLocalRef(details);
		}
		env->DeleteLocalRef(attributes);
		env->DeleteLocalRef(publications);
		if (status != LK_STATUS_OK || env->ExceptionCheck())
			break;
	}
	env->DeleteLocalRef(details_class);
	env->DeleteLocalRef(publication_class);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
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
JNIEXPORT void JNICALL Java_io_livekit_client_Native_updateRemoteTrackSettings(
    JNIEnv* env, jclass, jlong ptr, jbyteArray participantSid, jbyteArray trackSid,
    jboolean enabled, jint videoQuality, jint width, jint height, jint fps, jint priority) {
	std::string p = bytes(env, participantSid), t = bytes(env, trackSid);
	lk_remote_track_settings_t settings;
	lk_remote_track_settings_init(&settings);
	settings.enabled = enabled ? 1 : 0;
	settings.has_video_quality = videoQuality >= 0 ? 1 : 0;
	if (settings.has_video_quality)
		settings.video_quality = static_cast<lk_video_quality_t>(videoQuality);
	settings.video_width = static_cast<uint32_t>(width);
	settings.video_height = static_cast<uint32_t>(height);
	settings.video_fps = static_cast<uint32_t>(fps);
	settings.priority = static_cast<uint32_t>(priority);
	fail(env,
	     lk_room_update_remote_track_settings(handle(ptr)->room, p.c_str(), t.c_str(), &settings));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setTrackSubscriptionPermissions(
    JNIEnv* env, jclass, jlong ptr, jboolean allowAll, jobjectArray permissions) {
	if (permissions == nullptr)
		return;
	jclass cls = env->FindClass("io/livekit/client/ParticipantTrackPermission");
	if (cls == nullptr)
		return;
	jmethodID get_sid = env->GetMethodID(cls, "participantSid", "()Ljava/lang/String;");
	if (get_sid == nullptr)
		return;
	jmethodID get_identity = env->GetMethodID(cls, "participantIdentity", "()Ljava/lang/String;");
	if (get_identity == nullptr)
		return;
	jmethodID get_allow_all = env->GetMethodID(cls, "allowAll", "()Z");
	if (get_allow_all == nullptr)
		return;
	jmethodID get_tracks = env->GetMethodID(cls, "allowedTrackSids", "()[Ljava/lang/String;");
	if (get_tracks == nullptr)
		return;
	struct OwnedPermission {
		std::string sid;
		std::string identity;
		bool allow_all = false;
		std::vector<std::string> tracks;
		std::vector<const char*> track_pointers;
	};
	const jsize count = env->GetArrayLength(permissions);
	std::vector<OwnedPermission> owned(static_cast<size_t>(count));
	for (jsize i = 0; i < count; ++i) {
		if (env->PushLocalFrame(32) != JNI_OK)
			return;
		jobject item = env->GetObjectArrayElement(permissions, i);
		bool invalid = item == nullptr;
		if (item != nullptr && !env->ExceptionCheck()) {
			jstring sid = static_cast<jstring>(env->CallObjectMethod(item, get_sid));
			jstring identity =
			    env->ExceptionCheck()
			        ? nullptr
			        : static_cast<jstring>(env->CallObjectMethod(item, get_identity));
			jboolean allowed =
			    env->ExceptionCheck() ? JNI_FALSE : env->CallBooleanMethod(item, get_allow_all);
			jobjectArray tracks =
			    env->ExceptionCheck()
			        ? nullptr
			        : static_cast<jobjectArray>(env->CallObjectMethod(item, get_tracks));
			invalid = tracks == nullptr;
			if (!env->ExceptionCheck() && tracks != nullptr) {
				auto& value = owned[static_cast<size_t>(i)];
				value.sid = java_string_bytes(env, sid);
				if (!env->ExceptionCheck())
					value.identity = java_string_bytes(env, identity);
				value.allow_all = allowed == JNI_TRUE;
				jsize track_count = env->ExceptionCheck() ? 0 : env->GetArrayLength(tracks);
				for (jsize j = 0; j < track_count && !env->ExceptionCheck(); ++j) {
					jstring track = static_cast<jstring>(env->GetObjectArrayElement(tracks, j));
					if (!env->ExceptionCheck())
						value.tracks.push_back(java_string_bytes(env, track));
					env->DeleteLocalRef(track);
				}
			}
		}
		bool failed = env->ExceptionCheck() || invalid;
		env->PopLocalFrame(nullptr);
		if (failed)
			return;
	}
	std::vector<lk_participant_track_permission_t> raw(static_cast<size_t>(count));
	for (jsize i = 0; i < count; ++i) {
		auto& value = owned[static_cast<size_t>(i)];
		for (const auto& track : value.tracks)
			value.track_pointers.push_back(track.c_str());
		auto& permission = raw[static_cast<size_t>(i)];
		lk_participant_track_permission_init(&permission);
		permission.participant_sid = value.sid.c_str();
		permission.participant_identity = value.identity.c_str();
		permission.allow_all = value.allow_all ? 1 : 0;
		permission.allowed_track_sids = value.track_pointers.data();
		permission.allowed_track_sid_count = value.track_pointers.size();
	}
	fail(env, lk_room_set_track_subscription_permissions(handle(ptr)->room, allowAll ? 1 : 0,
	                                                     raw.data(), raw.size()));
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
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_e2eeConfigured(JNIEnv*, jclass,
                                                                        jlong ptr) {
	return lk_room_e2ee_is_configured(handle(ptr)->room) ? JNI_TRUE : JNI_FALSE;
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
JNIEXPORT void JNICALL Java_io_livekit_client_Native_removeSharedKey(JNIEnv* env, jclass, jlong ptr,
                                                                     jint index) {
	fail(env, lk_room_e2ee_remove_shared_key(handle(ptr)->room, static_cast<size_t>(index)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setParticipantKey(JNIEnv* env, jclass,
                                                                       jlong ptr,
                                                                       jbyteArray identity,
                                                                       jbyteArray key, jint index) {
	std::string who = bytes(env, identity), secret = bytes(env, key);
	if (env->ExceptionCheck())
		return;
	fail(env, lk_room_e2ee_set_participant_key(handle(ptr)->room, who.c_str(),
	                                           reinterpret_cast<const uint8_t*>(secret.data()),
	                                           secret.size(), static_cast<size_t>(index)));
}
JNIEXPORT jbyteArray JNICALL Java_io_livekit_client_Native_exportParticipantKey(JNIEnv* env, jclass,
                                                                                jlong ptr,
                                                                                jbyteArray identity,
                                                                                jint index) {
	std::string who = bytes(env, identity);
	if (env->ExceptionCheck())
		return nullptr;
	lk_room_t* room = handle(ptr)->room;
	size_t n = lk_room_e2ee_export_participant_key(room, who.c_str(), static_cast<size_t>(index),
	                                               nullptr, 0);
	if (n > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "E2EE key is too large");
		return nullptr;
	}
	std::vector<uint8_t> data(n);
	if (n != 0)
		lk_room_e2ee_export_participant_key(room, who.c_str(), static_cast<size_t>(index),
		                                    data.data(), data.size());
	return array(env, data.data(), data.size());
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_ratchetParticipantKey(JNIEnv* env, jclass,
                                                                           jlong ptr,
                                                                           jbyteArray identity,
                                                                           jint index) {
	std::string who = bytes(env, identity);
	if (!env->ExceptionCheck())
		fail(env, lk_room_e2ee_ratchet_participant_key(handle(ptr)->room, who.c_str(),
		                                               static_cast<size_t>(index)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_removeParticipantKey(JNIEnv* env, jclass,
                                                                          jlong ptr,
                                                                          jbyteArray identity,
                                                                          jint index) {
	std::string who = bytes(env, identity);
	if (!env->ExceptionCheck())
		fail(env, lk_room_e2ee_remove_participant_key(handle(ptr)->room, who.c_str(),
		                                              static_cast<size_t>(index)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_removeParticipantKeys(JNIEnv* env, jclass,
                                                                           jlong ptr,
                                                                           jbyteArray identity) {
	std::string who = bytes(env, identity);
	if (!env->ExceptionCheck())
		fail(env, lk_room_e2ee_remove_participant_keys(handle(ptr)->room, who.c_str()));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_clearE2eeKeys(JNIEnv* env, jclass, jlong ptr) {
	fail(env, lk_room_e2ee_clear_keys(handle(ptr)->room));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_dataKeyIndex(JNIEnv*, jclass, jlong ptr) {
	return bounded_long(lk_room_e2ee_data_key_index(handle(ptr)->room));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setDataKeyIndex(JNIEnv* env, jclass, jlong ptr,
                                                                     jlong index) {
	fail(env, lk_room_e2ee_set_data_key_index(handle(ptr)->room, static_cast<size_t>(index)));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setFrameCryptorEnabled(
    JNIEnv* env, jclass, jlong ptr, jbyteArray trackId, jint direction, jboolean enabled) {
	std::string track = bytes(env, trackId);
	if (!env->ExceptionCheck())
		fail(env, lk_room_e2ee_set_frame_cryptor_enabled(
		              handle(ptr)->room, track.c_str(),
		              static_cast<lk_frame_cryptor_direction_t>(direction), enabled ? 1 : 0));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setFrameCryptorKeyIndex(
    JNIEnv* env, jclass, jlong ptr, jbyteArray trackId, jint direction, jlong index) {
	std::string track = bytes(env, trackId);
	if (!env->ExceptionCheck())
		fail(env,
		     lk_room_e2ee_set_frame_cryptor_key_index(
		         handle(ptr)->room, track.c_str(),
		         static_cast<lk_frame_cryptor_direction_t>(direction), static_cast<size_t>(index)));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_setParticipantE2eeEnabled(JNIEnv* env, jclass,
                                                                                jlong ptr,
                                                                                jbyteArray identity,
                                                                                jboolean enabled) {
	std::string who = bytes(env, identity);
	if (env->ExceptionCheck())
		return 0;
	size_t updated = 0;
	lk_status_t status = lk_room_e2ee_set_participant_enabled(handle(ptr)->room, who.c_str(),
	                                                          enabled ? 1 : 0, &updated);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return 0;
	}
	return bounded_long(updated);
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_frameCryptors(JNIEnv* env, jclass,
                                                                           jlong ptr) {
	lk_frame_cryptor_list_t* raw = nullptr;
	lk_status_t status = lk_frame_cryptor_list_create(handle(ptr)->room, &raw);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_frame_cryptor_list_t, decltype(&lk_frame_cryptor_list_destroy)> cryptors(
	    raw, lk_frame_cryptor_list_destroy);
	const size_t count = lk_frame_cryptor_list_count(cryptors.get());
	if (count > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Too many frame cryptors");
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/EncryptionState");
	if (cls == nullptr)
		return nullptr;
	jmethodID ctor =
	    env->GetMethodID(cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;IIZJI)V");
	if (ctor == nullptr)
		return nullptr;
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), cls, nullptr);
	for (size_t i = 0; result != nullptr && i < count && !env->ExceptionCheck(); ++i) {
		if (env->PushLocalFrame(16) != JNI_OK)
			return nullptr;
		lk_frame_cryptor_info_t info;
		lk_frame_cryptor_info_init(&info);
		status = lk_frame_cryptor_list_info(cryptors.get(), i, &info);
		if (status != LK_STATUS_OK) {
			env->PopLocalFrame(nullptr);
			fail(env, status);
			return nullptr;
		}
		jbyteArray raw_track = getter(env, [&](char* p, size_t n) {
			return lk_frame_cryptor_list_track_id(cryptors.get(), i, p, n);
		});
		jbyteArray raw_identity =
		    env->ExceptionCheck() ? nullptr : getter(env, [&](char* p, size_t n) {
			    return lk_frame_cryptor_list_participant_identity(cryptors.get(), i, p, n);
		    });
		std::string track = raw_track == nullptr ? "" : bytes(env, raw_track);
		std::string identity = raw_identity == nullptr ? "" : bytes(env, raw_identity);
		jstring track_id = env->ExceptionCheck() ? nullptr : string(env, track.c_str());
		jstring who = env->ExceptionCheck() ? nullptr : string(env, identity.c_str());
		jobject item =
		    env->ExceptionCheck()
		        ? nullptr
		        : env->NewObject(cls, ctor, track_id, who, static_cast<jint>(info.kind),
		                         static_cast<jint>(info.direction),
		                         info.enabled ? JNI_TRUE : JNI_FALSE, bounded_long(info.key_index),
		                         static_cast<jint>(info.state));
		if (item != nullptr && !env->ExceptionCheck())
			env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		env->PopLocalFrame(nullptr);
	}
	return env->ExceptionCheck() ? nullptr : result;
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
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_microphoneProcessingOptions(JNIEnv* env,
                                                                                    jclass,
                                                                                    jlong ptr) {
	lk_audio_source_options_t options;
	lk_audio_source_options_init(&options);
	auto status = lk_audio_source_microphone_processing_options(
	    reinterpret_cast<lk_audio_source_t*>(ptr), &options);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass cls = env->FindClass("io/livekit/client/MicrophoneProcessingOptions");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZZZ)V");
	jobject result = env->NewObject(cls, ctor, options.echo_cancellation ? JNI_TRUE : JNI_FALSE,
	                                options.auto_gain_control ? JNI_TRUE : JNI_FALSE,
	                                options.noise_suppression ? JNI_TRUE : JNI_FALSE);
	env->DeleteLocalRef(cls);
	return result;
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_setMicrophoneProcessingOptions(
    JNIEnv* env, jclass, jlong ptr, jboolean echoCancellation, jboolean autoGainControl,
    jboolean noiseSuppression) {
	lk_audio_source_options_t options;
	lk_audio_source_options_init(&options);
	options.echo_cancellation = echoCancellation == JNI_TRUE ? 1 : 0;
	options.auto_gain_control = autoGainControl == JNI_TRUE ? 1 : 0;
	options.noise_suppression = noiseSuppression == JNI_TRUE ? 1 : 0;
	fail(env, lk_audio_source_microphone_set_processing_options(
	              reinterpret_cast<lk_audio_source_t*>(ptr), &options));
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_microphoneProcessingStats(JNIEnv* env,
                                                                                  jclass,
                                                                                  jlong ptr) {
	lk_microphone_processing_stats_t stats;
	lk_microphone_processing_stats_init(&stats);
	auto status = lk_audio_source_microphone_processing_stats(
	    reinterpret_cast<lk_audio_source_t*>(ptr), &stats);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	jclass double_class = env->FindClass("java/lang/Double");
	jclass integer_class = env->FindClass("java/lang/Integer");
	jclass cls = env->FindClass("io/livekit/client/MicrophoneProcessingStats");
	if (double_class == nullptr || integer_class == nullptr || cls == nullptr)
		return nullptr;
	jmethodID double_ctor = env->GetMethodID(double_class, "<init>", "(D)V");
	jmethodID integer_ctor = env->GetMethodID(integer_class, "<init>", "(I)V");
	jmethodID ctor = env->GetMethodID(
	    cls, "<init>",
	    "(JJJJJZLjava/lang/Double;Ljava/lang/Double;Ljava/lang/Double;"
	    "Ljava/lang/Double;Ljava/lang/Integer;Ljava/lang/Integer;Ljava/lang/Integer;)V");
	if (double_ctor == nullptr || integer_ctor == nullptr || ctor == nullptr)
		return nullptr;
	auto optional_double = [env, double_class, double_ctor](int available,
	                                                        double value) -> jobject {
		return available ? env->NewObject(double_class, double_ctor, static_cast<jdouble>(value))
		                 : nullptr;
	};
	auto optional_int = [env, integer_class, integer_ctor](int available, int value) -> jobject {
		return available ? env->NewObject(integer_class, integer_ctor, static_cast<jint>(value))
		                 : nullptr;
	};
	jobject echo_loss =
	    optional_double(stats.echo_return_loss_available, stats.echo_return_loss_db);
	jobject echo_enhancement = optional_double(stats.echo_return_loss_enhancement_available,
	                                           stats.echo_return_loss_enhancement_db);
	jobject echo_likelihood =
	    optional_double(stats.residual_echo_likelihood_available, stats.residual_echo_likelihood);
	jobject echo_recent_max = optional_double(stats.residual_echo_likelihood_recent_max_available,
	                                          stats.residual_echo_likelihood_recent_max);
	jobject delay_median = optional_int(stats.delay_median_available, stats.delay_median_ms);
	jobject delay_deviation =
	    optional_int(stats.delay_standard_deviation_available, stats.delay_standard_deviation_ms);
	jobject delay = optional_int(stats.delay_available, stats.delay_ms);
	jobject result =
	    env->ExceptionCheck()
	        ? nullptr
	        : env->NewObject(cls, ctor, static_cast<jlong>(stats.capture_frames_processed),
	                         static_cast<jlong>(stats.render_frames_processed),
	                         static_cast<jlong>(stats.capture_processing_errors),
	                         static_cast<jlong>(stats.render_processing_errors),
	                         static_cast<jlong>(stats.frames_dropped),
	                         stats.echo_cancellation_enabled ? JNI_TRUE : JNI_FALSE, echo_loss,
	                         echo_enhancement, echo_likelihood, echo_recent_max, delay_median,
	                         delay_deviation, delay);
	if (echo_loss != nullptr)
		env->DeleteLocalRef(echo_loss);
	if (echo_enhancement != nullptr)
		env->DeleteLocalRef(echo_enhancement);
	if (echo_likelihood != nullptr)
		env->DeleteLocalRef(echo_likelihood);
	if (echo_recent_max != nullptr)
		env->DeleteLocalRef(echo_recent_max);
	if (delay_median != nullptr)
		env->DeleteLocalRef(delay_median);
	if (delay_deviation != nullptr)
		env->DeleteLocalRef(delay_deviation);
	if (delay != nullptr)
		env->DeleteLocalRef(delay);
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(integer_class);
	env->DeleteLocalRef(double_class);
	return result;
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
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_localRtcStats(JNIEnv* env, jclass,
                                                                           jlong ptr) {
	lk_rtc_stats_snapshot_t* raw = nullptr;
	auto status =
	    lk_local_track_create_rtc_stats_snapshot(reinterpret_cast<lk_local_track_t*>(ptr), &raw);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_rtc_stats_snapshot_t, decltype(&lk_rtc_stats_snapshot_destroy)> snapshot(
	    raw, lk_rtc_stats_snapshot_destroy);
	return rtc_stats_array(env, snapshot.get());
}
JNIEXPORT jobjectArray JNICALL Java_io_livekit_client_Native_remoteRtcStats(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray trackSid) {
	const std::string identity = bytes(env, participantIdentity);
	const std::string sid = bytes(env, trackSid);
	lk_remote_participant_list_t* raw = nullptr;
	auto status = lk_room_create_remote_participant_snapshot(handle(room)->room, &raw);
	if (status != LK_STATUS_OK) {
		fail(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_remote_participant_list_t, decltype(&lk_remote_participant_list_destroy)>
	    participants(raw, lk_remote_participant_list_destroy);
	for (size_t i = 0; i < lk_remote_participant_list_count(participants.get()); ++i) {
		const lk_remote_participant_snapshot_t* participant = nullptr;
		status = lk_remote_participant_list_at(participants.get(), i, &participant);
		if (status != LK_STATUS_OK) {
			fail(env, status);
			return nullptr;
		}
		if (snapshot_string(env, participant, lk_remote_participant_snapshot_identity) != identity)
			continue;
		for (size_t j = 0; j < lk_remote_participant_snapshot_publication_count(participant); ++j) {
			const lk_remote_track_publication_snapshot_t* publication = nullptr;
			status = lk_remote_participant_snapshot_publication_at(participant, j, &publication);
			if (status != LK_STATUS_OK) {
				fail(env, status);
				return nullptr;
			}
			if (snapshot_string(env, publication, lk_remote_track_publication_snapshot_sid) != sid)
				continue;
			const lk_remote_track_snapshot_t* track = nullptr;
			status = lk_remote_track_publication_snapshot_track(publication, &track);
			if (status != LK_STATUS_OK) {
				fail(env, status);
				return nullptr;
			}
			lk_rtc_stats_snapshot_t* stats_raw = nullptr;
			status = lk_remote_track_snapshot_create_rtc_stats_snapshot(track, &stats_raw);
			if (status != LK_STATUS_OK) {
				fail(env, status);
				return nullptr;
			}
			std::unique_ptr<lk_rtc_stats_snapshot_t, decltype(&lk_rtc_stats_snapshot_destroy)>
			    stats(stats_raw, lk_rtc_stats_snapshot_destroy);
			return rtc_stats_array(env, stats.get());
		}
		break;
	}
	env->ThrowNew(env->FindClass("java/util/NoSuchElementException"), "Remote track was not found");
	return nullptr;
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
		fail_data_track(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(track);
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_publishDataTrackWithOptions(
    JNIEnv* env, jclass, jlong room, jbyteArray name, jint frameEncoding,
    jbyteArray customFrameEncoding, jbyteArray schemaName, jint schemaEncoding,
    jbyteArray customSchemaEncoding) {
	std::string name_value = bytes(env, name), frame_custom = bytes(env, customFrameEncoding);
	std::string schema_name = bytes(env, schemaName);
	std::string schema_custom = bytes(env, customSchemaEncoding);
	lk_data_track_publish_options_t options;
	lk_data_track_publish_options_init(&options);
	options.name = name_value.c_str();
	options.has_frame_encoding = frameEncoding != LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED;
	options.frame_encoding = static_cast<lk_data_track_frame_encoding_t>(frameEncoding);
	if (!frame_custom.empty())
		options.custom_frame_encoding = frame_custom.c_str();
	options.has_schema = !schema_name.empty();
	if (options.has_schema) {
		options.schema.name = schema_name.c_str();
		options.schema.encoding = static_cast<lk_data_track_schema_encoding_t>(schemaEncoding);
		if (!schema_custom.empty())
			options.schema.custom_encoding = schema_custom.c_str();
	}
	lk_local_data_track_t* track = nullptr;
	auto status = lk_room_publish_data_track(handle(room)->room, &options, &track);
	if (status != LK_DATA_TRACK_ERROR_NONE) {
		fail_data_track(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(track);
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_storeDataTrackSchema(
    JNIEnv* env, jclass, jlong room, jbyteArray name, jint encoding, jbyteArray customEncoding,
    jbyteArray definition) {
	std::string schema_name = bytes(env, name), custom = bytes(env, customEncoding);
	std::string payload = bytes(env, definition);
	lk_data_track_schema_id_t id;
	lk_data_track_schema_id_init(&id);
	id.name = schema_name.c_str();
	id.encoding = static_cast<lk_data_track_schema_encoding_t>(encoding);
	if (!custom.empty())
		id.custom_encoding = custom.c_str();
	auto status = lk_room_store_data_track_schema(
	    handle(room)->room, &id, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
	fail_data_track(env, status);
}
JNIEXPORT jobject JNICALL Java_io_livekit_client_Native_getDataTrackSchema(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray name, jint encoding,
    jbyteArray customEncoding) {
	std::string identity = bytes(env, participantIdentity), schema_name = bytes(env, name);
	std::string custom = bytes(env, customEncoding);
	lk_data_track_schema_id_t id;
	lk_data_track_schema_id_init(&id);
	id.name = schema_name.c_str();
	id.encoding = static_cast<lk_data_track_schema_encoding_t>(encoding);
	if (!custom.empty())
		id.custom_encoding = custom.c_str();
	lk_data_track_schema_t* raw = nullptr;
	auto status = lk_room_get_data_track_schema(handle(room)->room, identity.c_str(), &id, &raw);
	if (status != LK_DATA_TRACK_ERROR_NONE) {
		fail_data_track(env, status);
		return nullptr;
	}
	std::unique_ptr<lk_data_track_schema_t, decltype(&lk_data_track_schema_destroy)> schema(
	    raw, lk_data_track_schema_destroy);
	const size_t size = lk_data_track_schema_definition(schema.get(), nullptr, 0);
	if (size > static_cast<size_t>(INT32_MAX)) {
		env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Schema is too large");
		return nullptr;
	}
	std::vector<uint8_t> definition(size);
	if (size != 0)
		lk_data_track_schema_definition(schema.get(), definition.data(), size);
	jbyteArray data = array(env, definition.data(), size);
	auto read = [env, &schema](auto fn) {
		jbyteArray raw_value =
		    getter(env, [&schema, fn](char* p, size_t n) { return fn(schema.get(), p, n); });
		std::string value = bytes(env, raw_value);
		env->DeleteLocalRef(raw_value);
		return string(env, value.c_str());
	};
	jstring result_name = read(lk_data_track_schema_name);
	jstring result_custom = read(lk_data_track_schema_custom_encoding);
	jclass cls = env->FindClass("io/livekit/client/DataTrackSchema");
	jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;ILjava/lang/String;[B)V");
	jobject result = env->NewObject(cls, ctor, result_name,
	                                static_cast<jint>(lk_data_track_schema_encoding(schema.get())),
	                                result_custom, data);
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(result_name);
	env->DeleteLocalRef(result_custom);
	env->DeleteLocalRef(data);
	return result;
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
	fail_data_track(env, status);
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
		fail_data_track(env, lk_local_data_track_unpublish(track));
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_destroyDataTrack(JNIEnv* env, jclass,
                                                                      jlong ptr) {
	auto* track = reinterpret_cast<lk_local_data_track_t*>(ptr);
	if (lk_local_data_track_is_published(track)) {
		auto status = lk_local_data_track_unpublish(track);
		if (status != LK_DATA_TRACK_ERROR_NONE && status != LK_DATA_TRACK_ERROR_DISCONNECTED) {
			fail_data_track(env, status);
			return;
		}
	}
	fail_data_track(env, lk_local_data_track_destroy(track));
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_subscribeDataTrack(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray trackSid,
    jint targetFps, jint bufferCapacity, jint maxPartialFrames) {
	std::string identity = bytes(env, participantIdentity);
	std::string sid = bytes(env, trackSid);
	lk_data_track_subscription_options_t options;
	lk_data_track_subscription_options_init(&options);
	options.has_target_fps = targetFps > 0;
	options.target_fps = static_cast<uint32_t>(targetFps);
	options.buffer_capacity = static_cast<size_t>(bufferCapacity);
	options.max_partial_frames = static_cast<size_t>(maxPartialFrames);
	lk_data_track_reader_t* reader = nullptr;
	auto status = lk_room_subscribe_data_track(handle(room)->room, identity.c_str(), sid.c_str(),
	                                           &options, &reader);
	if (status != LK_DATA_TRACK_ERROR_NONE) {
		fail_data_track(env, status);
		return 0;
	}
	return reinterpret_cast<jlong>(reader);
}
JNIEXPORT void JNICALL Java_io_livekit_client_Native_updateDataTrackSubscription(
    JNIEnv* env, jclass, jlong room, jbyteArray participantIdentity, jbyteArray trackSid,
    jint targetFps, jint bufferCapacity, jint maxPartialFrames) {
	std::string identity = bytes(env, participantIdentity);
	std::string sid = bytes(env, trackSid);
	lk_data_track_subscription_options_t options;
	lk_data_track_subscription_options_init(&options);
	options.has_target_fps = targetFps > 0;
	options.target_fps = static_cast<uint32_t>(targetFps);
	options.buffer_capacity = static_cast<size_t>(bufferCapacity);
	options.max_partial_frames = static_cast<size_t>(maxPartialFrames);
	auto status = lk_room_update_data_track_subscription_options(
	    handle(room)->room, identity.c_str(), sid.c_str(), &options);
	if (status != LK_DATA_TRACK_ERROR_NONE)
		fail_data_track(env, status);
}
JNIEXPORT jboolean JNICALL Java_io_livekit_client_Native_dataTrackReaderClosed(JNIEnv*, jclass,
                                                                               jlong reader) {
	return lk_data_track_reader_is_closed(reinterpret_cast<lk_data_track_reader_t*>(reader))
	           ? JNI_TRUE
	           : JNI_FALSE;
}
JNIEXPORT jlong JNICALL Java_io_livekit_client_Native_dataTrackReaderDroppedFrames(JNIEnv*, jclass,
                                                                                   jlong reader) {
	return static_cast<jlong>(
	    lk_data_track_reader_dropped_frames(reinterpret_cast<lk_data_track_reader_t*>(reader)));
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
	jmethodID ctor =
	    env->GetMethodID(cls, "<init>",
	                     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/"
	                     "String;Ljava/lang/String;ILjava/lang/String;Z)V");
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
		jstring custom_frame_encoding = read(lk_remote_data_track_snapshot_custom_frame_encoding);
		jstring schema_name = read(lk_remote_data_track_snapshot_schema_name);
		jstring custom_schema_encoding = read(lk_remote_data_track_snapshot_custom_schema_encoding);
		jobject item =
		    env->NewObject(cls, ctor, identity, sid, name,
		                   info.has_frame_encoding ? static_cast<jint>(info.frame_encoding) : 0,
		                   custom_frame_encoding, schema_name,
		                   info.has_schema ? static_cast<jint>(info.schema_encoding) : 0,
		                   custom_schema_encoding, info.is_published ? JNI_TRUE : JNI_FALSE);
		env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(identity);
		env->DeleteLocalRef(sid);
		env->DeleteLocalRef(name);
		env->DeleteLocalRef(custom_frame_encoding);
		env->DeleteLocalRef(schema_name);
		env->DeleteLocalRef(custom_schema_encoding);
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
		fail_data_track(env, LK_DATA_TRACK_ERROR_INVALID_ARGUMENT);
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
JNIEXPORT jobjectArray JNICALL
Java_io_livekit_client_Native_remoteTrackPublicationDetails(JNIEnv* env, jclass, jlong room) {
	lk_remote_participant_list_t* raw = nullptr;
	lk_status_t status = lk_room_create_remote_participant_snapshot(handle(room)->room, &raw);
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
		if (publication_count > static_cast<size_t>(INT32_MAX)) {
			env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
			              "Too many remote track publications");
			return nullptr;
		}
	}
	jclass cls = env->FindClass("io/livekit/client/RemoteTrackPublicationDetails");
	jobjectArray result = env->NewObjectArray(static_cast<jsize>(publication_count), cls, nullptr);
	jsize output_index = 0;
	for (size_t i = 0; i < participant_count; ++i) {
		const lk_remote_participant_snapshot_t* participant = nullptr;
		status = lk_remote_participant_list_at(snapshot.get(), i, &participant);
		if (status != LK_STATUS_OK)
			break;
		std::string identity =
		    snapshot_string(env, participant, lk_remote_participant_snapshot_identity);
		const size_t count = lk_remote_participant_snapshot_publication_count(participant);
		for (size_t j = 0; j < count; ++j) {
			const lk_remote_track_publication_snapshot_t* publication = nullptr;
			status = lk_remote_participant_snapshot_publication_at(participant, j, &publication);
			if (status != LK_STATUS_OK)
				break;
			jobject item = publication_details(env, identity, publication);
			if (item == nullptr)
				break;
			env->SetObjectArrayElement(result, output_index++, item);
			env->DeleteLocalRef(item);
		}
		if (status != LK_STATUS_OK || env->ExceptionCheck())
			break;
	}
	env->DeleteLocalRef(cls);
	if (status != LK_STATUS_OK)
		fail(env, status);
	return env->ExceptionCheck() ? nullptr : result;
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
