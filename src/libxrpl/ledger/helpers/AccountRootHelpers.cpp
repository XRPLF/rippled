#include <xrpl/ledger/helpers/AccountRootHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace xrpl {

bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer)
{
    if (isXRP(issuer))
        return false;
    if (auto const sle = view.read(keylet::account(issuer)))
        return sle->isFlag(lsfGlobalFreeze);
    return false;
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

XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j)
{
    auto const sle = view.read(keylet::account(id));
    if (sle == nullptr)
        return beast::kZERO;

    // Return balance minus reserve
    std::uint32_t const ownerCount =
        confineOwnerCount(view.ownerCountHook(id, sle->getFieldU32(sfOwnerCount)), ownerCountAdj);

    // Pseudo-accounts have no reserve requirement
    auto const reserve =
        isPseudoAccount(sle) ? XRPAmount{0} : view.fees().accountReserve(ownerCount);

    auto const fullBalance = sle->getFieldAmount(sfBalance);

    auto const balance = view.balanceHookIOU(id, xrpAccount(), fullBalance);

    STAmount const amount = (balance < reserve) ? STAmount{0} : balance - reserve;

    JLOG(j.trace()) << "accountHolds:" << " account=" << to_string(id)
                    << " amount=" << amount.getFullText()
                    << " fullBalance=" << fullBalance.getFullText()
                    << " balance=" << balance.getFullText() << " reserve=" << reserve
                    << " ownerCount=" << ownerCount << " ownerCountAdj=" << ownerCountAdj;

    return amount.xrp();
}

Rate
transferRate(ReadView const& view, AccountID const& issuer)
{
    auto const sle = view.read(keylet::account(issuer));

    if (sle && sle->isFieldPresent(sfTransferRate))
        return Rate{sle->getFieldU32(sfTransferRate)};

    return kPARITY_RATE;
}

void
adjustOwnerCount(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    std::int32_t amount,
    beast::Journal j)
{
    if (!sle)
        return;
    XRPL_ASSERT(amount, "xrpl::adjustOwnerCount : nonzero amount input");
    std::uint32_t const current{sle->getFieldU32(sfOwnerCount)};
    AccountID const id = (*sle)[sfAccount];
    std::uint32_t const adjusted = confineOwnerCount(current, amount, id, j);
    view.adjustOwnerCountHook(id, current, adjusted);
    sle->at(sfOwnerCount) = adjusted;
    view.update(sle);
}

AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey)
{
    // This number must not be changed without an amendment
    constexpr std::uint16_t kMAX_ACCOUNT_ATTEMPTS = 256;
    for (std::uint16_t i = 0; i < kMAX_ACCOUNT_ATTEMPTS; ++i)
    {
        RipeshaHasher rsh;
        auto const hash = sha512Half(i, view.header().parentHash, pseudoOwnerKey);
        rsh(hash.data(), hash.size());
        AccountID const ret = AccountID::fromRaw(static_cast<RipeshaHasher::result_type>(rsh));
        if (!view.read(keylet::account(ret)))
            return ret;
    }
    return beast::kZERO;
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
    static std::vector<SField const*> const kPSEUDO_FIELDS = []() {
        auto const ar = LedgerFormats::getInstance().findByType(ltACCOUNT_ROOT);
        if (!ar)
        {
            // LCOV_EXCL_START
            Throw<std::logic_error>(
                "xrpl::getPseudoAccountFields : unable to find account root "
                "ledger format");
            // LCOV_EXCL_STOP
        }
        auto const& soTemplate = ar->getSOTemplate();

        std::vector<SField const*> pseudoFields;
        for (auto const& field : soTemplate)
        {
            if (field.sField().shouldMeta(SField::kSMD_PSEUDO_ACCOUNT))
                pseudoFields.emplace_back(&field.sField());
        }
        return pseudoFields;
    }();
    return kPSEUDO_FIELDS;
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
    if (accountId == beast::kZERO)
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

[[nodiscard]] TER
checkDestinationAndTag(SLE::const_ref toSle, bool hasDestinationTag)
{
    if (toSle == nullptr)
        return tecNO_DST;

    // The tag is basically account-specific information we don't
    // understand, but we can require someone to fill it in.
    if (toSle->isFlag(lsfRequireDestTag) && !hasDestinationTag)
        return tecDST_TAG_NEEDED;  // Cannot send without a tag

    return tesSUCCESS;
}

}  // namespace xrpl
