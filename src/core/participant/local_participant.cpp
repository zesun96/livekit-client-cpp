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

#include "local_participant.h"

#include "../data_track.h"
#include "../detail/converted_proto.h"
#include "../detail/data_stream_compression.h"
#include "../detail/data_track_proto.h"
#include "../detail/frame_metadata.h"
#include "../detail/logging.h"
#include "../detail/video_encoding.h"
#include "../e2ee/e2ee_manager_internal.h"
#include "../track/audio_source.h"
#include "../track/audio_track.h"
#include "../track/local_audio_track.h"
#include "../track/local_video_track.h"
#include "../track/microphone_audio_source.h"
#include "../track/video_source.h"
#include "../track/video_track.h"

#include "livekit/core/room_event_interface.h"
#include "livekit_models.pb.h"
#include "rtc_base/crypto_random.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

namespace livekit {
namespace core {

namespace {

bool PublishChatMessage(RtcEngine* engine, const ChatMessage& message) {
	if (engine == nullptr || message.id.empty()) {
		return false;
	}
	livekit::DataPacket packet;
	packet.set_kind(livekit::DataPacket_Kind_RELIABLE);
	auto* chat = packet.mutable_chat_message();
	chat->set_id(message.id);
	chat->set_timestamp(message.timestamp);
	if (message.edit_timestamp.has_value()) {
		chat->set_edit_timestamp(*message.edit_timestamp);
	}
	chat->set_message(message.message);
	chat->set_deleted(message.deleted);
	chat->set_generated(message.generated);
	return engine->SendDataPacket(packet, true);
}

std::optional<VideoCodec> ParseBackupVideoCodec(std::string codec) {
	const auto slash = codec.find('/');
	if (slash != std::string::npos) {
		codec.erase(0, slash + 1);
	}
	std::transform(codec.begin(), codec.end(), codec.begin(),
	               [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
	if (codec == "VP8") {
		return VideoCodec::VP8;
	}
	if (codec == "H264") {
		return VideoCodec::H264;
	}
	return std::nullopt;
}

bool IsSupportedBackupVideoCodec(VideoCodec codec) {
	return codec == VideoCodec::VP8 || codec == VideoCodec::H264;
}

livekit::BackupCodecPolicy ToProtoBackupCodecPolicy(BackupCodecPolicy policy) {
	return static_cast<livekit::BackupCodecPolicy>(static_cast<int>(policy));
}

bool ApplyVideoDegradationPreference(
    const webrtc::scoped_refptr<webrtc::RtpSenderInterface>& sender,
    VideoDegradationPreference preference) {
	const auto converted = ToRtcVideoDegradationPreference(preference);
	if (sender == nullptr || !converted) {
		return false;
	}
	auto parameters = sender->GetParameters();
	parameters.degradation_preference = *converted;
	return sender->SetParameters(parameters).ok();
}

} // namespace

class OutgoingDataStreamState {
public:
	explicit OutgoingDataStreamState(RtcEngine* engine) : engine_(engine) {}

	bool Send(const livekit::DataPacket& packet) {
		std::lock_guard<std::mutex> guard(mutex_);
		return engine_ != nullptr && engine_->SendDataPacket(packet, true);
	}

	void Invalidate() {
		std::lock_guard<std::mutex> guard(mutex_);
		engine_ = nullptr;
	}

private:
	std::mutex mutex_;
	RtcEngine* engine_;
};

namespace {

constexpr std::size_t kMaximumDataStreamChunkSize = 15'000;
constexpr uint64_t kMaximumCompressedDataStreamSize = 64ULL * 1024 * 1024;

int64_t CurrentTimestampMilliseconds() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

template <typename Options>
void PopulateStreamPacket(livekit::DataPacket& packet, const Options& options) {
	for (const auto& identity : options.destination_identities) {
		packet.add_destination_identities(identity);
	}
}

template <typename Options>
void PopulateStreamHeader(livekit::DataStream_Header& header, const std::string& stream_id,
                          uint64_t total_length, const Options& options) {
	header.set_stream_id(stream_id);
	header.set_timestamp(CurrentTimestampMilliseconds());
	header.set_topic(options.topic);
	header.set_total_length(total_length);
	header.set_compression(options.compress ? livekit::DataStream_CompressionType_DEFLATE_RAW
	                                        : livekit::DataStream_CompressionType_NONE);
	for (const auto& [key, value] : options.attributes) {
		(*header.mutable_attributes())[key] = value;
	}
}

template <typename Options>
bool SendStreamChunks(RtcEngine* engine, const std::string& stream_id, const uint8_t* data,
                      std::size_t size, const Options& options, uint64_t& chunk_index) {
	if (engine == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize || (data == nullptr && size != 0)) {
		return false;
	}
	for (std::size_t offset = 0; offset < size; offset += options.chunk_size) {
		const auto count = std::min(options.chunk_size, size - offset);
		livekit::DataPacket chunk_packet;
		PopulateStreamPacket(chunk_packet, options);
		auto* chunk = chunk_packet.mutable_stream_chunk();
		chunk->set_stream_id(stream_id);
		chunk->set_chunk_index(chunk_index++);
		chunk->set_content(data + offset, count);
		if (!engine->SendDataPacket(chunk_packet, true)) {
			return false;
		}
	}
	return true;
}

template <typename Options>
bool SendStreamPayload(RtcEngine* engine, const std::string& stream_id, const uint8_t* data,
                       std::size_t size, const Options& options) {
	if (options.compress && size > kMaximumCompressedDataStreamSize) {
		return false;
	}
	std::vector<uint8_t> compressed;
	if (options.compress) {
		detail::DeflateRawStream deflater;
		if (!deflater.IsValid()) {
			return false;
		}
		for (std::size_t offset = 0; offset < size; offset += options.chunk_size) {
			const auto count = std::min(options.chunk_size, size - offset);
			if (!deflater.Write(data + offset, count, compressed)) {
				return false;
			}
		}
		if (!deflater.Finish(compressed)) {
			return false;
		}
		data = compressed.data();
		size = compressed.size();
	}
	uint64_t chunk_index = 0;
	if (!SendStreamChunks(engine, stream_id, data, size, options, chunk_index)) {
		return false;
	}

	livekit::DataPacket trailer_packet;
	PopulateStreamPacket(trailer_packet, options);
	trailer_packet.mutable_stream_trailer()->set_stream_id(stream_id);
	return engine->SendDataPacket(trailer_packet, true);
}

template <typename Info>
void PopulateStreamingHeader(livekit::DataStream_Header& header, const Info& info, bool compress) {
	header.set_stream_id(info.stream_id);
	header.set_timestamp(info.timestamp);
	header.set_topic(info.topic);
	header.set_mime_type(info.mime_type);
	header.set_compression(compress ? livekit::DataStream_CompressionType_DEFLATE_RAW
	                                : livekit::DataStream_CompressionType_NONE);
	if (info.total_size.has_value()) {
		header.set_total_length(*info.total_size);
	}
	for (const auto& [key, value] : info.attributes) {
		(*header.mutable_attributes())[key] = value;
	}
}

bool SendStreamTrailer(const std::shared_ptr<OutgoingDataStreamState>& state,
                       const std::vector<std::string>& destination_identities,
                       const std::string& stream_id, const std::string& reason) {
	livekit::DataPacket packet;
	for (const auto& identity : destination_identities) {
		packet.add_destination_identities(identity);
	}
	auto* trailer = packet.mutable_stream_trailer();
	trailer->set_stream_id(stream_id);
	trailer->set_reason(reason);
	return state && state->Send(packet);
}

class OutgoingStreamWriterBase {
public:
	OutgoingStreamWriterBase(std::shared_ptr<OutgoingDataStreamState> state, DataStreamInfo info,
	                         std::vector<std::string> destination_identities,
	                         std::size_t chunk_size, int32_t version,
	                         DataStreamProgressHandler progress, bool compress)
	    : state_(std::move(state)), info_(std::move(info)),
	      destination_identities_(std::move(destination_identities)), chunk_size_(chunk_size),
	      version_(version), progress_(std::move(progress)),
	      deflater_(compress ? std::make_unique<detail::DeflateRawStream>() : nullptr) {}

	virtual ~OutgoingStreamWriterBase() { CancelInternal("writer destroyed before close"); }

	bool IsClosedInternal() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return closed_;
	}

	bool WriteBytes(const uint8_t* data, std::size_t size, bool preserve_utf8_boundaries) {
		DataStreamProgressHandler progress;
		uint64_t bytes_sent = 0;
		std::optional<uint64_t> total_size;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (closed_ || (data == nullptr && size != 0) ||
			    (deflater_ && (bytes_sent_ > kMaximumCompressedDataStreamSize ||
			                   size > kMaximumCompressedDataStreamSize - bytes_sent_))) {
				return false;
			}
			if (deflater_) {
				if (!deflater_->IsValid()) {
					closed_ = true;
					return false;
				}
				std::vector<uint8_t> output;
				if (!deflater_->Write(data, size, output) ||
				    !SendBytes(output.data(), output.size())) {
					closed_ = true;
					return false;
				}
				bytes_sent_ += size;
			} else {
				for (std::size_t offset = 0; offset < size;) {
					std::size_t count = std::min(chunk_size_, size - offset);
					if (preserve_utf8_boundaries && offset + count < size) {
						while (count > 0 && (data[offset + count] & 0xc0U) == 0x80U) {
							--count;
						}
						if (count == 0) {
							return false;
						}
					}
					livekit::DataPacket packet;
					for (const auto& identity : destination_identities_) {
						packet.add_destination_identities(identity);
					}
					auto* chunk = packet.mutable_stream_chunk();
					chunk->set_stream_id(info_.stream_id);
					chunk->set_chunk_index(next_chunk_++);
					chunk->set_version(version_);
					chunk->set_content(data + offset, count);
					if (!state_ || !state_->Send(packet)) {
						closed_ = true;
						return false;
					}
					offset += count;
					bytes_sent_ += count;
				}
			}
			progress = progress_;
			bytes_sent = bytes_sent_;
			total_size = info_.total_size;
		}
		if (progress) {
			progress(bytes_sent, total_size);
		}
		return true;
	}

	bool CloseInternal() {
		bool complete = true;
		DataStreamProgressHandler progress;
		uint64_t bytes_sent = 0;
		std::optional<uint64_t> total_size;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (closed_) {
				return false;
			}
			closed_ = true;
			complete = !info_.total_size.has_value() || bytes_sent_ == *info_.total_size;
			if (deflater_) {
				std::vector<uint8_t> output;
				if (!deflater_->Finish(output) || !SendBytes(output.data(), output.size())) {
					return false;
				}
			}
			if (!SendStreamTrailer(state_, destination_identities_, info_.stream_id,
			                       complete ? "" : "incomplete stream")) {
				return false;
			}
			progress = progress_;
			bytes_sent = bytes_sent_;
			total_size = info_.total_size;
		}
		if (complete && progress) {
			progress(bytes_sent, total_size);
		}
		return complete;
	}

	bool CancelInternal(std::string reason) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (closed_) {
			return false;
		}
		closed_ = true;
		if (reason.empty()) {
			reason = "cancelled";
		}
		return SendStreamTrailer(state_, destination_identities_, info_.stream_id, reason);
	}

protected:
	DataStreamInfo info_;

private:
	bool SendBytes(const uint8_t* data, std::size_t size) {
		for (std::size_t offset = 0; offset < size; offset += chunk_size_) {
			const auto count = std::min(chunk_size_, size - offset);
			livekit::DataPacket packet;
			for (const auto& identity : destination_identities_) {
				packet.add_destination_identities(identity);
			}
			auto* chunk = packet.mutable_stream_chunk();
			chunk->set_stream_id(info_.stream_id);
			chunk->set_chunk_index(next_chunk_++);
			chunk->set_version(version_);
			chunk->set_content(data + offset, count);
			if (!state_ || !state_->Send(packet)) {
				return false;
			}
		}
		return true;
	}

	std::shared_ptr<OutgoingDataStreamState> state_;
	std::vector<std::string> destination_identities_;
	std::size_t chunk_size_;
	int32_t version_;
	DataStreamProgressHandler progress_;
	std::unique_ptr<detail::DeflateRawStream> deflater_;
	mutable std::mutex mutex_;
	uint64_t next_chunk_ = 0;
	uint64_t bytes_sent_ = 0;
	bool closed_ = false;
};

class TextStreamWriter final : public TextStreamWriterInterface, private OutgoingStreamWriterBase {
public:
	TextStreamWriter(std::shared_ptr<OutgoingDataStreamState> state, TextStreamInfo info,
	                 StreamTextOptions options)
	    : OutgoingStreamWriterBase(
	          std::move(state), info, std::move(options.destination_identities), options.chunk_size,
	          options.version, std::move(options.on_progress), options.compress),
	      text_info_(std::move(info)) {}

	TextStreamInfo Info() const override { return text_info_; }
	bool Write(const std::string& text) override {
		return WriteBytes(reinterpret_cast<const uint8_t*>(text.data()), text.size(), true);
	}
	bool Close() override { return CloseInternal(); }
	bool Cancel(std::string reason) override { return CancelInternal(std::move(reason)); }
	bool IsClosed() const override { return IsClosedInternal(); }

private:
	TextStreamInfo text_info_;
};

class ByteStreamWriter final : public ByteStreamWriterInterface, private OutgoingStreamWriterBase {
public:
	ByteStreamWriter(std::shared_ptr<OutgoingDataStreamState> state, ByteStreamInfo info,
	                 StreamBytesOptions options)
	    : OutgoingStreamWriterBase(std::move(state), info,
	                               std::move(options.destination_identities), options.chunk_size, 0,
	                               std::move(options.on_progress), options.compress),
	      byte_info_(std::move(info)) {}

	ByteStreamInfo Info() const override { return byte_info_; }
	bool Write(const std::vector<uint8_t>& data) override {
		return WriteBytes(data.data(), data.size(), false);
	}
	bool Close() override { return CloseInternal(); }
	bool Cancel(std::string reason) override { return CancelInternal(std::move(reason)); }
	bool IsClosed() const override { return IsClosedInternal(); }

private:
	ByteStreamInfo byte_info_;
};

} // namespace

LocalParticipant::LocalParticipant(std::string sid, std::string identity,
                                   EncryptionType encryption_type, RtcEngine* engine,
                                   RoomOptions options)
    : engine_(engine), options_(options), encryption_type_(encryption_type),
      Participant(std::move(sid), std::move(identity), "", "",
                  std::map<std::string, std::string>{}),
      outgoing_stream_state_(std::make_shared<OutgoingDataStreamState>(engine)) {
	is_local_participant_ = true;
	backup_codec_worker_ = std::thread([this] { RunBackupCodecWorker(); });
	preconnect_worker_ = std::thread([this] { RunPreconnectWorker(); });
}

LocalParticipant::~LocalParticipant() {
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		if (auto* audio_track = dynamic_cast<LocalAudioTrack*>(publication->Track())) {
			audio_track->DiscardPreconnectBuffer();
		}
	}
	{
		std::lock_guard<std::mutex> guard(backup_codec_mutex_);
		stop_backup_codec_worker_ = true;
		backup_codec_requests_.clear();
	}
	backup_codec_cv_.notify_all();
	if (backup_codec_worker_.joinable()) {
		backup_codec_worker_.join();
	}
	{
		std::lock_guard<std::mutex> guard(preconnect_mutex_);
		stop_preconnect_worker_ = true;
		preconnect_flush_requests_.clear();
	}
	preconnect_cv_.notify_all();
	if (preconnect_worker_.joinable()) {
		preconnect_worker_.join();
	}
	outgoing_stream_state_->Invalidate();
}

void LocalParticipant::UpdateFromInfo(const livekit::ParticipantInfo& info) {
	const auto owned_publications = TrackPublicationsSnapshot();
	const auto owned_data_tracks = DataTracksSnapshot();
	Participant::UpdateFromInfo(info);
	// Participant snapshots can briefly omit a newly published local track while negotiation is
	// settling. Keep client-owned publications (and their LocalTrack pointers) until an explicit
	// unpublish operation or TrackUnpublishedResponse removes them.
	for (const auto& [sid, publication] : owned_publications) {
		if (dynamic_cast<LocalTrackPublication*>(publication.get()) != nullptr) {
			AddTrackPublication(publication);
		}
	}
	for (const auto& [sid, track] : owned_data_tracks) {
		if (dynamic_cast<LocalDataTrack*>(track.get()) != nullptr &&
		    GetDataTrackBySid(sid) == nullptr) {
			AddDataTrack(track);
		}
	}
	std::lock_guard<std::mutex> guard(participant_mutex_);
	is_local_participant_ = true;
}

void LocalParticipant::UpdateFromInfoPreservingTracks(const livekit::ParticipantInfo& info) {
	UpdateInfoFields(info);
	std::lock_guard<std::mutex> guard(participant_mutex_);
	is_local_participant_ = true;
}

LocalTrackInterface* LocalParticipant::CreateLocalAudioTreack(std::string label,
                                                              AudioSourceInterface* source) {
	if (engine_ == nullptr || source == nullptr) {
		return nullptr;
	}
	auto* audio_source = dynamic_cast<AudioSource*>(source);
	if (audio_source == nullptr) {
		return nullptr;
	}
	auto peer_transport_factory_ = engine_->GetSessionPeerTransportFactory();
	if (peer_transport_factory_) {
		if (auto* microphone = dynamic_cast<MicrophoneAudioSource*>(source);
		    microphone != nullptr &&
		    !microphone->BindAudioDevice(peer_transport_factory_->GetAudioDevice())) {
			return nullptr;
		}
		auto peer_factory_ = webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(
		    peer_transport_factory_->GetPeerConnectFactory());
		if (!peer_factory_) {
			return nullptr;
		}
		auto uuid = webrtc::CreateRandomUuid();
		auto rtc_audio_track = peer_factory_->CreateAudioTrack(uuid, audio_source->Get().get());
		if (!rtc_audio_track) {
			return nullptr;
		}
		auto audio_track = std::make_unique<AudioTrack>(rtc_audio_track);
		auto local_track = new LocalAudioTrack(label, std::move(audio_track), source);
		return local_track;
	}
	return nullptr;
}

LocalTrackInterface* LocalParticipant::CreateLocalVideoTrack(std::string label,
                                                             VideoSourceInterface* source) {
	if (engine_ == nullptr || source == nullptr) {
		return nullptr;
	}
	auto* video_source = dynamic_cast<VideoSource*>(source);
	if (video_source == nullptr) {
		return nullptr;
	}
	auto peer_transport_factory = engine_->GetSessionPeerTransportFactory();
	if (!peer_transport_factory) {
		return nullptr;
	}
	auto peer_factory = webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(
	    peer_transport_factory->GetPeerConnectFactory());
	if (!peer_factory) {
		return nullptr;
	}
	auto rtc_video_track =
	    peer_factory->CreateVideoTrack(video_source->Get(), webrtc::CreateRandomUuid());
	if (!rtc_video_track) {
		return nullptr;
	}
	auto video_track = std::make_unique<VideoTrack>(std::move(rtc_video_track));
	return new LocalVideoTrack(std::move(label), std::move(video_track), source);
}

bool LocalParticipant::PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	if (engine_ == nullptr || track == nullptr) {
		return false;
	}
	auto* local_track = dynamic_cast<LocalTrack*>(track);
	if (local_track == nullptr || local_track->media_track() == nullptr ||
	    local_track->media_track()->rtc_track() == nullptr) {
		return false;
	}
	auto kind = local_track->Kind();
	auto cid = local_track->media_track()->rtc_track()->id();
	auto req = livekit::AddTrackRequest();
	req.set_cid(cid);
	req.set_name(local_track->Name());
	req.set_type(to_proto(kind));
	req.set_source(to_proto(option.source));
	req.set_muted(local_track->Muted());
	req.set_disable_dtx(!option.dtx);
	req.set_disable_red(!option.red);
	req.set_encryption(to_proto(encryption_type_));
	req.set_stream(option.stream);
	auto* preconnect_track = dynamic_cast<LocalAudioTrack*>(local_track);
	if (option.preconnect_buffer) {
		if (kind != TrackKind::Audio || option.source != TrackSource::Microphone ||
		    preconnect_track == nullptr) {
			return false;
		}
		req.add_audio_features(livekit::TF_PRECONNECT_BUFFER);
	}
	std::shared_ptr<detail::FrameMetadataStore> frame_metadata_store;
	FrameMetadataFeatures frame_metadata_features;
	if (kind == TrackKind::Video) {
		auto* video_track = dynamic_cast<LocalVideoTrack*>(local_track);
		if (video_track == nullptr || video_track->source() == nullptr ||
		    video_track->source()->Width() == 0 || video_track->source()->Height() == 0) {
			return false;
		}
		if (!engine_->SupportsVideoCodec(option.video_codec)) {
			return false;
		}
		option.degradation_preference = option.degradation_preference.value_or(
		    DefaultVideoDegradationPreference(option.source));
		if (!ToRtcVideoDegradationPreference(*option.degradation_preference)) {
			return false;
		}
		if (option.backup_video_codec.has_value() &&
		    *option.backup_video_codec != option.video_codec &&
		    encryption_type_ == EncryptionType::None &&
		    (!IsSupportedBackupVideoCodec(*option.backup_video_codec) ||
		     !engine_->SupportsVideoCodec(*option.backup_video_codec))) {
			return false;
		}
		req.set_width(video_track->source()->Width());
		req.set_height(video_track->source()->Height());
		if (option.frame_metadata_features && option.frame_metadata_features->Any()) {
			frame_metadata_features = *option.frame_metadata_features;
			if (frame_metadata_features.user_timestamp) {
				req.add_packet_trailer_features(livekit::PTF_USER_TIMESTAMP);
			}
			if (frame_metadata_features.frame_id) {
				req.add_packet_trailer_features(livekit::PTF_FRAME_ID);
			}
			if (frame_metadata_features.user_data) {
				req.add_packet_trailer_features(livekit::PTF_USER_DATA);
			}
			if (auto* source = dynamic_cast<VideoSource*>(video_track->source())) {
				frame_metadata_store = source->GetFrameMetadataStore();
			}
			if (!frame_metadata_store) {
				return false;
			}
		}
	}
	VideoEncodingPlan video_encoding_plan;
	if (kind == TrackKind::Video) {
		video_encoding_plan = BuildVideoEncodingPlan(
		    req.width(), req.height(), option.source == TrackSource::ScreenShare, option);
		if (!video_encoding_plan.valid) {
			return false;
		}
		for (const auto& layer : video_encoding_plan.layers) {
			req.add_layers()->CopyFrom(layer);
		}
		auto* codec = req.add_simulcast_codecs();
		codec->set_codec(VideoCodecName(option.video_codec));
		codec->set_cid(cid);
		codec->set_video_layer_mode(video_encoding_plan.video_layer_mode);
		for (const auto& layer : video_encoding_plan.layers) {
			codec->add_layers()->CopyFrom(layer);
		}
		if (option.backup_video_codec.has_value() &&
		    *option.backup_video_codec != option.video_codec &&
		    encryption_type_ == EncryptionType::None) {
			req.set_backup_codec_policy(ToProtoBackupCodecPolicy(option.backup_codec_policy));
			auto* backup_codec = req.add_simulcast_codecs();
			backup_codec->set_codec(VideoCodecName(*option.backup_video_codec));
		}
	}
	bool preconnect_started = false;
	if (option.preconnect_buffer) {
		if (!preconnect_track->StartPreconnectBuffer(
		        [this] { NotifyPreconnectAudioAvailable(); })) {
			return false;
		}
		preconnect_started = preconnect_track->HasPreconnectBuffer();
	}
	auto discard_failed_preconnect = [&] {
		if (preconnect_started) {
			preconnect_track->DiscardPreconnectBuffer();
			preconnect_started = false;
		}
	};

	try {
		LKC_LOG_INFO << "publishing track: name=" << req.name() << ", kind=" << req.type();
		auto option_ti = engine_->AddTrack(req);
		if (!option_ti.has_value()) {
			discard_failed_preconnect();
			return false;
		}
		auto& ti = option_ti.value();
		std::string primary_codec_mime;
		const auto& codecs = ti.codecs();
		if (!codecs.empty()) {
			primary_codec_mime = codecs.Get(0).mime_type();
		}

		if (!primary_codec_mime.empty() && kind == TrackKind::Video) {
		}

		auto publication = std::make_shared<LocalTrackPublication>(ti, local_track);
		local_track->UpdateInfo(ti);
		auto transceiver =
		    engine_->CreateSender(local_track, option, video_encoding_plan.encodings);

		if (!transceiver) {
			discard_failed_preconnect();
			return false;
		}
		if (kind == TrackKind::Video &&
		    !ApplyVideoDegradationPreference(transceiver->sender(),
		                                     *option.degradation_preference)) {
			engine_->RemoveSender(transceiver);
			discard_failed_preconnect();
			return false;
		}
		if (e2ee_manager_ != nullptr &&
		    !E2EEManagerNativeAccess::AttachSender(*e2ee_manager_, local_track->Sid(), Identity(),
		                                           kind, transceiver->sender(),
		                                           frame_metadata_store, frame_metadata_features)) {
			engine_->RemoveSender(local_track);
			discard_failed_preconnect();
			return false;
		}
		if (e2ee_manager_ == nullptr && frame_metadata_store) {
			transceiver->sender()->SetFrameTransformer(detail::CreateFrameMetadataTransformer(
			    true, std::move(frame_metadata_store), frame_metadata_features));
		}
		local_track->SetTransceiver(transceiver);

		engine_->PublisherNegotiationNeeded();

		publication->UpdatePublishOptions(option);

		this->AddTrackPublication(publication);

		local_track->SetEnabled(true);
		if (auto* listener = event_listener_.load()) {
			listener->OnLocalTrackPublished(publication.get(), this);
			bool emit_subscribed = false;
			{
				std::lock_guard<std::mutex> guard(local_track_subscriptions_mutex_);
				emit_subscribed =
				    subscribed_local_track_sids_.count(publication->Sid()) != 0 &&
				    emitted_local_track_subscriptions_.insert(publication->Sid()).second;
			}
			if (emit_subscribed) {
				listener->OnLocalTrackSubscribed(publication.get(), this);
			}
		}
		TryQueuePreconnectBuffers();

	} catch (const std::exception& e) {
		discard_failed_preconnect();
		LKC_LOG_ERROR << "failed to publish track: " << e.what();
		return false;
	} catch (...) {
		discard_failed_preconnect();
		LKC_LOG_ERROR << "failed to publish track: unknown error";
		return false;
	}
	return true;
}

bool LocalParticipant::UpdateVideoEncoding(LocalTrackInterface* track, VideoEncoding encoding,
                                           bool backup_codec) {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	if (track == nullptr || !std::isfinite(encoding.max_framerate) ||
	    encoding.max_framerate < 0.0F) {
		return false;
	}
	auto* local_track = dynamic_cast<LocalVideoTrack*>(track);
	if (local_track == nullptr || local_track->source() == nullptr || local_track->Sid().empty()) {
		return false;
	}
	const auto publications = TrackPublicationsSnapshot();
	const auto publication = publications.find(local_track->Sid());
	if (publication == publications.end() || publication->second->Track() != local_track) {
		return false;
	}
	auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication->second.get());
	if (local_publication == nullptr) {
		return false;
	}
	auto updated_options = local_publication->PublishOptions();
	auto encoding_options = updated_options;
	webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver;
	VideoCodec codec = updated_options.video_codec;
	if (backup_codec) {
		if (!updated_options.backup_video_codec.has_value() ||
		    *updated_options.backup_video_codec == updated_options.video_codec) {
			return false;
		}
		codec = *updated_options.backup_video_codec;
		updated_options.backup_video_encoding = encoding;
		encoding_options.video_codec = codec;
		encoding_options.video_encoding = encoding;
		if (encoding_options.source == TrackSource::ScreenShare) {
			encoding_options.simulcast = false;
		}
		for (const auto& additional : local_track->AdditionalCodecs()) {
			if (additional.codec == codec) {
				transceiver = additional.transceiver;
				break;
			}
		}
	} else {
		updated_options.video_encoding = encoding;
		encoding_options.video_encoding = encoding;
		transceiver = local_track->Transceiver();
	}
	const auto encoding_plan = BuildVideoEncodingPlan(
	    local_track->source()->Width(), local_track->source()->Height(),
	    encoding_options.source == TrackSource::ScreenShare, encoding_options);
	if (!encoding_plan.valid || encoding_plan.encodings.empty()) {
		return false;
	}
	if (transceiver == nullptr) {
		if (!backup_codec) {
			return false;
		}
		local_publication->UpdatePublishOptions(std::move(updated_options));
		return true;
	}
	auto sender = transceiver->sender();
	if (sender == nullptr) {
		return false;
	}
	auto parameters = sender->GetParameters();
	if (!ApplyVideoEncodingPlan(parameters.encodings, encoding_plan.encodings) ||
	    !sender->SetParameters(parameters).ok()) {
		return false;
	}
	if (backup_codec &&
	    !local_track->UpdateAdditionalCodecEncodings(codec, encoding_plan.encodings)) {
		return false;
	}
	local_publication->UpdatePublishOptions(std::move(updated_options));
	return true;
}

bool LocalParticipant::UpdateVideoDegradationPreference(LocalTrackInterface* track,
                                                        VideoDegradationPreference preference) {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	const auto converted = ToRtcVideoDegradationPreference(preference);
	auto* local_track = dynamic_cast<LocalVideoTrack*>(track);
	if (!converted || local_track == nullptr || local_track->Sid().empty()) {
		return false;
	}
	const auto publications = TrackPublicationsSnapshot();
	const auto publication = publications.find(local_track->Sid());
	if (publication == publications.end() || publication->second->Track() != local_track) {
		return false;
	}
	auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication->second.get());
	if (local_publication == nullptr || local_track->Transceiver() == nullptr) {
		return false;
	}

	std::vector<webrtc::scoped_refptr<webrtc::RtpSenderInterface>> senders;
	senders.push_back(local_track->Transceiver()->sender());
	for (const auto& additional : local_track->AdditionalCodecs()) {
		if (additional.transceiver != nullptr) {
			senders.push_back(additional.transceiver->sender());
		}
	}
	if (std::any_of(senders.begin(), senders.end(),
	                [](const auto& sender) { return sender == nullptr; })) {
		return false;
	}

	std::vector<webrtc::RtpParameters> originals;
	originals.reserve(senders.size());
	for (std::size_t index = 0; index < senders.size(); ++index) {
		auto parameters = senders[index]->GetParameters();
		originals.push_back(parameters);
		parameters.degradation_preference = *converted;
		if (!senders[index]->SetParameters(parameters).ok()) {
			for (std::size_t rollback = 0; rollback < index; ++rollback) {
				senders[rollback]->SetParameters(originals[rollback]);
			}
			return false;
		}
	}

	auto options = local_publication->PublishOptions();
	options.degradation_preference = preference;
	local_publication->UpdatePublishOptions(std::move(options));
	return true;
}

bool LocalParticipant::UnpublishTrack(LocalTrackInterface* track, bool stop_on_unpublish) {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	if (engine_ == nullptr || track == nullptr) {
		return false;
	}
	auto* local_track = dynamic_cast<LocalTrack*>(track);
	if (local_track == nullptr || local_track->Sid().empty() || !local_track->Transceiver()) {
		return false;
	}
	auto publications = TrackPublicationsSnapshot();
	auto publication = publications.find(local_track->Sid());
	if (publication == publications.end() || publication->second->Track() != local_track) {
		return false;
	}
	if (!engine_->RemoveSender(local_track)) {
		return false;
	}
	if (auto* video_track = dynamic_cast<LocalVideoTrack*>(local_track); video_track != nullptr) {
		for (const auto& additional : video_track->AdditionalCodecs()) {
			engine_->RemoveSender(additional.transceiver);
		}
		video_track->ClearAdditionalCodecs();
	}
	if (e2ee_manager_ != nullptr) {
		E2EEManagerNativeAccess::Detach(*e2ee_manager_, local_track->Sid(),
		                                FrameCryptorDirection::Sender);
	}
	if (auto* audio_track = dynamic_cast<LocalAudioTrack*>(local_track)) {
		audio_track->DiscardPreconnectBuffer();
	}

	local_track->SetTransceiver(nullptr);
	RemoveTrackPublication(local_track->Sid());
	{
		std::lock_guard<std::mutex> guard(local_track_subscriptions_mutex_);
		subscribed_local_track_sids_.erase(local_track->Sid());
		emitted_local_track_subscriptions_.erase(local_track->Sid());
	}
	if (stop_on_unpublish) {
		local_track->SetEnabled(false);
	}
	engine_->PublisherNegotiationNeeded();
	if (auto* listener = event_listener_.load()) {
		listener->OnLocalTrackUnpublished(publication->second.get(), this);
	}
	return true;
}

void LocalParticipant::LocalTrackSubscribed(const std::string& track_sid) {
	if (track_sid.empty()) {
		return;
	}
	{
		std::lock_guard<std::mutex> guard(local_track_subscriptions_mutex_);
		subscribed_local_track_sids_.insert(track_sid);
	}
	auto publications = TrackPublicationsSnapshot();
	auto publication = publications.find(track_sid);
	if (publication == publications.end()) {
		return;
	}
	{
		std::lock_guard<std::mutex> guard(local_track_subscriptions_mutex_);
		if (!emitted_local_track_subscriptions_.insert(track_sid).second) {
			return;
		}
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnLocalTrackSubscribed(publication->second.get(), this);
	}
	TryQueuePreconnectBuffers();
}

void LocalParticipant::UpdateAgentIdentities(std::vector<std::string> identities) {
	{
		std::lock_guard<std::mutex> guard(preconnect_mutex_);
		agent_identities_.clear();
		for (auto& identity : identities) {
			if (!identity.empty()) {
				agent_identities_.insert(std::move(identity));
			}
		}
	}
	TryQueuePreconnectBuffers();
}

void LocalParticipant::TryQueuePreconnectBuffers() {
	std::vector<std::string> destinations;
	{
		std::lock_guard<std::mutex> guard(preconnect_mutex_);
		if (stop_preconnect_worker_ || agent_identities_.empty()) {
			return;
		}
		destinations.assign(agent_identities_.begin(), agent_identities_.end());
	}
	std::set<std::string> subscribed;
	{
		std::lock_guard<std::mutex> guard(local_track_subscriptions_mutex_);
		subscribed = subscribed_local_track_sids_;
	}
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		if (subscribed.count(sid) == 0) {
			continue;
		}
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication.get());
		auto* audio_track = local_publication != nullptr
		                        ? dynamic_cast<LocalAudioTrack*>(local_publication->Track())
		                        : nullptr;
		if (audio_track == nullptr || !local_publication->PublishOptions().preconnect_buffer) {
			continue;
		}
		if (!audio_track->HasPreconnectBuffer()) {
			audio_track->DiscardPreconnectBuffer();
			continue;
		}
		if (audio_track->PreconnectBufferSize() == 0) {
			continue;
		}
		auto data = audio_track->TakePreconnectBuffer();
		if (!data || data->bytes.empty()) {
			continue;
		}
		PreconnectFlushRequest request;
		request.track_sid = sid;
		request.bytes = std::move(data->bytes);
		request.sample_rate = data->sample_rate;
		request.channels = data->channels;
		request.dropped_bytes = data->dropped_bytes;
		request.destination_identities = destinations;
		{
			std::lock_guard<std::mutex> guard(preconnect_mutex_);
			if (stop_preconnect_worker_) {
				return;
			}
			preconnect_flush_requests_.push_back(std::move(request));
		}
		preconnect_cv_.notify_one();
	}
}

void LocalParticipant::NotifyPreconnectAudioAvailable() {
	{
		std::lock_guard<std::mutex> guard(preconnect_mutex_);
		if (stop_preconnect_worker_) {
			return;
		}
		preconnect_scan_requested_ = true;
	}
	preconnect_cv_.notify_one();
}

void LocalParticipant::RunPreconnectWorker() {
	for (;;) {
		PreconnectFlushRequest request;
		{
			std::unique_lock<std::mutex> lock(preconnect_mutex_);
			preconnect_cv_.wait(lock, [this] {
				return stop_preconnect_worker_ || preconnect_scan_requested_ ||
				       !preconnect_flush_requests_.empty();
			});
			if (stop_preconnect_worker_) {
				return;
			}
			if (preconnect_scan_requested_) {
				preconnect_scan_requested_ = false;
				lock.unlock();
				TryQueuePreconnectBuffers();
				continue;
			}
			request = std::move(preconnect_flush_requests_.front());
			preconnect_flush_requests_.pop_front();
		}
		ByteSendOptions options;
		options.topic = "lk.agent.pre-connect-audio-buffer";
		options.name = "preconnect-audio-buffer";
		options.destination_identities = std::move(request.destination_identities);
		options.attributes = {{"trackId", request.track_sid},
		                      {"sampleRate", std::to_string(request.sample_rate)},
		                      {"channels", std::to_string(request.channels)},
		                      {"commonFormat", "int16"}};
		if (!SendBytes(request.bytes, std::move(options))) {
			LKC_LOG_WARNING << "failed to send preconnect audio buffer: track="
			                << request.track_sid;
		} else {
			LKC_LOG_INFO << "sent preconnect audio buffer: track=" << request.track_sid
			             << ", bytes=" << request.bytes.size()
			             << ", dropped_bytes=" << request.dropped_bytes;
		}
	}
}

void LocalParticipant::QueueBackupCodec(std::string track_sid, VideoCodec codec) {
	std::lock_guard<std::mutex> guard(backup_codec_mutex_);
	const auto key = std::make_pair(track_sid, codec);
	if (stop_backup_codec_worker_ || !pending_backup_codecs_.insert(key).second) {
		return;
	}
	backup_codec_requests_.push_back({std::move(track_sid), codec});
	backup_codec_cv_.notify_one();
}

void LocalParticipant::RunBackupCodecWorker() {
	for (;;) {
		BackupCodecRequest request;
		{
			std::unique_lock<std::mutex> lock(backup_codec_mutex_);
			backup_codec_cv_.wait(lock, [this] {
				return stop_backup_codec_worker_ || !backup_codec_requests_.empty();
			});
			if (stop_backup_codec_worker_) {
				return;
			}
			request = std::move(backup_codec_requests_.front());
			backup_codec_requests_.pop_front();
		}
		try {
			PublishAdditionalCodec(request.track_sid, request.codec);
		} catch (const std::exception& error) {
			LKC_LOG_ERROR << "failed to publish backup codec: " << error.what();
		} catch (...) {
			LKC_LOG_ERROR << "failed to publish backup codec: unknown error";
		}
		std::lock_guard<std::mutex> guard(backup_codec_mutex_);
		pending_backup_codecs_.erase(std::make_pair(request.track_sid, request.codec));
	}
}

bool LocalParticipant::PublishAdditionalCodec(const std::string& track_sid, VideoCodec codec) {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	if (engine_ == nullptr || encryption_type_ != EncryptionType::None ||
	    !IsSupportedBackupVideoCodec(codec) || !engine_->SupportsVideoCodec(codec)) {
		return false;
	}
	const auto publications = TrackPublicationsSnapshot();
	const auto publication = publications.find(track_sid);
	if (publication == publications.end()) {
		return false;
	}
	auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication->second.get());
	auto* local_track = local_publication != nullptr
	                        ? dynamic_cast<LocalVideoTrack*>(local_publication->Track())
	                        : nullptr;
	if (local_track == nullptr || local_track->source() == nullptr ||
	    local_track->HasAdditionalCodec(codec)) {
		return false;
	}
	const auto options = local_publication->PublishOptions();
	if (!options.backup_video_codec.has_value() || *options.backup_video_codec != codec ||
	    options.video_codec == codec) {
		return false;
	}
	auto* video_source = dynamic_cast<VideoSource*>(local_track->source());
	if (video_source == nullptr || video_source->Width() == 0 || video_source->Height() == 0) {
		return false;
	}
	auto peer_transport_factory = engine_->GetSessionPeerTransportFactory();
	auto peer_factory = peer_transport_factory != nullptr
	                        ? webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(
	                              peer_transport_factory->GetPeerConnectFactory())
	                        : nullptr;
	if (peer_factory == nullptr) {
		return false;
	}
	auto rtc_video_track =
	    peer_factory->CreateVideoTrack(video_source->Get(), webrtc::CreateRandomUuid());
	if (rtc_video_track == nullptr) {
		return false;
	}
	auto media_track = std::make_unique<VideoTrack>(rtc_video_track);
	media_track->set_enabled(local_track->IsEnabled());

	auto backup_options = options;
	backup_options.video_codec = codec;
	backup_options.video_encoding = options.backup_video_encoding;
	if (backup_options.source == TrackSource::ScreenShare) {
		backup_options.simulcast = false;
	}
	const auto encoding_plan =
	    BuildVideoEncodingPlan(video_source->Width(), video_source->Height(),
	                           backup_options.source == TrackSource::ScreenShare, backup_options);
	if (!encoding_plan.valid) {
		return false;
	}

	livekit::AddTrackRequest req;
	req.set_cid(rtc_video_track->id());
	req.set_sid(track_sid);
	req.set_name(local_track->Name());
	req.set_type(livekit::TrackType::VIDEO);
	req.set_source(to_proto(backup_options.source));
	req.set_muted(local_track->Muted());
	req.set_encryption(livekit::Encryption_Type_NONE);
	req.set_stream(backup_options.stream);
	req.set_width(video_source->Width());
	req.set_height(video_source->Height());
	FrameMetadataFeatures frame_metadata_features;
	std::shared_ptr<detail::FrameMetadataStore> frame_metadata_store;
	if (backup_options.frame_metadata_features && backup_options.frame_metadata_features->Any()) {
		frame_metadata_features = *backup_options.frame_metadata_features;
		if (frame_metadata_features.user_timestamp) {
			req.add_packet_trailer_features(livekit::PTF_USER_TIMESTAMP);
		}
		if (frame_metadata_features.frame_id) {
			req.add_packet_trailer_features(livekit::PTF_FRAME_ID);
		}
		if (frame_metadata_features.user_data) {
			req.add_packet_trailer_features(livekit::PTF_USER_DATA);
		}
		frame_metadata_store = video_source->GetFrameMetadataStore();
	}
	for (const auto& layer : encoding_plan.layers) {
		req.add_layers()->CopyFrom(layer);
	}
	auto* simulcast_codec = req.add_simulcast_codecs();
	simulcast_codec->set_codec(VideoCodecName(codec));
	simulcast_codec->set_cid(req.cid());
	simulcast_codec->set_video_layer_mode(encoding_plan.video_layer_mode);
	for (const auto& layer : encoding_plan.layers) {
		simulcast_codec->add_layers()->CopyFrom(layer);
	}
	if (!engine_->SendAdditionalCodecTrack(req)) {
		return false;
	}
	auto transceiver =
	    engine_->CreateSender(rtc_video_track, backup_options, encoding_plan.encodings);
	if (transceiver == nullptr) {
		return false;
	}
	if (!backup_options.degradation_preference ||
	    !ApplyVideoDegradationPreference(transceiver->sender(),
	                                     *backup_options.degradation_preference)) {
		engine_->RemoveSender(transceiver);
		return false;
	}
	if (frame_metadata_store) {
		transceiver->sender()->SetFrameTransformer(detail::CreateFrameMetadataTransformer(
		    true, std::move(frame_metadata_store), frame_metadata_features));
	}
	if (!local_track->AddAdditionalCodec(codec, std::move(media_track), transceiver,
	                                     encoding_plan.encodings)) {
		engine_->RemoveSender(transceiver);
		return false;
	}
	engine_->PublisherNegotiationNeeded();
	return true;
}

void LocalParticipant::SubscribedQualityUpdate(core::SubscribedQualityUpdate update) {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	if (update.track_sid.empty()) {
		return;
	}
	auto publications = TrackPublicationsSnapshot();
	auto publication = publications.find(update.track_sid);
	if (publication == publications.end() || publication->second->Kind() != TrackKind::Video) {
		return;
	}
	auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication->second.get());
	if (local_publication == nullptr) {
		return;
	}
	local_publication->UpdateSubscribedQuality(update);
	const auto publish_options = local_publication->PublishOptions();
	if (publish_options.backup_video_codec.has_value()) {
		for (const auto& requested_codec : update.codecs) {
			const auto codec = ParseBackupVideoCodec(requested_codec.codec);
			const bool enabled =
			    std::any_of(requested_codec.qualities.begin(), requested_codec.qualities.end(),
			                [](const auto& quality) { return quality.enabled; });
			if (enabled && codec.has_value() && *codec == *publish_options.backup_video_codec) {
				QueueBackupCodec(update.track_sid, *codec);
			}
		}
	}
	bool dynacast = false;
	{
		std::lock_guard<std::mutex> guard(room_options_mutex_);
		dynacast = options_.dynacast;
	}
	if (dynacast) {
		auto* local_track = dynamic_cast<LocalTrack*>(local_publication->Track());
		auto transceiver = local_track != nullptr ? local_track->Transceiver() : nullptr;
		auto sender = transceiver != nullptr ? transceiver->sender() : nullptr;
		if (sender != nullptr) {
			auto parameters = sender->GetParameters();
			if (ApplySubscribedQualities(parameters.encodings, update,
			                             VideoCodecName(publish_options.video_codec))) {
				sender->SetParameters(parameters);
			}
		}
		auto* video_track = dynamic_cast<LocalVideoTrack*>(local_publication->Track());
		if (video_track != nullptr) {
			for (const auto& additional : video_track->AdditionalCodecs()) {
				auto additional_sender =
				    additional.transceiver != nullptr ? additional.transceiver->sender() : nullptr;
				if (additional_sender == nullptr) {
					continue;
				}
				auto parameters = additional_sender->GetParameters();
				if (ApplySubscribedQualities(parameters.encodings, update,
				                             VideoCodecName(additional.codec))) {
					additional_sender->SetParameters(parameters);
				}
			}
		}
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnSubscribedQualityUpdate(local_publication, this, update);
	}
}

std::size_t LocalParticipant::UnpublishTracks(const std::vector<LocalTrackInterface*>& tracks,
                                              bool stop_on_unpublish) {
	std::size_t unpublished = 0;
	for (auto* track : tracks) {
		if (UnpublishTrack(track, stop_on_unpublish)) {
			++unpublished;
		}
	}
	return unpublished;
}

bool LocalParticipant::RepublishAllTracks() {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	struct PendingTrack {
		LocalTrackInterface* track;
		TrackPublishOptions options;
	};

	std::vector<PendingTrack> tracks;
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication.get());
		auto* local_track = local_publication != nullptr
		                        ? dynamic_cast<LocalTrackInterface*>(publication->Track())
		                        : nullptr;
		if (local_track != nullptr) {
			tracks.push_back({local_track, local_publication->PublishOptions()});
		}
	}

	bool success = true;
	for (const auto& pending : tracks) {
		if (!UnpublishTrack(pending.track, false) ||
		    !PublishTrack(pending.track, pending.options)) {
			success = false;
		}
	}
	return success;
}

void LocalParticipant::DetachTrackTransceiversForReconnect() {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication.get());
		auto* local_track = local_publication != nullptr
		                        ? dynamic_cast<LocalTrack*>(publication->Track())
		                        : nullptr;
		if (local_track != nullptr) {
			if (auto* audio_track = dynamic_cast<LocalAudioTrack*>(local_track)) {
				audio_track->DiscardPreconnectBuffer();
			}
			if (e2ee_manager_ != nullptr) {
				E2EEManagerNativeAccess::Detach(*e2ee_manager_, sid, FrameCryptorDirection::Sender);
			}
			if (auto* video_track = dynamic_cast<LocalVideoTrack*>(local_track);
			    video_track != nullptr) {
				video_track->ClearAdditionalCodecs();
			}
			local_track->SetTransceiver(nullptr);
		}
	}
}

bool LocalParticipant::RepublishAllTracksAfterReconnect() {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	struct PendingTrack {
		std::string publication_sid;
		LocalTrack* track;
		TrackPublishOptions options;
	};

	std::vector<PendingTrack> tracks;
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication.get());
		auto* local_track = local_publication != nullptr
		                        ? dynamic_cast<LocalTrack*>(publication->Track())
		                        : nullptr;
		if (local_track != nullptr) {
			tracks.push_back({sid, local_track, local_publication->PublishOptions()});
		}
	}

	for (const auto& pending : tracks) {
		RemoveTrackPublication(pending.publication_sid);
	}

	bool success = true;
	for (const auto& pending : tracks) {
		if (!PublishTrack(pending.track, pending.options)) {
			success = false;
		}
	}
	return success;
}

bool LocalParticipant::RepublishAllDataTracksAfterReconnect() {
	if (engine_ == nullptr || !engine_->RepublishDataTracks()) {
		return false;
	}
	for (const auto& [sid, interface] : DataTracksSnapshot()) {
		auto* track = dynamic_cast<LocalDataTrack*>(interface.get());
		if (track == nullptr) {
			continue;
		}
		auto info = engine_->PublishedDataTrackInfo(track->Info().publisher_handle);
		if (!info) {
			continue;
		}
		const auto old_sid = track->Info().sid;
		track->UpdateInfo(detail::FromProto(*info));
		if (old_sid != info->sid()) {
			RemoveDataTrack(old_sid);
			AddDataTrack(interface);
		}
	}
	return true;
}

void LocalParticipant::LocalDataTrackUnpublished(uint16_t publisher_handle) {
	std::shared_ptr<LocalDataTrack> unpublished;
	for (const auto& [sid, interface] : DataTracksSnapshot()) {
		auto track = std::dynamic_pointer_cast<LocalDataTrack>(interface);
		if (track && track->Info().publisher_handle == publisher_handle) {
			track->MarkUnpublished();
			RemoveDataTrack(sid);
			unpublished = std::move(track);
			break;
		}
	}
	if (unpublished) {
		if (auto* listener = event_listener_.load()) {
			listener->OnLocalDataTrackUnpublished(unpublished.get(), this);
		}
	}
}

void LocalParticipant::SetEventListener(RoomEventInterface* listener) {
	event_listener_.store(listener);
}

void LocalParticipant::SetE2EEManager(E2EEManager* manager, EncryptionType encryption_type) {
	std::lock_guard<std::recursive_mutex> publish_guard(media_publish_mutex_);
	e2ee_manager_ = manager;
	encryption_type_ = encryption_type;
}

void LocalParticipant::UpdateRoomOptions(RoomOptions options) {
	std::lock_guard<std::mutex> guard(room_options_mutex_);
	options_ = std::move(options);
}

bool LocalParticipant::SetMetadata(const std::string& metadata) {
	return engine_ != nullptr && engine_->UpdateLocalMetadata(metadata, Name(), {});
}

bool LocalParticipant::SetName(const std::string& name) {
	return engine_ != nullptr && engine_->UpdateLocalMetadata(Metadata(), name, {});
}

bool LocalParticipant::SetAttributes(const std::map<std::string, std::string>& attributes) {
	return engine_ != nullptr && engine_->UpdateLocalMetadata(Metadata(), Name(), attributes);
}

bool LocalParticipant::PublishData(const std::vector<uint8_t>& data, DataPublishOptions options) {
	if (engine_ == nullptr) {
		return false;
	}
	livekit::DataPacket packet;
	packet.set_kind(options.reliable ? livekit::DataPacket_Kind_RELIABLE
	                                 : livekit::DataPacket_Kind_LOSSY);
	for (const auto& identity : options.destination_identities) {
		packet.add_destination_identities(identity);
	}
	auto* user = packet.mutable_user();
	user->set_participant_identity(Identity());
	user->set_payload(data.data(), data.size());
	if (!options.topic.empty()) {
		user->set_topic(std::move(options.topic));
	}
	return engine_->SendDataPacket(packet, options.reliable);
}

DataTrackPublishResult LocalParticipant::PublishDataTrack(DataTrackPublishOptions options) {
	if (engine_ == nullptr) {
		return {nullptr, {DataTrackErrorCode::Disconnected, "room is disconnected"}};
	}
	const bool encrypted = encryption_type_ != EncryptionType::None;
	auto [proto_info, error] = engine_->PublishDataTrack(options, encrypted);
	if (!proto_info) {
		return {nullptr, error};
	}
	auto track = std::make_shared<LocalDataTrack>(
	    detail::FromProto(*proto_info),
	    [engine = engine_](uint16_t handle, const DataTrackFrame& frame) {
		    return engine->PushDataTrackFrame(handle, frame);
	    },
	    [engine = engine_](uint16_t handle) { return engine->UnpublishDataTrack(handle); });
	AddDataTrack(track);
	if (auto* listener = event_listener_.load()) {
		listener->OnLocalDataTrackPublished(track.get(), this);
	}
	return {track, {}};
}

bool LocalParticipant::PublishDtmf(uint32_t code, std::string digit) {
	if (engine_ == nullptr) {
		return false;
	}
	livekit::DataPacket packet;
	packet.set_kind(livekit::DataPacket_Kind_RELIABLE);
	auto* dtmf = packet.mutable_sip_dtmf();
	dtmf->set_code(code);
	dtmf->set_digit(std::move(digit));
	return engine_->SendDataPacket(packet, true);
}

std::optional<ChatMessage> LocalParticipant::SendChatMessage(std::string message) {
	ChatMessage chat;
	chat.id = webrtc::CreateRandomUuid();
	chat.timestamp = CurrentTimestampMilliseconds();
	chat.message = std::move(message);
	chat.participant_identity = Identity();
	if (!PublishChatMessage(engine_, chat)) {
		return std::nullopt;
	}
	return chat;
}

std::optional<ChatMessage> LocalParticipant::EditChatMessage(std::string message,
                                                             const ChatMessage& original) {
	if (original.id.empty()) {
		return std::nullopt;
	}
	ChatMessage edited = original;
	edited.message = std::move(message);
	edited.edit_timestamp = CurrentTimestampMilliseconds();
	edited.participant_identity = Identity();
	if (!PublishChatMessage(engine_, edited)) {
		return std::nullopt;
	}
	return edited;
}

RpcResult LocalParticipant::PerformRpc(const PerformRpcParams& params) {
	if (engine_ == nullptr) {
		return RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::SendFailed));
	}
	return engine_->PerformRpc(params);
}

bool LocalParticipant::SetTrackSubscriptionPermissions(
    bool all_participants_allowed,
    const std::vector<ParticipantTrackPermission>& participant_permissions) {
	for (const auto& permission : participant_permissions) {
		if (permission.participant_sid.empty() && permission.participant_identity.empty()) {
			return false;
		}
		if (std::any_of(permission.allowed_track_sids.begin(), permission.allowed_track_sids.end(),
		                [](const std::string& sid) { return sid.empty(); })) {
			return false;
		}
	}
	{
		std::lock_guard<std::mutex> guard(subscription_permissions_mutex_);
		all_participants_allowed_to_subscribe_ = all_participants_allowed;
		participant_track_permissions_ = participant_permissions;
	}
	// Matching the official clients, permissions may be configured before Connect(). They are
	// retained and sent by ResendTrackSubscriptionPermissions() once signaling is available.
	if (engine_ != nullptr) {
		engine_->UpdateSubscriptionPermissions(all_participants_allowed, participant_permissions);
	}
	return true;
}

bool LocalParticipant::ResendTrackSubscriptionPermissions() {
	bool all_participants_allowed = true;
	std::vector<ParticipantTrackPermission> permissions;
	{
		std::lock_guard<std::mutex> guard(subscription_permissions_mutex_);
		all_participants_allowed = all_participants_allowed_to_subscribe_;
		permissions = participant_track_permissions_;
	}
	return engine_ != nullptr &&
	       engine_->UpdateSubscriptionPermissions(all_participants_allowed, permissions);
}

bool LocalParticipant::SendText(const std::string& text, TextSendOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize ||
	    (options.compress && text.size() > kMaximumCompressedDataStreamSize)) {
		return false;
	}
	const std::string stream_id = webrtc::CreateRandomUuid();
	livekit::DataPacket header_packet;
	PopulateStreamPacket(header_packet, options);
	auto* header = header_packet.mutable_stream_header();
	PopulateStreamHeader(*header, stream_id, text.size(), options);
	header->set_mime_type("text/plain");
	auto* text_header = header->mutable_text_header();
	text_header->set_operation_type(livekit::DataStream_OperationType_CREATE);
	text_header->set_reply_to_stream_id(options.reply_to_stream_id);
	for (const auto& attached_stream_id : options.attached_stream_ids) {
		text_header->add_attached_stream_ids(attached_stream_id);
	}
	if (!engine_->SendDataPacket(header_packet, true)) {
		return false;
	}
	return SendStreamPayload(engine_, stream_id, reinterpret_cast<const uint8_t*>(text.data()),
	                         text.size(), options);
}

bool LocalParticipant::SendBytes(const std::vector<uint8_t>& data, ByteSendOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize ||
	    (options.compress && data.size() > kMaximumCompressedDataStreamSize)) {
		return false;
	}
	const std::string stream_id = webrtc::CreateRandomUuid();
	livekit::DataPacket header_packet;
	PopulateStreamPacket(header_packet, options);
	auto* header = header_packet.mutable_stream_header();
	PopulateStreamHeader(*header, stream_id, data.size(), options);
	header->set_mime_type(options.mime_type);
	header->mutable_byte_header()->set_name(options.name);
	if (!engine_->SendDataPacket(header_packet, true)) {
		return false;
	}
	return SendStreamPayload(engine_, stream_id, data.data(), data.size(), options);
}

bool LocalParticipant::SendFile(const std::string& path, FileSendOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize) {
		return false;
	}
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		return false;
	}
	const auto end = input.tellg();
	if (end < 0) {
		return false;
	}
	const auto file_size = static_cast<uint64_t>(end);
	if (options.compress && file_size > kMaximumCompressedDataStreamSize) {
		return false;
	}
	input.seekg(0, std::ios::beg);

	const std::string stream_id = webrtc::CreateRandomUuid();

	livekit::DataPacket header_packet;
	PopulateStreamPacket(header_packet, options);
	auto* header = header_packet.mutable_stream_header();
	PopulateStreamHeader(*header, stream_id, file_size, options);
	header->set_mime_type(options.mime_type);
	header->mutable_byte_header()->set_name(std::filesystem::path(path).filename().string());
	if (!engine_->SendDataPacket(header_packet, true)) {
		return false;
	}

	std::vector<uint8_t> buffer(options.chunk_size);
	std::unique_ptr<detail::DeflateRawStream> deflater;
	if (options.compress) {
		deflater = std::make_unique<detail::DeflateRawStream>();
		if (!deflater->IsValid()) {
			return false;
		}
	}
	uint64_t chunk_index = 0;
	while (input) {
		input.read(reinterpret_cast<char*>(buffer.data()),
		           static_cast<std::streamsize>(buffer.size()));
		const auto count = input.gcount();
		if (count <= 0) {
			break;
		}
		const uint8_t* payload = buffer.data();
		std::size_t payload_size = static_cast<std::size_t>(count);
		std::vector<uint8_t> compressed;
		if (deflater) {
			if (!deflater->Write(payload, payload_size, compressed)) {
				return false;
			}
			payload = compressed.data();
			payload_size = compressed.size();
		}
		if (!SendStreamChunks(engine_, stream_id, payload, payload_size, options, chunk_index)) {
			return false;
		}
	}
	if (deflater) {
		std::vector<uint8_t> compressed;
		if (!deflater->Finish(compressed) ||
		    !SendStreamChunks(engine_, stream_id, compressed.data(), compressed.size(), options,
		                      chunk_index)) {
			return false;
		}
	}

	livekit::DataPacket trailer_packet;
	PopulateStreamPacket(trailer_packet, options);
	trailer_packet.mutable_stream_trailer()->set_stream_id(stream_id);
	return engine_->SendDataPacket(trailer_packet, true);
}

std::unique_ptr<TextStreamWriterInterface> LocalParticipant::StreamText(StreamTextOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize ||
	    (options.update && options.stream_id.empty()) ||
	    (options.compress && options.total_size &&
	     *options.total_size > kMaximumCompressedDataStreamSize)) {
		return nullptr;
	}
	if (options.compress) {
		detail::DeflateRawStream validation;
		if (!validation.IsValid()) {
			return nullptr;
		}
	}
	TextStreamInfo info;
	info.stream_id = options.stream_id.empty() ? webrtc::CreateRandomUuid() : options.stream_id;
	info.mime_type = "text/plain";
	info.topic = options.topic;
	info.participant_identity = Identity();
	info.attributes = options.attributes;
	info.total_size = options.total_size;
	info.timestamp = CurrentTimestampMilliseconds();
	info.reply_to_stream_id = options.reply_to_stream_id;
	info.attached_stream_ids = options.attached_stream_ids;
	livekit::DataPacket packet;
	PopulateStreamPacket(packet, options);
	auto* header = packet.mutable_stream_header();
	PopulateStreamingHeader(*header, info, options.compress);
	auto* text_header = header->mutable_text_header();
	text_header->set_operation_type(options.update ? livekit::DataStream_OperationType_UPDATE
	                                               : livekit::DataStream_OperationType_CREATE);
	text_header->set_version(options.version);
	text_header->set_reply_to_stream_id(options.reply_to_stream_id);
	for (const auto& stream_id : options.attached_stream_ids) {
		text_header->add_attached_stream_ids(stream_id);
	}
	if (!outgoing_stream_state_->Send(packet)) {
		return nullptr;
	}
	return std::make_unique<TextStreamWriter>(outgoing_stream_state_, std::move(info),
	                                          std::move(options));
}

std::unique_ptr<ByteStreamWriterInterface>
LocalParticipant::StreamBytes(StreamBytesOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize ||
	    (options.compress && options.total_size &&
	     *options.total_size > kMaximumCompressedDataStreamSize)) {
		return nullptr;
	}
	if (options.compress) {
		detail::DeflateRawStream validation;
		if (!validation.IsValid()) {
			return nullptr;
		}
	}
	ByteStreamInfo info;
	info.stream_id = options.stream_id.empty() ? webrtc::CreateRandomUuid() : options.stream_id;
	info.mime_type = options.mime_type;
	info.topic = options.topic;
	info.participant_identity = Identity();
	info.attributes = options.attributes;
	info.total_size = options.total_size;
	info.timestamp = CurrentTimestampMilliseconds();
	info.name = options.name;
	livekit::DataPacket packet;
	PopulateStreamPacket(packet, options);
	auto* header = packet.mutable_stream_header();
	PopulateStreamingHeader(*header, info, options.compress);
	header->mutable_byte_header()->set_name(options.name);
	if (!outgoing_stream_state_->Send(packet)) {
		return nullptr;
	}
	return std::make_unique<ByteStreamWriter>(outgoing_stream_state_, std::move(info),
	                                          std::move(options));
}

} // namespace core
} // namespace livekit
