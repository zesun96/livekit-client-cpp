#pragma once

#ifndef _LKC_CORE_DETAIL_DATA_TRACK_PROTO_H_
#define _LKC_CORE_DETAIL_DATA_TRACK_PROTO_H_

#include "livekit/core/data_track.h"
#include "livekit_models.pb.h"

#include <optional>

namespace livekit {
namespace core {
namespace detail {

DataTrackInfo FromProto(const livekit::DataTrackInfo& info);
bool ToProto(const DataTrackFrameEncoding& encoding, livekit::DataTrackFrameEncoding& output);
bool ToProto(const DataTrackSchemaEncoding& encoding, livekit::DataTrackSchemaEncoding& output);
bool ToProto(const DataTrackSchemaId& schema, livekit::DataTrackSchemaId& output);

} // namespace detail
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_DATA_TRACK_PROTO_H_
