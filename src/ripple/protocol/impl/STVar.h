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

#ifndef RIPPLE_PROTOCOL_STVAR_H_INCLUDED
#define RIPPLE_PROTOCOL_STVAR_H_INCLUDED

#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <typeinfo>

namespace ripple {
namespace detail {

struct defaultObject_t
{
    explicit defaultObject_t() = default;
};

struct nonPresentObject_t
{
    explicit nonPresentObject_t() = default;
};

extern defaultObject_t defaultObject;
extern nonPresentObject_t nonPresentObject;

// "variant" that can hold any type of serialized object
// and includes a small-object allocation optimization.
class STVar
{
private:
    // The largest "small object" we can accomodate
    static std::size_t constexpr max_size = 72;

    std::aligned_storage<max_size>::type d_;
    STBase* p_ = nullptr;

public:
    ~STVar();
    STVar (STVar const& other);
    STVar (STVar&& other);
    STVar& operator= (STVar const& rhs);
    STVar& operator= (STVar&& rhs);

    STVar (STBase&& t)
    {
        p_ = t.move(max_size, &d_);
    }

    STVar (STBase const& t)
    {
        p_ = t.copy(max_size, &d_);
    }

    STVar (defaultObject_t, SField const& name);
    STVar (nonPresentObject_t, SField const& name);
    STVar (SerialIter& sit, SField const& name, int depth = 0);

    STBase& get() { return *p_; }
    STBase& operator*() { return get(); }
    STBase* operator->() { return &get(); }
    STBase const& get() const { return *p_; }
    STBase const& operator*() const { return get(); }
    STBase const* operator->() const { return &get(); }

    template <class T, class... Args>
    friend
    STVar
    make_stvar(Args&&... args);

private:
    STVar() = default;

    STVar (SerializedTypeID id, SField const& name);

    void destroy();

    template <class T, class... Args>
    void
    construct(Args&&... args)
    {
        if(sizeof(T) > max_size)
            p_ = new T(std::forward<Args>(args)...);
        else
            p_ = new(&d_) T(std::forward<Args>(args)...);
    }

    bool
    on_heap() const
    {
        return static_cast<void const*>(p_) !=
            static_cast<void const*>(&d_);
    }
};

template <class T, class... Args>
inline
STVar
make_stvar(Args&&... args)
{
    STVar st;
    st.construct<T>(std::forward<Args>(args)...);
    return st;
}

inline
bool
operator== (STVar const& lhs, STVar const& rhs)
{
    return lhs.get().isEquivalent(rhs.get());
}

inline
bool
operator!= (STVar const& lhs, STVar const& rhs)
{
    return ! (lhs == rhs);
}

} // detail
} // ripple

#endif
