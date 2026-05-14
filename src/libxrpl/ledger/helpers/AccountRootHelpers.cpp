/** @file
 *  Implements AccountRoot ledger-object helper functions.
 *
 *  Covers freeze-state queries, spendable XRP balance, owner-count
 *  bookkeeping, transfer fees, destination-tag enforcement, and the
 *  creation and detection of pseudo-accounts (AMM, Vault, LoanBroker).
 *  Almost every transaction processor depends on at least one function here.
 */
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

/** Clamp a signed owner-count adjustment to the valid `[0, UINT32_MAX]` range.
 *
 *  Uses well-defined unsigned wrap to detect over/underflow: after adding a
 *  positive @p adjustment, `adjusted < current` means overflow; after adding a
 *  negative @p adjustment, `adjusted > current` means underflow.  Both are
 *  clamped and a fatal-level log entry is emitted when @p id is provided.
 *
 *  @param current     Current owner count stored in the SLE.
 *  @param adjustment  Signed delta to apply; may be zero.
 *  @param id          Account being adjusted; when provided, enables error
 *      logging.  Pass `std::nullopt` for speculative (reserve-only) calls.
 *  @param j           Journal used for fatal-level diagnostics.
 *  @return Clamped owner count after applying @p adjustment.
 */
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

/** @copydoc xrpLiquid(ReadView const&, AccountID const&, std::int32_t, beast::Journal)
 *
 *  @note Both the balance read and the owner-count read are routed through
 *      virtual hook methods (`balanceHookIOU`, `ownerCountHook`) on the view.
 *      In normal transaction processing these are identity functions, but when
 *      called from inside a `PaymentSandbox` they return conservative values
 *      that account for credits and owner-count changes made earlier in the
 *      same payment path — without any branching in this function.
 *      Pseudo-accounts bypass the reserve calculation entirely because
 *      protocol-controlled accounts are not subject to reserve requirements.
 */
XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j)
{
    auto const sle = view.read(keylet::account(id));
    if (sle == nullptr)
        return beast::kZERO;

    std::uint32_t const ownerCount =
        confineOwnerCount(view.ownerCountHook(id, sle->getFieldU32(sfOwnerCount)), ownerCountAdj);

    // Pseudo-accounts have no reserve requirement; all other accounts clamp at 0.
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

/** @copydoc adjustOwnerCount(ApplyView&, std::shared_ptr<SLE> const&, std::int32_t, beast::Journal)
 *
 *  @note `view.adjustOwnerCountHook()` is called before writing the new count
 *      to the SLE.  `PaymentSandbox` overrides this hook to record the
 *      high-water-mark owner count so that subsequent `ownerCountHook` reads
 *      within the same payment use the most conservative (highest) count seen,
 *      preventing a transient mid-payment count reduction from bypassing
 *      reserve checks.
 */
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

/** @copydoc pseudoAccountAddress(ReadView const&, uint256 const&)
 *
 *  Derives a candidate `AccountID` by hashing
 *  `(attempt_index, ledger.parentHash, pseudoOwnerKey)` through `sha512Half`
 *  then `ripesha_hasher`, retrying up to `kMAX_ACCOUNT_ATTEMPTS` times until
 *  an address with no existing `AccountRoot` is found.  Incorporating
 *  `parentHash` prevents precomputation of collisions.  Returns `beast::kZERO`
 *  on exhaustion; `createPseudoAccount` propagates that as `tecDUPLICATE`.
 *
 *  @note `kMAX_ACCOUNT_ATTEMPTS` is consensus-critical and must not be changed
 *      without an amendment, as it determines the pseudo-account address space.
 */
AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey)
{
    // Must not be changed without an amendment — alters the address space.
    constexpr std::uint16_t kMAX_ACCOUNT_ATTEMPTS = 256;
    for (std::uint16_t i = 0; i < kMAX_ACCOUNT_ATTEMPTS; ++i)
    {
        RipeshaHasher rsh;
        auto const hash = sha512Half(i, view.header().parentHash, pseudoOwnerKey);
        rsh(hash.data(), hash.size());
        AccountID const ret{static_cast<RipeshaHasher::result_type>(rsh)};
        if (!view.read(keylet::account(ret)))
            return ret;
    }
    return beast::kZERO;
}

/** @copydoc getPseudoAccountFields()
 *
 *  Scans the `ltACCOUNT_ROOT` `SOTemplate` from `LedgerFormats::getInstance()`
 *  at first call and caches the result in a `static` local.  A field qualifies
 *  if `shouldMeta(SField::kSMD_PSEUDO_ACCOUNT)` returns `true`.
 *
 *  @note Fields do NOT need to be amendment-gated here: a non-active amendment
 *      will never set the field, so the list is always accurate.  Do NOT
 *      check pseudo-account invariants here — that is `InvariantCheck`'s job.
 *  @note Every pseudo-account designator `SField` must carry the
 *      `SField::sMD_PseudoAccount` metadata flag (combined with
 *      `SField::sMD_Default`) in its definition, or it will be silently
 *      omitted from this list.
 */
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

/** @copydoc isPseudoAccount(std::shared_ptr<SLE const>, std::set<SField const*> const&)
 *
 *  @note The null-pointer and `ltACCOUNT_ROOT` type guards are intentionally
 *      defensive: callers may already guarantee them, but keeping them here
 *      ensures the semantics of a `true` return value are always unambiguous
 *      at negligible cost.
 */
[[nodiscard]] bool
isPseudoAccount(
    std::shared_ptr<SLE const> sleAcct,
    std::set<SField const*> const& pseudoFieldFilter)
{
    auto const& fields = getPseudoAccountFields();

    return sleAcct && sleAcct->getType() == ltACCOUNT_ROOT &&
        std::count_if(
            fields.begin(), fields.end(), [&sleAcct, &pseudoFieldFilter](SField const* sf) -> bool {
                return sleAcct->isFieldPresent(*sf) &&
                    (pseudoFieldFilter.empty() || pseudoFieldFilter.contains(sf));
            }) > 0;
}

/** @copydoc createPseudoAccount(ApplyView&, uint256 const&, SField const&)
 *
 *  Asserts (in debug builds) that @p ownerField carries the
 *  `SField::sMD_PseudoAccount` flag; misuse is caught at development time, not
 *  at runtime.  The caller is responsible for performing amendment checks
 *  before invoking this function.
 *
 *  The new `AccountRoot` is assembled with:
 *  - Zero balance and — when `featureSingleAssetVault` or
 *    `featureLendingProtocol` is enabled — sequence number `0`, making
 *    pseudo-accounts visually distinguishable and preventing transaction
 *    submission even if `lsfDisableMaster` were somehow bypassed.
 *  - `lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth`: no key-based
 *    transactions; trust-line flows enabled for AMM/vault assets; unsolicited
 *    incoming payments blocked.
 *  - The @p ownerField back-link stores @p pseudoOwnerKey in the new SLE.
 *
 *  @return The new SLE on success, or `tecDUPLICATE` if no collision-free
 *      address could be found within the attempt limit.
 */
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

    auto account = std::make_shared<SLE>(keylet::account(accountId));
    account->setAccountID(sfAccount, accountId);
    account->setFieldAmount(sfBalance, STAmount{});

    std::uint32_t const seqno =                           //
        view.rules().enabled(featureSingleAssetVault) ||  //
            view.rules().enabled(featureLendingProtocol)  //
        ? 0                                               //
        : view.seq();
    account->setFieldU32(sfSequence, seqno);
    account->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
    account->setFieldH256(ownerField, pseudoOwnerKey);

    view.insert(account);

    return account;
}

/** @copydoc checkDestinationAndTag(SLE::const_ref, bool)
 *
 *  @note Destination tags are opaque to the protocol; their content is
 *      meaningful only to the receiving account (e.g. exchange user IDs).
 *      The ledger enforces the presence requirement (`lsfRequireDestTag`) but
 *      never interprets the tag value itself.
 */
[[nodiscard]] TER
checkDestinationAndTag(SLE::const_ref toSle, bool hasDestinationTag)
{
    if (toSle == nullptr)
        return tecNO_DST;

    if (toSle->isFlag(lsfRequireDestTag) && !hasDestinationTag)
        return tecDST_TAG_NEEDED;

    return tesSUCCESS;
}

}  // namespace xrpl
