#ifndef XRPL_LEDGER_OPENVIEW_H_INCLUDED
#define XRPL_LEDGER_OPENVIEW_H_INCLUDED

#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/RawStateTable.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/XRPAmount.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>

#include <functional>
#include <utility>

namespace ripple {

/** Open ledger construction tag.

    Views constructed with this tag will have the
    rules of open ledgers applied during transaction
    processing.
 */
inline constexpr struct open_ledger_t
{
    explicit constexpr open_ledger_t() = default;
} open_ledger{};

/** Batch view construction tag.

    Views constructed with this tag are part of a stack of views
    used during batch transaction applied.
 */
inline constexpr struct batch_view_t
{
    explicit constexpr batch_view_t() = default;
} batch_view{};

//------------------------------------------------------------------------------

/** Writable ledger view that accumulates state and tx changes.

    @note Presented as ReadView to clients.
*/
class OpenView final : public ReadView, public TxsRawView
{
private:
    // Initial size for the monotonic_buffer_resource used for allocations
    // The size was chosen from the old `qalloc` code (which this replaces).
    // It is unclear how the size initially chosen in qalloc.
    static constexpr size_t initialBufferSize = kilobytes(256);

    class txs_iter_impl;

    struct txData
    {
        std::shared_ptr<Serializer const> txn;
        std::shared_ptr<Serializer const> meta;

        // Constructor needed for emplacement in std::map
        txData(
            std::shared_ptr<Serializer const> const& txn_,
            std::shared_ptr<Serializer const> const& meta_)
            : txn(txn_), meta(meta_)
        {
        }
    };

    // List of tx, key order
    // Use boost::pmr functionality instead of std::pmr
    // functions b/c clang does not support pmr yet (as-of 9/2020)
    using txs_map = std::map<
        key_type,
        txData,
        std::less<key_type>,
        boost::container::pmr::polymorphic_allocator<
            std::pair<key_type const, txData>>>;

    // monotonic_resource_ must outlive `items_`. Make a pointer so it may be
    // easily moved.
    std::unique_ptr<boost::container::pmr::monotonic_buffer_resource>
        monotonic_resource_;
    txs_map txs_;
    Rules rules_;
    LedgerInfo info_;
    ReadView const* base_;
    detail::RawStateTable items_;
    std::shared_ptr<void const> hold_;

    /// In batch mode, the number of transactions already executed.
    std::size_t baseTxCount_ = 0;

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
    OpenView(OpenView const&);

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

    OpenView(batch_view_t, OpenView& base) : OpenView(std::addressof(base))
    {
        baseTxCount_ = base.txCount();
    }

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

    std::optional<key_type>
    succ(
        key_type const& key,
        std::optional<key_type> const& last = std::nullopt) const override;

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
