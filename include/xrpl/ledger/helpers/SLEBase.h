#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <concepts>

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
 *
 * @note The const_cast is what makes reaching ApplyView::peek() possible, and
 *       it is safe only because xrpld never instantiates a ReadView as a
 *       genuinely const object -- every view is a non-const object that some
 *       call sites merely observe through a const reference. If an actually
 *       const-qualified view type is ever introduced (an immutable snapshot,
 *       say), this becomes undefined behaviour and must be revisited.
 *
 * @note Consequently a "read-only" wrapper over an ApplyView is not free of
 *       side effects: peek() installs an Action::Cache entry in the apply
 *       state table. That is benign for transaction metadata -- Cache entries
 *       are skipped in ApplyStateTable::apply(), ::visit() and in metadata
 *       generation -- but it does cost one deep SLE copy on first touch.
 */
inline SLE::const_pointer
resolveEntry(ReadView const& view, Keylet const& key)
{
    // Views are never const objects; the read-only wrapper only holds a const
    // reference because it does not itself modify the view.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
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
 * @tparam EntryType the ledger entry type this wrapper is statically bound to.
 * Derived per-type wrappers pass their own type (e.g. ltACCOUNT_ROOT); the
 * generic ReadOnlySLE / WritableSLE aliases leave it at ltANY, which opts out
 * of the static type check. Binding the type here is what keeps a wrapper for
 * one entry type from being constructed or converted from another -- see the
 * converting constructor below.
 *
 * Derived classes should provide domain-specific accessors that hide
 * implementation details of the underlying ledger entry format.
 */
template <typename ViewT, LedgerEntryType EntryType = ltANY>
class SLEBase
{
public:
    static constexpr bool kIsWritable = WritableView<ViewT>;

    // The ledger entry type this wrapper is bound to, and whether that binding
    // is meaningful (ltANY means "any type", i.e. no static check).
    static constexpr LedgerEntryType kEntryType = EntryType;
    static constexpr bool kIsTyped = (EntryType != ltANY);

    // SLE pointer type: mutable for writable views, const for read-only
    using sle_ptr_type = std::conditional_t<kIsWritable, std::shared_ptr<SLE>, SLE::const_pointer>;

    // View reference type: ApplyView& for writable, ReadView const& for
    // read-only
    using view_ref_type = std::conditional_t<kIsWritable, ApplyView&, ReadView const&>;

    // Non-virtual by design: these wrappers are parameterized on the view and
    // entry type, never used polymorphically through a base pointer. A vptr
    // would be 8 bytes of pure overhead on a type meant to be as cheap as the
    // shared_ptr it wraps. See the static_assert below the class.
    ~SLEBase() = default;

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
     * Returns the ledger entry type of this entry.
     *
     * For a per-type wrapper this is kEntryType, known at compile time and
     * valid whether or not the entry exists. Only the generic ReadOnlySLE /
     * WritableSLE aliases have to read it back out of the entry.
     *
     * @pre For generic (ltANY) wrappers, exists() must be true. The check is
     *      assert-only and is compiled out in Release builds.
     */
    [[nodiscard]] LedgerEntryType
    type() const
    {
        if constexpr (kIsTyped)
        {
            return kEntryType;
        }
        else
        {
            XRPL_ASSERT(exists(), "xrpl::SLEBase::type : exists");
            return sle_->getType();
        }
    }

    /**
     * Returns the keylet identifying this entry.
     *
     * Writable wrappers keep the keylet they were built from, so it is valid
     * even before newSLE(). Read-only wrappers derive it from the entry, which
     * must therefore exist.
     *
     * @pre For read-only wrappers, exists() must be true. Violating this
     *      dereferences a null pointer; the check is assert-only and is
     *      compiled out in Release builds.
     */
    [[nodiscard]] Keylet
    keylet() const
    {
        if constexpr (kIsWritable)
        {
            return key_;
        }
        else
        {
            XRPL_ASSERT(exists(), "xrpl::SLEBase::keylet : exists");
            return Keylet(type(), sle_->key());
        }
    }

    /**
     * Returns the ledger key of this entry.
     *
     * @pre Same as keylet(): for read-only wrappers exists() must be true, and
     *      the check is compiled out in Release builds.
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
    //
    // Everything that hands out mutable access (or mutates) is non-const, so
    // that a `WFooEntry const&` is as inert as a `RFooEntry`. Use readView()
    // when a const wrapper only needs to inspect the view.

    /**
     * Returns a mutable SLE for write operations
     */
    [[nodiscard]] sle_ptr_type const&
    mutableSle()
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
    applyView()
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

    /**
     * Erases the entry from the view.
     *
     * Drops the SLE afterwards, so the wrapper reports !exists() and any
     * further use trips an assertion here rather than either throwing from
     * deep inside ApplyStateTable or -- worse -- silently succeeding. For an
     * entry that already existed, ApplyStateTable::erase keeps holding this
     * exact SLE and builds the DeletedNode's FinalFields from it, so a write
     * through the wrapper after erase() would land in transaction metadata
     * with no diagnostic at all.
     */
    void
    erase()
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::erase : can modify");
        view_.erase(sle_);
        sle_ = nullptr;
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
    //     from a keylet, or -- read-only only -- from an already-fetched
    //     SLE). ---

    /**
     * Constructor for read-only context (adopt an already-fetched SLE).
     *
     * There is deliberately no writable equivalent: a writable wrapper needs
     * a Keylet so that newSLE() can still build an entry when none exists,
     * and that cannot be recovered from a null SLE.
     */
    explicit SLEBase(
        SLE::const_pointer sle,
        view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : view_(view), sle_(std::move(sle)), j_(j)
    {
        XRPL_ASSERT(
            !kIsTyped || !sle_ || sle_->getType() == kEntryType,
            "xrpl::SLEBase::SLEBase : adopted SLE matches wrapper entry type");
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
        XRPL_ASSERT(
            !kIsTyped || key.type == kEntryType,
            "xrpl::SLEBase::SLEBase : keylet matches wrapper entry type");
    }

    /**
     * Converting constructor: writable → read-only.
     *
     * Enables implicit conversion from SLEBase<ApplyView> to
     * SLEBase<ReadView>, so functions taking ReadOnlySLE const& can accept
     * WritableSLE.
     *
     * Constrained to the same entry type (or to a ltANY target, i.e. widening
     * a typed wrapper to a generic ReadOnlySLE). Without that constraint this
     * constructor is inherited into every per-type wrapper and will happily
     * bind any writable wrapper that slices to SLEBase, which would let e.g.
     * a WOfferEntry convert -- implicitly, at a call site, with no cast in
     * sight -- to an RAccountRootEntry.
     */
    template <typename OtherViewT, LedgerEntryType OtherType>
    SLEBase(SLEBase<OtherViewT, OtherType> const& other)
        requires(!kIsWritable && WritableView<OtherViewT> &&
                 (OtherType == EntryType || EntryType == ltANY))
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
        XRPL_ASSERT(
            !kIsTyped || key.type == kEntryType,
            "xrpl::SLEBase::SLEBase : keylet matches wrapper entry type");
    }

    /**
     * Constructor for writable context, for call sites that hold an
     * ApplyViewContext (peek from ctx.view by keylet).
     *
     * ctx.tx is not retained: this exists purely so transactors can pass the
     * context they already have instead of spelling out ctx.view. If a wrapper
     * ever needs the applying transaction, store it here rather than adding
     * another overload.
     */
    explicit SLEBase(
        Keylet const& key,
        ApplyViewContext const& ctx,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : SLEBase(key, ctx.view, j)
    {
    }

protected:
    view_ref_type view_{} {};

    // Keylet is only meaningful for writable views, which need it to build an
    // SLE that does not exist yet; read-only wrappers derive it from the entry.
    struct Empty
    {
    };
    // No default member initializer: Keylet is not default-constructible, so
    // every writable constructor must initialize key_ explicitly.
    [[no_unique_address]]
    std::conditional_t<kIsWritable, Keylet, Empty> key_{} {};

    sle_ptr_type sle_{};
    beast::Journal j_;
};

/**
 * Generic (any-entry-type) SLE handles.
 *
 * Use these when the concrete ledger entry type is not known at a given site;
 * otherwise prefer the per-type wrappers (e.g. AccountRootEntry.h), which
 * additionally enforce the entry type at compile time.
 *
 *   SLE::const_pointer / SLE::const_ref  ->  ReadOnlySLE
 *   SLE::pointer       / SLE::ref        ->  WritableSLE
 */
using ReadOnlySLE = SLEBase<ReadView>;
using WritableSLE = SLEBase<ApplyView>;

static_assert(
    !std::is_polymorphic_v<ReadOnlySLE> && !std::is_polymorphic_v<WritableSLE>,
    "SLEBase is a thin wrapper; it must not acquire a vtable");

}  // namespace xrpl
