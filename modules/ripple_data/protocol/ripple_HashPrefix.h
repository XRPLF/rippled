//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HASHPREFIX_H
#define RIPPLE_HASHPREFIX_H

/** Prefix for hashing functions.

    These prefixes are inserted before the source material used to
    generate various hashes. This is done to put each hash in its own
    "space." This way, two different types of objects with the
    same binary data will produce different hashes.

    Each prefix is a 4-byte value with the last byte set to zero
    and the first three bytes formed from the ASCII equivalent of
    some arbitrary string. For example "TXN".

    @note Hash prefixes are part of the Ripple protocol.

    @ingroup protocol
*/
struct HashPrefix
{
    // VFALCO TODO Make these Doxygen comments and expand the
    //             description to complete, concise sentences.
    //

    // transaction plus signature to give transaction ID
    static uint32 const transactionID       = 0x54584E00; // 'TXN'

    // transaction plus metadata
    static uint32 const txNode              = 0x534E4400; // 'TND'

    // account state
    static uint32 const leafNode            = 0x4D4C4E00; // 'MLN'

    // inner node in tree
    static uint32 const innerNode           = 0x4D494E00; // 'MIN'

    // ledger master data for signing
    static uint32 const ledgerMaster        = 0x4C575200; // 'LGR'

    // inner transaction to sign
    static uint32 const txSign              = 0x53545800; // 'STX'

    // validation for signing
    static uint32 const validation          = 0x56414C00; // 'VAL'

    // proposal for signing
    static uint32 const proposal            = 0x50525000; // 'PRP'

    // inner transaction to sign (TESTNET)
    static uint32 const txSignTestnet       = 0x73747800; // 'stx'

    // validation for signing (TESTNET)
    static uint32 const validationTestnet   = 0x76616C00; // 'val'

    // proposal for signing (TESTNET)
    static uint32 const proposalTestnet     = 0x70727000; // 'prp'
};

#endif
