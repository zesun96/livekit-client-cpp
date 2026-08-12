#pragma once

#ifndef _LKC_CORE_DATA_PACKET_H_
#define _LKC_CORE_DATA_PACKET_H_

#include <cstddef>
#include <cstdint>
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
};

struct FileReceivedEvent {
	std::string stream_id;
	std::string name;
	std::string mime_type;
	std::string topic;
	std::string participant_identity;
	std::vector<uint8_t> data;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DATA_PACKET_H_
