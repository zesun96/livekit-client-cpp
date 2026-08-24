/*
 * Copyright 2022 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "api/crypto/frame_crypto_transformer.h"

#include <openssl/evp.h>
#include <openssl/hkdf.h>

#include <cmath>
#include <string>

#include "absl/types/variant.h"
#include "api/array_view.h"
#include "common_video/h264/h264_common.h"
#ifdef RTC_ENABLE_H265
#include "common_video/h265/h265_common.h"
#endif // RTC_ENABLE_H265
#include "modules/rtp_rtcp/source/leb128.h"
#include "modules/rtp_rtcp/source/rtp_format_h264.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

enum class EncryptOrDecrypt { kEncrypt = 0, kDecrypt };

constexpr size_t kFrameTrailerSize = 2;
constexpr size_t kGcmTagSize = 16;
constexpr uint8_t kAv1SequenceHeaderObuWithSizeHeader = 0x0a;
constexpr uint8_t kAv1FrameObuWithSizeHeader = 0x32;
constexpr uint8_t kAv1RoutingFrameHeader = 0x00;

#define Success                     0
#define ErrorUnexpected             -1
#define OperationError              -2
#define ErrorDataTooSmall           -3
#define ErrorInvalidAesGcmTagLength -4

webrtc::VideoCodecType get_video_codec_type(webrtc::TransformableFrameInterface* frame) {
	auto videoFrame = static_cast<webrtc::TransformableVideoFrameInterface*>(frame);
	return videoFrame->header().codec;
}

webrtc::H264PacketizationMode
get_h264_packetization_mode(webrtc::TransformableFrameInterface* frame) {
	auto video_frame = static_cast<webrtc::TransformableVideoFrameInterface*>(frame);
	const auto& h264_header =
	    absl::get<webrtc::RTPVideoHeaderH264>(video_frame->header().video_type_header);
	return h264_header.packetization_mode;
}

const EVP_AEAD* GetAesGcmAlgorithmFromKeySize(size_t key_size_bytes) {
	switch (key_size_bytes) {
	case 16:
		return EVP_aead_aes_128_gcm();
	case 32:
		return EVP_aead_aes_256_gcm();
	default:
		return nullptr;
	}
}

inline bool FrameIsH264(webrtc::TransformableFrameInterface* frame,
                        webrtc::FrameCryptorTransformer::MediaType type) {
	switch (type) {
	case webrtc::FrameCryptorTransformer::MediaType::kVideoFrame: {
		auto videoFrame = static_cast<webrtc::TransformableVideoFrameInterface*>(frame);
		return videoFrame->header().codec == webrtc::VideoCodecType::kVideoCodecH264;
	}
	default:
		return false;
	}
}

inline bool FrameIsAV1(webrtc::TransformableFrameInterface* frame,
                       webrtc::FrameCryptorTransformer::MediaType type) {
	switch (type) {
	case webrtc::FrameCryptorTransformer::MediaType::kVideoFrame: {
		auto videoFrame = static_cast<webrtc::TransformableVideoFrameInterface*>(frame);
		return videoFrame->header().codec == webrtc::VideoCodecType::kVideoCodecAV1;
	}
	default:
		return false;
	}
}

webrtc::Buffer MakeAv1EncryptedFrameHeader(size_t encrypted_payload_size, bool key_frame) {
	const size_t sequence_header_size = key_frame ? 2 : 0;
	const size_t frame_obu_payload_size = 1 + encrypted_payload_size;
	webrtc::Buffer header(sequence_header_size + 1 + webrtc::Leb128Size(frame_obu_payload_size) +
	                      1);
	size_t offset = 0;
	if (key_frame) {
		header[offset++] = kAv1SequenceHeaderObuWithSizeHeader;
		header[offset++] = 0;
	}
	header[offset++] = kAv1FrameObuWithSizeHeader;
	offset += webrtc::WriteLeb128(frame_obu_payload_size, header.data() + offset);
	header[offset] = kAv1RoutingFrameHeader;
	return header;
}

bool ParseAv1EncryptedFrameHeader(webrtc::ArrayView<const uint8_t> data, size_t& header_size) {
	if (data.empty()) {
		return false;
	}
	const uint8_t* read_at = data.data();
	const uint8_t* end = data.data() + data.size();
	if (*read_at == kAv1SequenceHeaderObuWithSizeHeader) {
		++read_at;
		const uint64_t sequence_header_payload_size = webrtc::ReadLeb128(read_at, end);
		if (read_at == nullptr || sequence_header_payload_size != 0) {
			return false;
		}
	}
	if (read_at == end || *read_at++ != kAv1FrameObuWithSizeHeader) {
		return false;
	}
	const uint64_t frame_obu_payload_size = webrtc::ReadLeb128(read_at, end);
	if (read_at == nullptr || frame_obu_payload_size != static_cast<uint64_t>(end - read_at) ||
	    frame_obu_payload_size < 1 || *read_at++ != kAv1RoutingFrameHeader) {
		return false;
	}
	header_size = static_cast<size_t>(read_at - data.data());
	return true;
}

inline bool FrameIsH265(webrtc::TransformableFrameInterface* frame,
                        webrtc::FrameCryptorTransformer::MediaType type) {
	switch (type) {
	case webrtc::FrameCryptorTransformer::MediaType::kVideoFrame: {
		auto videoFrame = static_cast<webrtc::TransformableVideoFrameInterface*>(frame);
		return videoFrame->header().codec == webrtc::VideoCodecType::kVideoCodecH265;
	}
	default:
		return false;
	}
}

#ifdef RTC_ENABLE_H265
inline bool IsH265SliceNalu(webrtc::H265::NaluType nalu_type) {
	// VCL NALUs (Video Coding Layer) - slice segments
	return nalu_type == webrtc::H265::NaluType::kTrailN ||
	       nalu_type == webrtc::H265::NaluType::kTrailR ||
	       nalu_type == webrtc::H265::NaluType::kTsaN ||
	       nalu_type == webrtc::H265::NaluType::kTsaR ||
	       nalu_type == webrtc::H265::NaluType::kStsaN ||
	       nalu_type == webrtc::H265::NaluType::kStsaR ||
	       nalu_type == webrtc::H265::NaluType::kRadlN ||
	       nalu_type == webrtc::H265::NaluType::kRadlR ||
	       nalu_type == webrtc::H265::NaluType::kRaslN ||
	       nalu_type == webrtc::H265::NaluType::kRaslR ||
	       nalu_type == webrtc::H265::NaluType::kBlaWLp ||
	       nalu_type == webrtc::H265::NaluType::kBlaWRadl ||
	       nalu_type == webrtc::H265::NaluType::kBlaNLp ||
	       nalu_type == webrtc::H265::NaluType::kIdrWRadl ||
	       nalu_type == webrtc::H265::NaluType::kIdrNLp ||
	       nalu_type == webrtc::H265::NaluType::kCra;
}
#endif // RTC_ENABLE_H265

inline bool NeedsRbspUnescaping(const uint8_t* frameData, size_t frameSize) {
	if (frameSize < 3) {
		return false;
	}
	for (size_t i = 0; i < frameSize - 3; ++i) {
		if (frameData[i] == 0 && frameData[i + 1] == 0 && frameData[i + 2] == 3)
			return true;
	}
	return false;
}

uint8_t get_unencrypted_bytes(webrtc::TransformableFrameInterface* frame,
                              webrtc::FrameCryptorTransformer::MediaType type) {
	uint8_t unencrypted_bytes = 0;
	switch (type) {
	case webrtc::FrameCryptorTransformer::MediaType::kAudioFrame:
		unencrypted_bytes = 1;
		break;
	case webrtc::FrameCryptorTransformer::MediaType::kVideoFrame: {
		auto videoFrame = static_cast<webrtc::TransformableVideoFrameInterface*>(frame);
		if (videoFrame->header().codec == webrtc::VideoCodecType::kVideoCodecAV1) {
			unencrypted_bytes = 0;
		} else if (videoFrame->header().codec == webrtc::VideoCodecType::kVideoCodecVP8) {
			unencrypted_bytes = videoFrame->IsKeyFrame() ? 10 : 3;
		} else if (videoFrame->header().codec == webrtc::VideoCodecType::kVideoCodecH264) {
			webrtc::ArrayView<const uint8_t> data_in = frame->GetData();
			std::vector<webrtc::H264::NaluIndex> nalu_indices =
			    webrtc::H264::FindNaluIndices(data_in);

			int idx = 0;
			for (const auto& index : nalu_indices) {
				const uint8_t* slice = data_in.data() + index.payload_start_offset;
				webrtc::H264::NaluType nalu_type = webrtc::H264::ParseNaluType(slice[0]);
				switch (nalu_type) {
				case webrtc::H264::NaluType::kIdr:
				case webrtc::H264::NaluType::kSlice:
					unencrypted_bytes = index.payload_start_offset + 2;
					RTC_LOG(LS_INFO) << "NonParameterSetNalu::payload_size: " << index.payload_size
					                 << ", nalu_type " << nalu_type << ", NaluIndex [" << idx++
					                 << "] offset: " << index.payload_start_offset;
					return unencrypted_bytes;
				default:
					break;
				}
			}
#ifdef RTC_ENABLE_H265
		} else if (videoFrame->header().codec == webrtc::VideoCodecType::kVideoCodecH265) {
			webrtc::ArrayView<const uint8_t> data_in = frame->GetData();
			std::vector<webrtc::H265::NaluIndex> nalu_indices =
			    webrtc::H265::FindNaluIndices(data_in);

			int idx = 0;
			for (const auto& index : nalu_indices) {
				const uint8_t* slice = data_in.data() + index.payload_start_offset;
				webrtc::H265::NaluType nalu_type = webrtc::H265::ParseNaluType(slice[0]);
				if (IsH265SliceNalu(nalu_type)) {
					// H.265 has a 2-byte NALU header, so unencrypted bytes = offset +
					// header size
					unencrypted_bytes = index.payload_start_offset + webrtc::H265::kNaluHeaderSize;
					RTC_LOG(LS_INFO)
					    << "H265 NonParameterSetNalu::payload_size: " << index.payload_size
					    << ", nalu_type " << static_cast<int>(nalu_type) << ", NaluIndex [" << idx++
					    << "] offset: " << index.payload_start_offset
					    << ", unencrypted_bytes: " << unencrypted_bytes;
					return unencrypted_bytes;
				}
			}
#endif // RTC_ENABLE_H265
		}
		break;
	}
	default:
		break;
	}
	return unencrypted_bytes;
}

int DeriveHkdfSha256FromSecret(const std::vector<uint8_t>& secret, const std::vector<uint8_t>& salt,
                               unsigned int optional_length_bits,
                               std::vector<uint8_t>& derived_key) {
	size_t key_size_bytes = optional_length_bits / 8;
	// Use 128 bytes of zeros to padded as info.
	auto info = std::vector<uint8_t>(128, 0);
	derived_key.resize(key_size_bytes);
	if (::HKDF((uint8_t*)derived_key.data(), key_size_bytes, EVP_sha256(), secret.data(),
	           secret.size(), salt.data(), salt.size(), info.data(), info.size()) != 1) {
		RTC_LOG(LS_ERROR) << "Failed to derive HkdfSha256 key from secret.";
		return ErrorUnexpected;
	}

	return Success;
}

int DerivePBKDF2KeyFromRawKey(const std::vector<uint8_t>& raw_key, const std::vector<uint8_t>& salt,
                              unsigned int optional_length_bits,
                              std::vector<uint8_t>& derived_key) {
	size_t key_size_bytes = optional_length_bits / 8;
	derived_key.resize(key_size_bytes);

	if (PKCS5_PBKDF2_HMAC((const char*)raw_key.data(), raw_key.size(), salt.data(), salt.size(),
	                      100000, EVP_sha256(), key_size_bytes, derived_key.data()) != 1) {
		RTC_LOG(LS_ERROR) << "Failed to derive AES key from password.";
		return ErrorUnexpected;
	}

	return Success;
}

int AesGcmEncryptDecrypt(EncryptOrDecrypt mode, const std::vector<uint8_t> raw_key,
                         const webrtc::ArrayView<uint8_t> data, unsigned int tag_length_bytes,
                         webrtc::ArrayView<uint8_t> iv, webrtc::ArrayView<uint8_t> additional_data,
                         const EVP_AEAD* aead_alg, std::vector<uint8_t>* buffer) {
	bssl::ScopedEVP_AEAD_CTX ctx;

	if (!aead_alg) {
		RTC_LOG(LS_ERROR) << "Invalid AES-GCM key size.";
		return ErrorUnexpected;
	}

	if (!EVP_AEAD_CTX_init(ctx.get(), aead_alg, raw_key.data(), raw_key.size(), tag_length_bytes,
	                       nullptr)) {
		RTC_LOG(LS_ERROR) << "Failed to initialize AES-GCM context.";
		return OperationError;
	}

	size_t len;
	int ok;

	if (mode == EncryptOrDecrypt::kDecrypt) {
		if (data.size() < tag_length_bytes) {
			RTC_LOG(LS_ERROR) << "Data too small for AES-GCM tag.";
			return ErrorDataTooSmall;
		}

		buffer->resize(data.size() - tag_length_bytes);

		ok = EVP_AEAD_CTX_open(ctx.get(), buffer->data(), &len, buffer->size(), iv.data(),
		                       iv.size(), data.data(), data.size(), additional_data.data(),
		                       additional_data.size());
	} else {
		buffer->resize(data.size() + EVP_AEAD_max_overhead(aead_alg));

		ok = EVP_AEAD_CTX_seal(ctx.get(), buffer->data(), &len, buffer->size(), iv.data(),
		                       iv.size(), data.data(), data.size(), additional_data.data(),
		                       additional_data.size());
	}

	if (!ok) {
		RTC_LOG(LS_WARNING) << "Failed to perform AES-GCM operation.";
		return OperationError;
	}

	buffer->resize(len);

	return Success;
}

int AesEncryptDecrypt(EncryptOrDecrypt mode, webrtc::FrameCryptorTransformer::Algorithm algorithm,
                      const std::vector<uint8_t>& raw_key, webrtc::ArrayView<uint8_t> iv,
                      webrtc::ArrayView<uint8_t> additional_data,
                      const webrtc::ArrayView<uint8_t> data, std::vector<uint8_t>* buffer) {
	switch (algorithm) {
	case webrtc::FrameCryptorTransformer::Algorithm::kAesGcm: {
		unsigned int tag_length_bits = 128;
		const EVP_AEAD* cipher = GetAesGcmAlgorithmFromKeySize(raw_key.size());
		if (!cipher) {
			RTC_LOG(LS_ERROR) << "Invalid AES-GCM key size.";
			return ErrorUnexpected;
		}
		return AesGcmEncryptDecrypt(mode, raw_key, data, tag_length_bits / 8, iv, additional_data,
		                            cipher, buffer);
	}
	default:
		RTC_LOG(LS_ERROR) << "Unsupported algorithm.";
		return ErrorUnexpected;
	}
}
namespace webrtc {

int ParticipantKeyHandler::DoKeyDerivation(const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& salt,
                                           unsigned int optional_length_bits,
                                           std::vector<uint8_t>& derived_key) {
	RTC_DCHECK_GE(optional_length_bits, 8);
	RTC_DCHECK_EQ(optional_length_bits % 8, 0);
	switch (key_provider_->options().key_derivation_algorithm) {
	case KeyDerivationAlgorithm::kPBKDF2:
		return DerivePBKDF2KeyFromRawKey(key, salt, optional_length_bits, derived_key);
	case KeyDerivationAlgorithm::kHKDF:
		return DeriveHkdfSha256FromSecret(key, salt, optional_length_bits, derived_key);
	default:
		break;
	}

	RTC_LOG(LS_ERROR) << "Invalid key derivation algorithm !";

	return OperationError;
}

FrameCryptorTransformer::FrameCryptorTransformer(webrtc::Thread* signaling_thread,
                                                 const std::string participant_id, MediaType type,
                                                 Algorithm algorithm,
                                                 webrtc::scoped_refptr<KeyProvider> key_provider)
    : signaling_thread_(signaling_thread), thread_(webrtc::Thread::Create()),
      participant_id_(participant_id), type_(type), algorithm_(algorithm),
      key_provider_(key_provider) {
	RTC_DCHECK(key_provider_ != nullptr);
	thread_->SetName("FrameCryptorTransformer", this);
	thread_->Start();
}

FrameCryptorTransformer::~FrameCryptorTransformer() { thread_->Stop(); }

void FrameCryptorTransformer::Transform(
    std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
	webrtc::MutexLock lock(&sink_mutex_);
	if (sink_callback_ == nullptr && sink_callbacks_.size() == 0) {
		RTC_LOG(LS_WARNING) << "FrameCryptorTransformer::Transform sink_callback_ is NULL";
		return;
	}

	// do encrypt or decrypt here...
	switch (frame->GetDirection()) {
	case webrtc::TransformableFrameInterface::Direction::kSender:
		RTC_DCHECK(thread_ != nullptr);
		thread_->PostTask([frame = std::move(frame),
		                   self = webrtc::scoped_refptr<FrameCryptorTransformer>(this)]() mutable {
			self->encryptFrame(std::move(frame));
		});
		break;
	case webrtc::TransformableFrameInterface::Direction::kReceiver:
		RTC_DCHECK(thread_ != nullptr);
		thread_->PostTask([frame = std::move(frame),
		                   self = webrtc::scoped_refptr<FrameCryptorTransformer>(this)]() mutable {
			self->decryptFrame(std::move(frame));
		});
		break;
	case webrtc::TransformableFrameInterface::Direction::kUnknown:
		// do nothing
		RTC_LOG(LS_INFO) << "FrameCryptorTransformer::Transform() kUnknown";
		break;
	}
}

void FrameCryptorTransformer::encryptFrame(
    std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
	bool enabled_cryption = false;
	int key_index = 0;
	webrtc::scoped_refptr<webrtc::TransformedFrameCallback> sink_callback = nullptr;
	{
		webrtc::MutexLock lock(&mutex_);
		enabled_cryption = enabled_cryption_;
		key_index = key_index_;
	}
	{
		webrtc::MutexLock lock(&sink_mutex_);
		if (type_ == webrtc::FrameCryptorTransformer::MediaType::kAudioFrame) {
			sink_callback = sink_callback_;
		} else {
			sink_callback = sink_callbacks_[frame->GetSsrc()];
		}
	}

	if (sink_callback == nullptr) {
		RTC_LOG(LS_WARNING) << "FrameCryptorTransformer::encryptFrame() sink_callback is NULL";
		if (last_enc_error_ != FrameCryptionState::kInternalError) {
			last_enc_error_ = FrameCryptionState::kInternalError;
			onFrameCryptionStateChanged(last_enc_error_);
		}
		return;
	}

	webrtc::ArrayView<const uint8_t> data_in = frame->GetData();
	if (data_in.size() == 0) {
		RTC_LOG(LS_VERBOSE) << "FrameCryptorTransformer::encryptFrame() empty frame";
		if (key_provider_->options().discard_frame_when_cryptor_not_ready) {
			return;
		}
		sink_callback->OnTransformedFrame(std::move(frame));
		return;
	}
	if (!enabled_cryption) {
		sink_callback->OnTransformedFrame(std::move(frame));
		return;
	}

	auto key_handler = key_provider_->options().shared_key
	                       ? key_provider_->GetSharedKey(participant_id_)
	                       : key_provider_->GetKey(participant_id_);

	if (key_handler == nullptr || key_handler->GetKeySet(key_index) == nullptr) {
		RTC_LOG(LS_INFO) << "FrameCryptorTransformer::encryptFrame() no keys, or "
		                    "key_index["
		                 << key_index << "] out of range for participant " << participant_id_;
		if (last_enc_error_ != FrameCryptionState::kMissingKey) {
			last_enc_error_ = FrameCryptionState::kMissingKey;
			onFrameCryptionStateChanged(last_enc_error_);
		}
		return;
	}

	auto key_set = key_handler->GetKeySet(key_index);
	const bool is_av1 = FrameIsAV1(frame.get(), type_);
	size_t unencrypted_bytes = get_unencrypted_bytes(frame.get(), type_);
	if (unencrypted_bytes > data_in.size()) {
		if (last_enc_error_ != FrameCryptionState::kEncryptionFailed) {
			last_enc_error_ = FrameCryptionState::kEncryptionFailed;
			onFrameCryptionStateChanged(last_enc_error_);
		}
		return;
	}

	webrtc::Buffer frame_header;
	if (is_av1) {
		const size_t encrypted_obu_payload_size =
		    data_in.size() + kGcmTagSize + getIvSize() + kFrameTrailerSize;
		const auto* video_frame =
		    static_cast<webrtc::TransformableVideoFrameInterface*>(frame.get());
		frame_header =
		    MakeAv1EncryptedFrameHeader(encrypted_obu_payload_size, video_frame->IsKeyFrame());
	} else {
		frame_header.SetData(data_in.subview(0, unencrypted_bytes));
	}

	webrtc::Buffer frame_trailer(kFrameTrailerSize);
	frame_trailer[0] = getIvSize();
	frame_trailer[1] = static_cast<std::uint8_t>(key_index);
	webrtc::Buffer iv = makeIv(frame->GetSsrc(), frame->GetTimestamp());

	webrtc::Buffer payload(data_in.size() - unencrypted_bytes);
	for (size_t i = unencrypted_bytes; i < data_in.size(); i++) {
		payload[i - unencrypted_bytes] = data_in[i];
	}

	std::vector<uint8_t> buffer;
	if (AesEncryptDecrypt(EncryptOrDecrypt::kEncrypt, algorithm_, key_set->encryption_key, iv,
	                      frame_header, payload, &buffer) == Success) {
		webrtc::Buffer encrypted_payload(buffer.data(), buffer.size());
		webrtc::Buffer data_without_header;
		data_without_header.AppendData(encrypted_payload);
		data_without_header.AppendData(iv);
		data_without_header.AppendData(frame_trailer);

		webrtc::Buffer data_out;
		data_out.AppendData(frame_header);

		if (FrameIsH264(frame.get(), type_)) {
			H264::WriteRbsp(data_without_header.data(), data_without_header.size(), &data_out);
#ifdef RTC_ENABLE_H265
		} else if (FrameIsH265(frame.get(), type_)) {
			H265::WriteRbsp(data_without_header.data(), data_without_header.size(), &data_out);
#endif // RTC_ENABLE_H265
		} else {
			data_out.AppendData(data_without_header);
			RTC_CHECK_EQ(data_out.size(), frame_header.size() + encrypted_payload.size() +
			                                  iv.size() + frame_trailer.size());
		}

		frame->SetData(data_out);

		if (last_enc_error_ != FrameCryptionState::kOk) {
			last_enc_error_ = FrameCryptionState::kOk;
			onFrameCryptionStateChanged(last_enc_error_);
		}
		sink_callback->OnTransformedFrame(std::move(frame));
	} else {
		if (last_enc_error_ != FrameCryptionState::kEncryptionFailed) {
			last_enc_error_ = FrameCryptionState::kEncryptionFailed;
			onFrameCryptionStateChanged(last_enc_error_);
		}
		RTC_LOG(LS_ERROR) << "FrameCryptorTransformer::encryptFrame() failed";
	}
}

void FrameCryptorTransformer::decryptFrame(
    std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
	bool enabled_cryption = false;
	webrtc::scoped_refptr<webrtc::TransformedFrameCallback> sink_callback = nullptr;
	{
		webrtc::MutexLock lock(&mutex_);
		enabled_cryption = enabled_cryption_;
	}
	{
		webrtc::MutexLock lock(&sink_mutex_);
		if (type_ == webrtc::FrameCryptorTransformer::MediaType::kAudioFrame) {
			sink_callback = sink_callback_;
		} else {
			sink_callback = sink_callbacks_[frame->GetSsrc()];
		}
	}

	if (sink_callback == nullptr) {
		RTC_LOG(LS_WARNING) << "FrameCryptorTransformer::decryptFrame() sink_callback is NULL";
		if (last_dec_error_ != FrameCryptionState::kInternalError) {
			last_dec_error_ = FrameCryptionState::kInternalError;
			onFrameCryptionStateChanged(last_dec_error_);
		}
		return;
	}

	webrtc::ArrayView<const uint8_t> data_in = frame->GetData();

	if (data_in.size() == 0) {
		RTC_LOG(LS_VERBOSE) << "FrameCryptorTransformer::decryptFrame() empty frame";
		if (key_provider_->options().discard_frame_when_cryptor_not_ready) {
			return;
		}

		sink_callback->OnTransformedFrame(std::move(frame));
		return;
	}
	if (!enabled_cryption) {
		sink_callback->OnTransformedFrame(std::move(frame));
		return;
	}

	auto uncrypted_magic_bytes = key_provider_->options().uncrypted_magic_bytes;
	if (uncrypted_magic_bytes.size() > 0 && data_in.size() >= uncrypted_magic_bytes.size()) {
		auto tmp = data_in.subview(data_in.size() - (uncrypted_magic_bytes.size()),
		                           uncrypted_magic_bytes.size());
		auto data = std::vector<uint8_t>(tmp.begin(), tmp.end());
		if (uncrypted_magic_bytes == data) {
			RTC_CHECK_EQ(tmp.size(), uncrypted_magic_bytes.size());
			// magic bytes detected, this is a non-encrypted frame, skip frame
			// decryption.
			webrtc::Buffer data_out;
			data_out.AppendData(data_in.subview(0, data_in.size() - uncrypted_magic_bytes.size()));
			frame->SetData(data_out);
			sink_callback->OnTransformedFrame(std::move(frame));
			return;
		}
	}

	const bool is_av1 = FrameIsAV1(frame.get(), type_);
	size_t unencrypted_bytes = get_unencrypted_bytes(frame.get(), type_);
	if (is_av1 && !ParseAv1EncryptedFrameHeader(data_in, unencrypted_bytes)) {
		RTC_LOG(LS_WARNING) << "FrameCryptorTransformer::decryptFrame() invalid AV1 encrypted "
		                       "frame envelope, size="
		                    << data_in.size() << ", first_byte=" << static_cast<int>(data_in[0]);
		if (last_dec_error_ != FrameCryptionState::kDecryptionFailed) {
			last_dec_error_ = FrameCryptionState::kDecryptionFailed;
			onFrameCryptionStateChanged(last_dec_error_);
		}
		return;
	}

	if (unencrypted_bytes > data_in.size()) {
		if (last_dec_error_ != FrameCryptionState::kDecryptionFailed) {
			last_dec_error_ = FrameCryptionState::kDecryptionFailed;
			onFrameCryptionStateChanged(last_dec_error_);
		}
		return;
	}

	webrtc::Buffer frame_header(unencrypted_bytes);
	for (size_t i = 0; i < unencrypted_bytes; i++) {
		frame_header[i] = data_in[i];
	}

	webrtc::Buffer encrypted_buffer(data_in.size() - unencrypted_bytes);
	for (size_t i = unencrypted_bytes; i < data_in.size(); i++) {
		encrypted_buffer[i - unencrypted_bytes] = data_in[i];
	}

	if (FrameIsH264(frame.get(), type_) &&
	    NeedsRbspUnescaping(encrypted_buffer.data(), encrypted_buffer.size())) {
		encrypted_buffer.SetData(H264::ParseRbsp(encrypted_buffer.data(), encrypted_buffer.size()));
#ifdef RTC_ENABLE_H265
	} else if (FrameIsH265(frame.get(), type_) &&
	           NeedsRbspUnescaping(encrypted_buffer.data(), encrypted_buffer.size())) {
		encrypted_buffer.SetData(H265::ParseRbsp(encrypted_buffer.data(), encrypted_buffer.size()));
#endif // RTC_ENABLE_H265
	}

	if (encrypted_buffer.size() < kFrameTrailerSize) {
		if (last_dec_error_ != FrameCryptionState::kDecryptionFailed) {
			last_dec_error_ = FrameCryptionState::kDecryptionFailed;
			onFrameCryptionStateChanged(last_dec_error_);
		}
		return;
	}
	uint8_t ivLength = encrypted_buffer[encrypted_buffer.size() - 2];
	uint8_t key_index = encrypted_buffer[encrypted_buffer.size() - 1];
	if (ivLength != getIvSize() ||
	    encrypted_buffer.size() < kFrameTrailerSize + ivLength + kGcmTagSize) {
		if (last_dec_error_ != FrameCryptionState::kDecryptionFailed) {
			last_dec_error_ = FrameCryptionState::kDecryptionFailed;
			onFrameCryptionStateChanged(last_dec_error_);
		}
		return;
	}

	auto key_handler = key_provider_->options().shared_key
	                       ? key_provider_->GetSharedKey(participant_id_)
	                       : key_provider_->GetKey(participant_id_);
	if (key_index >= key_provider_->options().key_ring_size || key_handler == nullptr ||
	    key_handler->GetKeySet(key_index) == nullptr) {
		if (last_dec_error_ != FrameCryptionState::kMissingKey) {
			last_dec_error_ = FrameCryptionState::kMissingKey;
			onFrameCryptionStateChanged(last_dec_error_);
		}
		return;
	}
	if (last_dec_error_ == kDecryptionFailed && !key_handler->HasValidKey()) {
		return;
	}
	auto key_set = key_handler->GetKeySet(key_index);

	webrtc::Buffer iv(ivLength);
	for (size_t i = 0; i < ivLength; i++) {
		iv[i] = encrypted_buffer[encrypted_buffer.size() - kFrameTrailerSize - ivLength + i];
	}

	webrtc::Buffer encrypted_payload(encrypted_buffer.size() - ivLength - kFrameTrailerSize);
	for (size_t i = 0; i < encrypted_payload.size(); i++) {
		encrypted_payload[i] = encrypted_buffer[i];
	}

	std::vector<uint8_t> buffer;

	int ratchet_count = 0;
	auto initialKeyMaterial = key_set->material;
	bool decryption_success = false;
	if (AesEncryptDecrypt(EncryptOrDecrypt::kDecrypt, algorithm_, key_set->encryption_key, iv,
	                      frame_header, encrypted_payload, &buffer) == Success) {
		decryption_success = true;
	} else {
		RTC_LOG(LS_WARNING) << "FrameCryptorTransformer::decryptFrame() failed";
		webrtc::scoped_refptr<ParticipantKeyHandler::KeySet> ratcheted_key_set;
		auto currentKeyMaterial = key_set->material;
		if (key_provider_->options().ratchet_window_size > 0) {
			while (ratchet_count < key_provider_->options().ratchet_window_size) {
				ratchet_count++;

				RTC_LOG(LS_INFO) << "ratcheting key attempt " << ratchet_count << " of "
				                 << key_provider_->options().ratchet_window_size;

				auto new_material = key_handler->RatchetKeyMaterial(currentKeyMaterial);
				ratcheted_key_set = key_handler->DeriveKeys(
				    new_material, key_provider_->options().ratchet_salt, 128);
				if (new_material.empty() || !ratcheted_key_set) {
					break;
				}

				if (AesEncryptDecrypt(EncryptOrDecrypt::kDecrypt, algorithm_,
				                      ratcheted_key_set->encryption_key, iv, frame_header,
				                      encrypted_payload, &buffer) == Success) {
					RTC_LOG(LS_INFO) << "FrameCryptorTransformer::decryptFrame() "
					                    "ratcheted to key_index="
					                 << static_cast<int>(key_index);
					decryption_success = true;
					// success, so we set the new key
					key_handler->SetKeyFromMaterial(new_material, key_index);
					key_handler->SetHasValidKey();
					if (last_dec_error_ != FrameCryptionState::kKeyRatcheted) {
						last_dec_error_ = FrameCryptionState::kKeyRatcheted;
						onFrameCryptionStateChanged(last_dec_error_);
					}
					break;
				}
				// for the next ratchet attempt
				currentKeyMaterial = new_material;
			}

			/* Since the key it is first send and only afterwards actually used for
			  encrypting, there were situations when the decrypting failed due to the
			  fact that the received frame was not encrypted yet and ratcheting, of
			  course, did not solve the problem. So if we fail RATCHET_WINDOW_SIZE
			  times, we come back to the initial key.
			 */
			if (!decryption_success) {
				key_handler->SetKeyFromMaterial(initialKeyMaterial, key_index);
			}
		}
	}

	if (!decryption_success) {
		if (key_handler->DecryptionFailure()) {
			if (last_dec_error_ != FrameCryptionState::kDecryptionFailed) {
				last_dec_error_ = FrameCryptionState::kDecryptionFailed;
				onFrameCryptionStateChanged(last_dec_error_);
			}
		}
		return;
	}

	webrtc::Buffer payload(buffer.data(), buffer.size());
	webrtc::Buffer data_out;
	if (!is_av1) {
		data_out.AppendData(frame_header);
	}
	data_out.AppendData(payload);
	frame->SetData(data_out);

	if (last_dec_error_ != FrameCryptionState::kOk) {
		last_dec_error_ = FrameCryptionState::kOk;
		onFrameCryptionStateChanged(last_dec_error_);
	}
	sink_callback->OnTransformedFrame(std::move(frame));
}

void FrameCryptorTransformer::onFrameCryptionStateChanged(FrameCryptionState state) {
	webrtc::MutexLock lock(&mutex_);
	if (observer_) {
		RTC_DCHECK(signaling_thread_ != nullptr);
		signaling_thread_->PostTask(
		    [observer = observer_, state = state, participant_id = participant_id_]() mutable {
			    observer->OnFrameCryptionStateChanged(participant_id, state);
		    });
	}
}

webrtc::Buffer FrameCryptorTransformer::makeIv(uint32_t ssrc, uint32_t timestamp) {
	uint32_t send_count = 0;
	if (send_counts_.find(ssrc) == send_counts_.end()) {
		send_counts_[ssrc] = floor(CreateRandomNonZeroId() * 0xFFFF);
	} else {
		send_count = send_counts_[ssrc];
	}
	webrtc::ByteBufferWriter buf;
	buf.WriteUInt32(ssrc);
	buf.WriteUInt32(timestamp);
	buf.WriteUInt32(timestamp - (send_count % 0xFFFF));
	send_counts_[ssrc] = send_count + 1;

	RTC_CHECK_EQ(buf.Length(), getIvSize());

	return webrtc::Buffer(buf.Data(), buf.Length());
}

uint8_t FrameCryptorTransformer::getIvSize() {
	switch (algorithm_) {
	case Algorithm::kAesGcm:
		return 12;
	default:
		return 0;
	}
}

DataPacketCryptor::DataPacketCryptor(FrameCryptorTransformer::Algorithm algorithm,
                                     webrtc::scoped_refptr<KeyProvider> key_provider)
    : algorithm_(algorithm), key_provider_(key_provider) {
	RTC_DCHECK(key_provider_ != nullptr);
}

DataPacketCryptor::~DataPacketCryptor() {}

RTCErrorOr<webrtc::scoped_refptr<EncryptedPacket>>
DataPacketCryptor::Encrypt(const std::string participant_id, int key_index,
                           const std::vector<uint8_t>& data) {
	auto key_handler = key_provider_->options().shared_key
	                       ? key_provider_->GetSharedKey(participant_id)
	                       : key_provider_->GetKey(participant_id);

	if (key_handler == nullptr || key_handler->GetKeySet(key_index) == nullptr) {
		RTC_LOG(LS_INFO) << "DataPacketCryptor::Encrypt() no keys, or "
		                    "key_index["
		                 << key_index << "] out of range for participant " << participant_id;
		return RTCError(RTCErrorType::INVALID_PARAMETER,
		                "DataPacketCryptor::Encrypt() no keys, or key_index[" +
		                    std::to_string(key_index) + "] out of range for participant " +
		                    participant_id);
	}

	auto key_set = key_handler->GetKeySet(key_index);
	auto timestamp =
	    Timestamp::Millis(webrtc::TimeMillis()).ms(); // use current time millis as timestamp
	auto iv = makeIv(timestamp);                      // for data packets, ssrc is always 0

	std::vector<uint8_t> buffer;
	webrtc::Buffer payload(data.data(), data.size());
	auto frame_header = webrtc::Buffer(0); // no frame header for data packets
	if (AesEncryptDecrypt(EncryptOrDecrypt::kEncrypt, algorithm_, key_set->encryption_key, iv,
	                      frame_header, payload, &buffer) == Success) {
		webrtc::scoped_refptr<EncryptedPacket> encryptedPacket =
		    webrtc::make_ref_counted<EncryptedPacket>(
		        buffer, std::vector<uint8_t>(iv.begin(), iv.end()), key_index);
		return encryptedPacket;
	}

	return RTCError(RTCErrorType::INTERNAL_ERROR, "DataPacketCryptor::Encrypt() failed");
}

RTCErrorOr<std::vector<uint8_t>>
DataPacketCryptor::Decrypt(const std::string participant_id,
                           const webrtc::scoped_refptr<EncryptedPacket> encryptedPacket) {
	if (!encryptedPacket || encryptedPacket->iv.size() != 12 || encryptedPacket->data.size() < 16) {
		return RTCError(RTCErrorType::INVALID_PARAMETER,
		                "DataPacketCryptor::Decrypt() invalid encrypted packet");
	}
	auto key_handler = key_provider_->options().shared_key
	                       ? key_provider_->GetSharedKey(participant_id)
	                       : key_provider_->GetKey(participant_id);
	int key_index = encryptedPacket->key_index;
	if (key_handler == nullptr || key_handler->GetKeySet(key_index) == nullptr) {
		RTC_LOG(LS_INFO) << "DataPacketCryptor::Decrypt() no keys, or "
		                    "key_index["
		                 << key_index << "] out of range for participant " << participant_id;
		return RTCError(RTCErrorType::INVALID_PARAMETER,
		                "DataPacketCryptor::Decrypt() no keys, or key_index[" +
		                    std::to_string(key_index) + "] out of range for participant " +
		                    participant_id);
	}

	std::vector<uint8_t> buffer;
	webrtc::Buffer encrypted_payload(encryptedPacket->data.data(), encryptedPacket->data.size());
	webrtc::Buffer iv(encryptedPacket->iv.data(), encryptedPacket->iv.size());
	auto frame_header = webrtc::Buffer(0); // no frame header for data packets

	auto key_set = key_handler->GetKeySet(key_index);
	auto initialKeyMaterial = key_set->material;
	bool decryption_success = false;

	if (AesEncryptDecrypt(EncryptOrDecrypt::kDecrypt, algorithm_, key_set->encryption_key, iv,
	                      frame_header, encrypted_payload, &buffer) == Success) {
		decryption_success = true;
	} else {
		RTC_LOG(LS_WARNING) << "DataPacketCryptor::Decrypt() failed with key_index "
		                    << static_cast<int>(key_index);
		webrtc::scoped_refptr<ParticipantKeyHandler::KeySet> ratcheted_key_set;
		auto currentKeyMaterial = key_set->material;
		int ratchet_count = 0;
		if (key_provider_->options().ratchet_window_size > 0) {
			while (ratchet_count < key_provider_->options().ratchet_window_size) {
				ratchet_count++;

				RTC_LOG(LS_INFO) << "ratcheting key attempt " << ratchet_count << " of "
				                 << key_provider_->options().ratchet_window_size;

				auto new_material = key_handler->RatchetKeyMaterial(currentKeyMaterial);
				ratcheted_key_set = key_handler->DeriveKeys(
				    new_material, key_provider_->options().ratchet_salt, 128);
				if (new_material.empty() || !ratcheted_key_set) {
					break;
				}

				if (AesEncryptDecrypt(EncryptOrDecrypt::kDecrypt, algorithm_,
				                      ratcheted_key_set->encryption_key, iv, frame_header,
				                      encrypted_payload, &buffer) == Success) {
					RTC_LOG(LS_INFO) << "DataPacketCryptor::Decrypt() successfully "
					                    "ratcheted to key_index="
					                 << static_cast<int>(key_index);
					decryption_success = true;
					// success, so we set the new key
					key_handler->SetKeyFromMaterial(new_material, key_index);
					key_handler->SetHasValidKey();
					break;
				}
				// for the next ratchet attempt
				currentKeyMaterial = new_material;
			}

			/* Since the key it is first send and only afterwards actually used for
			  encrypting, there were situations when the decrypting failed due to the
			  fact that the received frame was not encrypted yet and ratcheting, of
			  course, did not solve the problem. So if we fail RATCHET_WINDOW_SIZE
			  times, we come back to the initial key.
			 */
			if (!decryption_success) {
				key_handler->SetKeyFromMaterial(initialKeyMaterial, key_index);
			}
		}
	}

	if (decryption_success) {
		return buffer;
	}

	return RTCError(RTCErrorType::INTERNAL_ERROR, "DataPacketCryptor::Decrypt() failed");
}

webrtc::Buffer DataPacketCryptor::makeIv(uint32_t timestamp) {
	if (send_count_ == 0) {
		send_count_ = floor(CreateRandomNonZeroId() * 0xFFFF);
	}
	webrtc::ByteBufferWriter buf;
	uint32_t random_u32 = CreateRandomId();
	buf.WriteUInt32(random_u32);
	buf.WriteUInt32(timestamp);
	buf.WriteUInt32(timestamp - (send_count_ % 0xFFFF));
	send_count_ += 1;

	RTC_CHECK_EQ(buf.Length(), 12);

	return webrtc::Buffer(buf.Data(), buf.Length());
}

} // namespace webrtc
