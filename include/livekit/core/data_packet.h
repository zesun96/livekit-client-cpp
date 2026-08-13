#pragma once

#ifndef _LKC_CORE_DATA_PACKET_H_
#define _LKC_CORE_DATA_PACKET_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

enum class DataStreamEventType {
	Open = 0,
	Chunk = 1,
	Closed = 2,
	Failed = 3,
};

struct DataStreamInfo {
	std::string stream_id;
	std::string mime_type;
	std::string topic;
	std::string participant_identity;
	std::map<std::string, std::string> attributes;
	std::optional<uint64_t> total_size;
	int64_t timestamp = 0;
};

struct TextStreamInfo : DataStreamInfo {
	std::string reply_to_stream_id;
	std::vector<std::string> attached_stream_ids;
};

struct ByteStreamInfo : DataStreamInfo {
	std::string name;
};

using DataStreamProgressHandler =
    std::function<void(uint64_t bytes_sent, std::optional<uint64_t> total_size)>;

struct StreamTextOptions {
	std::string topic;
	std::vector<std::string> destination_identities;
	std::map<std::string, std::string> attributes;
	std::string reply_to_stream_id;
	std::vector<std::string> attached_stream_ids;
	std::string stream_id;
	std::optional<uint64_t> total_size;
	std::size_t chunk_size = 15'000;
	bool update = false;
	int32_t version = 0;
	DataStreamProgressHandler on_progress;
};

struct StreamBytesOptions {
	std::string topic;
	std::string mime_type = "application/octet-stream";
	std::string name = "unknown";
	std::vector<std::string> destination_identities;
	std::map<std::string, std::string> attributes;
	std::string stream_id;
	std::optional<uint64_t> total_size;
	std::size_t chunk_size = 15'000;
	DataStreamProgressHandler on_progress;
};

struct TextStreamEvent {
	TextStreamInfo info;
	DataStreamEventType type = DataStreamEventType::Open;
	std::string content;
	uint64_t chunk_index = 0;
	std::string reason;
};

struct ByteStreamEvent {
	ByteStreamInfo info;
	DataStreamEventType type = DataStreamEventType::Open;
	std::vector<uint8_t> content;
	uint64_t chunk_index = 0;
	std::string reason;
};

using TextStreamHandler = std::function<void(const TextStreamEvent&)>;
using ByteStreamHandler = std::function<void(const ByteStreamEvent&)>;

class TextStreamWriterInterface {
public:
	virtual ~TextStreamWriterInterface() = default;
	virtual TextStreamInfo Info() const = 0;
	virtual bool Write(const std::string& text) = 0;
	virtual bool Close() = 0;
	virtual bool Cancel(std::string reason = "cancelled") = 0;
	virtual bool IsClosed() const = 0;
};

class ByteStreamWriterInterface {
public:
	virtual ~ByteStreamWriterInterface() = default;
	virtual ByteStreamInfo Info() const = 0;
	virtual bool Write(const std::vector<uint8_t>& data) = 0;
	virtual bool Close() = 0;
	virtual bool Cancel(std::string reason = "cancelled") = 0;
	virtual bool IsClosed() const = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DATA_PACKET_H_
