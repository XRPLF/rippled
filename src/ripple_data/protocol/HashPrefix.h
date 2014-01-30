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
// VFALCO NOTE there are ledger entry prefixes too but they are only
//             1 byte, find out why they are different. Maybe we should
//             group them all together?
//
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
};

#endif
