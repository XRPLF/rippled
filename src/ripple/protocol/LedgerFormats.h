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

/** Ledger entry types.

    These are stored in serialized data.

    @note Changing these values results in a hard fork.

    @ingroup protocol
*/
// Used as the type of a transaction or the type of a ledger entry.
enum LedgerEntryType {
    /** Special type, anything
        This is used when the type in the Keylet is unknown,
        such as when building metadata.
    */
    ltANY = -3,

    /** Special type, anything not a directory
        This is used when the type in the Keylet is unknown,
        such as when iterating
    */
    ltCHILD = -2,

    ltINVALID = -1,

    //---------------------------------------------------------------------------

    ltACCOUNT_ROOT = 'a',

    /** Directory node.

        A directory is a vector 256-bit values. Usually they represent
        hashes of other objects in the ledger.

        Used in an append-only fashion.

        (There's a little more information than this, see the template)
    */
    ltDIR_NODE = 'd',

    ltRIPPLE_STATE = 'r',

    ltTICKET = 'T',

    ltSIGNER_LIST = 'S',

    ltOFFER = 'o',

    ltLEDGER_HASHES = 'h',

    ltAMENDMENTS = 'f',

    ltFEE_SETTINGS = 's',

    ltESCROW = 'u',

    // Simple unidirection xrp channel
    ltPAYCHAN = 'x',

    ltCHECK = 'C',

    ltDEPOSIT_PREAUTH = 'p',

    // No longer used or supported. Left here to prevent accidental
    // reassignment of the ledger type.
    ltNICKNAME [[deprecated]] = 'n',

    ltNotUsed01 [[deprecated]] = 'c',
};

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
class LedgerFormats : public KnownFormats<LedgerEntryType>
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
