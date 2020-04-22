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

#ifndef RIPPLE_LEDGER_OPENVIEW_H_INCLUDED
#define RIPPLE_LEDGER_OPENVIEW_H_INCLUDED

#include <ripple/basics/XRPAmount.h>
#include <ripple/basics/qalloc.h>
#include <ripple/ledger/RawView.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/detail/RawStateTable.h>
#include <functional>
#include <utility>

namespace ripple {

/** Open ledger construction tag.

    Views constructed with this tag will have the
    rules of open ledgers applied during transaction
    processing.
*/
struct open_ledger_t
{
    explicit open_ledger_t() = default;
};

extern open_ledger_t const open_ledger;

//------------------------------------------------------------------------------

/** Writable ledger view that accumulates state and tx changes.

    @note Presented as ReadView to clients.
*/
class OpenView final : public ReadView, public TxsRawView
{
private:
    class txs_iter_impl;

    // List of tx, key order
    using txs_map = std::map<
        key_type,
        std::pair<
            std::shared_ptr<Serializer const>,
            std::shared_ptr<Serializer const>>,
        std::less<key_type>,
        qalloc_type<
            std::pair<
                key_type const,
                std::pair<
                    std::shared_ptr<Serializer const>,
                    std::shared_ptr<Serializer const>>>,
            false>>;

    Rules rules_;
    txs_map txs_;
    LedgerInfo info_;
    ReadView const* base_;
    detail::RawStateTable items_;
    std::shared_ptr<void const> hold_;
    bool open_ = true;

public:
    OpenView() = delete;
    OpenView&
    operator=(OpenView&&) = delete;
    OpenView&
    operator=(OpenView const&) = delete;

    OpenView(OpenView&&) = default;

    /** Construct a shallow copy.

        Effects:

            Creates a new object with a copy of
            the modification state table.

        The objects managed by shared pointers are
        not duplicated but shared between instances.
        Since the SLEs are immutable, calls on the
        RawView interface cannot break invariants.
    */
    OpenView(OpenView const&) = default;

    /** Construct an open ledger view.

        Effects:

            The sequence number is set to the
            sequence number of parent plus one.

            The parentCloseTime is set to the
            closeTime of parent.

            If `hold` is not nullptr, retains
            ownership of a copy of `hold` until
            the MetaView is destroyed.

            Calls to rules() will return the
            rules provided on construction.

        The tx list starts empty and will contain
        all newly inserted tx.
    */
    /** @{ */
    OpenView(
        open_ledger_t,
        ReadView const* base,
        Rules const& rules,
        std::shared_ptr<void const> hold = nullptr);

    OpenView(
        open_ledger_t,
        Rules const& rules,
        std::shared_ptr<ReadView const> const& base)
        : OpenView(open_ledger, &*base, rules, base)
    {
    }
    /** @} */

    /** Construct a new last closed ledger.

        Effects:

            The LedgerInfo is copied from the base.

            The rules are inherited from the base.

        The tx list starts empty and will contain
        all newly inserted tx.
    */
    OpenView(ReadView const* base, std::shared_ptr<void const> hold = nullptr);

    /** Returns true if this reflects an open ledger. */
    bool
    open() const override
    {
        return open_;
    }

    /** Return the number of tx inserted since creation.

        This is used to set the "apply ordinal"
        when calculating transaction metadata.
    */
    std::size_t
    txCount() const;

    /** Apply changes. */
    void
    apply(TxsRawView& to) const;

    // ReadView

    LedgerInfo const&
    info() const override;

    Fees const&
    fees() const override;

    Rules const&
    rules() const override;

    bool
    exists(Keylet const& k) const override;

    boost::optional<key_type>
    succ(
        key_type const& key,
        boost::optional<key_type> const& last = boost::none) const override;

    std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    std::unique_ptr<sles_type::iter_base>
    slesBegin() const override;

    std::unique_ptr<sles_type::iter_base>
    slesEnd() const override;

    std::unique_ptr<sles_type::iter_base>
    slesUpperBound(uint256 const& key) const override;

    std::unique_ptr<txs_type::iter_base>
    txsBegin() const override;

    std::unique_ptr<txs_type::iter_base>
    txsEnd() const override;

    bool
    txExists(key_type const& key) const override;

    tx_type
    txRead(key_type const& key) const override;

    // RawView

    void
    rawErase(std::shared_ptr<SLE> const& sle) override;

    void
    rawInsert(std::shared_ptr<SLE> const& sle) override;

    void
    rawReplace(std::shared_ptr<SLE> const& sle) override;

    void
    rawDestroyXRP(XRPAmount const& fee) override;

    // TxsRawView

    void
    rawTxInsert(
        key_type const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData) override;
};

}  // namespace ripple

#endif
