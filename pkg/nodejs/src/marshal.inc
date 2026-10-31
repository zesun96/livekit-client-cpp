template <typename T> T ReadNumber(napi_env env, napi_value input) {
	if constexpr (std::is_enum_v<T>)
		return static_cast<T>(ReadNumber<std::underlying_type_t<T>>(env, input));
	else {
		napi_valuetype type;
		Napi(napi_typeof(env, input, &type));
		if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
			if (type == napi_bigint) {
				T out;
				bool lossless;
				if constexpr (std::is_signed_v<T>)
					Napi(napi_get_value_bigint_int64(env, input, &out, &lossless));
				else
					Napi(napi_get_value_bigint_uint64(env, input, &out, &lossless));
				if (!lossless)
					throw Error("64-bit integer overflow");
				return out;
			}
		}
		double number;
		Napi(napi_get_value_double(env, input, &number));
		if (!std::isfinite(number))
			throw Error("Expected a finite number");
		if constexpr (std::is_integral_v<T>) {
			if (std::floor(number) != number || number < double(std::numeric_limits<T>::lowest()) ||
			    number > double(std::numeric_limits<T>::max()) ||
			    std::abs(number) > 9007199254740991.0)
				throw Error("Integer outside safe range; use bigint for 64-bit values");
		} else if (std::abs(number) > double(std::numeric_limits<T>::max()))
			throw Error("Floating-point overflow");
		return static_cast<T>(number);
	}
}
bool ReadBoolean(napi_env env, napi_value input) {
	bool value;
	Napi(napi_get_value_bool(env, input, &value));
	return value;
}
void RequireObject(napi_env env, napi_value input) {
	napi_valuetype type;
	Napi(napi_typeof(env, input, &type));
	bool array = false;
	Napi(napi_is_array(env, input, &array));
	napi_value null_value;
	Napi(napi_get_null(env, &null_value));
	bool null = false;
	Napi(napi_strict_equals(env, input, null_value, &null));
	if (type != napi_object || null || array)
		throw Error("Expected an options object");
}
struct Arena;
#include "struct_read_decls.inc"
struct Arena {
	std::deque<std::string> strings;
	std::vector<std::shared_ptr<void>> storage;
	const char* KeepString(napi_env env, napi_value input) {
		strings.push_back(String(env, input));
		return strings.back().c_str();
	}
	const char* CountedString(napi_env env, napi_value input, size_t& count) {
		strings.push_back(String(env, input, true));
		count = strings.back().size();
		return strings.back().c_str();
	}
	template <typename T> T* Keep(std::vector<T> values) {
		auto owned = std::make_shared<std::vector<T>>(std::move(values));
		auto* data = owned->data();
		storage.push_back(std::move(owned));
		return data;
	}
	template <typename T> T* Buffer(napi_env env, napi_value input, size_t& count) {
		napi_typedarray_type type;
		size_t length, offset;
		void* data;
		napi_value buffer;
		Napi(napi_get_typedarray_info(env, input, &type, &length, &data, &buffer, &offset));
		if (type != napi_uint8_array || length % sizeof(T))
			throw Error("Expected a complete byte buffer");
		count = length / sizeof(T);
		std::vector<T> values(count);
		if (length)
			std::memcpy(values.data(), data, length);
		return Keep(std::move(values));
	}
	const char* const* StringArray(napi_env env, napi_value input, size_t& count) {
		uint32_t length;
		Napi(napi_get_array_length(env, input, &length));
		count = length;
		std::vector<const char*> values;
		for (uint32_t i = 0; i < length; ++i) {
			napi_value item;
			Napi(napi_get_element(env, input, i, &item));
			values.push_back(KeepString(env, item));
		}
		return Keep(std::move(values));
	}
	template <typename T> T* ArrayOf(napi_env env, napi_value input, size_t& count) {
		uint32_t length;
		Napi(napi_get_array_length(env, input, &length));
		count = length;
		std::vector<T> values(length);
		for (uint32_t i = 0; i < length; ++i) {
			napi_value item;
			Napi(napi_get_element(env, input, i, &item));
			if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
				values[i] = ReadNumber<T>(env, item);
			else
				Read(env, item, values[i], *this);
		}
		return Keep(std::move(values));
	}
	template <typename T> T* One(napi_env env, napi_value input) {
		std::vector<T> values(1);
		Read(env, input, values.front(), *this);
		return Keep(std::move(values));
	}
};
#include "struct_read.inc"

Value FromJs(napi_env env, napi_value input, size_t depth) {
	if (depth > 32)
		throw Error("Callback result is nested too deeply");
	napi_valuetype type;
	Napi(napi_typeof(env, input, &type));
	if (type == napi_undefined || type == napi_null)
		return {};
	if (type == napi_string)
		return String(env, input);
	if (type == napi_boolean)
		return ReadBoolean(env, input);
	if (type == napi_number)
		return ReadNumber<double>(env, input);
	if (type == napi_bigint) {
		int sign;
		size_t words = 0;
		Napi(napi_get_value_bigint_words(env, input, &sign, &words, nullptr));
		if (words > 1)
			throw Error("Callback bigint exceeds 64 bits");
		return sign ? Value(ReadNumber<int64_t>(env, input))
		            : Value(ReadNumber<uint64_t>(env, input));
	}
	bool array = false;
	Napi(napi_is_array(env, input, &array));
	if (array) {
		uint32_t length;
		Napi(napi_get_array_length(env, input, &length));
		Array values;
		for (uint32_t i = 0; i < length; ++i) {
			napi_value item;
			Napi(napi_get_element(env, input, i, &item));
			values.push_back(FromJs(env, item, depth + 1));
		}
		return values;
	}
	RequireObject(env, input);
	napi_value keys;
	Napi(napi_get_property_names(env, input, &keys));
	uint32_t length;
	Napi(napi_get_array_length(env, keys, &length));
	Object object;
	for (uint32_t i = 0; i < length; ++i) {
		napi_value key, item;
		Napi(napi_get_element(env, keys, i, &key));
		Napi(napi_get_property(env, input, key, &item));
		object.emplace(String(env, key), FromJs(env, item, depth + 1));
	}
	return object;
}
