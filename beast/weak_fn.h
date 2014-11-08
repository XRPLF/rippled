//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_WEAK_FN_H_INCLUDED
#define BEAST_WEAK_FN_H_INCLUDED

#include <beast/utility/empty_base_optimization.h>
#include <memory>

// Original version:
// http://lists.boost.org/Archives/boost/att-189469/weak_fn.hpp
//
// This work was adapted from source code with this copyright notice:
//
//  weak_fun.hpp
//
//  Copyright (c) 2009 Artyom Beilis
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

namespace beast {

// Policy throws if weak pointer is expired
template <class V = void>
struct throw_if_invalid
{
    V operator()() const
    {
        throw std::bad_weak_ptr();
    }
};

// Policy returns a value if weak pointer is expired
template <class V>
struct return_default_if_invalid
{
    return_default_if_invalid()
        : def_value_()
        { }

    return_default_if_invalid(V def_value)
        : def_value_(def_value)
        { }

    V operator()() const
    {
        return def_value_;
    }

private:
    V def_value_;
};

// Policy does nothing if weak pointer is expired
template <class V>
struct ignore_if_invalid
{
    V operator()() const
    {
        return V();
    }
};

template <class V>
using default_invalid_policy = ignore_if_invalid<V>;

namespace detail {

template <class T, class R, class Policy, class... Args>
class weak_binder
    : private beast::empty_base_optimization<Policy>
{
private:
    typedef R (T::*member_type)(Args...);
    using pointer_type = std::weak_ptr<T>;
    using shared_type = std::shared_ptr<T>;
    member_type member_;
    pointer_type object_;

public:
    using result_type = R;

    weak_binder (member_type member,
            Policy policy, pointer_type object)
        : empty_base_optimization<Policy>(std::move(policy))
        , member_(member)
        , object_(object)
        { }

    R operator()(Args... args)
    {
        if(auto p = object_.lock())
            return ((*p).*member_)(args...);
        return this->member()();
    }    
};

} // detail

/** Returns a callback that can be used with std::bind and a weak_ptr.
    When called, it tries to lock weak_ptr to get a shared_ptr. If successful,
    it calls given member function with given arguments. If not successful,
    the policy functor is called. Built-in policies are:
    
    ignore_if_invalid           does nothing
    throw_if_invalid            throws `bad_weak_ptr`
    return_default_if_invalid   returns a chosen value
    
    Example:

    struct Foo {
        void bar(int i) {
            std::cout << i << std::endl;
        }
    };

    struct do_something {
        void operator()() {
            std::cout << "outdated reference" << std::endl;
        }
    };
    
    int main()
    {
        std::shared_ptr<Foo> sp(new Foo());
        std::weak_ptr<Foo> wp(sp);
    
        std::bind(weak_fn(&Foo::bar, wp), _1)(1);
        sp.reset();
        std::bind(weak_fn(&Foo::bar, wp), 1)();
        std::bind(weak_fn(&Foo::bar, wp, do_something()), 1)();
    }
*/
/** @{ */
template <class T, class R, class Policy, class... Args>
detail::weak_binder<T, R, Policy, Args...>
weak_fn (R (T::*member)(Args...), std::shared_ptr<T> p,
    Policy policy)
{
    return detail::weak_binder<T, R,
        Policy, Args...>(member, policy, p);
}

template <class T, class R, class... Args>
detail::weak_binder<T, R, default_invalid_policy<R>, Args...>
weak_fn (R (T::*member)(Args...), std::shared_ptr<T> p)
{
    return detail::weak_binder<T, R,
        default_invalid_policy<R>, Args...>(member,
            default_invalid_policy<R>{}, p);
}
/** @} */

} // beast

#endif
