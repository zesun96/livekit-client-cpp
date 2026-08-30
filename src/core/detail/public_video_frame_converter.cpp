/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "public_video_frame_converter.h"

#include "libyuv/convert.h"
#include "libyuv/planar_functions.h"

#include <cstdint>
#include <limits>

namespace livekit::core::detail {
namespace {

struct PlaneShape {
	std::size_t row_bytes = 0;
	std::size_t rows = 0;
};

bool CheckedMultiply(std::size_t left, std::size_t right, std::size_t& result) {
	if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
		return false;
	}
	result = left * right;
	return true;
}

bool PlaneShapes(VideoBufferType format, std::size_t width, std::size_t height,
                 std::vector<PlaneShape>& shapes) {
	const auto chroma_width = (width + 1U) / 2U;
	const auto chroma_height = (height + 1U) / 2U;
	shapes.clear();
	switch (format) {
	case VideoBufferType::RGBA:
	case VideoBufferType::ABGR:
	case VideoBufferType::ARGB:
	case VideoBufferType::BGRA:
		if (width > std::numeric_limits<std::size_t>::max() / 4U) {
			return false;
		}
		shapes.push_back({width * 4U, height});
		return true;
	case VideoBufferType::RGB24:
		if (width > std::numeric_limits<std::size_t>::max() / 3U) {
			return false;
		}
		shapes.push_back({width * 3U, height});
		return true;
	case VideoBufferType::I420:
		shapes = {{width, height}, {chroma_width, chroma_height}, {chroma_width, chroma_height}};
		return true;
	case VideoBufferType::I420A:
		shapes = {{width, height},
		          {chroma_width, chroma_height},
		          {chroma_width, chroma_height},
		          {width, height}};
		return true;
	case VideoBufferType::I422:
		shapes = {{width, height}, {chroma_width, height}, {chroma_width, height}};
		return true;
	case VideoBufferType::I444:
		shapes = {{width, height}, {width, height}, {width, height}};
		return true;
	case VideoBufferType::I010:
		if (width > std::numeric_limits<std::size_t>::max() / 2U ||
		    chroma_width > std::numeric_limits<std::size_t>::max() / 2U) {
			return false;
		}
		shapes = {{width * 2U, height},
		          {chroma_width * 2U, chroma_height},
		          {chroma_width * 2U, chroma_height}};
		return true;
	case VideoBufferType::NV12:
		if (chroma_width > std::numeric_limits<std::size_t>::max() / 2U) {
			return false;
		}
		shapes = {{width, height}, {chroma_width * 2U, chroma_height}};
		return true;
	}
	return false;
}

bool ValidRotation(VideoRotation rotation) {
	return rotation == VideoRotation::Rotation0 || rotation == VideoRotation::Rotation90 ||
	       rotation == VideoRotation::Rotation180 || rotation == VideoRotation::Rotation270;
}

} // namespace

bool ResolveVideoFramePlanes(const VideoFrame& frame, std::vector<VideoPlaneInfo>& planes) {
	planes.clear();
	if (frame.width == 0 || frame.height == 0 ||
	    frame.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
	    frame.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
	    !ValidRotation(frame.rotation)) {
		return false;
	}

	std::vector<PlaneShape> shapes;
	if (!PlaneShapes(frame.format, frame.width, frame.height, shapes)) {
		return false;
	}

	if (frame.planes.empty()) {
		std::size_t offset = 0;
		for (const auto& shape : shapes) {
			std::size_t size = 0;
			if (shape.row_bytes > std::numeric_limits<std::uint32_t>::max() ||
			    !CheckedMultiply(shape.row_bytes, shape.rows, size) || offset > frame.data.size() ||
			    size > frame.data.size() - offset) {
				return false;
			}
			planes.push_back({offset, size, static_cast<std::uint32_t>(shape.row_bytes)});
			offset += size;
		}
		return offset == frame.data.size();
	}

	if (frame.planes.size() != shapes.size()) {
		return false;
	}
	planes = frame.planes;
	for (std::size_t index = 0; index < shapes.size(); ++index) {
		const auto& plane = planes[index];
		const auto& shape = shapes[index];
		if (shape.row_bytes > std::numeric_limits<std::uint32_t>::max() ||
		    plane.stride < shape.row_bytes ||
		    plane.stride > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
		    plane.offset > frame.data.size() || plane.size > frame.data.size() - plane.offset) {
			return false;
		}
		std::size_t required = shape.row_bytes;
		if (shape.rows > 1U) {
			std::size_t preceding_rows = 0;
			if (!CheckedMultiply(plane.stride, shape.rows - 1U, preceding_rows) ||
			    preceding_rows > std::numeric_limits<std::size_t>::max() - required) {
				return false;
			}
			required += preceding_rows;
		}
		if (plane.size < required) {
			return false;
		}
	}
	return true;
}

bool ConvertVideoFrameToI420(const VideoFrame& frame, std::vector<std::uint8_t>& i420) {
	std::vector<VideoPlaneInfo> planes;
	if (!ResolveVideoFramePlanes(frame, planes)) {
		return false;
	}

	const auto width = static_cast<int>(frame.width);
	const auto height = static_cast<int>(frame.height);
	std::size_t y_size = 0;
	if (!CheckedMultiply(frame.width, frame.height, y_size)) {
		return false;
	}
	const std::size_t chroma_width = (static_cast<std::size_t>(frame.width) + 1U) / 2U;
	const std::size_t chroma_height = (static_cast<std::size_t>(frame.height) + 1U) / 2U;
	std::size_t chroma_size = 0;
	std::size_t combined_chroma_size = 0;
	if (!CheckedMultiply(chroma_width, chroma_height, chroma_size) ||
	    !CheckedMultiply(chroma_size, 2U, combined_chroma_size) ||
	    y_size > std::numeric_limits<std::size_t>::max() - combined_chroma_size) {
		return false;
	}
	i420.resize(y_size + combined_chroma_size);
	auto* destination_y = i420.data();
	auto* destination_u = destination_y + y_size;
	auto* destination_v = destination_u + chroma_size;
	const auto destination_stride_y = width;
	const auto destination_stride_uv = static_cast<int>(chroma_width);
	auto source = [&](std::size_t index) { return frame.data.data() + planes[index].offset; };
	auto stride = [&](std::size_t index) { return static_cast<int>(planes[index].stride); };

	int result = -1;
	switch (frame.format) {
	case VideoBufferType::RGBA:
		result = libyuv::ABGRToI420(source(0), stride(0), destination_y, destination_stride_y,
		                            destination_u, destination_stride_uv, destination_v,
		                            destination_stride_uv, width, height);
		break;
	case VideoBufferType::ABGR:
		result = libyuv::RGBAToI420(source(0), stride(0), destination_y, destination_stride_y,
		                            destination_u, destination_stride_uv, destination_v,
		                            destination_stride_uv, width, height);
		break;
	case VideoBufferType::ARGB:
		result = libyuv::BGRAToI420(source(0), stride(0), destination_y, destination_stride_y,
		                            destination_u, destination_stride_uv, destination_v,
		                            destination_stride_uv, width, height);
		break;
	case VideoBufferType::BGRA:
		result = libyuv::ARGBToI420(source(0), stride(0), destination_y, destination_stride_y,
		                            destination_u, destination_stride_uv, destination_v,
		                            destination_stride_uv, width, height);
		break;
	case VideoBufferType::RGB24:
		result = libyuv::RAWToI420(source(0), stride(0), destination_y, destination_stride_y,
		                           destination_u, destination_stride_uv, destination_v,
		                           destination_stride_uv, width, height);
		break;
	case VideoBufferType::I420:
	case VideoBufferType::I420A:
		result = libyuv::I420Copy(source(0), stride(0), source(1), stride(1), source(2), stride(2),
		                          destination_y, destination_stride_y, destination_u,
		                          destination_stride_uv, destination_v, destination_stride_uv,
		                          width, height);
		break;
	case VideoBufferType::I422:
		result = libyuv::I422ToI420(source(0), stride(0), source(1), stride(1), source(2),
		                            stride(2), destination_y, destination_stride_y, destination_u,
		                            destination_stride_uv, destination_v, destination_stride_uv,
		                            width, height);
		break;
	case VideoBufferType::I444:
		result = libyuv::I444ToI420(source(0), stride(0), source(1), stride(1), source(2),
		                            stride(2), destination_y, destination_stride_y, destination_u,
		                            destination_stride_uv, destination_v, destination_stride_uv,
		                            width, height);
		break;
	case VideoBufferType::I010:
		if ((reinterpret_cast<std::uintptr_t>(source(0)) & 1U) != 0 ||
		    (reinterpret_cast<std::uintptr_t>(source(1)) & 1U) != 0 ||
		    (reinterpret_cast<std::uintptr_t>(source(2)) & 1U) != 0 ||
		    (planes[0].stride & 1U) != 0 || (planes[1].stride & 1U) != 0 ||
		    (planes[2].stride & 1U) != 0) {
			return false;
		}
		result = libyuv::I010ToI420(
		    reinterpret_cast<const std::uint16_t*>(source(0)), stride(0) / 2,
		    reinterpret_cast<const std::uint16_t*>(source(1)), stride(1) / 2,
		    reinterpret_cast<const std::uint16_t*>(source(2)), stride(2) / 2, destination_y,
		    destination_stride_y, destination_u, destination_stride_uv, destination_v,
		    destination_stride_uv, width, height);
		break;
	case VideoBufferType::NV12:
		result = libyuv::NV12ToI420(source(0), stride(0), source(1), stride(1), destination_y,
		                            destination_stride_y, destination_u, destination_stride_uv,
		                            destination_v, destination_stride_uv, width, height);
		break;
	}
	if (result != 0) {
		i420.clear();
		return false;
	}
	return true;
}

} // namespace livekit::core::detail
