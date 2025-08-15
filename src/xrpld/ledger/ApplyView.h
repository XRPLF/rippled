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

#ifndef RIPPLE_LEDGER_APPLYVIEW_H_INCLUDED
#define RIPPLE_LEDGER_APPLYVIEW_H_INCLUDED

#include <xrpld/ledger/RawView.h>
#include <xrpld/ledger/ReadView.h>

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>

namespace ripple {

enum ApplyFlags : std::uint32_t {
    tapNONE = 0x00,

    // This is a local transaction with the
    // fail_hard flag set.
    tapFAIL_HARD = 0x10,

    // This is not the transaction's last pass
    // Transaction can be retried, soft failures allowed
    tapRETRY = 0x20,

    // Transaction came from a privileged source
    tapUNLIMITED = 0x400,

    // Transaction is executing as part of a batch
    tapBATCH = 0x800,

    // Transaction shouldn't be applied
    // Signatures shouldn't be checked
    tapDRY_RUN = 0x1000
};

constexpr ApplyFlags
operator|(ApplyFlags const& lhs, ApplyFlags const& rhs)
{
    return safe_cast<ApplyFlags>(
        safe_cast<std::underlying_type_t<ApplyFlags>>(lhs) |
        safe_cast<std::underlying_type_t<ApplyFlags>>(rhs));
}

static_assert(
    (tapFAIL_HARD | tapRETRY) == safe_cast<ApplyFlags>(0x30u),
    "ApplyFlags operator |");
static_assert(
    (tapRETRY | tapFAIL_HARD) == safe_cast<ApplyFlags>(0x30u),
    "ApplyFlags operator |");

constexpr ApplyFlags
operator&(ApplyFlags const& lhs, ApplyFlags const& rhs)
{
    return safe_cast<ApplyFlags>(
        safe_cast<std::underlying_type_t<ApplyFlags>>(lhs) &
        safe_cast<std::underlying_type_t<ApplyFlags>>(rhs));
}

static_assert((tapFAIL_HARD & tapRETRY) == tapNONE, "ApplyFlags operator &");
static_assert((tapRETRY & tapFAIL_HARD) == tapNONE, "ApplyFlags operator &");

constexpr ApplyFlags
operator~(ApplyFlags const& flags)
{
    return safe_cast<ApplyFlags>(
        ~safe_cast<std::underlying_type_t<ApplyFlags>>(flags));
}

static_assert(
    ~tapRETRY == safe_cast<ApplyFlags>(0xFFFFFFDFu),
    "ApplyFlags operator ~");

inline ApplyFlags
operator|=(ApplyFlags& lhs, ApplyFlags const& rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

inline ApplyFlags
operator&=(ApplyFlags& lhs, ApplyFlags const& rhs)
{
    lhs = lhs & rhs;
    return lhs;
}

//------------------------------------------------------------------------------

/** Writeable view to a ledger, for applying a transaction.

    This refinement of ReadView provides an interface where
    the SLE can be "checked out" for modifications and put
    back in an updated or removed state. Also added is an
    interface to provide contextual information necessary
    to calculate the results of transaction processing,
    including the metadata if the view is later applied to
    the parent (using an interface in the derived class).
    The context info also includes values from the base
    ledger such as sequence number and the network time.

    This allows implementations to journal changes made to
    the state items in a ledger, with the option to apply
    those changes to the base or discard the changes without
    affecting the base.

    Typical usage is to call read() for non-mutating
    operations.

    For mutating operations the sequence is as follows:

        // Add a new value
        v.insert(sle);

        // Check out a value for modification
        sle = v.peek(k);

        // Indicate that changes were made
        v.update(sle)

        // Or, erase the value
        v.erase(sle)

    The invariant is that insert, update, and erase may not
    be called with any SLE which belongs to different view.
*/
class ApplyView : public ReadView
{
private:
    /** Add an entry to a directory using the specified insert strategy */
    std::optional<std::uint64_t>
    dirAdd(
        bool preserveOrder,
        Keylet const& directory,
        uint256 const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe);

public:
    ApplyView() = default;

    /** Returns the tx apply flags.

        Flags can affect the outcome of transaction
        processing. For example, transactions applied
        to an open ledger generate "local" failures,
        while transactions applied to the consensus
        ledger produce hard failures (and claim a fee).
    */
    virtual ApplyFlags
    flags() const = 0;

    /** Prepare to modify the SLE associated with key.

        Effects:

            Gives the caller ownership of a modifiable
            SLE associated with the specified key.

        The returned SLE may be used in a subsequent
        call to erase or update.

        The SLE must not be passed to any other ApplyView.

        @return `nullptr` if the key is not present
    */
    virtual std::shared_ptr<SLE>
    peek(Keylet const& k) = 0;

    /** Remove a peeked SLE.

        Requirements:

            `sle` was obtained from prior call to peek()
            on this instance of the RawView.

        Effects:

            The key is no longer associated with the SLE.
    */
    virtual void
    erase(std::shared_ptr<SLE> const& sle) = 0;

    /** Insert a new state SLE

        Requirements:

            `sle` was not obtained from any calls to
            peek() on any instances of RawView.

            The SLE's key must not already exist.

        Effects:

            The key in the state map is associated
            with the SLE.

            The RawView acquires ownership of the shared_ptr.

        @note The key is taken from the SLE
    */
    virtual void
    insert(std::shared_ptr<SLE> const& sle) = 0;

    /** Indicate changes to a peeked SLE

        Requirements:

            The SLE's key must exist.

            `sle` was obtained from prior call to peek()
            on this instance of the RawView.

        Effects:

            The SLE is updated

        @note The key is taken from the SLE
    */
    /** @{ */
    virtual void
    update(std::shared_ptr<SLE> const& sle) = 0;

    //--------------------------------------------------------------------------

    // Called when a credit is made to an account
    // This is required to support PaymentSandbox
    virtual void
    creditHookIOU(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        STAmount const& preCreditBalance)
    {
        XRPL_ASSERT(
            amount.holds<Issue>(), "creditHookIOU: amount is for Issue");
    }

    virtual void
    creditHookMPT(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        std::uint64_t preCreditBalanceHolder,
        std::int64_t preCreditBalanceIssuer)
    {
        XRPL_ASSERT(
            amount.holds<MPTIssue>(), "creditHookMPT: amount is for MPTIssue");
    }

    /** Facilitate tracking of MPT sold by an issuer owning MPT sell offer.
     * Unlike IOU, MPT doesn't have bi-directional relationship with an issuer,
     * where a trustline limits an amount that can be issued to a holder.
     * Consequently, the credit step (last MPTEndpointStep or
     * BookStep buying MPT) might temporarily overflow OutstandingAmount.
     * Limiting of a step's output amount in this case is delegated to
     * the next step (in rev order). The next step always redeems when a holder
     * account sells MPT (first MPTEndpointStep or BookStep selling MPT).
     * In this case the holder account is only limited by the step's output
     * and it's available funds since it's transferring the funds from one
     * account to another account and doesn't change OutstandingAmount.
     * This doesn't apply to an offer owned by an issuer.
     * In this case the issuer sells or self debits and is increasing
     * OutstandingAmount. Ability to issue is limited by the issuer
     * originally available funds less already self sold MPT amounts (MPT sell
     * offer).
     * Consider an example:
     * - GW creates MPT(USD) with 1,000USD MaximumAmount.
     * - GW pays 950USD to A1.
     * - A1 creates an offer 100XRP(buy)/100USD(sell).
     * - GW creates an offer 100XRP(buy)/100USD(sell).
     * - A2 pays 200USD to A3 with sendMax of 200XRP.
     * Since the payment engine executes payments in reverse,
     * OutstandingAmount overflows in MPTEndpointStep: 950 + 200 = 1,150USD.
     * BookStep first consumes A1 offer. This reduces OutstandingAmount
     * by 100USD: 1,150 - 100 = 1,050USD. GW offer can only be partially
     * consumed because the initial available amount is 50USD = 1,000 - 950.
     * BookStep limits it's output to 150USD. This in turn limits A3's send
     * amount to 150XRP: A1 buys 100XRP and sells 100USD to A3. This doesn't
     * change OutstandingAmount. GW buys 50XRP and sells 50USD to A3. This
     * changes OutstandingAmount 10 1,000USD.
     */
    virtual void
    issuerSelfDebitHookMPT(
        MPTIssue const& issue,
        std::uint64_t amount,
        std::int64_t origBalance)
    {
    }

    // Called when the owner count changes
    // This is required to support PaymentSandbox
    virtual void
    adjustOwnerCountHook(
        AccountID const& account,
        std::uint32_t cur,
        std::uint32_t next)
    {
    }

    /** Append an entry to a directory

        Entries in the directory will be stored in order of insertion, i.e. new
        entries will always be added at the tail end of the last page.

        @param directory the base of the directory
        @param key the entry to insert
        @param describe callback to add required entries to a new page

        @return a \c std::optional which, if insertion was successful,
                will contain the page number in which the item was stored.

        @note this function may create a page (including a root page), if no
              page with space is available. This function will only fail if the
              page counter exceeds the protocol-defined maximum number of
              allowable pages.
    */
    /** @{ */
    std::optional<std::uint64_t>
    dirAppend(
        Keylet const& directory,
        Keylet const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe)
    {
        if (key.type != ltOFFER)
        {
            UNREACHABLE(
                "ripple::ApplyView::dirAppend : only Offers are appended to "
                "book directories");
            // Only Offers are appended to book directories. Call dirInsert()
            // instead
            return std::nullopt;
        }
        return dirAdd(true, directory, key.key, describe);
    }
    /** @} */

    /** Insert an entry to a directory

        Entries in the directory will be stored in a semi-random order, but
        each page will be maintained in sorted order.

        @param directory the base of the directory
        @param key the entry to insert
        @param describe callback to add required entries to a new page

        @return a \c std::optional which, if insertion was successful,
                will contain the page number in which the item was stored.

        @note this function may create a page (including a root page), if no
              page with space is available.this function will only fail if the
              page counter exceeds the protocol-defined maximum number of
              allowable pages.
    */
    /** @{ */
    std::optional<std::uint64_t>
    dirInsert(
        Keylet const& directory,
        uint256 const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe)
    {
        return dirAdd(false, directory, key, describe);
    }

    std::optional<std::uint64_t>
    dirInsert(
        Keylet const& directory,
        Keylet const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe)
    {
        return dirAdd(false, directory, key.key, describe);
    }
    /** @} */

    /** Remove an entry from a directory

        @param directory the base of the directory
        @param page the page number for this page
        @param key the entry to remove
        @param keepRoot if deleting the last entry, don't
                        delete the root page (i.e. the directory itself).

        @return \c true if the entry was found and deleted and
                \c false otherwise.

        @note This function will remove zero or more pages from the directory;
              the root page will not be deleted even if it is empty, unless
              \p keepRoot is not set and the directory is empty.
    */
    /** @{ */
    bool
    dirRemove(
        Keylet const& directory,
        std::uint64_t page,
        uint256 const& key,
        bool keepRoot);

    bool
    dirRemove(
        Keylet const& directory,
        std::uint64_t page,
        Keylet const& key,
        bool keepRoot)
    {
        return dirRemove(directory, page, key.key, keepRoot);
    }
    /** @} */

    /** Remove the specified directory, invoking the callback for every node. */
    bool
    dirDelete(
        Keylet const& directory,
        std::function<void(uint256 const&)> const&);

    /** Remove the specified directory, if it is empty.

        @param directory the identifier of the directory node to be deleted
        @return \c true if the directory was found and was successfully deleted
                \c false otherwise.

        @note The function should only be called with the root entry (i.e. with
              the first page) of a directory.
    */
    bool
    emptyDirDelete(Keylet const& directory);
};

}  // namespace ripple

#endif
