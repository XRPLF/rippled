#include <xrpl/ledger/helpers/AccountRootHelpers.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <limits>

namespace xrpl {

template <typename ViewT>
bool
AccountRoot<ViewT>::isGlobalFrozen() const
{
    if (!this->exists())
        return false;
    return this->sle_->isFlag(lsfGlobalFreeze);
}

// An owner count cannot be negative. If adjustment would cause a negative
// owner count, clamp the owner count at 0. Similarly for overflow. This
// adjustment allows the ownerCount to be adjusted up or down in multiple steps.
// If id != std::nullopt, then do error reporting.
//
// Returns adjusted owner count.
static std::uint32_t
confineOwnerCount(
    std::uint32_t current,
    std::int32_t adjustment,
    std::optional<AccountID> const& id = std::nullopt,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
{
    std::uint32_t adjusted{current + adjustment};
    if (adjustment > 0)
    {
        // Overflow is well defined on unsigned
        if (adjusted < current)
        {
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count exceeds max!";
            }
            adjusted = std::numeric_limits<std::uint32_t>::max();
        }
    }
    else
    {
        // Underflow is well defined on unsigned
        if (adjusted > current)
        {
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count set below 0!";
            }
            adjusted = 0;
            XRPL_ASSERT(!id, "xrpl::confineOwnerCount : id is not set");
        }
    }
    return adjusted;
}

template <typename ViewT>
XRPAmount
AccountRoot<ViewT>::xrpLiquid(std::int32_t ownerCountAdj, beast::Journal j) const
{
    if (!this->exists())
        return beast::zero;

    // Return balance minus reserve
    std::uint32_t const ownerCount = confineOwnerCount(
        this->readView().ownerCountHook(id_, this->sle_->getFieldU32(sfOwnerCount)), ownerCountAdj);

    // Pseudo-accounts have no reserve requirement
    auto const reserve =
        this->isPseudoAccount() ? XRPAmount{0} : this->readView().fees().accountReserve(ownerCount);

    auto const fullBalance = this->sle_->getFieldAmount(sfBalance);

    auto const balance = this->readView().balanceHook(id_, xrpAccount(), fullBalance);

    STAmount const amount = (balance < reserve) ? STAmount{0} : balance - reserve;

    JLOG(j.trace()) << "accountHolds:" << " account=" << to_string(id_)
                    << " amount=" << amount.getFullText()
                    << " fullBalance=" << fullBalance.getFullText()
                    << " balance=" << balance.getFullText() << " reserve=" << reserve
                    << " ownerCount=" << ownerCount << " ownerCountAdj=" << ownerCountAdj;

    return amount.xrp();
}

template <typename ViewT>
Rate
AccountRoot<ViewT>::transferRate() const
{
    if (this->sle_ && this->sle_->isFieldPresent(sfTransferRate))
        return Rate{this->sle_->getFieldU32(sfTransferRate)};

    return parityRate;
}

template <typename ViewT>
void
AccountRoot<ViewT>::adjustOwnerCount(std::int32_t amount, beast::Journal j)
    requires is_writable
{
    XRPL_ASSERT(this->canModify(), "xrpl::adjustOwnerCount : can modify");
    XRPL_ASSERT(amount, "xrpl::adjustOwnerCount : nonzero amount input");
    std::uint32_t const current{this->sle_->getFieldU32(sfOwnerCount)};
    AccountID const id = (*this->sle_)[sfAccount];
    std::uint32_t const adjusted = confineOwnerCount(current, amount, id, j);
    this->applyView().adjustOwnerCountHook(id_, current, adjusted);
    this->sle_->at(sfOwnerCount) = adjusted;
    this->update();
}

AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey)
{
    // This number must not be changed without an amendment
    constexpr std::uint16_t maxAccountAttempts = 256;
    for (std::uint16_t i = 0; i < maxAccountAttempts; ++i)
    {
        ripesha_hasher rsh;
        auto const hash = sha512Half(i, view.header().parentHash, pseudoOwnerKey);
        rsh(hash.data(), hash.size());
        AccountID const ret{static_cast<ripesha_hasher::result_type>(rsh)};
        if (!view.read(keylet::account(ret)))
            return ret;
    }
    return beast::zero;
}

// Pseudo-account designator fields MUST be maintained by including the
// SField::sMD_PseudoAccount flag in the SField definition. (Don't forget to
// "| SField::sMD_Default"!) The fields do NOT need to be amendment-gated,
// since a non-active amendment will not set any field, by definition.
// Specific properties of a pseudo-account are NOT checked here, that's what
// InvariantCheck is for.
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields()
{
    static std::vector<SField const*> const pseudoFields = []() {
        auto const ar = LedgerFormats::getInstance().findByType(ltACCOUNT_ROOT);
        if (!ar)
        {
            // LCOV_EXCL_START
            LogicError(
                "xrpl::getPseudoAccountFields : unable to find account root "
                "ledger format");
            // LCOV_EXCL_STOP
        }
        auto const& soTemplate = ar->getSOTemplate();

        std::vector<SField const*> pseudoFields;
        for (auto const& field : soTemplate)
        {
            if (field.sField().shouldMeta(SField::sMD_PseudoAccount))
                pseudoFields.emplace_back(&field.sField());
        }
        return pseudoFields;
    }();
    return pseudoFields;
}

template <typename ViewT>
[[nodiscard]] bool
AccountRoot<ViewT>::isPseudoAccount(std::set<SField const*> const& pseudoFieldFilter) const
{
    auto const& fields = getPseudoAccountFields();

    // Intentionally use defensive coding here because it's cheap and makes the
    // semantics of true return value clean.
    return this->sle_ && this->sle_->getType() == ltACCOUNT_ROOT &&
        std::count_if(
            fields.begin(), fields.end(), [this, &pseudoFieldFilter](SField const* sf) -> bool {
                return this->sle_->isFieldPresent(*sf) &&
                    (pseudoFieldFilter.empty() || pseudoFieldFilter.contains(sf));
            }) > 0;
}

[[nodiscard]] bool
isPseudoAccount(
    std::shared_ptr<SLE const> sleAcct,
    std::set<SField const*> const& pseudoFieldFilter)
{
    auto const& fields = getPseudoAccountFields();

    // Intentionally use defensive coding here because it's cheap and makes the
    // semantics of true return value clean.
    return sleAcct && sleAcct->getType() == ltACCOUNT_ROOT &&
        std::count_if(
            fields.begin(), fields.end(), [&sleAcct, &pseudoFieldFilter](SField const* sf) -> bool {
                return sleAcct->isFieldPresent(*sf) &&
                    (pseudoFieldFilter.empty() || pseudoFieldFilter.contains(sf));
            }) > 0;
}

Expected<std::shared_ptr<SLE>, TER>
createPseudoAccount(ApplyView& view, uint256 const& pseudoOwnerKey, SField const& ownerField)
{
    [[maybe_unused]]
    auto const& fields = getPseudoAccountFields();
    XRPL_ASSERT(
        std::count_if(
            fields.begin(),
            fields.end(),
            [&ownerField](SField const* sf) -> bool { return *sf == ownerField; }) == 1,
        "xrpl::createPseudoAccount : valid owner field");

    auto const accountId = pseudoAccountAddress(view, pseudoOwnerKey);
    if (accountId == beast::zero)
        return Unexpected(tecDUPLICATE);

    // Create pseudo-account.
    auto account = std::make_shared<SLE>(keylet::account(accountId));
    account->setAccountID(sfAccount, accountId);
    account->setFieldAmount(sfBalance, STAmount{});

    // Pseudo-accounts can't submit transactions, so set the sequence number
    // to 0 to make them easier to spot and verify, and add an extra level
    // of protection.
    std::uint32_t const seqno =                           //
        view.rules().enabled(featureSingleAssetVault) ||  //
            view.rules().enabled(featureLendingProtocol)  //
        ? 0                                               //
        : view.seq();
    account->setFieldU32(sfSequence, seqno);
    // Ignore reserves requirement, disable the master key, allow default
    // rippling, and enable deposit authorization to prevent payments into
    // pseudo-account.
    account->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
    // Link the pseudo-account with its owner object.
    account->setFieldH256(ownerField, pseudoOwnerKey);

    view.insert(account);

    return account;
}

template <typename ViewT>
[[nodiscard]] TER
AccountRoot<ViewT>::checkDestinationAndTag(bool hasDestinationTag) const
{
    if (this->sle_ == nullptr)
        return tecNO_DST;

    // The tag is basically account-specific information we don't
    // understand, but we can require someone to fill it in.
    if (this->sle_->isFlag(lsfRequireDestTag) && !hasDestinationTag)
        return tecDST_TAG_NEEDED;  // Cannot send without a tag

    return tesSUCCESS;
}

// Explicit template instantiations
template class AccountRoot<ReadView>;
template class AccountRoot<ApplyView>;

}  // namespace xrpl
