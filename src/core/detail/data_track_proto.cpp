#include "data_track_proto.h"

namespace livekit {
namespace core {
namespace detail {

namespace {
DataTrackFrameEncoding FromProto(const livekit::DataTrackFrameEncoding& encoding) {
	DataTrackFrameEncoding output;
	if (encoding.has_custom()) {
		output.kind = DataTrackFrameEncodingKind::Custom;
		output.custom = encoding.custom();
		return output;
	}
	switch (encoding.well_known()) {
	case livekit::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_ROS1:
		output.kind = DataTrackFrameEncodingKind::Ros1;
		break;
	case livekit::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_CDR:
		output.kind = DataTrackFrameEncodingKind::Cdr;
		break;
	case livekit::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_PROTOBUF:
		output.kind = DataTrackFrameEncodingKind::Protobuf;
		break;
	case livekit::
	    DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_FLATBUFFER:
		output.kind = DataTrackFrameEncodingKind::Flatbuffer;
		break;
	case livekit::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_CBOR:
		output.kind = DataTrackFrameEncodingKind::Cbor;
		break;
	case livekit::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_MSGPACK:
		output.kind = DataTrackFrameEncodingKind::Msgpack;
		break;
	case livekit::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_JSON:
		output.kind = DataTrackFrameEncodingKind::Json;
		break;
	default:
		output.kind = DataTrackFrameEncodingKind::Unspecified;
		break;
	}
	return output;
}

DataTrackSchemaEncoding FromProto(const livekit::DataTrackSchemaEncoding& encoding) {
	DataTrackSchemaEncoding output;
	if (encoding.has_custom()) {
		output.kind = DataTrackSchemaEncodingKind::Custom;
		output.custom = encoding.custom();
		return output;
	}
	switch (encoding.well_known()) {
	case livekit::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_PROTOBUF:
		output.kind = DataTrackSchemaEncodingKind::Protobuf;
		break;
	case livekit::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_FLATBUFFER:
		output.kind = DataTrackSchemaEncodingKind::Flatbuffer;
		break;
	case livekit::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_ROS1_MSG:
		output.kind = DataTrackSchemaEncodingKind::Ros1Message;
		break;
	case livekit::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_ROS2_MSG:
		output.kind = DataTrackSchemaEncodingKind::Ros2Message;
		break;
	case livekit::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_ROS2_IDL:
		output.kind = DataTrackSchemaEncodingKind::Ros2Idl;
		break;
	case livekit::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_OMG_IDL:
		output.kind = DataTrackSchemaEncodingKind::OmgIdl;
		break;
	case livekit::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_JSON_SCHEMA:
		output.kind = DataTrackSchemaEncodingKind::JsonSchema;
		break;
	default:
		output.kind = DataTrackSchemaEncodingKind::Unspecified;
		break;
	}
	return output;
}
} // namespace

DataTrackInfo FromProto(const livekit::DataTrackInfo& info) {
	DataTrackInfo output;
	output.publisher_handle = static_cast<uint16_t>(info.pub_handle());
	output.sid = info.sid();
	output.name = info.name();
	output.uses_e2ee = info.encryption() != livekit::Encryption_Type_NONE;
	if (info.has_frame_encoding()) {
		output.frame_encoding = FromProto(info.frame_encoding());
	}
	if (info.has_schema()) {
		output.schema = FromProto(info.schema());
	}
	return output;
}

DataTrackSchemaId FromProto(const livekit::DataTrackSchemaId& schema) {
	return {schema.name(), FromProto(schema.encoding())};
}

bool ToProto(const DataTrackFrameEncoding& encoding, livekit::DataTrackFrameEncoding& output) {
	using Proto = livekit::DataTrackFrameEncoding_WellKnownFrameEncoding;
	if (encoding.kind == DataTrackFrameEncodingKind::Custom) {
		if (encoding.custom.empty() || encoding.custom.size() > 32) {
			return false;
		}
		output.set_custom(encoding.custom);
		return true;
	}
	Proto value =
	    Proto::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_UNSPECIFIED;
	switch (encoding.kind) {
	case DataTrackFrameEncodingKind::Ros1:
		value = Proto::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_ROS1;
		break;
	case DataTrackFrameEncodingKind::Cdr:
		value = Proto::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_CDR;
		break;
	case DataTrackFrameEncodingKind::Protobuf:
		value =
		    Proto::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_PROTOBUF;
		break;
	case DataTrackFrameEncodingKind::Flatbuffer:
		value = Proto::
		    DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_FLATBUFFER;
		break;
	case DataTrackFrameEncodingKind::Cbor:
		value = Proto::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_CBOR;
		break;
	case DataTrackFrameEncodingKind::Msgpack:
		value =
		    Proto::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_MSGPACK;
		break;
	case DataTrackFrameEncodingKind::Json:
		value = Proto::DataTrackFrameEncoding_WellKnownFrameEncoding_WELL_KNOWN_FRAME_ENCODING_JSON;
		break;
	case DataTrackFrameEncodingKind::Unspecified:
		break;
	case DataTrackFrameEncodingKind::Custom:
		return false;
	}
	output.set_well_known(value);
	return true;
}

bool ToProto(const DataTrackSchemaEncoding& encoding, livekit::DataTrackSchemaEncoding& output) {
	using Proto = livekit::DataTrackSchemaEncoding_WellKnownSchemaEncoding;
	if (encoding.kind == DataTrackSchemaEncodingKind::Custom) {
		if (encoding.custom.empty() || encoding.custom.size() > 32) {
			return false;
		}
		output.set_custom(encoding.custom);
		return true;
	}
	Proto value = Proto::
	    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_UNSPECIFIED;
	switch (encoding.kind) {
	case DataTrackSchemaEncodingKind::Protobuf:
		value = Proto::
		    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_PROTOBUF;
		break;
	case DataTrackSchemaEncodingKind::Flatbuffer:
		value = Proto::
		    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_FLATBUFFER;
		break;
	case DataTrackSchemaEncodingKind::Ros1Message:
		value = Proto::
		    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_ROS1_MSG;
		break;
	case DataTrackSchemaEncodingKind::Ros2Message:
		value = Proto::
		    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_ROS2_MSG;
		break;
	case DataTrackSchemaEncodingKind::Ros2Idl:
		value = Proto::
		    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_ROS2_IDL;
		break;
	case DataTrackSchemaEncodingKind::OmgIdl:
		value = Proto::
		    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_OMG_IDL;
		break;
	case DataTrackSchemaEncodingKind::JsonSchema:
		value = Proto::
		    DataTrackSchemaEncoding_WellKnownSchemaEncoding_WELL_KNOWN_SCHEMA_ENCODING_JSON_SCHEMA;
		break;
	case DataTrackSchemaEncodingKind::Unspecified:
		break;
	case DataTrackSchemaEncodingKind::Custom:
		return false;
	}
	output.set_well_known(value);
	return true;
}

bool ToProto(const DataTrackSchemaId& schema, livekit::DataTrackSchemaId& output) {
	if (schema.name.empty() || schema.name.size() > 256) {
		return false;
	}
	output.set_name(schema.name);
	return ToProto(schema.encoding, *output.mutable_encoding());
}

} // namespace detail
} // namespace core
} // namespace livekit
