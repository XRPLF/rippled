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

namespace xrpl {

STJson::STJson(SField const& name) : STBase{name}, data_{Map{}}
{
}

STJson::STJson(SerialIter& sit, SField const& name) : STBase{name}
{
    if (sit.empty())
    {
        data_ = Map{};
        return;
    }

    int length = sit.getVLDataLength();
    if (length < 0)
        Throw<std::runtime_error>("Invalid STJson length");

    if (length == 0)
    {
        data_ = Map{};
        return;
    }

    // Read type byte
    auto typeByte = sit.get8();
    JsonType type = static_cast<JsonType>(typeByte);
    length--;  // Account for type byte

    int initialBytesLeft = sit.getBytesLeft();

    if (type == JsonType::Array)
    {
        Array array;
        while (sit.getBytesLeft() > 0 &&
               (initialBytesLeft - sit.getBytesLeft()) < length)
        {
            auto valueVL = sit.getVL();
            if (!valueVL.empty())
            {
                SerialIter valueSit(valueVL.data(), valueVL.size());
                auto value = makeValueFromVLWithType(valueSit);
                array.push_back(std::move(value));
            }
            else
            {
                array.push_back(nullptr);
            }
        }
        data_ = std::move(array);
    }
    else  // JsonType::Object
    {
        Map map;
        while (sit.getBytesLeft() > 0 &&
               (initialBytesLeft - sit.getBytesLeft()) < length)
        {
            auto [key, value] = parsePair(sit);
            map.emplace(std::move(key), std::move(value));
        }
        data_ = std::move(map);
    }

    int consumedBytes = initialBytesLeft - sit.getBytesLeft();
    if (consumedBytes != length)
        Throw<std::runtime_error>("STJson length mismatch");
}

STJson::STJson(Map&& map) : data_(std::move(map))
{
}

STJson::STJson(Array&& array) : data_(std::move(array))
{
}

SerializedTypeID
STJson::getSType() const
{
    return STI_JSON;
}

bool
STJson::isArray() const
{
    return std::holds_alternative<Array>(data_);
}

bool
STJson::isObject() const
{
    return std::holds_alternative<Map>(data_);
}

STJson::JsonType
STJson::getType() const
{
    return isArray() ? JsonType::Array : JsonType::Object;
}

int
STJson::getDepth() const
{
    if (isArray())
    {
        auto const& array = std::get<Array>(data_);
        for (auto const& value : array)
        {
            if (value)
            {
                auto nested = std::dynamic_pointer_cast<STJson>(value);
                if (nested)
                    return 1 + nested->getDepth();
            }
        }
        return 0;
    }
    else  // isObject()
    {
        auto const& map = std::get<Map>(data_);
        for (auto const& [key, value] : map)
        {
            if (value)
            {
                auto nested = std::dynamic_pointer_cast<STJson>(value);
                if (nested)
                    return 1 + nested->getDepth();
            }
        }
        return 0;
    }
}

void
STJson::validateDepth(Value const& value, int currentDepth) const
{
    if (!value)
        return;

    auto nested = std::dynamic_pointer_cast<STJson>(value);
    if (!nested)
        return;

    int valueDepth = nested->getDepth();
    if (currentDepth + valueDepth > 1)
        Throw<std::runtime_error>("STJson nesting depth exceeds maximum of 1");
}

void
STJson::setObjectField(Key const& key, Value const& value)
{
    if (!isObject())
        Throw<std::runtime_error>(
            "STJson::setObjectField called on non-object");
    validateDepth(value, 0);
    std::get<Map>(data_)[key] = value;
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
    if (sit.empty())
        return nullptr;

    int length = sit.getVLDataLength();
    if (length < 0)
        Throw<std::runtime_error>("Invalid STJson length");

    if (length == 0)
        return std::make_shared<STJson>(Map{});

    // Read type byte
    auto typeByte = sit.get8();
    JsonType type = static_cast<JsonType>(typeByte);
    length--;  // Account for type byte

    int initialBytesLeft = sit.getBytesLeft();

    if (type == JsonType::Array)
    {
        Array array;
        while (sit.getBytesLeft() > 0 &&
               (initialBytesLeft - sit.getBytesLeft()) < length)
        {
            auto valueVL = sit.getVL();
            if (!valueVL.empty())
            {
                SerialIter valueSit(valueVL.data(), valueVL.size());
                auto value = makeValueFromVLWithType(valueSit);
                array.push_back(std::move(value));
            }
            else
            {
                array.push_back(nullptr);
            }
        }

        int consumedBytes = initialBytesLeft - sit.getBytesLeft();
        if (consumedBytes != length)
            Throw<std::runtime_error>("STJson length mismatch");

        return std::make_shared<STJson>(std::move(array));
    }
    else  // JsonType::Object
    {
        Map map;
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

STJson::Array
STJson::parseArray(SerialIter& sit, int length)
{
    Array array;
    int initialBytesLeft = sit.getBytesLeft();

    while (sit.getBytesLeft() > 0 &&
           (initialBytesLeft - sit.getBytesLeft()) < length)
    {
        auto valueVL = sit.getVL();
        if (!valueVL.empty())
        {
            SerialIter valueSit(valueVL.data(), valueVL.size());
            auto value = makeValueFromVLWithType(valueSit);
            array.push_back(std::move(value));
        }
        else
        {
            array.push_back(nullptr);
        }
    }

    return array;
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
STJson::getObjectField(Key const& key) const
{
    if (!isObject())
        return std::nullopt;

    auto const& map = std::get<Map>(data_);
    auto it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;
    return it->second;
}

void
STJson::setNestedObjectField(
    Key const& key,
    Key const& nestedKey,
    Value const& value)
{
    if (!isObject())
        Throw<std::runtime_error>(
            "STJson::setNestedObjectField called on non-object");

    validateDepth(value, 1);  // We're at depth 1 (nested)

    auto& map = std::get<Map>(data_);
    auto it = map.find(key);
    std::shared_ptr<STJson> nested;
    if (it == map.end() || !it->second)
    {
        // Create new nested STJson
        nested = std::make_shared<STJson>();
        map[key] = nested;
    }
    else
    {
        nested = std::dynamic_pointer_cast<STJson>(it->second);
        if (!nested)
        {
            // Overwrite with new STJson if not an STJson
            nested = std::make_shared<STJson>();
            map[key] = nested;
        }
    }
    nested->setObjectField(nestedKey, value);
}

std::optional<STJson::Value>
STJson::getNestedObjectField(Key const& key, Key const& nestedKey) const
{
    if (!isObject())
        return std::nullopt;

    auto const& map = std::get<Map>(data_);
    auto it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;
    auto nested = std::dynamic_pointer_cast<STJson>(it->second);
    if (!nested)
        return std::nullopt;
    return nested->getObjectField(nestedKey);
}

STJson::Map const&
STJson::getMap() const
{
    if (!isObject())
        Throw<std::runtime_error>("STJson::getMap called on non-object");
    return std::get<Map>(data_);
}

STJson::Array const&
STJson::getArray() const
{
    if (!isArray())
        Throw<std::runtime_error>("STJson::getArray called on non-array");
    return std::get<Array>(data_);
}

void
STJson::pushArrayElement(Value const& value)
{
    if (!isArray())
        Throw<std::runtime_error>(
            "STJson::pushArrayElement called on non-array");
    validateDepth(value, 0);
    std::get<Array>(data_).push_back(value);
}

std::optional<STJson::Value>
STJson::getArrayElement(size_t index) const
{
    if (!isArray())
        return std::nullopt;

    auto const& array = std::get<Array>(data_);
    if (index >= array.size())
        return std::nullopt;

    return array[index];
}

void
STJson::setArrayElement(size_t index, Value const& value)
{
    if (!isArray())
        Throw<std::runtime_error>(
            "STJson::setArrayElement called on non-array");
    validateDepth(value, 0);

    auto& array = std::get<Array>(data_);
    // Auto-resize with nulls if needed
    if (index >= array.size())
        array.resize(index + 1, nullptr);

    array[index] = value;
}

void
STJson::setArrayElementField(size_t index, Key const& key, Value const& value)
{
    if (!isArray())
        Throw<std::runtime_error>(
            "STJson::setArrayElementField called on non-array");

    validateDepth(value, 1);  // We're at depth 1 (inside array element)

    auto& array = std::get<Array>(data_);
    // Auto-resize with nulls if needed
    if (index >= array.size())
        array.resize(index + 1, nullptr);

    // Get or create STJson object at index
    std::shared_ptr<STJson> element;
    if (!array[index])
    {
        element = std::make_shared<STJson>(Map{});
        array[index] = element;
    }
    else
    {
        element = std::dynamic_pointer_cast<STJson>(array[index]);
        if (!element)
        {
            // Replace with new STJson if not an STJson
            element = std::make_shared<STJson>(Map{});
            array[index] = element;
        }
    }

    element->setObjectField(key, value);
}

std::optional<STJson::Value>
STJson::getArrayElementField(size_t index, Key const& key) const
{
    if (!isArray())
        return std::nullopt;

    auto const& array = std::get<Array>(data_);
    if (index >= array.size())
        return std::nullopt;

    auto element = std::dynamic_pointer_cast<STJson>(array[index]);
    if (!element)
        return std::nullopt;

    return element->getObjectField(key);
}

size_t
STJson::arraySize() const
{
    if (!isArray())
        return 0;
    return std::get<Array>(data_).size();
}

void
STJson::setNestedArrayElement(Key const& key, size_t index, Value const& value)
{
    if (!isObject())
        Throw<std::runtime_error>(
            "STJson::setNestedArrayElement called on non-object");

    validateDepth(value, 1);  // We're at depth 1 (nested array)

    auto& map = std::get<Map>(data_);
    auto it = map.find(key);
    std::shared_ptr<STJson> arrayJson;

    if (it == map.end() || !it->second)
    {
        // Create new nested STJson array
        arrayJson = std::make_shared<STJson>(Array{});
        map[key] = arrayJson;
    }
    else
    {
        arrayJson = std::dynamic_pointer_cast<STJson>(it->second);
        if (!arrayJson)
        {
            // Replace with new STJson array if not an STJson
            arrayJson = std::make_shared<STJson>(Array{});
            map[key] = arrayJson;
        }
        else if (!arrayJson->isArray())
        {
            // Replace with array if not an array
            arrayJson = std::make_shared<STJson>(Array{});
            map[key] = arrayJson;
        }
    }

    arrayJson->setArrayElement(index, value);
}

void
STJson::setNestedArrayElementField(
    Key const& key,
    size_t index,
    Key const& nestedKey,
    Value const& value)
{
    if (!isObject())
        Throw<std::runtime_error>(
            "STJson::setNestedArrayElementField called on non-object");

    validateDepth(value, 1);  // We're at depth 1 (nested array element field -
                              // still counts as depth 1)

    auto& map = std::get<Map>(data_);
    auto it = map.find(key);
    std::shared_ptr<STJson> arrayJson;

    if (it == map.end() || !it->second)
    {
        // Create new nested STJson array
        arrayJson = std::make_shared<STJson>(Array{});
        map[key] = arrayJson;
    }
    else
    {
        arrayJson = std::dynamic_pointer_cast<STJson>(it->second);
        if (!arrayJson)
        {
            // Replace with new STJson array if not an STJson
            arrayJson = std::make_shared<STJson>(Array{});
            map[key] = arrayJson;
        }
        else if (!arrayJson->isArray())
        {
            // Replace with array if not an array
            arrayJson = std::make_shared<STJson>(Array{});
            map[key] = arrayJson;
        }
    }

    arrayJson->setArrayElementField(index, nestedKey, value);
}

std::optional<STJson::Value>
STJson::getNestedArrayElement(Key const& key, size_t index) const
{
    if (!isObject())
        return std::nullopt;

    auto const& map = std::get<Map>(data_);
    auto it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;

    auto arrayJson = std::dynamic_pointer_cast<STJson>(it->second);
    if (!arrayJson || !arrayJson->isArray())
        return std::nullopt;

    return arrayJson->getArrayElement(index);
}

std::optional<STJson::Value>
STJson::getNestedArrayElementField(
    Key const& key,
    size_t index,
    Key const& nestedKey) const
{
    if (!isObject())
        return std::nullopt;

    auto const& map = std::get<Map>(data_);
    auto it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;

    auto arrayJson = std::dynamic_pointer_cast<STJson>(it->second);
    if (!arrayJson || !arrayJson->isArray())
        return std::nullopt;

    return arrayJson->getArrayElementField(index, nestedKey);
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

    // Add type byte
    inner.add8(static_cast<uint8_t>(getType()));

    if (isArray())
    {
        auto const& array = std::get<Array>(data_);
        for (auto const& value : array)
        {
            addVLValue(inner, value);
        }
    }
    else  // isObject()
    {
        auto const& map = std::get<Map>(data_);
        for (auto const& [key, value] : map)
        {
            addVLKey(inner, key);
            addVLValue(inner, value);
        }
    }

    s.addVL(inner.peekData().data(), inner.peekData().size());
}

Json::Value
STJson::getJson(JsonOptions options) const
{
    if (isArray())
    {
        Json::Value arr(Json::arrayValue);
        auto const& array = std::get<Array>(data_);
        for (auto const& value : array)
        {
            if (value)
                arr.append(value->getJson(options));
            else
                arr.append(Json::nullValue);
        }
        return arr;
    }
    else  // isObject()
    {
        Json::Value obj(Json::objectValue);
        auto const& map = std::get<Map>(data_);
        for (auto const& [key, value] : map)
        {
            if (value)
                obj[key] = value->getJson(options);
            else
                obj[key] = Json::nullValue;
        }
        return obj;
    }
}

bool
STJson::isEquivalent(STBase const& t) const
{
    auto const* const tPtr = dynamic_cast<STJson const*>(&t);
    return tPtr && (data_ == tPtr->data_);
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
    data_ = v.data_;
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

}  // namespace xrpl
