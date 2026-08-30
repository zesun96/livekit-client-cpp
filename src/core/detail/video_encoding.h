#pragma once

#ifndef _LKC_CORE_DETAIL_VIDEO_ENCODING_H_
#define _LKC_CORE_DETAIL_VIDEO_ENCODING_H_

#include "livekit/core/option/media_option.h"
#include "livekit/core/track/subscribed_quality.h"

#include "api/rtp_parameters.h"
#include "livekit_models.pb.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace livekit {
namespace core {

struct VideoEncodingPlan {
	std::vector<webrtc::RtpEncodingParameters> encodings;
	std::vector<livekit::VideoLayer> layers;
	livekit::VideoLayer::Mode video_layer_mode = livekit::VideoLayer::MODE_UNUSED;
	bool valid = true;
};

const char* VideoCodecName(VideoCodec codec);
VideoDegradationPreference DefaultVideoDegradationPreference(TrackSource source);
std::optional<webrtc::DegradationPreference>
ToRtcVideoDegradationPreference(VideoDegradationPreference preference);
VideoEncodingPlan BuildVideoEncodingPlan(uint32_t width, uint32_t height, bool screen_share,
                                         const TrackPublishOptions& options);
bool ApplySubscribedQualities(std::vector<webrtc::RtpEncodingParameters>& encodings,
                              const SubscribedQualityUpdate& update,
                              const std::string& published_codec);
bool ApplyVideoEncodingPlan(std::vector<webrtc::RtpEncodingParameters>& encodings,
                            const std::vector<webrtc::RtpEncodingParameters>& planned_encodings);

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_VIDEO_ENCODING_H_
