#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

#include <type_traits>
#include <variant>

namespace ripple {

namespace detail {

template <
    class V,
    class N,
    class = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<N>, SLE> &&
        std::is_base_of_v<ReadView, V>>>
bool
internalDirNext(
    V& view,
    uint256 const& root,
    std::shared_ptr<N>& page,
    unsigned int& index,
    uint256& entry)
{
    auto const& svIndexes = page->getFieldV256(sfIndexes);
    XRPL_ASSERT(
        index <= svIndexes.size(),
        "ripple::detail::internalDirNext : index inside range");

    if (index >= svIndexes.size())
    {
        auto const next = page->getFieldU64(sfIndexNext);

        if (!next)
        {
            entry.zero();
            return false;
        }

        if constexpr (std::is_const_v<N>)
            page = view.read(keylet::page(root, next));
        else
            page = view.peek(keylet::page(root, next));

        XRPL_ASSERT(page, "ripple::detail::internalDirNext : non-null root");

        if (!page)
            return false;

        index = 0;

        return internalDirNext(view, root, page, index, entry);
    }

    entry = svIndexes[index++];
    return true;
}

template <
    class V,
    class N,
    class = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<N>, SLE> &&
        std::is_base_of_v<ReadView, V>>>
bool
internalDirFirst(
    V& view,
    uint256 const& root,
    std::shared_ptr<N>& page,
    unsigned int& index,
    uint256& entry)
{
    if constexpr (std::is_const_v<N>)
        page = view.read(keylet::page(root));
    else
        page = view.peek(keylet::page(root));

    if (!page)
        return false;

    index = 0;

    return internalDirNext(view, root, page, index, entry);
}

}  // namespace detail

bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

bool
cdirFirst(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
cdirNext(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

bool
hasExpired(ReadView const& view, std::optional<std::uint32_t> const& exp)
{
    using d = NetClock::duration;
    using tp = NetClock::time_point;

    return exp && (view.parentCloseTime() >= tp{d{*exp}});
}

bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer)
{
    if (isXRP(issuer))
        return false;
    if (auto const sle = view.read(keylet::account(issuer)))
        return sle->isFlag(lsfGlobalFreeze);
    return false;
}

bool
isGlobalFrozen(ReadView const& view, MPTIssue const& mptIssue)
{
    if (auto const sle = view.read(keylet::mptIssuance(mptIssue.getMptID())))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

bool
isGlobalFrozen(ReadView const& view, Asset const& asset)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                return isGlobalFrozen(view, issue.getIssuer());
            else
                return isGlobalFrozen(view, issue);
        },
        asset.value());
}

bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
        return false;
    if (issuer != account)
    {
        // Check if the issuer froze the line
        auto const sle = view.read(keylet::line(account, issuer, currency));
        if (sle &&
            sle->isFlag((issuer > account) ? lsfHighFreeze : lsfLowFreeze))
            return true;
    }
    return false;
}

bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue)
{
    if (auto const sle =
            view.read(keylet::mptoken(mptIssue.getMptID(), account)))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

// Can the specified account spend the specified currency issued by
// the specified issuer or does the freeze flag prohibit it?
bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
        return false;
    auto sle = view.read(keylet::account(issuer));
    if (sle && sle->isFlag(lsfGlobalFreeze))
        return true;
    if (issuer != account)
    {
        // Check if the issuer froze the line
        sle = view.read(keylet::line(account, issuer, currency));
        if (sle &&
            sle->isFlag((issuer > account) ? lsfHighFreeze : lsfLowFreeze))
            return true;
    }
    return false;
}

bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    int depth)
{
    return isGlobalFrozen(view, mptIssue) ||
        isIndividualFrozen(view, account, mptIssue) ||
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

bool
isVaultPseudoAccountFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptShare,
    int depth)
{
    if (!view.rules().enabled(featureSingleAssetVault))
        return false;

    if (depth >= maxAssetCheckDepth)
        return true;  // LCOV_EXCL_LINE

    auto const mptIssuance =
        view.read(keylet::mptIssuance(mptShare.getMptID()));
    if (mptIssuance == nullptr)
        return false;  // zero MPToken won't block deletion of MPTokenIssuance

    auto const issuer = mptIssuance->getAccountID(sfIssuer);
    auto const mptIssuer = view.read(keylet::account(issuer));
    if (mptIssuer == nullptr)
    {
        // LCOV_EXCL_START
        UNREACHABLE("ripple::isVaultPseudoAccountFrozen : null MPToken issuer");
        return false;
        // LCOV_EXCL_STOP
    }

    if (!mptIssuer->isFieldPresent(sfVaultID))
        return false;  // not a Vault pseudo-account, common case

    auto const vault =
        view.read(keylet::vault(mptIssuer->getFieldH256(sfVaultID)));
    if (vault == nullptr)
    {  // LCOV_EXCL_START
        UNREACHABLE("ripple::isVaultPseudoAccountFrozen : null vault");
        return false;
        // LCOV_EXCL_STOP
    }

    return isAnyFrozen(view, {issuer, account}, vault->at(sfAsset), depth + 1);
}

bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
    {
        return false;
    }

    if (issuer == account)
    {
        return false;
    }

    auto const sle = view.read(keylet::line(account, issuer, currency));
    if (!sle)
    {
        return false;
    }

    return sle->isFlag(lsfHighDeepFreeze) || sle->isFlag(lsfLowDeepFreeze);
}

bool
isLPTokenFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& asset,
    Issue const& asset2)
{
    return isFrozen(view, account, asset.currency, asset.account) ||
        isFrozen(view, account, asset2.currency, asset2.account);
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j)
{
    STAmount amount;
    if (isXRP(currency))
    {
        return {xrpLiquid(view, account, 0, j)};
    }

    // IOU: Return balance on trust line modulo freeze
    auto const sle = view.read(keylet::line(account, issuer, currency));
    auto const allowBalance = [&]() {
        if (!sle)
        {
            return false;
        }

        if (zeroIfFrozen == fhZERO_IF_FROZEN)
        {
            if (isFrozen(view, account, currency, issuer) ||
                isDeepFrozen(view, account, currency, issuer))
            {
                return false;
            }

            // when fixFrozenLPTokenTransfer is enabled, if currency is lptoken,
            // we need to check if the associated assets have been frozen
            if (view.rules().enabled(fixFrozenLPTokenTransfer))
            {
                auto const sleIssuer = view.read(keylet::account(issuer));
                if (!sleIssuer)
                {
                    return false;  // LCOV_EXCL_LINE
                }
                else if (sleIssuer->isFieldPresent(sfAMMID))
                {
                    auto const sleAmm =
                        view.read(keylet::amm((*sleIssuer)[sfAMMID]));

                    if (!sleAmm ||
                        isLPTokenFrozen(
                            view,
                            account,
                            (*sleAmm)[sfAsset].get<Issue>(),
                            (*sleAmm)[sfAsset2].get<Issue>()))
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }();

    if (allowBalance)
    {
        amount = sle->getFieldAmount(sfBalance);
        if (account > issuer)
        {
            // Put balance in account terms.
            amount.negate();
        }
        amount.setIssuer(issuer);
    }
    else
    {
        amount.clear(Issue{currency, issuer});
    }

    JLOG(j.trace()) << "accountHolds:"
                    << " account=" << to_string(account)
                    << " amount=" << amount.getFullText();

    return view.balanceHook(account, issuer, amount);
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j)
{
    return accountHolds(
        view, account, issue.currency, issue.account, zeroIfFrozen, j);
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j)
{
    STAmount amount;

    auto const sleMpt =
        view.read(keylet::mptoken(mptIssue.getMptID(), account));

    if (!sleMpt)
        amount.clear(mptIssue);
    else if (
        zeroIfFrozen == fhZERO_IF_FROZEN && isFrozen(view, account, mptIssue))
        amount.clear(mptIssue);
    else
    {
        amount = STAmount{mptIssue, sleMpt->getFieldU64(sfMPTAmount)};

        // Only if auth check is needed, as it needs to do an additional read
        // operation. Note featureSingleAssetVault will affect error codes.
        if (zeroIfUnauthorized == ahZERO_IF_UNAUTHORIZED &&
            view.rules().enabled(featureSingleAssetVault))
        {
            if (auto const err =
                    requireAuth(view, mptIssue, account, AuthType::StrongAuth);
                !isTesSuccess(err))
                amount.clear(mptIssue);
        }
        else if (zeroIfUnauthorized == ahZERO_IF_UNAUTHORIZED)
        {
            auto const sleIssuance =
                view.read(keylet::mptIssuance(mptIssue.getMptID()));

            // if auth is enabled on the issuance and mpt is not authorized,
            // clear amount
            if (sleIssuance && sleIssuance->isFlag(lsfMPTRequireAuth) &&
                !sleMpt->isFlag(lsfMPTAuthorized))
                amount.clear(mptIssue);
        }
    }

    return amount;
}

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j)
{
    return std::visit(
        [&](auto const& value) {
            if constexpr (std::is_same_v<
                              std::remove_cvref_t<decltype(value)>,
                              Issue>)
            {
                return accountHolds(view, account, value, zeroIfFrozen, j);
            }
            return accountHolds(
                view, account, value, zeroIfFrozen, zeroIfUnauthorized, j);
        },
        asset.value());
}

STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    if (!saDefault.native() && saDefault.getIssuer() == id)
        return saDefault;

    return accountHolds(
        view,
        id,
        saDefault.getCurrency(),
        saDefault.getIssuer(),
        freezeHandling,
        j);
}

// Prevent ownerCount from wrapping under error conditions.
//
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
                JLOG(j.fatal())
                    << "Account " << *id << " owner count exceeds max!";
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
                JLOG(j.fatal())
                    << "Account " << *id << " owner count set below 0!";
            }
            adjusted = 0;
            XRPL_ASSERT(!id, "ripple::confineOwnerCount : id is not set");
        }
    }
    return adjusted;
}

XRPAmount
xrpLiquid(
    ReadView const& view,
    AccountID const& id,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    auto const sle = view.read(keylet::account(id));
    if (sle == nullptr)
        return beast::zero;

    // Return balance minus reserve
    std::uint32_t const ownerCount = confineOwnerCount(
        view.ownerCountHook(id, sle->getFieldU32(sfOwnerCount)), ownerCountAdj);

    // Pseudo-accounts have no reserve requirement
    auto const reserve = isPseudoAccount(sle)
        ? XRPAmount{0}
        : view.fees().accountReserve(ownerCount);

    auto const fullBalance = sle->getFieldAmount(sfBalance);

    auto const balance = view.balanceHook(id, xrpAccount(), fullBalance);

    STAmount const amount =
        (balance < reserve) ? STAmount{0} : balance - reserve;

    JLOG(j.trace()) << "accountHolds:"
                    << " account=" << to_string(id)
                    << " amount=" << amount.getFullText()
                    << " fullBalance=" << fullBalance.getFullText()
                    << " balance=" << balance.getFullText()
                    << " reserve=" << reserve << " ownerCount=" << ownerCount
                    << " ownerCountAdj=" << ownerCountAdj;

    return amount.xrp();
}

void
forEachItem(
    ReadView const& view,
    Keylet const& root,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f)
{
    XRPL_ASSERT(
        root.type == ltDIR_NODE, "ripple::forEachItem : valid root type");

    if (root.type != ltDIR_NODE)
        return;

    auto pos = root;

    while (true)
    {
        auto sle = view.read(pos);
        if (!sle)
            return;
        for (auto const& key : sle->getFieldV256(sfIndexes))
            f(view.read(keylet::child(key)));
        auto const next = sle->getFieldU64(sfIndexNext);
        if (!next)
            return;
        pos = keylet::page(root, next);
    }
}

bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f)
{
    XRPL_ASSERT(
        root.type == ltDIR_NODE, "ripple::forEachItemAfter : valid root type");

    if (root.type != ltDIR_NODE)
        return false;

    auto currentIndex = root;

    // If startAfter is not zero try jumping to that page using the hint
    if (after.isNonZero())
    {
        auto const hintIndex = keylet::page(root, hint);

        if (auto hintDir = view.read(hintIndex))
        {
            for (auto const& key : hintDir->getFieldV256(sfIndexes))
            {
                if (key == after)
                {
                    // We found the hint, we can start here
                    currentIndex = hintIndex;
                    break;
                }
            }
        }

        bool found = false;
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return found;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
            {
                if (!found)
                {
                    if (key == after)
                        found = true;
                }
                else if (f(view.read(keylet::child(key))) && limit-- <= 1)
                {
                    return found;
                }
            }

            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return found;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
    else
    {
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return true;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
                if (f(view.read(keylet::child(key))) && limit-- <= 1)
                    return true;
            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return true;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
}

Rate
transferRate(ReadView const& view, AccountID const& issuer)
{
    auto const sle = view.read(keylet::account(issuer));

    if (sle && sle->isFieldPresent(sfTransferRate))
        return Rate{sle->getFieldU32(sfTransferRate)};

    return parityRate;
}

Rate
transferRate(ReadView const& view, MPTID const& issuanceID)
{
    // fee is 0-50,000 (0-50%), rate is 1,000,000,000-2,000,000,000
    // For example, if transfer fee is 50% then 10,000 * 50,000 = 500,000
    // which represents 50% of 1,000,000,000
    if (auto const sle = view.read(keylet::mptIssuance(issuanceID));
        sle && sle->isFieldPresent(sfTransferFee))
        return Rate{1'000'000'000u + 10'000 * sle->getFieldU16(sfTransferFee)};

    return parityRate;
}

Rate
transferRate(ReadView const& view, STAmount const& amount)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                return transferRate(view, issue.getIssuer());
            else
                return transferRate(view, issue.getMptID());
        },
        amount.asset().value());
}

bool
areCompatible(
    ReadView const& validLedger,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason)
{
    bool ret = true;

    if (validLedger.info().seq < testLedger.info().seq)
    {
        // valid -> ... -> test
        auto hash = hashOfSeq(
            testLedger,
            validLedger.info().seq,
            beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != validLedger.info().hash))
        {
            JLOG(s) << reason << " incompatible with valid ledger";

            JLOG(s) << "Hash(VSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (validLedger.info().seq > testLedger.info().seq)
    {
        // test -> ... -> valid
        auto hash = hashOfSeq(
            validLedger,
            testLedger.info().seq,
            beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != testLedger.info().hash))
        {
            JLOG(s) << reason << " incompatible preceding ledger";

            JLOG(s) << "Hash(NSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (
        (validLedger.info().seq == testLedger.info().seq) &&
        (validLedger.info().hash != testLedger.info().hash))
    {
        // Same sequence number, different hash
        JLOG(s) << reason << " incompatible ledger";

        ret = false;
    }

    if (!ret)
    {
        JLOG(s) << "Val: " << validLedger.info().seq << " "
                << to_string(validLedger.info().hash);

        JLOG(s) << "New: " << testLedger.info().seq << " "
                << to_string(testLedger.info().hash);
    }

    return ret;
}

bool
areCompatible(
    uint256 const& validHash,
    LedgerIndex validIndex,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason)
{
    bool ret = true;

    if (testLedger.info().seq > validIndex)
    {
        // Ledger we are testing follows last valid ledger
        auto hash = hashOfSeq(
            testLedger,
            validIndex,
            beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != validHash))
        {
            JLOG(s) << reason << " incompatible following ledger";
            JLOG(s) << "Hash(VSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (
        (validIndex == testLedger.info().seq) &&
        (testLedger.info().hash != validHash))
    {
        JLOG(s) << reason << " incompatible ledger";

        ret = false;
    }

    if (!ret)
    {
        JLOG(s) << "Val: " << validIndex << " " << to_string(validHash);

        JLOG(s) << "New: " << testLedger.info().seq << " "
                << to_string(testLedger.info().hash);
    }

    return ret;
}

bool
dirIsEmpty(ReadView const& view, Keylet const& k)
{
    auto const sleNode = view.read(k);
    if (!sleNode)
        return true;
    if (!sleNode->getFieldV256(sfIndexes).empty())
        return false;
    // The first page of a directory may legitimately be empty even if there
    // are other pages (the first page is the anchor page) so check to see if
    // there is another page. If there is, the directory isn't empty.
    return sleNode->getFieldU64(sfIndexNext) == 0;
}

std::set<uint256>
getEnabledAmendments(ReadView const& view)
{
    std::set<uint256> amendments;

    if (auto const sle = view.read(keylet::amendments()))
    {
        if (sle->isFieldPresent(sfAmendments))
        {
            auto const& v = sle->getFieldV256(sfAmendments);
            amendments.insert(v.begin(), v.end());
        }
    }

    return amendments;
}

majorityAmendments_t
getMajorityAmendments(ReadView const& view)
{
    majorityAmendments_t ret;

    if (auto const sle = view.read(keylet::amendments()))
    {
        if (sle->isFieldPresent(sfMajorities))
        {
            using tp = NetClock::time_point;
            using d = tp::duration;

            auto const majorities = sle->getFieldArray(sfMajorities);

            for (auto const& m : majorities)
                ret[m.getFieldH256(sfAmendment)] =
                    tp(d(m.getFieldU32(sfCloseTime)));
        }
    }

    return ret;
}

std::optional<uint256>
hashOfSeq(ReadView const& ledger, LedgerIndex seq, beast::Journal journal)
{
    // Easy cases...
    if (seq > ledger.seq())
    {
        JLOG(journal.warn())
            << "Can't get seq " << seq << " from " << ledger.seq() << " future";
        return std::nullopt;
    }
    if (seq == ledger.seq())
        return ledger.info().hash;
    if (seq == (ledger.seq() - 1))
        return ledger.info().parentHash;

    if (int diff = ledger.seq() - seq; diff <= 256)
    {
        // Within 256...
        auto const hashIndex = ledger.read(keylet::skip());
        if (hashIndex)
        {
            XRPL_ASSERT(
                hashIndex->getFieldU32(sfLastLedgerSequence) ==
                    (ledger.seq() - 1),
                "ripple::hashOfSeq : matching ledger sequence");
            STVector256 vec = hashIndex->getFieldV256(sfHashes);
            if (vec.size() >= diff)
                return vec[vec.size() - diff];
            JLOG(journal.warn())
                << "Ledger " << ledger.seq() << " missing hash for " << seq
                << " (" << vec.size() << "," << diff << ")";
        }
        else
        {
            JLOG(journal.warn())
                << "Ledger " << ledger.seq() << ":" << ledger.info().hash
                << " missing normal list";
        }
    }

    if ((seq & 0xff) != 0)
    {
        JLOG(journal.debug())
            << "Can't get seq " << seq << " from " << ledger.seq() << " past";
        return std::nullopt;
    }

    // in skiplist
    auto const hashIndex = ledger.read(keylet::skip(seq));
    if (hashIndex)
    {
        auto const lastSeq = hashIndex->getFieldU32(sfLastLedgerSequence);
        XRPL_ASSERT(lastSeq >= seq, "ripple::hashOfSeq : minimum last ledger");
        XRPL_ASSERT(
            (lastSeq & 0xff) == 0, "ripple::hashOfSeq : valid last ledger");
        auto const diff = (lastSeq - seq) >> 8;
        STVector256 vec = hashIndex->getFieldV256(sfHashes);
        if (vec.size() > diff)
            return vec[vec.size() - diff - 1];
    }
    JLOG(journal.warn()) << "Can't get seq " << seq << " from " << ledger.seq()
                         << " error";
    return std::nullopt;
}

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

void
adjustOwnerCount(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    std::int32_t amount,
    beast::Journal j)
{
    if (!sle)
        return;
    XRPL_ASSERT(amount, "ripple::adjustOwnerCount : nonzero amount input");
    std::uint32_t const current{sle->getFieldU32(sfOwnerCount)};
    AccountID const id = (*sle)[sfAccount];
    std::uint32_t const adjusted = confineOwnerCount(current, amount, id, j);
    view.adjustOwnerCountHook(id, current, adjusted);
    sle->at(sfOwnerCount) = adjusted;
    view.update(sle);
}

std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account)
{
    return [&account](std::shared_ptr<SLE> const& sle) {
        (*sle)[sfOwner] = account;
    };
}

TER
dirLink(ApplyView& view, AccountID const& owner, std::shared_ptr<SLE>& object)
{
    auto const page = view.dirInsert(
        keylet::ownerDir(owner), object->key(), describeOwnerDir(owner));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    object->setFieldU64(sfOwnerNode, *page);
    return tesSUCCESS;
}

AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey)
{
    // This number must not be changed without an amendment
    constexpr std::uint16_t maxAccountAttempts = 256;
    for (std::uint16_t i = 0; i < maxAccountAttempts; ++i)
    {
        ripesha_hasher rsh;
        auto const hash = sha512Half(i, view.info().parentHash, pseudoOwnerKey);
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
                "ripple::isPseudoAccount : unable to find account root ledger "
                "format");
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

[[nodiscard]] bool
isPseudoAccount(std::shared_ptr<SLE const> sleAcct)
{
    auto const& fields = getPseudoAccountFields();

    // Intentionally use defensive coding here because it's cheap and makes the
    // semantics of true return value clean.
    return sleAcct && sleAcct->getType() == ltACCOUNT_ROOT &&
        std::count_if(
            fields.begin(), fields.end(), [&sleAcct](SField const* sf) -> bool {
                return sleAcct->isFieldPresent(*sf);
            }) > 0;
}

Expected<std::shared_ptr<SLE>, TER>
createPseudoAccount(
    ApplyView& view,
    uint256 const& pseudoOwnerKey,
    SField const& ownerField)
{
    [[maybe_unused]] auto const& fields = getPseudoAccountFields();
    XRPL_ASSERT(
        std::count_if(
            fields.begin(),
            fields.end(),
            [&ownerField](SField const* sf) -> bool {
                return *sf == ownerField;
            }) == 1,
        "ripple::createPseudoAccount : valid owner field");

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
    std::uint32_t const seqno =                        //
        view.rules().enabled(featureSingleAssetVault)  //
        ? 0                                            //
        : view.seq();
    account->setFieldU32(sfSequence, seqno);
    // Ignore reserves requirement, disable the master key, allow default
    // rippling, and enable deposit authorization to prevent payments into
    // pseudo-account.
    account->setFieldU32(
        sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
    // Link the pseudo-account with its owner object.
    account->setFieldH256(ownerField, pseudoOwnerKey);

    view.insert(account);

    return account;
}

[[nodiscard]] TER
canAddHolding(ReadView const& view, Issue const& issue)
{
    if (issue.native())
        return tesSUCCESS;  // No special checks for XRP

    auto const issuer = view.read(keylet::account(issue.getIssuer()));
    if (!issuer)
        return terNO_ACCOUNT;
    else if (!issuer->isFlag(lsfDefaultRipple))
        return terNO_RIPPLE;

    return tesSUCCESS;
}

[[nodiscard]] TER
canAddHolding(ReadView const& view, MPTIssue const& mptIssue)
{
    auto mptID = mptIssue.getMptID();
    auto issuance = view.read(keylet::mptIssuance(mptID));
    if (!issuance)
        return tecOBJECT_NOT_FOUND;
    if (!issuance->isFlag(lsfMPTCanTransfer))
        return tecNO_AUTH;

    return tesSUCCESS;
}

[[nodiscard]] TER
canAddHolding(ReadView const& view, Asset const& asset)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return canAddHolding(view, issue);
        },
        asset.value());
}

[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Issue const& issue,
    beast::Journal journal)
{
    // Every account can hold XRP.
    if (issue.native())
        return tesSUCCESS;

    auto const& issuerId = issue.getIssuer();
    auto const& currency = issue.currency;
    if (isGlobalFrozen(view, issuerId))
        return tecFROZEN;  // LCOV_EXCL_LINE

    auto const& srcId = issuerId;
    auto const& dstId = accountID;
    auto const high = srcId > dstId;
    auto const index = keylet::line(srcId, dstId, currency);
    auto const sleSrc = view.peek(keylet::account(srcId));
    auto const sleDst = view.peek(keylet::account(dstId));
    if (!sleDst || !sleSrc)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (!sleSrc->isFlag(lsfDefaultRipple))
        return tecINTERNAL;  // LCOV_EXCL_LINE
    // If the line already exists, don't create it again.
    if (view.read(index))
        return tecDUPLICATE;

    // Can the account cover the trust line reserve ?
    std::uint32_t const ownerCount = sleDst->at(sfOwnerCount);
    if (priorBalance < view.fees().accountReserve(ownerCount + 1))
        return tecNO_LINE_INSUF_RESERVE;

    return trustCreate(
        view,
        high,
        srcId,
        dstId,
        index.key,
        sleDst,
        /*auth=*/false,
        /*noRipple=*/true,
        /*freeze=*/false,
        /*deepFreeze*/ false,
        /*balance=*/STAmount{Issue{currency, noAccount()}},
        /*limit=*/STAmount{Issue{currency, dstId}},
        /*qualityIn=*/0,
        /*qualityOut=*/0,
        journal);
}

[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
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

    return authorizeMPToken(view, priorBalance, mptID, accountID, journal);
}

[[nodiscard]] TER
authorizeMPToken(
    ApplyView& view,
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
        if (flags & tfMPTUnauthorize)
        {
            auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);
            auto const sleMpt = view.peek(mptokenKey);
            if (!sleMpt || (*sleMpt)[sfMPTAmount] != 0)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (!view.dirRemove(
                    keylet::ownerDir(account),
                    (*sleMpt)[sfOwnerNode],
                    sleMpt->key(),
                    false))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            adjustOwnerCount(view, sleAcct, -1, journal);

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
        std::uint32_t const uOwnerCount = sleAcct->getFieldU32(sfOwnerCount);
        XRPAmount const reserveCreate(
            (uOwnerCount < 2) ? XRPAmount(beast::zero)
                              : view.fees().accountReserve(uOwnerCount + 1));

        if (priorBalance < reserveCreate)
            return tecINSUFFICIENT_RESERVE;

        auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);
        auto mptoken = std::make_shared<SLE>(mptokenKey);
        if (auto ter = dirLink(view, account, mptoken))
            return ter;  // LCOV_EXCL_LINE

        (*mptoken)[sfAccount] = account;
        (*mptoken)[sfMPTokenIssuanceID] = mptIssuanceID;
        (*mptoken)[sfFlags] = 0;
        view.insert(mptoken);

        // Update owner count.
        adjustOwnerCount(view, sleAcct, 1, journal);

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
    if (flags & tfMPTUnauthorize)
        flagsOut &= ~lsfMPTAuthorized;
    // Issuer wants to authorize a holder, set lsfMPTAuthorized on their
    // MPToken
    else
        flagsOut |= lsfMPTAuthorized;

    if (flagsIn != flagsOut)
        sleMpt->setFieldU32(sfFlags, flagsOut);

    view.update(sleMpt);
    return tesSUCCESS;
}

TER
trustCreate(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,      // --> ripple state entry
    SLE::ref sleAccount,        // --> the account being set.
    bool const bAuth,           // --> authorize account.
    bool const bNoRipple,       // --> others cannot ripple through
    bool const bFreeze,         // --> funds cannot leave
    bool bDeepFreeze,           // --> can neither receive nor send funds
    STAmount const& saBalance,  // --> balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // --> limit for account being set.
                                // Issuer should be the account being set.
    std::uint32_t uQualityIn,
    std::uint32_t uQualityOut,
    beast::Journal j)
{
    JLOG(j.trace()) << "trustCreate: " << to_string(uSrcAccountID) << ", "
                    << to_string(uDstAccountID) << ", "
                    << saBalance.getFullText();

    auto const& uLowAccountID = !bSrcHigh ? uSrcAccountID : uDstAccountID;
    auto const& uHighAccountID = bSrcHigh ? uSrcAccountID : uDstAccountID;

    auto const sleRippleState = std::make_shared<SLE>(ltRIPPLE_STATE, uIndex);
    view.insert(sleRippleState);

    auto lowNode = view.dirInsert(
        keylet::ownerDir(uLowAccountID),
        sleRippleState->key(),
        describeOwnerDir(uLowAccountID));

    if (!lowNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto highNode = view.dirInsert(
        keylet::ownerDir(uHighAccountID),
        sleRippleState->key(),
        describeOwnerDir(uHighAccountID));

    if (!highNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    bool const bSetDst = saLimit.getIssuer() == uDstAccountID;
    bool const bSetHigh = bSrcHigh ^ bSetDst;

    XRPL_ASSERT(sleAccount, "ripple::trustCreate : non-null SLE");
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        sleAccount->getAccountID(sfAccount) ==
            (bSetHigh ? uHighAccountID : uLowAccountID),
        "ripple::trustCreate : matching account ID");
    auto const slePeer =
        view.peek(keylet::account(bSetHigh ? uLowAccountID : uHighAccountID));
    if (!slePeer)
        return tecNO_TARGET;

    // Remember deletion hints.
    sleRippleState->setFieldU64(sfLowNode, *lowNode);
    sleRippleState->setFieldU64(sfHighNode, *highNode);

    sleRippleState->setFieldAmount(
        bSetHigh ? sfHighLimit : sfLowLimit, saLimit);
    sleRippleState->setFieldAmount(
        bSetHigh ? sfLowLimit : sfHighLimit,
        STAmount(Issue{
            saBalance.getCurrency(), bSetDst ? uSrcAccountID : uDstAccountID}));

    if (uQualityIn)
        sleRippleState->setFieldU32(
            bSetHigh ? sfHighQualityIn : sfLowQualityIn, uQualityIn);

    if (uQualityOut)
        sleRippleState->setFieldU32(
            bSetHigh ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

    std::uint32_t uFlags = bSetHigh ? lsfHighReserve : lsfLowReserve;

    if (bAuth)
    {
        uFlags |= (bSetHigh ? lsfHighAuth : lsfLowAuth);
    }
    if (bNoRipple)
    {
        uFlags |= (bSetHigh ? lsfHighNoRipple : lsfLowNoRipple);
    }
    if (bFreeze)
    {
        uFlags |= (bSetHigh ? lsfHighFreeze : lsfLowFreeze);
    }
    if (bDeepFreeze)
    {
        uFlags |= (bSetHigh ? lsfHighDeepFreeze : lsfLowDeepFreeze);
    }

    if ((slePeer->getFlags() & lsfDefaultRipple) == 0)
    {
        // The other side's default is no rippling
        uFlags |= (bSetHigh ? lsfLowNoRipple : lsfHighNoRipple);
    }

    sleRippleState->setFieldU32(sfFlags, uFlags);
    adjustOwnerCount(view, sleAccount, 1, j);

    // ONLY: Create ripple balance.
    sleRippleState->setFieldAmount(
        sfBalance, bSetHigh ? -saBalance : saBalance);

    view.creditHook(
        uSrcAccountID, uDstAccountID, saBalance, saBalance.zeroed());

    return tesSUCCESS;
}

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Issue const& issue,
    beast::Journal journal)
{
    if (issue.native())
    {
        auto const sle = view.read(keylet::account(accountID));
        if (!sle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const balance = sle->getFieldAmount(sfBalance);
        if (balance.xrp() != 0)
            return tecHAS_OBLIGATIONS;

        return tesSUCCESS;
    }

    // `asset` is an IOU.
    auto const line = view.peek(keylet::line(accountID, issue));
    if (!line)
        return tecOBJECT_NOT_FOUND;
    if (line->at(sfBalance)->iou() != beast::zero)
        return tecHAS_OBLIGATIONS;

    // Adjust the owner count(s)
    if (line->isFlag(lsfLowReserve))
    {
        // Clear reserve for low account.
        auto sleLowAccount =
            view.peek(keylet::account(line->at(sfLowLimit)->getIssuer()));
        if (!sleLowAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        adjustOwnerCount(view, sleLowAccount, -1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
        line->clearFlag(lsfLowReserve);
    }

    if (line->isFlag(lsfHighReserve))
    {
        // Clear reserve for high account.
        auto sleHighAccount =
            view.peek(keylet::account(line->at(sfHighLimit)->getIssuer()));
        if (!sleHighAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        adjustOwnerCount(view, sleHighAccount, -1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
        line->clearFlag(lsfHighReserve);
    }

    return trustDelete(
        view,
        line,
        line->at(sfLowLimit)->getIssuer(),
        line->at(sfHighLimit)->getIssuer(),
        journal);
}

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    MPTIssue const& mptIssue,
    beast::Journal journal)
{
    auto const& mptID = mptIssue.getMptID();
    auto const mptoken = view.peek(keylet::mptoken(mptID, accountID));
    if (!mptoken)
        return tecOBJECT_NOT_FOUND;
    if (mptoken->at(sfMPTAmount) != 0)
        return tecHAS_OBLIGATIONS;

    return authorizeMPToken(
        view,
        {},  // priorBalance
        mptID,
        accountID,
        journal,
        tfMPTUnauthorize  // flags
    );
}

TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j)
{
    // Detect legacy dirs.
    std::uint64_t uLowNode = sleRippleState->getFieldU64(sfLowNode);
    std::uint64_t uHighNode = sleRippleState->getFieldU64(sfHighNode);

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: low";

    if (!view.dirRemove(
            keylet::ownerDir(uLowAccountID),
            uLowNode,
            sleRippleState->key(),
            false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: high";

    if (!view.dirRemove(
            keylet::ownerDir(uHighAccountID),
            uHighNode,
            sleRippleState->key(),
            false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: state";
    view.erase(sleRippleState);

    return tesSUCCESS;
}

TER
offerDelete(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j)
{
    if (!sle)
        return tesSUCCESS;
    auto offerIndex = sle->key();
    auto owner = sle->getAccountID(sfAccount);

    // Detect legacy directories.
    uint256 uDirectory = sle->getFieldH256(sfBookDirectory);

    if (!view.dirRemove(
            keylet::ownerDir(owner),
            sle->getFieldU64(sfOwnerNode),
            offerIndex,
            false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    if (!view.dirRemove(
            keylet::page(uDirectory),
            sle->getFieldU64(sfBookNode),
            offerIndex,
            false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    if (sle->isFieldPresent(sfAdditionalBooks))
    {
        XRPL_ASSERT(
            sle->isFlag(lsfHybrid) && sle->isFieldPresent(sfDomainID),
            "ripple::offerDelete : should be a hybrid domain offer");

        auto const& additionalBookDirs = sle->getFieldArray(sfAdditionalBooks);

        for (auto const& bookDir : additionalBookDirs)
        {
            auto const& dirIndex = bookDir.getFieldH256(sfBookDirectory);
            auto const& dirNode = bookDir.getFieldU64(sfBookNode);

            if (!view.dirRemove(
                    keylet::page(dirIndex), dirNode, offerIndex, false))
            {
                return tefBAD_LEDGER;  // LCOV_EXCL_LINE
            }
        }
    }

    adjustOwnerCount(view, view.peek(keylet::account(owner)), -1, j);

    view.erase(sle);

    return tesSUCCESS;
}

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line if needed.
// --> bCheckIssuer : normally require issuer to be involved.
static TER
rippleCreditIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j)
{
    AccountID const& issuer = saAmount.getIssuer();
    Currency const& currency = saAmount.getCurrency();

    // Make sure issuer is involved.
    XRPL_ASSERT(
        !bCheckIssuer || uSenderID == issuer || uReceiverID == issuer,
        "ripple::rippleCreditIOU : matching issuer or don't care");
    (void)issuer;

    // Disallow sending to self.
    XRPL_ASSERT(
        uSenderID != uReceiverID,
        "ripple::rippleCreditIOU : sender is not receiver");

    bool const bSenderHigh = uSenderID > uReceiverID;
    auto const index = keylet::line(uSenderID, uReceiverID, currency);

    XRPL_ASSERT(
        !isXRP(uSenderID) && uSenderID != noAccount(),
        "ripple::rippleCreditIOU : sender is not XRP");
    XRPL_ASSERT(
        !isXRP(uReceiverID) && uReceiverID != noAccount(),
        "ripple::rippleCreditIOU : receiver is not XRP");

    // If the line exists, modify it accordingly.
    if (auto const sleRippleState = view.peek(index))
    {
        STAmount saBalance = sleRippleState->getFieldAmount(sfBalance);

        if (bSenderHigh)
            saBalance.negate();  // Put balance in sender terms.

        view.creditHook(uSenderID, uReceiverID, saAmount, saBalance);

        STAmount const saBefore = saBalance;

        saBalance -= saAmount;

        JLOG(j.trace()) << "rippleCreditIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID)
                        << " : before=" << saBefore.getFullText()
                        << " amount=" << saAmount.getFullText()
                        << " after=" << saBalance.getFullText();

        std::uint32_t const uFlags(sleRippleState->getFieldU32(sfFlags));
        bool bDelete = false;

        // FIXME This NEEDS to be cleaned up and simplified. It's impossible
        //       for anyone to understand.
        if (saBefore > beast::zero
            // Sender balance was positive.
            && saBalance <= beast::zero
            // Sender is zero or negative.
            && (uFlags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
            // Sender reserve is set.
            &&
            static_cast<bool>(
                uFlags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
                static_cast<bool>(
                    view.read(keylet::account(uSenderID))->getFlags() &
                    lsfDefaultRipple) &&
            !(uFlags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) &&
            !sleRippleState->getFieldAmount(
                !bSenderHigh ? sfLowLimit : sfHighLimit)
            // Sender trust limit is 0.
            && !sleRippleState->getFieldU32(
                   !bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
            // Sender quality in is 0.
            && !sleRippleState->getFieldU32(
                   !bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
        // Sender quality out is 0.
        {
            // Clear the reserve of the sender, possibly delete the line!
            adjustOwnerCount(
                view, view.peek(keylet::account(uSenderID)), -1, j);

            // Clear reserve flag.
            sleRippleState->setFieldU32(
                sfFlags,
                uFlags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

            // Balance is zero, receiver reserve is clear.
            bDelete = !saBalance  // Balance is zero.
                && !(uFlags & (bSenderHigh ? lsfLowReserve : lsfHighReserve));
            // Receiver reserve is clear.
        }

        if (bSenderHigh)
            saBalance.negate();

        // Want to reflect balance to zero even if we are deleting line.
        sleRippleState->setFieldAmount(sfBalance, saBalance);
        // ONLY: Adjust ripple balance.

        if (bDelete)
        {
            return trustDelete(
                view,
                sleRippleState,
                bSenderHigh ? uReceiverID : uSenderID,
                !bSenderHigh ? uReceiverID : uSenderID,
                j);
        }

        view.update(sleRippleState);
        return tesSUCCESS;
    }

    STAmount const saReceiverLimit(Issue{currency, uReceiverID});
    STAmount saBalance{saAmount};

    saBalance.setIssuer(noAccount());

    JLOG(j.debug()) << "rippleCreditIOU: "
                       "create line: "
                    << to_string(uSenderID) << " -> " << to_string(uReceiverID)
                    << " : " << saAmount.getFullText();

    auto const sleAccount = view.peek(keylet::account(uReceiverID));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    bool const noRipple = (sleAccount->getFlags() & lsfDefaultRipple) == 0;

    return trustCreate(
        view,
        bSenderHigh,
        uSenderID,
        uReceiverID,
        index.key,
        sleAccount,
        false,
        noRipple,
        false,
        false,
        saBalance,
        saReceiverLimit,
        0,
        0,
        j);
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer to deliver to receiver.
// <-- saActual: Amount actually cost.  Sender pays fees.
static TER
rippleSendIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    auto const issuer = saAmount.getIssuer();

    XRPL_ASSERT(
        !isXRP(uSenderID) && !isXRP(uReceiverID),
        "ripple::rippleSendIOU : neither sender nor receiver is XRP");
    XRPL_ASSERT(
        uSenderID != uReceiverID,
        "ripple::rippleSendIOU : sender is not receiver");

    if (uSenderID == issuer || uReceiverID == issuer || issuer == noAccount())
    {
        // Direct send: redeeming IOUs and/or sending own IOUs.
        auto const ter =
            rippleCreditIOU(view, uSenderID, uReceiverID, saAmount, false, j);
        if (view.rules().enabled(featureDeletableAccounts) && ter != tesSUCCESS)
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Sending 3rd party IOUs: transit.

    // Calculate the amount to transfer accounting
    // for any transfer fees if the fee is not waived:
    saActual = (waiveFee == WaiveTransferFee::Yes)
        ? saAmount
        : multiply(saAmount, transferRate(view, issuer));

    JLOG(j.debug()) << "rippleSendIOU> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID)
                    << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    TER terResult =
        rippleCreditIOU(view, issuer, uReceiverID, saAmount, true, j);

    if (tesSUCCESS == terResult)
        terResult = rippleCreditIOU(view, uSenderID, issuer, saActual, true, j);

    return terResult;
}

static TER
accountSendIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    if (view.rules().enabled(fixAMMv1_1))
    {
        if (saAmount < beast::zero || saAmount.holds<MPTIssue>())
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    else
    {
        // LCOV_EXCL_START
        XRPL_ASSERT(
            saAmount >= beast::zero && !saAmount.holds<MPTIssue>(),
            "ripple::accountSendIOU : minimum amount and not MPT");
        // LCOV_EXCL_STOP
    }

    /* If we aren't sending anything or if the sender is the same as the
     * receiver then we don't need to do anything.
     */
    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    if (!saAmount.native())
    {
        STAmount saActual;

        JLOG(j.trace()) << "accountSendIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : "
                        << saAmount.getFullText();

        return rippleSendIOU(
            view, uSenderID, uReceiverID, saAmount, saActual, j, waiveFee);
    }

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup is used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */
    TER terResult(tesSUCCESS);

    SLE::pointer sender = uSenderID != beast::zero
        ? view.peek(keylet::account(uSenderID))
        : SLE::pointer();
    SLE::pointer receiver = uReceiverID != beast::zero
        ? view.peek(keylet::account(uReceiverID))
        : SLE::pointer();

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");
        std::string receiver_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendIOU> " << to_string(uSenderID) << " ("
               << sender_bal << ") -> " << to_string(uReceiverID) << " ("
               << receiver_bal << ") : " << saAmount.getFullText();
    }

    if (sender)
    {
        if (sender->getFieldAmount(sfBalance) < saAmount)
        {
            // VFALCO Its laborious to have to mutate the
            //        TER based on params everywhere
            // LCOV_EXCL_START
            terResult = view.open() ? TER{telFAILED_PROCESSING}
                                    : TER{tecFAILED_PROCESSING};
            // LCOV_EXCL_STOP
        }
        else
        {
            auto const sndBal = sender->getFieldAmount(sfBalance);
            view.creditHook(uSenderID, xrpAccount(), saAmount, sndBal);

            // Decrement XRP balance.
            sender->setFieldAmount(sfBalance, sndBal - saAmount);
            view.update(sender);
        }
    }

    if (tesSUCCESS == terResult && receiver)
    {
        // Increment XRP balance.
        auto const rcvBal = receiver->getFieldAmount(sfBalance);
        receiver->setFieldAmount(sfBalance, rcvBal + saAmount);
        view.creditHook(xrpAccount(), uReceiverID, saAmount, -rcvBal);

        view.update(receiver);
    }

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");
        std::string receiver_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendIOU< " << to_string(uSenderID) << " ("
               << sender_bal << ") -> " << to_string(uReceiverID) << " ("
               << receiver_bal << ") : " << saAmount.getFullText();
    }

    return terResult;
}

static TER
rippleCreditMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j)
{
    // Do not check MPT authorization here - it must have been checked earlier
    auto const mptID = keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID());
    auto const issuer = saAmount.getIssuer();
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;
    if (uSenderID == issuer)
    {
        (*sleIssuance)[sfOutstandingAmount] += saAmount.mpt().value();
        view.update(sleIssuance);
    }
    else
    {
        auto const mptokenID = keylet::mptoken(mptID.key, uSenderID);
        if (auto sle = view.peek(mptokenID))
        {
            auto const amt = sle->getFieldU64(sfMPTAmount);
            auto const pay = saAmount.mpt().value();
            if (amt < pay)
                return tecINSUFFICIENT_FUNDS;
            (*sle)[sfMPTAmount] = amt - pay;
            view.update(sle);
        }
        else
            return tecNO_AUTH;
    }

    if (uReceiverID == issuer)
    {
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        auto const redeem = saAmount.mpt().value();
        if (outstanding >= redeem)
        {
            sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - redeem);
            view.update(sleIssuance);
        }
        else
            return tecINTERNAL;  // LCOV_EXCL_LINE
    }
    else
    {
        auto const mptokenID = keylet::mptoken(mptID.key, uReceiverID);
        if (auto sle = view.peek(mptokenID))
        {
            (*sle)[sfMPTAmount] += saAmount.mpt().value();
            view.update(sle);
        }
        else
            return tecNO_AUTH;
    }

    return tesSUCCESS;
}

static TER
rippleSendMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT(
        uSenderID != uReceiverID,
        "ripple::rippleSendMPT : sender is not receiver");

    // Safe to get MPT since rippleSendMPT is only called by accountSendMPT
    auto const issuer = saAmount.getIssuer();

    auto const sle =
        view.read(keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    if (uSenderID == issuer || uReceiverID == issuer)
    {
        // if sender is issuer, check that the new OutstandingAmount will not
        // exceed MaximumAmount
        if (uSenderID == issuer)
        {
            auto const sendAmount = saAmount.mpt().value();
            auto const maximumAmount =
                sle->at(~sfMaximumAmount).value_or(maxMPTokenAmount);
            if (sendAmount > maximumAmount ||
                sle->getFieldU64(sfOutstandingAmount) >
                    maximumAmount - sendAmount)
                return tecPATH_DRY;
        }

        // Direct send: redeeming MPTs and/or sending own MPTs.
        auto const ter =
            rippleCreditMPT(view, uSenderID, uReceiverID, saAmount, j);
        if (ter != tesSUCCESS)
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Sending 3rd party MPTs: transit.
    saActual = (waiveFee == WaiveTransferFee::Yes)
        ? saAmount
        : multiply(
              saAmount,
              transferRate(view, saAmount.get<MPTIssue>().getMptID()));

    JLOG(j.debug()) << "rippleSendMPT> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID)
                    << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    if (auto const terResult =
            rippleCreditMPT(view, issuer, uReceiverID, saAmount, j);
        terResult != tesSUCCESS)
        return terResult;

    return rippleCreditMPT(view, uSenderID, issuer, saActual, j);
}

static TER
accountSendMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT(
        saAmount >= beast::zero && saAmount.holds<MPTIssue>(),
        "ripple::accountSendMPT : minimum amount and MPT");

    /* If we aren't sending anything or if the sender is the same as the
     * receiver then we don't need to do anything.
     */
    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    STAmount saActual{saAmount.asset()};

    return rippleSendMPT(
        view, uSenderID, uReceiverID, saAmount, saActual, j, waiveFee);
}

TER
accountSend(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                return accountSendIOU(
                    view, uSenderID, uReceiverID, saAmount, j, waiveFee);
            else
                return accountSendMPT(
                    view, uSenderID, uReceiverID, saAmount, j, waiveFee);
        },
        saAmount.asset().value());
}

static bool
updateTrustLine(
    ApplyView& view,
    SLE::pointer state,
    bool bSenderHigh,
    AccountID const& sender,
    STAmount const& before,
    STAmount const& after,
    beast::Journal j)
{
    if (!state)
        return false;
    std::uint32_t const flags(state->getFieldU32(sfFlags));

    auto sle = view.peek(keylet::account(sender));
    if (!sle)
        return false;

    // YYY Could skip this if rippling in reverse.
    if (before > beast::zero
        // Sender balance was positive.
        && after <= beast::zero
        // Sender is zero or negative.
        && (flags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
        // Sender reserve is set.
        && static_cast<bool>(
               flags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
            static_cast<bool>(sle->getFlags() & lsfDefaultRipple) &&
        !(flags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) &&
        !state->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit)
        // Sender trust limit is 0.
        && !state->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
        // Sender quality in is 0.
        &&
        !state->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
    // Sender quality out is 0.
    {
        // VFALCO Where is the line being deleted?
        // Clear the reserve of the sender, possibly delete the line!
        adjustOwnerCount(view, sle, -1, j);

        // Clear reserve flag.
        state->setFieldU32(
            sfFlags, flags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

        // Balance is zero, receiver reserve is clear.
        if (!after  // Balance is zero.
            && !(flags & (bSenderHigh ? lsfLowReserve : lsfHighReserve)))
            return true;
    }
    return false;
}

TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j)
{
    XRPL_ASSERT(
        !isXRP(account) && !isXRP(issue.account),
        "ripple::issueIOU : neither account nor issuer is XRP");

    // Consistency check
    XRPL_ASSERT(issue == amount.issue(), "ripple::issueIOU : matching issue");

    // Can't send to self!
    XRPL_ASSERT(
        issue.account != account, "ripple::issueIOU : not issuer account");

    JLOG(j.trace()) << "issueIOU: " << to_string(account) << ": "
                    << amount.getFullText();

    bool bSenderHigh = issue.account > account;

    auto const index = keylet::line(issue.account, account, issue.currency);

    if (auto state = view.peek(index))
    {
        STAmount final_balance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            final_balance.negate();  // Put balance in sender terms.

        STAmount const start_balance = final_balance;

        final_balance -= amount;

        auto const must_delete = updateTrustLine(
            view,
            state,
            bSenderHigh,
            issue.account,
            start_balance,
            final_balance,
            j);

        view.creditHook(issue.account, account, amount, start_balance);

        if (bSenderHigh)
            final_balance.negate();

        // Adjust the balance on the trust line if necessary. We do this even if
        // we are going to delete the line to reflect the correct balance at the
        // time of deletion.
        state->setFieldAmount(sfBalance, final_balance);
        if (must_delete)
            return trustDelete(
                view,
                state,
                bSenderHigh ? account : issue.account,
                bSenderHigh ? issue.account : account,
                j);

        view.update(state);

        return tesSUCCESS;
    }

    // NIKB TODO: The limit uses the receiver's account as the issuer and
    // this is unnecessarily inefficient as copying which could be avoided
    // is now required. Consider available options.
    STAmount const limit(Issue{issue.currency, account});
    STAmount final_balance = amount;

    final_balance.setIssuer(noAccount());

    auto const receiverAccount = view.peek(keylet::account(account));
    if (!receiverAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    bool noRipple = (receiverAccount->getFlags() & lsfDefaultRipple) == 0;

    return trustCreate(
        view,
        bSenderHigh,
        issue.account,
        account,
        index.key,
        receiverAccount,
        false,
        noRipple,
        false,
        false,
        final_balance,
        limit,
        0,
        0,
        j);
}

TER
redeemIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j)
{
    XRPL_ASSERT(
        !isXRP(account) && !isXRP(issue.account),
        "ripple::redeemIOU : neither account nor issuer is XRP");

    // Consistency check
    XRPL_ASSERT(issue == amount.issue(), "ripple::redeemIOU : matching issue");

    // Can't send to self!
    XRPL_ASSERT(
        issue.account != account, "ripple::redeemIOU : not issuer account");

    JLOG(j.trace()) << "redeemIOU: " << to_string(account) << ": "
                    << amount.getFullText();

    bool bSenderHigh = account > issue.account;

    if (auto state =
            view.peek(keylet::line(account, issue.account, issue.currency)))
    {
        STAmount final_balance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            final_balance.negate();  // Put balance in sender terms.

        STAmount const start_balance = final_balance;

        final_balance -= amount;

        auto const must_delete = updateTrustLine(
            view, state, bSenderHigh, account, start_balance, final_balance, j);

        view.creditHook(account, issue.account, amount, start_balance);

        if (bSenderHigh)
            final_balance.negate();

        // Adjust the balance on the trust line if necessary. We do this even if
        // we are going to delete the line to reflect the correct balance at the
        // time of deletion.
        state->setFieldAmount(sfBalance, final_balance);

        if (must_delete)
        {
            return trustDelete(
                view,
                state,
                bSenderHigh ? issue.account : account,
                bSenderHigh ? account : issue.account,
                j);
        }

        view.update(state);
        return tesSUCCESS;
    }

    // In order to hold an IOU, a trust line *MUST* exist to track the
    // balance. If it doesn't, then something is very wrong. Don't try
    // to continue.
    // LCOV_EXCL_START
    JLOG(j.fatal()) << "redeemIOU: " << to_string(account)
                    << " attempts to redeem " << amount.getFullText()
                    << " but no trust line exists!";

    return tefINTERNAL;
    // LCOV_EXCL_STOP
}

TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j)
{
    XRPL_ASSERT(
        from != beast::zero, "ripple::transferXRP : nonzero from account");
    XRPL_ASSERT(to != beast::zero, "ripple::transferXRP : nonzero to account");
    XRPL_ASSERT(from != to, "ripple::transferXRP : sender is not receiver");
    XRPL_ASSERT(amount.native(), "ripple::transferXRP : amount is XRP");

    SLE::pointer const sender = view.peek(keylet::account(from));
    SLE::pointer const receiver = view.peek(keylet::account(to));
    if (!sender || !receiver)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    JLOG(j.trace()) << "transferXRP: " << to_string(from) << " -> "
                    << to_string(to) << ") : " << amount.getFullText();

    if (sender->getFieldAmount(sfBalance) < amount)
    {
        // VFALCO Its unfortunate we have to keep
        //        mutating these TER everywhere
        // FIXME: this logic should be moved to callers maybe?
        // LCOV_EXCL_START
        return view.open() ? TER{telFAILED_PROCESSING}
                           : TER{tecFAILED_PROCESSING};
        // LCOV_EXCL_STOP
    }

    // Decrement XRP balance.
    sender->setFieldAmount(
        sfBalance, sender->getFieldAmount(sfBalance) - amount);
    view.update(sender);

    receiver->setFieldAmount(
        sfBalance, receiver->getFieldAmount(sfBalance) + amount);
    view.update(receiver);

    return tesSUCCESS;
}

TER
requireAuth(
    ReadView const& view,
    Issue const& issue,
    AccountID const& account,
    AuthType authType)
{
    if (isXRP(issue) || issue.account == account)
        return tesSUCCESS;

    auto const trustLine =
        view.read(keylet::line(account, issue.account, issue.currency));
    // If account has no line, and this is a strong check, fail
    if (!trustLine && authType == AuthType::StrongAuth)
        return tecNO_LINE;

    // If this is a weak or legacy check, or if the account has a line, fail if
    // auth is required and not set on the line
    if (auto const issuerAccount = view.read(keylet::account(issue.account));
        issuerAccount && (*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        if (trustLine)
            return ((*trustLine)[sfFlags] &
                    ((account > issue.account) ? lsfLowAuth : lsfHighAuth))
                ? tesSUCCESS
                : TER{tecNO_AUTH};
        return TER{tecNO_LINE};
    }

    return tesSUCCESS;
}

TER
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

    if (view.rules().enabled(featureSingleAssetVault))
    {
        if (depth >= maxAssetCheckDepth)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // requireAuth is recursive if the issuer is a vault pseudo-account
        auto const sleIssuer = view.read(keylet::account(mptIssuer));
        if (!sleIssuer)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        if (sleIssuer->isFieldPresent(sfVaultID))
        {
            auto const sleVault =
                view.read(keylet::vault(sleIssuer->getFieldH256(sfVaultID)));
            if (!sleVault)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            auto const asset = sleVault->at(sfAsset);
            if (auto const err = std::visit(
                    [&]<ValidIssueType TIss>(TIss const& issue) {
                        if constexpr (std::is_same_v<TIss, Issue>)
                            return requireAuth(view, issue, account, authType);
                        else
                            return requireAuth(
                                view, issue, account, authType, depth + 1);
                    },
                    asset.value());
                !isTesSuccess(err))
                return err;
        }
    }

    auto const mptokenID = keylet::mptoken(mptID.key, account);
    auto const sleToken = view.read(mptokenID);

    // if account has no MPToken, fail
    if (!sleToken &&
        (authType == AuthType::StrongAuth || authType == AuthType::Legacy))
        return tecNO_AUTH;

    // Note, this check is not amendment-gated because DomainID will be always
    // empty **unless** writing to it has been enabled by an amendment
    auto const maybeDomainID = sleIssuance->at(~sfDomainID);
    if (maybeDomainID)
    {
        XRPL_ASSERT(
            sleIssuance->getFieldU32(sfFlags) & lsfMPTRequireAuth,
            "ripple::requireAuth : issuance requires authorization");
        // ter = tefINTERNAL | tecOBJECT_NOT_FOUND | tecNO_AUTH | tecEXPIRED
        if (auto const ter =
                credentials::validDomain(view, *maybeDomainID, account);
            isTesSuccess(ter))
            return ter;  // Note: sleToken might be null
        else if (!sleToken)
            return ter;
        // We ignore error from validDomain if we found sleToken, as it could
        // belong to someone who is explicitly authorized e.g. a vault owner.
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
        "ripple::enforceMPTokenAuthorization : authorization required");

    if (account == sleIssuance->at(sfIssuer))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const keylet = keylet::mptoken(mptIssuanceID, account);
    auto const sleToken = view.read(keylet);  //  NOTE: might be null
    auto const maybeDomainID = sleIssuance->at(~sfDomainID);
    bool expired = false;
    bool const authorizedByDomain = [&]() -> bool {
        // NOTE: defensive here, shuld be checked in preclaim
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
    else if (!authorizedByDomain && maybeDomainID.has_value())
    {
        // Found an MPToken but the account is not authorized and we expect
        // it to have been authorized by the domain. This could be because the
        // credentials used to create the MPToken have expired or been deleted.
        return expired ? tecEXPIRED : tecNO_AUTH;
    }
    else if (!authorizedByDomain)
    {
        // We found an MPToken, but sfDomainID is not set, so this is a classic
        // MPToken which requires authorization by the token issuer.
        XRPL_ASSERT(
            sleToken != nullptr && !maybeDomainID.has_value(),
            "ripple::enforceMPTokenAuthorization : found MPToken");
        if (sleToken->isFlag(lsfMPTAuthorized))
            return tesSUCCESS;

        return tecNO_AUTH;
    }
    else if (authorizedByDomain && sleToken != nullptr)
    {
        // Found an MPToken, authorized by the domain. Ignore authorization flag
        // lsfMPTAuthorized because it is meaningless. Return tesSUCCESS
        XRPL_ASSERT(
            maybeDomainID.has_value(),
            "ripple::enforceMPTokenAuthorization : found MPToken for domain");
        return tesSUCCESS;
    }
    else if (authorizedByDomain)
    {
        // Could not find MPToken but there should be one because we are
        // authorized by domain. Proceed to create it, then return tesSUCCESS
        XRPL_ASSERT(
            maybeDomainID.has_value() && sleToken == nullptr,
            "ripple::enforceMPTokenAuthorization : new MPToken for domain");
        if (auto const err = authorizeMPToken(
                view,
                priorBalance,   // priorBalance
                mptIssuanceID,  // mptIssuanceID
                account,        // account
                j);
            !isTesSuccess(err))
            return err;

        return tesSUCCESS;
    }

    // LCOV_EXCL_START
    UNREACHABLE(
        "ripple::enforceMPTokenAuthorization : condition list is incomplete");
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

    if (!(sleIssuance->getFieldU32(sfFlags) & lsfMPTCanTransfer))
    {
        if (from != (*sleIssuance)[sfIssuer] && to != (*sleIssuance)[sfIssuer])
            return TER{tecNO_AUTH};
    }
    return tesSUCCESS;
}

TER
cleanupOnAccountDelete(
    ApplyView& view,
    Keylet const& ownerDirKeylet,
    EntryDeleter const& deleter,
    beast::Journal j,
    std::optional<uint16_t> maxNodesToDelete)
{
    // Delete all the entries in the account directory.
    std::shared_ptr<SLE> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};
    std::uint32_t deleted = 0;

    if (view.exists(ownerDirKeylet) &&
        dirFirst(view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry))
    {
        do
        {
            if (maxNodesToDelete && ++deleted > *maxNodesToDelete)
                return tecINCOMPLETE;

            // Choose the right way to delete each directory node.
            auto sleItem = view.peek(keylet::child(dirEntry));
            if (!sleItem)
            {
                // Directory node has an invalid index.  Bail out.
                // LCOV_EXCL_START
                JLOG(j.fatal())
                    << "DeleteAccount: Directory node in ledger " << view.seq()
                    << " has index to object that is missing: "
                    << to_string(dirEntry);
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }

            LedgerEntryType const nodeType{safe_cast<LedgerEntryType>(
                sleItem->getFieldU16(sfLedgerEntryType))};

            // Deleter handles the details of specific account-owned object
            // deletion
            auto const [ter, skipEntry] = deleter(nodeType, dirEntry, sleItem);
            if (ter != tesSUCCESS)
                return ter;

            // dirFirst() and dirNext() are like iterators with exposed
            // internal state.  We'll take advantage of that exposed state
            // to solve a common C++ problem: iterator invalidation while
            // deleting elements from a container.
            //
            // We have just deleted one directory entry, which means our
            // "iterator state" is invalid.
            //
            //  1. During the process of getting an entry from the
            //     directory uDirEntry was incremented from 'it' to 'it'+1.
            //
            //  2. We then deleted the entry at index 'it', which means the
            //     entry that was at 'it'+1 has now moved to 'it'.
            //
            //  3. So we verify that uDirEntry is indeed 'it'+1.  Then we jam it
            //     back to 'it' to "un-invalidate" the iterator.
            XRPL_ASSERT(
                uDirEntry >= 1,
                "ripple::cleanupOnAccountDelete : minimum dir entries");
            if (uDirEntry == 0)
            {
                // LCOV_EXCL_START
                JLOG(j.error())
                    << "DeleteAccount iterator re-validation failed.";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
            if (skipEntry == SkipEntry::No)
                uDirEntry--;

        } while (
            dirNext(view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry));
    }

    return tesSUCCESS;
}

TER
deleteAMMTrustLine(
    ApplyView& view,
    std::shared_ptr<SLE> sleState,
    std::optional<AccountID> const& ammAccountID,
    beast::Journal j)
{
    if (!sleState || sleState->getType() != ltRIPPLE_STATE)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const& [low, high] = std::minmax(
        sleState->getFieldAmount(sfLowLimit).getIssuer(),
        sleState->getFieldAmount(sfHighLimit).getIssuer());
    auto sleLow = view.peek(keylet::account(low));
    auto sleHigh = view.peek(keylet::account(high));
    if (!sleLow || !sleHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    bool const ammLow = sleLow->isFieldPresent(sfAMMID);
    bool const ammHigh = sleHigh->isFieldPresent(sfAMMID);

    // can't both be AMM
    if (ammLow && ammHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // at least one must be
    if (!ammLow && !ammHigh)
        return terNO_AMM;

    // one must be the target amm
    if (ammAccountID && (low != *ammAccountID && high != *ammAccountID))
        return terNO_AMM;

    if (auto const ter = trustDelete(view, sleState, low, high, j);
        ter != tesSUCCESS)
    {
        JLOG(j.error())
            << "deleteAMMTrustLine: failed to delete the trustline.";
        return ter;
    }

    auto const uFlags = !ammLow ? lsfLowReserve : lsfHighReserve;
    if (!(sleState->getFlags() & uFlags))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    adjustOwnerCount(view, !ammLow ? sleLow : sleHigh, -1, j);

    return tesSUCCESS;
}

TER
rippleCredit(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return rippleCreditIOU(
                    view, uSenderID, uReceiverID, saAmount, bCheckIssuer, j);
            }
            else
            {
                XRPL_ASSERT(
                    !bCheckIssuer,
                    "ripple::rippleCredit : not checking issuer");
                return rippleCreditMPT(
                    view, uSenderID, uReceiverID, saAmount, j);
            }
        },
        saAmount.asset().value());
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets)
{
    XRPL_ASSERT(
        !assets.negative(),
        "ripple::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "ripple::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return STAmount{
            shares.asset(),
            Number(assets.mantissa(), assets.exponent() + vault->at(sfScale))
                .truncate()};

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    shares = (shareTotal * (assets / assetTotal)).truncate();
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares)
{
    XRPL_ASSERT(
        !shares.negative(),
        "ripple::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "ripple::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return STAmount{
            assets.asset(),
            shares.mantissa(),
            shares.exponent() - vault->at(sfScale),
            false};

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = assetTotal * (shares / shareTotal);
    return assets;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate)
{
    XRPL_ASSERT(
        !assets.negative(),
        "ripple::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "ripple::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number assetTotal = vault->at(sfAssetsTotal);
    assetTotal -= vault->at(sfLossUnrealized);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return shares;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    Number result = shareTotal * (assets / assetTotal);
    if (truncate == TruncateShares::yes)
        result = result.truncate();
    shares = result;
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares)
{
    XRPL_ASSERT(
        !shares.negative(),
        "ripple::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "ripple::sharesToAssetsWithdraw : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number assetTotal = vault->at(sfAssetsTotal);
    assetTotal -= vault->at(sfLossUnrealized);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return assets;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = assetTotal * (shares / shareTotal);
    return assets;
}

TER
rippleLockEscrowMPT(
    ApplyView& view,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal j)
{
    auto const mptIssue = amount.get<MPTIssue>();
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleLockEscrowMPT: MPT issuance not found for "
                        << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    if (amount.getIssuer() == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error())
            << "rippleLockEscrowMPT: sender is the issuer, cannot lock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    // 1. Decrease the MPT Holder MPTAmount
    // 2. Increase the MPT Holder EscrowedAmount
    {
        auto const mptokenID = keylet::mptoken(mptID.key, sender);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleLockEscrowMPT: MPToken not found for " << sender;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto const amt = sle->getFieldU64(sfMPTAmount);
        auto const pay = amount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, amt), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleLockEscrowMPT: insufficient MPTAmount for "
                << to_string(sender) << ": " << amt << " < " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        (*sle)[sfMPTAmount] = amt - pay;

        // Overflow check for addition
        uint64_t const locked = (*sle)[~sfLockedAmount].value_or(0);

        if (!canAdd(STAmount(mptIssue, locked), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleLockEscrowMPT: overflow on locked amount for "
                << to_string(sender) << ": " << locked << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (sle->isFieldPresent(sfLockedAmount))
            (*sle)[sfLockedAmount] += pay;
        else
            sle->setFieldU64(sfLockedAmount, pay);

        view.update(sle);
    }

    // 1. Increase the Issuance EscrowedAmount
    // 2. DO NOT change the Issuance OutstandingAmount
    {
        uint64_t const issuanceEscrowed =
            (*sleIssuance)[~sfLockedAmount].value_or(0);
        auto const pay = amount.mpt().value();

        // Overflow check for addition
        if (!canAdd(
                STAmount(mptIssue, issuanceEscrowed), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "rippleLockEscrowMPT: overflow on issuance "
                               "locked amount for "
                            << mptIssue.getMptID() << ": " << issuanceEscrowed
                            << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (sleIssuance->isFieldPresent(sfLockedAmount))
            (*sleIssuance)[sfLockedAmount] += pay;
        else
            sleIssuance->setFieldU64(sfLockedAmount, pay);

        view.update(sleIssuance);
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
        XRPL_ASSERT(
            netAmount == grossAmount,
            "ripple::rippleUnlockEscrowMPT : netAmount == grossAmount");

    auto const& issuer = netAmount.getIssuer();
    auto const& mptIssue = netAmount.get<MPTIssue>();
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleUnlockEscrowMPT: MPT issuance not found for "
                        << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    // Decrease the Issuance EscrowedAmount
    {
        if (!sleIssuance->isFieldPresent(sfLockedAmount))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: no locked amount in issuance for "
                << mptIssue.getMptID();
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const locked = sleIssuance->getFieldU64(sfLockedAmount);
        auto const redeem = grossAmount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(
                STAmount(mptIssue, locked), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: insufficient locked amount for "
                << mptIssue.getMptID() << ": " << locked << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const newLocked = locked - redeem;
        if (newLocked == 0)
            sleIssuance->makeFieldAbsent(sfLockedAmount);
        else
            sleIssuance->setFieldU64(sfLockedAmount, newLocked);
        view.update(sleIssuance);
    }

    if (issuer != receiver)
    {
        // Increase the MPT Holder MPTAmount
        auto const mptokenID = keylet::mptoken(mptID.key, receiver);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: MPToken not found for " << receiver;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto current = sle->getFieldU64(sfMPTAmount);
        auto delta = netAmount.mpt().value();

        // Overflow check for addition
        if (!canAdd(STAmount(mptIssue, current), STAmount(mptIssue, delta)))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: overflow on MPTAmount for "
                << to_string(receiver) << ": " << current << " + " << delta;
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
        if (!canSubtract(
                STAmount(mptIssue, outstanding), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: insufficient outstanding amount for "
                << mptIssue.getMptID() << ": " << outstanding << " < "
                << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - redeem);
        view.update(sleIssuance);
    }

    if (issuer == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "rippleUnlockEscrowMPT: sender is the issuer, "
                           "cannot unlock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP
    else
    {
        // Decrease the MPT Holder EscrowedAmount
        auto const mptokenID = keylet::mptoken(mptID.key, sender);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: MPToken not found for " << sender;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        if (!sle->isFieldPresent(sfLockedAmount))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: no locked amount in MPToken for "
                << to_string(sender);
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const locked = sle->getFieldU64(sfLockedAmount);
        auto const delta = grossAmount.mpt().value();

        // Underflow check for subtraction
        if (!canSubtract(STAmount(mptIssue, locked), STAmount(mptIssue, delta)))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: insufficient locked amount for "
                << to_string(sender) << ": " << locked << " < " << delta;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const newLocked = locked - delta;
        if (newLocked == 0)
            sle->makeFieldAbsent(sfLockedAmount);
        else
            sle->setFieldU64(sfLockedAmount, newLocked);
        view.update(sle);
    }

    // Note: The gross amount is the amount that was locked, the net
    // amount is the amount that is being unlocked. The difference is the fee
    // that was charged for the transfer. If this difference is greater than
    // zero, we need to update the outstanding amount.
    auto const diff = grossAmount.mpt().value() - netAmount.mpt().value();
    if (diff != 0)
    {
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        // Underflow check for subtraction
        if (!canSubtract(
                STAmount(mptIssue, outstanding), STAmount(mptIssue, diff)))
        {  // LCOV_EXCL_START
            JLOG(j.error())
                << "rippleUnlockEscrowMPT: insufficient outstanding amount for "
                << mptIssue.getMptID() << ": " << outstanding << " < " << diff;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - diff);
        view.update(sleIssuance);
    }
    return tesSUCCESS;
}

bool
after(NetClock::time_point now, std::uint32_t mark)
{
    return now.time_since_epoch().count() > mark;
}

}  // namespace ripple
