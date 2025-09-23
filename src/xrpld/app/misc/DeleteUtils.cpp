//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/misc/DeleteUtils.h>
#include <xrpld/app/tx/detail/ContractDelete.h>
#include <xrpld/app/tx/detail/DID.h>
#include <xrpld/app/tx/detail/DelegateSet.h>
#include <xrpld/app/tx/detail/DeleteAccount.h>
#include <xrpld/app/tx/detail/DeleteOracle.h>
#include <xrpld/app/tx/detail/DepositPreauth.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/tx/detail/SetSignerList.h>

#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/digest.h>

#include <unordered_set>

namespace ripple {

// Local function definitions that provides signature compatibility.
TER
offerDelete(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return offerDelete(view, sleDel, j);
}

TER
removeSignersFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return SetSignerList::removeFromLedger(app, view, account, j);
}

TER
removeTicketFromLedger(
    Application&,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const&,
    beast::Journal j)
{
    return Transactor::ticketDelete(view, account, delIndex, j);
}

TER
removeDepositPreauthFromLedger(
    Application&,
    ApplyView& view,
    AccountID const&,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const&,
    beast::Journal j)
{
    return DepositPreauth::removeFromLedger(view, delIndex, j);
}

TER
removeNFTokenOfferFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal)
{
    if (!nft::deleteTokenOffer(view, sleDel))
        return tefBAD_LEDGER;

    return tesSUCCESS;
}

TER
removeDIDFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return DIDDelete::deleteSLE(view, sleDel, account, j);
}

TER
removeOracleFromLedger(
    Application&,
    ApplyView& view,
    AccountID const& account,
    uint256 const&,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return DeleteOracle::deleteOracle(view, sleDel, account, j);
}

TER
removeCredentialFromLedger(
    Application&,
    ApplyView& view,
    AccountID const&,
    uint256 const&,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return credentials::deleteSLE(view, sleDel, j);
}

TER
removeDelegateFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return DelegateSet::deleteDelegate(view, sleDel, account, j);
}

TER
removeContractFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return ContractDelete::deleteContract(view, sleDel, account, j);
}

// Return nullptr if the LedgerEntryType represents an obligation that can't
// be deleted.  Otherwise return the pointer to the function that can delete
// the non-obligation
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
        case ltCONTRACT:
            return removeContractFromLedger;
        default:
            return nullptr;
    }
}

TER
deletePreclaim(
    PreclaimContext const& ctx,
    std::uint32_t seqDelta,
    AccountID const account,
    AccountID const dest,
    bool isPseudoAccount)
{
    auto destSle = ctx.view.read(keylet::account(dest));

    if (!destSle)
        return tecNO_DST;

    if ((*destSle)[sfFlags] & lsfRequireDestTag && !ctx.tx[~sfDestinationTag])
        return tecDST_TAG_NEEDED;

    // If credentials are provided - check them anyway
    if (auto const err = credentials::valid(ctx.tx, ctx.view, account, ctx.j);
        !isTesSuccess(err))
        return err;

    // if credentials then postpone auth check to doApply, to check for expired
    // credentials
    if (!ctx.tx.isFieldPresent(sfCredentialIDs))
    {
        // Check whether the destination account requires deposit authorization.
        if (ctx.view.rules().enabled(featureDepositAuth) &&
            (destSle->getFlags() & lsfDepositAuth))
        {
            if (!ctx.view.exists(keylet::depositPreauth(dest, account)) &&
                !isPseudoAccount)
                return tecNO_PERMISSION;
        }
    }

    auto srcSle = ctx.view.read(keylet::account(account));
    XRPL_ASSERT(srcSle, "ripple::DeleteAccount::preclaim : non-null account");
    if (!srcSle)
        return terNO_ACCOUNT;

    if (ctx.view.rules().enabled(featureNonFungibleTokensV1))
    {
        // If an issuer has any issued NFTs resident in the ledger then it
        // cannot be deleted.
        if ((*srcSle)[~sfMintedNFTokens] != (*srcSle)[~sfBurnedNFTokens])
            return tecHAS_OBLIGATIONS;

        // If the account owns any NFTs it cannot be deleted.
        Keylet const first = keylet::nftpage_min(account);
        Keylet const last = keylet::nftpage_max(account);

        auto const cp = ctx.view.read(Keylet(
            ltNFTOKEN_PAGE,
            ctx.view.succ(first.key, last.key.next()).value_or(last.key)));
        if (cp)
            return tecHAS_OBLIGATIONS;
    }

    // We don't allow an account to be deleted if its sequence number
    // is within 256 of the current ledger.  This prevents replay of old
    // transactions if this account is resurrected after it is deleted.
    //
    // We look at the account's Sequence rather than the transaction's
    // Sequence in preparation for Tickets.
    if ((*srcSle)[sfSequence] + seqDelta > ctx.view.seq())
        return tecTOO_SOON;

    // When fixNFTokenRemint is enabled, we don't allow an account to be
    // deleted if <FirstNFTokenSequence + MintedNFTokens> is within 256 of the
    // current ledger. This is to prevent having duplicate NFTokenIDs after
    // account re-creation.
    //
    // Without this restriction, duplicate NFTokenIDs can be reproduced when
    // authorized minting is involved. Because when the minter mints a NFToken,
    // the issuer's sequence does not change. So when the issuer re-creates
    // their account and mints a NFToken, it is possible that the
    // NFTokenSequence of this NFToken is the same as the one that the
    // authorized minter minted in a previous ledger.
    if (ctx.view.rules().enabled(fixNFTokenRemint) &&
        ((*srcSle)[~sfFirstNFTokenSequence].value_or(0) +
             (*srcSle)[~sfMintedNFTokens].value_or(0) + seqDelta >
         ctx.view.seq()))
        return tecTOO_SOON;

    // Verify that the account does not own any objects that would prevent
    // the account from being deleted.
    Keylet const ownerDirKeylet{keylet::ownerDir(account)};
    if (dirIsEmpty(ctx.view, ownerDirKeylet))
        return tesSUCCESS;

    std::shared_ptr<SLE const> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};

    // Account has no directory at all.  This _should_ have been caught
    // by the dirIsEmpty() check earlier, but it's okay to catch it here.
    if (!cdirFirst(
            ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry))
        return tesSUCCESS;

    std::int32_t deletableDirEntryCount{0};
    do
    {
        // Make sure any directory node types that we find are the kind
        // we can delete.
        auto sleItem = ctx.view.read(keylet::child(dirEntry));
        if (!sleItem)
        {
            // Directory node has an invalid index.  Bail out.
            JLOG(ctx.j.fatal())
                << "DeleteAccount: directory node in ledger " << ctx.view.seq()
                << " has index to object that is missing: "
                << to_string(dirEntry);
            return tefBAD_LEDGER;
        }

        LedgerEntryType const nodeType{
            safe_cast<LedgerEntryType>((*sleItem)[sfLedgerEntryType])};

        if (!nonObligationDeleter(nodeType))
            return tecHAS_OBLIGATIONS;

        // We found a deletable directory entry.  Count it.  If we find too
        // many deletable directory entries then bail out.
        if (++deletableDirEntryCount > maxDeletableDirEntries)
            return tefTOO_BIG;

    } while (cdirNext(
        ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry));

    return tesSUCCESS;
}

TER
deleteDoApply(
    ApplyContext& applyCtx,
    STAmount const& accountBalance,
    AccountID const& account,
    AccountID const& dest)
{
    auto& view = applyCtx.view();
    STTx const tx = applyCtx.tx;
    beast::Journal j = applyCtx.journal;

    auto srcSle = view.peek(keylet::account(account));
    XRPL_ASSERT(
        srcSle, "ripple::DeleteAccount::doApply : non-null source account");

    if (!srcSle)
        return tefBAD_LEDGER;

    auto destSle = view.peek(keylet::account(dest));
    XRPL_ASSERT(
        destSle,
        "ripple::DeleteAccount::doApply : non-null destination account");

    if (!destSle)
        return tefBAD_LEDGER;

    if (view.rules().enabled(featureDepositAuth) &&
        tx.isFieldPresent(sfCredentialIDs))
    {
        if (auto err =
                verifyDepositPreauth(tx, view, account, dest, destSle, j);
            !isTesSuccess(err))
            return err;
    }

    Keylet const ownerDirKeylet{keylet::ownerDir(account)};
    auto const ter = cleanupOnAccountDelete(
        view,
        ownerDirKeylet,
        [&](LedgerEntryType nodeType,
            uint256 const& dirEntry,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            if (auto deleter = nonObligationDeleter(nodeType))
            {
                TER const result{
                    deleter(applyCtx.app, view, account, dirEntry, sleItem, j)};

                return {result, SkipEntry::No};
            }

            UNREACHABLE(
                "ripple::DeleteAccount::doApply : undeletable item not found "
                "in preclaim");
            JLOG(j.error()) << "DeleteAccount undeletable item not "
                               "found in preclaim.";
            return {tecHAS_OBLIGATIONS, SkipEntry::No};
        },
        j);
    if (ter != tesSUCCESS)
        return ter;

    // Transfer any XRP remaining after the fee is paid to the destination:
    (*destSle)[sfBalance] = (*destSle)[sfBalance] + accountBalance;
    (*srcSle)[sfBalance] = (*srcSle)[sfBalance] - accountBalance;
    applyCtx.deliver(accountBalance);

    XRPL_ASSERT(
        (*srcSle)[sfBalance] == XRPAmount(0),
        "ripple::DeleteAccount::doApply : source balance is zero");

    // If there's still an owner directory associated with the source account
    // delete it.
    if (view.exists(ownerDirKeylet) && !view.emptyDirDelete(ownerDirKeylet))
    {
        JLOG(j.error()) << "DeleteAccount cannot delete root dir node of "
                        << toBase58(account);
        return tecHAS_OBLIGATIONS;
    }

    // Re-arm the password change fee if we can and need to.
    if (accountBalance > XRPAmount(0) && (*destSle).isFlag(lsfPasswordSpent))
        (*destSle).clearFlag(lsfPasswordSpent);

    view.update(destSle);
    view.erase(srcSle);

    return tesSUCCESS;
}

}  // namespace ripple
