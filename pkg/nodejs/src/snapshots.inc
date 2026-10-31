Array StatsSnapshot(lk_rtc_stats_snapshot_t* raw) {
	Owned<lk_rtc_stats_snapshot_t, lk_rtc_stats_snapshot_destroy> owned(raw);
	Array values;
	for (size_t i = 0; i < lk_rtc_stats_snapshot_count(raw); ++i) {
		lk_rtc_track_stats_t info{};
		lk_rtc_track_stats_init(&info);
		Check(lk_rtc_stats_snapshot_info(raw, i, &info));
		auto object = std::get<Object>(Pack(info).data);
#define STATS_TEXT(key, function)                                                                  \
	object[key] = Text([&](char* b, size_t n) { return function(raw, i, b, n); });
		STATS_TEXT("id", lk_rtc_stats_snapshot_id)
		STATS_TEXT("kind", lk_rtc_stats_snapshot_kind)
		STATS_TEXT("rid", lk_rtc_stats_snapshot_rid)
		STATS_TEXT("mimeType", lk_rtc_stats_snapshot_codec_mime_type)
		STATS_TEXT("implementation", lk_rtc_stats_snapshot_codec_implementation)
		STATS_TEXT("qualityLimitationReason", lk_rtc_stats_snapshot_quality_limitation_reason)
#undef STATS_TEXT
		values.push_back(std::move(object));
	}
	return values;
}
Object LocalSnapshot(lk_room_t* room) {
	lk_local_participant_snapshot_t* raw = nullptr;
	Check(lk_room_create_local_participant_snapshot(room, &raw));
	Owned<lk_local_participant_snapshot_t, lk_local_participant_snapshot_destroy> owned(raw);
	lk_local_participant_snapshot_info_t info{};
	lk_local_participant_snapshot_info_init(&info);
	Check(lk_local_participant_snapshot_info(raw, &info));
	auto object = std::get<Object>(Pack(info).data);
#define LOCAL_TEXT(key, function)                                                                  \
	object[key] = Text([&](char* b, size_t n) { return function(raw, b, n); });
	LOCAL_TEXT("sid", lk_local_participant_snapshot_sid)
	LOCAL_TEXT("identity", lk_local_participant_snapshot_identity)
	LOCAL_TEXT("name", lk_local_participant_snapshot_name)
	LOCAL_TEXT("metadata", lk_local_participant_snapshot_metadata)
#undef LOCAL_TEXT
	Object attributes;
	for (size_t i = 0; i < lk_local_participant_snapshot_attribute_count(raw); ++i) {
		auto key = Text([&](char* b, size_t n) {
			return lk_local_participant_snapshot_attribute_key(raw, i, b, n);
		});
		attributes[key] = Text([&](char* b, size_t n) {
			return lk_local_participant_snapshot_attribute_value(raw, i, b, n);
		});
	}
	lk_participant_permissions_t permissions{};
	Check(lk_local_participant_snapshot_permissions(raw, &permissions));
	object["attributes"] = std::move(attributes);
	object["permissions"] = Pack(permissions);
	return object;
}
void RemoteDetails(Object& object, const lk_remote_participant_snapshot_t* p) {
	lk_remote_participant_snapshot_info_t info{};
	lk_remote_participant_snapshot_info_init(&info);
	Check(lk_remote_participant_snapshot_info(p, &info));
	auto packed = Pack(info);
	for (auto& [key, value] : std::get<Object>(packed.data))
		object[key] = value;
	Object attributes;
	for (size_t i = 0; i < lk_remote_participant_snapshot_attribute_count(p); ++i) {
		auto key = Text([&](char* b, size_t n) {
			return lk_remote_participant_snapshot_attribute_key(p, i, b, n);
		});
		attributes[key] = Text([&](char* b, size_t n) {
			return lk_remote_participant_snapshot_attribute_value(p, i, b, n);
		});
	}
	lk_participant_permissions_t permissions{};
	Check(lk_remote_participant_snapshot_permissions(p, &permissions));
	object["attributes"] = std::move(attributes);
	object["permissions"] = Pack(permissions);
}
void PublicationDetails(Object& object, const lk_remote_track_publication_snapshot_t* p,
                        const lk_remote_track_publication_snapshot_info_t& info) {
	auto packed = Pack(info);
	for (auto& [key, value] : std::get<Object>(packed.data))
		object[key] = value;
	if (!info.has_subscribed_track) {
		object["track"] = Value();
		return;
	}
	const lk_remote_track_snapshot_t* track = nullptr;
	Check(lk_remote_track_publication_snapshot_track(p, &track));
	if (!track) {
		object["track"] = Value();
		return;
	}
	lk_remote_track_snapshot_info_t track_info{};
	lk_remote_track_snapshot_info_init(&track_info);
	Check(lk_remote_track_snapshot_info(track, &track_info));
	auto value = std::get<Object>(Pack(track_info).data);
	value["sid"] =
	    Text([&](char* b, size_t n) { return lk_remote_track_snapshot_sid(track, b, n); });
	value["name"] =
	    Text([&](char* b, size_t n) { return lk_remote_track_snapshot_name(track, b, n); });
	lk_rtc_stats_snapshot_t* stats = nullptr;
	Check(lk_remote_track_snapshot_create_rtc_stats_snapshot(track, &stats));
	value["rtcStats"] = StatsSnapshot(stats);
	object["track"] = std::move(value);
}
