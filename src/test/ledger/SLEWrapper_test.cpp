#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootEntry.h>
#include <xrpl/ledger/helpers/OfferEntry.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/ledger/helpers/TicketEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <type_traits>

namespace xrpl {

// The wrappers have no consumers yet, and an un-instantiated class template is
// barely type-checked. Instantiate every one explicitly so the compiler
// actually checks them. Delete this block once real call sites exist.
//
// Driving this off ledger_entries.macro keeps it exhaustive by construction:
// adding a ledger entry type without adding its wrapper stops compiling here,
// and the static_assert pins each wrapper to the right LedgerEntryType.

template class SLEBase<ReadView>;
template class SLEBase<ApplyView>;

#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, ...)                                                    \
    template class name##Entry<ReadView>;                                                      \
    template class name##Entry<ApplyView>;                                                     \
    static_assert(                                                                             \
        name##Entry<ReadView>::kEntryType == tag && name##Entry<ApplyView>::kEntryType == tag, \
        #name "Entry must be bound to " #tag);

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")

// --- Entry-type safety, checked at compile time. ---
//
// The writable -> read-only converting constructor is inherited into every
// per-type wrapper, so without the entry-type constraint it will bind any
// writable wrapper that slices to SLEBase. These assertions pin down which
// conversions are legal.

// A wrapper for one entry type must never be constructible from another.
static_assert(
    !std::is_convertible_v<WOfferEntry, RAccountRootEntry>,
    "cross-entry-type conversion must not compile");
static_assert(
    !std::is_constructible_v<RAccountRootEntry, WOfferEntry>,
    "cross-entry-type construction must not compile, even explicitly");
static_assert(
    !std::is_convertible_v<ROfferEntry, RAccountRootEntry>,
    "read-only cross-entry-type conversion must not compile");

// Nor from a type-erased writable handle, which carries no static type.
static_assert(
    !std::is_convertible_v<WritableSLE, RAccountRootEntry>,
    "generic -> typed conversion must not compile");

// The intended conversions must keep working: same type writable -> read-only,
// and typed -> generic widening.
static_assert(
    std::is_convertible_v<WAccountRootEntry, RAccountRootEntry>,
    "same-type writable -> read-only conversion must keep working");
static_assert(
    std::is_convertible_v<WAccountRootEntry, ReadOnlySLE>,
    "typed -> generic widening must keep working");

namespace test {

class SLEWrapper_test : public beast::unit_test::Suite
{
    void
    testReadOnly()
    {
        testcase("read-only wrapper");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice);
        env.close();

        RAccountRootEntry const absent(bob.id(), *env.current());
        BEAST_EXPECT(!absent.exists());
        BEAST_EXPECT(!static_cast<bool>(absent));
        // A typed wrapper knows its entry type even with nothing to read.
        BEAST_EXPECT(absent.type() == ltACCOUNT_ROOT);

        RAccountRootEntry const present(alice.id(), *env.current());
        BEAST_EXPECT(present.exists());
        BEAST_EXPECT(static_cast<bool>(present));
        BEAST_EXPECT(present.key() == keylet::account(alice.id()).key);
        BEAST_EXPECT(present.type() == ltACCOUNT_ROOT);
        BEAST_EXPECT(present.keylet().type == ltACCOUNT_ROOT);
        BEAST_EXPECT(present->getType() == ltACCOUNT_ROOT);
    }

    void
    testWritableLifecycle()
    {
        testcase("writable wrapper lifecycle");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        // A view we never apply, so nothing here reaches the ledger.
        ApplyViewImpl av(&*env.current(), TapNone);

        // Entry that does not exist yet: newSLE() -> insert().
        {
            WTicketEntry ticket(keylet::ticket(alice.id(), 1), av);
            BEAST_EXPECT(!ticket.exists());
            BEAST_EXPECT(!ticket.canModify());
            BEAST_EXPECT(ticket.key() == keylet::ticket(alice.id(), 1).key);
            BEAST_EXPECT(ticket.type() == ltTICKET);
            BEAST_EXPECT(ticket.keylet().type == ltTICKET);

            ticket.newSLE();
            BEAST_EXPECT(ticket.exists());
            BEAST_EXPECT(ticket.canModify());
            ticket.insert();
            ticket.update();

            // Erasing an entry inserted in this same view drops it outright.
            ticket.erase();
            BEAST_EXPECT(!ticket.exists());
            BEAST_EXPECT(!ticket.canModify());
        }

        // Entry that already exists. ApplyStateTable::erase() keeps holding
        // this exact SLE and builds the DeletedNode's FinalFields from it, so
        // the wrapper must drop its pointer or a later write would silently
        // land in transaction metadata.
        {
            WAccountRootEntry account(alice.id(), av);
            BEAST_EXPECT(account.exists());
            BEAST_EXPECT(account.canModify());

            account.erase();
            BEAST_EXPECT(!account.exists());
            BEAST_EXPECT(!account.canModify());
        }
    }

    void
    testConversion()
    {
        testcase("writable to read-only conversion");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        ApplyViewImpl av(&*env.current(), TapNone);

        WAccountRootEntry const writable(alice.id(), av);
        BEAST_EXPECT(writable.exists());

        RAccountRootEntry const readOnly = writable;
        BEAST_EXPECT(readOnly.exists());
        BEAST_EXPECT(readOnly.sle() == writable.sle());

        ReadOnlySLE const generic = writable;
        BEAST_EXPECT(generic.exists());
        BEAST_EXPECT(generic.sle() == writable.sle());
        // A generic wrapper has to read the type back out of the entry.
        BEAST_EXPECT(generic.type() == ltACCOUNT_ROOT);
    }

public:
    void
    run() override
    {
        testReadOnly();
        testWritableLifecycle();
        testConversion();
    }
};

BEAST_DEFINE_TESTSUITE(SLEWrapper, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
