#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace xrpl {

// Concept to distinguish read-only vs writable view types
template <typename V>
concept WritableView = std::derived_from<V, ApplyView>;

/**
 * View-parameterized base class for all ledger entry wrappers.
 *
 * SLEBase<ReadView>  — read-only:  holds shared_ptr<SLE const> + ReadView const&
 * SLEBase<ApplyView> — writable:   holds shared_ptr<SLE> + ApplyView& + Keylet,
 *                                   plus insert/update/erase operations
 *
 * Write-only members are gated by `requires` clauses, providing compile-time
 * guarantees that read-only wrappers cannot mutate state.
 *
 * Derived classes should provide domain-specific accessors that hide
 * implementation details of the underlying ledger entry format.
 */
template <typename ViewT>
class SLEBase
{
public:
    static constexpr bool kIS_WRITABLE = WritableView<ViewT>;

    // SLE pointer type: mutable for writable views, const for read-only
    using sle_ptr_type = std::conditional_t<kIS_WRITABLE, std::shared_ptr<SLE>, SLE::const_pointer>;

    // View reference type: ApplyView& for writable, ReadView const& for read-only
    using view_ref_type = std::conditional_t<kIS_WRITABLE, ApplyView&, ReadView const&>;

    virtual ~SLEBase() = default;

    SLEBase(SLEBase const&) = default;
    SLEBase(SLEBase&&) = default;
    SLEBase&
    operator=(SLEBase const&) = delete;
    SLEBase&
    operator=(SLEBase&&) = delete;

    // --- Common interface (always available) ---

    /** Returns true if the ledger entry exists */
    [[nodiscard]] bool
    exists() const
    {
        return sle_ != nullptr;
    }

    /** Explicit conversion to bool for convenient existence checking */
    explicit
    operator bool() const
    {
        return exists();
    }

    /** Returns the underlying SLE for read access */
    [[nodiscard]] SLE::const_pointer
    sle() const
    {
        return sle_;
    }

    /** Returns the read view (always available; ApplyView inherits ReadView) */
    [[nodiscard]] ReadView const&
    readView() const
    {
        return view_;
    }

    /** Const dereference operators (always available) */
    STLedgerEntry const*
    operator->() const
    {
        XRPL_ASSERT(exists(), "xrpl::SLEBase::operator-> : exists");
        return sle_.get();
    }

    STLedgerEntry const&
    operator*() const
    {
        XRPL_ASSERT(exists(), "xrpl::SLEBase::operator* : exists");
        return *sle_;
    }

    // --- Writable interface (compile-time gated) ---

    /** Returns a mutable SLE for write operations */
    [[nodiscard]] sle_ptr_type const&
    mutableSle() const
        requires kIS_WRITABLE
    {
        return sle_;
    }

    /** Returns true if this wrapper supports write operations */
    [[nodiscard]] bool
    canModify() const
        requires kIS_WRITABLE
    {
        return sle_ != nullptr;
    }

    /** Returns the apply view for write operations */
    [[nodiscard]] ApplyView&
    applyView() const
        requires kIS_WRITABLE
    {
        return view_;
    }

    /** Mutable dereference operators */
    STLedgerEntry*
    operator->()
        requires kIS_WRITABLE
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::operator-> : can modify");
        return sle_.get();
    }

    STLedgerEntry&
    operator*()
        requires kIS_WRITABLE
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::operator* : can modify");
        return *sle_;
    }

    void
    insert()
        requires kIS_WRITABLE
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::insert : can modify");
        view_.insert(sle_);
    }

    void
    erase()
        requires kIS_WRITABLE
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::erase : can modify");
        view_.erase(sle_);
    }

    void
    update()
        requires kIS_WRITABLE
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::update : can modify");
        view_.update(sle_);
    }

    void
    newSLE()
        requires kIS_WRITABLE
    {
        XRPL_ASSERT(!canModify(), "xrpl::SLEBase::newSLE : no existing SLE");
        sle_ = std::make_shared<SLE>(key_);
    }

    [[nodiscard]] beast::Journal
    journal() const
    {
        return j_;
    }

protected:
    SLEBase() = delete;

    /** Constructor for read-only context */
    explicit SLEBase(
        SLE::const_pointer sle,
        ReadView const& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIS_WRITABLE)
        : view_(view), sle_(std::move(sle)), j_(j)
    {
    }

    /** Converting constructor: writable → read-only.
     *  Enables implicit conversion from SLEBase<ApplyView> to
     *  SLEBase<ReadView>, so functions taking ReadOnlySLE const& can
     *  accept WritableSLE.
     */
    template <WritableView OtherViewT>
    SLEBase(SLEBase<OtherViewT> const& other)
        requires(!kIS_WRITABLE)
        : view_(other.readView()), sle_(other.sle()), j_(other.journal())
    {
    }

    /** Constructor for writable context (from existing SLE) */
    explicit SLEBase(
        std::shared_ptr<SLE> sle,
        ApplyView& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIS_WRITABLE
        : view_(view)
        , key_(sle ? Keylet(sle->getType(), sle->key()) : Keylet(ltANY, uint256{}))
        , sle_(std::move(sle))
        , j_(j)
    {
    }

    /** Constructor for writable context (peek from view by keylet) */
    explicit SLEBase(
        Keylet const& key,
        ApplyView& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIS_WRITABLE
        : view_(view), key_(key), sle_(view_.peek(key)), j_(j)
    {
    }

    view_ref_type view_;

    // Keylet is only meaningful for writable views, but we conditionally
    // include it to avoid wasting space in read-only wrappers.
    struct Empty
    {
    };
    [[no_unique_address]]
    std::conditional_t<kIS_WRITABLE, Keylet, Empty> key_{};

    sle_ptr_type sle_;
    beast::Journal j_;
};

}  // namespace xrpl
