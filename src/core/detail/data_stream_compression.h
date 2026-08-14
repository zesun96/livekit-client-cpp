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

#pragma once

#ifndef _LKC_CORE_DETAIL_DATA_STREAM_COMPRESSION_H_
#define _LKC_CORE_DETAIL_DATA_STREAM_COMPRESSION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace livekit {
namespace core {
namespace detail {

class DeflateRawStream {
public:
	DeflateRawStream();
	~DeflateRawStream();
	DeflateRawStream(const DeflateRawStream&) = delete;
	DeflateRawStream& operator=(const DeflateRawStream&) = delete;

	bool IsValid() const noexcept;
	bool Write(const uint8_t* data, std::size_t size, std::vector<uint8_t>& output);
	bool Finish(std::vector<uint8_t>& output);

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

class InflateRawStream {
public:
	explicit InflateRawStream(uint64_t maximum_output_size);
	~InflateRawStream();
	InflateRawStream(const InflateRawStream&) = delete;
	InflateRawStream& operator=(const InflateRawStream&) = delete;

	bool IsValid() const noexcept;
	bool Write(const uint8_t* data, std::size_t size, std::vector<uint8_t>& output);
	bool Finished() const noexcept;
	uint64_t OutputSize() const noexcept;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace detail
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_DATA_STREAM_COMPRESSION_H_
