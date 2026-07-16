#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>  // increaseOwnerCount, accountReserve
#include <xrpl/ledger/helpers/DirectoryHelpers.h>    // describeOwnerDir
#include <xrpl/ledger/helpers/SponsorHelpers.h>      // addSponsorToLedgerEntry, checkReserve
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>  // keylet::account, keylet::ownerDir
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

// Concept to distinguish read-only vs writable view types
template <typename V>
concept WritableView = std::derived_from<V, ApplyView>;

/**
 * Describes one directory a ledger entry is linked into, for create()/destroy().
 *
 * @param owner the account whose owner directory this is (the directory is
 *              keylet::ownerDir(owner)).
 * @param node the field on the entry holding this directory's page index
 *             (sfOwnerNode, sfDestinationNode, sfSubjectNode, ...).
 * @param countsToward whether linking here consumes `owner`'s OwnerCount /
 *                      reserve (true for the owning account, false for
 *                      auxiliary links such as a destination's tracking
 *                      directory).
 */
struct OwnerDirLink
{
    AccountID owner;
    SField const* node{} {};
    bool countsToward{} {};
};

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

    // View reference type: ApplyView& for writable, ReadView const& for read-only
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

    /**
     * The field holding the account that owns this entry (sfAccount, sfOwner,
     * sfIssuer, ...).
     *
     * Single-directory entry types override this to name their owning-account
     * field; the default ownerDirs() then links a single owner directory
     * keyed on it. Types that live in multiple directories override
     * ownerDirs() directly instead. The generic base has no owner.
     */
    [[nodiscard]] virtual SField const&
    ownerField() const
    {
        UNREACHABLE("xrpl::SLEBase::ownerField : type does not define an owner field");
        return sfAccount;  // unreachable; present only to satisfy the return type
    }

    /**
     * The directories this entry is linked into, and where each stores its
     * page index.
     *
     * Default: a single owner directory for ownerField()'s account, recorded
     * in sfOwnerNode, counting toward that account's reserve. Types linked
     * into more than one directory (e.g. Check/PayChannel/Escrow's
     * destination directory, Credential's subject directory) override this to
     * list them; only links with `countsToward == true` consume an
     * OwnerCount/reserve slot.
     */
    [[nodiscard]] virtual std::vector<OwnerDirLink>
    ownerDirs() const
    {
        return {{sle_->getAccountID(ownerField()), &sfOwnerNode, /*countsToward=*/true}};
    }

    /**
     * Number of OwnerCount/reserve slots a counted link consumes (default 1).
     * Types whose footprint scales with their contents (e.g. Oracle)
     * override.
     */
    [[nodiscard]] virtual std::uint32_t
    reserveCount() const
    {
        return 1;
    }

    /**
     * Link this entry into each listed owner directory (keylet::ownerDir),
     * and record the assigned page in the link's node field.
     *
     * This is the shared directory-linking primitive used by create() and by
     * the bespoke create() overrides of entries that live in several owner
     * directories. The OwnerDirLink::countsToward flag is ignored here —
     * reserve and OwnerCount accounting is the caller's responsibility.
     * Returns tecDIR_FULL if any directory is full.
     */
    [[nodiscard]] TER
    linkOwnerDirs(std::vector<OwnerDirLink> const& dirs)
        requires kIsWritable
    {
        for (auto const& d : dirs)
        {
            auto const page =
                view_.dirInsert(keylet::ownerDir(d.owner), sle_->key(), describeOwnerDir(d.owner));
            if (!page)
                return tecDIR_FULL;  // LCOV_EXCL_LINE
            sle_->setFieldU64(*d.node, *page);
        }
        return tesSUCCESS;
    }

    /**
     * Unlink this entry from each listed owner directory, using the page
     * stored in each link's node field. Inverse of linkOwnerDirs(). Returns
     * tefBAD_LEDGER if any removal fails.
     */
    [[nodiscard]] TER
    unlinkOwnerDirs(std::vector<OwnerDirLink> const& dirs)
        requires kIsWritable
    {
        for (auto const& d : dirs)
        {
            {
                if (!view_.dirRemove(
                        keylet::ownerDir(d.owner),
                        sle_->getFieldU64(*d.node),
                        sle_->key(),
                        /*keepRoot=*/false))
                    return tefBAD_LEDGER;  // LCOV_EXCL_LINE
            }
        }
        return tesSUCCESS;
    }

    /**
     * Link a freshly-populated entry into its owner directories and insert
     * it.
     *
     * Handles the create-time boilerplate shared by owned ledger entries:
     *   1. reserve check against `ownerReserveBalance` (the owner's pre-fee
     *      XRP balance — pass the transactor's preFeeBalance_ when the entry
     *      is owned by the transaction submitter). Pass std::nullopt to skip
     *      the check entirely, e.g. for entries an internal caller installs
     *      on a pseudo-account (VaultCreate),
     *   2. link into each ownerDirs() directory, recording the page in its
     *      node field,
     *   3. bump the OwnerCount of each counted owner by reserveCount(),
     *   4. insert the entry into the view.
     *
     * The caller must have already called newSLE() and populated the entry's
     * domain fields (in particular the account fields ownerDirs() reads).
     */
    [[nodiscard]] TER
    create(std::optional<XRPAmount> ownerReserveBalance)
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::create : can modify");
        auto const dirs = ownerDirs();
        Adjustment const adj{.ownerCountDelta = static_cast<std::int32_t>(reserveCount())};

        // The reserve sponsor covers only the transaction submitter's own
        // objects, so it is resolved from the counted owner (yielding no sponsor
        // when the owner is a pseudo-account or otherwise not the submitter).
        // Null unless the wrapper was built with an ApplyViewContext (a create
        // path) and the transaction carries a reserve sponsor for the owner.
        SLE::pointer sponsorSle;

        for (auto const& d : dirs)
        {
            if (!d.countsToward)
                continue;
            auto const ownerSle = view_.peek(keylet::account(d.owner));
            if (!ownerSle)
                return tecNO_ENTRY;  // LCOV_EXCL_LINE
            if (tx_ != nullptr != nullptr)
            {
                auto const sponsorExp =
                    getEffectiveTxReserveSponsor(ApplyViewContext{view_, *tx_}, ownerSle);
                if (!sponsorExp)
                    return sponsorExp.error();  // LCOV_EXCL_LINE
                sponsorSle = *sponsorExp;
            }
            if (!ownerReserveBalance)
                continue;
            // Route the reserve check through checkReserve() so a reserve
            // sponsor is honored; otherwise fall back to the account's own
            // reserve.
            if (tx_ != nullptr != nullptr)
            {
                if (auto const ret = checkReserve(
                        ApplyViewContext{view_, *tx_},
                        ownerSle,
                        *ownerReserveBalance,
                        sponsorSle,
                        adj,
                        j_);
                    !isTesSuccess(ret))
                    return ret;
            }
            else if (*ownerReserveBalance < accountReserve(view_, ownerSle, j_, adj))
            {
                {
                    {
                        {
                            return tecINSUFFICIENT_RESERVE;
                        }
                    }
                }
            }
        }

        if (auto const ter = linkOwnerDirs(dirs); !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE

        for (auto const& d : dirs)
        {
            {
                if (d.countsToward)
                {
                    {
                        increaseOwnerCount(
                            view_,
                            view_.peek(keylet::account(d.owner)),
                            sponsorSle,
                            reserveCount(),
                            j_);
                    }
                }
            }
        }

        // Stamp the reserve sponsor (if any) onto the new entry so that a later
        // delete refunds the sponsor rather than the owner. A no-op when
        // sponsorSle is null.
        if (tx_ != nullptr != nullptr)
            addSponsorToLedgerEntry(sle_, sponsorSle);

        view_.insert(sle_);
        return tesSUCCESS;
    }

    /**
     * Unlink an owned entry from its directories and erase it.
     *
     * Inverse of create(): removes the entry from each ownerDirs() directory
     * (using the stored node fields), decrements each counted owner's
     * OwnerCount by reserveCount(), and erases the entry.
     */
    [[nodiscard]] TER
    destroy()
        requires kIsWritable
    {
        XRPL_ASSERT(canModify(), "xrpl::SLEBase::destroy : can modify");
        auto const dirs = ownerDirs();

        if (auto const ter = unlinkOwnerDirs(dirs); !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE

        // decreaseOwnerCountForObject derives the reserve sponsor (if any) from
        // the entry's sfSponsor field, refunding the sponsor rather than the
        // owner when the object was sponsored.
        for (auto const& d : dirs)
        {
            {
                if (d.countsToward)
                {
                    {
                        if (auto ownerSle = view_.peek(keylet::account(d.owner)))
                            decreaseOwnerCountForObject(view_, ownerSle, sle_, reserveCount(), j_);
                    }
                }
            }
        }

        view_.erase(sle_);
        return tesSUCCESS;
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
        ReadView const& view,
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
        ReadView const& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : view_(view), sle_(view.read(key)), j_(j)
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
     * Constructor for writable context (from existing SLE)
     */
    explicit SLEBase(
        std::shared_ptr<SLE> sle,
        ApplyView& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : view_(view)
        , key_(sle ? Keylet(sle->getType(), sle->key()) : Keylet(ltANY, uint256{}))
        , sle_(std::move(sle))
        , j_(j)
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
     * (from existing SLE). Providing the ApplyViewContext lets create()
     * perform reserve-sponsorship-aware accounting.
     */
    explicit SLEBase(
        std::shared_ptr<SLE> sle,
        ApplyViewContext ctx,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : view_(ctx.view)
        , key_(sle ? Keylet(sle->getType(), sle->key()) : Keylet(ltANY, uint256{}))
        , sle_(std::move(sle))
        , j_(j)
        , tx_(&ctx.tx)
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
        : view_(ctx.view), key_(key), sle_(view_.peek(key)), j_(j), tx_(&ctx.tx)
    {
    }

protected:
    view_ref_type view_{};

    // Keylet is only meaningful for writable views, but we conditionally
    // include it to avoid wasting space in read-only wrappers.
    struct Empty
    {
    };
    [[no_unique_address]]
    std::conditional_t<kIsWritable, Keylet, Empty> key_{};

    sle_ptr_type sle_{};
    beast::Journal j_;

    // The applying transaction, when the wrapper was constructed from an
    // ApplyViewContext. Only meaningful for writable wrappers on a create path;
    // null otherwise. Enables reserve-sponsorship-aware create().
    STTx const* tx_ = nullptr;
};

/**
 * Generic (any-entry-type) SLE handles.
 *
 * Use these when the concrete ledger entry type is not known at a given site;
 * otherwise prefer the per-type wrappers in SLEWrappers.h.
 *
 *   SLE::const_pointer / SLE::const_ref  ->  ReadOnlySLE
 *   SLE::pointer       / SLE::ref        ->  WritableSLE
 */
using ReadOnlySLE = SLEBase<ReadView>;
using WritableSLE = SLEBase<ApplyView>;

}  // namespace xrpl
