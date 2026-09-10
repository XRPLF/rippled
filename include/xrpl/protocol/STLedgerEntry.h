#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace xrpl {

class Rules;
namespace test {
class InvariantsMisc_test;
}  // namespace test

class STLedgerEntry final : public STObject, public CountedObject<STLedgerEntry>
{
    UInt256 key_;
    LedgerEntryType type_;

public:
    using pointer = std::shared_ptr<STLedgerEntry>;
    using Ref = std::shared_ptr<STLedgerEntry> const&;
    using const_pointer = std::shared_ptr<STLedgerEntry const>;
    using ConstRef = std::shared_ptr<STLedgerEntry const> const&;

    /**
     * Create an empty object with the given key and type.
     */
    explicit STLedgerEntry(Keylet const& k);
    STLedgerEntry(LedgerEntryType type, UInt256 const& key);
    STLedgerEntry(SerialIter& sit, UInt256 const& index);
    STLedgerEntry(SerialIter&& sit, UInt256 const& index);
    STLedgerEntry(STObject const& object, UInt256 const& index);

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    [[nodiscard]] std::string
    getFullText() const override;

    [[nodiscard]] std::string
    getText() const override;

    [[nodiscard]] json::Value
    getJson(JsonOptions options = JsonOptions::Values::None) const override;

    /**
     * Returns the 'key' (or 'index') of this item.
     * The key identifies this entry's position in
     * the SHAMap associative container.
     */
    [[nodiscard]] UInt256 const&
    key() const;

    [[nodiscard]] LedgerEntryType
    getType() const;

    // is this a ledger entry that can be threaded
    [[nodiscard]] bool
    isThreadedType(Rules const& rules) const;

    bool
    thread(
        UInt256 const& txID,
        std::uint32_t ledgerSeq,
        UInt256& prevTxID,
        std::uint32_t& prevLedgerID);

private:
    /*  Make STObject comply with the template for this SLE type
        Can throw
    */
    void
    setSLEType();

    friend test::InvariantsMisc_test;  // this test wants access to the
                                       // private type_

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

using SLE = STLedgerEntry;

inline STLedgerEntry::STLedgerEntry(LedgerEntryType type, UInt256 const& key)
    : STLedgerEntry(Keylet(type, key))
{
}

inline STLedgerEntry::STLedgerEntry(
    SerialIter&& sit,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    UInt256 const& index)
    : STLedgerEntry(sit, index)
{
}

/**
 * Returns the 'key' (or 'index') of this item.
 * The key identifies this entry's position in
 * the SHAMap associative container.
 */
inline UInt256 const&
STLedgerEntry::key() const
{
    return key_;
}

inline LedgerEntryType
STLedgerEntry::getType() const
{
    return type_;
}

}  // namespace xrpl
