#pragma once

#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/ledger/LedgerFFI.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

namespace xrpl::test::formal_verification {

// Base for suites that mirror a rippled view into a Lean ledger
class LedgerSuite : public LeanSuite
{
    static bool
    isModeled(LedgerEntryType t)
    {
        switch (t)
        {
#define X(Name) case ledger_entries::Name::entryType:
            XRPL_LEAN_LEDGER_ENTRIES(X)
#undef X
            return true;
            default:
                return false;
        }
    }

    static std::vector<std::shared_ptr<SLE const>>
    modeledEntries(ReadView const& view)
    {
        std::vector<std::shared_ptr<SLE const>> out;
        for (auto key = view.succ(uint256{}); key; key = view.succ(*key))
        {
            if (auto sle = view.read(keylet::unchecked(*key)); sle && isModeled(sle->getType()))
                out.push_back(std::move(sle));
        }
        return out;
    }

    static void
    sleToLean(LedgerFFIBuilder& b, std::shared_ptr<SLE const> const& sle)
    {
        auto const k = sle->key();
        switch (sle->getType())
        {
#define X(Name)                                                                \
    case ledger_entries::Name::entryType:                                      \
        b.add(Name##FFIBuilder().fromCpp(ledger_entries::Name(sle)).build(k)); \
        break;
            XRPL_LEAN_LEDGER_ENTRIES(X)
#undef X
            default:
                break;
        }
    }

    static std::shared_ptr<SLE const>
    sleFromLean(LedgerFFI const& l, std::shared_ptr<SLE const> const& sle)
    {
        auto const entry = l.read(sle->key());
        return entry ? entry->toSle() : nullptr;
    }

    LedgerFFI
    fromCpp(ReadView const& view)
    {
        auto const& info = view.header();
        auto const closeTime = info.parentCloseTime.time_since_epoch().count();

        LedgerFFIBuilder lb;
        lb.header(LedgerHeaderFFI::build(info.seq, closeTime, info.parentHash))
            .fees(FeesFFI::build(view.fees().base, view.fees().reserve, view.fees().increment));
        for (auto const& sle : modeledEntries(view))
            sleToLean(lb, sle);

        return lb.build();
    }

    static bool
    headersEqual(ReadView const& view, LedgerFFI const& ledger)
    {
        auto const& info = view.header();
        auto const h = ledger.header();
        return h.seq() == info.seq &&
            h.parentCloseTime() == info.parentCloseTime.time_since_epoch().count() &&
            h.parentHash() == info.parentHash;
    }

    static bool
    feesEqual(ReadView const& view, LedgerFFI const& ledger)
    {
        auto const f = ledger.fees();
        return f.base() == view.fees().base && f.reserve() == view.fees().reserve &&
            f.increment() == view.fees().increment;
    }

    bool
    expectLedgersMatch(ReadView const& view, LedgerFFI const& ledger, char const* label)
    {
        auto const entries = modeledEntries(view);
        for (auto const& sle : entries)
        {
            auto const lean = sleFromLean(ledger, sle);
            if (!lean || *sle != *lean)
            {
                std::stringstream ss;
                ss << label << ": entry " << to_string(sle->key());
                if (lean)
                    ss << " differs\n  C++:  " << sle->getText() << "\n  Lean: " << lean->getText();
                else
                    ss << " missing on the Lean side";
                fail(ss.str());
                return false;
            }
        }
        std::set<uint256> cppKeys;
        for (auto const& sle : entries)
            cppKeys.insert(sle->key());
        for (auto const& k : ledger.keys())
        {
            if (!cppKeys.count(k))
            {
                auto const lean = ledger.read(k);
                std::stringstream ss;
                ss << label << ": Lean holds entry " << to_string(k) << " absent from C++";
                if (lean)
                    ss << "\n  Lean: " << lean->toSle()->getText();
                fail(ss.str());
                return false;
            }
        }
        if (!headersEqual(view, ledger))
        {
            auto const& info = view.header();
            auto const h = ledger.header();
            std::stringstream ss;
            ss << label << ": header differs\n  C++:  seq=" << info.seq
               << " parentCloseTime=" << info.parentCloseTime.time_since_epoch().count()
               << " parentHash=" << to_string(info.parentHash) << "\n  Lean: seq=" << h.seq()
               << " parentCloseTime=" << h.parentCloseTime()
               << " parentHash=" << to_string(h.parentHash());
            fail(ss.str());
            return false;
        }
        if (!feesEqual(view, ledger))
        {
            auto const f = ledger.fees();
            std::stringstream ss;
            ss << label << ": fees differ\n  C++:  base=" << view.fees().base
               << " reserve=" << view.fees().reserve << " increment=" << view.fees().increment
               << "\n  Lean: base=" << f.base() << " reserve=" << f.reserve()
               << " increment=" << f.increment();
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

protected:
    void
    expectSameSle(
        SLE const& expected,
        SLE const& got,
        char const* label,
        char const* side = nullptr)
    {
        if (expected == got)
        {
            pass();
            return;
        }
        std::stringstream ss;
        ss << label;
        if (side)
            ss << " (" << side << ")";
        ss << ":\n  expected " << expected.getText() << "\n  got      " << got.getText();
        fail(ss.str());
    }

    template <class Op>
    void
    runLedgerTest(ReadView const& view, char const* label, Op&& op)
    {
        beginCase(label);
        LedgerFFI ledger = fromCpp(view);
        op(ledger);
        expectLedgersMatch(view, ledger, label);
    }

    // A native (XRP) AccountRoot balance is a protocol invariant >= 0; flag any negative.
    void
    expectNoNegativeNativeBalance(ReadView const& view, char const* label)
    {
        for (auto key = view.succ(uint256{}); key; key = view.succ(*key))
        {
            auto const sle = view.read(keylet::unchecked(*key));
            if (sle && sle->getType() == ltACCOUNT_ROOT &&
                sle->getFieldAmount(sfBalance) < beast::kZero)
                fail(std::string(label) + ": negative XRP balance");
        }
    }

    std::optional<STAmount>
    holdingSTAmount(ReadView const& view, AccountID const& account, Asset const& asset)
    {
        if (account == beast::kZero)
            return std::nullopt;
        if (asset.native())
        {
            if (auto const sle = view.read(keylet::account(account)))
                return sle->getFieldAmount(sfBalance);
            return std::nullopt;
        }
        if (asset.holds<Issue>())
        {
            auto const& iss = asset.get<Issue>();
            if (account == iss.account)
                return std::nullopt;
            if (auto const sle = view.read(keylet::line(account, iss.account, iss.currency)))
            {
                STAmount bal = sle->getFieldAmount(sfBalance);
                if (account > iss.account)
                    bal.negate();  // flip for the high account
                return bal;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::uint64_t>
    holdingMPTAmount(ReadView const& view, AccountID const& account, Asset const& asset)
    {
        if (!asset.holds<MPTIssue>() || account == beast::kZero)
            return std::nullopt;
        auto const& mpt = asset.get<MPTIssue>();
        if (account == mpt.getIssuer())
            return std::nullopt;
        if (auto const sle = view.read(keylet::mptoken(mpt.getMptID(), account)))
            return sle->getFieldU64(sfMPTAmount);
        return std::nullopt;
    }

    // A send must not increase the sender's holding nor decrease a receiver's.
    template <typename T>
    void
    expectHoldingDir(
        std::optional<T> const& pre,
        std::optional<T> const& post,
        bool sender,
        char const* label)
    {
        if (!pre || !post)
            return;
        if (sender && *post > *pre)
            fail(std::string(label) + ": sender holding increased");
        if (!sender && *post < *pre)
            fail(std::string(label) + ": receiver holding decreased");
    }
};

}  // namespace xrpl::test::formal_verification
