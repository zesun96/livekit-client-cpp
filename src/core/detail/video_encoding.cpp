#include "video_encoding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>

namespace livekit {
namespace core {
namespace {

struct Preset {
	uint32_t width;
	uint32_t height;
	uint64_t bitrate;
	double framerate;
};

constexpr std::array<const char*, 3> kVideoRids{"q", "h", "f"};
constexpr std::array<Preset, 8> kWidePresets{{
    {160, 90, 90000, 20},
    {320, 180, 160000, 20},
    {384, 216, 180000, 20},
    {640, 360, 450000, 20},
    {960, 540, 800000, 25},
    {1280, 720, 1700000, 30},
    {1920, 1080, 3000000, 30},
    {3840, 2160, 8000000, 30},
}};
constexpr std::array<Preset, 8> kStandardPresets{{
    {160, 120, 70000, 20},
    {240, 180, 125000, 20},
    {320, 240, 140000, 20},
    {480, 360, 330000, 20},
    {640, 480, 500000, 20},
    {960, 720, 1300000, 30},
    {1440, 1080, 2300000, 30},
    {1920, 1440, 3800000, 30},
}};

bool IsWide(uint32_t width, uint32_t height) {
	const auto longer = static_cast<double>(std::max(width, height));
	const auto shorter = static_cast<double>(std::min(width, height));
	if (shorter == 0) {
		return true;
	}
	const auto aspect = longer / shorter;
	return std::abs(aspect - 16.0 / 9.0) < std::abs(aspect - 4.0 / 3.0);
}

Preset OriginalPreset(uint32_t width, uint32_t height, bool screen_share,
                      const TrackPublishOptions& options) {
	Preset result;
	if (screen_share) {
		result = {width, height, 2500000, 15};
	} else if (IsWide(width, height)) {
		const auto size = std::max(width, height);
		for (const auto& preset : kWidePresets) {
			if (preset.width >= size) {
				result = {width, height, preset.bitrate, preset.framerate};
				break;
			}
		}
		if (result.bitrate == 0) {
			result = {width, height, kWidePresets.back().bitrate, kWidePresets.back().framerate};
		}
	} else {
		const auto size = std::max(width, height);
		for (const auto& preset : kStandardPresets) {
			if (preset.width >= size) {
				result = {width, height, preset.bitrate, preset.framerate};
				break;
			}
		}
		if (result.bitrate == 0) {
			result = {width, height, kStandardPresets.back().bitrate,
			          kStandardPresets.back().framerate};
		}
	}
	if (options.video_encoding.max_bitrate != 0) {
		result.bitrate = options.video_encoding.max_bitrate;
	}
	if (options.video_encoding.max_framerate > 0) {
		result.framerate = options.video_encoding.max_framerate;
	}
	return result;
}

VideoQuality QualityForRid(const std::string& rid) {
	if (rid == "q") {
		return VideoQuality::Low;
	}
	if (rid == "h") {
		return VideoQuality::Medium;
	}
	return VideoQuality::High;
}

std::string NormalizeCodec(std::string codec) {
	std::transform(codec.begin(), codec.end(), codec.begin(),
	               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	constexpr const char* kVideoPrefix = "video/";
	if (codec.starts_with(kVideoPrefix)) {
		codec.erase(0, std::char_traits<char>::length(kVideoPrefix));
	}
	return codec;
}

} // namespace

const char* VideoCodecName(VideoCodec codec) {
	switch (codec) {
	case VideoCodec::H264:
		return "H264";
	case VideoCodec::VP9:
		return "VP9";
	case VideoCodec::AV1:
		return "AV1";
	case VideoCodec::VP8:
	default:
		return "VP8";
	}
}

VideoEncodingPlan BuildVideoEncodingPlan(uint32_t width, uint32_t height, bool screen_share,
                                         const TrackPublishOptions& options) {
	VideoEncodingPlan result;
	if (width == 0 || height == 0) {
		return result;
	}
	const auto original = OriginalPreset(width, height, screen_share, options);
	std::vector<Preset> presets;
	const bool supports_simulcast =
	    options.video_codec == VideoCodec::VP8 || options.video_codec == VideoCodec::H264;
	const bool use_simulcast = options.simulcast && supports_simulcast;
	if (use_simulcast && std::max(width, height) >= 480) {
		if (screen_share) {
			presets.push_back({std::max(1u, width / 2), std::max(1u, height / 2),
			                   std::max<uint64_t>(150000, original.bitrate / 4),
			                   original.framerate});
		} else if (IsWide(width, height)) {
			presets.push_back(kWidePresets[1]);
			if (std::max(width, height) >= 960) {
				presets.push_back(kWidePresets[3]);
			}
		} else {
			presets.push_back(kStandardPresets[1]);
			if (std::max(width, height) >= 960) {
				presets.push_back(kStandardPresets[3]);
			}
		}
	}
	presets.push_back(original);
	for (std::size_t index = 0; index < presets.size(); ++index) {
		const auto& preset = presets[index];
		webrtc::RtpEncodingParameters encoding;
		if (use_simulcast) {
			encoding.rid = kVideoRids[index];
		}
		const auto scale = std::max(
		    1.0, static_cast<double>(std::min(width, height)) /
		             static_cast<double>(std::max(1u, std::min(preset.width, preset.height))));
		encoding.scale_resolution_down_by = scale;
		if (preset.bitrate != 0) {
			encoding.max_bitrate_bps = static_cast<int>(
			    std::min<uint64_t>(preset.bitrate, static_cast<uint64_t>(INT32_MAX)));
		}
		const auto framerate = original.framerate > 0 && preset.framerate > 0
		                           ? std::min(original.framerate, preset.framerate)
		                           : preset.framerate;
		if (framerate > 0) {
			encoding.max_framerate = framerate;
		}
		result.encodings.push_back(encoding);

		livekit::VideoLayer layer;
		layer.set_quality(static_cast<livekit::VideoQuality>(
		    static_cast<int>(use_simulcast ? QualityForRid(encoding.rid) : VideoQuality::High)));
		layer.set_width(static_cast<uint32_t>(std::ceil(width / scale)));
		layer.set_height(static_cast<uint32_t>(std::ceil(height / scale)));
		layer.set_bitrate(static_cast<uint32_t>(
		    std::min<uint64_t>(preset.bitrate, static_cast<uint64_t>(UINT32_MAX))));
		layer.set_rid(encoding.rid);
		result.layers.push_back(std::move(layer));
	}
	return result;
}

bool ApplySubscribedQualities(std::vector<webrtc::RtpEncodingParameters>& encodings,
                              const SubscribedQualityUpdate& update,
                              const std::string& published_codec) {
	if (encodings.size() < 2) {
		return false;
	}
	const std::vector<SubscribedQuality>* qualities = nullptr;
	if (!update.codecs.empty()) {
		const auto normalized_codec = NormalizeCodec(published_codec);
		for (const auto& codec : update.codecs) {
			if (NormalizeCodec(codec.codec) == normalized_codec) {
				qualities = &codec.qualities;
				break;
			}
		}
	} else {
		qualities = &update.qualities;
	}
	if (qualities == nullptr || qualities->empty()) {
		return false;
	}
	bool changed = false;
	for (auto& encoding : encodings) {
		if (encoding.rid != "q" && encoding.rid != "h" && encoding.rid != "f") {
			continue;
		}
		const auto quality = QualityForRid(encoding.rid);
		const auto subscribed =
		    std::find_if(qualities->begin(), qualities->end(),
		                 [quality](const auto& candidate) { return candidate.quality == quality; });
		if (subscribed != qualities->end() && encoding.active != subscribed->enabled) {
			encoding.active = subscribed->enabled;
			changed = true;
		}
	}
	return changed;
}

} // namespace core
} // namespace livekit
