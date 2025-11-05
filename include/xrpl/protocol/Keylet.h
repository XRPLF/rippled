#ifndef XRPL_PROTOCOL_KEYLET_H_INCLUDED
#define XRPL_PROTOCOL_KEYLET_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace ripple {

class STLedgerEntry;

/** A pair of SHAMap key and LedgerEntryType.

    A Keylet identifies both a key in the state map
    and its ledger entry type.

    @note Keylet is a portmanteau of the words key
          and LET, an acronym for LedgerEntryType.
*/
struct Keylet
{
    uint256 key;
    LedgerEntryType type;

    Keylet(LedgerEntryType type_, uint256 const& key_) : key(key_), type(type_)
    {
    }

    /** Returns true if the SLE matches the type */
    bool
    check(STLedgerEntry const&) const;
};

}  // namespace ripple

#endif
