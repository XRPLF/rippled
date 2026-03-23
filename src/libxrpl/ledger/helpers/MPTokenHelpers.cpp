#include <xrpl/ledger/helpers/MPTokenHelpers.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

// Forward declarations for functions that remain in View.h/cpp
bool
isVaultPseudoAccountFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptShare,
    int depth);

[[nodiscard]] TER
dirLink(
    ApplyView& view,
    AccountID const& owner,
    std::shared_ptr<SLE>& object,
    SF_UINT64 const& node = sfOwnerNode);

bool
MPTokenIssuance::isGlobalFrozen() const
{
    if (sle_)
        return sle_->isFlag(lsfMPTLocked);
    return false;
}

bool
MPTokenIssuance::isIndividualFrozen(AccountID const& account) const
{
    if (auto const sle = readView_.read(keylet::mptoken(mptID_, account)))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

bool
MPTokenIssuance::isFrozen(AccountID const& account, int depth) const
{
    return isGlobalFrozen() || isIndividualFrozen(account) ||
        isVaultPseudoAccountFrozen(readView_, account, mptIssue_, depth);
}

[[nodiscard]] bool
MPTokenIssuance::isAnyFrozen(std::initializer_list<AccountID> const& accounts, int depth) const
{
    if (isGlobalFrozen())
        return true;

    for (auto const& account : accounts)
    {
        if (isIndividualFrozen(account))
            return true;
    }

    for (auto const& account : accounts)
    {
        if (isVaultPseudoAccountFrozen(readView_, account, mptIssue_, depth))
            return true;
    }

    return false;
}

TER
MPTokenIssuance::checkFrozen(AccountID const& account) const
{
    return isFrozen(account) ? TER{tecLOCKED} : TER{tesSUCCESS};
}

bool
MPTokenIssuance::isDeepFrozen(AccountID const& account, int depth) const
{
    return isFrozen(account, depth);
}

TER
MPTokenIssuance::checkDeepFrozen(AccountID const& account) const
{
    return isDeepFrozen(account) ? TER{tecLOCKED} : TER{tesSUCCESS};
}

Rate
MPTokenIssuance::transferRate() const
{
    // fee is 0-50,000 (0-50%), rate is 1,000,000,000-2,000,000,000
    // For example, if transfer fee is 50% then 10,000 * 50,000 = 500,000
    // which represents 50% of 1,000,000,000
    if (sle_ && sle_->isFieldPresent(sfTransferFee))
        return Rate{1'000'000'000u + 10'000 * sle_->getFieldU16(sfTransferFee)};

    return parityRate;
}

[[nodiscard]] TER
MPTokenIssuance::canAddHolding() const
{
    if (!sle_)
    {
        return tecOBJECT_NOT_FOUND;
    }
    if (!sle_->isFlag(lsfMPTCanTransfer))
    {
        return tecNO_AUTH;
    }

    return tesSUCCESS;
}

[[nodiscard]] TER
WritableMPTokenIssuance::addEmptyHolding(
    AccountID const& accountID,
    XRPAmount priorBalance,
    beast::Journal journal)
{
    if (!mutableSle_)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (mutableSle_->isFlag(lsfMPTLocked))
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (applyView_.peek(keylet::mptoken(mptID_, accountID)))
        return tecDUPLICATE;
    if (accountID == mptIssue_.getIssuer())
        return tesSUCCESS;

    return authorizeMPToken(priorBalance, accountID, journal);
}

[[nodiscard]] TER
WritableMPTokenIssuance::authorizeMPToken(
    XRPAmount const& priorBalance,
    AccountID const& account,
    beast::Journal journal,
    std::uint32_t flags,
    std::optional<AccountID> holderID)
{
    WritableAccountRoot wrappedAcct(account, applyView_);
    if (!wrappedAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // If the account that submitted the tx is a holder
    // Note: `account_` is holder's account
    //       `holderID` is NOT used
    if (!holderID)
    {
        // When a holder wants to unauthorize/delete a MPT, the ledger must
        //      - delete mptokenKey from owner directory
        //      - delete the MPToken
        if (flags & tfMPTUnauthorize)
        {
            auto const mptokenKey = keylet::mptoken(mptID_, account);
            auto const sleMpt = applyView_.peek(mptokenKey);
            if (!sleMpt || (*sleMpt)[sfMPTAmount] != 0)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (!applyView_.dirRemove(
                    keylet::ownerDir(account), (*sleMpt)[sfOwnerNode], sleMpt->key(), false))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            wrappedAcct.adjustOwnerCount(-1, journal);

            applyView_.erase(sleMpt);
            return tesSUCCESS;
        }

        // A potential holder wants to authorize/hold a mpt, the ledger must:
        //      - add the new mptokenKey to the owner directory
        //      - create the MPToken object for the holder

        // The reserve that is required to create the MPToken. Note
        // that although the reserve increases with every item
        // an account owns, in the case of MPTokens we only
        // *enforce* a reserve if the user owns more than two
        // items. This is similar to the reserve requirements of trust lines.
        std::uint32_t const uOwnerCount = wrappedAcct->getFieldU32(sfOwnerCount);
        XRPAmount const reserveCreate(
            (uOwnerCount < 2) ? XRPAmount(beast::zero)
                              : applyView_.fees().accountReserve(uOwnerCount + 1));

        if (priorBalance < reserveCreate)
            return tecINSUFFICIENT_RESERVE;

        // Defensive check before we attempt to create MPToken for the issuer
        if (!mutableSle_ || mutableSle_->getAccountID(sfIssuer) == account)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::authorizeMPToken : invalid issuance or issuers token");
            if (applyView_.rules().enabled(featureLendingProtocol))
                return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        auto const mptokenKey = keylet::mptoken(mptID_, account);
        auto mptoken = std::make_shared<SLE>(mptokenKey);
        if (auto ter = dirLink(applyView_, account, mptoken))
            return ter;  // LCOV_EXCL_LINE

        (*mptoken)[sfAccount] = account;
        (*mptoken)[sfMPTokenIssuanceID] = mptID_;
        (*mptoken)[sfFlags] = 0;
        applyView_.insert(mptoken);

        // Update owner count.
        wrappedAcct.adjustOwnerCount(1, journal);

        return tesSUCCESS;
    }

    if (!mutableSle_)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // If the account that submitted this tx is the issuer of the MPT
    // Note: `account_` is issuer's account
    //       `holderID` is holder's account
    if (account != (*mutableSle_)[sfIssuer])
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMpt = applyView_.peek(keylet::mptoken(mptID_, *holderID));
    if (!sleMpt)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sleMpt->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    // Issuer wants to unauthorize the holder, unset lsfMPTAuthorized on
    // their MPToken
    if (flags & tfMPTUnauthorize)
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

    applyView_.update(sleMpt);
    return tesSUCCESS;
}

[[nodiscard]] TER
WritableMPTokenIssuance::removeEmptyHolding(AccountID const& accountID, beast::Journal journal)
{
    // If the account is the issuer, then no token should exist. MPTs do not
    // have the legacy ability to create such a situation, but check anyway. If
    // a token does exist, it will get deleted. If not, return success.
    bool const accountIsIssuer = accountID == mptIssue_.getIssuer();
    auto const mptoken = applyView_.peek(keylet::mptoken(mptID_, accountID));
    if (!mptoken)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    // Unlike a trust line, if the account is the issuer, and the token has a
    // balance, it can not just be deleted, because that will throw the issuance
    // accounting out of balance, so fail. Since this should be impossible
    // anyway, I'm not going to put any effort into it.
    if (mptoken->at(sfMPTAmount) != 0)
        return tecHAS_OBLIGATIONS;

    return authorizeMPToken(
        {},  // priorBalance
        accountID,
        journal,
        tfMPTUnauthorize  // flags
    );
}

[[nodiscard]] TER
MPTokenIssuance::requireAuth(AccountID const& account, AuthType authType, int depth) const
{
    if (!sle_)
        return tecOBJECT_NOT_FOUND;

    auto const mptIssuer = AccountRoot(sle_->getAccountID(sfIssuer), readView_);

    // issuer is always "authorized"
    if (mptIssuer == account)  // Issuer won't have MPToken
        return tesSUCCESS;

    bool const featureSAVEnabled = readView_.rules().enabled(featureSingleAssetVault);

    if (featureSAVEnabled)
    {
        if (depth >= maxAssetCheckDepth)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // requireAuth is recursive if the issuer is a vault pseudo-account
        if (!mptIssuer.exists())
            return tefINTERNAL;  // LCOV_EXCL_LINE

        if (mptIssuer->isFieldPresent(sfVaultID))
        {
            auto const sleVault = readView_.read(keylet::vault(mptIssuer->getFieldH256(sfVaultID)));
            if (!sleVault)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            auto const asset = sleVault->at(sfAsset);
            if (auto const err =
                    makeTokenBase(readView_, asset)->requireAuth(account, authType, depth + 1);
                !isTesSuccess(err))
                return err;
        }
    }

    auto const sleToken = readView_.read(keylet::mptoken(mptID_, account));

    // if account has no MPToken, fail
    if (!sleToken && (authType == AuthType::StrongAuth || authType == AuthType::Legacy))
        return tecNO_AUTH;

    // Note, this check is not amendment-gated because DomainID will be always
    // empty **unless** writing to it has been enabled by an amendment
    auto const maybeDomainID = sle_->at(~sfDomainID);
    if (maybeDomainID)
    {
        XRPL_ASSERT(
            sle_->getFieldU32(sfFlags) & lsfMPTRequireAuth,
            "xrpl::requireAuth : issuance requires authorization");
        // ter = tefINTERNAL | tecOBJECT_NOT_FOUND | tecNO_AUTH | tecEXPIRED
        auto const ter = credentials::validDomain(readView_, *maybeDomainID, account);
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
        if (isPseudoAccount(readView_, account, {&sfVaultID, &sfLoanBrokerID}))
            return tesSUCCESS;
    }

    // mptoken must be authorized if issuance enabled requireAuth
    if (sle_->isFlag(lsfMPTRequireAuth) && (!sleToken || !sleToken->isFlag(lsfMPTAuthorized)))
        return tecNO_AUTH;

    return tesSUCCESS;  // Note: sleToken might be null
}

[[nodiscard]] TER
WritableMPTokenIssuance::enforceMPTokenAuthorization(
    AccountID const& account,
    XRPAmount const& priorBalance,  // for MPToken authorization
    beast::Journal j)
{
    if (!mutableSle_)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        mutableSle_->isFlag(lsfMPTRequireAuth),
        "xrpl::enforceMPTokenAuthorization : authorization required");

    if (account == mutableSle_->at(sfIssuer))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const keylet = keylet::mptoken(mptID_, account);
    auto const sleToken = readView_.read(keylet);  //  NOTE: might be null
    auto const maybeDomainID = mutableSle_->at(~sfDomainID);
    bool expired = false;
    bool const authorizedByDomain = [&]() -> bool {
        // NOTE: defensive here, should be checked in preclaim
        if (!maybeDomainID)
            return false;  // LCOV_EXCL_LINE

        auto const ter = verifyValidDomain(applyView(), account, *maybeDomainID, j);
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
    if (!authorizedByDomain && maybeDomainID)
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
            sleToken != nullptr && !maybeDomainID,
            "xrpl::enforceMPTokenAuthorization : found MPToken");
        if (sleToken->isFlag(lsfMPTAuthorized))
            return tesSUCCESS;

        return tecNO_AUTH;
    }
    if (authorizedByDomain && sleToken != nullptr)
    {
        // Found an MPToken, authorized by the domain. Ignore authorization flag
        // lsfMPTAuthorized because it is meaningless. Return tesSUCCESS
        XRPL_ASSERT(maybeDomainID, "xrpl::enforceMPTokenAuthorization : found MPToken for domain");
        return tesSUCCESS;
    }
    if (authorizedByDomain)
    {
        // Could not find MPToken but there should be one because we are
        // authorized by domain. Proceed to create it, then return tesSUCCESS
        XRPL_ASSERT(
            maybeDomainID && sleToken == nullptr,
            "xrpl::enforceMPTokenAuthorization : new MPToken for domain");
        if (auto const err = authorizeMPToken(
                priorBalance,  // priorBalance
                account,       // account
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
MPTokenIssuance::canTransfer(AccountID const& from, AccountID const& to) const
{
    if (!sle_)
        return tecOBJECT_NOT_FOUND;

    if (!(sle_->getFieldU32(sfFlags) & lsfMPTCanTransfer))
    {
        if (from != (*sle_)[sfIssuer] && to != (*sle_)[sfIssuer])
            return TER{tecNO_AUTH};
    }
    return tesSUCCESS;
}

//------------------------------------------------------------------------------
//
// Token capability checks (MPT-specific)
//
//------------------------------------------------------------------------------

bool
MPTokenIssuance::canClawback() const
{
    if (!sle_)
        return false;
    return sle_->isFlag(lsfMPTCanClawback);
}

bool
MPTokenIssuance::requiresAuth() const
{
    if (!sle_)
        return false;
    return sle_->isFlag(lsfMPTRequireAuth);
}

TER
rippleLockEscrowMPT(
    ApplyView& view,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal j)
{
    auto const mptIssue = amount.get<MPTIssue>();
    auto mptIssuance = WritableMPTokenIssuance(view, mptIssue);
    if (!mptIssuance.exists())
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleLockEscrowMPT: MPT issuance not found for "
                        << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    if (mptIssuance.getIssuer() == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleLockEscrowMPT: sender is the issuer, cannot lock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    // 1. Decrease the MPT Holder MPTAmount
    // 2. Increase the MPT Holder EscrowedAmount
    {
        auto const mptokenID = keylet::mptoken(mptIssuance.getMptID(), sender);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleLockEscrowMPT: MPToken not found for " << sender;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto const amt = sle->getFieldU64(sfMPTAmount);
        auto const pay = amount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, amt), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleLockEscrowMPT: insufficient MPTAmount for "
                            << to_string(sender) << ": " << amt << " < " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        (*sle)[sfMPTAmount] = amt - pay;

        // Overflow check for addition
        uint64_t const locked = (*sle)[~sfLockedAmount].value_or(0);

        if (!canAdd(STAmount(mptIssue, locked), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleLockEscrowMPT: overflow on locked amount for "
                            << to_string(sender) << ": " << locked << " + " << pay;
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
        uint64_t const issuanceEscrowed = (*mptIssuance)[~sfLockedAmount].value_or(0);
        auto const pay = amount.mpt().value();

        // Overflow check for addition
        if (!canAdd(STAmount(mptIssue, issuanceEscrowed), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleLockEscrowMPT: overflow on issuance "
                               "locked amount for "
                            << mptIssue.getMptID() << ": " << issuanceEscrowed << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (mptIssuance->isFieldPresent(sfLockedAmount))
        {
            (*mptIssuance)[sfLockedAmount] += pay;
        }
        else
        {
            mptIssuance->setFieldU64(sfLockedAmount, pay);
        }

        mptIssuance.update();
    }
    return tesSUCCESS;
}

TER
rippleUnlockEscrowMPT(
    ApplyView& view,
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j)
{
    if (!view.rules().enabled(fixTokenEscrowV1))
    {
        XRPL_ASSERT(
            netAmount == grossAmount, "xrpl::rippleUnlockEscrowMPT : netAmount == grossAmount");
    }

    auto const& issuer = netAmount.getIssuer();
    auto const& mptIssue = netAmount.get<MPTIssue>();
    auto mptIssuance = WritableMPTokenIssuance(view, mptIssue);
    if (!mptIssuance)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleUnlockEscrowMPT: MPT issuance not found for "
                        << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    // Decrease the Issuance EscrowedAmount
    {
        if (!mptIssuance->isFieldPresent(sfLockedAmount))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleUnlockEscrowMPT: no locked amount in issuance for "
                            << mptIssue.getMptID();
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const locked = mptIssuance->getFieldU64(sfLockedAmount);
        auto const redeem = grossAmount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, locked), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleUnlockEscrowMPT: insufficient locked amount for "
                            << mptIssue.getMptID() << ": " << locked << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const newLocked = locked - redeem;
        if (newLocked == 0)
        {
            mptIssuance->makeFieldAbsent(sfLockedAmount);
        }
        else
        {
            mptIssuance->setFieldU64(sfLockedAmount, newLocked);
        }
        mptIssuance.update();
    }

    if (issuer != receiver)
    {
        // Increase the MPT Holder MPTAmount
        auto const mptokenID = keylet::mptoken(mptIssue.getMptID(), receiver);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleUnlockEscrowMPT: MPToken not found for " << receiver;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto current = sle->getFieldU64(sfMPTAmount);
        auto delta = netAmount.mpt().value();

        // Overflow check for addition
        if (!canAdd(STAmount(mptIssue, current), STAmount(mptIssue, delta)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleUnlockEscrowMPT: overflow on MPTAmount for "
                            << to_string(receiver) << ": " << current << " + " << delta;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        (*sle)[sfMPTAmount] += delta;
        view.update(sle);
    }
    else
    {
        // Decrease the Issuance OutstandingAmount
        auto const outstanding = mptIssuance->getFieldU64(sfOutstandingAmount);
        auto const redeem = netAmount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleUnlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        mptIssuance->setFieldU64(sfOutstandingAmount, outstanding - redeem);
        mptIssuance.update();
    }

    if (issuer == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleUnlockEscrowMPT: sender is the issuer, "
                           "cannot unlock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP
    // Decrease the MPT Holder EscrowedAmount
    auto const mptokenID = keylet::mptoken(mptIssue.getMptID(), sender);
    auto sle = view.peek(mptokenID);
    if (!sle)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleUnlockEscrowMPT: MPToken not found for " << sender;
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    if (!sle->isFieldPresent(sfLockedAmount))
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleUnlockEscrowMPT: no locked amount in MPToken for "
                        << to_string(sender);
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    auto const locked = sle->getFieldU64(sfLockedAmount);
    auto const delta = grossAmount.mpt().value();

    // Underflow check for subtraction
    if (!canSubtract(STAmount(mptIssue, locked), STAmount(mptIssue, delta)))
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleUnlockEscrowMPT: insufficient locked amount for "
                        << to_string(sender) << ": " << locked << " < " << delta;
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
        auto const outstanding = mptIssuance->getFieldU64(sfOutstandingAmount);
        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, diff)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleUnlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << diff;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        mptIssuance->setFieldU64(sfOutstandingAmount, outstanding - diff);
        mptIssuance.update();
    }
    return tesSUCCESS;
}

STAmount
MPTokenIssuance::accountHolds(
    AccountID const& account,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance) const
{
    return accountHolds(account, zeroIfFrozen, ahIGNORE_AUTH, j, includeFullBalance);
}

STAmount
MPTokenIssuance::accountHolds(
    AccountID const& account,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance) const
{
    bool const returnSpendable = (includeFullBalance == shFULL_BALANCE);

    if (returnSpendable && account == mptIssue_.getIssuer())
    {
        // if the account is the issuer, and the issuance exists, their limit is
        // the issuance limit minus the outstanding value

        if (!sle_)
        {
            return STAmount{mptIssue_};
        }
        return STAmount{
            mptIssue_,
            sle_->at(~sfMaximumAmount).value_or(maxMPTokenAmount) - sle_->at(sfOutstandingAmount)};
    }

    STAmount amount;

    auto const sleMpt = readView_.read(keylet::mptoken(mptID_, account));

    if (!sleMpt)
    {
        amount.clear(mptIssue_);
    }
    else if (zeroIfFrozen == fhZERO_IF_FROZEN && isFrozen(account))
    {
        amount.clear(mptIssue_);
    }
    else
    {
        amount = STAmount{mptIssue_, sleMpt->getFieldU64(sfMPTAmount)};

        // Only if auth check is needed, as it needs to do an additional read
        // operation. Note featureSingleAssetVault will affect error codes.
        if (zeroIfUnauthorized == ahZERO_IF_UNAUTHORIZED &&
            readView_.rules().enabled(featureSingleAssetVault))
        {
            if (auto const err = requireAuth(account, AuthType::StrongAuth); !isTesSuccess(err))
                amount.clear(mptIssue_);
        }
        else if (zeroIfUnauthorized == ahZERO_IF_UNAUTHORIZED)
        {
            // if auth is enabled on the issuance and mpt is not authorized,
            // clear amount
            if (sle_ && sle_->isFlag(lsfMPTRequireAuth) && !sleMpt->isFlag(lsfMPTAuthorized))
                amount.clear(mptIssue_);
        }
    }

    return amount;
}

}  // namespace xrpl
