/**
 *
 * Copyright (c) 2024 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#pragma once

#ifndef _LKC_CORE_TRACK_LOCAL_VIDEO_TRACK_H_
#define _LKC_CORE_TRACK_LOCAL_VIDEO_TRACK_H_

#include "local_track.h"

#include "video_source.h"
#include "video_track.h"

#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_interface.h"

#include <map>
#include <mutex>
#include <vector>

namespace livekit {
namespace core {

class LocalVideoTrack : public LocalTrack {
public:
	struct AdditionalCodecSender {
		VideoCodec codec;
		webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver;
		std::vector<webrtc::RtpEncodingParameters> encodings;
	};

	LocalVideoTrack(std::string name, std::unique_ptr<VideoTrack> video_track,
	                VideoSourceInterface* source);
	~LocalVideoTrack() override = default;
	VideoSourceInterface* source() const { return source_; }
	void SetEnabled(bool enabled) override;
	bool HasAdditionalCodec(VideoCodec codec) const;
	bool AddAdditionalCodec(VideoCodec codec, std::unique_ptr<VideoTrack> media_track,
	                        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver,
	                        std::vector<webrtc::RtpEncodingParameters> encodings);
	std::vector<AdditionalCodecSender> AdditionalCodecs() const;
	bool UpdateAdditionalCodecEncodings(VideoCodec codec,
	                                    std::vector<webrtc::RtpEncodingParameters> encodings);
	void ClearAdditionalCodecs();

private:
	struct AdditionalCodecState {
		std::unique_ptr<VideoTrack> media_track;
		webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver;
		std::vector<webrtc::RtpEncodingParameters> encodings;
	};

	VideoSourceInterface* source_;
	mutable std::mutex additional_codecs_mutex_;
	std::map<VideoCodec, AdditionalCodecState> additional_codecs_;
};

} // namespace core
} // namespace livekit

#endif //
