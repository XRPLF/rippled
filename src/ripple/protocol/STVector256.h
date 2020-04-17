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

#ifndef RIPPLE_PROTOCOL_STVECTOR256_H_INCLUDED
#define RIPPLE_PROTOCOL_STVECTOR256_H_INCLUDED

#include <ripple/protocol/STBase.h>
#include <ripple/protocol/STBitString.h>
#include <ripple/protocol/STInteger.h>

namespace ripple {

class STVector256 : public STBase
{
public:
    using value_type = std::vector<uint256> const&;

    STVector256() = default;

    explicit STVector256(SField const& n) : STBase(n)
    {
    }

    explicit STVector256(std::vector<uint256> const& vector) : mValue(vector)
    {
    }

    STVector256(SField const& n, std::vector<uint256> const& vector)
        : STBase(n), mValue(vector)
    {
    }

    STVector256(SerialIter& sit, SField const& name);

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

    SerializedTypeID
    getSType() const override
    {
        return STI_VECTOR256;
    }

    void
    add(Serializer& s) const override;

    Json::Value getJson(JsonOptions) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override
    {
        return mValue.empty();
    }

    STVector256&
    operator=(std::vector<uint256> const& v)
    {
        mValue = v;
        return *this;
    }

    STVector256&
    operator=(std::vector<uint256>&& v)
    {
        mValue = std::move(v);
        return *this;
    }

    void
    setValue(const STVector256& v)
    {
        mValue = v.mValue;
    }

    /** Retrieve a copy of the vector we contain */
    explicit operator std::vector<uint256>() const
    {
        return mValue;
    }

    std::size_t
    size() const
    {
        return mValue.size();
    }

    void
    resize(std::size_t n)
    {
        return mValue.resize(n);
    }

    bool
    empty() const
    {
        return mValue.empty();
    }

    std::vector<uint256>::reference
    operator[](std::vector<uint256>::size_type n)
    {
        return mValue[n];
    }

    std::vector<uint256>::const_reference
    operator[](std::vector<uint256>::size_type n) const
    {
        return mValue[n];
    }

    std::vector<uint256> const&
    value() const
    {
        return mValue;
    }

    std::vector<uint256>::iterator
    insert(std::vector<uint256>::const_iterator pos, uint256 const& value)
    {
        return mValue.insert(pos, value);
    }

    std::vector<uint256>::iterator
    insert(std::vector<uint256>::const_iterator pos, uint256&& value)
    {
        return mValue.insert(pos, std::move(value));
    }

    void
    push_back(uint256 const& v)
    {
        mValue.push_back(v);
    }

    std::vector<uint256>::iterator
    begin()
    {
        return mValue.begin();
    }

    std::vector<uint256>::const_iterator
    begin() const
    {
        return mValue.begin();
    }

    std::vector<uint256>::iterator
    end()
    {
        return mValue.end();
    }

    std::vector<uint256>::const_iterator
    end() const
    {
        return mValue.end();
    }

    std::vector<uint256>::iterator
    erase(std::vector<uint256>::iterator position)
    {
        return mValue.erase(position);
    }

    void
    clear() noexcept
    {
        return mValue.clear();
    }

private:
    std::vector<uint256> mValue;
};

}  // namespace ripple

#endif
