//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_IMPL_DETAILS_OPTIONUTILS_H_INCLUDED
#define RIPPLE_TX_IMPL_DETAILS_OPTIONUTILS_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpl/protocol/Option.h>

namespace ripple {

namespace option {

/**
 * @struct SealedOptionData
 * @brief Represents data for a sealed (matched) option agreement between two
 * parties.
 *
 * When an option is matched between two parties, this structure holds the
 * details of that agreement including the option ID, counterparty account,
 * quantity, and price.
 */
struct SealedOptionData
{
    uint256 offerID;    ///< Unique identifier for the offer
    AccountID account;  ///< Account ID of the counterparty
    std::uint32_t
        quantitySealed;  ///< Amount of the asset covered by this sealed option
    STAmount premium;    ///< Price paid for the option
};

/**
 * @brief Matches option offers on the ledger based on provided criteria.
 *
 * Searches through the option book to find matching offers that meet the
 * specified criteria including asset type, strike price, expiration, and option
 * type (put/call). For each match found, updates the open interest of the
 * matched offer and creates a sealed option relationship between the parties.
 *
 * @param sb Sandbox ledger view
 * @param issue The underlying asset (currency and issuer)
 * @param strike Strike price of the option (in the smallest units)
 * @param expiration Expiration time of the option as a Unix timestamp
 * @param isPut Whether this is a put option (true) or call option (false)
 * @param isSell Whether this is a sell order (true) or buy order (false)
 * @param desiredQuantity The quantity of options to match
 * @param account The account ID of the requester
 * @param optionIndex The index of the requester's option offer
 * @param isMarketOrder Whether this is a market order (true) or limit order
 * (false)
 * @param limitPrice The price limit for a limit order (ignored for market
 * orders)
 * @return std::vector<SealedOptionData> Vector of matched options with their
 * details
 */
std::vector<SealedOptionData>
matchOptions(
    Sandbox& sb,
    Issue issue,
    std::uint64_t strike,
    std::uint32_t expiration,
    bool isPut,
    bool isSell,
    std::uint32_t desiredQuantity,
    AccountID const& account,
    uint256 const& optionIndex,
    bool isMarketOrder = true,
    STAmount const& limitPrice = STAmount(0));

/**
 * @brief Creates a new option offer on the ledger.
 *
 * Sets up a new option offer with all required fields, including strike price,
 * premium, expiration date, and relationships to any sealed options. Adds the
 * option to the owner directory and the appropriate book directory.
 *
 * @param sb Sandbox ledger view
 * @param account The account creating the offer
 * @param optionOfferKeylet Keylet for the option offer
 * @param flags Option flags (including tfPut, tfSell)
 * @param quantity Total quantity of the option
 * @param openInterest Available quantity not yet matched
 * @param premium The premium (price) for the option
 * @param isSell Whether this is a sell offer
 * @param quantityShares Amount of the asset to lock as collateral (for sell
 * offers)
 * @param issue The underlying asset (currency and issuer)
 * @param strikePrice The strike price as an STAmount
 * @param strike The strike price as an integer
 * @param expiration Expiration time of the option
 * @param optionBookDirKeylet Keylet for the option book directory
 * @param sealedOptions Vector of sealed options to include with this offer
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
createOffer(
    Sandbox& sb,
    AccountID const& account,
    Keylet const& optionOfferKeylet,
    std::uint32_t flags,
    std::uint32_t quantity,
    std::uint32_t openInterest,
    STAmount const& premium,
    bool isSell,
    STAmount const& quantityShares,
    Issue const& issue,
    STAmount strikePrice,
    std::int64_t strike,
    std::uint32_t expiration,
    Keylet const& optionBookDirKeylet,
    std::vector<SealedOptionData> const& sealedOptions,
    beast::Journal j_);

/**
 * @brief Locks tokens as collateral for selling an option.
 *
 * When creating a sell option, this function locks the necessary assets as
 * collateral, either XRP or issued tokens. The locked amount is subtracted from
 * the account's available balance.
 *
 * @param sb Sandbox ledger view
 * @param sourceBalance Current XRP balance of the account
 * @param account Account whose tokens will be locked
 * @param quantityShares Amount to lock
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
lockTokens(
    Sandbox& sb,
    XRPAmount const& sourceBalance,
    AccountID const& account,
    STAmount const& quantityShares,
    beast::Journal j_);

/**
 * @brief Unlocks tokens that were previously locked as collateral.
 *
 * When an option is closed, exercised, or expires, this function releases
 * the locked collateral back to the specified account.
 *
 * @param sb Sandbox ledger view
 * @param receiver Account receiving the unlocked tokens
 * @param sleReceiver SLE of the receiving account
 * @param quantityShares Amount to unlock
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
unlockTokens(
    Sandbox& sb,
    AccountID const& receiver,
    std::shared_ptr<SLE> const& sleReceiver,
    STAmount const& quantityShares,
    beast::Journal j_);

/**
 * @brief Transfers tokens from one account to another.
 *
 * Used for premium payments and settlements when options are exercised.
 * Handles both XRP and issued currencies.
 *
 * @param sb Sandbox ledger view
 * @param sender Account sending tokens
 * @param receiver Account receiving tokens
 * @param amount Amount to transfer
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
transferTokens(
    Sandbox& sb,
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    beast::Journal j_);

/**
 * @brief Closes an existing option offer.
 *
 * Verifies ownership, unlocks any collateral, and handles relationships with
 * sealed options. If necessary, finds replacement offers for counterparties
 * to maintain their positions.
 *
 * @param sb Sandbox ledger view
 * @param account Account closing the offer
 * @param offerKeylet Keylet of the offer to close
 * @param isPut Whether this is a put option
 * @param isSell Whether this is a sell offer
 * @param issue The underlying asset
 * @param strike The strike price
 * @param expiration The expiration time
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
closeOffer(
    Sandbox& sb,
    AccountID const& account,
    Keylet const& offerKeylet,
    bool isPut,
    bool isSell,
    Issue const& issue,
    std::uint64_t const& strike,
    std::uint32_t const& expiration,
    beast::Journal j);

/**
 * @brief Exercises an option contract.
 *
 * Executes the option by transferring assets between buyer and seller
 * according to the option terms. Updates or removes the option from the ledger.
 *
 * @param sb Sandbox ledger view
 * @param isPut Whether this is a put option
 * @param strikePrice The strike price
 * @param buyer Account exercising the option
 * @param sleBuyer SLE of the buyer's account
 * @param issue The underlying asset
 * @param sealedOptions Array of sealed options to exercise
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
exerciseOffer(
    Sandbox& sb,
    bool isPut,
    STAmount const& strikePrice,
    AccountID const& buyer,
    std::shared_ptr<SLE> const& sleBuyer,
    Issue const& issue,
    STArray const& sealedOptions,
    beast::Journal j_);

/**
 * @brief Handles expiration of an option offer.
 *
 * Called when an option expires. Returns collateral to the seller,
 * updates counterparty references, and removes the option from the ledger.
 *
 * @param view Ledger view
 * @param sle SLE of the expired option
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
expireOffer(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);

/**
 * @brief Removes an option offer from the ledger.
 *
 * Removes the option from owner and book directories, adjusts the owner count,
 * and erases the ledger entry.
 *
 * @param view Ledger view
 * @param sle SLE of the option to delete
 * @param j Journal for logging
 * @return TER Transaction result code
 */
TER
deleteOffer(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);

}  // namespace option
}  // namespace ripple

#endif  // RIPPLE_TX_IMPL_DETAILS_OPTIONUTILS_H_INCLUDED