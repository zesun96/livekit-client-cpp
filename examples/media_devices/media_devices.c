#include "livekit/capi/livekit.h"

#include <stdio.h>
#include <stdlib.h>

static const char* kind_name(lk_media_device_kind_t kind) {
	switch (kind) {
	case LK_MEDIA_DEVICE_KIND_AUDIO_INPUT:
		return "audio input";
	case LK_MEDIA_DEVICE_KIND_AUDIO_OUTPUT:
		return "audio output";
	case LK_MEDIA_DEVICE_KIND_VIDEO_INPUT:
		return "video input";
	default:
		return "unknown";
	}
}

int main(void) {
	lk_media_device_list_t* devices = NULL;
	if (lk_media_device_list_create(&devices) != LK_STATUS_OK) {
		fprintf(stderr, "Device enumeration failed: %s\n", lk_last_error());
		return 1;
	}

	const size_t count = lk_media_device_list_count(devices);
	printf("Found %zu media device(s)\n", count);
	for (size_t index = 0; index < count; ++index) {
		lk_media_device_info_t info = {sizeof(info), LK_MEDIA_DEVICE_KIND_AUDIO_INPUT, 0};
		const size_t id_size = lk_media_device_list_id(devices, index, NULL, 0);
		const size_t label_size = lk_media_device_list_label(devices, index, NULL, 0);
		char* id = (char*)malloc(id_size);
		char* label = (char*)malloc(label_size);
		if (id == NULL || label == NULL ||
		    lk_media_device_list_info(devices, index, &info) != LK_STATUS_OK) {
			free(id);
			free(label);
			lk_media_device_list_destroy(devices);
			return 1;
		}
		lk_media_device_list_id(devices, index, id, id_size);
		lk_media_device_list_label(devices, index, label, label_size);
		printf("%s%s %s\n  %s\n", kind_name(info.kind), info.is_default ? " [default]" : "", label,
		       id);
		free(id);
		free(label);
	}

	lk_media_device_list_destroy(devices);
	return 0;
}
