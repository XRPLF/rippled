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

#ifndef RIPPLE_LEDGERFORMATS_H_INCLUDED
#define RIPPLE_LEDGERFORMATS_H_INCLUDED

namespace ripple {

/** Ledger entry types.

    These are stored in serialized data.

    @note Changing these values results in a hard fork.

    @ingroup protocol
*/
// Used as the type of a transaction or the type of a ledger entry.
enum LedgerEntryType
{
    ltINVALID           = -1,

    ltACCOUNT_ROOT      = 'a',

    /** Directory node.

        A directory is a vector 256-bit values. Usually they represent
        hashes of other objects in the ledger.

        Used in an append-only fashion.

        (There's a little more information than this, see the template)
    */
    ltDIR_NODE          = 'd',

    ltGENERATOR_MAP     = 'g',

    /** Describes a trust line.
    */
    // VFALCO TODO Rename to TrustLine or something similar.
    ltRIPPLE_STATE      = 'r',

    /** Deprecated.
    */
    ltNICKNAME          = 'n',

    ltOFFER             = 'o',

    ltCONTRACT          = 'c',

    ltLEDGER_HASHES     = 'h',

    ltAMENDMENTS        = 'f',

    ltFEE_SETTINGS      = 's',
};

/**
    @ingroup protocol
*/
// Used as a prefix for computing ledger indexes (keys).
// VFALCO TODO Why are there a separate set of prefixes? i.e. class HashPrefix
enum LedgerNameSpace
{
    spaceAccount        = 'a',
    spaceDirNode        = 'd',
    spaceGenerator      = 'g',
    spaceNickname       = 'n',
    spaceRipple         = 'r',
    spaceOffer          = 'o',  // Entry for an offer.
    spaceOwnerDir       = 'O',  // Directory of things owned by an account.
    spaceBookDir        = 'B',  // Directory of order books.
    spaceContract       = 'c',
    spaceSkipList       = 's',
    spaceAmendment      = 'f',
    spaceFee            = 'e',
};

/**
    @ingroup protocol
*/
enum LedgerSpecificFlags
{
    // ltACCOUNT_ROOT
    lsfPasswordSpent    = 0x00010000,   // True, if password set fee is spent.
    lsfRequireDestTag   = 0x00020000,   // True, to require a DestinationTag for payments.
    lsfRequireAuth      = 0x00040000,   // True, to require a authorization to hold IOUs.
    lsfDisallowXRP      = 0x00080000,   // True, to disallow sending XRP.
    lsfDisableMaster    = 0x00100000,   // True, force regular key

    // ltOFFER
    lsfPassive          = 0x00010000,
    lsfSell             = 0x00020000,   // True, offer was placed as a sell.

    // ltRIPPLE_STATE
    lsfLowReserve       = 0x00010000,   // True, if entry counts toward reserve.
    lsfHighReserve      = 0x00020000,
    lsfLowAuth          = 0x00040000,
    lsfHighAuth         = 0x00080000,
    lsfLowNoRipple      = 0x00100000,
    lsfHighNoRipple     = 0x00200000,
};

//------------------------------------------------------------------------------

/** Holds the list of known ledger entry formats.
*/
class LedgerFormats : public KnownFormats <LedgerEntryType>
{
public:
    LedgerFormats ();

    static LedgerFormats* getInstance ();

private:
    void addCommonFields (Item& item);
};

} // ripple

#endif
