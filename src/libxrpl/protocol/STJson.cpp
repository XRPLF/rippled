#include <xrpl/protocol/STJson.h>

#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/Serializer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace xrpl {

// The deepest an STJson nests below the top: an object holding an array of objects, which
// setNestedArrayElementField builds.
static constexpr int kMaxNestingDepth = 2;

STJson::STJson(SField const& name) : STBase{name}, data_{Map{}}
{
}

STJson::STJson(SerialIter& sit, SField const& name) : STBase{name}
{
    parse(sit, 0);
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
                auto const nested = std::dynamic_pointer_cast<STJson>(value);
                if (nested)
                    return 1 + nested->getDepth();
            }
        }
        return 0;
    }
    // isObject()

    auto const& map = std::get<Map>(data_);
    for (auto const& [key, value] : map)
    {
        if (value)
        {
            auto const nested = std::dynamic_pointer_cast<STJson>(value);
            if (nested)
                return 1 + nested->getDepth();
        }
    }
    return 0;
}

void
STJson::validateDepth(Value const& value, int currentDepth) const
{
    if (!value)
        return;

    auto const nested = std::dynamic_pointer_cast<STJson>(value);
    if (!nested)
        return;

    // Adding an STJson value increases depth by 1
    int const totalDepth = currentDepth + 1 + nested->getDepth();
    if (totalDepth > 1)
        Throw<std::runtime_error>("STJson nesting depth exceeds maximum of 1");

    // Arrays cannot contain arrays
    if (isArray() && nested->isArray())
        Throw<std::runtime_error>("STJson arrays cannot contain arrays");
}

void
STJson::setObjectField(Key const& key, Value const& value)
{
    if (!isObject())
        Throw<std::runtime_error>("STJson::setObjectField called on non-object");
    validateDepth(value, 0);
    std::get<Map>(data_)[key] = value;
}

void
STJson::eraseObjectField(Key const& key)
{
    if (!isObject())
        Throw<std::runtime_error>("STJson::eraseObjectField called on non-object");
    std::get<Map>(data_).erase(key);
}

std::shared_ptr<STJson>
STJson::fromBlob(void const* data, std::size_t size)
{
    SerialIter sit(static_cast<uint8_t const*>(data), size);
    auto json = std::make_shared<STJson>(sit, sfGeneric);
    if (!sit.empty())
        Throw<std::runtime_error>("STJson has trailing bytes");
    return json;
}

void
STJson::parse(SerialIter& sit, int depth)
{
    if (depth > kMaxNestingDepth)
        Throw<std::runtime_error>("STJson nesting depth exceeds maximum");

    SerialIter body(sit.getSlice(static_cast<std::size_t>(sit.getVLDataLength())));
    if (body.empty())
        Throw<std::runtime_error>("Invalid STJson length");

    auto const type = body.get8();
    if (type == static_cast<std::uint8_t>(JsonType::Array))
    {
        Array array;
        while (!body.empty())
        {
            auto value = parseEntryValue(body, depth);
            if (auto const nested = std::dynamic_pointer_cast<STJson>(value);
                nested && nested->isArray())
                Throw<std::runtime_error>("STJson arrays cannot contain arrays");
            array.push_back(std::move(value));
        }
        data_ = std::move(array);
    }
    else if (type == static_cast<std::uint8_t>(JsonType::Object))
    {
        Map map;
        while (!body.empty())
        {
            auto const keyBlob = body.getVL();
            std::string key(reinterpret_cast<char const*>(keyBlob.data()), keyBlob.size());
            if (!map.emplace(std::move(key), parseEntryValue(body, depth)).second)
                Throw<std::runtime_error>("STJson duplicate key");
        }
        data_ = std::move(map);
    }
    else
    {
        Throw<std::runtime_error>("STJson unknown type");
    }
}

STJson::Value
STJson::parseEntryValue(SerialIter& sit, int depth)
{
    auto const bytes = sit.getVL();
    if (bytes.empty())
        return nullptr;

    SerialIter valueSit(bytes.data(), bytes.size());
    auto value = parseValue(valueSit, depth + 1);
    if (!valueSit.empty())
        Throw<std::runtime_error>("STJson value has trailing bytes");
    return value;
}

STJson::Value
STJson::makeValueFromVLWithType(SerialIter& sit)
{
    return parseValue(sit, 1);
}

STJson::Value
STJson::parseValue(SerialIter& sit, int depth)
{
    switch (static_cast<SerializedTypeID>(sit.get8()))
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
            return std::make_shared<STUInt160>(sfTakerPaysCurrency, sit.get160());
        case STI_UINT192:
            return std::make_shared<STUInt192>(sfMPTokenIssuanceID, sit.get192());
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
        case STI_ISSUE:
            return std::make_shared<STIssue>(sit, sfAsset);
        case STI_CURRENCY:
            return std::make_shared<STCurrency>(sit, sfBaseAsset);
        case STI_JSON: {
            auto json = std::make_shared<STJson>(sfContractJson);
            json->parse(sit, depth);
            return json;
        }
        default:
            Throw<std::runtime_error>("STJson unsupported value type");
    }
}

std::optional<STJson::Value>
STJson::getObjectField(Key const& key) const
{
    if (!isObject())
        return std::nullopt;

    auto const& map = std::get<Map>(data_);
    auto const it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;
    return it->second;
}

void
STJson::setNestedObjectField(Key const& key, Key const& nestedKey, Value const& value)
{
    if (!isObject())
        Throw<std::runtime_error>("STJson::setNestedObjectField called on non-object");

    validateDepth(value, 1);  // We're at depth 1 (nested)

    auto& map = std::get<Map>(data_);
    auto const it = map.find(key);
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
    auto const it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;
    auto const nested = std::dynamic_pointer_cast<STJson>(it->second);
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
        Throw<std::runtime_error>("STJson::pushArrayElement called on non-array");
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
        Throw<std::runtime_error>("STJson::setArrayElement called on non-array");
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
        Throw<std::runtime_error>("STJson::setArrayElementField called on non-array");

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

    auto const element = std::dynamic_pointer_cast<STJson>(array[index]);
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
        Throw<std::runtime_error>("STJson::setNestedArrayElement called on non-object");

    validateDepth(value, 1);  // We're at depth 1 (nested array)

    auto& map = std::get<Map>(data_);
    auto const it = map.find(key);
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
        if (!arrayJson || !arrayJson->isArray())
        {
            // Replace a value that is not an STJson array with an empty one.
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
        Throw<std::runtime_error>("STJson::setNestedArrayElementField called on non-object");

    validateDepth(value, 1);  // We're at depth 1 (nested array element field -
                              // still counts as depth 1)

    auto& map = std::get<Map>(data_);
    auto const it = map.find(key);
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
        if (!arrayJson || !arrayJson->isArray())
        {
            // Replace a value that is not an STJson array with an empty one.
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
    auto const it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;

    auto const arrayJson = std::dynamic_pointer_cast<STJson>(it->second);
    if (!arrayJson || !arrayJson->isArray())
        return std::nullopt;

    return arrayJson->getArrayElement(index);
}

std::optional<STJson::Value>
STJson::getNestedArrayElementField(Key const& key, size_t index, Key const& nestedKey) const
{
    if (!isObject())
        return std::nullopt;

    auto const& map = std::get<Map>(data_);
    auto const it = map.find(key);
    if (it == map.end() || !it->second)
        return std::nullopt;

    auto const arrayJson = std::dynamic_pointer_cast<STJson>(it->second);
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

json::Value
STJson::getJson(JsonOptions options) const
{
    if (isArray())
    {
        json::Value arr(json::ValueType::Array);
        auto const& array = std::get<Array>(data_);
        for (auto const& value : array)
        {
            if (value)
            {
                arr.append(value->getJson(options));
            }
            else
            {
                arr.append(json::ValueType::Null);
            }
        }
        return arr;
    }
    // isObject()

    json::Value obj(json::ValueType::Object);
    auto const& map = std::get<Map>(data_);
    for (auto const& [key, value] : map)
    {
        auto const hexKey = strHex(key);
        if (value)
        {
            obj[hexKey] = value->getJson(options);
        }
        else
        {
            obj[hexKey] = json::ValueType::Null;
        }
    }
    return obj;
}

// Whether two values are both null, or both present and equivalent.
static bool
sameValue(STJson::Value const& a, STJson::Value const& b)
{
    if (!a || !b)
        return !a && !b;
    return a->isEquivalent(*b);
}

bool
STJson::isEquivalent(STBase const& t) const
{
    auto const* const other = dynamic_cast<STJson const*>(&t);
    if (other == nullptr || isArray() != other->isArray())
        return false;
    if (isArray())
        return std::ranges::equal(std::get<Array>(data_), std::get<Array>(other->data_), sameValue);
    return std::ranges::equal(
        std::get<Map>(data_), std::get<Map>(other->data_), [](auto const& a, auto const& b) {
            return a.first == b.first && sameValue(a.second, b.second);
        });
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
