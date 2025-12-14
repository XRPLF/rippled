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

#define LEDGER_ENTRY(tag, value, ...) tag = value,

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

// define an X-MACRO format for the ledger-specific-flags.
// This common macro-definition can be used to declare an enum and a std::unordered_map for these flags
#define LIST_OF_LSF_FLAGS(X, START_SECTION_XMACRO, END_SECTION_XMACRO) \
    /* ltACCOUNT_ROOT */ \
    START_SECTION_XMACRO(ltACCOUNT_ROOT) \
        X(lsfPasswordSpent, 0x00010000) /* True, if password set fee is spent. */ \
        X(lsfRequireDestTag, 0x00020000) /* True, to require a DestinationTag for payments. */ \
        X(lsfRequireAuth, 0x00040000) /* True, to require a authorization to hold IOUs. */ \
        X(lsfDisallowXRP, 0x00080000) /* True, to disallow sending XRP. */ \
        X(lsfDisableMaster, 0x00100000) /* True, force regular key */ \
        X(lsfNoFreeze, 0x00200000) /* True, cannot freeze ripple states */ \
        X(lsfGlobalFreeze, 0x00400000) /* True, all assets frozen */ \
        X(lsfDefaultRipple, 0x00800000) /* True, incoming trust lines allow rippling by default */ \
        X(lsfDepositAuth, 0x01000000) /* True, all deposits require authorization */ \
        /*  // reserved for Hooks amendment
        X(lsfTshCollect = 0x02000000)     // True, allow TSH collect-calls to acc hooks
        */ \
        X(lsfDisallowIncomingNFTokenOffer, 0x04000000) /* True, reject new incoming NFT offers */ \
        X(lsfDisallowIncomingCheck, 0x08000000) /* True, reject new checks */ \
        X(lsfDisallowIncomingPayChan, 0x10000000) /* True, reject new paychans */ \
        X(lsfDisallowIncomingTrustline, 0x20000000) /* True, reject new trustlines (only if no issued assets) */ \
        X(lsfAllowTrustLineLocking, 0x40000000) /* True, enable trustline locking */ \
        X(lsfAllowTrustLineClawback, 0x80000000) /* True, enable clawback */ \
    END_SECTION_XMACRO \
    \
    /* OFFER */ \
    START_SECTION_XMACRO(ltOFFER) \
        X(lsfPassive, 0x00010000) \
        X(lsfSell, 0x00020000) /* True, offer was placed as a sell. */ \
        X(lsfHybrid, 0x00040000) /* True, offer is hybrid. */ \
    END_SECTION_XMACRO \
    \
    /* ltRIPPLE_STATE */ \
    START_SECTION_XMACRO(ltRIPPLE_STATE) \
        X(lsfLowReserve, 0x00010000) /* True, if entry counts toward reserve. */ \
        X(lsfHighReserve, 0x00020000) \
        X(lsfLowAuth, 0x00040000) \
        X(lsfHighAuth, 0x00080000) \
        X(lsfLowNoRipple, 0x00100000) \
        X(lsfHighNoRipple, 0x00200000) \
        X(lsfLowFreeze, 0x00400000) /* True, low side has set freeze flag */ \
        X(lsfHighFreeze, 0x00800000) /* True, high side has set freeze flag */ \
        X(lsfLowDeepFreeze, 0x02000000) /* True, low side has set deep freeze flag */ \
        X(lsfHighDeepFreeze, 0x04000000) /* True, high side has set deep freeze flag */ \
        X(lsfAMMNode, 0x01000000) /* True, trust line to AMM. Used by client apps to identify payments via AMM. */ \
    END_SECTION_XMACRO \
    \
    /* ltSIGNER_LIST */ \
    START_SECTION_XMACRO(ltSIGNER_LIST) \
        X(lsfOneOwnerCount, 0x00010000) /* True, uses only one OwnerCount */ \
    END_SECTION_XMACRO \
    \
    /* ltDIR_NODE */ \
    START_SECTION_XMACRO(ltDIR_NODE) \
        X(lsfNFTokenBuyOffers, 0x00000001) \
        X(lsfNFTokenSellOffers, 0x00000002) \
    END_SECTION_XMACRO \
    \
    /* ltNFTOKEN_OFFER */ \
    START_SECTION_XMACRO(ltNFTOKEN_OFFER) \
        X(lsfSellNFToken, 0x00000001) \
    END_SECTION_XMACRO \
    \
    /* ltMPTOKEN_ISSUANCE */ \
    START_SECTION_XMACRO(ltMPTOKEN_ISSUANCE) \
        X(lsfMPTLocked, 0x00000001) /* Also used in ltMPTOKEN */ \
        X(lsfMPTCanLock, 0x00000002) \
        X(lsfMPTRequireAuth, 0x00000004) \
        X(lsfMPTCanEscrow, 0x00000008) \
        X(lsfMPTCanTrade, 0x00000010) \
        X(lsfMPTCanTransfer, 0x00000020) \
        X(lsfMPTCanClawback, 0x00000040) \
        \
        X(lsmfMPTCanMutateCanLock, 0x00000002) \
        X(lsmfMPTCanMutateRequireAuth, 0x00000004) \
        X(lsmfMPTCanMutateCanEscrow, 0x00000008) \
        X(lsmfMPTCanMutateCanTrade, 0x00000010) \
        X(lsmfMPTCanMutateCanTransfer, 0x00000020) \
        X(lsmfMPTCanMutateCanClawback, 0x00000040) \
        X(lsmfMPTCanMutateMetadata, 0x00010000) \
        X(lsmfMPTCanMutateTransferFee, 0x00020000) \
    END_SECTION_XMACRO \
    \
    /* ltMPTOKEN */ \
    START_SECTION_XMACRO(ltMPTOKEN) \
        X(lsfMPTAuthorized, 0x00000002) \
    END_SECTION_XMACRO \
    \
    /* ltCREDENTIAL */ \
    START_SECTION_XMACRO(ltCREDENTIAL) \
        X(lsfAccepted, 0x00010000) \
    END_SECTION_XMACRO \
    \
    /* ltVAULT */ \
    START_SECTION_XMACRO(ltVAULT) \
        X(lsfVaultPrivate, 0x00010000) \
    END_SECTION_XMACRO \
    \
    /* ltLOAN */ \
    START_SECTION_XMACRO(ltLOAN) \
        X(lsfLoanDefault, 0x00010000) \
        X(lsfLoanImpaired, 0x00020000) \
        X(lsfLoanOverpayment, 0x00040000) /* True, loan allows overpayments */ \
    END_SECTION_XMACRO \

#define EXPAND_FLAG_VALUES(flagName, flagValue) {#flagName, flagValue},
#define START_LE_MAP(ledgerObjectName) {#ledgerObjectName, std::unordered_map<std::string, unsigned int>{
#define END_LE_MAP },},
std::unordered_map<std::string, std::unordered_map<std::string, unsigned int>> const LSFlags = { LIST_OF_LSF_FLAGS(EXPAND_FLAG_VALUES, START_LE_MAP, END_LE_MAP) };
#undef END_LE_MAP
#undef START_LE_MAP
#undef EXPAND_FLAG_VALUES

// create the enum LedgerSpecificFlags
/**
    @ingroup protocol
*/
#define START_ENUM_SECTION_NOOP(ledgerObjectName)
#define END_ENUM_SECION_NOOP
#define ENUM_FLAG_DEFINITION(flagName, flagValue) flagName = flagValue,
enum LedgerSpecificFlags
{
    LIST_OF_LSF_FLAGS(ENUM_FLAG_DEFINITION, START_ENUM_SECTION_NOOP, END_ENUM_SECION_NOOP)
};
#undef ENUM_FLAG_DEFINITION
#undef END_ENUM_SECION_NOOP
#undef START_ENUM_SECTION_NOOP

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
