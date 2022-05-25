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
    std::vector<uint256> mValue;

public:
    using value_type = std::vector<uint256> const&;

    STVector256() = default;

    explicit STVector256(SField const& n);
    explicit STVector256(std::vector<uint256> const& vector);
    STVector256(SField const& n, std::vector<uint256> const& vector);
    STVector256(SerialIter& sit, SField const& name);

    SerializedTypeID
    getSType() const override;

    void
    add(Serializer& s) const override;

    Json::Value getJson(JsonOptions) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    STVector256&
    operator=(std::vector<uint256> const& v);

    STVector256&
    operator=(std::vector<uint256>&& v);

    void
    setValue(const STVector256& v);

    /** Retrieve a copy of the vector we contain */
    explicit operator std::vector<uint256>() const;

    std::size_t
    size() const;

    void
    resize(std::size_t n);

    bool
    empty() const;

    std::vector<uint256>::reference
    operator[](std::vector<uint256>::size_type n);

    std::vector<uint256>::const_reference
    operator[](std::vector<uint256>::size_type n) const;

    std::vector<uint256> const&
    value() const;

    std::vector<uint256>::iterator
    insert(std::vector<uint256>::const_iterator pos, uint256 const& value);

    std::vector<uint256>::iterator
    insert(std::vector<uint256>::const_iterator pos, uint256&& value);

    void
    push_back(uint256 const& v);

    std::vector<uint256>::iterator
    begin();

    std::vector<uint256>::const_iterator
    begin() const;

    std::vector<uint256>::iterator
    end();

    std::vector<uint256>::const_iterator
    end() const;

    std::vector<uint256>::iterator
    erase(std::vector<uint256>::iterator position);

    void
    clear() noexcept;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STVector256::STVector256(SField const& n) : STBase(n)
{
}

inline STVector256::STVector256(std::vector<uint256> const& vector)
    : mValue(vector)
{
}

inline STVector256::STVector256(
    SField const& n,
    std::vector<uint256> const& vector)
    : STBase(n), mValue(vector)
{
}

inline STVector256&
STVector256::operator=(std::vector<uint256> const& v)
{
    mValue = v;
    return *this;
}

inline STVector256&
STVector256::operator=(std::vector<uint256>&& v)
{
    mValue = std::move(v);
    return *this;
}

inline void
STVector256::setValue(const STVector256& v)
{
    mValue = v.mValue;
}

/** Retrieve a copy of the vector we contain */
inline STVector256::operator std::vector<uint256>() const
{
    return mValue;
}

inline std::size_t
STVector256::size() const
{
    return mValue.size();
}

inline void
STVector256::resize(std::size_t n)
{
    return mValue.resize(n);
}

inline bool
STVector256::empty() const
{
    return mValue.empty();
}

inline std::vector<uint256>::reference
STVector256::operator[](std::vector<uint256>::size_type n)
{
    return mValue[n];
}

inline std::vector<uint256>::const_reference
STVector256::operator[](std::vector<uint256>::size_type n) const
{
    return mValue[n];
}

inline std::vector<uint256> const&
STVector256::value() const
{
    return mValue;
}

inline std::vector<uint256>::iterator
STVector256::insert(
    std::vector<uint256>::const_iterator pos,
    uint256 const& value)
{
    return mValue.insert(pos, value);
}

inline std::vector<uint256>::iterator
STVector256::insert(std::vector<uint256>::const_iterator pos, uint256&& value)
{
    return mValue.insert(pos, std::move(value));
}

inline void
STVector256::push_back(uint256 const& v)
{
    mValue.push_back(v);
}

inline std::vector<uint256>::iterator
STVector256::begin()
{
    return mValue.begin();
}

inline std::vector<uint256>::const_iterator
STVector256::begin() const
{
    return mValue.begin();
}

inline std::vector<uint256>::iterator
STVector256::end()
{
    return mValue.end();
}

inline std::vector<uint256>::const_iterator
STVector256::end() const
{
    return mValue.end();
}

inline std::vector<uint256>::iterator
STVector256::erase(std::vector<uint256>::iterator position)
{
    return mValue.erase(position);
}

inline void
STVector256::clear() noexcept
{
    return mValue.clear();
}

}  // namespace ripple

#endif
