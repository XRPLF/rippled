#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <optional>
#include <string>

namespace xrpl {
namespace test {

/**
 * Scaffolding shared by the per-entry-type suites.
 *
 * Each of those suites needs the same three things: a ledger with a few funded
 * accounts, a throwaway ApplyView that is never applied, and some arbitrary
 * uint256 to stand in for an object ID. Build one of these per suite rather
 * than per testcase -- Env construction dominates the runtime of these tests by
 * a wide margin, and none of the assertions mutate the ledger.
 */
class EntryTestEnv
{
public:
    jtx::Env env;
    jtx::Account const alice{"alice"};
    jtx::Account const bob{"bob"};
    jtx::Account const carol{"carol"};

    explicit EntryTestEnv(beast::unit_test::Suite& suite) : env(suite)
    {
        env.fund(jtx::XRP(10'000), alice, bob, carol);
        env.close();
        // Built last: it holds a pointer to the ledger the assertions read
        // from, so it has to be the ledger that funding left behind.
        av_.emplace(&*env.current(), TapNone);
    }

    ReadView const&
    read()
    {
        return *env.current();
    }

    ApplyView&
    apply()
    {
        return *av_;
    }

    /**
     * An arbitrary but stable uint256, for the entry constructors that take
     * an object ID directly. Nothing in the ledger has this key, which is the
     * point: those overloads should resolve to a non-existent entry.
     */
    uint256
    someID()
    {
        return read().header().parentHash;
    }

private:
    std::optional<ApplyViewImpl> av_;
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
expectKeylet(
    beast::unit_test::Suite& suite,
    EntryTestEnv& e,
    Keylet const& expected,
    std::string const& what,
    Args const&... args)
{
    bool const present = e.read().read(expected) != nullptr;

    // The writable entry retains its keylet, so it can be inspected whether
    // or not the entry exists.
    Entry<ApplyView> w(args..., e.apply());
    suite.expect(w.keylet().key == expected.key, what + ": writable key");
    suite.expect(w.keylet().type == expected.type, what + ": writable type");
    suite.expect(w.exists() == present, what + ": writable exists");

    // The read-only entry has no keylet of its own -- it derives one from the
    // SLE, and only when the SLE exists. Its agreement with a direct read
    // of the expected keylet is what shows it resolved the same key.
    Entry<ReadView> const r(args..., e.read());
    suite.expect(r.exists() == present, what + ": read-only exists");
    suite.expect(r.type() == expected.type, what + ": read-only type");
    if (present)
        suite.expect(r.key() == expected.key, what + ": read-only key");

    // The compile-time binding has to agree with the keylet too.
    static_assert(Entry<ReadView>::kEntryType == Entry<ApplyView>::kEntryType);
    suite.expect(Entry<ReadView>::kEntryType == expected.type, what + ": kEntryType");
}

}  // namespace test
}  // namespace xrpl
