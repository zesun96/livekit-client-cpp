Value WriterInfo(lk_data_stream_writer_info_snapshot_t* raw) {
	Owned<lk_data_stream_writer_info_snapshot_t, lk_data_stream_writer_info_snapshot_destroy> owned(
	    raw);
	lk_data_stream_writer_info_t info{};
	lk_data_stream_writer_info_init(&info);
	Check(lk_data_stream_writer_info_snapshot_info(raw, &info));
	auto object = std::get<Object>(Pack(info).data);
#define WRITER_TEXT(key, function)                                                                 \
	object[key] = Text([&](char* b, size_t n) { return function(raw, b, n); });
	WRITER_TEXT("streamId", lk_data_stream_writer_info_snapshot_stream_id)
	WRITER_TEXT("mimeType", lk_data_stream_writer_info_snapshot_mime_type)
	WRITER_TEXT("topic", lk_data_stream_writer_info_snapshot_topic)
	WRITER_TEXT("participantIdentity", lk_data_stream_writer_info_snapshot_participant_identity)
	WRITER_TEXT("name", lk_data_stream_writer_info_snapshot_name)
	WRITER_TEXT("replyToStreamId", lk_data_stream_writer_info_snapshot_reply_to_stream_id)
#undef WRITER_TEXT
	Object attributes;
	Array attached;
	for (size_t i = 0; i < info.attribute_count; ++i) {
		auto key = Text([&](char* b, size_t n) {
			return lk_data_stream_writer_info_snapshot_attribute_key(raw, i, b, n);
		});
		attributes[key] = Text([&](char* b, size_t n) {
			return lk_data_stream_writer_info_snapshot_attribute_value(raw, i, b, n);
		});
	}
	for (size_t i = 0; i < info.attached_stream_id_count; ++i)
		attached.push_back(Text([&](char* b, size_t n) {
			return lk_data_stream_writer_info_snapshot_attached_stream_id(raw, i, b, n);
		}));
	object["attributes"] = std::move(attributes);
	object["attachedStreamIds"] = std::move(attached);
	return object;
}
Value Streams(napi_env env, Room& state, const std::string& op, napi_value args) {
	Arena arena;
	auto* r = state.room;
	const auto kind = Str(env, args, "kind");
	const auto id = Num(env, args, "id", 0);
	if (op == "streamText" || op == "streamBytes") {
		const auto new_id = state.Id();
		auto context = std::make_unique<WriterContext>(WriterContext{&state, new_id});
		if (op == "streamText") {
			auto o = Options<lk_stream_text_options_t>(env, args, arena);
			o.on_progress = WriterProgress;
			o.progress_user_data = context.get();
			o.on_complete = WriterComplete;
			o.completion_user_data = context.get();
			lk_text_stream_writer_t* raw = nullptr;
			Check(lk_room_stream_text(r, &o, &raw));
			state.text_writers.emplace(
			    new_id, Owned<lk_text_stream_writer_t, lk_text_stream_writer_destroy>(raw));
		} else {
			auto o = Options<lk_stream_bytes_options_t>(env, args, arena);
			o.on_progress = WriterProgress;
			o.progress_user_data = context.get();
			o.on_complete = WriterComplete;
			o.completion_user_data = context.get();
			lk_byte_stream_writer_t* raw = nullptr;
			Check(lk_room_stream_bytes(r, &o, &raw));
			state.byte_writers.emplace(
			    new_id, Owned<lk_byte_stream_writer_t, lk_byte_stream_writer_destroy>(raw));
		}
		state.writer_contexts.push_back(std::move(context));
		return new_id;
	}
	if (op == "streamRegister") {
		const auto topic = Str(env, args, "topic");
		if (kind == "text")
			Check(
			    lk_room_register_text_stream_handler(r, topic.c_str(), TextStreamHandler, &state));
		else if (kind == "bytes")
			Check(
			    lk_room_register_byte_stream_handler(r, topic.c_str(), ByteStreamHandler, &state));
		else
			throw Error("Unknown stream kind");
		return {};
	}
	if (op == "streamUnregister") {
		const auto topic = Str(env, args, "topic");
		if (kind == "text")
			Check(lk_room_unregister_text_stream_handler(r, topic.c_str()));
		else if (kind == "bytes")
			Check(lk_room_unregister_byte_stream_handler(r, topic.c_str()));
		else
			throw Error("Unknown stream kind");
		return {};
	}
	if (op.starts_with("writer")) {
		if (kind == "text") {
			auto* writer = Resource(state.text_writers, id);
			if (op == "writerWrite") {
				const auto text = String(env, Get(env, args, "text"), true);
				Check(lk_text_stream_writer_write(writer, text.data(), text.size()));
			} else if (op == "writerClose")
				Check(lk_text_stream_writer_close(writer));
			else if (op == "writerCancel")
				Check(lk_text_stream_writer_cancel(writer, Str(env, args, "reason").c_str()));
			else if (op == "writerDispose")
				state.text_writers.erase(id);
			else if (op == "writerInfo") {
				lk_data_stream_writer_info_snapshot_t* raw = nullptr;
				Check(lk_text_stream_writer_create_info_snapshot(writer, &raw));
				return WriterInfo(raw);
			} else if (op == "writerClosed")
				return bool(lk_text_stream_writer_is_closed(writer));
			else
				throw Error("Unknown writer operation");
		} else if (kind == "bytes") {
			auto* writer = Resource(state.byte_writers, id);
			if (op == "writerWrite") {
				const auto data = Data(env, args, "data");
				Check(lk_byte_stream_writer_write(writer, data.data(), data.size()));
			} else if (op == "writerClose")
				Check(lk_byte_stream_writer_close(writer));
			else if (op == "writerCancel")
				Check(lk_byte_stream_writer_cancel(writer, Str(env, args, "reason").c_str()));
			else if (op == "writerDispose")
				state.byte_writers.erase(id);
			else if (op == "writerInfo") {
				lk_data_stream_writer_info_snapshot_t* raw = nullptr;
				Check(lk_byte_stream_writer_create_info_snapshot(writer, &raw));
				return WriterInfo(raw);
			} else if (op == "writerClosed")
				return bool(lk_byte_stream_writer_is_closed(writer));
			else
				throw Error("Unknown writer operation");
		} else
			throw Error("Unknown writer kind");
		return {};
	}
	if (op == "readerCreate") {
		auto o = Options<lk_media_stream_options_t>(env, args, arena);
		const auto identity = Str(env, args, "participantIdentity"),
		           sid = Str(env, args, "trackSid");
		const auto new_id = state.Id();
		if (kind == "audio") {
			lk_audio_stream_t* raw = nullptr;
			Check(lk_room_create_audio_stream(r, identity.c_str(), sid.c_str(), &o, &raw));
			state.audio_readers.emplace(new_id,
			                            Owned<lk_audio_stream_t, lk_audio_stream_destroy>(raw));
		} else if (kind == "video") {
			lk_video_stream_t* raw = nullptr;
			Check(lk_room_create_video_stream(r, identity.c_str(), sid.c_str(), &o, &raw));
			state.video_readers.emplace(new_id,
			                            Owned<lk_video_stream_t, lk_video_stream_destroy>(raw));
		} else
			throw Error("Unknown reader kind");
		return new_id;
	}
	if (op.starts_with("reader")) {
		const auto timeout = std::min<uint32_t>(100, Num(env, args, "timeoutMs", 0));
		if (kind == "audio") {
			auto* reader = Resource(state.audio_readers, id);
			if (op == "readerClose") {
				lk_audio_stream_close(reader);
				state.audio_readers.erase(id);
				return {};
			}
			if (op == "readerInfo")
				return Object{{"closed", bool(lk_audio_stream_is_closed(reader))},
				              {"droppedFrames", double(lk_audio_stream_dropped_frames(reader))}};
			if (op != "readerRead")
				throw Error("Unknown reader operation");
			lk_owned_audio_frame_t* raw = nullptr;
			const auto status = lk_audio_stream_read_for(reader, timeout, &raw);
			Owned<lk_owned_audio_frame_t, lk_owned_audio_frame_destroy> frame(raw);
			if (status == LK_MEDIA_STREAM_READ_INVALID_ARGUMENT)
				Check(LK_STATUS_OPERATION_FAILED);
			if (status != LK_MEDIA_STREAM_READ_FRAME)
				return Object{{"status", int(status)}};
			lk_audio_frame_t value{};
			Check(lk_owned_audio_frame_data(raw, &value));
			return Object{{"status", int(status)}, {"frame", Pack(value)}};
		}
		if (kind == "video") {
			auto* reader = Resource(state.video_readers, id);
			if (op == "readerClose") {
				lk_video_stream_close(reader);
				state.video_readers.erase(id);
				return {};
			}
			if (op == "readerInfo")
				return Object{{"closed", bool(lk_video_stream_is_closed(reader))},
				              {"droppedFrames", double(lk_video_stream_dropped_frames(reader))}};
			if (op != "readerRead")
				throw Error("Unknown reader operation");
			lk_owned_video_frame_t* raw = nullptr;
			const auto status = lk_video_stream_read_for(reader, timeout, &raw);
			Owned<lk_owned_video_frame_t, lk_owned_video_frame_destroy> frame(raw);
			if (status == LK_MEDIA_STREAM_READ_INVALID_ARGUMENT)
				Check(LK_STATUS_OPERATION_FAILED);
			if (status != LK_MEDIA_STREAM_READ_FRAME)
				return Object{{"status", int(status)}};
			lk_video_frame_t value{};
			Check(lk_owned_video_frame_data(raw, &value));
			lk_video_frame_metadata_t metadata{};
			lk_video_frame_metadata_init(&metadata);
			Check(lk_owned_video_frame_metadata(raw, &metadata));
			return Object{
			    {"status", int(status)}, {"frame", Pack(value)}, {"metadata", Pack(metadata)}};
		}
		throw Error("Unknown reader kind");
	}
	if (op == "recorderStart") {
		auto o = Options<lk_track_recorder_options_t>(env, args, arena);
		lk_track_recorder_t* raw = nullptr;
		Check(lk_room_start_track_recording(r, Str(env, args, "participantIdentity").c_str(),
		                                    Str(env, args, "trackSid").c_str(), &o, &raw));
		const auto new_id = state.Id();
		state.recorders.emplace(new_id, Owned<lk_track_recorder_t, lk_track_recorder_destroy>(raw));
		return new_id;
	}
	if (op.starts_with("recorder")) {
		auto* recorder = Resource(state.recorders, id);
		if (op == "recorderClose") {
			lk_track_recorder_stop(recorder);
			state.recorders.erase(id);
			return {};
		}
		if (op == "recorderStop") {
			lk_track_recorder_stop(recorder);
			return {};
		}
		if (op != "recorderStats")
			throw Error("Unknown recorder operation");
		lk_track_recorder_stats_t stats{};
		lk_track_recorder_stats_init(&stats);
		Check(lk_track_recorder_get_stats(recorder, &stats));
		return Object{{"stats", Pack(stats)},
		              {"outputPath", Text([&](char* b, size_t n) {
			               return lk_track_recorder_output_path(recorder, b, n);
		               })},
		              {"error", Text([&](char* b, size_t n) {
			               return lk_track_recorder_error(recorder, b, n);
		               })}};
	}
	throw Error("Unknown stream operation");
}
