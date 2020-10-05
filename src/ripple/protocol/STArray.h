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
    STArray(STArray&&);
    STArray(STArray const&) = default;
    STArray(SField const& f, int n);
    STArray(SerialIter& sit, SField const& f, int depth = 0);
    explicit STArray(int n);
    explicit STArray(SField const& f);
    STArray&
    operator=(STArray const&) = default;
    STArray&
    operator=(STArray&&);

    STBase*
    copy(std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move(std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    STObject&
    operator[](std::size_t j)
    {
        return v_[j];
    }

    STObject const&
    operator[](std::size_t j) const
    {
        return v_[j];
    }

    STObject&
    back()
    {
        return v_.back();
    }

    STObject const&
    back() const
    {
        return v_.back();
    }

    template <class... Args>
    void
    emplace_back(Args&&... args)
    {
        v_.emplace_back(std::forward<Args>(args)...);
    }

    void
    push_back(STObject const& object)
    {
        v_.push_back(object);
    }

    void
    push_back(STObject&& object)
    {
        v_.push_back(std::move(object));
    }

    iterator
    begin()
    {
        return v_.begin();
    }

    iterator
    end()
    {
        return v_.end();
    }

    const_iterator
    begin() const
    {
        return v_.begin();
    }

    const_iterator
    end() const
    {
        return v_.end();
    }

    size_type
    size() const
    {
        return v_.size();
    }

    bool
    empty() const
    {
        return v_.empty();
    }
    void
    clear()
    {
        v_.clear();
    }
    void
    reserve(std::size_t n)
    {
        v_.reserve(n);
    }
    void
    swap(STArray& a) noexcept
    {
        v_.swap(a.v_);
    }

    virtual std::string
    getFullText() const override;
    virtual std::string
    getText() const override;

    virtual Json::Value
    getJson(JsonOptions index) const override;
    virtual void
    add(Serializer& s) const override;

    void
    sort(bool (*compare)(const STObject& o1, const STObject& o2));

    bool
    operator==(const STArray& s) const
    {
        return v_ == s.v_;
    }
    bool
    operator!=(const STArray& s) const
    {
        return v_ != s.v_;
    }

    virtual SerializedTypeID
    getSType() const override
    {
        return STI_ARRAY;
    }
    virtual bool
    isEquivalent(const STBase& t) const override;
    virtual bool
    isDefault() const override
    {
        return v_.empty();
    }
};

}  // namespace ripple

#endif
