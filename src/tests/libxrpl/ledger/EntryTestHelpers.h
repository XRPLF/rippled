#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>

#include <string>

namespace xrpl::test {

/**
 * Scaffolding shared by the per-entry-type suites.
 *
 * Each of those suites needs the same three things: a ledger with a few funded
 * accounts, a throwaway ApplyView that is never applied, and some arbitrary
 * uint256 to stand in for an object ID. Build one of these per test case --
 * TxTest construction dominates the runtime of these tests by a wide margin,
 * and none of the assertions mutate the ledger.
 */
class EntryTestEnv
{
public:
    TxTest env;
    Account const alice{"alice"};
    Account const bob{"bob"};
    Account const carol{"carol"};

    EntryTestEnv() : av_(&fundAndClose(), TapNone)
    {
    }

    /**
     * The closed ledger apply() was built over. Nothing here closes another
     * ledger or submits a transaction afterward, so this and apply() never
     * diverge.
     */
    [[nodiscard]] ReadView const&
    read() const
    {
        return env.getClosedLedger();
    }

    [[nodiscard]] ApplyView&
    apply()
    {
        return av_;
    }

    /**
     * An arbitrary but stable uint256, for the entry constructors that take
     * an object ID directly. Nothing in the ledger has this key, which is the
     * point: those overloads should resolve to a non-existent entry.
     */
    [[nodiscard]] uint256
    someID() const
    {
        return read().header().parentHash;
    }

private:
    // Runs from the av_ member initializer, so it may only touch env and the
    // accounts -- everything declared above av_.
    ReadView const&
    fundAndClose()
    {
        env.createAccount(alice, XRP(10'000));
        env.createAccount(bob, XRP(10'000));
        env.createAccount(carol, XRP(10'000));
        env.close();
        return env.getClosedLedger();
    }

    ApplyViewImpl av_;
};

/**
 * Assert that both flavors of @p Entry built from @p args resolve the ledger
 * object that @p expected names.
 *
 * The entry classes are near identical, so the defect they invite is a
 * copy-paste one: a constructor that reaches the wrong keylet:: function, or
 * that transposes two same-typed arguments. Comparing against an independently
 * spelled-out keylet at the call site catches exactly that.
 *
 * @p what names the overload under test, so a failure says which one broke.
 */
template <template <typename> class Entry, typename... Args>
void
expectKeylet(EntryTestEnv& e, Keylet const& expected, std::string const& what, Args const&... args)
{
    bool const present = e.read().read(expected) != nullptr;

    // The writable entry retains its keylet, so it can be inspected whether
    // or not the entry exists.
    Entry<ApplyView> const w(args..., e.apply());
    EXPECT_EQ(w.keylet().key, expected.key) << what << ": writable key";
    EXPECT_EQ(w.keylet().type, expected.type) << what << ": writable type";
    EXPECT_EQ(w.exists(), present) << what << ": writable exists";

    // The read-only entry has no keylet of its own -- it derives one from the
    // SLE, and only when the SLE exists. Its agreement with a direct read
    // of the expected keylet is what shows it resolved the same key.
    Entry<ReadView> const r(args..., e.read());
    EXPECT_EQ(r.exists(), present) << what << ": read-only exists";
    if (present)
    {
        EXPECT_EQ(r.key(), expected.key) << what << ": read-only key";
    }

    // Not r.type(): for a typed entry that returns kEntryType, so checking it
    // would just be this same assertion spelled twice.
    static_assert(Entry<ReadView>::kEntryType == Entry<ApplyView>::kEntryType);
    EXPECT_EQ(Entry<ReadView>::kEntryType, expected.type) << what << ": kEntryType";
}

}  // namespace xrpl::test
