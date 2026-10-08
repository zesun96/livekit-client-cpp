extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdio>

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "usage: livekit_lua_test_runner script.lua\n");
		return 2;
	}
	lua_State* state = luaL_newstate();
	if (state == nullptr)
		return 2;
	luaL_openlibs(state);
	const int status = luaL_dofile(state, argv[1]);
	if (status != 0)
		std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
	lua_close(state);
	return status == 0 ? 0 : 1;
}
