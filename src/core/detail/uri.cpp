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

#include "uri.h"
#include "strings.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace livekit {
namespace core {
Url::Url(std::string const& url) {
	auto urlIter = url.cbegin();

	{
		std::string const SchemeEnd = "://";
		auto const schemePos = url.find(SchemeEnd);

		if (schemePos != std::string::npos) {
			scheme_ = detail::StringExtensions::ToLower(url.substr(0, schemePos));
			urlIter += schemePos + SchemeEnd.length();
		}
	}

	{
		auto const hostIter = std::find_if(urlIter, url.end(),
		                                   [](auto c) { return c == '/' || c == '?' || c == ':'; });

		host_ = std::string(urlIter, hostIter);
		urlIter = hostIter;
	}

	if (urlIter == url.end()) {
		return;
	}

	if (*urlIter == ':') {
		++urlIter;
		auto const portIter =
		    std::find_if_not(urlIter, url.end(), detail::StringExtensions::IsDigit);

		auto const portNumber = std::stoi(std::string(urlIter, portIter));

		// stoi will throw out_of_range when `int` is overflow, but we need to throw if uint16
		// is overflow
		{
			constexpr auto const MaxPortNumberSupported = (std::numeric_limits<uint16_t>::max)();
			if (portNumber > MaxPortNumberSupported) {
				throw std::out_of_range(
				    "The port number is out of range. The max supported number is " +
				    std::to_string(MaxPortNumberSupported) + ".");
			}
		}

		// cast is safe because the overflow was detected before
		port_ = static_cast<uint16_t>(portNumber);
		urlIter = portIter;
	}

	if (urlIter == url.end()) {
		return;
	}

	if (*urlIter != '/' && *urlIter != '?') {
		// only char '/' or '?' is valid after the port (or the end of the URL). Any other char
		// is an invalid input
		throw std::invalid_argument("The port number contains invalid characters.");
	}

	if (*urlIter == '/') {
		++urlIter;

		auto const pathIter = std::find(urlIter, url.end(), '?');
		encoded_path_ = std::string(urlIter, pathIter);

		urlIter = pathIter;
	}

	if (urlIter != url.end() && *urlIter == '?') {
		++urlIter;
		AppendQueryParameters(std::string(urlIter, std::find(urlIter, url.end(), '#')));
	}
}

std::string Url::Decode(std::string const& value) {
	std::string decodedValue;
	auto const valueSize = value.size();
	for (size_t i = 0; i < valueSize; ++i) {
		auto const c = value[i];
		switch (c) {
		case '%':
			if ((valueSize - i) < 3 // need at least 3 characters: "%XY"
			    || !detail::StringExtensions::IsHexDigit(value[i + 1]) ||
			    !detail::StringExtensions::IsHexDigit(value[i + 2])) {
				throw std::runtime_error("failed when decoding URL component");
			}

			decodedValue += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
			i += 2;
			break;

		case '+':
			decodedValue += ' ';
			break;

		default:
			decodedValue += c;
			break;
		}
	}

	return decodedValue;
}

namespace {
bool ShouldEncode(char c) {
	static std::unordered_set<char> const ExtraNonEncodableChars = {'-', '.', '_', '~'};

	return !detail::StringExtensions::IsAlphaNumeric(c) &&
	       ExtraNonEncodableChars.find(c) == ExtraNonEncodableChars.end();
}
} // namespace

std::string Url::Encode(const std::string& value, const std::string& doNotEncodeSymbols) {
	auto const Hex = "0123456789ABCDEF";

	std::unordered_set<char> const doNotEncodeSymbolsSet(doNotEncodeSymbols.begin(),
	                                                     doNotEncodeSymbols.end());

	std::string encoded;
	for (auto const c : value) {
		// encode if char is not in the default non-encoding set AND if it is NOT in chars to ignore
		// from user input
		if (ShouldEncode(c) && doNotEncodeSymbolsSet.find(c) == doNotEncodeSymbolsSet.end()) {
			auto const u8 = static_cast<uint8_t>(c);

			encoded += '%';
			encoded += Hex[(u8 >> 4) & 0x0f];
			encoded += Hex[u8 & 0x0f];
		} else {
			encoded += c;
		}
	}

	return encoded;
}

void Url::AppendQueryParameters(const std::string& query) {
	std::string::const_iterator cur = query.begin();
	if (cur != query.end() && *cur == '?') {
		++cur;
	}

	while (cur != query.end()) {
		auto key_end = std::find(cur, query.end(), '=');
		std::string query_key = std::string(cur, key_end);

		cur = key_end;
		if (cur != query.end()) {
			++cur;
		}

		auto value_end = std::find(cur, query.end(), '&');
		std::string query_value = std::string(cur, value_end);

		cur = value_end;
		if (cur != query.end()) {
			++cur;
		}
		encoded_query_parameters_[std::move(query_key)] = std::move(query_value);
	}
}

std::string Url::GetUrlWithoutQuery(bool relative) const {
	std::string url;

	if (!relative) {
		if (!scheme_.empty()) {
			url += scheme_ + "://";
		}
		url += host_;
		if (port_ != 0) {
			url += ":" + std::to_string(port_);
		}
	}

	if (!encoded_path_.empty()) {
		if (!relative) {
			if (encoded_path_[0] != '/') {
				url += "/";
			}
		}

		url += encoded_path_;
	}

	return url;
}

std::string Url::GetRelativeUrl() const {
	return GetUrlWithoutQuery(true) +
	       detail::FormatEncodedUrlQueryParameters(encoded_query_parameters_);
}

std::string Url::GetAbsoluteUrl() const {
	return GetUrlWithoutQuery(false) +
	       detail::FormatEncodedUrlQueryParameters(encoded_query_parameters_);
}

} // namespace core
} // namespace livekit