#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace livekit::test_support {

inline std::uint32_t ReadLittleEndian32(const std::array<std::uint8_t, 4>& bytes) {
	return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
	       (static_cast<std::uint32_t>(bytes[2]) << 16) |
	       (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline std::vector<std::int16_t> LoadPcm16Mono48Khz(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		throw std::runtime_error("failed to open audio fixture: " + path.string());
	}
	std::array<char, 12> riff{};
	input.read(riff.data(), riff.size());
	if (!input || std::string_view(riff.data(), 4) != "RIFF" ||
	    std::string_view(riff.data() + 8, 4) != "WAVE") {
		throw std::runtime_error("audio fixture is not a RIFF/WAVE file");
	}
	bool valid_format = false;
	while (input) {
		std::array<char, 4> chunk_id{};
		std::array<std::uint8_t, 4> chunk_size_bytes{};
		input.read(chunk_id.data(), chunk_id.size());
		input.read(reinterpret_cast<char*>(chunk_size_bytes.data()), chunk_size_bytes.size());
		if (!input) {
			break;
		}
		const auto chunk_size = ReadLittleEndian32(chunk_size_bytes);
		if (std::string_view(chunk_id.data(), 4) == "fmt ") {
			std::vector<std::uint8_t> format(chunk_size);
			input.read(reinterpret_cast<char*>(format.data()), format.size());
			if (format.size() >= 16) {
				const auto read16 = [&](std::size_t offset) {
					return static_cast<std::uint16_t>(format[offset]) |
					       (static_cast<std::uint16_t>(format[offset + 1]) << 8);
				};
				const std::array<std::uint8_t, 4> rate_bytes{format[4], format[5], format[6],
				                                             format[7]};
				valid_format = read16(0) == 1 && read16(2) == 1 &&
				               ReadLittleEndian32(rate_bytes) == 48000 && read16(14) == 16;
			}
		} else if (std::string_view(chunk_id.data(), 4) == "data") {
			if (!valid_format || chunk_size % 2 != 0) {
				throw std::runtime_error("audio fixture must be 48 kHz mono PCM16");
			}
			std::vector<std::uint8_t> bytes(chunk_size);
			input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
			std::vector<std::int16_t> samples(bytes.size() / 2);
			for (std::size_t index = 0; index < samples.size(); ++index) {
				samples[index] = static_cast<std::int16_t>(
				    static_cast<std::uint16_t>(bytes[index * 2]) |
				    (static_cast<std::uint16_t>(bytes[index * 2 + 1]) << 8));
			}
			return samples;
		} else {
			input.seekg(chunk_size, std::ios::cur);
		}
		if (chunk_size % 2 != 0) {
			input.seekg(1, std::ios::cur);
		}
	}
	throw std::runtime_error("audio fixture has no PCM data chunk");
}

} // namespace livekit::test_support
