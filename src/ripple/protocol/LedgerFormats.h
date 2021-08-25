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

#include <ripple/protocol/KnownFormats.h>

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
    /** A ledger object which describes an account.

        \sa keylet::account
     */
    ltACCOUNT_ROOT = 0x0061,

    /** A ledger object which contains a list of object identifiers.

        \sa keylet::page, keylet::quality, keylet::book, keylet::next and
            keylet::ownerDir
     */
    ltDIR_NODE = 0x0064,

    /** A ledger object which describes a bidirectional trust line.

        @note Per Vinnie Falco this should be renamed to ltTRUST_LINE

        \sa keylet::line
     */
    ltRIPPLE_STATE = 0x0072,

    /** A ledger object which describes a ticket.

        \sa keylet::ticket
     */
    ltTICKET = 0x0054,

    /** A ledger object which contains a signer list for an account.

        \sa keylet::signers
     */
    ltSIGNER_LIST = 0x0053,

    /** A ledger object which describes an offer on the DEX.

        \sa keylet::offer
     */
    ltOFFER = 0x006f,

    /** A ledger object that contains a list of ledger hashes.

        This type is used to store the ledger hashes which the protocol uses
        to implement skip lists that allow for efficient backwards (and, in
        theory, forward) forward iteration across large ledger ranges.

        \sa keylet::skip
     */
    ltLEDGER_HASHES = 0x0068,

    /** The ledger object which lists details about amendments on the network.

        \note This is a singleton: only one such object exists in the ledger.

        \sa keylet::amendments
     */
    ltAMENDMENTS = 0x0066,

    /** The ledger object which lists the network's fee settings.

        \note This is a singleton: only one such object exists in the ledger.

        \sa keylet::fees
     */
    ltFEE_SETTINGS = 0x0073,

    /** A ledger object describing a single escrow.

        \sa keylet::escrow
     */
    ltESCROW = 0x0075,

    /** A ledger object describing a single unidirectional XRP payment channel.

        \sa keylet::payChan
     */
    ltPAYCHAN = 0x0078,

    /** A ledger object which describes a check.

        \sa keylet::check
     */
    ltCHECK = 0x0043,

    /** A ledger object which describes a deposit preauthorization.

        \sa keylet::depositPreauth
     */
    ltDEPOSIT_PREAUTH = 0x0070,

    /** The ledger object which tracks the current negative UNL state.

        \note This is a singleton: only one such object exists in the ledger.

        \sa keylet::negativeUNL
     */
    ltNEGATIVE_UNL = 0x004e,

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

    // ltSIGNER_LIST
    lsfOneOwnerCount = 0x00010000,  // True, uses only one OwnerCount
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
