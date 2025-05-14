/**
 *
 * Copyright (c) 2025 sunze
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

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#ifndef _LKC_CORE_DETAIL_STRINGS_H_
#define _LKC_CORE_DETAIL_STRINGS_H_

namespace livekit {
namespace core {
namespace detail {
// from:
// https://github.com/Azure/azure-sdk-for-cpp/blob/main/sdk/core/azure-core/inc/azure/core/internal/strings.hpp
/**
 * @brief Extend the functionality of std::string by offering static methods for string
 * operations.
 */
struct StringExtensions final {
	static constexpr char ToUpper(char c) noexcept {
		return (c < 'a' || c > 'z') ? c : c - ('a' - 'A');
	}

	static constexpr char ToLower(char c) noexcept {
		return (c < 'A' || c > 'Z') ? c : c + ('a' - 'A');
	}

	static constexpr bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }

	static constexpr bool IsHexDigit(char c) noexcept {
		return IsDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
	}

	static constexpr bool IsAlphaNumeric(char c) noexcept {
		return IsDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	}

	static constexpr bool IsSpace(char c) noexcept { return c == ' ' || (c >= '\t' && c <= '\r'); }

	static constexpr bool IsPrintable(char c) noexcept { return c >= ' ' && c <= '~'; }

	struct CaseInsensitiveComparator final {
		bool operator()(std::string const& lhs, std::string const& rhs) const {
			return std::lexicographical_compare(
			    lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
			    [](auto l, auto r) { return ToLower(l) < ToLower(r); });
		}
	};

	static bool LocaleInvariantCaseInsensitiveEqual(std::string const& lhs,
	                                                std::string const& rhs) noexcept {
		auto const rhsSize = rhs.size();
		if (lhs.size() != rhsSize) {
			return false;
		}

		auto const lhsData = lhs.c_str();
		auto const rhsData = rhs.c_str();
		for (size_t i = 0; i < rhsSize; ++i) {
			if (ToLower(lhsData[i]) != ToLower(rhsData[i])) {
				return false;
			}
		}

		return true;
	}

	static std::string ToLower(std::string src) {
		std::transform(src.begin(), src.end(), src.begin(), [](auto c) { return ToLower(c); });
		return src;
	}

	static std::string ToUpper(std::string src) {
		std::transform(src.begin(), src.end(), src.begin(), [](auto c) { return ToUpper(c); });
		return src;
	}

	static std::vector<std::string> Split(const std::string& s, char separator,
	                                      bool removeEmptyEntries = false) {
		std::vector<std::string> result;

		const auto len = s.size();
		size_t start = 0;
		while (start < len) {
			auto end = s.find(separator, start);
			if (end == std::string::npos) {
				end = len;
			}
			if (!removeEmptyEntries || start < end) {
				result.push_back(s.substr(start, end - start));
			}

			start = end + 1;
		}

		return result;
	}
};
} // namespace detail

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_STRINGS_H_