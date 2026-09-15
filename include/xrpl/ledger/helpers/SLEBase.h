#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace xrpl {

// Concept to distinguish read-only vs writable view types
template <typename V>
concept IsWritableView = std::derived_from<V, ApplyView>;

namespace detail {

/**
 * Resolves a keylet for a read-only entry.
 *
 * ReadView::read() on an ApplyView returns the underlying ledger's entry
 * whenever the view is not already tracking one, while peek() installs the
 * view's own copy and returns that. A read-only entry built with read()
 * would therefore hold an SLE that goes stale the moment anything peeks the
 * same key and modifies it. Resolve through peek() whenever the view really is
 * an ApplyView, so every entry over that view shares one SLE.
 *
 * @note The const_cast is what makes reaching ApplyView::peek() possible, and
 *       it is safe only because xrpld never instantiates a ReadView as a
 *       genuinely const object -- every view is a non-const object that some
 *       call sites merely observe through a const reference. If an actually
 *       const-qualified view type is ever introduced (an immutable snapshot,
 *       say), this becomes undefined behavior and must be revisited.
 *
 * @note Consequently a "read-only" entry over an ApplyView is not free of
 *       side effects: peek() installs an Action::Cache entry in the apply
 *       state table. That is benign for transaction metadata -- Cache entries
 *       are skipped in ApplyStateTable::apply(), ::visit() and in metadata
 *       generation -- but it does cost one deep SLE copy on first touch.
 */
inline SLE::const_pointer
resolveEntry(ReadView const& view, Keylet const& key)
{
    // Views are never const objects; the read-only entry only holds a const
    // reference because it does not itself modify the view.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    if (auto const applyView = dynamic_cast<ApplyView*>(const_cast<ReadView*>(&view)))
        return applyView->peek(key);
    return view.read(key);
}

}  // namespace detail

/**
 * View-parameterized base class for all ledger entries.
 *
 * SLEBase<ReadView>  — read-only:  holds shared_ptr<SLE const> + ReadView const&
 * SLEBase<ApplyView> — writable:   holds shared_ptr<SLE> + ApplyView& + Keylet,
 *                                   plus insert/update/erase operations
 *
 * Write-only members are gated by `requires` clauses, providing compile-time
 * guarantees that read-only entries cannot mutate state.
 *
 * @tparam EntryType the ledger entry type this entry is statically bound to.
 * Derived per-type entries pass their own type (e.g. ltACCOUNT_ROOT); the
 * generic ReadOnlySLE / WritableSLE aliases leave it at ltANY, which opts out
 * of the static type check. Binding the type here is what keeps an entry for
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
    static constexpr bool kIsWritable = IsWritableView<ViewT>;

    // The ledger entry type this entry is bound to, and whether that binding
    // is meaningful (ltANY means "any type", i.e. no static check).
    static constexpr LedgerEntryType kEntryType = EntryType;
    static constexpr bool kIsTyped = (EntryType != ltANY);

    // SLE pointer type: mutable for writable views, const for read-only
    using SlePtrType = std::conditional_t<kIsWritable, SLE::pointer, SLE::const_pointer>;

    // View reference type: ApplyView& for writable, ReadView const& for
    // read-only
    using ViewRefType = std::conditional_t<kIsWritable, ApplyView&, ReadView const&>;

    // Non-virtual by design: these entries are parameterized on the view and
    // entry type, never used polymorphically through a base pointer. A vptr
    // would be 8 bytes of pure overhead on a type meant to be as cheap as the
    // shared_ptr it wraps. See the static_assert below the class.
    //
    // The destructor is public because the ReadOnlySLE / WritableSLE aliases
    // name this class directly and are used as value types. Since it is not
    // virtual, never delete a derived entry through an SLEBase*.
    ~SLEBase() = default;

    SLEBase(SLEBase const&)
        requires(!kIsWritable)
    = default;
    SLEBase(SLEBase&&) = default;
    SLEBase&
    operator=(SLEBase const&) = delete;
    SLEBase&
    operator=(SLEBase&&) = delete;
    SLEBase() = delete;

    // --- Constructors that adopt/resolve an SLE (public so the ReadOnlySLE /
    //     WritableSLE aliases and the per-type entries can be built directly
    //     from a keylet, or -- read-only only -- from an already-fetched
    //     SLE). ---

    /**
     * Constructor for read-only context (adopt an already-fetched SLE).
     *
     * There is deliberately no writable equivalent: a writable entry needs
     * a Keylet so that newSLE() can still build an entry when none exists,
     * and that cannot be recovered from a null SLE.
     */
    explicit SLEBase(
        SLE::const_pointer sle,
        ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : view_(view), sle_(std::move(sle)), j_(j)
    {
        XRPL_ASSERT(
            !kIsTyped || !sle_ || sle_->getType() == kEntryType,
            "xrpl::SLEBase::SLEBase : adopted SLE matches bound entry type");
    }

    /**
     * Constructor for read-only context (read from view by keylet)
     */
    explicit SLEBase(
        Keylet const& key,
        ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : view_(view), sle_(detail::resolveEntry(view, key)), j_(j)
    {
        XRPL_ASSERT(
            !kIsTyped || key.type == kEntryType,
            "xrpl::SLEBase::SLEBase : keylet matches bound entry type");
    }

    /**
     * Converting constructor: writable → read-only.
     *
     * Enables implicit conversion from SLEBase<ApplyView> to
     * SLEBase<ReadView>, so functions taking ReadOnlySLE const& can accept
     * WritableSLE.
     *
     * Constrained to the same entry type (or to a ltANY target, i.e. widening
     * a typed entry to a generic ReadOnlySLE). The constraint is load-bearing:
     * this constructor is inherited into every per-type entry, and unconstrained
     * it would bind any writable entry that slices to SLEBase, so an OfferEntryW
     * would convert to an AccountRootEntryR with no cast at the call site.
     */
    template <typename OtherViewT, LedgerEntryType OtherType>
    SLEBase(SLEBase<OtherViewT, OtherType> const& other)
        requires(!kIsWritable && IsWritableView<OtherViewT> &&
                 (OtherType == EntryType || EntryType == ltANY))
        : view_(other.readView()), sle_(other.rawSle()), j_(other.journal())
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
            "xrpl::SLEBase::SLEBase : keylet matches bound entry type");
    }

    /**
     * Constructor for writable context, for call sites that hold an
     * ApplyViewContext (peek from ctx.view by keylet).
     *
     * ctx.tx is not retained: this exists purely so transactors can pass the
     * context they already have instead of spelling out ctx.view. If an entry
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
     * Returns the underlying SLE for read access.
     *
     * Prefer operator-> / operator* for field access; this is for the call
     * sites that need the shared_ptr itself.
     */
    [[nodiscard]] SLE::const_pointer
    rawSle() const
    {
        return sle_;
    }

    /**
     * Returns the ledger entry type of this entry.
     *
     * For a per-type entry this is kEntryType, known at compile time and
     * valid whether or not the entry exists. Only the generic ReadOnlySLE /
     * WritableSLE aliases have to read it back out of the SLE.
     *
     * @throws std::logic_error for a generic (ltANY) entry if exists() is
     *         false.
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
            if (!exists())
                Throw<std::logic_error>("xrpl::SLEBase::type : entry does not exist");
            return sle_->getType();
        }
    }

    /**
     * Returns the keylet identifying this entry.
     *
     * Writable entries keep the keylet they were built from, so it is valid
     * even before newSLE(). Read-only entries derive it from the SLE, which
     * must therefore exist.
     *
     * @throws std::logic_error for a read-only entry if exists() is false.
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
            if (!exists())
                Throw<std::logic_error>("xrpl::SLEBase::keylet : entry does not exist");
            // Take the type from the SLE, not from kEntryType: the adopt-SLE
            // constructor's type check is assert-only, so a Release build can
            // be holding an SLE whose type disagrees with the binding, and the
            // SLE is the one telling the truth.
            return Keylet(sle_->getType(), sle_->key());
        }
    }

    /**
     * Returns the ledger key of this entry.
     *
     * @throws std::logic_error same as keylet(): for read-only entries,
     *         if exists() is false.
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
     *
     * @throws std::logic_error if exists() is false.
     */
    STLedgerEntry const*
    operator->() const
    {
        if (!exists())
            Throw<std::logic_error>("xrpl::SLEBase::operator-> : entry does not exist");
        return sle_.get();
    }

    STLedgerEntry const&
    operator*() const
    {
        if (!exists())
            Throw<std::logic_error>("xrpl::SLEBase::operator* : entry does not exist");
        return *sle_;
    }

    // --- Writable interface (compile-time gated) ---
    //
    // Everything that hands out mutable access (or mutates) is non-const, so
    // that a `FooEntryW const&` is as inert as a `FooEntryR`. Use readView()
    // when a const entry only needs to inspect the view.

    /**
     * Returns the underlying SLE for write access.
     *
     * Prefer operator-> / operator* for field access; this is for the call
     * sites that need the shared_ptr itself.
     */
    [[nodiscard]] SlePtrType const&
    mutableRawSle()
        requires kIsWritable
    {
        return sle_;
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
     *
     * @throws std::logic_error if exists() is false.
     */
    STLedgerEntry*
    operator->()
        requires kIsWritable
    {
        if (!exists())
            Throw<std::logic_error>("xrpl::SLEBase::operator-> : entry does not exist");
        return sle_.get();
    }

    STLedgerEntry&
    operator*()
        requires kIsWritable
    {
        if (!exists())
            Throw<std::logic_error>("xrpl::SLEBase::operator* : entry does not exist");
        return *sle_;
    }

    /**
     * Inserts the entry into the view.
     *
     * @throws std::logic_error if exists() is false.
     */
    void
    insert()
        requires kIsWritable
    {
        if (!exists())
            Throw<std::logic_error>("xrpl::SLEBase::insert : entry does not exist");
        view_.insert(sle_);
    }

    /**
     * Erases the entry from the view.
     *
     * Drops the SLE afterwards, so the entry reports !exists() and any
     * further use trips an assertion here rather than either throwing from
     * deep inside ApplyStateTable or -- worse -- silently succeeding. For an
     * entry that already existed, ApplyStateTable::erase keeps holding this
     * exact SLE and builds the DeletedNode's FinalFields from it, so a write
     * through the entry after erase() would land in transaction metadata
     * with no diagnostic at all.
     *
     * @throws std::logic_error if exists() is false.
     */
    void
    erase()
        requires kIsWritable
    {
        if (!exists())
            Throw<std::logic_error>("xrpl::SLEBase::erase : entry does not exist");
        view_.erase(sle_);
        sle_ = nullptr;
    }

    /**
     * @throws std::logic_error if exists() is false.
     */
    void
    update()
        requires kIsWritable
    {
        if (!exists())
            Throw<std::logic_error>("xrpl::SLEBase::update : entry does not exist");
        view_.update(sle_);
    }

    /**
     * @throws std::logic_error if exists() is true: newSLE() would otherwise
     *         silently discard the SLE already held.
     */
    void
    newSLE()
        requires kIsWritable
    {
        if (exists())
            Throw<std::logic_error>("xrpl::SLEBase::newSLE : entry already exists");
        sle_ = std::make_shared<SLE>(key_);
    }

    [[nodiscard]] beast::Journal
    journal() const
    {
        return j_;
    }

protected:
    ViewRefType view_;

    // Keylet is only meaningful for writable views, which need it to build an
    // SLE that does not exist yet; read-only entries derive it from the SLE.
    struct Empty
    {
    };

    // No default member initializer: Keylet is not default-constructible, so
    // every writable constructor must initialize key_ explicitly.
    [[no_unique_address]]
    std::conditional_t<kIsWritable, Keylet, Empty> key_;

    SlePtrType sle_{};
    beast::Journal j_;
};

/**
 * Generic (any-entry-type) SLE entries.
 *
 * Use these when the concrete ledger entry type is not known at a given site;
 * otherwise prefer the per-type entries (e.g. AccountRootEntry.h), which
 * additionally enforce the entry type at compile time.
 *
 *   SLE::const_pointer / SLE::const_ref  ->  ReadOnlySLE
 *   SLE::pointer       / SLE::ref        ->  WritableSLE
 */
using ReadOnlySLE = SLEBase<ReadView>;
using WritableSLE = SLEBase<ApplyView>;

static_assert(
    !std::is_polymorphic_v<ReadOnlySLE> && !std::is_polymorphic_v<WritableSLE>,
    "SLEBase must stay a thin value type; it must not acquire a vtable");

}  // namespace xrpl
