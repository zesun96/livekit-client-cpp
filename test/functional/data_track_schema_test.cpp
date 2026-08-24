#include "data_track_proto.h"

#include <gtest/gtest.h>

#include <array>

namespace livekit::core::detail {
namespace {

TEST(DataTrackSchemaTest, RoundTripsWellKnownAndCustomIdentifiers) {
	const std::array<DataTrackSchemaEncoding, 9> encodings = {
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::Unspecified, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::Protobuf, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::Flatbuffer, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::Ros1Message, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::Ros2Message, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::Ros2Idl, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::OmgIdl, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::JsonSchema, {}},
	    DataTrackSchemaEncoding{DataTrackSchemaEncodingKind::Custom, "application/test"},
	};
	for (const auto& encoding : encodings) {
		DataTrackSchemaId input{"telemetry", encoding};
		livekit::DataTrackSchemaId proto;
		ASSERT_TRUE(ToProto(input, proto));
		EXPECT_EQ(FromProto(proto), input);
	}
}

TEST(DataTrackSchemaTest, RejectsInvalidIdentifiersAndExposesProtocolLimit) {
	livekit::DataTrackSchemaId proto;
	EXPECT_FALSE(ToProto({"", {DataTrackSchemaEncodingKind::JsonSchema, {}}}, proto));
	EXPECT_FALSE(
	    ToProto({std::string(257, 'x'), {DataTrackSchemaEncodingKind::JsonSchema, {}}}, proto));
	EXPECT_FALSE(ToProto({"telemetry", {DataTrackSchemaEncodingKind::Custom, {}}}, proto));
	EXPECT_FALSE(
	    ToProto({"telemetry", {DataTrackSchemaEncodingKind::Custom, std::string(33, 'x')}}, proto));
	EXPECT_EQ(kMaximumDataTrackSchemaDefinitionSize, 50u * 1024u);
}

} // namespace
} // namespace livekit::core::detail
