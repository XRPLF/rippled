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

#ifndef RIPPLE_PROTOCOL_LEDGERFORMATS_H_INCLUDED
#define RIPPLE_PROTOCOL_LEDGERFORMATS_H_INCLUDED

#include <xrpl/protocol/KnownFormats.h>

namespace ripple {

/** Identifiers for on-ledger objects.

    Each ledger object requires a unique type identifier, which is stored
    within the object itself; this makes it possible to iterate the entire
    ledger and determine each object's type and verify that the object you
    retrieved from a given hash matches the expected type.

    @warning Since these values are stored inside objects stored on the ledger
             they are part of the protocol. **Changing them should be avoided
             because without special handling, this will result in a hard
   fork.**

    @note Values outside this range may be used internally by the code for
          various purposes, but attempting to use such values to identify
          on-ledger objects will results in an invariant failure.

    @note When retiring types, the specific values should not be removed but
          should be marked as [[deprecated]]. This is to avoid accidental
          reuse of identifiers.

    @todo The C++ language does not enable checking for duplicate values
          here. If it becomes possible then we should do this.

    @ingroup protocol
*/
// clang-format off
enum LedgerEntryType : std::uint16_t
{

#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, fields) tag = value,

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")

    //---------------------------------------------------------------------------
    /** A special type, matching any ledger entry type.

        The value does not represent a concrete type, but rather is used in
        contexts where the specific type of a ledger object is unimportant,
        unknown or unavailable.

        Objects with this special type cannot be created or stored on the
        ledger.

        \sa keylet::unchecked
    */
    ltANY = 0,

    /** A special type, matching any ledger type except directory nodes.

        The value does not represent a concrete type, but rather is used in
        contexts where the ledger object must not be a directory node but
        its specific type is otherwise unimportant, unknown or unavailable.

        Objects with this special type cannot be created or stored on the
        ledger.

        \sa keylet::child
     */
    ltCHILD = 0x1CD2,

    //---------------------------------------------------------------------------
    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    ltNICKNAME [[deprecated("This object type is not supported and should not be used.")]] = 0x006e,

    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    ltCONTRACT [[deprecated("This object type is not supported and should not be used.")]] = 0x0063,

    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    ltGENERATOR_MAP [[deprecated("This object type is not supported and should not be used.")]]  = 0x0067,
};
// clang-format off

/**
    @ingroup protocol
*/
enum LedgerSpecificFlags {
    // ltACCOUNT_ROOT
    lsfPasswordSpent = 0x00010000,  // True, if password set fee is spent.
    lsfRequireDestTag =
        0x00020000,  // True, to require a DestinationTag for payments.
    lsfRequireAuth =
        0x00040000,  // True, to require a authorization to hold IOUs.
    lsfDisallowXRP = 0x00080000,    // True, to disallow sending XRP.
    lsfDisableMaster = 0x00100000,  // True, force regular key
    lsfNoFreeze = 0x00200000,       // True, cannot freeze ripple states
    lsfGlobalFreeze = 0x00400000,   // True, all assets frozen
    lsfDefaultRipple =
        0x00800000,               // True, trust lines allow rippling by default
    lsfDepositAuth = 0x01000000,  // True, all deposits require authorization
/*  // reserved for Hooks amendment
    lsfTshCollect = 0x02000000,     // True, allow TSH collect-calls to acc hooks
*/
    lsfDisallowIncomingNFTokenOffer =
        0x04000000,               // True, reject new incoming NFT offers
    lsfDisallowIncomingCheck =
        0x08000000,               // True, reject new checks
    lsfDisallowIncomingPayChan =
        0x10000000,               // True, reject new paychans
    lsfDisallowIncomingTrustline =
        0x20000000,               // True, reject new trustlines (only if no issued assets)
    lsfAllowTokenLocking =
        0x40000000,               // True, enable token locking
    lsfAllowTrustLineClawback =
        0x80000000,               // True, enable clawback

    // ltOFFER
    lsfPassive = 0x00010000,
    lsfSell = 0x00020000,  // True, offer was placed as a sell.

    // ltRIPPLE_STATE
    lsfLowReserve = 0x00010000,  // True, if entry counts toward reserve.
    lsfHighReserve = 0x00020000,
    lsfLowAuth = 0x00040000,
    lsfHighAuth = 0x00080000,
    lsfLowNoRipple = 0x00100000,
    lsfHighNoRipple = 0x00200000,
    lsfLowFreeze = 0x00400000,   // True, low side has set freeze flag
    lsfHighFreeze = 0x00800000,  // True, high side has set freeze flag
    lsfAMMNode = 0x01000000,     // True, trust line to AMM. Used by client
                                 // apps to identify payments via AMM.

    // ltSIGNER_LIST
    lsfOneOwnerCount = 0x00010000,  // True, uses only one OwnerCount

    // ltDIR_NODE
    lsfNFTokenBuyOffers = 0x00000001,
    lsfNFTokenSellOffers = 0x00000002,

    // ltNFTOKEN_OFFER
    lsfSellNFToken = 0x00000001,

    // ltMPTOKEN_ISSUANCE
    lsfMPTLocked = 0x00000001, // Also used in ltMPTOKEN
    lsfMPTCanLock = 0x00000002,
    lsfMPTRequireAuth = 0x00000004,
    lsfMPTCanEscrow = 0x00000008,
    lsfMPTCanTrade = 0x00000010,
    lsfMPTCanTransfer = 0x00000020,
    lsfMPTCanClawback = 0x00000040,

    // ltMPTOKEN
    lsfMPTAuthorized = 0x00000002,

    // ltCREDENTIAL
    lsfAccepted = 0x00010000,
};

//------------------------------------------------------------------------------

/** Holds the list of known ledger entry formats.
 */
class LedgerFormats : public KnownFormats<LedgerEntryType, LedgerFormats>
{
private:
    /** Create the object.
        This will load the object with all the known ledger formats.
    */
    LedgerFormats();

public:
    static LedgerFormats const&
    getInstance();
};

}  // namespace ripple

#endif
