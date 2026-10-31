template <typename T>
T Options(napi_env env, napi_value args, Arena& arena, const char* key = "options") {
	napi_value input;
	if (Has(env, args, key))
		input = Get(env, args, key);
	else
		Napi(napi_create_object(env, &input));
	T out{};
	Read(env, input, out, arena);
	return out;
}
template <typename Getter> Bytes BufferValue(Getter getter) {
	Bytes bytes(getter(nullptr, 0));
	if (bytes.empty())
		CheckLastError();
	if (!bytes.empty()) {
		const auto required = getter(bytes.data(), bytes.size());
		if (!required)
			CheckLastError();
		if (required > bytes.size()) {
			bytes.resize(required);
			getter(bytes.data(), bytes.size());
		}
	}
	return bytes;
}
void DataCheck(lk_data_track_error_code_t code) {
	if (code != LK_DATA_TRACK_ERROR_NONE)
		Check(LK_STATUS_OPERATION_FAILED);
}
Value ConnectRoom(napi_env env, Room& state, napi_value args, bool token_source) {
	Arena arena;
	auto options = Options<lk_room_connect_options_t>(env, args, arena);
	if (Bool(env, args, "useReconnectPolicy", false)) {
		if (!state.channel)
			throw Error("Reconnect policy requires an application callback channel");
		options.reconnect_policy = ReconnectPolicy;
		options.reconnect_policy_user_data = &state;
	}
	if (token_source) {
		if (!state.channel)
			throw Error("Token source requires an application callback channel");
		auto fetch = Options<lk_token_source_fetch_options_t>(env, args, arena, "fetchOptions");
		Check(lk_room_connect_with_token_source_and_options(state.room, TokenSource, &state, &fetch,
		                                                    &options));
	} else {
		const auto url = Str(env, args, "url"), token = Str(env, args, "token");
		Check(lk_room_connect_with_options(state.room, url.c_str(), token.c_str(), &options));
	}
	return {};
}
Value Crypto(napi_env env, Room& state, const std::string& op, napi_value args) {
	auto* r = state.room;
	const auto index = Num(env, args, "keyIndex", 0);
	const auto identity = Str(env, args, "participantIdentity");
	if (op == "e2eeStatus")
		return Object{{"configured", bool(lk_room_e2ee_is_configured(r))},
		              {"enabled", bool(lk_room_e2ee_is_enabled(r))},
		              {"dataKeyIndex", double(lk_room_e2ee_data_key_index(r))}};
	if (op == "e2eeEnable")
		Check(lk_room_e2ee_set_enabled(r, Bool(env, args, "enabled", true)));
	else if (op == "e2eeSetSharedKey" || op == "e2eeSetParticipantKey") {
		const auto key = Data(env, args, "key");
		if (op == "e2eeSetSharedKey")
			Check(lk_room_e2ee_set_shared_key(r, key.data(), key.size(), index));
		else
			Check(lk_room_e2ee_set_participant_key(r, identity.c_str(), key.data(), key.size(),
			                                       index));
	} else if (op == "e2eeExportSharedKey")
		return BufferValue(
		    [&](uint8_t* b, size_t n) { return lk_room_e2ee_export_shared_key(r, index, b, n); });
	else if (op == "e2eeExportParticipantKey")
		return BufferValue([&](uint8_t* b, size_t n) {
			return lk_room_e2ee_export_participant_key(r, identity.c_str(), index, b, n);
		});
	else if (op == "e2eeRatchetSharedKey")
		Check(lk_room_e2ee_ratchet_shared_key(r, index));
	else if (op == "e2eeRatchetParticipantKey")
		Check(lk_room_e2ee_ratchet_participant_key(r, identity.c_str(), index));
	else if (op == "e2eeRemoveSharedKey")
		Check(lk_room_e2ee_remove_shared_key(r, index));
	else if (op == "e2eeRemoveParticipantKey")
		Check(lk_room_e2ee_remove_participant_key(r, identity.c_str(), index));
	else if (op == "e2eeRemoveParticipantKeys")
		Check(lk_room_e2ee_remove_participant_keys(r, identity.c_str()));
	else if (op == "e2eeClearKeys")
		Check(lk_room_e2ee_clear_keys(r));
	else if (op == "e2eeDataKeyIndex")
		Check(lk_room_e2ee_set_data_key_index(r, index));
	else if (op == "e2eeCryptorEnabled" || op == "e2eeCryptorKeyIndex") {
		const auto track = Str(env, args, "trackId");
		const auto direction =
		    static_cast<lk_frame_cryptor_direction_t>(Num(env, args, "direction", 0));
		if (op == "e2eeCryptorEnabled")
			Check(lk_room_e2ee_set_frame_cryptor_enabled(r, track.c_str(), direction,
			                                             Bool(env, args, "enabled", true)));
		else
			Check(lk_room_e2ee_set_frame_cryptor_key_index(r, track.c_str(), direction, index));
	} else if (op == "e2eeParticipantEnabled") {
		size_t updated = 0;
		Check(lk_room_e2ee_set_participant_enabled(r, identity.c_str(),
		                                           Bool(env, args, "enabled", true), &updated));
		return double(updated);
	} else if (op == "e2eeCryptors") {
		lk_frame_cryptor_list_t* raw = nullptr;
		Check(lk_frame_cryptor_list_create(r, &raw));
		Owned<lk_frame_cryptor_list_t, lk_frame_cryptor_list_destroy> list(raw);
		Array values;
		for (size_t i = 0; i < lk_frame_cryptor_list_count(raw); ++i) {
			lk_frame_cryptor_info_t info{};
			lk_frame_cryptor_info_init(&info);
			Check(lk_frame_cryptor_list_info(raw, i, &info));
			auto object = std::get<Object>(Pack(info).data);
			object["trackId"] = Text(
			    [&](char* b, size_t n) { return lk_frame_cryptor_list_track_id(raw, i, b, n); });
			object["participantIdentity"] = Text([&](char* b, size_t n) {
				return lk_frame_cryptor_list_participant_identity(raw, i, b, n);
			});
			values.push_back(std::move(object));
		}
		return values;
	} else
		throw Error("Unknown E2EE operation");
	return {};
}
Value MediaOperation(napi_env env, Room& state, const std::string& op, napi_value args) {
	Arena arena;
	auto* r = state.room;
	if (op == "createCapture") {
		auto media = std::make_unique<Media>();
		media->name = Str(env, args, "name", "node-capture");
		const auto kind = Str(env, args, "kind");
		if (kind == "microphone") {
			auto o = Options<lk_microphone_capture_options_t>(env, args, arena);
			Check(lk_audio_source_create_microphone(&o, &media->audio));
		} else if (kind == "systemAudio") {
			auto o = Options<lk_system_audio_capture_options_t>(env, args, arena);
			Check(lk_audio_source_create_system_audio(&o, &media->audio));
		} else if (kind == "camera") {
			auto o = Options<lk_camera_capture_options_t>(env, args, arena);
			Check(lk_video_source_create_camera(&o, &media->video));
		} else if (kind == "screen") {
			auto o = Options<lk_screen_capture_options_t>(env, args, arena);
			Check(lk_video_source_create_screen(&o, &media->video));
		} else
			throw Error("Unknown capture kind");
		media->capture_kind = kind;
		const auto id = state.Id();
		state.media.emplace(id, std::move(media));
		return id;
	}
	if (op == "screenSources") {
		lk_screen_source_list_t* raw = nullptr;
		Check(lk_screen_source_list_create(&raw));
		Owned<lk_screen_source_list_t, lk_screen_source_list_destroy> list(raw);
		Array values;
		for (size_t i = 0; i < lk_screen_source_list_count(raw); ++i) {
			lk_screen_source_info_t info{};
			info.struct_size = sizeof(info);
			Check(lk_screen_source_list_info(raw, i, &info));
			auto object = std::get<Object>(Pack(info).data);
			object["id"] =
			    Text([&](char* b, size_t n) { return lk_screen_source_list_id(raw, i, b, n); });
			object["label"] =
			    Text([&](char* b, size_t n) { return lk_screen_source_list_label(raw, i, b, n); });
			values.push_back(std::move(object));
		}
		return values;
	}
	if (op == "speakerStatus") {
		lk_audio_playback_stats_t stats{};
		lk_audio_playback_stats_init(&stats);
		Check(lk_room_audio_playback_stats(r, &stats));
		return Object{{"volume", double(lk_room_speaker_volume(r))},
		              {"muted", bool(lk_room_speaker_is_muted(r))},
		              {"deviceId", Text([&](char* b, size_t n) {
			               return lk_room_audio_output_device(r, b, n);
		               })},
		              {"stats", Pack(stats)}};
	}
	if (op == "speakerVolume") {
		Check(lk_room_set_speaker_volume(r, ReadNumber<float>(env, Get(env, args, "volume"))));
		return {};
	}
	if (op == "speakerMuted") {
		Check(lk_room_set_speaker_muted(r, Bool(env, args, "muted", false)));
		return {};
	}
	if (op == "speakerDevice") {
		Check(lk_room_set_audio_output_device(r, Str(env, args, "deviceId").c_str()));
		return {};
	}
	const auto it = state.media.find(Num(env, args, "id", 0));
	if (it == state.media.end())
		throw Error("Local track is closed or belongs to another room", 1, 2);
	auto& m = *it->second;
	if (op == "trackRelease") {
		if (m.track) {
			Check(lk_local_track_destroy(m.track));
			m.track = nullptr;
		}
		state.media.erase(it);
		return {};
	}
	if (op == "captureFrame") {
		if (!m.video)
			throw Error("Not a video source");
		auto input = Options<lk_video_frame_input_t>(env, args, arena);
		Check(lk_video_source_capture_frame(m.video, &input));
	} else if (op == "audioQueuedDuration") {
		uint32_t duration = 0;
		Check(lk_audio_source_queued_duration_ms(m.audio, &duration));
		return duration;
	} else if (op == "audioClearQueue")
		Check(lk_audio_source_clear_queue(m.audio));
	else if (op == "audioWaitForPlayout")
		Check(lk_audio_source_wait_for_playout(m.audio, Num(env, args, "timeoutMs", 1000)));
	else if (op == "videoDimensions") {
		uint32_t width = 0, height = 0;
		Check(lk_video_source_dimensions(m.video, &width, &height));
		return Object{{"width", width}, {"height", height}};
	} else if (op == "trackStats") {
		lk_rtc_stats_snapshot_t* raw = nullptr;
		Check(lk_local_track_create_rtc_stats_snapshot(m.track, &raw));
		return StatsSnapshot(raw);
	} else if (op == "trackStatsJson")
		return Text([&](char* b, size_t n) { return lk_local_track_rtc_stats(m.track, b, n); });
	else if (op == "trackEncoding") {
		auto o = Options<lk_video_encoding_t>(env, args, arena);
		Check(lk_local_video_track_update_encoding(m.track, &o,
		                                           Bool(env, args, "backupCodec", false)));
	} else if (op == "trackDegradation")
		Check(lk_local_video_track_update_degradation_preference(
		    m.track,
		    static_cast<lk_video_degradation_preference_t>(Num(env, args, "preference", 0))));
	else if (op == "captureStart" || op == "captureStop" || op == "captureSwitch" ||
	         op == "captureStatus") {
		const auto device = Str(env, args, "deviceId");
#define CAPTURE_CASE(kind, prefix, handle, getter)                                                 \
	if (m.capture_kind == kind) {                                                                  \
		if (op == "captureStart")                                                                  \
			Check(prefix##_start(handle));                                                         \
		else if (op == "captureStop")                                                              \
			Check(prefix##_stop(handle));                                                          \
		else if (op == "captureSwitch")                                                            \
			Check(prefix##_switch_##getter(handle, device.c_str()));                               \
		else                                                                                       \
			return Object{{"capturing", bool(prefix##_is_capturing(handle))},                      \
			              {"deviceId", Text([&](char* b, size_t n) {                               \
				               return prefix##_##getter##_id(handle, b, n);                        \
			               })}};                                                                   \
	}
		CAPTURE_CASE("microphone", lk_audio_source_microphone, m.audio, device)
		else CAPTURE_CASE(
		    "systemAudio", lk_audio_source_system_audio, m.audio,
		    device) else CAPTURE_CASE("camera", lk_video_source_camera, m.video,
		                              device) else CAPTURE_CASE("screen", lk_video_source_screen,
		                                                        m.video,
		                                                        source) else throw Error("This "
		                                                                                 "track is "
		                                                                                 "an "
		                                                                                 "external "
		                                                                                 "source");
#undef CAPTURE_CASE
	} else if (op == "microphoneMuted")
		Check(lk_audio_source_microphone_set_muted(m.audio, Bool(env, args, "muted", false)));
	else if (op == "microphoneVolume")
		Check(lk_audio_source_microphone_set_volume(
		    m.audio, ReadNumber<float>(env, Get(env, args, "volume"))));
	else if (op == "microphoneProcessing") {
		auto o = Options<lk_audio_source_options_t>(env, args, arena);
		Check(lk_audio_source_microphone_set_processing_options(m.audio, &o));
	} else if (op == "microphoneStatus") {
		lk_audio_source_options_t options{};
		lk_audio_source_options_init(&options);
		Check(lk_audio_source_microphone_processing_options(m.audio, &options));
		lk_microphone_processing_stats_t stats{};
		lk_microphone_processing_stats_init(&stats);
		Check(lk_audio_source_microphone_processing_stats(m.audio, &stats));
		return Object{{"muted", bool(lk_audio_source_microphone_is_muted(m.audio))},
		              {"volume", double(lk_audio_source_microphone_volume(m.audio))},
		              {"options", Pack(options)},
		              {"stats", Pack(stats)}};
	} else
		throw Error("Unknown media operation");
	return {};
}
#include "datatracks.inc"
#include "streams.inc"
Value Advanced(napi_env env, Room& state, napi_value args) {
	Arena arena;
	const auto op = Str(env, args, "op");
	auto* r = state.room;
	if (op.starts_with("e2ee"))
		return Crypto(env, state, op, args);
	if (op.starts_with("writer") || op.starts_with("stream") || op.starts_with("reader") ||
	    op.starts_with("recorder"))
		return Streams(env, state, op, args);
	if (op.starts_with("dataTrack") || op.starts_with("dataReader"))
		return DataTracks(env, state, op, args);
	if (op.starts_with("capture") || op.starts_with("audio") || op.starts_with("video") ||
	    op.starts_with("track") || op.starts_with("microphone") || op.starts_with("speaker") ||
	    op == "createCapture" || op == "screenSources")
		return MediaOperation(env, state, op, args);
	if (op == "connectTokenSource")
		return ConnectRoom(env, state, args, true);
	if (op == "registerRpc") {
		if (!state.channel)
			throw Error("RPC handlers require an application callback channel");
		auto context = std::make_unique<RpcContext>(RpcContext{&state, Str(env, args, "method")});
		Check(
		    lk_room_register_rpc_method_out(r, context->method.c_str(), RpcHandler, context.get()));
		state.rpc_contexts.push_back(std::move(context));
	} else if (op == "unregisterRpc")
		Check(lk_room_unregister_rpc_method(r, Str(env, args, "method").c_str()));
	else if (op == "remoteTrackSettings") {
		auto o = Options<lk_remote_track_settings_t>(env, args, arena);
		Check(lk_room_update_remote_track_settings(r, Str(env, args, "participantSid").c_str(),
		                                           Str(env, args, "trackSid").c_str(), &o));
	} else if (op == "subscriptionPermissions") {
		size_t count = 0;
		auto* permissions = arena.ArrayOf<lk_participant_track_permission_t>(
		    env, Get(env, args, "permissions"), count);
		Check(lk_room_set_track_subscription_permissions(
		    r, Bool(env, args, "allParticipantsAllowed", true), permissions, count));
	} else if (op == "republishTracks")
		Check(lk_room_republish_all_tracks(r));
	else if (op == "sendChat") {
		char id[LK_CHAT_MESSAGE_ID_BUFFER_SIZE]{};
		int64_t timestamp = 0;
		Check(lk_room_send_chat_message(r, Str(env, args, "message").c_str(), id, sizeof(id),
		                                &timestamp));
		return Object{{"id", id}, {"timestamp", timestamp}};
	} else if (op == "editChat")
		Check(lk_room_edit_chat_message(r, Str(env, args, "messageId").c_str(),
		                                ReadNumber<int64_t>(env, Get(env, args, "timestamp")),
		                                Str(env, args, "message").c_str()));
	else if (op == "publishDtmf")
		Check(lk_room_publish_dtmf(r, Num(env, args, "code", 0), Str(env, args, "digit").c_str()));
	else if (op == "diagnosticsSubscribe") {
		std::lock_guard config(diagnostics_config_mutex);
		{
			std::lock_guard lock(diagnostics_sinks_mutex);
			diagnostics_sinks[&state] = {Bool(env, args, "logs", false),
			                             Bool(env, args, "traces", false)};
		}
		UpdateDiagnosticsLocked();
	} else if (op == "logOptions") {
		auto o = Options<lk_log_options_t>(env, args, arena);
		Check(lk_log_set_options(&o));
	} else if (op == "getLogOptions") {
		lk_log_options_t o{};
		lk_log_options_init(&o);
		Check(lk_log_get_options(&o));
		return Pack(o);
	} else if (op == "traceOptions") {
		auto o = Options<lk_trace_options_t>(env, args, arena);
		Check(lk_trace_set_options(&o));
	} else if (op == "getTraceOptions") {
		lk_trace_options_t o{};
		lk_trace_options_init(&o);
		Check(lk_trace_get_options(&o));
		return Pack(o);
	} else if (op == "startTraceFile") {
		std::lock_guard config(diagnostics_config_mutex);
		Check(lk_trace_start_json_file(Str(env, args, "path").c_str()));
		trace_installed = false;
		trace_file_owner = &state;
	} else if (op == "stopTrace") {
		std::lock_guard config(diagnostics_config_mutex);
		Check(lk_trace_stop());
		trace_installed = false;
		trace_file_owner = nullptr;
	} else
		throw Error("Unknown operation");
	return {};
}
