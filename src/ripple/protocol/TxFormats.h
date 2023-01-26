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

#ifndef RIPPLE_PROTOCOL_TXFORMATS_H_INCLUDED
#define RIPPLE_PROTOCOL_TXFORMATS_H_INCLUDED

#include <ripple/protocol/KnownFormats.h>

namespace ripple {

/** Transaction type identifiers.

    These are part of the binary message format.

    @ingroup protocol
*/
/** Transaction type identifieers

    Each ledger object requires a unique type identifier, which is stored
    within the object itself; this makes it possible to iterate the entire
    ledger and determine each object's type and verify that the object you
    retrieved from a given hash matches the expected type.

    @warning Since these values are included in transactions, which are signed
             objects, and used by the code to determine the type of transaction
             being invoked, they are part of the protocol. **Changing them
             should be avoided because without special handling, this will
             result in a hard fork.**

    @note When retiring types, the specific values should not be removed but
          should be marked as [[deprecated]]. This is to avoid accidental
          reuse of identifiers.

    @todo The C++ language does not enable checking for duplicate values
          here. If it becomes possible then we should do this.

    @ingroup protocol
*/
// clang-format off
enum TxType : std::uint16_t
{
    /** This transaction type executes a payment. */
    ttPAYMENT = 0,

    /** This transaction type creates an escrow object. */
    ttESCROW_CREATE = 1,

    /** This transaction type completes an existing escrow. */
    ttESCROW_FINISH = 2,

    /** This transaction type adjusts various account settings. */
    ttACCOUNT_SET = 3,

    /** This transaction type cancels an existing escrow. */
    ttESCROW_CANCEL = 4,

    /** This transaction type sets or clears an account's "regular key". */
    ttREGULAR_KEY_SET = 5,

    /** This transaction type is deprecated; it is retained for historical purposes. */
    ttNICKNAME_SET [[deprecated("This transaction type is not supported and should not be used.")]] = 6,

    /** This transaction type creates an offer to trade one asset for another. */
    ttOFFER_CREATE = 7,

    /** This transaction type cancels existing offers to trade one asset for another. */
    ttOFFER_CANCEL = 8,

    /** This transaction type is deprecated; it is retained for historical purposes. */
    ttCONTRACT [[deprecated("This transaction type is not supported and should not be used.")]] = 9,

    /** This transaction type creates a new set of tickets. */
    ttTICKET_CREATE = 10,

    /** This identifier was never used, but the slot is reserved for historical purposes. */
    ttSPINAL_TAP [[deprecated("This transaction type is not supported and should not be used.")]] = 11,

    /** This transaction type modifies the signer list associated with an account. */
    ttSIGNER_LIST_SET = 12,

    /** This transaction type creates a new unidirectional XRP payment channel. */
    ttPAYCHAN_CREATE = 13,

    /** This transaction type funds an existing unidirectional XRP payment channel. */
    ttPAYCHAN_FUND = 14,

    /** This transaction type submits a claim against an existing unidirectional payment channel. */
    ttPAYCHAN_CLAIM = 15,

    /** This transaction type creates a new check. */
    ttCHECK_CREATE = 16,

    /** This transaction type cashes an existing check. */
    ttCHECK_CASH = 17,

    /** This transaction type cancels an existing check. */
    ttCHECK_CANCEL = 18,

    /** This transaction type grants or revokes authorization to transfer funds. */
    ttDEPOSIT_PREAUTH = 19,

    /** This transaction type modifies a trustline between two accounts. */
    ttTRUST_SET = 20,

    /** This transaction type deletes an existing account. */
    ttACCOUNT_DELETE = 21,

    /** This transaction type installs a hook. */
    ttHOOK_SET [[maybe_unused]] = 22,

    /** This transaction mints a new NFT. */
    ttNFTOKEN_MINT = 25,

    /** This transaction burns (i.e. destroys) an existing NFT. */
    ttNFTOKEN_BURN = 26,

    /** This transaction creates a new offer to buy or sell an NFT. */
    ttNFTOKEN_CREATE_OFFER = 27,

    /** This transaction cancels an existing offer to buy or sell an existing NFT. */
    ttNFTOKEN_CANCEL_OFFER = 28,

    /** This transaction accepts an existing offer to buy or sell an existing  NFT. */
    ttNFTOKEN_ACCEPT_OFFER = 29,

    /** This transaction mints/burns/buys/sells a URI TOKEN */
    ttURI_TOKEN = 45,

    /** This system-generated transaction type is used to update the status of the various amendments.

        For details, see: https://xrpl.org/amendments.html
     */
    ttAMENDMENT = 100,

    /** This system-generated transaction type is used to update the network's fee settings.

        For details, see: https://xrpl.org/fee-voting.html
     */
    ttFEE = 101,

    /** This system-generated transaction type is used to update the network's negative UNL

        For details, see: https://xrpl.org/negative-unl.html
     */
    ttUNL_MODIFY = 102,
};
// clang-format on

/** Manages the list of known transaction formats.
 */
class TxFormats : public KnownFormats<TxType, TxFormats>
{
private:
    /** Create the object.
        This will load the object with all the known transaction formats.
    */
    TxFormats();

public:
    static TxFormats const&
    getInstance();
};

}  // namespace ripple

#endif
