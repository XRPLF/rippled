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

#ifndef RIPPLE_PROTOCOL_STJSON_H_INCLUDED
#define RIPPLE_PROTOCOL_STJSON_H_INCLUDED

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace xrpl {

/**
 * STJson: Serialized Type for JSON-like structures (objects or arrays).
 *
 * Supports two modes:
 * - Object: Key-value pairs where keys are VL-encoded strings
 * - Array: Ordered list of values
 *
 * Values are [SType marker][VL-encoded SType serialization].
 * Values can be any SType, including nested STJson.
 *
 * Serialization format: [type_byte][VL_length][data...]
 * - type_byte: 0x00 = Object, 0x01 = Array
 */
class STJson : public STBase
{
public:
    enum class JsonType : uint8_t { Object = 0x00, Array = 0x01 };

    using Key = std::string;
    using Value = std::shared_ptr<STBase>;
    using Map = std::map<Key, Value>;
    using Array = std::vector<Value>;

    STJson() = default;

    explicit STJson(Map&& map);
    explicit STJson(Array&& array);
    explicit STJson(SField const& name);
    explicit STJson(SerialIter& sit, SField const& name);

    SerializedTypeID
    getSType() const override;

    // Type checking
    bool
    isArray() const;

    bool
    isObject() const;

    JsonType
    getType() const;

    // Depth checking (0 = no nesting, 1 = one level of nesting)
    int
    getDepth() const;

    // Parse from binary blob
    static std::shared_ptr<STJson>
    fromBlob(void const* data, std::size_t size);

    // Parse from SerialIter
    static std::shared_ptr<STJson>
    fromSerialIter(SerialIter& sit);

    // Serialize to binary
    void
    add(Serializer& s) const override;

    // JSON representation
    Json::Value
    getJson(JsonOptions options) const override;

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

    // Blob representation
    Blob
    toBlob() const;

    // STJson size
    std::size_t
    size() const;

    // Object accessors (only valid when isObject() == true)
    Map const&
    getMap() const;

    void
    setObjectField(Key const& key, Value const& value);

    std::optional<STJson::Value>
    getObjectField(Key const& key) const;

    void
    setNestedObjectField(
        Key const& key,
        Key const& nestedKey,
        Value const& value);

    std::optional<Value>
    getNestedObjectField(Key const& key, Key const& nestedKey) const;

    // Array accessors (only valid when isArray() == true)
    Array const&
    getArray() const;

    void
    pushArrayElement(Value const& value);

    std::optional<Value>
    getArrayElement(size_t index) const;

    void
    setArrayElement(size_t index, Value const& value);

    void
    setArrayElementField(size_t index, Key const& key, Value const& value);

    std::optional<Value>
    getArrayElementField(size_t index, Key const& key) const;

    size_t
    arraySize() const;

    // Nested array accessors (for arrays stored in object fields)
    void
    setNestedArrayElement(Key const& key, size_t index, Value const& value);

    void
    setNestedArrayElementField(
        Key const& key,
        size_t index,
        Key const& nestedKey,
        Value const& value);

    std::optional<Value>
    getNestedArrayElement(Key const& key, size_t index) const;

    std::optional<Value>
    getNestedArrayElementField(
        Key const& key,
        size_t index,
        Key const& nestedKey) const;

    // Factory for SType value from blob (with SType marker)
    static Value
    makeValueFromVLWithType(SerialIter& sit);

    void
    setValue(STJson const& v);

private:
    std::variant<Map, Array> data_{Map{}};
    bool default_{false};

    // Helper: validate nesting depth (max 1 level)
    void
    validateDepth(Value const& value, int currentDepth) const;

    // Helper: parse a single key-value pair from SerialIter
    static std::pair<Key, Value>
    parsePair(SerialIter& sit);

    // Helper: parse array elements from SerialIter
    static Array
    parseArray(SerialIter& sit, int length);

    // Helper: encode a key as VL
    static void
    addVLKey(Serializer& s, std::string const& str);

    // Helper: encode a value as [SType marker][VL]
    static void
    addVLValue(Serializer& s, std::shared_ptr<STBase> const& value);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

}  // namespace xrpl

#endif
