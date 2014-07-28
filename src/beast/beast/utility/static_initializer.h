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

#include <beast/utility/noexcept.h>
#include <utility>

#ifdef _MSC_VER
#include <cassert>
#include <new>
#include <thread>
#include <type_traits>
#include <intrin.h>
#endif

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
#ifdef _MSC_VER
template <
    class T,
    class Tag = void
>
class static_initializer
{
private:
    struct data_t
    {
        //  0 = unconstructed
        //  1 = constructing
        //  2 = constructed
        long volatile state;

        typename std::aligned_storage <sizeof(T),
            std::alignment_of <T>::value>::type storage;
    };

    struct destroyer
    {
        T* t_;
        explicit destroyer (T* t) : t_(t) { }
        ~destroyer() { t_->~T(); }
    };

    static
    data_t&
    data() noexcept;

public:
    template <class... Args>
    explicit static_initializer (Args&&... args);

    T&
    get() noexcept;

    T const&
    get() const noexcept
    {
        return const_cast<static_initializer&>(*this).get();
    }

    T&
    operator*() noexcept
    {
        return get();
    }

    T const&
    operator*() const noexcept
    {
        return get();
    }

    T*
    operator->() noexcept
    {
        return &get();
    }

    T const*
    operator->() const noexcept
    {
        return &get();
    }
};

//------------------------------------------------------------------------------

template <class T, class Tag>
auto
static_initializer <T, Tag>::data() noexcept ->
    data_t&
{
    static data_t _; // zero-initialized
    return _;
}

template <class T, class Tag>
template <class... Args>
static_initializer <T, Tag>::static_initializer (Args&&... args)
{
    data_t& _(data());

    // Double Checked Locking Pattern

    if (_.state != 2)
    {
        T* const t (reinterpret_cast<T*>(&_.storage));

        for(;;)
        {
            long prev;
            prev = _InterlockedCompareExchange(&_.state, 1, 0);
            if (prev == 0)
            {
                try
                {
                    ::new(t) T (std::forward<Args>(args)...);                   
                    static destroyer on_exit (t);
                    _InterlockedIncrement(&_.state);
                }
                catch(...)
                {
                    // Constructors that throw exceptions are unsupported
                    std::terminate();
                }
            }
            else if (prev == 1)
            {
                std::this_thread::yield();
            }
            else
            {
                assert(prev == 2);
                break;
            }
        }
    }
}

template <class T, class Tag>
T&
static_initializer <T, Tag>::get() noexcept
{
    data_t& _(data());
    for(;;)
    {
        if (_.state == 2)
            break;
        std::this_thread::yield();
    }
    return *reinterpret_cast<T*>(&_.storage);
}

#else
template <
    class T,
    class Tag = void
>
class static_initializer
{
private:
    T* instance_;

public:
    template <class... Args>
    explicit
    static_initializer (Args&&... args);

    static_initializer ();
    
    T&
    get() noexcept
    {
        return *instance_;
    }

    T const&
    get() const noexcept
    {
        return const_cast<static_initializer&>(*this).get();
    }

    T&
    operator*() noexcept
    {
        return get();
    }

    T const&
    operator*() const noexcept
    {
        return get();
    }

    T*
    operator->() noexcept
    {
        return &get();
    }

    T const*
    operator->() const noexcept
    {
        return &get();
    }
};

template <class T, class Tag>
static_initializer <T, Tag>::static_initializer ()
{
    static T t;
    instance_ = &t;
}

template <class T, class Tag>
template <class... Args>
static_initializer <T, Tag>::static_initializer (Args&&... args)
{
    static T t (std::forward<Args>(args)...);
    instance_ = &t;
}

#endif

}

#endif
