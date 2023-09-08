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

#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/STVector256.h>
#include <ripple/protocol/jss.h>

namespace ripple {

STVector256::STVector256(SerialIter& sit, SField const& name) : STBase(name)
{
    auto const slice = sit.getVL();

    if (slice.size() % uint256::size() != 0)
        Throw<std::runtime_error>(
            "Bad serialization for STVector256: " +
            std::to_string(slice.size()));

    auto const cnt = slice.size() / uint256::size();

    mValue.reserve(cnt);

    for (std::size_t i = 0; i != cnt; ++i)
        mValue.emplace_back(slice.substr(i * uint256::size(), uint256::size()));
}

STBase*
STVector256::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STVector256::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STVector256::getSType() const
{
    return STI_VECTOR256;
}

bool
STVector256::isDefault() const
{
    return mValue.empty();
}

void
STVector256::add(SerializerBase& s) const
{
    assert(getFName().isBinary());
    assert(getFName().fieldType == STI_VECTOR256);

    // Ensure that `base_uint` has no padding bytes.
    static_assert(
        std::is_trivially_copyable_v<decltype(mValue)::value_type> &&
        std::has_unique_object_representations_v<decltype(
            mValue)::value_type> &&
        sizeof(decltype(mValue)::value_type) ==
            decltype(mValue)::value_type::bytes);

    s.addVL(mValue.data(), mValue.size() * decltype(mValue)::value_type::bytes);
}

bool
STVector256::isEquivalent(const STBase& t) const
{
    auto* v = dynamic_cast<STVector256 const*>(&t);
    return v && (mValue == v->mValue);
}

Json::Value STVector256::getJson(JsonOptions) const
{
    Json::Value ret(Json::arrayValue);

    for (auto const& vEntry : mValue)
        ret.append(to_string(vEntry));

    return ret;
}

}  // namespace ripple
