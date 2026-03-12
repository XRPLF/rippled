/**
 * @file TrustLine.h
 * @brief Wrapper for trust line (RippleState) ledger entries.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/RippleState.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

/**
 * @brief Describes how an account was found in a path.
 *
 * "Outgoing" is defined as the source account, or an account found via a
 * trust line that has rippling enabled on the account's side.
 *
 * "Incoming" is defined as an account found via a trust line that has rippling
 * disabled on the account's side. Any trust lines for an incoming account that
 * have rippling disabled are unusable in paths.
 */
enum class LineDirection : bool { incoming = false, outgoing = true };

/**
 * @brief Wraps a trust line SLE for convenience.
 *
 * The complication of trust lines is that there is a "low" account and a
 * "high" account. This wraps the SLE and expresses its data from the
 * perspective of a chosen account on the line.
 *
 * This class uses a thin wrapper around the SLE to minimise memory usage.
 * The SLE must remain valid for the lifetime of this object.
 */
class TrustLine final : public CountedObject<TrustLine>
{
public:
    TrustLine() = delete;

    /**
     * @brief Construct a TrustLine from a RippleState ledger entry.
     * @param rippleState The RippleState ledger entry.
     * @param viewAccount The account from whose perspective to view the line.
     */
    TrustLine(ledger_entries::RippleState const& rippleState, AccountID const& viewAccount);

    /**
     * @brief Create a TrustLine from an SLE if it is a valid RippleState.
     * @param accountID The account from whose perspective to view the line.
     * @param sle The shared ledger entry.
     * @return The TrustLine, or nullopt if the SLE is not a RippleState.
     */
    static std::optional<TrustLine>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    /**
     * @brief Get all trust lines for an account.
     * @param accountID The account to get trust lines for.
     * @param view The ledger view to read from.
     * @param direction Filter by line direction (default: outgoing).
     * @return A vector of TrustLine objects.
     */
    static std::vector<TrustLine>
    getItems(
        AccountID const& accountID,
        ReadView const& view,
        LineDirection direction = LineDirection::outgoing);

    /**
     * @brief Returns the state map key for the ledger entry.
     * @return The key (hash) of the RippleState entry.
     */
    uint256 const&
    key() const;

    /**
     * @brief Get our account ID.
     * @return The account ID from whose perspective this line is viewed.
     */
    AccountID const&
    getAccountID() const;

    /**
     * @brief Get the peer's account ID.
     * @return The account ID of the other party on this trust line.
     */
    AccountID const&
    getAccountIDPeer() const;

    /**
     * @brief Check if we have authorised the peer.
     * @return True if we have provided authorisation to the peer.
     */
    bool
    getAuth() const;

    /**
     * @brief Check if the peer has authorised us.
     * @return True if the peer has provided authorisation to us.
     */
    bool
    getAuthPeer() const;

    /**
     * @brief Check if we have the NoRipple flag set.
     * @return True if NoRipple is set on our side.
     */
    bool
    getNoRipple() const;

    /**
     * @brief Check if the peer has the NoRipple flag set.
     * @return True if NoRipple is set on the peer's side.
     */
    bool
    getNoRipplePeer() const;

    /**
     * @brief Get our line direction based on NoRipple flag.
     * @return Incoming if NoRipple is set, outgoing otherwise.
     */
    LineDirection
    getDirection() const
    {
        return getNoRipple() ? LineDirection::incoming : LineDirection::outgoing;
    }

    /**
     * @brief Get the peer's line direction based on their NoRipple flag.
     * @return Incoming if peer's NoRipple is set, outgoing otherwise.
     */
    LineDirection
    getDirectionPeer() const
    {
        return getNoRipplePeer() ? LineDirection::incoming : LineDirection::outgoing;
    }

    /**
     * @brief Check if we have frozen the peer.
     * @return True if we have set the freeze flag on the peer.
     */
    bool
    getFreeze() const;

    /**
     * @brief Check if we have deep frozen the peer.
     * @return True if we have set the deep freeze flag on the peer.
     */
    bool
    getDeepFreeze() const;

    /**
     * @brief Check if the peer has frozen us.
     * @return True if the peer has set the freeze flag on us.
     */
    bool
    getFreezePeer() const;

    /**
     * @brief Check if the peer has deep frozen us.
     * @return True if the peer has set the deep freeze flag on us.
     */
    bool
    getDeepFreezePeer() const;

    /**
     * @brief Get the balance on this trust line.
     * @return The balance from our perspective (positive = we hold IOUs).
     */
    STAmount
    getBalance() const;

    /**
     * @brief Get our trust limit.
     * @return The maximum amount we are willing to hold.
     */
    STAmount
    getLimit() const;

    /**
     * @brief Get the peer's trust limit.
     * @return The maximum amount the peer is willing to hold.
     */
    STAmount
    getLimitPeer() const;

    /**
     * @brief Get the quality in rate.
     * @return The rate applied to incoming payments.
     */
    Rate
    getQualityIn() const;

    /**
     * @brief Get the quality out rate.
     * @return The rate applied to outgoing payments.
     */
    Rate
    getQualityOut() const;

    /**
     * @brief Get a JSON representation of this trust line.
     * @param level Detail level for the JSON output.
     * @return JSON value representing this trust line.
     */
    Json::Value
    getJson(int level);

private:
    TrustLine(std::shared_ptr<SLE const> sle, AccountID const& viewAccount);

    ledger_entries::RippleState rippleState_; /**< The underlying RippleState entry. */
    bool viewLowest_;                         /**< True if viewAccount is the low account. */
};

}  // namespace xrpl
