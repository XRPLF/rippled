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

#include <ripple/protocol/STAccount.h>

#include <cstring>

namespace ripple {

STAccount::STAccount() : STBase(), value_(beast::zero), default_(true)
{
}

STAccount::STAccount(SField const& n)
    : STBase(n), value_(beast::zero), default_(true)
{
}

STAccount::STAccount(SField const& n, Buffer&& v) : STAccount(n)
{
    if (v.empty())
        return;  // Zero is a valid size for a defaulted STAccount.

    // Is it safe to throw from this constructor?  Today (November 2015)
    // the only place that calls this constructor is
    //    STVar::STVar (SerialIter&, SField const&)
    // which throws.  If STVar can throw in its constructor, then so can
    // STAccount.
    if (v.size() != uint160::bytes)
        Throw<std::runtime_error>("Invalid STAccount size");

    default_ = false;
    memcpy(value_.begin(), v.data(), uint160::bytes);
}

STAccount::STAccount(SerialIter& sit, SField const& name)
    : STAccount(name, sit.getVLBuffer())
{
}

STAccount::STAccount(SField const& n, AccountID const& v)
    : STBase(n), value_(v), default_(false)
{
}

STBase*
STAccount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STAccount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STAccount::getSType() const
{
    return STI_ACCOUNT;
}

void
STAccount::add(Serializer& s) const
{
    assert(getFName().isBinary());
    assert(getFName().fieldType == STI_ACCOUNT);

    // Preserve the serialization behavior of an STBlob:
    //  o If we are default (all zeros) serialize as an empty blob.
    //  o Otherwise serialize 160 bits.
    int const size = isDefault() ? 0 : uint160::bytes;
    s.addVL(value_.data(), size);
}

bool
STAccount::isEquivalent(const STBase& t) const
{
    auto const* const tPtr = dynamic_cast<STAccount const*>(&t);
    return tPtr && (default_ == tPtr->default_) && (value_ == tPtr->value_);
}

bool
STAccount::isDefault() const
{
    return default_;
}

std::string
STAccount::getText() const
{
    if (isDefault())
        return "";
    return toBase58(value());
}

}  // namespace ripple
