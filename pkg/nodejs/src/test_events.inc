// Exercise registered callbacks with borrowed storage from a non-JavaScript thread.
if (method == "_testAdvancedEvents") {
	std::thread producer([&] {
		Bytes bytes(24, uint8_t{7});
		lk_participant_info_t p{
		    "PA_test", "peer", "Peer", "metadata", 0.5f, LK_CONNECTION_QUALITY_EXCELLENT, 1, 0};
		lk_track_publication_info_t track{};
		track.sid = "TR_test";
		track.name = "video";
		track.kind = LK_TRACK_KIND_VIDEO;
		lk_video_frame_t video{bytes.data(), bytes.size(), 4, 4, 9007199254740993LL};
		lk_video_frame_metadata_t metadata{};
		lk_video_frame_metadata_init(&metadata);
		metadata.has_user_timestamp_us = 1;
		metadata.user_timestamp_us = 18446744073709551615ULL;
		metadata.has_user_data = 1;
		metadata.user_data = bytes.data();
		metadata.user_data_size = 3;
		state.test_callbacks.on_video_frame_with_metadata(&state, state.room, &track, &p, &video,
		                                                  &metadata);
		lk_attribute_t attr{"example", "value"};
		state.test_callbacks.on_participant_attributes_changed(&state, state.room, &attr, 1, &p);
		lk_data_track_info_t data_track{};
		data_track.sid = "DT_test";
		data_track.name = "data";
		lk_data_track_frame_view_t frame{bytes.data(), 3, 1, 18446744073709551615ULL};
		state.test_callbacks.on_data_track_frame(&state, state.room, &data_track, &p, &frame);
		const char content[] = {'a', '\0', 'b'};
		lk_text_stream_event_t text{};
		text.content = content;
		text.content_size = sizeof(content);
		text.chunk_index = 9007199254740993ULL;
		TextStreamHandler(&state, state.room, &text);
		WriterContext writer{&state, 42};
		WriterProgress(&writer, 9007199254740993ULL, 1, UINT64_MAX);
		std::fill(bytes.begin(), bytes.end(), uint8_t{255});
	});
	producer.join();
	return {};
}
