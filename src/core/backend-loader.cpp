//
// Copyright (C) 2008 Maciej Sobczak with contributions from Artyom Tonkikh
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/soci-platform.h"
#include "soci/backend-loader.h"
#include "soci/error.h"
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#ifndef _MSC_VER
#include <stdint.h>
#endif

#include "soci_backends_config.h"

using namespace soci;
using namespace soci::dynamic_backends;

#ifdef _WIN32

#include <windows.h>

typedef CRITICAL_SECTION soci_mutex_t;
typedef HMODULE soci_handler_t;

#define LOCK(x) EnterCriticalSection(x)
#define UNLOCK(x) LeaveCriticalSection(x)
#define MUTEX_INIT(x) InitializeCriticalSection(x)
#define MUTEX_DEST(x) DeleteCriticalSection(x)
#ifdef _UNICODE
#define DLOPEN(x) LoadLibraryA(x)
#else
#define DLOPEN(x) LoadLibrary(x)
#endif
#define DLCLOSE(x) FreeLibrary(x)
#define DLSYM(x, y) GetProcAddress(x, y)

#ifdef SOCI_ABI_VERSION
  #ifndef NDEBUG
    #define LIBNAME(x) (SOCI_LIB_PREFIX + x + "_" SOCI_ABI_VERSION SOCI_DEBUG_POSTFIX SOCI_LIB_SUFFIX)
  #else
    #define LIBNAME(x) (SOCI_LIB_PREFIX + x + "_" SOCI_ABI_VERSION SOCI_LIB_SUFFIX)
  #endif
#else
#define LIBNAME(x) (SOCI_LIB_PREFIX + x + SOCI_LIB_SUFFIX)
#endif // SOCI_ABI_VERSION

#else

#include <pthread.h>
#include <dlfcn.h>

typedef pthread_mutex_t soci_mutex_t;
typedef void * soci_handler_t;

#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)
#define MUTEX_INIT(x) pthread_mutex_init(x, NULL)
#define MUTEX_DEST(x) pthread_mutex_destroy(x)
#define DLOPEN(x) dlopen(x, RTLD_LAZY)
#define DLCLOSE(x) dlclose(x)
#define DLSYM(x, y) dlsym(x, y)

#ifdef SOCI_ABI_VERSION

#ifdef __APPLE__
#define LIBNAME(x) (SOCI_LIB_PREFIX + x + "." SOCI_ABI_VERSION SOCI_LIB_SUFFIX)
#else
#define LIBNAME(x) (SOCI_LIB_PREFIX + x + SOCI_LIB_SUFFIX "." SOCI_ABI_VERSION)
#endif

#else
#define LIBNAME(x) (SOCI_LIB_PREFIX + x + SOCI_LIB_SUFFIX)
#endif // SOCI_ABI_VERSION

#endif // _WIN32


namespace // unnamed
{

struct info
{
    soci_handler_t handler_;
    backend_factory const * factory_;
    info() : handler_(0), factory_(0) {}
};

typedef std::map<std::string, info> factory_map;
factory_map factories_;

std::vector<std::string> search_paths_;

soci_mutex_t mutex_;

std::vector<std::string> get_default_paths()
{
    std::vector<std::string> paths;

    // TODO: may be problem with finding getenv in std namespace in Visual C++ --mloskot
    char const* const penv = std::getenv("SOCI_BACKENDS_PATH");
    if (0 == penv)
    {
        paths.push_back(".");
        paths.push_back(DEFAULT_BACKENDS_PATH);
        return paths;
    }

    std::string const env = penv;
    if (env.empty())
    {
        paths.push_back(".");
        paths.push_back(DEFAULT_BACKENDS_PATH);
        return paths;
    }

    std::string::size_type searchFrom = 0;
    while (searchFrom != env.size())
    {
        std::string::size_type const found = env.find(":", searchFrom);
        if (found == searchFrom)
        {
            ++searchFrom;
        }
        else if (std::string::npos != found)
        {
            std::string const path(env.substr(searchFrom, found - searchFrom));
            paths.push_back(path);

            searchFrom = found + 1;
        }
        else // found == npos
        {
            std::string const path = env.substr(searchFrom);
            paths.push_back(path);

            searchFrom = env.size();
        }
    }

    return paths;
}

// used to automatically initialize the global state
struct static_state_mgr
{
    static_state_mgr()
    {
        MUTEX_INIT(&mutex_);

        search_paths_ = get_default_paths();
    }

    ~static_state_mgr()
    {
        unload_all();

        MUTEX_DEST(&mutex_);
    }
} static_state_mgr_;

class scoped_lock
{
public:
    scoped_lock(soci_mutex_t * m) : mptr(m) { LOCK(m); };
    ~scoped_lock() { UNLOCK(mptr); };
private:
    soci_mutex_t * mptr;
};

// non-synchronized helper for the other functions
void do_unload(std::string const & name)
{
    factory_map::iterator i = factories_.find(name);

    if (i != factories_.end())
    {
        soci_handler_t h = i->second.handler_;
        if (h != NULL)
        {
            DLCLOSE(h);
        }

        factories_.erase(i);
    }
}

// non-synchronized helper
void do_register_backend(std::string const & name, std::string const & shared_object)
{
    // The rules for backend search are as follows:
    // - if the shared_object is given,
    //   it names the library file and the search paths are not used
    // - otherwise (shared_object not provided or empty):
    //   - file named libsoci_NAME.so.SOVERSION is searched in the list of search paths

    soci_handler_t h = 0;
    if (shared_object.empty() == false)
    {
        h = DLOPEN(shared_object.c_str());
    }
    else
    {
        // try system paths
        h = DLOPEN(LIBNAME(name).c_str());
        if (0 == h)
        {
            // try all search paths
            for (std::size_t i = 0; i != search_paths_.size(); ++i)
            {
                std::string const fullFileName(search_paths_[i] + "/" + LIBNAME(name));
                h = DLOPEN(fullFileName.c_str());
                if (0 != h)
                {
                    // already found
                    break;
                }
             }
         }
    }

    if (0 == h)
    {
        throw soci_error("Failed to find shared library for backend " + name);
    }

    std::string symbol = "factory_" + name;

    typedef backend_factory const * bfc_ptr;
    typedef bfc_ptr (*get_t)(void);
    get_t entry;
    entry = reinterpret_cast<get_t>(
            reinterpret_cast<uintptr_t>(DLSYM(h, symbol.c_str())));

    if (0 == entry)
    {
        DLCLOSE(h);
        throw soci_error("Failed to resolve dynamic symbol: " + symbol);
    }

    // unload the existing handler if it's already loaded

    do_unload(name);

    backend_factory const* f = entry();

    info new_entry;
    new_entry.factory_ = f;
    new_entry.handler_ = h;

    factories_[name] = new_entry;
}

} // unnamed namespace

backend_factory const& dynamic_backends::get(std::string const& name)
{
    scoped_lock lock(&mutex_);

    factory_map::iterator i = factories_.find(name);

    if (i != factories_.end())
    {
        return *(i->second.factory_);
    }

    // no backend found with this name, try to register it first

    do_register_backend(name, std::string());

    // second attempt, must succeed (the backend is already loaded)

    i = factories_.find(name);

    return *(i->second.factory_);
}

SOCI_DECL std::vector<std::string>& search_paths()
{
    return search_paths_;
}

SOCI_DECL void dynamic_backends::register_backend(
    std::string const& name, std::string const& shared_object)
{
    scoped_lock lock(&mutex_);

    do_register_backend(name, shared_object);
}

SOCI_DECL void dynamic_backends::register_backend(
    std::string const& name, backend_factory const& factory)
{
    scoped_lock lock(&mutex_);

    // unload the existing handler if it's already loaded

    do_unload(name);

    info new_entry;
    new_entry.factory_ = &factory;

    factories_[name] = new_entry;
}

SOCI_DECL std::vector<std::string> dynamic_backends::list_all()
{
    scoped_lock lock(&mutex_);

    std::vector<std::string> ret;
    ret.reserve(factories_.size());

    for (factory_map::iterator i = factories_.begin(); i != factories_.end(); ++i)
    {
        std::string const& name = i->first;
        ret.push_back(name);
    }

    return ret;
}

SOCI_DECL void dynamic_backends::unload(std::string const& name)
{
    scoped_lock lock(&mutex_);

    do_unload(name);
}

SOCI_DECL void dynamic_backends::unload_all()
{
    scoped_lock lock(&mutex_);

    for (factory_map::iterator i = factories_.begin(); i != factories_.end(); ++i)
    {
        soci_handler_t h = i->second.handler_;
        if (0 != h)
        {
            DLCLOSE(h);
        }
    }

    factories_.clear();
}
