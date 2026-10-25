#pragma once

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

    using value_type = STJson;
    [[nodiscard]] value_type
    value() const
    {
        return *this;
    }

    using Key = std::string;
    using Value = std::shared_ptr<STBase>;
    using Map = std::map<Key, Value>;
    using Array = std::vector<Value>;

    STJson() = default;

    explicit STJson(Map&& map);
    explicit STJson(Array&& array);
    explicit STJson(SField const& name);
    explicit STJson(SerialIter& sit, SField const& name);

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    // Type checking
    [[nodiscard]] bool
    isArray() const;

    [[nodiscard]] bool
    isObject() const;

    [[nodiscard]] JsonType
    getType() const;

    // Depth checking (0 = no nesting, 1 = one level of nesting)
    [[nodiscard]] int
    getDepth() const;

    // Parse from binary blob; throws unless the bytes are exactly one STJson
    static std::shared_ptr<STJson>
    fromBlob(void const* data, std::size_t size);

    // Serialize to binary
    void
    add(Serializer& s) const override;

    // JSON representation
    [[nodiscard]] json::Value
    getJson(JsonOptions options) const override;

    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    [[nodiscard]] bool
    isDefault() const override;

    // Blob representation
    [[nodiscard]] Blob
    toBlob() const;

    // STJson size
    [[nodiscard]] std::size_t
    size() const;

    // Object accessors (only valid when isObject() == true)
    [[nodiscard]] Map const&
    getMap() const;

    void
    setObjectField(Key const& key, Value const& value);

    // Removes `key`; an object without it is left as it is.
    void
    eraseObjectField(Key const& key);

    [[nodiscard]] std::optional<STJson::Value>
    getObjectField(Key const& key) const;

    void
    setNestedObjectField(Key const& key, Key const& nestedKey, Value const& value);

    [[nodiscard]] std::optional<Value>
    getNestedObjectField(Key const& key, Key const& nestedKey) const;

    // Array accessors (only valid when isArray() == true)
    [[nodiscard]] Array const&
    getArray() const;

    void
    pushArrayElement(Value const& value);

    [[nodiscard]] std::optional<Value>
    getArrayElement(size_t index) const;

    void
    setArrayElement(size_t index, Value const& value);

    void
    setArrayElementField(size_t index, Key const& key, Value const& value);

    [[nodiscard]] std::optional<Value>
    getArrayElementField(size_t index, Key const& key) const;

    [[nodiscard]] size_t
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

    [[nodiscard]] std::optional<Value>
    getNestedArrayElement(Key const& key, size_t index) const;

    [[nodiscard]] std::optional<Value>
    getNestedArrayElementField(Key const& key, size_t index, Key const& nestedKey) const;

    // One value (an SType marker, then that type's bytes) to place in a top-level STJson;
    // throws on an empty or malformed value
    static Value
    makeValueFromVLWithType(SerialIter& sit);

private:
    std::variant<Map, Array> data_{Map{}};
    bool default_{false};

    // Helper: validate nesting depth (max 1 level)
    void
    validateDepth(Value const& value, int currentDepth) const;

    // Read this STJson's bytes, it being nested `depth` containers below the top
    void
    parse(SerialIter& sit, int depth);

    // Read an entry's length-prefixed value inside a container at `depth`
    static Value
    parseEntryValue(SerialIter& sit, int depth);

    // Read a typed value inside a container at `depth` - 1
    static Value
    parseValue(SerialIter& sit, int depth);

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
