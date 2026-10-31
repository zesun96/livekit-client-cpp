template <typename T, auto Destroy> struct Owned {
	T* value = nullptr;
	Owned() = default;
	explicit Owned(T* v) : value(v) {}
	Owned(const Owned&) = delete;
	Owned& operator=(const Owned&) = delete;
	Owned(Owned&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
	~Owned() {
		if (value)
			Destroy(value);
	}
};
template <typename Map> auto* Resource(Map& map, uint32_t id) {
	const auto it = map.find(id);
	if (it == map.end())
		throw Error("Resource is closed or belongs to another room", 1, 2);
	return it->second.value;
}
struct Room;
struct RpcContext {
	Room* room;
	std::string method;
};
struct WriterContext {
	Room* room;
	uint32_t id;
};
void RemoveDiagnostics(Room* room);
