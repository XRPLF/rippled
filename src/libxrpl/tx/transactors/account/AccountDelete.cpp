/** @file
 *  Implements AccountDelete: the only mechanism by which an account can be
 *  permanently erased from the XRP Ledger.  Walks the four-phase pipeline
 *  (checkExtraFeatures → preflight → preclaim → doApply), cleans up every
 *  owned ledger object, sweeps the remaining XRP balance to a destination,
 *  and finally erases the source account root SLE.
 */
#include <xrpl/tx/transactors/account/AccountDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/account/SignerListSet.h>
#include <xrpl/tx/transactors/delegate/DelegateSet.h>
#include <xrpl/tx/transactors/did/DIDDelete.h>
#include <xrpl/tx/transactors/oracle/OracleDelete.h>
#include <xrpl/tx/transactors/payment/DepositPreauth.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace xrpl {

bool
AccountDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfCredentialIDs) || ctx.rules.enabled(featureCredentials);
}

NotTEC
AccountDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
    {
        // An account cannot be deleted and give itself the resulting XRP.
        return temDST_IS_SRC;
    }

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

XRPAmount
AccountDelete::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return calculateOwnerReserveFee(view, tx);
}

namespace {

/** Uniform function-pointer type for per-type ledger object deleters.
 *
 *  Every adapter registered in `nonObligationDeleter` must match this
 *  signature.  Adapters that do not need a particular parameter simply
 *  omit its name to silence unused-parameter warnings.
 *
 *  @param registry  Service registry forwarded to deleters that require it
 *      (e.g. `SignerListSet::removeFromLedger`).
 *  @param view      Mutable apply view; deleters perform their erasure here.
 *  @param account   Owner account whose object is being removed.
 *  @param delIndex  Ledger key of the entry being deleted.
 *  @param sleDel    Shared pointer to the SLE being deleted.
 *  @param j         Journal for diagnostic logging.
 *  @return `tesSUCCESS` on clean removal; a `tef*` sentinel (e.g.
 *      `tefBAD_LEDGER`) on ledger-corruption detected during deletion.
 */
using DeleterFuncPtr = TER (*)(
    ServiceRegistry& registry,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j);

// --- Deleter adapters ---
// Each adapter below adapts a feature-specific static deletion helper to
// the uniform DeleterFuncPtr signature.  Unused parameters are intentionally
// unnamed to suppress compiler warnings.

/** Adapter: delete an offer via `offerDelete(view, sleDel, j)`. */
TER
offerDelete(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return offerDelete(view, sleDel, j);
}

/** Adapter: remove a signer list via `SignerListSet::removeFromLedger`. */
TER
removeSignersFromLedger(
    ServiceRegistry& registry,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return SignerListSet::removeFromLedger(registry, view, account, j);
}

/** Adapter: remove a ticket via `Transactor::ticketDelete`. */
TER
removeTicketFromLedger(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const&,
    beast::Journal j)
{
    return Transactor::ticketDelete(view, account, delIndex, j);
}

/** Adapter: remove a deposit-preauth entry via `DepositPreauth::removeFromLedger`. */
TER
removeDepositPreauthFromLedger(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const&,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const&,
    beast::Journal j)
{
    return DepositPreauth::removeFromLedger(view, delIndex, j);
}

/** Adapter: remove an NFToken offer via `nft::deleteTokenOffer`.
 *
 *  Returns `tefBAD_LEDGER` if the offer cannot be found in the token-offer
 *  directory — a condition that indicates ledger corruption because
 *  `preclaim` verified the offer was present.
 */
TER
removeNFTokenOfferFromLedger(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal)
{
    if (!nft::deleteTokenOffer(view, sleDel))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}

/** Adapter: remove a DID entry via `DIDDelete::deleteSLE`. */
TER
removeDIDFromLedger(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return DIDDelete::deleteSLE(view, sleDel, account, j);
}

/** Adapter: remove a price oracle via `OracleDelete::deleteOracle`. */
TER
removeOracleFromLedger(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const& account,
    uint256 const&,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return OracleDelete::deleteOracle(view, sleDel, account, j);
}

/** Adapter: remove a credential entry via `credentials::deleteSLE`. */
TER
removeCredentialFromLedger(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const&,
    uint256 const&,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return credentials::deleteSLE(view, sleDel, j);
}

/** Adapter: remove a delegate entry via `DelegateSet::deleteDelegate`. */
TER
removeDelegateFromLedger(
    ServiceRegistry&,
    ApplyView& view,
    AccountID const&,
    uint256 const&,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return DelegateSet::deleteDelegate(view, sleDel, j);
}

/** Map a `LedgerEntryType` to its deletion adapter, or `nullptr` if the type
 *  is a blocking obligation.
 *
 *  This function is the single source of truth for classifying whether an
 *  owned ledger object can be deleted as part of `AccountDelete`.  It is
 *  called in *both* `preclaim` (to detect obligations before any mutation)
 *  and `doApply` (to invoke the actual deletion), keeping the two phases
 *  perfectly in sync without duplicating the type list.
 *
 *  Deletable types: `ltOFFER`, `ltSIGNER_LIST`, `ltTICKET`,
 *  `ltDEPOSIT_PREAUTH`, `ltNFTOKEN_OFFER`, `ltDID`, `ltORACLE`,
 *  `ltCREDENTIAL`, `ltDELEGATE`.  Any other type — including trust lines,
 *  escrows, checks, and payment channels — is treated as an obligation and
 *  returns `nullptr`.
 *
 *  @param t  The ledger entry type of the owned object.
 *  @return   A pointer to the adapter that deletes this type, or `nullptr`
 *      if `t` represents a blocking obligation.
 */
DeleterFuncPtr
nonObligationDeleter(LedgerEntryType t)
{
    switch (t)
    {
        case ltOFFER:
            return offerDelete;
        case ltSIGNER_LIST:
            return removeSignersFromLedger;
        case ltTICKET:
            return removeTicketFromLedger;
        case ltDEPOSIT_PREAUTH:
            return removeDepositPreauthFromLedger;
        case ltNFTOKEN_OFFER:
            return removeNFTokenOfferFromLedger;
        case ltDID:
            return removeDIDFromLedger;
        case ltORACLE:
            return removeOracleFromLedger;
        case ltCREDENTIAL:
            return removeCredentialFromLedger;
        case ltDELEGATE:
            return removeDelegateFromLedger;
        default:
            return nullptr;
    }
}

}  // namespace

TER
AccountDelete::preclaim(PreclaimContext const& ctx)
{
    AccountID const account{ctx.tx[sfAccount]};
    AccountID const dst{ctx.tx[sfDestination]};

    auto sleDst = ctx.view.read(keylet::account(dst));

    if (!sleDst)
        return tecNO_DST;

    if ((((*sleDst)[sfFlags] & lsfRequireDestTag) != 0u) && !ctx.tx[~sfDestinationTag])
        return tecDST_TAG_NEEDED;

    if (auto const err = credentials::valid(ctx.tx, ctx.view, account, ctx.j); !isTesSuccess(err))
        return err;

    // Deposit-auth with credentials: defer to doApply so that expired
    // credentials can be removed on the mutable ApplyView.
    if (!ctx.tx.isFieldPresent(sfCredentialIDs))
    {
        if ((sleDst->getFlags() & lsfDepositAuth) != 0u)
        {
            if (!ctx.view.exists(keylet::depositPreauth(dst, account)))
                return tecNO_PERMISSION;
        }
    }

    auto sleAccount = ctx.view.read(keylet::account(account));
    XRPL_ASSERT(sleAccount, "xrpl::AccountDelete::preclaim : non-null account");
    if (!sleAccount)
        return terNO_ACCOUNT;

    if ((*sleAccount)[~sfMintedNFTokens] != (*sleAccount)[~sfBurnedNFTokens])
        return tecHAS_OBLIGATIONS;

    Keylet const first = keylet::nftpageMin(account);
    Keylet const last = keylet::nftpageMax(account);

    auto const cp = ctx.view.read(
        Keylet(ltNFTOKEN_PAGE, ctx.view.succ(first.key, last.key.next()).value_or(last.key)));
    if (cp)
        return tecHAS_OBLIGATIONS;

    // Sequence freshness: must be ≥ 256 ledgers old to prevent transaction
    // replay if the account is resurrected.  We inspect the account's
    // Sequence (not the transaction's) to remain compatible with Tickets.
    constexpr std::uint32_t kSEQ_DELTA{255};
    if ((*sleAccount)[sfSequence] + kSEQ_DELTA > ctx.view.seq())
        return tecTOO_SOON;

    // NFT-sequence freshness: FirstNFTokenSequence + MintedNFTokens must also
    // be ≥ 256 ledgers old.  Authorized minting does not advance the issuer's
    // account sequence, so without this guard the issuer could re-create and
    // produce a duplicate NFTokenID matching one minted by an authorized minter
    // before deletion.
    if ((*sleAccount)[~sfFirstNFTokenSequence].value_or(0) +
            (*sleAccount)[~sfMintedNFTokens].value_or(0) + kSEQ_DELTA >
        ctx.view.seq())
        return tecTOO_SOON;

    Keylet const ownerDirKeylet{keylet::ownerDir(account)};
    if (dirIsEmpty(ctx.view, ownerDirKeylet))
        return tesSUCCESS;

    std::shared_ptr<SLE const> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::kZERO};

    // dirIsEmpty() should have caught an absent directory, but guard again
    // to avoid undefined behaviour if the ledger is corrupt.
    if (!cdirFirst(ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry))
        return tesSUCCESS;

    std::uint32_t deletableDirEntryCount{0};
    do
    {
        auto sleItem = ctx.view.read(keylet::child(dirEntry));
        if (!sleItem)
        {
            // Directory entry points to a missing object — ledger corruption.
            // LCOV_EXCL_START
            JLOG(ctx.j.fatal()) << "AccountDelete: directory node in ledger " << ctx.view.seq()
                                << " has index to object that is missing: " << to_string(dirEntry);
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        LedgerEntryType const nodeType{safeCast<LedgerEntryType>((*sleItem)[sfLedgerEntryType])};

        if (nonObligationDeleter(nodeType) == nullptr)
            return tecHAS_OBLIGATIONS;

        if (++deletableDirEntryCount > kMAX_DELETABLE_DIR_ENTRIES)
            return tefTOO_BIG;

    } while (cdirNext(ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry));

    return tesSUCCESS;
}

TER
AccountDelete::doApply()
{
    auto src = view().peek(keylet::account(account_));
    XRPL_ASSERT(src, "xrpl::AccountDelete::doApply : non-null source account");

    auto const dstID = ctx_.tx[sfDestination];
    auto dst = view().peek(keylet::account(dstID));
    XRPL_ASSERT(dst, "xrpl::AccountDelete::doApply : non-null destination account");

    if (!src || !dst)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (ctx_.tx.isFieldPresent(sfCredentialIDs))
    {
        if (auto err =
                verifyDepositPreauth(ctx_.tx, ctx_.view(), account_, dstID, dst, ctx_.journal);
            !isTesSuccess(err))
            return err;
    }

    Keylet const ownerDirKeylet{keylet::ownerDir(account_)};
    auto const ter = cleanupOnAccountDelete(
        view(),
        ownerDirKeylet,
        [&](LedgerEntryType nodeType,
            uint256 const& dirEntry,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            if (auto deleter = nonObligationDeleter(nodeType))
            {
                TER const result{deleter(ctx_.registry, view(), account_, dirEntry, sleItem, j_)};

                return {result, SkipEntry::No};
            }

            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::AccountDelete::doApply : undeletable item not found "
                "in preclaim");
            JLOG(j_.error()) << "AccountDelete undeletable item not "
                                "found in preclaim.";
            return {tecHAS_OBLIGATIONS, SkipEntry::No};
            // LCOV_EXCL_STOP
        },
        ctx_.journal);
    if (!isTesSuccess(ter))
        return ter;

    auto const remainingBalance = src->getFieldAmount(sfBalance).xrp();
    (*dst)[sfBalance] = (*dst)[sfBalance] + remainingBalance;
    (*src)[sfBalance] = (*src)[sfBalance] - remainingBalance;
    ctx_.deliver(remainingBalance);

    XRPL_ASSERT(
        (*src)[sfBalance] == XRPAmount(0), "xrpl::AccountDelete::doApply : source balance is zero");

    if (view().exists(ownerDirKeylet) && !view().emptyDirDelete(ownerDirKeylet))
    {
        JLOG(j_.error()) << "AccountDelete cannot delete root dir node of " << toBase58(account_);
        return tecHAS_OBLIGATIONS;
    }

    // Re-arm the free password-change allowance on the destination: receiving
    // XRP resets lsfPasswordSpent so the destination can change its password
    // without an extra fee again.
    if (remainingBalance > XRPAmount(0) && dst->isFlag(lsfPasswordSpent))
        dst->clearFlag(lsfPasswordSpent);

    view().update(dst);
    view().erase(src);

    return tesSUCCESS;
}

void
AccountDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
AccountDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
