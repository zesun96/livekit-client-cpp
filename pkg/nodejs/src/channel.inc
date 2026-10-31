// A response channel belongs to the application environment, not the Room Worker. A synchronous
// C ABI callback can therefore wait while async JavaScript runs on the application thread.
struct PendingCallback {
	std::mutex mutex;
	std::condition_variable changed;
	std::string id;
	std::string kind;
	Value payload;
	Value reply;
	bool done = false;
};
struct Channel {
	napi_env env = nullptr;
	napi_threadsafe_function function = nullptr;
	std::string id;
	std::mutex mutex;
	std::map<std::string, std::shared_ptr<PendingCallback>> pending;
	bool cancelled = false;
	size_t capacity = 128;
	uint32_t timeout_ms = 10000;
	size_t byte_limit = 1024 * 1024;
	uint64_t sequence = 0;
	void Cancel() noexcept {
		std::lock_guard lock(mutex);
		cancelled = true;
		for (const auto& [_, request] : pending) {
			std::lock_guard guard(request->mutex);
			request->done = true;
			request->reply = Object{{"ok", false}, {"message", "Callback cancelled"}};
			request->changed.notify_all();
		}
	}
	Value Ask(std::string kind, Value payload, uint32_t timeout = 0) {
		if (payload.Size() > byte_limit)
			throw Error("Callback payload exceeds byte limit");
		auto request = std::make_shared<PendingCallback>();
		request->kind = std::move(kind);
		request->payload = std::move(payload);
		napi_threadsafe_function fn;
		{
			std::lock_guard lock(mutex);
			if (cancelled || !function)
				throw Error("Callback channel closed", 1, 2);
			if (pending.size() >= capacity)
				throw Error("Callback queue limit exceeded");
			request->id = std::to_string(++sequence);
			fn = function;
			Napi(napi_acquire_threadsafe_function(fn));
			pending.emplace(request->id, request);
		}
		auto data = std::make_unique<std::shared_ptr<PendingCallback>>(request);
		const auto scheduled = napi_call_threadsafe_function(fn, data.get(), napi_tsfn_nonblocking);
		if (scheduled == napi_ok)
			data.release();
		napi_release_threadsafe_function(fn, napi_tsfn_release);
		Value reply;
		{
			std::unique_lock lock(request->mutex);
			if (scheduled != napi_ok ||
			    !request->changed.wait_for(
			        lock,
			        std::chrono::milliseconds(timeout ? std::min(timeout, timeout_ms) : timeout_ms),
			        [&] { return request->done; })) {
				request->done = true;
				request->reply =
				    Object{{"ok", false}, {"message", "Callback timed out or could not be queued"}};
			}
			reply = std::move(request->reply);
		}
		{
			std::lock_guard lock(mutex);
			pending.erase(request->id);
		}
		return reply;
	}
};
std::mutex channels_mutex;
std::map<std::string, std::shared_ptr<Channel>> channels;
std::atomic<uint64_t> channel_sequence{0};
std::shared_ptr<Channel> FindChannel(const std::string& id) {
	std::lock_guard lock(channels_mutex);
	const auto it = channels.find(id);
	if (it == channels.end())
		throw Error("Unknown callback channel");
	return it->second;
}
std::string VString(const Value& value, const char* key, const char* fallback = "") {
	const auto* object = std::get_if<Object>(&value.data);
	if (!object)
		return fallback;
	const auto it = object->find(key);
	if (it == object->end())
		return fallback;
	const auto* text = std::get_if<std::string>(&it->second.data);
	if (!text || text->find('\0') != std::string::npos)
		throw Error("Invalid callback string");
	return *text;
}
bool VBool(const Value& value, const char* key, bool fallback = false) {
	const auto* object = std::get_if<Object>(&value.data);
	if (!object)
		return fallback;
	const auto it = object->find(key);
	if (it == object->end())
		return fallback;
	const auto* boolean = std::get_if<bool>(&it->second.data);
	if (!boolean)
		throw Error("Invalid callback boolean");
	return *boolean;
}
uint32_t VNumber(const Value& value, const char* key, uint32_t fallback = 0) {
	const auto* object = std::get_if<Object>(&value.data);
	if (!object)
		return fallback;
	const auto it = object->find(key);
	if (it == object->end())
		return fallback;
	const auto* number = std::get_if<double>(&it->second.data);
	if (!number || !std::isfinite(*number) || *number < 0 || *number > UINT32_MAX ||
	    std::floor(*number) != *number)
		throw Error("Invalid callback integer");
	return static_cast<uint32_t>(*number);
}
void ChannelJs(napi_env env, napi_value callback, void*, void* data) noexcept {
	std::unique_ptr<std::shared_ptr<PendingCallback>> holder(
	    static_cast<std::shared_ptr<PendingCallback>*>(data));
	if (!env || !callback)
		return;
	auto& request = **holder;
	try {
		{
			std::lock_guard lock(request.mutex);
			if (request.done)
				return;
		}
		const auto value = Js(
		    env, Object{{"id", request.id}, {"kind", request.kind}, {"payload", request.payload}});
		napi_value receiver, result;
		Napi(napi_get_undefined(env, &receiver));
		Napi(napi_call_function(env, receiver, callback, 1, &value, &result));
	} catch (...) {
		std::lock_guard lock((*holder)->mutex);
		if ((*holder)->done)
			return;
		(*holder)->reply =
		    Object{{"ok", false}, {"message", "JavaScript callback dispatch failed"}};
		(*holder)->done = true;
		(*holder)->changed.notify_all();
	}
}
void ChannelCleanup(void* data) noexcept {
	auto* channel = static_cast<Channel*>(data);
	channel->Cancel();
	{
		std::lock_guard lock(channel->mutex);
		if (channel->function)
			napi_release_threadsafe_function(channel->function, napi_tsfn_abort);
		channel->function = nullptr;
	}
	std::lock_guard lock(channels_mutex);
	channels.erase(channel->id);
}
