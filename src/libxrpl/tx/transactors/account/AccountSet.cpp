#include <xrpl/tx/transactors/account/AccountSet.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <unistd.h>

#include <cstdint>

namespace xrpl {

TxConsequences
AccountSet::makeTxConsequences(PreflightContext const& ctx)
{
    // The AccountSet may be a blocker, but only if it sets or clears
    // specific account flags.
    auto getTxConsequencesCategory = [](STTx const& tx) {
        if (std::uint32_t const uTxFlags = tx.getFlags();
            uTxFlags & (tfRequireAuth | tfOptionalAuth))
            return TxConsequences::Category::Blocker;

        if (auto const uSetFlag = tx[~sfSetFlag]; uSetFlag &&
            (*uSetFlag == asfRequireAuth || *uSetFlag == asfDisableMaster ||
             *uSetFlag == asfAccountTxnID))
            return TxConsequences::Category::Blocker;

        if (auto const uClearFlag = tx[~sfClearFlag]; uClearFlag &&
            (*uClearFlag == asfRequireAuth || *uClearFlag == asfDisableMaster ||
             *uClearFlag == asfAccountTxnID))
            return TxConsequences::Category::Blocker;

        return TxConsequences::Category::Normal;
    };

    return TxConsequences{ctx.tx, getTxConsequencesCategory(ctx.tx)};
}

std::uint32_t
AccountSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfAccountSetMask;
}

NotTEC
AccountSet::preflight(PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uSetFlag = tx.getFieldU32(sfSetFlag);
    std::uint32_t const uClearFlag = tx.getFieldU32(sfClearFlag);

    if ((uSetFlag != 0) && (uSetFlag == uClearFlag))
    {
        JLOG(j.trace()) << "Malformed transaction: Set and clear same flag.";
        return temINVALID_FLAG;
    }

    //
    // RequireAuth
    //
    bool const bSetRequireAuth = tx.isFlag(tfRequireAuth) || (uSetFlag == asfRequireAuth);
    bool const bClearRequireAuth = tx.isFlag(tfOptionalAuth) || (uClearFlag == asfRequireAuth);

    if (bSetRequireAuth && bClearRequireAuth)
    {
        JLOG(j.trace()) << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    //
    // RequireDestTag
    //
    bool const bSetRequireDest = tx.isFlag(tfRequireDestTag) || (uSetFlag == asfRequireDest);
    bool const bClearRequireDest = tx.isFlag(tfOptionalDestTag) || (uClearFlag == asfRequireDest);

    if (bSetRequireDest && bClearRequireDest)
    {
        JLOG(j.trace()) << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    //
    // DisallowXRP
    //
    bool const bSetDisallowXRP = tx.isFlag(tfDisallowXRP) || (uSetFlag == asfDisallowXRP);
    bool const bClearDisallowXRP = tx.isFlag(tfAllowXRP) || (uClearFlag == asfDisallowXRP);

    if (bSetDisallowXRP && bClearDisallowXRP)
    {
        JLOG(j.trace()) << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    // TransferRate
    if (tx.isFieldPresent(sfTransferRate))
    {
        std::uint32_t const uRate = tx.getFieldU32(sfTransferRate);

        if ((uRate != 0u) && (uRate < QUALITY_ONE))
        {
            JLOG(j.trace()) << "Malformed transaction: Transfer rate too small.";
            return temBAD_TRANSFER_RATE;
        }

        if (uRate > 2 * QUALITY_ONE)
        {
            JLOG(j.trace()) << "Malformed transaction: Transfer rate too large.";
            return temBAD_TRANSFER_RATE;
        }
    }

    // TickSize
    if (tx.isFieldPresent(sfTickSize))
    {
        auto uTickSize = tx[sfTickSize];
        if ((uTickSize != 0u) &&
            ((uTickSize < Quality::kMinTickSize) || (uTickSize > Quality::kMaxTickSize)))
        {
            JLOG(j.trace()) << "Malformed transaction: Bad tick size.";
            return temBAD_TICK_SIZE;
        }
    }

    if (auto const mk = tx[~sfMessageKey])
    {
        if (!mk->empty() && !publicKeyType({mk->data(), mk->size()}))
        {
            JLOG(j.trace()) << "Invalid message key specified.";
            return telBAD_PUBLIC_KEY;
        }
    }

    if (auto const domain = tx[~sfDomain]; domain && domain->size() > kMaxDomainLength)
    {
        JLOG(j.trace()) << "domain too long";
        return telBAD_DOMAIN;
    }

    // Configure authorized minting account:
    if (uSetFlag == asfAuthorizedNFTokenMinter && !tx.isFieldPresent(sfNFTokenMinter))
        return temMALFORMED;

    if (uClearFlag == asfAuthorizedNFTokenMinter && tx.isFieldPresent(sfNFTokenMinter))
        return temMALFORMED;

    return tesSUCCESS;
}

NotTEC
AccountSet::checkPermission(ReadView const& view, STTx const& tx)
{
    // AccountSet is prohibited to be granted on a transaction level,
    // but some granular permissions are allowed.
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttACCOUNT_SET, granularPermissions);

    auto const uSetFlag = tx.getFieldU32(sfSetFlag);
    auto const uClearFlag = tx.getFieldU32(sfClearFlag);
    // We don't support any flag based granular permission under
    // AccountSet transaction. If any delegated account is trying to
    // update the flag on behalf of another account, it is not
    // authorized.
    if (uSetFlag != 0 || uClearFlag != 0 || ((tx.getFlags() & tfUniversalMask) != 0u))
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfEmailHash) && !granularPermissions.contains(AccountEmailHashSet))
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfWalletLocator) || tx.isFieldPresent(sfNFTokenMinter))
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfMessageKey) && !granularPermissions.contains(AccountMessageKeySet))
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfDomain) && !granularPermissions.contains(AccountDomainSet))
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfTransferRate) && !granularPermissions.contains(AccountTransferRateSet))
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfTickSize) && !granularPermissions.contains(AccountTickSizeSet))
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
AccountSet::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    AccountRoot const acctRoot(id, ctx.view);
    if (!acctRoot)
        return terNO_ACCOUNT;

    std::uint32_t const uSetFlag = ctx.tx.getFieldU32(sfSetFlag);

    // legacy AccountSet flags
    bool const bSetRequireAuth = ctx.tx.isFlag(tfRequireAuth) || (uSetFlag == asfRequireAuth);

    //
    // RequireAuth
    //
    if (bSetRequireAuth && !acctRoot->isFlag(lsfRequireAuth))
    {
        if (!dirIsEmpty(ctx.view, keylet::ownerDir(id)))
        {
            JLOG(ctx.j.trace()) << "Retry: Owner directory not empty.";
            return ((ctx.flags & TapRetry) != 0u) ? TER{terOWNERS} : TER{tecOWNERS};
        }
    }

    //
    // Clawback
    //
    if (ctx.view.rules().enabled(featureClawback))
    {
        if (uSetFlag == asfAllowTrustLineClawback)
        {
            if (acctRoot->isFlag(lsfNoFreeze))
            {
                JLOG(ctx.j.trace()) << "Can't set Clawback if NoFreeze is set";
                return tecNO_PERMISSION;
            }

            if (!dirIsEmpty(ctx.view, keylet::ownerDir(id)))
            {
                JLOG(ctx.j.trace()) << "Owner directory not empty.";
                return tecOWNERS;
            }
        }
        else if (uSetFlag == asfNoFreeze)
        {
            // Cannot set NoFreeze if clawback is enabled
            if (acctRoot->isFlag(lsfAllowTrustLineClawback))
            {
                JLOG(ctx.j.trace()) << "Can't set NoFreeze if clawback is enabled";
                return tecNO_PERMISSION;
            }
        }
    }

    return tesSUCCESS;
}

TER
AccountSet::doApply()
{
    if (!account_)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const uFlagsIn = account_->getFieldU32(sfFlags);
    std::uint32_t uFlagsOut = uFlagsIn;

    STTx const& tx{ctx_.tx};
    std::uint32_t const uSetFlag{tx.getFieldU32(sfSetFlag)};
    std::uint32_t const uClearFlag{tx.getFieldU32(sfClearFlag)};

    // legacy AccountSet flags
    bool const bSetRequireDest{tx.isFlag(tfRequireDestTag) || (uSetFlag == asfRequireDest)};
    bool const bClearRequireDest{tx.isFlag(tfOptionalDestTag) || (uClearFlag == asfRequireDest)};
    bool const bSetRequireAuth{tx.isFlag(tfRequireAuth) || (uSetFlag == asfRequireAuth)};
    bool const bClearRequireAuth{tx.isFlag(tfOptionalAuth) || (uClearFlag == asfRequireAuth)};
    bool const bSetDisallowXRP{tx.isFlag(tfDisallowXRP) || (uSetFlag == asfDisallowXRP)};
    bool const bClearDisallowXRP{tx.isFlag(tfAllowXRP) || (uClearFlag == asfDisallowXRP)};

    bool const sigWithMaster{[&tx, &acct = accountID_]() {
        auto const spk = tx.getSigningPubKey();

        if (publicKeyType(makeSlice(spk)))
        {
            PublicKey const signingPubKey(makeSlice(spk));

            if (calcAccountID(signingPubKey) == acct)
                return true;
        }
        return false;
    }()};

    //
    // RequireAuth
    //
    if (bSetRequireAuth && !account_->isFlag(lsfRequireAuth))
    {
        JLOG(j_.trace()) << "Set RequireAuth.";
        uFlagsOut |= lsfRequireAuth;
    }

    if (bClearRequireAuth && account_->isFlag(lsfRequireAuth))
    {
        JLOG(j_.trace()) << "Clear RequireAuth.";
        uFlagsOut &= ~lsfRequireAuth;
    }

    //
    // RequireDestTag
    //
    if (bSetRequireDest && !account_->isFlag(lsfRequireDestTag))
    {
        JLOG(j_.trace()) << "Set lsfRequireDestTag.";
        uFlagsOut |= lsfRequireDestTag;
    }

    if (bClearRequireDest && account_->isFlag(lsfRequireDestTag))
    {
        JLOG(j_.trace()) << "Clear lsfRequireDestTag.";
        uFlagsOut &= ~lsfRequireDestTag;
    }

    //
    // DisallowXRP
    //
    if (bSetDisallowXRP && !account_->isFlag(lsfDisallowXRP))
    {
        JLOG(j_.trace()) << "Set lsfDisallowXRP.";
        uFlagsOut |= lsfDisallowXRP;
    }

    if (bClearDisallowXRP && account_->isFlag(lsfDisallowXRP))
    {
        JLOG(j_.trace()) << "Clear lsfDisallowXRP.";
        uFlagsOut &= ~lsfDisallowXRP;
    }

    //
    // DisableMaster
    //
    if ((uSetFlag == asfDisableMaster) && !account_->isFlag(lsfDisableMaster))
    {
        if (!sigWithMaster)
        {
            JLOG(j_.trace()) << "Must use master key to disable master key.";
            return tecNEED_MASTER_KEY;
        }

        if ((!account_->isFieldPresent(sfRegularKey)) &&
            (!view().peek(keylet::signers(accountID_))))
        {
            // Account has no regular key or multi-signer signer list.
            return tecNO_ALTERNATIVE_KEY;
        }

        JLOG(j_.trace()) << "Set lsfDisableMaster.";
        uFlagsOut |= lsfDisableMaster;
    }

    if ((uClearFlag == asfDisableMaster) && account_->isFlag(lsfDisableMaster))
    {
        JLOG(j_.trace()) << "Clear lsfDisableMaster.";
        uFlagsOut &= ~lsfDisableMaster;
    }

    //
    // DefaultRipple
    //
    if (uSetFlag == asfDefaultRipple)
    {
        JLOG(j_.trace()) << "Set lsfDefaultRipple.";
        uFlagsOut |= lsfDefaultRipple;
    }
    else if (uClearFlag == asfDefaultRipple)
    {
        JLOG(j_.trace()) << "Clear lsfDefaultRipple.";
        uFlagsOut &= ~lsfDefaultRipple;
    }

    //
    // NoFreeze
    //
    if (uSetFlag == asfNoFreeze)
    {
        if (!sigWithMaster && !account_->isFlag(lsfDisableMaster))
        {
            JLOG(j_.trace()) << "Must use master key to set NoFreeze.";
            return tecNEED_MASTER_KEY;
        }

        JLOG(j_.trace()) << "Set NoFreeze flag";
        uFlagsOut |= lsfNoFreeze;
    }

    // Anyone may set global freeze
    if (uSetFlag == asfGlobalFreeze)
    {
        JLOG(j_.trace()) << "Set GlobalFreeze flag";
        uFlagsOut |= lsfGlobalFreeze;
    }

    // If you have set NoFreeze, you may not clear GlobalFreeze
    // This prevents those who have set NoFreeze from using
    // GlobalFreeze strategically.
    if ((uSetFlag != asfGlobalFreeze) && (uClearFlag == asfGlobalFreeze) &&
        ((uFlagsOut & lsfNoFreeze) == 0))
    {
        JLOG(j_.trace()) << "Clear GlobalFreeze flag";
        uFlagsOut &= ~lsfGlobalFreeze;
    }

    //
    // Track transaction IDs signed by this account in its root
    //
    if ((uSetFlag == asfAccountTxnID) && !account_->isFieldPresent(sfAccountTxnID))
    {
        JLOG(j_.trace()) << "Set AccountTxnID.";
        account_->makeFieldPresent(sfAccountTxnID);
    }

    if ((uClearFlag == asfAccountTxnID) && account_->isFieldPresent(sfAccountTxnID))
    {
        JLOG(j_.trace()) << "Clear AccountTxnID.";
        account_->makeFieldAbsent(sfAccountTxnID);
    }

    //
    // DepositAuth
    //
    if (uSetFlag == asfDepositAuth)
    {
        JLOG(j_.trace()) << "Set lsfDepositAuth.";
        uFlagsOut |= lsfDepositAuth;
    }
    else if (uClearFlag == asfDepositAuth)
    {
        JLOG(j_.trace()) << "Clear lsfDepositAuth.";
        uFlagsOut &= ~lsfDepositAuth;
    }

    //
    // EmailHash
    //
    if (tx.isFieldPresent(sfEmailHash))
    {
        uint128 const uHash = tx.getFieldH128(sfEmailHash);

        if (!uHash)
        {
            JLOG(j_.trace()) << "unset email hash";
            account_->makeFieldAbsent(sfEmailHash);
        }
        else
        {
            JLOG(j_.trace()) << "set email hash";
            account_->setFieldH128(sfEmailHash, uHash);
        }
    }

    //
    // WalletLocator
    //
    if (tx.isFieldPresent(sfWalletLocator))
    {
        uint256 const uHash = tx.getFieldH256(sfWalletLocator);

        if (!uHash)
        {
            JLOG(j_.trace()) << "unset wallet locator";
            account_->makeFieldAbsent(sfWalletLocator);
        }
        else
        {
            JLOG(j_.trace()) << "set wallet locator";
            account_->setFieldH256(sfWalletLocator, uHash);
        }
    }

    //
    // MessageKey
    //
    if (tx.isFieldPresent(sfMessageKey))
    {
        Blob const messageKey = tx.getFieldVL(sfMessageKey);

        if (messageKey.empty())
        {
            JLOG(j_.debug()) << "clear message key";
            account_->makeFieldAbsent(sfMessageKey);
        }
        else
        {
            JLOG(j_.debug()) << "set message key";
            account_->setFieldVL(sfMessageKey, messageKey);
        }
    }

    //
    // Domain
    //
    if (tx.isFieldPresent(sfDomain))
    {
        Blob const domain = tx.getFieldVL(sfDomain);

        if (domain.empty())
        {
            JLOG(j_.trace()) << "unset domain";
            account_->makeFieldAbsent(sfDomain);
        }
        else
        {
            JLOG(j_.trace()) << "set domain";
            account_->setFieldVL(sfDomain, domain);
        }
    }

    //
    // TransferRate
    //
    if (tx.isFieldPresent(sfTransferRate))
    {
        std::uint32_t const uRate = tx.getFieldU32(sfTransferRate);

        if (uRate == 0 || uRate == QUALITY_ONE)
        {
            JLOG(j_.trace()) << "unset transfer rate";
            account_->makeFieldAbsent(sfTransferRate);
        }
        else
        {
            JLOG(j_.trace()) << "set transfer rate";
            account_->setFieldU32(sfTransferRate, uRate);
        }
    }

    //
    // TickSize
    //
    if (tx.isFieldPresent(sfTickSize))
    {
        auto uTickSize = tx[sfTickSize];
        if ((uTickSize == 0) || (uTickSize == Quality::kMaxTickSize))
        {
            JLOG(j_.trace()) << "unset tick size";
            account_->makeFieldAbsent(sfTickSize);
        }
        else
        {
            JLOG(j_.trace()) << "set tick size";
            account_->setFieldU8(sfTickSize, uTickSize);
        }
    }

    // Configure authorized minting account:
    if (uSetFlag == asfAuthorizedNFTokenMinter)
        account_->setAccountID(sfNFTokenMinter, ctx_.tx[sfNFTokenMinter]);

    if (uClearFlag == asfAuthorizedNFTokenMinter && account_->isFieldPresent(sfNFTokenMinter))
        account_->makeFieldAbsent(sfNFTokenMinter);

    if (uSetFlag == asfDisallowIncomingNFTokenOffer)
    {
        uFlagsOut |= lsfDisallowIncomingNFTokenOffer;
    }
    else if (uClearFlag == asfDisallowIncomingNFTokenOffer)
    {
        uFlagsOut &= ~lsfDisallowIncomingNFTokenOffer;
    }

    if (uSetFlag == asfDisallowIncomingCheck)
    {
        uFlagsOut |= lsfDisallowIncomingCheck;
    }
    else if (uClearFlag == asfDisallowIncomingCheck)
    {
        uFlagsOut &= ~lsfDisallowIncomingCheck;
    }

    if (uSetFlag == asfDisallowIncomingPayChan)
    {
        uFlagsOut |= lsfDisallowIncomingPayChan;
    }
    else if (uClearFlag == asfDisallowIncomingPayChan)
    {
        uFlagsOut &= ~lsfDisallowIncomingPayChan;
    }

    if (uSetFlag == asfDisallowIncomingTrustline)
    {
        uFlagsOut |= lsfDisallowIncomingTrustline;
    }
    else if (uClearFlag == asfDisallowIncomingTrustline)
    {
        uFlagsOut &= ~lsfDisallowIncomingTrustline;
    }

    // Set or clear flags for disallowing escrow
    if (ctx_.view().rules().enabled(featureTokenEscrow))
    {
        if (uSetFlag == asfAllowTrustLineLocking)
        {
            uFlagsOut |= lsfAllowTrustLineLocking;
        }
        else if (uClearFlag == asfAllowTrustLineLocking)
        {
            uFlagsOut &= ~lsfAllowTrustLineLocking;
        }
    }

    // Set flag for clawback
    if (ctx_.view().rules().enabled(featureClawback) && uSetFlag == asfAllowTrustLineClawback)
    {
        JLOG(j_.trace()) << "set allow clawback";
        uFlagsOut |= lsfAllowTrustLineClawback;
    }

    if (uFlagsIn != uFlagsOut)
        account_->setFieldU32(sfFlags, uFlagsOut);

    account_.update();

    return tesSUCCESS;
}

void
AccountSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
AccountSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
