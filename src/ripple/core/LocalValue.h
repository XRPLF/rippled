//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_LOCALVALUE_H_INCLUDED
#define RIPPLE_BASICS_LOCALVALUE_H_INCLUDED

#include <boost/optional.hpp>
#include <boost/thread/tss.hpp>
#include <memory>
#include <unordered_map>

namespace ripple {

namespace detail {

struct LocalValues
{
    bool onCoro = true;

    struct BasicValue
    {
        virtual ~BasicValue() = default;
        virtual void* get() = 0;
    };

    template <class T>
    struct Value : BasicValue
    {
        boost::optional<T> t;

        void* get() override
        {
            return &t;
        }
    };

    std::unordered_map<void const*, std::unique_ptr<BasicValue>> values;

    static
    inline
    void
    cleanup(LocalValues* lvs)
    {
        if (lvs && ! lvs->onCoro)
            delete lvs;
    }
};

template<class = void>
boost::thread_specific_ptr<detail::LocalValues>&
getLocalValues()
{
    static boost::thread_specific_ptr<
        detail::LocalValues> tsp(&detail::LocalValues::cleanup);
    return tsp;
}

} // detail

template <class T>
class LocalValue
{
public:
    /** Stores instance of T specific to the calling coroutine or thread. */
    boost::optional<T>&
    get()
    {
        auto lvs = detail::getLocalValues().get();
        if (! lvs)
        {
            lvs = new detail::LocalValues();
            lvs->onCoro = false;
            detail::getLocalValues().reset(lvs);
        }
        else
        {
            auto const iter = lvs->values.find(this);
            if (iter != lvs->values.end())
                return *reinterpret_cast<boost::optional<T>*>(
                    iter->second->get());
        }

        auto const result = lvs->values.emplace(this,
            std::make_unique<detail::LocalValues::Value<T>>());
        return *reinterpret_cast<boost::optional<T>*>(
            result.first->second->get());
    }
};

} // ripple

#endif
