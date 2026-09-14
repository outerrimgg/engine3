#ifndef LUNA_H
#define LUNA_H 1

/*****************************************************************************
 *     .:: Luna ::.                                                          *
 *                                                                *   , *    *
 *  C++ library for binding classes into Lua.     By nornagon.       ((   *  *
 *                                                               *    `      *
 *  Example:                                                                 *
 *****************************************************************************

    class Foo {
      public:
        Foo(lua_State *L) {
          printf("in constructor\n");
        }

        int foo(lua_State *L) {
          printf("in foo\n");
        }

        ~Foo() {
          printf("in destructor\n");
        }

        static const char className[];
        static const Luna<Foo>::RegType Register[];
    };

    const char Foo::className[] = "Foo";
    const Luna<Foo>::RegType Foo::Register[] = {
      { "foo", &Foo::foo },
      { 0 }
    };

 *****************************************************************************
 * Then:                                                                     *
 *****************************************************************************

    Luna<Foo>::Register(L);

 *****************************************************************************
 * From Lua:                                                                 *
 *****************************************************************************

    local foo = Foo()
    foo:foo()

 *****************************************************************************
 * Clean up:                                                                 *
 *****************************************************************************

    lua_close(L);

 *****************************************************************************
 * Output:                                                                   *
 *****************************************************************************

    in constructor
    in foo
    in destructor

 *****************************************************************************
 * This program is free software. It comes without any warranty, to          *
 * the extent permitted by applicable law. You can redistribute it           *
 * and/or modify it under the terms of the Do What The Fuck You Want         *
 * To Public License, Version 2, as published by Sam Hocevar. See            *
 * http://sam.zoy.org/wtfpl/COPYING for more details.                        *
 ****************************************************************************/


// convenience macros
#define luna_register(L, klass) (Luna<klass>::Register((L)))
#define luna_registermetatable(L, klass) (Luna<klass>::RegisterMetatable((L)))
#define luna_inject(L, klass, t) (Luna<klass>::inject((L), (t)))

#define luna_method(class, name) {#name, &class::name}

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstring>

#include "system/lang/Exception.h"

// Forward-decl of Lua's internal error type (defined in ldo.c). When Lua is
// compiled as C++ (see MMOCoreORB/external/lua), a Lua error is raised as
// `throw((lua_longjmp*)c)`; the guards below rethrow it untouched so a genuine
// in-binding Lua error propagates to the enclosing pcall with its real error
// object, instead of being mislabeled/re-wrapped as a foreign C++ exception.
struct lua_longjmp;

namespace engine {
namespace lua {

template<class T> class Luna {
  public:
    static void Register(lua_State *L) {
      lua_pushcfunction(L, &Luna<T>::constructor);
      lua_setglobal(L, T::className); // T() in lua will make a new instance.

      RegisterMetatable(L);
    }

    // register the metatable without registering the class constructor
    static void RegisterMetatable(lua_State *L) {
      luaL_newmetatable(L, T::className); // create a metatable in the registry
      lua_pushstring(L, "__gc");
      lua_pushcfunction(L, &Luna<T>::gc_obj);
      lua_settable(L, -3); // metatable["__gc"] = Luna<T>::gc_obj
      lua_pop(L, 1);
    }

    static int constructor(lua_State *L) {
      // A C++ exception must never unwind into the (C-compiled) Lua VM, whose
      // protected-call machinery is setjmp/longjmp and cannot intercept it: the
      // unwind bypasses Lua's error recovery and leaves this thread's lua_State
      // corrupt (dangling open upvalues), crashing later in luaC_upvalbarrier.
      // Catch here (all C++ destructors run during unwind) and convert to a
      // normal Lua error via lua_error() AFTER the catch scope has unwound.
      char errbuf[256];
      try {
        return inject(L, new T(L));
      } catch (lua_longjmp*) {
        throw; // genuine Lua error under the C++-compiled VM: propagate untouched
      } catch (const Exception& e) {
        const char* w = e.what();
        std::strncpy(errbuf, w ? w : "C++ exception (no message)", sizeof(errbuf) - 1);
        errbuf[sizeof(errbuf) - 1] = '\0';
      } catch (...) {
        std::strncpy(errbuf, "unhandled C++ exception in Lua constructor", sizeof(errbuf) - 1);
        errbuf[sizeof(errbuf) - 1] = '\0';
      }
      // Push + raise OUTSIDE the catch: lua_pushstring can allocate and longjmp,
      // which must not fire while a C++ exception frame is still unwinding.
      lua_pushstring(L, errbuf);
      return lua_error(L);
    }

    static int inject(lua_State *L, T* obj) {
      lua_newtable(L); // create a new table for the class object ('self')

      lua_pushnumber(L, 0);

      T** a = static_cast<T**>(lua_newuserdata(L, sizeof(T*))); // store a ptr to the ptr
      *a = obj; // set the ptr to the ptr to point to the ptr... >.>
      luaL_newmetatable(L, T::className); // get (or create) the unique metatable
      lua_setmetatable(L, -2); // self.metatable = uniqe_metatable

      lua_settable(L, -3); // self[0] = obj;

      for (int i = 0; T::Register[i].name; i++) { // register the functions
        lua_pushstring(L, T::Register[i].name);
        lua_pushnumber(L, i); // let the thunk know which method we mean
        lua_pushcclosure(L, &Luna<T>::thunk, 1);
        lua_settable(L, -3); // self["function"] = thunk("function")
      }
      return 1;
    }

    static int thunk(lua_State *L) {
      // redirect method call to the real thing
      int i = (int)lua_tonumber(L, lua_upvalueindex(1)); // which function?
      lua_pushnumber(L, 0);
      lua_gettable(L, 1); // get the class table (i.e, self)

      T** obj = static_cast<T**>(luaL_checkudata(L, -1, T::className));
      lua_remove(L, -1); // remove the userdata from the stack

      // Guard the C++/Lua boundary: a C++ exception thrown by the bound method
      // must not unwind into the C-compiled Lua VM (it would corrupt this
      // thread's lua_State and crash later in luaC_upvalbarrier). Convert it to
      // a normal Lua error; lua_error() longjmps AFTER the catch has unwound.
      char errbuf[256];
      try {
        return ((*obj)->*(T::Register[i].mfunc))(L); // execute the thunk
      } catch (lua_longjmp*) {
        throw; // genuine Lua error under the C++-compiled VM: propagate untouched
      } catch (const Exception& e) {
        const char* w = e.what();
        std::strncpy(errbuf, w ? w : "C++ exception (no message)", sizeof(errbuf) - 1);
        errbuf[sizeof(errbuf) - 1] = '\0';
      } catch (...) {
        std::strncpy(errbuf, "unhandled C++ exception in Lua method binding", sizeof(errbuf) - 1);
        errbuf[sizeof(errbuf) - 1] = '\0';
      }
      // Push + raise OUTSIDE the catch: lua_pushstring can allocate and longjmp,
      // which must not fire while a C++ exception frame is still unwinding.
      lua_pushstring(L, errbuf);
      return lua_error(L);
    }

    static int gc_obj(lua_State *L) {
      // clean up
      //printf("GC called: %s\n", T::className);
      T** obj = static_cast<T**>(luaL_checkudata(L, -1, T::className));
      // Runs as a Lua __gc finalizer inside a GC step; a C++ exception escaping
      // here would unwind into the VM mid-collection. Destructors must not throw
      // — swallow defensively rather than corrupt the collector.
      try {
        delete (*obj);
      } catch (...) {
      }
      return 0;
    }

    struct RegType {
      const char *name;
      int(T::*mfunc)(lua_State*);
    };
};


}
}

using namespace engine::lua;

#endif /* LUNA_H */
