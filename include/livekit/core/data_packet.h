#pragma once

#ifndef _LKC_CORE_DATA_PACKET_H_
#define _LKC_CORE_DATA_PACKET_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace livekit {
namespace core {

struct DataPublishOptions {
	bool reliable = true;
	std::string topic;
	std::vector<std::string> destination_identities;
};

struct DataReceivedEvent {
	std::vector<uint8_t> payload;
	std::string topic;
	std::string participant_identity;
	bool reliable = true;
};

struct FileSendOptions {
	std::string topic = "files";
	std::string mime_type = "application/octet-stream";
	std::vector<std::string> destination_identities;
	std::size_t chunk_size = 15'000;
	std::map<std::string, std::string> attributes;
};

struct TextSendOptions {
	std::string topic;
	std::vector<std::string> destination_identities;
	std::map<std::string, std::string> attributes;
	std::string reply_to_stream_id;
	std::vector<std::string> attached_stream_ids;
	std::size_t chunk_size = 15'000;
};

struct ByteSendOptions {
	std::string topic;
	std::string mime_type = "application/octet-stream";
	std::string name;
	std::vector<std::string> destination_identities;
	std::map<std::string, std::string> attributes;
	std::size_t chunk_size = 15'000;
};

struct TextReceivedEvent {
	std::string stream_id;
	std::string text;
	std::string topic;
	std::string participant_identity;
	std::string reply_to_stream_id;
	std::vector<std::string> attached_stream_ids;
	std::map<std::string, std::string> attributes;
	int64_t timestamp = 0;
};

struct FileReceivedEvent {
	std::string stream_id;
	std::string name;
	std::string mime_type;
	std::string topic;
	std::string participant_identity;
	std::vector<uint8_t> data;
	std::map<std::string, std::string> attributes;
	int64_t timestamp = 0;
};

struct ByteReceivedEvent : FileReceivedEvent {};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DATA_PACKET_H_
