//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LEDGERFORMAT_H
#define RIPPLE_LEDGERFORMAT_H

/**

    These are stored in serialized data.

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

    ltFEATURES          = 'f',

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
    spaceFeature        = 'f',
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
    lsfDisableMaster	= 0x00100000,	// True, force regular key

    // ltOFFER
    lsfPassive          = 0x00010000,
    lsfSell             = 0x00020000,   // True, offer was placed as a sell.

    // ltRIPPLE_STATE
    lsfLowReserve       = 0x00010000,   // True, if entry counts toward reserve.
    lsfHighReserve      = 0x00020000,
    lsfLowAuth          = 0x00040000,
    lsfHighAuth         = 0x00080000,
};

// VFALCO TODO See if we can merge LedgerEntryFormat with TxFormats
//
class LedgerEntryFormat
{
public:
    std::string                 t_name;
    LedgerEntryType             t_type;
    SOTemplate                  elements;

    static std::map<int, LedgerEntryFormat*>            byType;
    static std::map<std::string, LedgerEntryFormat*>    byName;

    LedgerEntryFormat (const char* name, LedgerEntryType type) : t_name (name), t_type (type)
    {
        byName[name] = this;
        byType[type] = this;
    }
    LedgerEntryFormat& operator<< (const SOElement& el)
    {
        elements.push_back (el);
        return *this;
    }

    static LedgerEntryFormat* getLgrFormat (LedgerEntryType t);
    static LedgerEntryFormat* getLgrFormat (const std::string& t);
    static LedgerEntryFormat* getLgrFormat (int t);
};

#endif
// vim:ts=4
