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

namespace ripple {

/**
 * STJson: Serialized Type for arbitrary key-value pairs (JSON-like).
 * Keys are VL-encoded strings. Values are [SType marker][VL-encoded SType
 * serialization]. Values can be any SType, including nested STJson.
 */
class STJson : public STBase
{
public:
    using Key = std::string;
    using Value = std::shared_ptr<STBase>;
    using Map = std::map<Key, Value>;

    STJson() = default;

    explicit STJson(Map&& map);
    explicit STJson(SField const& name);
    explicit STJson(SerialIter& sit, SField const& name);

    SerializedTypeID
    getSType() const override;

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

    // Accessors
    Map const&
    getMap() const
    {
        return map_;
    }

    void
    set(Key const& key, Value const& value);

    std::optional<STJson::Value>
    get(Key const& key) const;

    void
    setNested(Key const& key, Key const& nestedKey, Value const& value);

    std::optional<Value>
    getNested(Key const& key, Key const& nestedKey) const;

    // Factory for SType value from blob (with SType marker)
    static Value
    makeValueFromVLWithType(SerialIter& sit);

    void
    setValue(STJson const& v);

private:
    Map map_;
    bool default_{false};

    // Helper: parse a single key-value pair from SerialIter
    static std::pair<Key, Value>
    parsePair(SerialIter& sit);

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

}  // namespace ripple

#endif
