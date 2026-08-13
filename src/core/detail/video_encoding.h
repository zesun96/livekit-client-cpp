#pragma once

#ifndef _LKC_CORE_DETAIL_VIDEO_ENCODING_H_
#define _LKC_CORE_DETAIL_VIDEO_ENCODING_H_

#include "livekit/core/option/media_option.h"
#include "livekit/core/track/subscribed_quality.h"

#include "api/rtp_parameters.h"
#include "livekit_models.pb.h"

#include <cstdint>
#include <vector>

namespace livekit {
namespace core {

struct VideoEncodingPlan {
	std::vector<webrtc::RtpEncodingParameters> encodings;
	std::vector<livekit::VideoLayer> layers;
};

const char* VideoCodecName(VideoCodec codec);
VideoEncodingPlan BuildVideoEncodingPlan(uint32_t width, uint32_t height, bool screen_share,
                                         const TrackPublishOptions& options);
bool ApplySubscribedQualities(std::vector<webrtc::RtpEncodingParameters>& encodings,
                              const SubscribedQualityUpdate& update,
                              const std::string& published_codec);

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_VIDEO_ENCODING_H_
