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

#ifndef RIPPLE_LEDGER_VIEW_H_INCLUDED
#define RIPPLE_LEDGER_VIEW_H_INCLUDED

#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TER.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/View.h>
#include <beast/utility/Journal.h>
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <utility>

#include <vector>

namespace ripple {

/** Reflects the fee settings for a particular ledger.

    The fees are always the same for any transactions applied
    to a ledger. Changes to fees occur in between ledgers.
*/
struct Fees
{
    std::uint64_t base = 0;         // Reference tx cost (drops)
    std::uint32_t units = 0;        // Reference fee units
    std::uint32_t reserve = 0;      // Reserve base (drops)
    std::uint32_t increment = 0;    // Reserve increment (drops)

    Fees() = default;
    Fees (Fees const&) = default;
    Fees& operator= (Fees const&) = default;

    /** Returns the account reserve given the owner count, in drops.

        The reserve is calculated as the reserve base plus
        the reserve increment times the number of increments.
    */
    std::uint64_t
    accountReserve (std::size_t ownerCount) const
    {
        return reserve + ownerCount * increment;
    }
};

//------------------------------------------------------------------------------

/** Information about the notional ledger backing the view. */
struct ViewInfo
{
    // Fields for all ledgers
    bool open = true;
    LedgerIndex seq = 0;
    std::uint32_t parentCloseTime = 0;

    // Fields for closed ledgers
    // Closed means "tx set already determined"
    uint256 hash = zero;
    //uint256 txHash;
    //uint256 stateHash;
    //uint256 parentHash;
    //std::uint64_t coins = 0;
    //bool validated = false;
    //int closeTimeRes = 0;

    // For closed ledgers, the time the ledger
    // closed. For open ledgers, the time the ledger
    // will close if there's no transactions.
    //
    std::uint32_t closeTime = 0;
};

//------------------------------------------------------------------------------

/** A view into a ledger.

    This interface provides read access to state
    and transaction items. There is no checkpointing
    or calculation of metadata.

    A raw interace is provided for mutable ledgers,
    this is used internally by implementations and
    should not be called directly.
*/
// VFALCO Rename unchecked functions to raw
class BasicView
{
protected:
    class iterator_impl;

public:
    using key_type = uint256;
    using mapped_type =
        std::shared_ptr<SLE const>;

    BasicView()
        : txs(*this)
    {
    }

    virtual ~BasicView() = default;

    /** Returns information about the ledger. */
    virtual
    ViewInfo const&
    info() const = 0;

    /** Returns true if this reflects an open ledger. */
    bool
    open() const
    {
        return info().open;
    }

    /** Returns true if this reflects a closed ledger. */
    bool
    closed() const
    {
        return ! info().open;
    }

    /** Returns the close time of the previous ledger. */
    std::uint32_t
    parentCloseTime() const
    {
        return info().parentCloseTime;
    }

    /** Returns the sequence number of the base ledger. */
    LedgerIndex
    seq() const
    {
        return info().seq;
    }

    /** Returns the fees for the base ledger. */
    virtual
    Fees const&
    fees() const = 0;

    /** Determine if a state item exists.

        @note This can be more efficient than calling read.

        @return `true` if a SLE is associated with the
                specified key.
    */
    virtual
    bool
    exists (Keylet const& k) const = 0;

    /** Return the key of the next state item.

        This returns the key of the first state item
        whose key is greater than the specified key. If
        no such key is present, boost::none is returned.

        If `last` is engaged, returns boost::none when
        the key returned would be outside the open
        interval (key, last).
    */
    virtual
    boost::optional<uint256>
    succ (uint256 const& key, boost::optional<
        uint256> last = boost::none) const = 0;

    /** Return the state item associated with a key.

        Effects:
            If the key exists, gives the caller ownership
            of the non-modifiable corresponding SLE.

        @note While the returned SLE is `const` from the
              perspective of the caller, it can be changed
              by other callers through raw operations.

        @return `nullptr` if the key is not present or
                if the type does not match.
    */
    virtual
    std::shared_ptr<SLE const>
    read (Keylet const& k) const = 0;

    // used by the implementation
    virtual
    bool
    txEmpty() const = 0;

    // used by the implementation
    virtual
    std::unique_ptr<iterator_impl>
    txBegin() const = 0;

    // used by the implementation
    virtual
    std::unique_ptr<iterator_impl>
    txEnd() const = 0;

    /** Unconditionally erase a state item.

        Requirements:
            key must exist
        
        Effects:
            The item associated with key is
            unconditionally removed.

        This can break invariants

        @return `true` if the key was found
    */
    virtual
    bool
    unchecked_erase (uint256 const& key) = 0;

    /** Unconditionally insert a state item.

        Requirements:
            The key must not already exist.

        Effects:
            The key is associated with the SLE.

            Ownership of the SLE is transferred
            to the view.

        This can break invariants.

        @note The key is taken from the SLE
    */
    virtual
    void
    unchecked_insert (std::shared_ptr<SLE>&& sle) = 0;

    /** Unconditionally replace a state item.

        Requirements:
            The key must exist.

        Effects:
            The key is associated with the SLE.

            Ownership of the SLE is transferred
            to the view.

        This can break invariants.

        @note The key is taken from the SLE
    */
    virtual
    void
    unchecked_replace (std::shared_ptr<SLE>&& sle) = 0;

    /** Destroy XRP.

        This is used to pay for transaction fees.
    */
    virtual
    void
    destroyCoins (std::uint64_t feeDrops) = 0;

    /** Returns the number of newly inserted transactions.

        This will always be zero for closed ledgers, there
        is no efficient way to count the number of tx in
        the map. For views representing open ledgers this
        starts out as one and gets incremented for each
        transaction that is applied.
    */
    virtual
    std::size_t
    txCount() const = 0;

    /** Returns `true` if a tx exists in the tx map.

        A tx exists in the map if it is part of the
        base ledger, or if it is a newly inserted tx.
    */
    virtual
    bool
    txExists (uint256 const& key) const = 0;

    /** Add a transaction to the tx map.

        @param metaData Optional metadata (may be nullptr)
    */
    virtual
    void
    txInsert (uint256 const& key,
        std::shared_ptr<Serializer const
            > const& txn, std::shared_ptr<
                Serializer const> const& metaData) = 0;

    // DEBUG ROUTINE
    // Return a list of transaction keys in the tx map.
    virtual
    std::vector<uint256>
    txList() const = 0;

    //--------------------------------------------------------------------------

    // A Forward Range container
    // representing the list of transactions
    //
    class txs_type
    {
    private:
        friend class BasicView;

        BasicView& view_;

        explicit
        txs_type (BasicView& view)
            : view_ (view)
        {
        }

    public:
        /** ForwardIterator to access transactions */
        class iterator;
        using const_iterator = iterator;

        /** Transaction followed by optional metadata. */
        using value_type = std::pair<
            std::shared_ptr<STTx const>,
                std::shared_ptr<STObject const>>;

        txs_type() = delete;
        txs_type (txs_type const&) = delete;
        txs_type& operator= (txs_type const&) = delete;

        bool
        empty() const;

        /** Return iterator to the beginning of the tx list.
        
            Meets the requirements of `ForwardIterator`
        */
        iterator
        begin() const;

        /** Return iterator to one past the end of the tx list.

            Meets the requirements of `ForwardIterator`
        */
        iterator
        end() const;
    };

    // Memberspace
    // The list of transactions.
    txs_type const txs;

    //--------------------------------------------------------------------------

    // Called to adjust returned balances
    // This is required to support PaymentView
    virtual
    STAmount
    balanceHook (AccountID const& account,
        AccountID const& issuer,
            STAmount const& amount) const
    {
        return amount;
    }
};

//------------------------------------------------------------------------------

class BasicView::iterator_impl
{
public:
    virtual
    ~iterator_impl() = default;

    virtual
    std::unique_ptr<iterator_impl>
    copy() const = 0;

    virtual
    bool
    equal (iterator_impl const& impl) const = 0;

    virtual
    void
    increment() = 0;

    virtual
    txs_type::value_type
    dereference() const = 0;
};

//------------------------------------------------------------------------------

class BasicView::txs_type::iterator
{
public:
    using value_type =
        BasicView::txs_type::value_type;

    using pointer = value_type const*;

    using reference = value_type const&;

    using difference_type =
        std::ptrdiff_t;

    using iterator_category =
        std::forward_iterator_tag;

    iterator() = default;

    iterator (iterator const& other)
        : view_ (other.view_)
        , impl_ (other.impl_->copy())
        , cache_ (other.cache_)
    {
    }

    iterator (iterator&& other)
        : view_ (other.view_)
        , impl_ (std::move(other.impl_))
        , cache_ (std::move(other.cache_))
    {
    }

    // Used by the implementation
    explicit
    iterator (BasicView const& view,
            std::unique_ptr<
                iterator_impl> impl)
        : view_ (&view)
        , impl_ (std::move(impl))
    {
    }

    iterator&
    operator= (iterator const& other)
    {
        if (this == &other)
            return *this;
        view_ = other.view_;
        impl_ = other.impl_->copy();
        cache_ = other.cache_;
        return *this;
    }

    iterator&
    operator= (iterator&& other)
    {
        view_ = other.view_;
        impl_ = std::move(other.impl_);
        cache_ = std::move(other.cache_);
        return *this;
    }

    bool
    operator== (iterator const& other) const
    {
        assert(view_ == other.view_);
        return impl_->equal(*other.impl_);
    }

    bool
    operator!= (iterator const& other) const
    {
        return ! (*this == other);
    }

    // Can throw
    reference
    operator*() const
    {
        if (! cache_)
            cache_ = impl_->dereference();
        return *cache_;
    }

    // Can throw
    pointer
    operator->() const
    {
        return &**this;
    }

    iterator&
    operator++()
    {
        impl_->increment();
        cache_ = boost::none;
        return *this;
    }

    iterator
    operator++(int)
    {
        iterator prev(*view_,
            impl_->copy());
        prev.cache_ = std::move(cache_);
        ++(*this);
        return prev;
    }

private:
    BasicView const* view_ = nullptr;
    std::unique_ptr<iterator_impl> impl_;
    boost::optional<value_type> mutable cache_;
};

inline
bool
BasicView::txs_type::empty() const
{
    return view_.txEmpty();
}

inline
auto
BasicView::txs_type::begin() const ->
    iterator
{
    return iterator(view_, view_.txBegin());
}

inline
auto
BasicView::txs_type::end() const ->
    iterator
{
    return iterator(view_, view_.txEnd());
}

//------------------------------------------------------------------------------

enum ViewFlags
{
    tapNONE             = 0x00,

    // Signature already checked
    tapNO_CHECK_SIGN    = 0x01,

    // Enable supressed features for testing.
    // This lets unit tests exercise code that
    // is not turned on for production.
    //
    tapENABLE_TESTING   = 0x02,

    // This is not the transaction's last pass
    // Transaction can be retried, soft failures allowed
    tapRETRY            = 0x20,

    // Transaction came from a privileged source
    tapADMIN            = 0x400,
};

inline
ViewFlags
operator|(ViewFlags const& lhs,
    ViewFlags const& rhs)
{
    return static_cast<ViewFlags>(
        static_cast<int>(lhs) |
            static_cast<int>(rhs));
}

inline
ViewFlags
operator&(ViewFlags const& lhs,
    ViewFlags const& rhs)
{
    return static_cast<ViewFlags>(
        static_cast<int>(lhs) &
            static_cast<int>(rhs));
}

/** A contextual view into a ledger's state items.

    This refinement of BasicView provides an interface where
    the SLE can be "checked out" for modifications and put
    back in an updated or removed state. Also added is an
    interface to provide contextual information necessary
    to calculate the results of transaction processing,
    including the metadata if the view is later applied to
    the parent (using an interface in the derived class).
    The context info also includes values from the base
    ledger such as sequence number and the network time.

    This allows the MetaView implementation to journal
    changes made to the state items in a ledger, with the
    option to apply those changes to the parent ledger
    or view or discard the changes without affecting the
    parent.

    Typical usage is to call read() for non-mutating
    operations. This can be done by calling any function that
    takes a BasicView parameter.

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
    be called with any SLE which belongs to different View.
*/
class View : public BasicView
{
public:
    virtual ~View() = default;

    /** Returns the contextual tx processing flags.

        Transactions may process differently depending on
        information in the context. For example, transactions
        applied to an open ledger generate "local" failures,
        while transactions applied to the consensus ledger
        produce hard failures (and claim a fee).
    */
    virtual
    ViewFlags
    flags() const = 0;

    /** Prepare to modify the SLE associated with key.

        Effects:
            Gives the caller ownership of the SLE associated
            with the specified key.

        The returned SLE may be used in a subsequent
        call to erase or update.

        The SLE must not be passed to any other View.

        @return `nullptr` if the key is not present
    */
    virtual
    std::shared_ptr<SLE>
    peek (Keylet const& k) = 0;

    /** Remove a peeked SLE.

        Requirements:
            `sle` was obtained from prior call to peek()
            on this instance of the View.

        Effects:
            The key is no longer associated with the SLE.
    */
    virtual
    void
    erase (std::shared_ptr<SLE> const& sle) = 0;

    /** Insert a new state SLE

        Requirements:
            `sle` was not obtained from any calls to
            peek() on any instances of View.

        Effects:
            assert if the key already exists

            The key in the state map is associated
            with the SLE.

            The View acquires ownership of the shared_ptr.

        @note The key is taken from the SLE
    */
    virtual
    void
    insert (std::shared_ptr<SLE> const& sle) = 0;

    /** Indicate changes to a peeked SLE

        Requirements:
            `sle` was obtained from prior call to peek()
            on this instance of the View.

        Effects:
            The View is notified that the SLE changed.

        @note The key is taken from the SLE
    */
    /** @{ */
    virtual
    void
    update (std::shared_ptr<SLE> const& sle) = 0;

    //--------------------------------------------------------------------------

    // Called when a credit is made to an account
    // This is required to support PaymentView
    virtual
    void
    creditHook (AccountID const& from,
        AccountID const& to,
            STAmount const& amount)
    {
    }
};

//------------------------------------------------------------------------------

// Wrapper to facilitate subclasses,
// forwards all non-overriden virtuals.
//
template <class Member>
class BasicViewWrapper : public BasicView
{
protected:
    Member view_;

public:
    template <class... Args>
    explicit
    BasicViewWrapper (Args&&... args)
        : view_(std::forward<Args>(args)...)
    {
    }

    ViewInfo const&
    info() const
    {
        return view_.info();
    }

    Fees const&
    fees() const override
    {
        return view_.fees();
    }

    bool
    exists (Keylet const& k) const override
    {
        return view_.exists(k);
    }

    boost::optional<uint256>
    succ (uint256 const& key, boost::optional<
        uint256> last = boost::none) const override
    {
        return view_.succ(key, last);
    }

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override
    {
        return view_.read(k);
    }

    bool
    txEmpty() const override
    {
        return view_.txEmpty();
    }

    std::unique_ptr<iterator_impl>
    txBegin() const override
    {
        return view_.txBegin();
    }

    std::unique_ptr<iterator_impl>
    txEnd() const override
    {
        return view_.txEnd();
    }

    bool
    unchecked_erase(
        uint256 const& key) override
    {
        return view_.unchecked_erase(key);
    }

    void
    unchecked_insert(
        std::shared_ptr<SLE>&& sle) override
    {
        return view_.unchecked_insert(
            std::move(sle));
    }

    void
    unchecked_replace(
        std::shared_ptr<SLE>&& sle) override
    {
        return view_.unchecked_replace(
            std::move(sle));
    }

    void
    destroyCoins (std::uint64_t feeDrops) override
    {
        return view_.destroyCoins(feeDrops);
    }

    std::size_t
    txCount() const override
    {
        return view_.txCount();
    }

    bool
    txExists (uint256 const& key) const override
    {
        return view_.txExists(key);
    }

    void
    txInsert (uint256 const& key,
        std::shared_ptr<Serializer const
            > const& txn, std::shared_ptr<
                Serializer const> const& metaData) override
    {
        view_.txInsert(key, txn, metaData);
    }

    std::vector<uint256>
    txList() const override
    {
        return view_.txList();
    }
};

// Wrapper to facilitate subclasses,
// forwards all non-overriden virtuals.
//
template <class Member>
class ViewWrapper : public View
{
protected:
    Member view_;

public:
    template <class... Args>
    explicit
    ViewWrapper (Args&&... args)
        : view_(std::forward<Args>(args)...)
    {
    }

    ViewInfo const&
    info() const
    {
        return view_.info();
    }

    Fees const&
    fees() const override
    {
        return view_.fees();
    }

    bool
    exists (Keylet const& k) const override
    {
        return view_.exists(k);
    }

    boost::optional<uint256>
    succ (uint256 const& key, boost::optional<
        uint256> last = boost::none) const override
    {
        return view_.succ(key, last);
    }

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override
    {
        return view_.read(k);
    }

    bool
    txEmpty() const override
    {
        return view_.txEmpty();
    }

    std::unique_ptr<iterator_impl>
    txBegin() const override
    {
        return view_.txBegin();
    }

    std::unique_ptr<iterator_impl>
    txEnd() const override
    {
        return view_.txEnd();
    }

    bool
    unchecked_erase(
        uint256 const& key) override
    {
        return view_.unchecked_erase(key);
    }

    void
    unchecked_insert(
        std::shared_ptr<SLE>&& sle) override
    {
        return view_.unchecked_insert(
            std::move(sle));
    }

    void
    unchecked_replace(
        std::shared_ptr<SLE>&& sle) override
    {
        return view_.unchecked_replace(
            std::move(sle));
    }

    void
    destroyCoins (std::uint64_t feeDrops) override
    {
        return view_.destroyCoins(feeDrops);
    }

    std::size_t
    txCount() const override
    {
        return view_.txCount();
    }

    bool
    txExists (uint256 const& key) const override
    {
        return view_.txExists(key);
    }

    void
    txInsert (uint256 const& key,
        std::shared_ptr<Serializer const
            > const& txn, std::shared_ptr<
                Serializer const> const& metaData) override
    {
        view_.txInsert(key, txn, metaData);
    }

    std::vector<uint256>
    txList() const override
    {
        return view_.txList();
    }

    //-----

    ViewFlags
    flags() const override
    {
        return view_.flags();
    }

    std::shared_ptr<SLE>
    peek (Keylet const& k) override
    {
        return view_.peek(k);
    }

    void
    erase (std::shared_ptr<
        SLE> const& sle) override
    {
        return view_.erase(sle);
    }

    void
    insert (std::shared_ptr<
        SLE> const& sle)  override
    {
        return view_.insert(sle);
    }

    void
    update (std::shared_ptr<
        SLE> const& sle) override
    {
        return view_.update(sle);
    }

    void
    creditHook (AccountID const& from,
        AccountID const& to,
            STAmount const& amount) override
    {
        return view_.creditHook (from, to, amount);
    }
};

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

/** Controls the treatment of frozen account balances */
enum FreezeHandling
{
    fhIGNORE_FREEZE,
    fhZERO_IF_FROZEN
};

Fees
getFees (BasicView const& view,
    Config const& config);

bool
isGlobalFrozen (BasicView const& view,
    AccountID const& issuer);

// Returns the amount an account can spend without going into debt.
//
// <-- saAmount: amount of currency held by account. May be negative.
STAmount
accountHolds (BasicView const& view,
    AccountID const& account, Currency const& currency,
        AccountID const& issuer, FreezeHandling zeroIfFrozen,
            Config const& config);

STAmount
accountFunds (BasicView const& view, AccountID const& id,
    STAmount const& saDefault, FreezeHandling freezeHandling,
        Config const& config);

/** Iterate all items in an account's owner directory. */
void
forEachItem (BasicView const& view, AccountID const& id,
    std::function<void (std::shared_ptr<SLE const> const&)> f);

/** Iterate all items after an item in an owner directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
bool
forEachItemAfter (BasicView const& view, AccountID const& id,
    uint256 const& after, std::uint64_t const hint,
        unsigned int limit, std::function<
            bool (std::shared_ptr<SLE const> const&)> f);

std::uint32_t
rippleTransferRate (BasicView const& view,
    AccountID const& issuer);

std::uint32_t
rippleTransferRate (BasicView const& view,
    AccountID const& uSenderID,
        AccountID const& uReceiverID,
            AccountID const& issuer);

/** Returns `true` if the directory is empty
    @param key The key of the directory
*/
bool
dirIsEmpty (BasicView const& view,
    Keylet const& k);

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
cdirFirst (BasicView const& view,
    uint256 const& uRootIndex,  // --> Root of directory.
    std::shared_ptr<SLE const>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-- next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
cdirNext (BasicView const& view,
    uint256 const& uRootIndex,  // --> Root of directory
    std::shared_ptr<SLE const>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-> next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

/** Adjust the owner count up or down. */
void
adjustOwnerCount (View& view,
    std::shared_ptr<SLE> const& sle,
        int amount);

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
dirFirst (View& view,
    uint256 const& uRootIndex,  // --> Root of directory.
    std::shared_ptr<SLE>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-- next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
dirNext (View& view,
    uint256 const& uRootIndex,  // --> Root of directory
    std::shared_ptr<SLE>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-> next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// Only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TER
dirAdd (View& view,
    std::uint64_t&                      uNodeDir,      // Node of entry.
    uint256 const&                      uRootIndex,
    uint256 const&                      uLedgerIndex,
    std::function<void (SLE::ref, bool)> fDescriber);

TER
dirDelete (View& view,
    const bool           bKeepRoot,
    const std::uint64_t& uNodeDir,      // Node item is mentioned in.
    uint256 const&       uRootIndex,
    uint256 const&       uLedgerIndex,  // Item being deleted
    const bool           bStable,
    const bool           bSoft);

// VFALCO NOTE Both STAmount parameters should just
//             be "Amount", a unit-less number.
//
/** Create a trust line

    This can set an initial balance.
*/
TER
trustCreate (View& view,
    const bool      bSrcHigh,
    AccountID const&  uSrcAccountID,
    AccountID const&  uDstAccountID,
    uint256 const&  uIndex,             // --> ripple state entry
    SLE::ref        sleAccount,         // --> the account being set.
    const bool      bAuth,              // --> authorize account.
    const bool      bNoRipple,          // --> others cannot ripple through
    const bool      bFreeze,            // --> funds cannot leave
    STAmount const& saBalance,          // --> balance of account being set.
                                        // Issuer should be noAccount()
    STAmount const& saLimit,            // --> limit for account being set.
                                        // Issuer should be the account being set.
    std::uint32_t uSrcQualityIn,
    std::uint32_t uSrcQualityOut);

TER
trustDelete (View& view,
    std::shared_ptr<SLE> const& sleRippleState,
        AccountID const& uLowAccountID,
            AccountID const& uHighAccountID);

/** Delete an offer.

    Requirements:
        The passed `sle` be obtained from a prior
        call to view.peek()
*/
TER
offerDelete (View& view,
    std::shared_ptr<SLE> const& sle);

//------------------------------------------------------------------------------

//
// Money Transfers
//

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// --> bCheckIssuer : normally require issuer to be involved.
TER
rippleCredit (View& view,
    AccountID const& uSenderID, AccountID const& uReceiverID,
    const STAmount & saAmount, bool bCheckIssuer);

TER
accountSend (View& view,
    AccountID const& from,
        AccountID const& to,
            const STAmount & saAmount);

TER 
issueIOU (View& view,
    AccountID const& account,
        STAmount const& amount,
            Issue const& issue);

TER
redeemIOU (View& view,
    AccountID const& account,
        STAmount const& amount,
            Issue const& issue);

TER
transferXRP (View& view,
    AccountID const& from,
        AccountID const& to,
            STAmount const& amount);

} // ripple

#endif
