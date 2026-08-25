/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#pragma once

#ifndef _LKC_CORE_DATA_TRACK_H_
#define _LKC_CORE_DATA_TRACK_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace livekit {
namespace core {

enum class DataTrackFrameEncodingKind {
	Unspecified,
	Ros1,
	Cdr,
	Protobuf,
	Flatbuffer,
	Cbor,
	Msgpack,
	Json,
	Custom,
};

enum class DataTrackSchemaEncodingKind {
	Unspecified,
	Protobuf,
	Flatbuffer,
	Ros1Message,
	Ros2Message,
	Ros2Idl,
	OmgIdl,
	JsonSchema,
	Custom,
};

struct DataTrackFrameEncoding {
	DataTrackFrameEncodingKind kind = DataTrackFrameEncodingKind::Unspecified;
	std::string custom;

	bool operator==(const DataTrackFrameEncoding&) const = default;
};

struct DataTrackSchemaEncoding {
	DataTrackSchemaEncodingKind kind = DataTrackSchemaEncodingKind::Unspecified;
	std::string custom;

	bool operator==(const DataTrackSchemaEncoding&) const = default;
};

struct DataTrackSchemaId {
	std::string name;
	DataTrackSchemaEncoding encoding;

	bool operator==(const DataTrackSchemaId&) const = default;
};

inline constexpr std::size_t kMaximumDataTrackSchemaDefinitionSize = 50 * 1024;

struct DataTrackSchema {
	DataTrackSchemaId id;
	std::vector<uint8_t> definition;

	bool operator==(const DataTrackSchema&) const = default;
};

struct DataTrackInfo {
	uint16_t publisher_handle = 0;
	std::string sid;
	std::string name;
	bool uses_e2ee = false;
	std::optional<DataTrackFrameEncoding> frame_encoding;
	std::optional<DataTrackSchemaId> schema;

	bool operator==(const DataTrackInfo&) const = default;
};

struct RemoteDataTrackSnapshot {
	DataTrackInfo info;
	std::string publisher_identity;
	bool published = false;

	bool operator==(const RemoteDataTrackSnapshot&) const = default;
};

struct DataTrackFrame {
	std::vector<uint8_t> payload;
	std::optional<uint64_t> user_timestamp;
};

enum class DataTrackErrorCode {
	None,
	InvalidName,
	InvalidSchema,
	DuplicateName,
	HandleLimitReached,
	NotAllowed,
	Disconnected,
	Timeout,
	Unpublished,
	QueueFull,
	InvalidFrame,
	ProtocolError,
	SendFailed,
	NotFound,
};

struct DataTrackError {
	DataTrackErrorCode code = DataTrackErrorCode::None;
	std::string message;

	explicit operator bool() const { return code != DataTrackErrorCode::None; }
	bool operator==(const DataTrackError&) const = default;
};

struct DataTrackPublishOptions {
	std::string name;
	std::optional<DataTrackFrameEncoding> frame_encoding;
	std::optional<DataTrackSchemaId> schema;
};

struct DataTrackSubscriptionOptions {
	std::optional<uint32_t> target_fps;
	std::size_t buffer_capacity = 16;
	std::size_t max_partial_frames = 1;
};

class DataTrackReader {
public:
	~DataTrackReader();
	DataTrackReader(const DataTrackReader&) = delete;
	DataTrackReader& operator=(const DataTrackReader&) = delete;

	bool Read(DataTrackFrame& frame);
	bool ReadFor(DataTrackFrame& frame, std::chrono::milliseconds timeout);
	bool TryRead(DataTrackFrame& frame);
	void Close();
	bool IsClosed() const;
	std::size_t DroppedFrames() const;

private:
	class Impl;
	explicit DataTrackReader(std::shared_ptr<Impl> impl);
	std::shared_ptr<Impl> impl_;

	friend class RemoteDataTrack;
};

class DataTrackInterface {
public:
	virtual ~DataTrackInterface() = default;
	virtual DataTrackInfo Info() const = 0;
	virtual bool IsPublished() const = 0;
};

class LocalDataTrackInterface : public virtual DataTrackInterface {
public:
	~LocalDataTrackInterface() override = default;
	virtual DataTrackError TryPush(const DataTrackFrame& frame) = 0;
	virtual DataTrackError Unpublish() = 0;
};

struct DataTrackPublishResult {
	std::shared_ptr<LocalDataTrackInterface> track;
	DataTrackError error;

	explicit operator bool() const { return track != nullptr && !error; }
};

struct DataTrackSchemaResult {
	std::optional<DataTrackSchema> schema;
	DataTrackError error;

	explicit operator bool() const { return schema.has_value() && !error; }
};

class RemoteDataTrackInterface : public virtual DataTrackInterface {
public:
	~RemoteDataTrackInterface() override = default;
	virtual std::string PublisherIdentity() const = 0;
	virtual std::shared_ptr<DataTrackReader>
	Subscribe(DataTrackSubscriptionOptions options = {}) = 0;
	virtual bool SetSubscriptionOptions(DataTrackSubscriptionOptions options) = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DATA_TRACK_H_
