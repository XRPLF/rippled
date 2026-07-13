#pragma once

#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

/**
 * Concrete, view-parameterized wrappers for each ledger entry type.
 *
 * Every wrapper derives from SLEBase<ViewT> and, for now, does nothing more
 * than translate the entry's "keylet parts" (the arguments to its keylet::
 * function) into a Keylet that the base class resolves against the view:
 *
 *   AccountRootEntry<ApplyView> acct{id, view};     // peek (writable)
 *   AccountRootEntry<ReadView>  acct{id, readView}; // read  (read-only)
 *
 * Domain-specific accessors will be layered onto each wrapper over time.
 */

// Ordered to match include/xrpl/protocol/detail/ledger_entries.macro.

template <typename ViewT>
class NFTokenOfferEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NFTokenOfferEntry(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenOffer(owner, seq), view, j)
    {
    }

    /**
     * Create an NFToken offer.
     *
     *  An NFToken offer lives in its owner's directory (sfOwnerNode) and in the
     *  NFToken's buy or sell offer directory (sfNFTokenOfferNode), the latter
     *  keyed by the NFToken id rather than an account, so this shadows
     *  SLEBase::create(). The caller must have populated sfOwner, sfNFTokenID
     *  and sfFlags (the lsfSellNFToken bit selects the buy/sell directory).
     *  Mirrors the former createTokenOffer() helper.
     */
    [[nodiscard]] TER
    create(std::optional<XRPAmount> ownerReserveBalance)
        requires SLEBase<ViewT>::kIsWritable
    {
        auto& view = this->applyView();
        auto const& offer = this->mutableSle();
        AccountID const owner = (*offer)[sfOwner];
        auto const acctKeylet = keylet::account(owner);

        if (ownerReserveBalance)
        {
            auto const acct = view.peek(acctKeylet);
            if (!acct)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            if (*ownerReserveBalance <
                accountReserve(view, acct, this->journal(), {.ownerCountDelta = 1}))
                return tecINSUFFICIENT_RESERVE;
        }

        // Always added to the owner's owner directory.
        auto const ownerNode =
            view.dirInsert(keylet::ownerDir(owner), offer->key(), describeOwnerDir(owner));
        if (!ownerNode)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        bool const isSellOffer = offer->isFlag(lsfSellNFToken);
        uint256 const nftokenID = (*offer)[sfNFTokenID];

        // Also added to the token's buy or sell offer directory.
        auto const offerNode = view.dirInsert(
            isSellOffer ? keylet::nftSells(nftokenID) : keylet::nftBuys(nftokenID),
            offer->key(),
            [&nftokenID, isSellOffer](SLE::ref sle) {
                (*sle)[sfFlags] = isSellOffer ? lsfNFTokenSellOffers : lsfNFTokenBuyOffers;
                (*sle)[sfNFTokenID] = nftokenID;
            });
        if (!offerNode)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        (*offer)[sfOwnerNode] = *ownerNode;
        (*offer)[sfNFTokenOfferNode] = *offerNode;

        view.insert(offer);
        // NFToken offers are never reserve-sponsored, so they count against
        // their own owner.
        increaseOwnerCount(view, owner, std::optional<AccountID>{}, 1, this->journal());
        return tesSUCCESS;
    }

    /**
     * Remove an NFToken offer from the ledger (inverse of create()). Mirrors
     *  the former deleteTokenOffer() helper.
     */
    [[nodiscard]] TER
    destroy()
        requires SLEBase<ViewT>::kIsWritable
    {
        auto& view = this->applyView();
        auto const& offer = this->mutableSle();
        AccountID const owner = (*offer)[sfOwner];

        if (!view.dirRemove(keylet::ownerDir(owner), (*offer)[sfOwnerNode], offer->key(), false))
            return tefBAD_LEDGER;

        uint256 const nftokenID = (*offer)[sfNFTokenID];
        if (!view.dirRemove(
                offer->isFlag(lsfSellNFToken) ? keylet::nftSells(nftokenID)
                                              : keylet::nftBuys(nftokenID),
                (*offer)[sfNFTokenOfferNode],
                offer->key(),
                false))
            return tefBAD_LEDGER;

        // NFToken offers are never reserve-sponsored.
        decreaseOwnerCount(view, owner, std::optional<AccountID>{}, 1, this->journal());
        view.erase(offer);
        return tesSUCCESS;
    }
};

template <typename ViewT>
class CheckEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit CheckEntry(
        AccountID const& id,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::check(id, seq), view, j)
    {
    }

    // Owner dir (counts toward the source's reserve) + destination tracking dir
    // (added only for a real, non-self check, matching CheckCreate).
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        auto const owner = this->sle()->getAccountID(sfAccount);
        auto const dest = this->sle()->getAccountID(sfDestination);
        std::vector<OwnerDirLink> dirs{{owner, &sfOwnerNode, /*countsToward=*/true}};
        if (dest != owner)
            dirs.push_back({dest, &sfDestinationNode, /*countsToward=*/false});
        return dirs;
    }
};

template <typename ViewT>
class DIDEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DIDEntry(
        AccountID const& account,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::did(account), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
    }
};

template <typename ViewT>
class NegativeUNLEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NegativeUNLEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::negativeUNL(), view, j)
    {
    }
};

template <typename ViewT>
class NFTokenPageEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NFTokenPageEntry(
        Keylet const& page,
        uint256 const& token,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenPage(page, token), view, j)
    {
    }
};

template <typename ViewT>
class SignerListEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit SignerListEntry(
        AccountID const& account,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::signerList(account), view, j)
    {
    }
};

template <typename ViewT>
class TicketEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit TicketEntry(
        AccountID const& id,
        std::uint32_t ticketSeq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ticket(id, ticketSeq), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
    }
};

template <typename ViewT>
class AccountRootEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AccountRootEntry(
        AccountID const& id,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::account(id), view, j)
    {
    }
};

template <typename ViewT>
class DirectoryNodeEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DirectoryNodeEntry(
        AccountID const& id,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ownerDir(id), view, j)
    {
    }
};

template <typename ViewT>
class AmendmentsEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AmendmentsEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amendments(), view, j)
    {
    }
};

template <typename ViewT>
class LedgerHashesEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LedgerHashesEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::skip(), view, j)
    {
    }
};

template <typename ViewT>
class BridgeEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit BridgeEntry(
        STXChainBridge const& bridge,
        STXChainBridge::ChainType chainType,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::bridge(bridge, chainType), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
    }
};

template <typename ViewT>
class OfferEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit OfferEntry(
        AccountID const& id,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::offer(id, seq), view, j)
    {
    }

    /**
     * Remove an offer from the ledger.
     *
     *  An offer lives in its owner's directory (sfOwnerNode) and in one or more
     *  order-book page directories (sfBookDirectory/sfBookNode, plus the
     *  sfAdditionalBooks of a hybrid domain offer). Those book pages are keyed
     *  by keylet::page rather than keylet::ownerDir, so this shadows
     *  SLEBase::destroy() instead of using the OwnerDirLink model. Mirrors the
     *  former free-function offerDelete().
     */
    [[nodiscard]] TER
    destroy()
        requires SLEBase<ViewT>::kIsWritable
    {
        auto& view = this->applyView();
        auto const& sle = this->mutableSle();
        auto const offerIndex = sle->key();
        auto const owner = sle->getAccountID(sfAccount);

        // Detect legacy directories.
        uint256 const uDirectory = sle->getFieldH256(sfBookDirectory);

        if (!view.dirRemove(
                keylet::ownerDir(owner), sle->getFieldU64(sfOwnerNode), offerIndex, false))
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        if (!view.dirRemove(
                keylet::page(uDirectory), sle->getFieldU64(sfBookNode), offerIndex, false))
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        if (sle->isFieldPresent(sfAdditionalBooks))
        {
            XRPL_ASSERT(
                sle->isFlag(lsfHybrid) && sle->isFieldPresent(sfDomainID),
                "xrpl::OfferEntry::destroy : should be a hybrid domain offer");

            for (auto const& bookDir : sle->getFieldArray(sfAdditionalBooks))
            {
                if (!view.dirRemove(
                        keylet::page(bookDir.getFieldH256(sfBookDirectory)),
                        bookDir.getFieldU64(sfBookNode),
                        offerIndex,
                        false))
                    return tefBAD_LEDGER;  // LCOV_EXCL_LINE
            }
        }

        decreaseOwnerCountForObject(view, owner, sle, 1, this->journal());
        view.erase(sle);
        return tesSUCCESS;
    }
};

template <typename ViewT>
class DepositPreauthEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DepositPreauthEntry(
        AccountID const& owner,
        AccountID const& preauthorized,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::depositPreauth(owner, preauthorized), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
    }
};

template <typename ViewT>
class XChainOwnedClaimIDEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit XChainOwnedClaimIDEntry(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::xChainClaimID(bridge, seq), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
    }
};

template <typename ViewT>
class RippleStateEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit RippleStateEntry(
        AccountID const& id0,
        AccountID const& id1,
        Currency const& currency,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::trustLine(id0, id1, currency), view, j)
    {
    }

    /**
     * Remove a trust line from the ledger.
     *
     *  A trust line sits in both endpoints' directories (sfLowNode/sfHighNode,
     *  not sfOwnerNode) and, unlike single-owner entries, does not adjust
     *  OwnerCount here — trust-line reserve ownership is tracked by the
     *  lsfLow/HighReserve flags and reconciled by the balance-update path. The
     *  low/high accounts are the issuers recorded in sfLowLimit/sfHighLimit.
     *  This shadows SLEBase::destroy(); mirrors the former trustDelete() helper.
     */
    [[nodiscard]] TER
    destroy()
        requires SLEBase<ViewT>::kIsWritable
    {
        auto const& sle = this->mutableSle();
        AccountID const low = sle->getFieldAmount(sfLowLimit).getIssuer();
        AccountID const high = sle->getFieldAmount(sfHighLimit).getIssuer();

        if (auto const ter = this->unlinkOwnerDirs(
                {{low, &sfLowNode, /*countsToward=*/false},
                 {high, &sfHighNode, /*countsToward=*/false}});
            !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE

        this->applyView().erase(sle);
        return tesSUCCESS;
    }
};

template <typename ViewT>
class FeeSettingsEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit FeeSettingsEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::feeSettings(), view, j)
    {
    }
};

template <typename ViewT>
class XChainOwnedCreateAccountClaimIDEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit XChainOwnedCreateAccountClaimIDEntry(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::xChainCreateAccountClaimID(bridge, seq), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
    }
};

template <typename ViewT>
class EscrowEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit EscrowEntry(
        AccountID const& src,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::escrow(src, seq), view, j)
    {
    }

    // Owner dir (counts toward the sender's reserve) plus tracking dirs: the
    // destination's (when not a self-send) and, for IOU escrows, the issuer's
    // (to track the locked balance). MPT escrows track the lock on the issuance
    // object instead, so they take no issuer dir. Mirrors EscrowCreate.
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        auto const& sle = *this->sle();
        AccountID const account = sle.getAccountID(sfAccount);
        AccountID const dest = sle.getAccountID(sfDestination);
        STAmount const amount = sle.getFieldAmount(sfAmount);

        std::vector<OwnerDirLink> dirs;
        dirs.push_back({account, &sfOwnerNode, /*countsToward=*/true});
        if (dest != account)
            dirs.push_back({dest, &sfDestinationNode, /*countsToward=*/false});

        AccountID const issuer = amount.getIssuer();
        if (!isXRP(amount) && issuer != account && issuer != dest && !amount.holds<MPTIssue>())
            dirs.push_back({issuer, &sfIssuerNode, /*countsToward=*/false});
        return dirs;
    }
};

template <typename ViewT>
class PayChannelEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit PayChannelEntry(
        AccountID const& src,
        AccountID const& dst,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::payChannel(src, dst, seq), view, j)
    {
    }

    // Owner dir (counts toward the source's reserve) + destination tracking dir
    // (PaymentChannelCreate forbids dst == src, so both are always present).
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        return {
            {this->sle()->getAccountID(sfAccount), &sfOwnerNode, /*countsToward=*/true},
            {this->sle()->getAccountID(sfDestination), &sfDestinationNode, /*countsToward=*/false}};
    }
};

template <typename ViewT>
class AMMEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AMMEntry(
        Asset const& issue1,
        Asset const& issue2,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amm(issue1, issue2), view, j)
    {
    }

    // The AMM object lives in its own pseudo-account's directory and does not
    // count toward any reserve (pseudo-accounts hold no reserve). The default
    // create() therefore links the directory and inserts without touching an
    // OwnerCount. Mirrors AMMCreate.
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        return {{this->sle()->getAccountID(sfAccount), &sfOwnerNode, /*countsToward=*/false}};
    }

    /**
     * Remove the AMM object from its pseudo-account's directory.
     *
     *  Unlike the default destroy(), this also collapses the pseudo-account's
     *  now-empty owner directory root and reports tecINTERNAL on failure. The
     *  pseudo-account (AccountRoot) itself is erased by the caller. Mirrors
     *  deleteAMMAccountIfEmpty().
     */
    [[nodiscard]] TER
    destroy()
        requires SLEBase<ViewT>::kIsWritable
    {
        auto& view = this->applyView();
        auto const& ammSle = this->mutableSle();
        AccountID const ammAccountID = (*ammSle)[sfAccount];
        auto const ownerDirKeylet = keylet::ownerDir(ammAccountID);

        if (!view.dirRemove(ownerDirKeylet, (*ammSle)[sfOwnerNode], ammSle->key(), false))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (view.exists(ownerDirKeylet) && !view.emptyDirDelete(ownerDirKeylet))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        view.erase(ammSle);
        return tesSUCCESS;
    }
};

template <typename ViewT>
class MPTokenIssuanceEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit MPTokenIssuanceEntry(
        std::uint32_t seq,
        AccountID const& issuer,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::mptokenIssuance(seq, issuer), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfIssuer;
    }
};

template <typename ViewT>
class MPTokenEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit MPTokenEntry(
        MPTID const& issuanceID,
        AccountID const& holder,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::mptoken(issuanceID, holder), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
    }

    /**
     * Create an MPToken for its holder.
     *
     *  Like a trust line, an MPToken is free of reserve until the holder owns
     *  two or more objects — but a reserve sponsor on the transaction must
     *  always cover it. This shadows SLEBase::create() only to decide whether
     *  the reserve is enforced (clearing ownerReserveBalance to skip the check
     *  in the free tier), then defers the sponsor-aware directory link,
     *  OwnerCount bump, sponsor stamp, and insert to the base. Pass std::nullopt
     *  to skip the reserve check outright (internal callers on a settled
     *  account). Mirrors authorizeMPToken.
     */
    [[nodiscard]] TER
    create(std::optional<XRPAmount> ownerReserveBalance)
        requires SLEBase<ViewT>::kIsWritable
    {
        if (ownerReserveBalance)
        {
            auto const owner = this->sle()->getAccountID(sfAccount);
            auto const ownerSle = this->applyView().peek(keylet::account(owner));
            if (!ownerSle)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            bool sponsored = false;
            if (this->tx_)
            {
                auto const sponsorExp = getEffectiveTxReserveSponsor(
                    ApplyViewContext{this->applyView(), *this->tx_}, ownerSle);
                if (!sponsorExp)
                    return sponsorExp.error();  // LCOV_EXCL_LINE
                sponsored = static_cast<bool>(*sponsorExp);
            }

            // Free tier: no sponsor and fewer than two owned objects.
            if (!sponsored && ownerCount(ownerSle, this->journal()) < 2)
                ownerReserveBalance = std::nullopt;
        }
        return SLEBase<ViewT>::create(ownerReserveBalance);
    }
};

template <typename ViewT>
class CredentialEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit CredentialEntry(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credType,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::credential(subject, issuer, credType), view, j)
    {
    }

    // A credential lives in both the issuer's and subject's directories, but is
    // only counted against one owner's reserve at a time: the issuer holds it
    // until the subject accepts (lsfAccepted), after which the subject owns it.
    // A self-issued credential is always owned (and counted) by the issuer.
    // Mirrors CredentialCreate / credentials::deleteSLE.
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        auto const& sle = *this->sle();
        AccountID const issuer = sle.getAccountID(sfIssuer);
        AccountID const subject = sle.getAccountID(sfSubject);
        bool const accepted = sle.isFlag(lsfAccepted);

        std::vector<OwnerDirLink> dirs;
        dirs.push_back({issuer, &sfIssuerNode, /*countsToward=*/!accepted || subject == issuer});
        if (subject != issuer)
            dirs.push_back({subject, &sfSubjectNode, /*countsToward=*/accepted});
        return dirs;
    }
};

template <typename ViewT>
class PermissionedDomainEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit PermissionedDomainEntry(
        AccountID const& account,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::permissionedDomain(account, seq), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfOwner;
    }
};

template <typename ViewT>
class DelegateEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DelegateEntry(
        AccountID const& account,
        AccountID const& authorizedAccount,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::delegate(account, authorizedAccount), view, j)
    {
    }

    // Owner dir (counts toward the delegator's reserve) + the authorized
    // account's dir (so AccountDelete can find inbound delegations; no count).
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        return {
            {this->sle()->getAccountID(sfAccount), &sfOwnerNode, /*countsToward=*/true},
            {this->sle()->getAccountID(sfAuthorize), &sfDestinationNode, /*countsToward=*/false}};
    }
};

template <typename ViewT>
class VaultEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit VaultEntry(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::vault(owner, seq), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfOwner;
    }

    // A vault charges its owner for two reserve slots: the Vault object itself
    // and the pseudo-account it owns (the pseudo-account is created/destroyed by
    // the transactor around create()/destroy()). Only the Vault object lives in
    // the owner's directory. Mirrors VaultCreate / VaultDelete.
    [[nodiscard]] std::uint32_t
    reserveCount() const override
    {
        return 2;
    }
};

template <typename ViewT>
class LoanBrokerEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LoanBrokerEntry(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::loanBroker(owner, seq), view, j)
    {
    }

    // A loan broker lives in its owner's directory (counts toward the owner's
    // reserve) and, for tracking, in its backing vault's pseudo-account
    // directory (does not count). The owner is charged two reserve slots: the
    // broker object and its own pseudo-account (created/destroyed by the
    // transactor around create()/destroy()). Mirrors LoanBrokerSet/Delete.
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        auto const& sle = *this->sle();
        std::vector<OwnerDirLink> dirs{
            {sle.getAccountID(sfOwner), &sfOwnerNode, /*countsToward=*/true}};
        if (auto const sleVault = this->readView().read(keylet::vault(sle.getFieldH256(sfVaultID))))
            dirs.push_back(
                {sleVault->getAccountID(sfAccount), &sfVaultNode, /*countsToward=*/false});
        return dirs;
    }

    [[nodiscard]] std::uint32_t
    reserveCount() const override
    {
        return 2;
    }
};

template <typename ViewT>
class LoanEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LoanEntry(
        uint256 const& loanBrokerID,
        std::uint32_t loanSeq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::loan(loanBrokerID, loanSeq), view, j)
    {
    }

    /**
     * Materialize the loan and link it into both directories it lives in: its
     *  LoanBroker's pseudo-account directory (sfLoanBrokerNode) and its
     *  borrower's directory (sfOwnerNode).
     *
     *  A loan carries no reserve of its own, and its two OwnerCount effects — on
     *  the borrower's account and on the LoanBroker *object* (which counts
     *  outstanding loans) — are managed by the transactor, not here, because
     *  they must be sequenced against other borrower-account changes. This
     *  therefore shadows SLEBase::create() to only insert and link. Requires
     *  sfBorrower and sfLoanBrokerID to be populated. Mirrors LoanSet.
     */
    [[nodiscard]] TER
    create()
        requires SLEBase<ViewT>::kIsWritable
    {
        auto const dirs = loanDirs();
        if (dirs.empty())
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        this->applyView().insert(this->mutableSle());
        return this->linkOwnerDirs(dirs);
    }

    /**
     * Unlink the loan from both directories and erase it (inverse of the
     *  directory work in create()). OwnerCount decrements on the borrower and
     *  LoanBroker object are handled by the transactor. Mirrors LoanDelete.
     */
    [[nodiscard]] TER
    destroy()
        requires SLEBase<ViewT>::kIsWritable
    {
        auto const dirs = loanDirs();
        if (dirs.empty())
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        if (auto const ter = this->unlinkOwnerDirs(dirs); !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE

        this->applyView().erase(this->mutableSle());
        return tesSUCCESS;
    }

private:
    // The two owner directories a loan lives in: its LoanBroker's pseudo-account
    // directory (sfLoanBrokerNode) and its borrower's directory (sfOwnerNode).
    // Empty if the backing broker cannot be resolved.
    [[nodiscard]] std::vector<OwnerDirLink>
    loanDirs() const
        requires SLEBase<ViewT>::kIsWritable
    {
        auto const& loan = *this->sle();
        auto const broker =
            this->readView().read(keylet::loanBroker(loan.getFieldH256(sfLoanBrokerID)));
        if (!broker)
            return {};  // LCOV_EXCL_LINE
        return {
            {broker->getAccountID(sfAccount), &sfLoanBrokerNode, /*countsToward=*/false},
            {loan.getAccountID(sfBorrower), &sfOwnerNode, /*countsToward=*/false}};
    }
};

}  // namespace xrpl
