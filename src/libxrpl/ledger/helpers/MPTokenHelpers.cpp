#include <xrpl/ledger/helpers/MPTokenHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>

namespace xrpl {

template <typename ViewT>
bool
MPTokenIssuance<ViewT>::isGlobalFrozen() const
{
    if (this->exists())
        return this->sle_->isFlag(lsfMPTLocked);
    return false;
}

template <typename ViewT>
bool
MPTokenIssuance<ViewT>::isIndividualFrozen(AccountID const& account) const
{
    if (auto const sle = this->readView().read(keylet::mptoken(mptID_, account)))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

template <typename ViewT>
bool
MPTokenIssuance<ViewT>::isFrozen(AccountID const& account, int depth) const
{
    return isGlobalFrozen() || isIndividualFrozen(account) ||
        isVaultPseudoAccountFrozen(this->readView(), account, mptIssue_, depth);
}

template <typename ViewT>
[[nodiscard]] bool
MPTokenIssuance<ViewT>::isAnyFrozen(std::initializer_list<AccountID> const& accounts, int depth)
    const
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
        if (isVaultPseudoAccountFrozen(this->readView(), account, mptIssue_, depth))
            return true;
    }

    return false;
}

template <typename ViewT>
Rate
MPTokenIssuance<ViewT>::transferRate() const
{
    // fee is 0-50,000 (0-50%), rate is 1,000,000,000-2,000,000,000
    // For example, if transfer fee is 50% then 10,000 * 50,000 = 500,000
    // which represents 50% of 1,000,000,000
    if (this->exists() && this->sle_->isFieldPresent(sfTransferFee))
    {
        auto const fee = this->sle_->getFieldU16(sfTransferFee);
        XRPL_ASSERT(fee <= kMaxTransferFee, "xrpl::transferRate : fee is too large");
        return Rate{1'000'000'000u + (10'000 * fee)};
    }

    return kParityRate;
}

template <typename ViewT>
[[nodiscard]] TER
MPTokenIssuance<ViewT>::canAddHolding() const
{
    if (!this->exists())
    {
        return tecOBJECT_NOT_FOUND;
    }
    if (!this->sle_->isFlag(lsfMPTCanTransfer))
    {
        return tecNO_AUTH;
    }

    return tesSUCCESS;
}

template <typename ViewT>
[[nodiscard]] TER
MPTokenIssuance<ViewT>::addEmptyHolding(
    AccountID const& accountID,
    XRPAmount priorBalance,
    beast::Journal journal)
    requires kIsWritable
{
    if (!this->canModify())
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (this->sle_->isFlag(lsfMPTLocked))
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (this->applyView().peek(keylet::mptoken(mptID_, accountID)))
        return tecDUPLICATE;
    if (accountID == getIssuer())
        return tesSUCCESS;

    return authorizeMPToken(priorBalance, accountID, journal);
}

template <typename ViewT>
[[nodiscard]] TER
MPTokenIssuance<ViewT>::authorizeMPToken(
    XRPAmount const& priorBalance,
    AccountID const& account,
    beast::Journal journal,
    std::uint32_t flags,
    std::optional<AccountID> holderID)
    requires kIsWritable
{
    ApplyView& view = this->applyView();
    WAccountRoot wrappedAcct(account, view, journal);
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
        if ((flags & tfMPTUnauthorize) != 0u)
        {
            auto const mptokenKey = keylet::mptoken(mptID_, account);
            auto const sleMpt = view.peek(mptokenKey);
            if (!sleMpt || (*sleMpt)[sfMPTAmount] != 0 ||
                (view.rules().enabled(fixCleanup3_1_3) &&
                 (*sleMpt)[~sfLockedAmount].valueOr(0) != 0))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (!view.dirRemove(
                    keylet::ownerDir(account), (*sleMpt)[sfOwnerNode], sleMpt->key(), false))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            wrappedAcct.adjustOwnerCount(-1);

            view.erase(sleMpt);
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
            (uOwnerCount < 2) ? XRPAmount(beast::kZero)
                              : view.fees().accountReserve(uOwnerCount + 1));

        if (priorBalance < reserveCreate)
            return tecINSUFFICIENT_RESERVE;

        // Defensive check before we attempt to create MPToken for the issuer
        if (!this->sle_ || this->sle_->getAccountID(sfIssuer) == account)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::authorizeMPToken : invalid issuance or issuers token");
            if (view.rules().enabled(featureLendingProtocol))
                return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        auto const mptokenKey = keylet::mptoken(mptID_, account);
        auto mptoken = std::make_shared<SLE>(mptokenKey);
        if (auto ter = dirLink(view, account, mptoken))
            return ter;  // LCOV_EXCL_LINE

        (*mptoken)[sfAccount] = account;
        (*mptoken)[sfMPTokenIssuanceID] = mptID_;
        (*mptoken)[sfFlags] = 0;
        view.insert(mptoken);

        // Update owner count.
        wrappedAcct.adjustOwnerCount(1);

        return tesSUCCESS;
    }

    if (!this->sle_)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // If the account that submitted this tx is the issuer of the MPT
    // Note: `account_` is issuer's account
    //       `holderID` is holder's account
    if (account != (*this->sle_)[sfIssuer])
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMpt = view.peek(keylet::mptoken(mptID_, *holderID));
    if (!sleMpt)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sleMpt->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    // Issuer wants to unauthorize the holder, unset lsfMPTAuthorized on
    // their MPToken
    if ((flags & tfMPTUnauthorize) != 0u)
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

template <typename ViewT>
[[nodiscard]] TER
MPTokenIssuance<ViewT>::removeEmptyHolding(AccountID const& accountID, beast::Journal journal)
    requires kIsWritable
{
    ApplyView& view = this->applyView();
    // If the account is the issuer, then no token should exist. MPTs do not
    // have the legacy ability to create such a situation, but check anyway. If
    // a token does exist, it will get deleted. If not, return success.
    bool const accountIsIssuer = accountID == getIssuer();
    auto const mptoken = view.peek(keylet::mptoken(mptID_, accountID));
    if (!mptoken)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    // Unlike a trust line, if the account is the issuer, and the token has a
    // balance, it can not just be deleted, because that will throw the issuance
    // accounting out of balance, so fail. Since this should be impossible
    // anyway, I'm not going to put any effort into it.
    if (mptoken->at(sfMPTAmount) != 0 ||
        (view.rules().enabled(fixCleanup3_1_3) && (*mptoken)[~sfLockedAmount].valueOr(0) != 0))
        return tecHAS_OBLIGATIONS;

    return authorizeMPToken(
        {},  // priorBalance
        accountID,
        journal,
        tfMPTUnauthorize  // flags
    );
}

template <typename ViewT>
[[nodiscard]] TER
MPTokenIssuance<ViewT>::requireAuth(AccountID const& account, AuthType authType, int depth) const
{
    if (!this->exists())
        return tecOBJECT_NOT_FOUND;

    auto const mptIssuer = AccountRoot(this->sle_->getAccountID(sfIssuer), this->readView());

    // issuer is always "authorized"
    if (mptIssuer == account)  // Issuer won't have MPToken
        return tesSUCCESS;

    bool const featureSAVEnabled = this->readView().rules().enabled(featureSingleAssetVault);

    if (featureSAVEnabled)
    {
        if (depth >= kMaxAssetCheckDepth)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // requireAuth is recursive if the issuer is a vault pseudo-account
        if (!mptIssuer.exists())
            return tefINTERNAL;  // LCOV_EXCL_LINE

        if (mptIssuer->isFieldPresent(sfVaultID))
        {
            auto const sleVault =
                this->readView().read(keylet::vault(mptIssuer->getFieldH256(sfVaultID)));
            if (!sleVault)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            auto const asset = sleVault->at(sfAsset);
            if (auto const err = asset.visit(
                    [&](Issue const& issue) {
                        return IOUIssuance(this->readView(), issue).requireAuth(account, authType);
                    },
                    [&](MPTIssue const& issue) {
                        return MPTokenIssuance<ReadView>(this->readView(), issue)
                            .requireAuth(account, authType, depth + 1);
                    });
                !isTesSuccess(err))
                return err;
        }
    }

    auto const mptokenID = keylet::mptoken(mptID_, account);
    auto const sleToken = this->readView().read(mptokenID);

    // if account has no MPToken, fail
    if (!sleToken && (authType == AuthType::StrongAuth || authType == AuthType::Legacy))
        return tecNO_AUTH;

    // Note, this check is not amendment-gated because DomainID will be always
    // empty **unless** writing to it has been enabled by an amendment
    auto const maybeDomainID = this->sle_->at(~sfDomainID);
    if (maybeDomainID)
    {
        XRPL_ASSERT(
            this->sle_->isFlag(lsfMPTRequireAuth),
            "xrpl::requireAuth : issuance requires authorization");
        // ter = tefINTERNAL | tecOBJECT_NOT_FOUND | tecNO_AUTH | tecEXPIRED
        auto const ter = credentials::validDomain(this->readView(), *maybeDomainID, account);
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
        AccountRoot const accountRoot(account, this->readView());
        if (accountRoot.isPseudoAccount({&sfVaultID, &sfLoanBrokerID}))
            return tesSUCCESS;
    }

    // mptoken must be authorized if issuance enabled requireAuth
    if (this->sle_->isFlag(lsfMPTRequireAuth) && (!sleToken || !sleToken->isFlag(lsfMPTAuthorized)))
        return tecNO_AUTH;

    return tesSUCCESS;  // Note: sleToken might be null
}

template <typename ViewT>
[[nodiscard]] TER
MPTokenIssuance<ViewT>::enforceMPTokenAuthorization(
    AccountID const& account,
    XRPAmount const& priorBalance,  // for MPToken authorization
    beast::Journal j)
    requires kIsWritable
{
    if (!this->canModify())
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        this->sle_->isFlag(lsfMPTRequireAuth),
        "xrpl::enforceMPTokenAuthorization : authorization required");

    if (account == this->sle_->at(sfIssuer))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const keylet = keylet::mptoken(mptID_, account);
    auto const sleToken = this->readView().read(keylet);  //  NOTE: might be null
    auto const maybeDomainID = this->sle_->at(~sfDomainID);
    bool expired = false;
    bool const authorizedByDomain = [&]() -> bool {
        // NOTE: defensive here, should be checked in preclaim
        if (!static_cast<bool>(maybeDomainID))
            return false;  // LCOV_EXCL_LINE

        auto const ter = verifyValidDomain(this->applyView(), account, *maybeDomainID, j);
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
    if (!authorizedByDomain && static_cast<bool>(maybeDomainID))
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
            sleToken != nullptr && !static_cast<bool>(maybeDomainID),
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
            static_cast<bool>(maybeDomainID),
            "xrpl::enforceMPTokenAuthorization : found MPToken for domain");
        return tesSUCCESS;
    }
    if (authorizedByDomain)
    {
        // Could not find MPToken but there should be one because we are
        // authorized by domain. Proceed to create it, then return tesSUCCESS
        XRPL_ASSERT(
            static_cast<bool>(maybeDomainID) && sleToken == nullptr,
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

template <typename ViewT>
TER
MPTokenIssuance<ViewT>::canTransfer(AccountID const& from, AccountID const& to) const
{
    if (!this->exists())
        return tecOBJECT_NOT_FOUND;

    if (!this->sle_->isFlag(lsfMPTCanTransfer))
    {
        if (from != (*this->sle_)[sfIssuer] && to != (*this->sle_)[sfIssuer])
            return TER{tecNO_AUTH};
    }
    return tesSUCCESS;
}

template <typename ViewT>
bool
MPTokenIssuance<ViewT>::requiresAuth() const
{
    if (!this->exists())
        return false;
    return this->sle_->isFlag(lsfMPTRequireAuth);
}

template <typename ViewT>
bool
MPTokenIssuance<ViewT>::canClawback() const
{
    if (!this->exists())
        return false;
    return this->sle_->isFlag(lsfMPTCanClawback);
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

template <typename ViewT>
TER
MPTokenIssuance<ViewT>::lockEscrow(
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal j)
    requires kIsWritable
{
    ApplyView& view = this->applyView();
    auto const& mptIssue = mptIssue_;
    if (!this->exists())
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
        auto const mptokenID = keylet::mptoken(mptID_, sender);
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
        uint64_t const locked = (*sle)[~sfLockedAmount].valueOr(0);

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
        uint64_t const issuanceEscrowed = (*this->sle_)[~sfLockedAmount].valueOr(0);
        auto const pay = amount.mpt().value();

        // Overflow check for addition
        if (!canAdd(STAmount(mptIssue, issuanceEscrowed), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: overflow on issuance "
                               "locked amount for "
                            << mptIssue.getMptID() << ": " << issuanceEscrowed << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (this->sle_->isFieldPresent(sfLockedAmount))
        {
            (*this->sle_)[sfLockedAmount] += pay;
        }
        else
        {
            this->sle_->setFieldU64(sfLockedAmount, pay);
        }

        this->update();
    }
    return tesSUCCESS;
}

template <typename ViewT>
TER
MPTokenIssuance<ViewT>::unlockEscrow(
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j)
    requires kIsWritable
{
    ApplyView& view = this->applyView();
    if (!view.rules().enabled(fixTokenEscrowV1))
    {
        XRPL_ASSERT(netAmount == grossAmount, "xrpl::unlockEscrowMPT : netAmount == grossAmount");
    }

    auto const& issuer = netAmount.getIssuer();
    auto const& mptIssue = mptIssue_;
    if (!this->exists())
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: MPT issuance not found for " << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    // Decrease the Issuance EscrowedAmount
    {
        if (!this->sle_->isFieldPresent(sfLockedAmount))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: no locked amount in issuance for "
                            << mptIssue.getMptID();
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const locked = this->sle_->getFieldU64(sfLockedAmount);
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
            this->sle_->makeFieldAbsent(sfLockedAmount);
        }
        else
        {
            this->sle_->setFieldU64(sfLockedAmount, newLocked);
        }
        this->update();
    }

    if (issuer != receiver)
    {
        // Increase the MPT Holder MPTAmount
        auto const mptokenID = keylet::mptoken(mptID_, receiver);
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
        auto const outstanding = this->sle_->getFieldU64(sfOutstandingAmount);
        auto const redeem = netAmount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        this->sle_->setFieldU64(sfOutstandingAmount, outstanding - redeem);
        this->update();
    }

    if (issuer == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: sender is the issuer, "
                           "cannot unlock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP
    // Decrease the MPT Holder EscrowedAmount
    auto const mptokenID = keylet::mptoken(mptID_, sender);
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
        auto const outstanding = this->sle_->getFieldU64(sfOutstandingAmount);
        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, diff)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << diff;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        this->sle_->setFieldU64(sfOutstandingAmount, outstanding - diff);
        this->update();
    }
    return tesSUCCESS;
}

template <typename ViewT>
TER
MPTokenIssuance<ViewT>::createMPToken(AccountID const& account, std::uint32_t flags)
    requires kIsWritable
{
    ApplyView& view = this->applyView();
    auto const mptokenKey = keylet::mptoken(mptID_, account);

    auto const ownerNode =
        view.dirInsert(keylet::ownerDir(account), mptokenKey, describeOwnerDir(account));

    if (!ownerNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto mptoken = std::make_shared<SLE>(mptokenKey);
    (*mptoken)[sfAccount] = account;
    (*mptoken)[sfMPTokenIssuanceID] = mptID_;
    (*mptoken)[sfFlags] = flags;
    (*mptoken)[sfOwnerNode] = *ownerNode;

    view.insert(mptoken);

    return tesSUCCESS;
}

template <typename ViewT>
TER
MPTokenIssuance<ViewT>::checkCreateMPT(AccountID const& holder, beast::Journal j)
    requires kIsWritable
{
    ApplyView& view = this->applyView();
    if (getIssuer() == holder)
        return tesSUCCESS;

    auto const mptokenID = keylet::mptoken(mptID_, holder);
    if (!view.exists(mptokenID))
    {
        if (auto const err = createMPToken(holder, 0); !isTesSuccess(err))
        {
            return err;
        }
        auto acct = WAccountRoot(holder, view, j);
        if (!acct)
        {
            return tecINTERNAL;
        }
        acct.adjustOwnerCount(1);
    }
    return tesSUCCESS;
}

std::int64_t
maxMPTAmount(SLE const& sleIssuance)
{
    return sleIssuance[~sfMaximumAmount].value_or(kMaxMpTokenAmount);
}

std::int64_t
availableMPTAmount(SLE const& sleIssuance)
{
    auto const max = maxMPTAmount(sleIssuance);
    auto const outstanding = sleIssuance[sfOutstandingAmount];
    return max - outstanding;
}

template <typename ViewT>
std::int64_t
MPTokenIssuance<ViewT>::maxAmount() const
{
    if (!this->exists())
        Throw<std::runtime_error>(transHuman(tecINTERNAL));
    return maxMPTAmount(*this->sle_);
}

template <typename ViewT>
std::int64_t
MPTokenIssuance<ViewT>::availableAmount() const
{
    if (!this->exists())
        Throw<std::runtime_error>(transHuman(tecINTERNAL));
    return availableMPTAmount(*this->sle_);
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

template <typename ViewT>
STAmount
MPTokenIssuance<ViewT>::issuerFundsToSelfIssue() const
{
    STAmount amount{mptIssue_};

    if (!this->exists())
        return amount;
    auto const available = availableMPTAmount(*this->sle_);
    return this->readView().balanceHookSelfIssueMPT(mptIssue_, available);
}

template <typename ViewT>
void
MPTokenIssuance<ViewT>::issuerSelfDebitHook(std::uint64_t amount)
    requires kIsWritable
{
    auto const available = availableAmount();
    this->applyView().issuerSelfDebitHookMPT(mptIssue_, amount, available);
}

[[nodiscard]] TER
deleteAMMMPToken(
    ApplyView& view,
    std::shared_ptr<SLE> sleMpt,
    AccountID const& ammAccountID,
    beast::Journal j)
{
    if (!view.dirRemove(
            keylet::ownerDir(ammAccountID), (*sleMpt)[sfOwnerNode], sleMpt->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    view.erase(sleMpt);

    return tesSUCCESS;
}

static TER
checkMPTAllowed(ReadView const& view, TxType txType, Asset const& asset, AccountID const& accountID)
{
    if (!asset.holds<MPTIssue>())
        return tesSUCCESS;

    auto const& issuanceID = asset.get<MPTIssue>().getMptID();
    auto const validTx = txType == ttAMM_CREATE || txType == ttAMM_DEPOSIT ||
        txType == ttAMM_WITHDRAW || txType == ttOFFER_CREATE || txType == ttCHECK_CREATE ||
        txType == ttCHECK_CASH || txType == ttPAYMENT;
    XRPL_ASSERT(validTx, "xrpl::checkMPTAllowed : all MPT tx or DEX");
    if (!validTx)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& issuer = asset.getIssuer();
    if (!view.exists(keylet::account(issuer)))
        return tecNO_ISSUER;  // LCOV_EXCL_LINE

    auto const issuanceKey = keylet::mptIssuance(issuanceID);
    auto const issuanceSle = view.read(issuanceKey);
    if (!issuanceSle)
        return tecOBJECT_NOT_FOUND;  // LCOV_EXCL_LINE

    auto const flags = issuanceSle->getFlags();

    if ((flags & lsfMPTLocked) != 0u)
        return tecLOCKED;  // LCOV_EXCL_LINE
    // Offer crossing and Payment
    if ((flags & lsfMPTCanTrade) == 0)
        return tecNO_PERMISSION;

    if (accountID != issuer)
    {
        if ((flags & lsfMPTCanTransfer) == 0)
            return tecNO_PERMISSION;

        auto const mptSle = view.read(keylet::mptoken(issuanceKey.key, accountID));
        // Allow to succeed since some tx create MPToken if it doesn't exist.
        // Tx's have their own check for missing MPToken.
        if (!mptSle)
            return tesSUCCESS;

        if (mptSle->isFlag(lsfMPTLocked))
            return tecLOCKED;
    }

    return tesSUCCESS;
}

TER
checkMPTTxAllowed(
    ReadView const& view,
    TxType txType,
    Asset const& asset,
    AccountID const& accountID)
{
    // use isDEXAllowed for payment/offer crossing
    XRPL_ASSERT(txType != ttPAYMENT, "xrpl::checkMPTTxAllowed : not payment");
    return checkMPTAllowed(view, txType, asset, accountID);
}

// Explicit template instantiations
template class MPTokenIssuance<ReadView>;
template class MPTokenIssuance<ApplyView>;

}  // namespace xrpl
