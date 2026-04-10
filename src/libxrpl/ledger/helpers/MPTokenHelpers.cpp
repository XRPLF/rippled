#include <xrpl/ledger/helpers/MPTokenHelpers.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

bool
isGlobalFrozen(ReadView const& view, MPTIssue const& mptIssue)
{
    if (auto const sle = view.read(keylet::mptIssuance(mptIssue.getMptID())))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

bool
isIndividualFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue)
{
    if (auto const sle = view.read(keylet::mptoken(mptIssue.getMptID(), account)))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

bool
isFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue, int depth)
{
    return isGlobalFrozen(view, mptIssue) || isIndividualFrozen(view, account, mptIssue) ||
        isVaultPseudoAccountFrozen(view, account, mptIssue, depth);
}

[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    MPTIssue const& mptIssue,
    int depth)
{
    if (isGlobalFrozen(view, mptIssue))
        return true;

    for (auto const& account : accounts)
    {
        if (isIndividualFrozen(view, account, mptIssue))
            return true;
    }

    for (auto const& account : accounts)
    {
        if (isVaultPseudoAccountFrozen(view, account, mptIssue, depth))
            return true;
    }

    return false;
}

Rate
transferRate(ReadView const& view, MPTID const& issuanceID)
{
    // fee is 0-50,000 (0-50%), rate is 1,000,000,000-2,000,000,000
    // For example, if transfer fee is 50% then 10,000 * 50,000 = 500,000
    // which represents 50% of 1,000,000,000
    if (auto const sle = view.read(keylet::mptIssuance(issuanceID));
        sle && sle->isFieldPresent(sfTransferFee))
        return Rate{1'000'000'000u + (10'000 * sle->getFieldU16(sfTransferFee))};

    return parityRate;
}

[[nodiscard]] TER
canAddHolding(ReadView const& view, MPTIssue const& mptIssue)
{
    auto mptID = mptIssue.getMptID();
    auto issuance = view.read(keylet::mptIssuance(mptID));
    if (!issuance)
    {
        return tecOBJECT_NOT_FOUND;
    }
    if (!issuance->isFlag(lsfMPTCanTransfer))
    {
        return tecNO_AUTH;
    }

    return tesSUCCESS;
}

[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    STTx const& tx,
    AccountID const& accountID,
    XRPAmount priorBalance,
    MPTIssue const& mptIssue,
    beast::Journal journal)
{
    auto const& mptID = mptIssue.getMptID();
    auto const mpt = view.peek(keylet::mptIssuance(mptID));
    if (!mpt)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (mpt->isFlag(lsfMPTLocked))
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (view.peek(keylet::mptoken(mptID, accountID)))
        return tecDUPLICATE;
    if (accountID == mptIssue.getIssuer())
        return tesSUCCESS;

    return authorizeMPToken(view, tx, priorBalance, mptID, accountID, journal);
}

[[nodiscard]] TER
authorizeMPToken(
    ApplyView& view,
    STTx const& tx,
    XRPAmount const& priorBalance,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    beast::Journal journal,
    std::uint32_t flags,
    std::optional<AccountID> holderID)
{
    auto const sleAcct = view.peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // If the account that submitted the tx is a holder
    // Note: `account_` is holder's account
    //       `holderID` is NOT used
    if (!holderID)
    {
        // When a holder wants to unauthorize/delete a MPT, the ledger must
        //      - delete mptokenKey from owner directory
        //      - delete the MPToken
        if ((flags & tfMPTUnauthorize) != 0)
        {
            auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);
            auto const sleMpt = view.peek(mptokenKey);
            if (!sleMpt || (*sleMpt)[sfMPTAmount] != 0 ||
                (view.rules().enabled(fixSecurity3_1_3) &&
                 (*sleMpt)[~sfLockedAmount].value_or(0) != 0))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (!view.dirRemove(
                    keylet::ownerDir(account), (*sleMpt)[sfOwnerNode], sleMpt->key(), false))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            auto const sponsor = getLedgerEntryReserveSponsor(view, sleMpt);
            adjustOwnerCount(view, sleAcct, sponsor, -1, journal);

            view.erase(sleMpt);
            return tesSUCCESS;
        }

        // A potential holder wants to authorize/hold a mpt, the ledger must:
        //      - add the new mptokenKey to the owner directory
        //      - create the MPToken object for the holder

        auto const sponsor = getTxReserveSponsor(view, tx);

        auto const isSponsoredAndPreFunded = sponsor && !isSponsorReserveCoSigning(tx);

        // The reserve that is required to create the MPToken. Note
        // that although the reserve increases with every item
        // an account owns, in the case of MPTokens we only
        // *enforce* a reserve if the user owns more than two
        // items. This is similar to the reserve requirements of trust lines.
        // If PreFunded Sponsor, it must be checked whether sufficient
        // ReserveCount exists.
        if (ownerCount(sponsor ? sponsor : sleAcct) >= 2 || isSponsoredAndPreFunded)
        {
            if (auto const ret =
                    checkInsufficientReserve(view, tx, sleAcct, priorBalance, sponsor, 1);
                !isTesSuccess(ret))
                return ret;
        }

        // Defensive check before we attempt to create MPToken for the issuer
        auto const mpt = view.read(keylet::mptIssuance(mptIssuanceID));
        if (!mpt || mpt->getAccountID(sfIssuer) == account)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::authorizeMPToken : invalid issuance or issuers token");
            if (view.rules().enabled(featureLendingProtocol))
                return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);
        auto mptoken = std::make_shared<SLE>(mptokenKey);
        if (auto ter = dirLink(view, account, mptoken))
            return ter;  // LCOV_EXCL_LINE

        (*mptoken)[sfAccount] = account;
        (*mptoken)[sfMPTokenIssuanceID] = mptIssuanceID;
        (*mptoken)[sfFlags] = 0;
        view.insert(mptoken);

        // Update owner count.
        adjustOwnerCount(view, sleAcct, sponsor, 1, journal);
        addSponsorToLedgerEntry(mptoken, sponsor);

        return tesSUCCESS;
    }

    auto const sleMptIssuance = view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleMptIssuance)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // If the account that submitted this tx is the issuer of the MPT
    // Note: `account_` is issuer's account
    //       `holderID` is holder's account
    if (account != (*sleMptIssuance)[sfIssuer])
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMpt = view.peek(keylet::mptoken(mptIssuanceID, *holderID));
    if (!sleMpt)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sleMpt->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    // Issuer wants to unauthorize the holder, unset lsfMPTAuthorized on
    // their MPToken
    if ((flags & tfMPTUnauthorize) != 0)
    {
        flagsOut &= ~lsfMPTAuthorized;
    }
    // Issuer wants to authorize a holder, set lsfMPTAuthorized on their
    // MPToken
    else
    {
        flagsOut |= lsfMPTAuthorized;
    }

    if (flagsIn != flagsOut)
        sleMpt->setFieldU32(sfFlags, flagsOut);

    view.update(sleMpt);
    return tesSUCCESS;
}

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    STTx const& tx,
    AccountID const& accountID,
    MPTIssue const& mptIssue,
    beast::Journal journal)
{
    // If the account is the issuer, then no token should exist. MPTs do not
    // have the legacy ability to create such a situation, but check anyway. If
    // a token does exist, it will get deleted. If not, return success.
    bool const accountIsIssuer = accountID == mptIssue.getIssuer();
    auto const& mptID = mptIssue.getMptID();
    auto const mptoken = view.peek(keylet::mptoken(mptID, accountID));
    if (!mptoken)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    // Unlike a trust line, if the account is the issuer, and the token has a
    // balance, it can not just be deleted, because that will throw the issuance
    // accounting out of balance, so fail. Since this should be impossible
    // anyway, I'm not going to put any effort into it.
    if (mptoken->at(sfMPTAmount) != 0 ||
        (view.rules().enabled(fixSecurity3_1_3) && (*mptoken)[~sfLockedAmount].value_or(0) != 0))
        return tecHAS_OBLIGATIONS;

    // Don't delete if the token still has confidential balances
    if (mptoken->isFieldPresent(sfConfidentialBalanceInbox) ||
        mptoken->isFieldPresent(sfConfidentialBalanceSpending))
    {
        return tecHAS_OBLIGATIONS;
    }

    return authorizeMPToken(
        view,
        tx,
        {},  // priorBalance
        mptID,
        accountID,
        journal,
        tfMPTUnauthorize  // flags
    );
}

[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& account,
    AuthType authType,
    int depth)
{
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto const sleIssuance = view.read(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    auto const mptIssuer = sleIssuance->getAccountID(sfIssuer);

    // issuer is always "authorized"
    if (mptIssuer == account)  // Issuer won't have MPToken
        return tesSUCCESS;

    bool const featureSAVEnabled = view.rules().enabled(featureSingleAssetVault);

    if (featureSAVEnabled)
    {
        if (depth >= maxAssetCheckDepth)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // requireAuth is recursive if the issuer is a vault pseudo-account
        auto const sleIssuer = view.read(keylet::account(mptIssuer));
        if (!sleIssuer)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        if (sleIssuer->isFieldPresent(sfVaultID))
        {
            auto const sleVault = view.read(keylet::vault(sleIssuer->getFieldH256(sfVaultID)));
            if (!sleVault)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            auto const asset = sleVault->at(sfAsset);
            if (auto const err = asset.visit(
                    [&](Issue const& issue) { return requireAuth(view, issue, account, authType); },
                    [&](MPTIssue const& issue) {
                        return requireAuth(view, issue, account, authType, depth + 1);
                    });
                !isTesSuccess(err))
                return err;
        }
    }

    auto const mptokenID = keylet::mptoken(mptID.key, account);
    auto const sleToken = view.read(mptokenID);

    // if account has no MPToken, fail
    if (!sleToken && (authType == AuthType::StrongAuth || authType == AuthType::Legacy))
        return tecNO_AUTH;

    // Note, this check is not amendment-gated because DomainID will be always
    // empty **unless** writing to it has been enabled by an amendment
    auto const maybeDomainID = sleIssuance->at(~sfDomainID);
    if (maybeDomainID)
    {
        // Defensive check: An issuance with sfDomainID must strictly enforce lsfMPTRequireAuth.
        // While preclaim checks prevent clearing this flag when sfDomainID is set, we verify here
        // to guard against potential logic regressions in the future changes.
        if (view.rules().enabled(fixSecurity3_1_3))
        {
            // Post-fixSecurity3_1_3: Return a proper error code instead of asserting.
            if (!(sleIssuance->getFieldU32(sfFlags) & lsfMPTRequireAuth))
                return tefINTERNAL;  // LCOV_EXCL_LINE
        }
        else
        {
            // Pre-fixSecurity3_1_3: Fault via assertion in Debug mode;
            // potentially fall through in Release mode.
            XRPL_ASSERT(
                sleIssuance->getFieldU32(sfFlags) & lsfMPTRequireAuth,
                "xrpl::requireAuth : issuance requires authorization");
        }

        // ter = tefINTERNAL | tecOBJECT_NOT_FOUND | tecNO_AUTH | tecEXPIRED
        auto const ter = credentials::validDomain(view, *maybeDomainID, account);
        if (isTesSuccess(ter))
        {
            return ter;  // Note: sleToken might be null
        }
        if (!sleToken)
        {
            return ter;
        }
        // We ignore error from validDomain if we found sleToken, as it could
        // belong to someone who is explicitly authorized e.g. a vault owner.
    }

    if (featureSAVEnabled)
    {
        // Implicitly authorize Vault and LoanBroker pseudo-accounts
        if (isPseudoAccount(view, account, {&sfVaultID, &sfLoanBrokerID}))
            return tesSUCCESS;
    }

    // mptoken must be authorized if issuance enabled requireAuth
    if (sleIssuance->isFlag(lsfMPTRequireAuth) &&
        (!sleToken || !sleToken->isFlag(lsfMPTAuthorized)))
        return tecNO_AUTH;

    return tesSUCCESS;  // Note: sleToken might be null
}

[[nodiscard]] TER
enforceMPTokenAuthorization(
    ApplyView& view,
    STTx const& tx,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    XRPAmount const& priorBalance,  // for MPToken authorization
    beast::Journal j)
{
    auto const sleIssuance = view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        sleIssuance->isFlag(lsfMPTRequireAuth),
        "xrpl::enforceMPTokenAuthorization : authorization required");

    if (account == sleIssuance->at(sfIssuer))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const keylet = keylet::mptoken(mptIssuanceID, account);
    auto const sleToken = view.read(keylet);  //  NOTE: might be null
    auto const maybeDomainID = sleIssuance->at(~sfDomainID);
    bool expired = false;
    bool const authorizedByDomain = [&]() -> bool {
        // NOTE: defensive here, should be checked in preclaim
        if (!maybeDomainID.has_value())
            return false;  // LCOV_EXCL_LINE

        auto const ter = verifyValidDomain(view, account, *maybeDomainID, j);
        if (isTesSuccess(ter))
            return true;
        if (ter == tecEXPIRED)
            expired = true;
        return false;
    }();

    if (!authorizedByDomain && sleToken == nullptr)
    {
        // Could not find MPToken and won't create one, could be either of:
        //
        // 1. Field sfDomainID not set in MPTokenIssuance or
        // 2. Account has no matching and accepted credentials or
        // 3. Account has all expired credentials (deleted in verifyValidDomain)
        //
        // Either way, return tecNO_AUTH and there is nothing else to do
        return expired ? tecEXPIRED : tecNO_AUTH;
    }
    if (!authorizedByDomain && maybeDomainID.has_value())
    {
        // Found an MPToken but the account is not authorized and we expect
        // it to have been authorized by the domain. This could be because the
        // credentials used to create the MPToken have expired or been deleted.
        return expired ? tecEXPIRED : tecNO_AUTH;
    }
    if (!authorizedByDomain)
    {
        // We found an MPToken, but sfDomainID is not set, so this is a classic
        // MPToken which requires authorization by the token issuer.
        XRPL_ASSERT(
            sleToken != nullptr && !maybeDomainID.has_value(),
            "xrpl::enforceMPTokenAuthorization : found MPToken");
        if (sleToken->isFlag(lsfMPTAuthorized))
            return tesSUCCESS;

        return tecNO_AUTH;
    }
    if (authorizedByDomain && sleToken != nullptr)
    {
        // Found an MPToken, authorized by the domain. Ignore authorization flag
        // lsfMPTAuthorized because it is meaningless. Return tesSUCCESS
        XRPL_ASSERT(
            maybeDomainID.has_value(),
            "xrpl::enforceMPTokenAuthorization : found MPToken for domain");
        return tesSUCCESS;
    }
    if (authorizedByDomain)
    {
        // Could not find MPToken but there should be one because we are
        // authorized by domain. Proceed to create it, then return tesSUCCESS
        XRPL_ASSERT(
            maybeDomainID.has_value() && sleToken == nullptr,
            "xrpl::enforceMPTokenAuthorization : new MPToken for domain");
        if (auto const err = authorizeMPToken(
                view,
                tx,
                priorBalance,   // priorBalance
                mptIssuanceID,  // mptIssuanceID
                account,        // account
                j);
            !isTesSuccess(err))
            return err;

        return tesSUCCESS;
    }

    // LCOV_EXCL_START
    UNREACHABLE("xrpl::enforceMPTokenAuthorization : condition list is incomplete");
    return tefINTERNAL;
    // LCOV_EXCL_STOP
}

TER
canTransfer(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& from,
    AccountID const& to)
{
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto const sleIssuance = view.read(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanTransfer))
    {
        if (from != (*sleIssuance)[sfIssuer] && to != (*sleIssuance)[sfIssuer])
            return TER{tecNO_AUTH};
    }
    return tesSUCCESS;
}

TER
canTrade(ReadView const& view, Asset const& asset)
{
    return asset.visit(
        [&](Issue const&) -> TER { return tesSUCCESS; },
        [&](MPTIssue const& mptIssue) -> TER {
            auto const sleIssuance = view.read(keylet::mptIssuance(mptIssue.getMptID()));
            if (!sleIssuance)
                return tecOBJECT_NOT_FOUND;
            if (!sleIssuance->isFlag(lsfMPTCanTrade))
                return tecNO_PERMISSION;
            return tesSUCCESS;
        });
}

TER
canMPTTradeAndTransfer(
    ReadView const& view,
    Asset const& asset,
    AccountID const& from,
    AccountID const& to)
{
    if (!asset.holds<MPTIssue>())
        return tesSUCCESS;

    if (auto const ter = canTrade(view, asset); !isTesSuccess(ter))
        return ter;

    return canTransfer(view, asset, from, to);
}

TER
lockEscrowMPT(ApplyView& view, AccountID const& sender, STAmount const& amount, beast::Journal j)
{
    auto const mptIssue = amount.get<MPTIssue>();
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "lockEscrowMPT: MPT issuance not found for " << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    if (amount.getIssuer() == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "lockEscrowMPT: sender is the issuer, cannot lock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    // 1. Decrease the MPT Holder MPTAmount
    // 2. Increase the MPT Holder EscrowedAmount
    {
        auto const mptokenID = keylet::mptoken(mptID.key, sender);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: MPToken not found for " << sender;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto const amt = sle->getFieldU64(sfMPTAmount);
        auto const pay = amount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, amt), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: insufficient MPTAmount for " << to_string(sender)
                            << ": " << amt << " < " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        (*sle)[sfMPTAmount] = amt - pay;

        // Overflow check for addition
        uint64_t const locked = (*sle)[~sfLockedAmount].value_or(0);

        if (!canAdd(STAmount(mptIssue, locked), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: overflow on locked amount for " << to_string(sender)
                            << ": " << locked << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (sle->isFieldPresent(sfLockedAmount))
        {
            (*sle)[sfLockedAmount] += pay;
        }
        else
        {
            sle->setFieldU64(sfLockedAmount, pay);
        }

        view.update(sle);
    }

    // 1. Increase the Issuance EscrowedAmount
    // 2. DO NOT change the Issuance OutstandingAmount
    {
        uint64_t const issuanceEscrowed = (*sleIssuance)[~sfLockedAmount].value_or(0);
        auto const pay = amount.mpt().value();

        // Overflow check for addition
        if (!canAdd(STAmount(mptIssue, issuanceEscrowed), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: overflow on issuance "
                               "locked amount for "
                            << mptIssue.getMptID() << ": " << issuanceEscrowed << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (sleIssuance->isFieldPresent(sfLockedAmount))
        {
            (*sleIssuance)[sfLockedAmount] += pay;
        }
        else
        {
            sleIssuance->setFieldU64(sfLockedAmount, pay);
        }

        view.update(sleIssuance);
    }
    return tesSUCCESS;
}

TER
unlockEscrowMPT(
    ApplyView& view,
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j)
{
    if (!view.rules().enabled(fixTokenEscrowV1))
    {
        XRPL_ASSERT(netAmount == grossAmount, "xrpl::unlockEscrowMPT : netAmount == grossAmount");
    }

    auto const& issuer = netAmount.getIssuer();
    auto const& mptIssue = netAmount.get<MPTIssue>();
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: MPT issuance not found for " << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    // Decrease the Issuance EscrowedAmount
    {
        if (!sleIssuance->isFieldPresent(sfLockedAmount))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: no locked amount in issuance for "
                            << mptIssue.getMptID();
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const locked = sleIssuance->getFieldU64(sfLockedAmount);
        auto const redeem = grossAmount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, locked), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient locked amount for "
                            << mptIssue.getMptID() << ": " << locked << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const newLocked = locked - redeem;
        if (newLocked == 0)
        {
            sleIssuance->makeFieldAbsent(sfLockedAmount);
        }
        else
        {
            sleIssuance->setFieldU64(sfLockedAmount, newLocked);
        }
        view.update(sleIssuance);
    }

    if (issuer != receiver)
    {
        // Increase the MPT Holder MPTAmount
        auto const mptokenID = keylet::mptoken(mptID.key, receiver);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: MPToken not found for " << receiver;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto current = sle->getFieldU64(sfMPTAmount);
        auto delta = netAmount.mpt().value();

        // Overflow check for addition
        if (!canAdd(STAmount(mptIssue, current), STAmount(mptIssue, delta)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: overflow on MPTAmount for " << to_string(receiver)
                            << ": " << current << " + " << delta;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        (*sle)[sfMPTAmount] += delta;
        view.update(sle);
    }
    else
    {
        // Decrease the Issuance OutstandingAmount
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        auto const redeem = netAmount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - redeem);
        view.update(sleIssuance);
    }

    if (issuer == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: sender is the issuer, "
                           "cannot unlock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP
    // Decrease the MPT Holder EscrowedAmount
    auto const mptokenID = keylet::mptoken(mptID.key, sender);
    auto sle = view.peek(mptokenID);
    if (!sle)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: MPToken not found for " << sender;
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    if (!sle->isFieldPresent(sfLockedAmount))
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: no locked amount in MPToken for " << to_string(sender);
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    auto const locked = sle->getFieldU64(sfLockedAmount);
    auto const delta = grossAmount.mpt().value();

    // Underflow check for subtraction
    if (!canSubtract(STAmount(mptIssue, locked), STAmount(mptIssue, delta)))
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: insufficient locked amount for " << to_string(sender)
                        << ": " << locked << " < " << delta;
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    auto const newLocked = locked - delta;
    if (newLocked == 0)
    {
        sle->makeFieldAbsent(sfLockedAmount);
    }
    else
    {
        sle->setFieldU64(sfLockedAmount, newLocked);
    }
    view.update(sle);

    // Note: The gross amount is the amount that was locked, the net
    // amount is the amount that is being unlocked. The difference is the fee
    // that was charged for the transfer. If this difference is greater than
    // zero, we need to update the outstanding amount.
    auto const diff = grossAmount.mpt().value() - netAmount.mpt().value();
    if (diff != 0)
    {
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, diff)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << diff;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - diff);
        view.update(sleIssuance);
    }
    return tesSUCCESS;
}

TER
createMPToken(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    std::optional<AccountID> const& sponsor,
    std::uint32_t const flags)
{
    auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);

    auto const ownerNode =
        view.dirInsert(keylet::ownerDir(account), mptokenKey, describeOwnerDir(account));

    if (!ownerNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto mptoken = std::make_shared<SLE>(mptokenKey);
    (*mptoken)[sfAccount] = account;
    (*mptoken)[sfMPTokenIssuanceID] = mptIssuanceID;
    (*mptoken)[sfFlags] = flags;
    (*mptoken)[sfOwnerNode] = *ownerNode;

    if (sponsor)
    {
        auto const sponsorSle = view.peek(keylet::account(*sponsor));
        if (!sponsorSle)
            return tecINTERNAL;
        addSponsorToLedgerEntry(mptoken, sponsorSle);
    }

    view.insert(mptoken);

    return tesSUCCESS;
}

TER
checkCreateMPT(
    xrpl::ApplyView& view,
    xrpl::MPTIssue const& mptIssue,
    xrpl::AccountID const& holder,
    std::optional<xrpl::AccountID> const& sponsor,
    beast::Journal j)
{
    if (mptIssue.getIssuer() == holder)
        return tesSUCCESS;

    auto const mptIssuanceID = keylet::mptIssuance(mptIssue.getMptID());
    auto const mptokenID = keylet::mptoken(mptIssuanceID.key, holder);
    if (!view.exists(mptokenID))
    {
        if (auto const err = createMPToken(view, mptIssue.getMptID(), holder, sponsor, 0);
            !isTesSuccess(err))
        {
            return err;
        }
        auto const sleAcct = view.peek(keylet::account(holder));
        if (!sleAcct)
        {
            return tecINTERNAL;
        }
        auto const sleSponsor =
            sponsor ? view.peek(keylet::account(*sponsor)) : std::shared_ptr<SLE>();
        adjustOwnerCount(view, sleAcct, sleSponsor, 1, j);
    }
    return tesSUCCESS;
}

std::int64_t
maxMPTAmount(SLE const& sleIssuance)
{
    return sleIssuance[~sfMaximumAmount].value_or(maxMPTokenAmount);
}

std::int64_t
availableMPTAmount(SLE const& sleIssuance)
{
    auto const max = maxMPTAmount(sleIssuance);
    auto const outstanding = sleIssuance[sfOutstandingAmount];
    return max - outstanding;
}

std::int64_t
availableMPTAmount(ReadView const& view, MPTID const& mptID)
{
    auto const sle = view.read(keylet::mptIssuance(mptID));
    if (!sle)
        Throw<std::runtime_error>(transHuman(tecINTERNAL));
    return availableMPTAmount(*sle);
}

bool
isMPTOverflow(
    std::int64_t sendAmount,
    std::uint64_t outstandingAmount,
    std::int64_t maximumAmount,
    AllowMPTOverflow allowOverflow)
{
    std::uint64_t const limit = (allowOverflow == AllowMPTOverflow::Yes)
        ? std::numeric_limits<std::uint64_t>::max()
        : maximumAmount;
    return (sendAmount > maximumAmount || outstandingAmount > (limit - sendAmount));
}

STAmount
issuerFundsToSelfIssue(ReadView const& view, MPTIssue const& issue)
{
    STAmount amount{issue};

    auto const sle = view.read(keylet::mptIssuance(issue));
    if (!sle)
        return amount;
    auto const available = availableMPTAmount(*sle);
    return view.balanceHookSelfIssueMPT(issue, available);
}

void
issuerSelfDebitHookMPT(ApplyView& view, MPTIssue const& issue, std::uint64_t amount)
{
    auto const available = availableMPTAmount(view, issue);
    view.issuerSelfDebitHookMPT(issue, amount, available);
}

}  // namespace xrpl
