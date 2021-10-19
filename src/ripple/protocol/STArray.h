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

#ifndef RIPPLE_PROTOCOL_STARRAY_H_INCLUDED
#define RIPPLE_PROTOCOL_STARRAY_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/STObject.h>

namespace ripple {

class STArray final : public STBase, public CountedObject<STArray>
{
private:
    using list_type = std::vector<STObject>;

    list_type v_;

public:
    using size_type = list_type::size_type;
    using iterator = list_type::iterator;
    using const_iterator = list_type::const_iterator;

    STArray() = default;
    STArray(STArray const&) = default;
    STArray&
    operator=(STArray const&) = default;
    STArray(STArray&&);
    STArray&
    operator=(STArray&&);

    STArray(SField const& f, int n);
    STArray(SerialIter& sit, SField const& f, int depth = 0);
    explicit STArray(int n);
    explicit STArray(SField const& f);

    STObject&
    operator[](std::size_t j);

    STObject const&
    operator[](std::size_t j) const;

    STObject&
    back();

    STObject const&
    back() const;

    template <class... Args>
    void
    emplace_back(Args&&... args);

    void
    push_back(STObject const& object);

    void
    push_back(STObject&& object);

    iterator
    begin();

    iterator
    end();

    const_iterator
    begin() const;

    const_iterator
    end() const;

    size_type
    size() const;

    bool
    empty() const;

    void
    clear();

    void
    reserve(std::size_t n);

    void
    swap(STArray& a) noexcept;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value
    getJson(JsonOptions index) const override;

    void
    add(Serializer& s) const override;

    void
    sort(bool (*compare)(const STObject& o1, const STObject& o2));

    bool
    operator==(const STArray& s) const;

    bool
    operator!=(const STArray& s) const;

    SerializedTypeID
    getSType() const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STObject&
STArray::operator[](std::size_t j)
{
    return v_[j];
}

inline STObject const&
STArray::operator[](std::size_t j) const
{
    return v_[j];
}

inline STObject&
STArray::back()
{
    return v_.back();
}

inline STObject const&
STArray::back() const
{
    return v_.back();
}

template <class... Args>
inline void
STArray::emplace_back(Args&&... args)
{
    v_.emplace_back(std::forward<Args>(args)...);
}

inline void
STArray::push_back(STObject const& object)
{
    v_.push_back(object);
}

inline void
STArray::push_back(STObject&& object)
{
    v_.push_back(std::move(object));
}

inline STArray::iterator
STArray::begin()
{
    return v_.begin();
}

inline STArray::iterator
STArray::end()
{
    return v_.end();
}

inline STArray::const_iterator
STArray::begin() const
{
    return v_.begin();
}

inline STArray::const_iterator
STArray::end() const
{
    return v_.end();
}

inline STArray::size_type
STArray::size() const
{
    return v_.size();
}

inline bool
STArray::empty() const
{
    return v_.empty();
}

inline void
STArray::clear()
{
    v_.clear();
}

inline void
STArray::reserve(std::size_t n)
{
    v_.reserve(n);
}

inline void
STArray::swap(STArray& a) noexcept
{
    v_.swap(a.v_);
}

inline bool
STArray::operator==(const STArray& s) const
{
    return v_ == s.v_;
}

inline bool
STArray::operator!=(const STArray& s) const
{
    return v_ != s.v_;
}

}  // namespace ripple

#endif
