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

#ifndef _LKC_CORE_DETAIL_URI_H_
#define _LKC_CORE_DETAIL_URI_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace livekit {
namespace core {

namespace detail {
inline std::string
FormatEncodedUrlQueryParameters(std::map<std::string, std::string> const& encodedQueryParameters) {
	{
		std::string queryStr;
		if (!encodedQueryParameters.empty()) {
			auto separator = '?';
			for (const auto& q : encodedQueryParameters) {
				queryStr += separator + q.first + '=' + q.second;
				separator = '&';
			}
		}

		return queryStr;
	}
}
} // namespace detail

class Url final {
public:
	/**
	 * @brief Decodes \p value by transforming all escaped characters to it's non-encoded value.
	 *
	 * @param value URL-encoded string.
	 * @return `std::string` with non-URL encoded values.
	 */
	static std::string Decode(const std::string& value);

	/**
	 * @brief Encodes \p value by escaping characters to the form of %HH where HH are hex digits.
	 *
	 * @note \p doNotEncodeSymbols arg can be used to explicitly ask this function to skip
	 * characters from encoding. For instance, using this `= -` input would prevent encoding `=`, `
	 * ` and `-`.
	 *
	 * @param value Non URL-encoded string.
	 * @param doNotEncodeSymbols A string consisting of characters that do not need to be encoded.
	 * @return std::string
	 */
	static std::string Encode(const std::string& value, const std::string& doNotEncodeSymbols = "");

public:
	/**
	 * @brief Constructs a new, empty URL object.
	 *
	 */
	Url() = default;

	/**
	 * @brief Constructs a URL from a URL-encoded string.
	 *
	 * @param encodedUrl A URL-encoded string.
	 * @note encodedUrl is expected to have all parts URL-encoded.
	 */
	explicit Url(const std::string& encodedUrl);

	/**
	 * @brief Sets URL scheme.
	 *
	 * @param scheme URL scheme.
	 */
	void SetScheme(const std::string& scheme) { scheme_ = scheme; }

	/**
	 * @brief Sets URL host.
	 *
	 * @param encodedHost URL host, already encoded.
	 */
	void SetHost(const std::string& encodedHost) { host_ = encodedHost; }

	/**
	 * @brief Sets URL port.
	 *
	 * @param port URL port.
	 */
	void SetPort(uint16_t port) { port_ = port; }

	/**
	 * @brief Sets URL path.
	 *
	 * @param encodedPath URL path, already encoded.
	 */
	void SetPath(const std::string& encodedPath) { encoded_path_ = encodedPath; }

	/**
	 * @brief Sets the query parameters from an existing query parameter map.
	 *
	 * @note Keys and values in \p queryParameters are expected to be URL-encoded.
	 *
	 * @param queryParameters query parameters for request.
	 */
	void SetQueryParameters(std::map<std::string, std::string> queryParameters) {
		// creates a copy and discard previous
		encoded_query_parameters_ = std::move(queryParameters);
	}

	/**
	 * @brief Appends an element of URL path.
	 *
	 * @param encodedPath URL path element to append, already encoded.
	 */
	void AppendPath(const std::string& encodedPath) {
		if (!encoded_path_.empty() && encoded_path_.back() != '/') {
			encoded_path_ += '/';
		}
		encoded_path_ += encodedPath;
	}

	/**
	 * @brief The value of a query parameter is expected to be non-URL-encoded and, by default, it
	 * will be encoded before adding to the URL. Use \p isValueEncoded = true when the
	 * value is already encoded.
	 *
	 * @note Overrides the value of existing query parameters.
	 *
	 * @param encodedKey Name of the query parameter, already encoded.
	 * @param encodedValue Value of the query parameter, already encoded.
	 */
	void AppendQueryParameter(const std::string& encodedKey, const std::string& encodedValue) {
		encoded_query_parameters_[encodedKey] = encodedValue;
	}

	/**
	 * @brief Removes an existing query parameter.
	 *
	 * @param encodedKey The name of the query parameter to be removed.
	 */
	void RemoveQueryParameter(const std::string& encodedKey) {
		encoded_query_parameters_.erase(encodedKey);
	}

	/**
	 * @brief Gets URL host.
	 *
	 */
	const std::string& GetHost() const { return host_; }

	/**
	 * @brief Gets the URL path.
	 *
	 * @return const std::string&
	 */
	const std::string& GetPath() const { return encoded_path_; }

	/**
	 * @brief Gets the port number set for the URL.
	 *
	 * @note If the port was not set for the URL, the returned port is 0. An HTTP request cannot
	 * be performed to port zero, an HTTP client is expected to set the default port depending on
	 * the request's schema when the port was not defined in the URL.
	 *
	 * @return The port number from the URL.
	 */
	uint16_t GetPort() const { return port_; }

	/**
	 * @brief Gets a copy of the list of query parameters from the URL.
	 *
	 * @note The query parameters are URL-encoded.
	 *
	 * @return A copy of the query parameters map.
	 */
	std::map<std::string, std::string> GetQueryParameters() const {
		return encoded_query_parameters_;
	}

	/**
	 * @brief Gets the URL scheme.
	 *
	 */
	const std::string& GetScheme() const { return scheme_; }

	/**
	 * @brief Gets the path and query parameters.
	 *
	 * @return Relative URL with URL-encoded query parameters.
	 */
	std::string GetRelativeUrl() const;

	/**
	 * @brief Gets Scheme, host, path and query parameters.
	 *
	 * @return Absolute URL with URL-encoded query parameters.
	 */
	std::string GetAbsoluteUrl() const;

private:
	std::string GetUrlWithoutQuery(bool relative) const;

	/**
	 * @brief Finds the first '?' symbol and parses everything after it as query parameters.
	 * separated by '&'.
	 *
	 * @param encoded_query_parameters_ `std::string` containing one or more query parameters.
	 */
	void AppendQueryParameters(const std::string& encoded_query_parameters_);

private:
	std::string scheme_;
	std::string host_;
	uint16_t port_{0};
	std::string encoded_path_;
	// query parameters are all encoded
	std::map<std::string, std::string> encoded_query_parameters_;
};
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_URI_H_