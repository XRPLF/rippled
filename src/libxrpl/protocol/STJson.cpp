//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STDataType.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace ripple {

STJson::STJson(SField const& name) : STBase{name}
{
}

STJson::STJson(SerialIter& sit, SField const& name) : STBase{name}
{
    if (sit.empty())
        return;

    int length = sit.getVLDataLength();
    if (length < 0)
        Throw<std::runtime_error>("Invalid STJson length");

    int initialBytesLeft = sit.getBytesLeft();
    while (sit.getBytesLeft() > 0 &&
           (initialBytesLeft - sit.getBytesLeft()) < length)
    {
        auto [key, value] = parsePair(sit);
        map_.emplace(std::move(key), std::move(value));
    }

    int consumedBytes = initialBytesLeft - sit.getBytesLeft();
    if (consumedBytes != length)
        Throw<std::runtime_error>("STJson length mismatch");
}

STJson::STJson(Map&& map) : map_(std::move(map))
{
}

SerializedTypeID
STJson::getSType() const
{
    return STI_JSON;
}

void
STJson::set(Key const& key, Value const& value)
{
    map_[key] = value;
}

std::shared_ptr<STJson>
STJson::fromBlob(void const* data, std::size_t size)
{
    SerialIter sit(static_cast<uint8_t const*>(data), size);
    return fromSerialIter(sit);
}

std::shared_ptr<STJson>
STJson::fromSerialIter(SerialIter& sit)
{
    Map map;
    if (sit.empty())
        return nullptr;

    int length = sit.getVLDataLength();
    if (length < 0)
        Throw<std::runtime_error>("Invalid STJson length");

    int initialBytesLeft = sit.getBytesLeft();
    while (sit.getBytesLeft() > 0 &&
           (initialBytesLeft - sit.getBytesLeft()) < length)
    {
        auto [key, value] = parsePair(sit);
        map.emplace(std::move(key), std::move(value));
    }

    int consumedBytes = initialBytesLeft - sit.getBytesLeft();
    if (consumedBytes != length)
        Throw<std::runtime_error>("STJson length mismatch");

    return std::make_shared<STJson>(std::move(map));
}

std::pair<STJson::Key, STJson::Value>
STJson::parsePair(SerialIter& sit)
{
    auto keyBlob = sit.getVL();
    std::string key(
        reinterpret_cast<char const*>(keyBlob.data()), keyBlob.size());
    auto valueVL = sit.getVL();
    if (valueVL.empty())
        return {std::move(key), nullptr};

    SerialIter valueSit(valueVL.data(), valueVL.size());
    auto value = makeValueFromVLWithType(valueSit);

    return {std::move(key), std::move(value)};
}

STJson::Value
STJson::makeValueFromVLWithType(SerialIter& sit)
{
    if (sit.getBytesLeft() == 0)
        return nullptr;

    // Read SType marker (1 byte)
    auto typeCode = sit.get8();
    SerializedTypeID stype = static_cast<SerializedTypeID>(typeCode);

    // Dispatch to correct SType
    switch (stype)
    {
        case STI_UINT8:
            return std::make_shared<STUInt8>(sfCloseResolution, sit.get8());
        case STI_UINT16:
            return std::make_shared<STUInt16>(sfSignerWeight, sit.get16());
        case STI_UINT32:
            return std::make_shared<STUInt32>(sfNetworkID, sit.get32());
        case STI_UINT64:
            return std::make_shared<STUInt64>(sfIndexNext, sit.get64());
        case STI_UINT128:
            return std::make_shared<STUInt128>(sfEmailHash, sit.get128());
        case STI_UINT160:
            return std::make_shared<STUInt160>(
                sfTakerPaysCurrency, sit.get160());
        case STI_UINT192:
            return std::make_shared<STUInt192>(
                sfMPTokenIssuanceID, sit.get192());
        case STI_UINT256:
            return std::make_shared<STUInt256>(sfLedgerHash, sit.get256());
        case STI_VL: {
            auto blob = sit.getVL();
            return std::make_shared<STBlob>(sfData, blob.data(), blob.size());
        }
        case STI_ACCOUNT:
            return std::make_shared<STAccount>(sit, sfAccount);
        case STI_AMOUNT:
            return std::make_shared<STAmount>(sit, sfAmount);
        // case STI_NUMBER:
        //     return std::make_shared<STNumber>(sit, sfNumber);
        case STI_ISSUE:
            return std::make_shared<STIssue>(sit, sfAsset);
        case STI_CURRENCY:
            return std::make_shared<STCurrency>(sit, sfBaseAsset);
        case STI_JSON:
            return std::make_shared<STJson>(sit, sfContractJson);
        case STI_OBJECT:
        case STI_ARRAY:
        case STI_PATHSET:
        case STI_VECTOR256:
        default:
            // Unknown type, treat as blob
            {
                auto blob = sit.getSlice(sit.getBytesLeft());
                return std::make_shared<STBlob>(
                    sfData, blob.data(), blob.size());
            }
    }
}

std::optional<STJson::Value>
STJson::get(Key const& key) const
{
    auto it = map_.find(key);
    if (it == map_.end() || !it->second)
        return std::nullopt;
    return it->second;
}

void
STJson::setNested(Key const& nestedKey, Key const& key, Value const& value)
{
    auto it = map_.find(key);
    std::shared_ptr<STJson> nested;
    if (it == map_.end() || !it->second)
    {
        // Create new nested STJson
        nested = std::make_shared<STJson>();
        map_[key] = nested;
    }
    else
    {
        nested = std::dynamic_pointer_cast<STJson>(it->second);
        if (!nested)
        {
            // Overwrite with new STJson if not an STJson
            nested = std::make_shared<STJson>();
            map_[key] = nested;
        }
    }
    nested->set(nestedKey, value);
}

std::optional<STJson::Value>
STJson::getNested(Key const& nestedKey, Key const& key) const
{
    auto it = map_.find(key);
    if (it == map_.end() || !it->second)
        return std::nullopt;
    auto nested = std::dynamic_pointer_cast<STJson>(it->second);
    if (!nested)
        return std::nullopt;
    return nested->get(nestedKey);
}

void
STJson::addVLKey(Serializer& s, std::string const& str)
{
    s.addVL(str.data(), str.size());
}

void
STJson::addVLValue(Serializer& s, std::shared_ptr<STBase> const& value)
{
    if (!value)
    {
        s.addVL(nullptr, 0);
        return;
    }
    Serializer tmp;
    tmp.add8(static_cast<uint8_t>(value->getSType()));
    value->add(tmp);
    s.addVL(tmp.peekData().data(), tmp.peekData().size());
}

void
STJson::add(Serializer& s) const
{
    Serializer inner;
    for (auto const& [key, value] : map_)
    {
        addVLKey(inner, key);
        addVLValue(inner, value);
    }
    s.addVL(inner.peekData().data(), inner.peekData().size());
}

Json::Value
STJson::getJson(JsonOptions options) const
{
    Json::Value obj(Json::objectValue);
    for (auto const& [key, value] : map_)
    {
        if (value)
            obj[key] = value->getJson(options);
        else
            obj[key] = Json::nullValue;
    }
    return obj;
}

bool
STJson::isEquivalent(STBase const& t) const
{
    auto const* const tPtr = dynamic_cast<STJson const*>(&t);
    return tPtr && (map_ == tPtr->map_);
}

bool
STJson::isDefault() const
{
    return default_;
}

Blob
STJson::toBlob() const
{
    Serializer s;
    add(s);
    return s.peekData();
}

std::size_t
STJson::size() const
{
    Serializer s;
    add(s);
    return s.size();
}

void
STJson::setValue(STJson const& v)
{
    map_ = v.map_;
}

STBase*
STJson::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STJson::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

}  // namespace ripple
