/**
 * Copyright (c) 2026 sunze
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

#include "data_stream_compression.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <limits>

namespace livekit {
namespace core {
namespace detail {
namespace {

constexpr std::size_t kCompressionBufferSize = 16 * 1024;

} // namespace

class DeflateRawStream::Impl {
public:
	Impl() {
		stream_.zalloc = Z_NULL;
		stream_.zfree = Z_NULL;
		stream_.opaque = Z_NULL;
		valid_ = deflateInit2(&stream_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
		                      Z_DEFAULT_STRATEGY) == Z_OK;
	}

	~Impl() {
		if (valid_) {
			deflateEnd(&stream_);
		}
	}

	bool Write(const uint8_t* data, std::size_t size, int flush, std::vector<uint8_t>& output) {
		if (!valid_ || finished_ || (data == nullptr && size != 0) ||
		    size > std::numeric_limits<uInt>::max()) {
			return false;
		}
		stream_.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
		stream_.avail_in = static_cast<uInt>(size);
		std::array<uint8_t, kCompressionBufferSize> buffer{};
		int result = Z_OK;
		do {
			stream_.next_out = buffer.data();
			stream_.avail_out = static_cast<uInt>(buffer.size());
			result = deflate(&stream_, flush);
			if (result != Z_OK && result != Z_STREAM_END) {
				return false;
			}
			output.insert(output.end(), buffer.begin(), buffer.end() - stream_.avail_out);
		} while (stream_.avail_in != 0 || stream_.avail_out == 0 ||
		         (flush == Z_FINISH && result != Z_STREAM_END));
		if (result == Z_STREAM_END) {
			finished_ = true;
		}
		return flush != Z_FINISH || finished_;
	}

	bool valid_ = false;
	bool finished_ = false;
	z_stream stream_{};
};

DeflateRawStream::DeflateRawStream() : impl_(std::make_unique<Impl>()) {}
DeflateRawStream::~DeflateRawStream() = default;
bool DeflateRawStream::IsValid() const noexcept { return impl_ && impl_->valid_; }

bool DeflateRawStream::Write(const uint8_t* data, std::size_t size, std::vector<uint8_t>& output) {
	return impl_ && impl_->Write(data, size, Z_SYNC_FLUSH, output);
}

bool DeflateRawStream::Finish(std::vector<uint8_t>& output) {
	return impl_ && impl_->Write(nullptr, 0, Z_FINISH, output);
}

class InflateRawStream::Impl {
public:
	explicit Impl(uint64_t maximum_output_size) : maximum_output_size_(maximum_output_size) {
		stream_.zalloc = Z_NULL;
		stream_.zfree = Z_NULL;
		stream_.opaque = Z_NULL;
		valid_ = inflateInit2(&stream_, -MAX_WBITS) == Z_OK;
	}

	~Impl() {
		if (valid_) {
			inflateEnd(&stream_);
		}
	}

	bool Write(const uint8_t* data, std::size_t size, std::vector<uint8_t>& output) {
		if (!valid_ || finished_ || (data == nullptr && size != 0) ||
		    size > std::numeric_limits<uInt>::max()) {
			return false;
		}
		stream_.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
		stream_.avail_in = static_cast<uInt>(size);
		std::array<uint8_t, kCompressionBufferSize> buffer{};
		do {
			stream_.next_out = buffer.data();
			const auto output_capacity = static_cast<uInt>(
			    std::min<uint64_t>(buffer.size(), maximum_output_size_ - output_size_ + 1));
			stream_.avail_out = output_capacity;
			if (stream_.avail_out == 0) {
				return false;
			}
			const auto result = inflate(&stream_, Z_NO_FLUSH);
			if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR) {
				return false;
			}
			const auto produced = output_capacity - stream_.avail_out;
			if (produced > maximum_output_size_ - output_size_) {
				return false;
			}
			output.insert(output.end(), buffer.begin(), buffer.begin() + produced);
			output_size_ += produced;
			if (result == Z_STREAM_END) {
				finished_ = true;
				return stream_.avail_in == 0;
			}
			if (result == Z_BUF_ERROR && produced == 0) {
				return stream_.avail_in == 0;
			}
		} while (stream_.avail_in != 0 || stream_.avail_out == 0);
		return true;
	}

	bool valid_ = false;
	bool finished_ = false;
	uint64_t maximum_output_size_ = 0;
	uint64_t output_size_ = 0;
	z_stream stream_{};
};

InflateRawStream::InflateRawStream(uint64_t maximum_output_size)
    : impl_(std::make_unique<Impl>(maximum_output_size)) {}
InflateRawStream::~InflateRawStream() = default;
bool InflateRawStream::IsValid() const noexcept { return impl_ && impl_->valid_; }

bool InflateRawStream::Write(const uint8_t* data, std::size_t size, std::vector<uint8_t>& output) {
	return impl_ && impl_->Write(data, size, output);
}

bool InflateRawStream::Finished() const noexcept { return impl_ && impl_->finished_; }
uint64_t InflateRawStream::OutputSize() const noexcept { return impl_ ? impl_->output_size_ : 0; }

} // namespace detail
} // namespace core
} // namespace livekit
