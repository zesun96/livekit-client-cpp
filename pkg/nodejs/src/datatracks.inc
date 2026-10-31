Value RemoteDataTracks(Room& state) {
	lk_remote_data_track_list_t* raw = nullptr;
	Check(lk_room_create_remote_data_track_snapshot(state.room, &raw));
	Owned<lk_remote_data_track_list_t, lk_remote_data_track_list_destroy> list(raw);
	Array values;
	for (size_t i = 0; i < lk_remote_data_track_list_count(raw); ++i) {
		const lk_remote_data_track_snapshot_t* track = nullptr;
		Check(lk_remote_data_track_list_at(raw, i, &track));
		lk_data_track_snapshot_info_t info{};
		lk_data_track_snapshot_info_init(&info);
		Check(lk_remote_data_track_snapshot_info(track, &info));
		auto object = std::get<Object>(Pack(info).data);
#define REMOTE_DATA_TEXT(key, function)                                                            \
	object[key] = Text([&](char* b, size_t n) { return function(track, b, n); });
		REMOTE_DATA_TEXT("publisherIdentity", lk_remote_data_track_snapshot_publisher_identity)
		REMOTE_DATA_TEXT("sid", lk_remote_data_track_snapshot_sid)
		REMOTE_DATA_TEXT("name", lk_remote_data_track_snapshot_name)
		REMOTE_DATA_TEXT("customFrameEncoding", lk_remote_data_track_snapshot_custom_frame_encoding)
		REMOTE_DATA_TEXT("schemaName", lk_remote_data_track_snapshot_schema_name)
		REMOTE_DATA_TEXT("customSchemaEncoding",
		                 lk_remote_data_track_snapshot_custom_schema_encoding)
#undef REMOTE_DATA_TEXT
		values.push_back(std::move(object));
	}
	return values;
}
Value DataTracks(napi_env env, Room& state, const std::string& op, napi_value args) {
	Arena arena;
	auto* r = state.room;
	const auto id = Num(env, args, "id", 0);
	if (op == "dataTrackSnapshot")
		return RemoteDataTracks(state);
	if (op == "dataTrackStoreSchema" || op == "dataTrackGetSchema") {
		auto schema = Options<lk_data_track_schema_id_t>(env, args, arena);
		if (op == "dataTrackStoreSchema") {
			const auto definition = Data(env, args, "definition");
			DataCheck(
			    lk_room_store_data_track_schema(r, &schema, definition.data(), definition.size()));
			return {};
		}
		lk_data_track_schema_t* raw = nullptr;
		DataCheck(lk_room_get_data_track_schema(r, Str(env, args, "participantIdentity").c_str(),
		                                        &schema, &raw));
		Owned<lk_data_track_schema_t, lk_data_track_schema_destroy> owned(raw);
		return Object{
		    {"name", Text([&](char* b, size_t n) { return lk_data_track_schema_name(raw, b, n); })},
		    {"encoding", int(lk_data_track_schema_encoding(raw))},
		    {"customEncoding", Text([&](char* b, size_t n) {
			     return lk_data_track_schema_custom_encoding(raw, b, n);
		     })},
		    {"definition", BufferValue([&](uint8_t* b, size_t n) {
			     return lk_data_track_schema_definition(raw, b, n);
		     })}};
	}
	if (op == "dataTrackPublish") {
		auto options = Options<lk_data_track_publish_options_t>(env, args, arena);
		lk_local_data_track_t* raw = nullptr;
		DataCheck(lk_room_publish_data_track(r, &options, &raw));
		const auto new_id = state.Id();
		state.data_tracks.emplace(new_id,
		                          Owned<lk_local_data_track_t, lk_local_data_track_destroy>(raw));
		return new_id;
	}
	if (op == "dataReaderCreate" || op == "dataTrackUpdateSubscription") {
		auto options = Options<lk_data_track_subscription_options_t>(env, args, arena);
		const auto identity = Str(env, args, "participantIdentity"),
		           sid = Str(env, args, "trackSid");
		if (op == "dataTrackUpdateSubscription") {
			DataCheck(lk_room_update_data_track_subscription_options(r, identity.c_str(),
			                                                         sid.c_str(), &options));
			return {};
		}
		lk_data_track_reader_t* raw = nullptr;
		DataCheck(lk_room_subscribe_data_track(r, identity.c_str(), sid.c_str(), &options, &raw));
		const auto new_id = state.Id();
		state.data_readers.emplace(
		    new_id, Owned<lk_data_track_reader_t, lk_data_track_reader_destroy>(raw));
		return new_id;
	}
	if (op.starts_with("dataReader")) {
		auto* reader = Resource(state.data_readers, id);
		if (op == "dataReaderClose") {
			lk_data_track_reader_close(reader);
			state.data_readers.erase(id);
			return {};
		}
		if (op == "dataReaderInfo")
			return Object{{"closed", bool(lk_data_track_reader_is_closed(reader))},
			              {"droppedFrames", double(lk_data_track_reader_dropped_frames(reader))}};
		if (op != "dataReaderRead")
			throw Error("Unknown DataTrack reader operation");
		lk_data_track_frame_t* raw = nullptr;
		const auto status = lk_data_track_reader_read_for(
		    reader, std::min<uint32_t>(100, Num(env, args, "timeoutMs", 0)), &raw);
		Owned<lk_data_track_frame_t, lk_data_track_frame_destroy> frame(raw);
		if (status == LK_DATA_TRACK_READ_INVALID_ARGUMENT)
			Check(LK_STATUS_OPERATION_FAILED);
		if (status != LK_DATA_TRACK_READ_FRAME)
			return Object{{"status", int(status)}};
		return Object{
		    {"status", int(status)},
		    {"frame",
		     Object{{"data", BufferValue([&](uint8_t* b, size_t n) {
			             return lk_data_track_frame_data(raw, b, n);
		             })},
		            {"hasUserTimestamp", bool(lk_data_track_frame_has_user_timestamp(raw))},
		            {"userTimestamp", lk_data_track_frame_user_timestamp(raw)}}}};
	}
	auto* track = Resource(state.data_tracks, id);
	if (op == "dataTrackPush") {
		const auto data = Data(env, args, "data");
		const bool has = Has(env, args, "userTimestamp");
		const auto timestamp = has ? ReadNumber<uint64_t>(env, Get(env, args, "userTimestamp")) : 0;
		DataCheck(lk_local_data_track_try_push(track, data.data(), data.size(), has, timestamp));
	} else if (op == "dataTrackUnpublish")
		DataCheck(lk_local_data_track_unpublish(track));
	else if (op == "dataTrackClose") {
		DataCheck(lk_local_data_track_destroy(track));
		state.data_tracks.at(id).value = nullptr;
		state.data_tracks.erase(id);
	} else if (op == "dataTrackInfo") {
		lk_data_track_snapshot_info_t info{};
		lk_data_track_snapshot_info_init(&info);
		Check(lk_local_data_track_info(track, &info));
		auto object = std::get<Object>(Pack(info).data);
#define LOCAL_DATA_TEXT(key, function)                                                             \
	object[key] = Text([&](char* b, size_t n) { return function(track, b, n); });
		LOCAL_DATA_TEXT("sid", lk_local_data_track_sid)
		LOCAL_DATA_TEXT("name", lk_local_data_track_name)
		LOCAL_DATA_TEXT("customFrameEncoding", lk_local_data_track_custom_frame_encoding)
		LOCAL_DATA_TEXT("schemaName", lk_local_data_track_schema_name)
		LOCAL_DATA_TEXT("customSchemaEncoding", lk_local_data_track_custom_schema_encoding)
#undef LOCAL_DATA_TEXT
		object["published"] = bool(lk_local_data_track_is_published(track));
		return object;
	} else
		throw Error("Unknown DataTrack operation");
	return {};
}
