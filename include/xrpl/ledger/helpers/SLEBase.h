#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace xrpl {

// Concept to distinguish read-only vs writable view types
template <typename V>
concept WritableView = std::derived_from<V, ApplyView>;

namespace detail {

/**
 * Resolves a keylet for a read-only wrapper.
 *
 * ReadView::read() on an ApplyView returns the underlying ledger's entry
 * whenever the view is not already tracking one, while peek() installs the
 * view's own copy and returns that. A read-only wrapper built with read()
 * would therefore hold an entry that goes stale the moment anything peeks the
 * same key and modifies it. Resolve through peek() whenever the view really is
 * an ApplyView, so every wrapper over that view shares one entry.
 */
inline SLE::const_pointer
resolveEntry(ReadView const& view, Keylet const& key)
{
    // Views are never const objects; the read-only wrapper only holds a const
    // reference because it does not itself modify the view.
    if (auto const applyView = dynamic_cast<ApplyView*>(const_cast<ReadView*>(&view)))
        return applyView->peek(key);
    return view.read(key);
}

}  // namespace detail

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
    static constexpr bool kIsWritable = WritableView<ViewT>;

    // SLE pointer type: mutable for writable views, const for read-only
    using sle_ptr_type = std::conditional_t<kIsWritable, std::shared_ptr<SLE>, SLE::const_pointer>;

    // View reference type: ApplyView& for writable, ReadView const& for
    // read-only
    using view_ref_type = std::conditional_t<kIsWritable, ApplyView&, ReadView const&>;

    virtual ~SLEBase() = default;

    SLEBase(SLEBase const&) = default;
    SLEBase(SLEBase&&) = default;
    SLEBase&
    operator=(SLEBase const&) = delete;
    SLEBase&
    operator=(SLEBase&&) = delete;
    SLEBase() = delete;

    // --- Common interface (always available) ---

    /**
     * Returns true if the ledger entry exists
     */
    [[nodiscard]] bool
    exists() const
    {
        return sle_ != nullptr;
    }

    /**
     * Explicit conversion to bool for convenient existence checking
     */
    explicit
    operator bool() const
    {
        return exists();
    }

    /**
     * Returns the underlying SLE for read access
     */
    [[nodiscard]] SLE::const_pointer
    sle() const
    {
        return sle_;
    }

    /**
     * Returns the keylet identifying this entry.
     *
     * Writable wrappers keep the keylet they were built from, so it is valid
     * even before newSLE(). Read-only wrappers derive it from the entry, which
     * must therefore exist.
     */
    [[nodiscard]] Keylet
    keylet() const
    {
        if constexpr (kIsWritable)
            return key_;
        else
        {
            XRPL_ASSERT(exists(), "xrpl::SLEBase::keylet : exists");
            return Keylet(sle_->getType(), sle_->key());
        }
    }

    /**
     * Returns the ledger key of this entry. See keylet() for validity.
     */
    [[nodiscard]] uint256
    key() const
    {
        return keylet().key;
    }

    /**
     * Returns the read view (always available; ApplyView inherits ReadView)
     */
    [[nodiscard]] ReadView const&
    readView() const
    {
        return view_;
    }

    /**
     * Const dereference operators (always available)
     */
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

    /**
     * Returns a mutable SLE for write operations
     */
    [[nodiscard]] sle_ptr_type const&
    mutableSle() const
        requires kIsWritable
    {
        return sle_;
    }

    /**
     * Returns true if this wrapper supports write operations
     */
    [[nodiscard]] bool
    canModify() const
        requires kIsWritable
    {
        return sle_ != nullptr;
    }

    /**
     * Returns the apply view for write operations
     */
    [[nodiscard]] ApplyView&
    applyView() const
        requires kIsWritable
    {
        return view_;
    }

    /**
     * Mutable dereference operators
     */
    STLedgerEntry*
    operator->()
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::operator-> : can modify");
        return sle_.get();
    }

    STLedgerEntry&
    operator*()
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::operator* : can modify");
        return *sle_;
    }

    void
    insert()
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::insert : can modify");
        view_.insert(sle_);
    }

    void
    erase()
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::erase : can modify");
        view_.erase(sle_);
    }

    void
    update()
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::update : can modify");
        view_.update(sle_);
    }

    void
    newSLE()
        requires kIsWritable
    {
        XRPL_ASSERT(!canModify(), "xrpl::SLEBase::newSLE : no existing SLE");
        sle_ = std::make_shared<SLE>(key_);
    }

    [[nodiscard]] beast::Journal
    journal() const
    {
        return j_;
    }

    // --- Constructors that adopt/resolve an SLE (public so the ReadOnlySLE /
    //     WritableSLE aliases and the per-type wrappers can be built directly
    //     from an already-fetched SLE or a keylet). ---

    /**
     * Constructor for read-only context
     */
    explicit SLEBase(
        SLE::const_pointer sle,
        view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : view_(view), sle_(std::move(sle)), j_(j)
    {
    }

    /**
     * Constructor for read-only context (read from view by keylet)
     */
    explicit SLEBase(
        Keylet const& key,
        view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : view_(view), sle_(detail::resolveEntry(view, key)), j_(j)
    {
    }

    /**
     * Converting constructor: writable → read-only.
     * Enables implicit conversion from SLEBase<ApplyView> to
     * SLEBase<ReadView>, so functions taking ReadOnlySLE const& can accept
     * WritableSLE.
     */
    template <WritableView OtherViewT>
    SLEBase(SLEBase<OtherViewT> const& other)
        requires(!kIsWritable)
        : view_(other.readView()), sle_(other.sle()), j_(other.journal())
    {
    }

    /**
     * Constructor for writable context (peek from view by keylet)
     */
    explicit SLEBase(
        Keylet const& key,
        ApplyView& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : view_(view), key_(key), sle_(view_.peek(key)), j_(j)
    {
    }

    /**
     * Constructor for writable context carrying the applying transaction
     * (peek from view by keylet).
     */
    explicit SLEBase(
        Keylet const& key,
        ApplyViewContext ctx,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : view_(ctx.view), key_(key), sle_(view_.peek(key)), j_(j)
    {
    }

protected:
    view_ref_type view_;

    // Keylet is only meaningful for writable views, which need it to build an
    // SLE that does not exist yet; read-only wrappers derive it from the entry.
    struct Empty
    {
    };
    [[no_unique_address]]
    std::conditional_t<kIsWritable, Keylet, Empty> key_{};

    sle_ptr_type sle_{};
    beast::Journal j_;
};

/**
 * Generic (any-entry-type) SLE handles.
 *
 * Use these when the concrete ledger entry type is not known at a given site;
 * otherwise prefer the per-type wrappers (e.g. AccountRootEntry.h).
 *
 *   SLE::const_pointer / SLE::const_ref  ->  ReadOnlySLE
 *   SLE::pointer       / SLE::ref        ->  WritableSLE
 */
using ReadOnlySLE = SLEBase<ReadView>;
using WritableSLE = SLEBase<ApplyView>;

}  // namespace xrpl
