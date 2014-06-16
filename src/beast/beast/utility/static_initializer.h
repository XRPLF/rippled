//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_UTILITY_STATIC_INITIALIZER_H_INCLUDED
#define BEAST_UTILITY_STATIC_INITIALIZER_H_INCLUDED

#ifdef _MSC_VER

#include <beast/utility/noexcept.h>
#include <atomic>
#include <chrono>
#include <new>
#include <thread>
#include <type_traits>

namespace beast {

/** Returns an object with static storage duration.
    This is a workaround for Visual Studio 2013 and earlier non-thread
    safe initialization of function local objects with static storage duration.

    Usage:
    @code
    my_class& foo()
    {
        static static_initializer <my_class> instance;
        return *instance;
    }
    @endcode
*/
template <class T, class Tag = void>
class static_initializer
{
private:
    T* instance_;

public:
    template <class... Args>
    static_initializer(Args&&... args)
    {
        static std::aligned_storage <sizeof(T),
            std::alignment_of <T>::value>::type storage;
        instance_ = reinterpret_cast<T*>(&storage);

        // double checked lock
        static bool volatile initialized;
        if (! initialized)
        {
            static std::atomic_flag lock;
            while (lock.test_and_set())
                std::this_thread::sleep_for (
                    std::chrono::milliseconds(10));
            if (! initialized)
            {
                try
                {
                    ::new(instance_) T(std::forward<Args>(args)...);
                    
                    struct destroyer
                    {
                        T* t_;

                        destroyer (T* t)
                            : t_(t)
                        {
                        }

                        ~destroyer()
                        {
                            t_->~T();
                        }
                    };

                    static destroyer on_exit (instance_);
                }
                catch(...)
                {
                    lock.clear();
                    throw;
                }
                initialized = true;
            }
            lock.clear();
        }
    }

    T&
    get() noexcept
    {
        return *instance_;
    }

    T&
    operator*() noexcept
    {
        return *instance_;
    }
};

}

#endif

#endif
