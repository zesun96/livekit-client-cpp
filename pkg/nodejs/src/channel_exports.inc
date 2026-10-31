std::vector<napi_value> Arguments(napi_env env, napi_callback_info info, size_t expected) {
	std::vector<napi_value> args(expected);
	size_t length = expected;
	Napi(napi_get_cb_info(env, info, &length, args.data(), nullptr, nullptr));
	if (length != expected)
		throw Error("Wrong number of native arguments");
	return args;
}
napi_value CreateChannel(napi_env env, napi_callback_info info) {
	return Guard(env, [&] {
		const auto args = Arguments(env, info, 2);
		auto channel = std::make_shared<Channel>();
		channel->env = env;
		channel->id = std::to_string(++channel_sequence);
		channel->capacity = Num(env, args[1], "callbackCapacity", 128);
		channel->timeout_ms = Num(env, args[1], "callbackTimeoutMs", 10000);
		channel->byte_limit = Num(env, args[1], "callbackByteLimit", 1024 * 1024);
		if (!channel->capacity || !channel->timeout_ms)
			throw Error("Callback limits must be positive");
		auto holder = std::make_unique<std::shared_ptr<Channel>>(channel);
		Napi(napi_create_threadsafe_function(
		    env, args[0], nullptr, Js(env, "LiveKit callback channel"), channel->capacity, 1,
		    holder.get(),
		    [](napi_env, void* data, void*) {
			    delete static_cast<std::shared_ptr<Channel>*>(data);
		    },
		    nullptr, ChannelJs, &channel->function));
		holder.release();
		Napi(napi_unref_threadsafe_function(env, channel->function));
		{
			std::lock_guard lock(channels_mutex);
			channels.emplace(channel->id, channel);
		}
		Napi(napi_add_env_cleanup_hook(env, ChannelCleanup, channel.get()));
		return Js(env, channel->id);
	});
}
napi_value CancelChannel(napi_env env, napi_callback_info info) {
	return Guard(env, [&] {
		const auto args = Arguments(env, info, 1);
		auto channel = FindChannel(String(env, args[0]));
		if (channel->env != env)
			throw Error("Callback channel belongs to a different environment");
		channel->Cancel();
		return Js(env, Value());
	});
}
napi_value ReleaseChannel(napi_env env, napi_callback_info info) {
	return Guard(env, [&] {
		const auto args = Arguments(env, info, 1);
		auto channel = FindChannel(String(env, args[0]));
		if (channel->env != env)
			throw Error("Callback channel belongs to a different environment");
		Napi(napi_remove_env_cleanup_hook(env, ChannelCleanup, channel.get()));
		ChannelCleanup(channel.get());
		return Js(env, Value());
	});
}
napi_value CompleteCallback(napi_env env, napi_callback_info info) {
	return Guard(env, [&] {
		const auto args = Arguments(env, info, 3);
		auto channel = FindChannel(String(env, args[0]));
		if (channel->env != env)
			throw Error("Callback channel belongs to a different environment");
		const auto id = String(env, args[1]);
		auto reply = FromJs(env, args[2]);
		if (reply.Size() > channel->byte_limit)
			reply = Object{{"ok", false}, {"message", "Callback response exceeds byte limit"}};
		std::lock_guard lock(channel->mutex);
		const auto it = channel->pending.find(id);
		if (it == channel->pending.end())
			return Js(env, false);
		std::lock_guard guard(it->second->mutex);
		if (it->second->done)
			return Js(env, false);
		it->second->reply = std::move(reply);
		it->second->done = true;
		it->second->changed.notify_all();
		return Js(env, true);
	});
}
