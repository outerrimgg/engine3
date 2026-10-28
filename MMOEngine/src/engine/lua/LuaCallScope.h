/*
** Copyright (C) 2007-2026 SWGEmu
** See file COPYING for copying conditions.
*/

#pragma once

#include <lua.hpp>
#include <atomic>
#include <cstdio>

namespace engine {
namespace lua {

// Outer Rim: each Lua VM's own heap accounting (lua_gc LUA_GCCOUNT, O(1)) as of the end of its last outermost call,
// so total and per-VM Lua memory can be read from another thread without touching any VM. Fixed size; a VM gets a
// slot when its scope context is created, and VMs past MAX_SLOTS are only counted. Closed VMs keep their last value.
class LuaHeapSamples {
public:
	static const int MAX_SLOTS = 256;

	struct Slot {
		std::atomic<unsigned long long> bytes;
		std::atomic<unsigned long long> peak;
		std::atomic<unsigned long long> calls;
	};

	static Slot* slots() {
		static Slot table[MAX_SLOTS] = {};
		return table;
	}

	static std::atomic<int>& assigned() {
		static std::atomic<int> count{0};
		return count;
	}

	static int assign() {
		return assigned().fetch_add(1);
	}

	static void record(int slot, lua_State* L) {
		if (slot < 0 || slot >= MAX_SLOTS)
			return;

		const unsigned long long bytes = ((unsigned long long) lua_gc(L, LUA_GCCOUNT, 0) << 10) + (unsigned long long) lua_gc(L, LUA_GCCOUNTB, 0);
		Slot& entry = slots()[slot];
		entry.bytes.store(bytes, std::memory_order_relaxed);
		entry.calls.fetch_add(1, std::memory_order_relaxed);

		unsigned long long peak = entry.peak.load(std::memory_order_relaxed);

		while (bytes > peak && !entry.peak.compare_exchange_weak(peak, bytes, std::memory_order_relaxed)) {
		}
	}
};

// Lifetime of borrowed, cached binding objects is one outermost protected Lua
// invocation. The registry is shared by coroutines, unlike lua_State itself.
// Cleanup must not execute script or create new borrowed binding references.
class LuaCallScope {
	struct Context {
		unsigned int depth;
		unsigned long long failures;
		lua_CFunction cleanup;
		bool cleaning;
		bool reentered;
		int heapSlot;
	};

	lua_State* state;
	Context* context;

	static const void* registryKey() {
		static const char key = 0;
		return &key;
	}

	static Context* getContext(lua_State* L) {
		lua_rawgetp(L, LUA_REGISTRYINDEX, registryKey());
		auto context = static_cast<Context*>(lua_touserdata(L, -1));

		if (context == nullptr) {
			lua_pop(L, 1);
			context = static_cast<Context*>(lua_newuserdata(L, sizeof(Context)));
			*context = {0, 0, nullptr, false, false, LuaHeapSamples::assign()};
			lua_pushvalue(L, -1);
			lua_rawsetp(L, LUA_REGISTRYINDEX, registryKey());
		}

		lua_pop(L, 1);
		return context;
	}

public:
	explicit LuaCallScope(lua_State* L) : state(L), context(getContext(L)) {
		++context->depth;

		if (context->cleaning)
			context->reentered = true;
	}

	LuaCallScope(const LuaCallScope&) = delete;
	LuaCallScope& operator=(const LuaCallScope&) = delete;

	static void setCleanup(lua_State* L, lua_CFunction cleanup) {
		getContext(L)->cleanup = cleanup;
	}

	static unsigned long long getCleanupFailures(lua_State* L) {
		return getContext(L)->failures;
	}

	~LuaCallScope() noexcept {
		if (context->depth == 1 && !context->cleaning)
			LuaHeapSamples::record(context->heapSlot, state);

		if (--context->depth != 0 || context->cleaning || context->cleanup == nullptr)
			return;

		const int top = lua_gettop(state);
		context->cleaning = true;
		context->reentered = false;
		bool failed = false;

		try {
			lua_pushcfunction(state, context->cleanup);

			if (lua_pcall(state, 0, 0, 0) != LUA_OK) {
				const char* error = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : "non-string Lua error";
				std::fprintf(stderr, "ERROR LuaCallScope: pooled-object cleanup failed: %s\n", error);
				failed = true;
			}
		} catch (...) {
			std::fputs("ERROR LuaCallScope: C++ exception during pooled-object cleanup\n", stderr);
			failed = true;
		}

		// The callback is deliberately non-scriptable. Reentry would violate that
		// contract and could leave newly bound references behind; expose it loudly.
		if (context->reentered) {
			std::fputs("ERROR LuaCallScope: unexpected Lua reentry during pooled-object cleanup\n", stderr);
			failed = true;
		}

		if (failed)
			++context->failures;

		lua_settop(state, top);
		context->cleaning = false;
	}
};

}
}
