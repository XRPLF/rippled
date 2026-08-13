#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/noop.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMEntry.h>  // IWYU pragma: keep
#include <xrpl/ledger/helpers/AccountRootEntry.h>
#include <xrpl/ledger/helpers/AmendmentsEntry.h>       // IWYU pragma: keep
#include <xrpl/ledger/helpers/BridgeEntry.h>           // IWYU pragma: keep
#include <xrpl/ledger/helpers/CheckEntry.h>            // IWYU pragma: keep
#include <xrpl/ledger/helpers/CredentialEntry.h>       // IWYU pragma: keep
#include <xrpl/ledger/helpers/DIDEntry.h>              // IWYU pragma: keep
#include <xrpl/ledger/helpers/DelegateEntry.h>         // IWYU pragma: keep
#include <xrpl/ledger/helpers/DepositPreauthEntry.h>   // IWYU pragma: keep
#include <xrpl/ledger/helpers/DirectoryNodeEntry.h>    // IWYU pragma: keep
#include <xrpl/ledger/helpers/EscrowEntry.h>           // IWYU pragma: keep
#include <xrpl/ledger/helpers/FeeSettingsEntry.h>      // IWYU pragma: keep
#include <xrpl/ledger/helpers/LedgerHashesEntry.h>     // IWYU pragma: keep
#include <xrpl/ledger/helpers/LoanBrokerEntry.h>       // IWYU pragma: keep
#include <xrpl/ledger/helpers/LoanEntry.h>             // IWYU pragma: keep
#include <xrpl/ledger/helpers/MPTokenEntry.h>          // IWYU pragma: keep
#include <xrpl/ledger/helpers/MPTokenIssuanceEntry.h>  // IWYU pragma: keep
#include <xrpl/ledger/helpers/NFTokenOfferEntry.h>     // IWYU pragma: keep
#include <xrpl/ledger/helpers/NFTokenPageEntry.h>      // IWYU pragma: keep
#include <xrpl/ledger/helpers/NegativeUNLEntry.h>      // IWYU pragma: keep
#include <xrpl/ledger/helpers/OfferEntry.h>
#include <xrpl/ledger/helpers/OracleEntry.h>              // IWYU pragma: keep
#include <xrpl/ledger/helpers/PayChannelEntry.h>          // IWYU pragma: keep
#include <xrpl/ledger/helpers/PermissionedDomainEntry.h>  // IWYU pragma: keep
#include <xrpl/ledger/helpers/RippleStateEntry.h>         // IWYU pragma: keep
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/ledger/helpers/SignerListEntry.h>   // IWYU pragma: keep
#include <xrpl/ledger/helpers/SponsorshipEntry.h>  // IWYU pragma: keep
#include <xrpl/ledger/helpers/TicketEntry.h>
#include <xrpl/ledger/helpers/VaultEntry.h>                            // IWYU pragma: keep
#include <xrpl/ledger/helpers/XChainOwnedClaimIDEntry.h>               // IWYU pragma: keep
#include <xrpl/ledger/helpers/XChainOwnedCreateAccountClaimIDEntry.h>  // IWYU pragma: keep
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>

#include <type_traits>

namespace xrpl {

// The entry classes have no consumers yet, and an un-instantiated class
// template is barely type-checked. Instantiate every one explicitly so the
// compiler actually checks them. Delete this block once real call sites exist.
//
// Driving this off ledger_entries.macro keeps it exhaustive by construction:
// adding a ledger entry type without adding its entry class stops compiling
// here, and the static_assert pins each one to the right LedgerEntryType.
//
// Keep this loop in one file rather than splitting it across the per-entry
// *Entry_test.cpp suites. Those are hand-written, so a new ledger entry type
// would simply have no file there and nothing would complain; this is the only
// thing making the coverage exhaustive rather than merely extensive.

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
// per-type entry, so without the entry-type constraint it will bind any
// writable entry that slices to SLEBase. These assertions pin down which
// conversions are legal.

// An entry class for one entry type must never be constructible from another.
static_assert(
    !std::is_convertible_v<WOfferEntry, RAccountRootEntry>,
    "cross-entry-type conversion must not compile");
static_assert(
    !std::is_constructible_v<RAccountRootEntry, WOfferEntry>,
    "cross-entry-type construction must not compile, even explicitly");
static_assert(
    !std::is_convertible_v<ROfferEntry, RAccountRootEntry>,
    "read-only cross-entry-type conversion must not compile");

// Nor from a type-erased writable entry, which carries no static type.
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

// Detection idioms for the writable interface. These have to go through a
// template parameter: a requires-expression over a concrete type is checked
// eagerly, so spelling the calls out inline would be a hard error rather than
// the `false` the assertions below want.
template <typename T>
concept HasMutableSle = requires(T& t) { t.mutableSle(); };

template <typename T>
concept HasApplyView = requires(T& t) { t.applyView(); };

namespace test {

class SLEBase_test : public beast::unit_test::Suite
{
    void
    testReadOnly()
    {
        testcase("read-only entry");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice);
        env.close();

        RAccountRootEntry const absent(bob.id(), *env.current());
        BEAST_EXPECT(!absent.exists());
        BEAST_EXPECT(!static_cast<bool>(absent));
        // A typed entry knows its entry type even with nothing to read.
        BEAST_EXPECT(absent.type() == ltACCOUNT_ROOT);

        RAccountRootEntry const present(alice.id(), *env.current());
        BEAST_EXPECT(present.exists());
        BEAST_EXPECT(static_cast<bool>(present));
        BEAST_EXPECT(present.key() == keylet::account(alice.id()).key);
        BEAST_EXPECT(present.type() == ltACCOUNT_ROOT);
        BEAST_EXPECT(present.keylet().type == ltACCOUNT_ROOT);
        BEAST_EXPECT(present->getType() == ltACCOUNT_ROOT);
        BEAST_EXPECT((*present).getType() == ltACCOUNT_ROOT);
        BEAST_EXPECT(&present.readView() == &*env.current());
    }

    void
    testAdoptSLE()
    {
        testcase("read-only entry adopting an SLE");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto const sle = env.current()->read(keylet::account(alice.id()));
        BEAST_EXPECT(sle != nullptr);

        RAccountRootEntry const adopted(sle, *env.current(), env.journal);
        BEAST_EXPECT(adopted.exists());
        BEAST_EXPECT(adopted.sle() == sle);
        BEAST_EXPECT(adopted.key() == keylet::account(alice.id()).key);
        BEAST_EXPECT(adopted.type() == ltACCOUNT_ROOT);
        // keylet() reports the SLE's own type, not the entry's static binding, so
        // it stays truthful in a Release build where the constructor's
        // entry-type assert is compiled out.
        BEAST_EXPECT(adopted.keylet().type == ltACCOUNT_ROOT);

        // Adopting a null SLE is allowed: the assert only fires on a
        // type mismatch, and a null pointer has no type to mismatch.
        RAccountRootEntry const empty(SLE::const_pointer{}, *env.current());
        BEAST_EXPECT(!empty.exists());
        BEAST_EXPECT(empty.type() == ltACCOUNT_ROOT);

        // A generic entry adopting the same SLE has to read the type back.
        ReadOnlySLE const generic(sle, *env.current());
        BEAST_EXPECT(generic.exists());
        BEAST_EXPECT(generic.type() == ltACCOUNT_ROOT);
        BEAST_EXPECT(generic.keylet().type == ltACCOUNT_ROOT);

        // There is deliberately no writable equivalent.
        static_assert(
            !std::is_constructible_v<WAccountRootEntry, SLE::pointer, ApplyView&>,
            "writable entries must not be constructible from a bare SLE");
    }

    void
    testWritableAccessors()
    {
        testcase("writable entry accessors");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto const ledger = env.current();
        ApplyViewImpl av(&*ledger, TapNone);

        WAccountRootEntry account(alice.id(), av, env.journal);
        BEAST_EXPECT(account.canModify());
        BEAST_EXPECT(account.mutableSle() == account.sle());
        BEAST_EXPECT(&account.applyView() == &av);
        BEAST_EXPECT(&account.readView() == static_cast<ReadView const*>(&av));
        BEAST_EXPECT(&account.journal().sink() == &env.journal.sink());

        // The mutable dereference operators reach the same entry.
        BEAST_EXPECT(account.operator->() == account.sle().get());
        BEAST_EXPECT(&*account == account.sle().get());

        // Everything handing out mutable access is non-const, so a const
        // writable entry is as inert as a read-only one.
        static_assert(HasMutableSle<WAccountRootEntry>);
        static_assert(HasApplyView<WAccountRootEntry>);
        static_assert(
            !HasMutableSle<WAccountRootEntry const>,
            "mutableSle() must not be callable on a const writable entry");
        static_assert(
            !HasApplyView<WAccountRootEntry const>,
            "applyView() must not be callable on a const writable entry");

        // Read-only entries do not have the writable interface at all.
        static_assert(
            !HasMutableSle<RAccountRootEntry>, "mutableSle() must not exist on a read-only entry");
        static_assert(
            !HasApplyView<RAccountRootEntry>, "applyView() must not exist on a read-only entry");
    }

    void
    testApplyViewContextCtor()
    {
        testcase("writable entry from ApplyViewContext");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto const ledger = env.current();
        ApplyViewImpl av(&*ledger, TapNone);

        auto const jt = env.jt(noop(alice));
        BEAST_EXPECT(jt.stx != nullptr);
        ApplyViewContext const ctx{.view = av, .tx = *jt.stx};

        // Delegates to the (Keylet, ApplyView&) constructor; ctx.tx is not
        // retained, so this must be indistinguishable from building from
        // ctx.view directly.
        WAccountRootEntry fromCtx(keylet::account(alice.id()), ctx, env.journal);
        BEAST_EXPECT(fromCtx.exists());
        BEAST_EXPECT(fromCtx.canModify());
        BEAST_EXPECT(&fromCtx.applyView() == &av);
        BEAST_EXPECT(fromCtx.key() == keylet::account(alice.id()).key);

        WAccountRootEntry const fromView(keylet::account(alice.id()), av, env.journal);
        BEAST_EXPECT(fromCtx.sle() == fromView.sle());
    }

    void
    testWritableLifecycle()
    {
        testcase("writable entry lifecycle");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        // A view we never apply, so nothing here reaches the ledger. Hold the
        // ledger it points into: OpenLedger::current() hands out a copy of its
        // own shared_ptr, so a later close would otherwise free it.
        auto const ledger = env.current();
        ApplyViewImpl av(&*ledger, TapNone);

        // Entry that does not exist yet: newSLE() -> insert().
        {
            WTicketEntry ticket(keylet::ticket(alice.id(), SeqProxy::rawTicket(1)), av);
            BEAST_EXPECT(!ticket.exists());
            BEAST_EXPECT(!ticket.canModify());
            BEAST_EXPECT(ticket.key() == keylet::ticket(alice.id(), SeqProxy::rawTicket(1)).key);
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
        // the entry must drop its pointer or a later write would silently
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

        auto const ledger = env.current();
        ApplyViewImpl av(&*ledger, TapNone);

        WAccountRootEntry const writable(alice.id(), av);
        BEAST_EXPECT(writable.exists());

        RAccountRootEntry const readOnly = writable;
        BEAST_EXPECT(readOnly.exists());
        BEAST_EXPECT(readOnly.sle() == writable.sle());

        ReadOnlySLE const generic = writable;
        BEAST_EXPECT(generic.exists());
        BEAST_EXPECT(generic.sle() == writable.sle());
        // A generic entry has to read the type back out of the SLE.
        BEAST_EXPECT(generic.type() == ltACCOUNT_ROOT);
    }

    void
    testResolveEntryPeeks()
    {
        testcase("read-only entry over an ApplyView shares the view's SLE");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        // env.current() is an OpenView, which derives from ReadView but not
        // from ApplyView, so resolveEntry's dynamic_cast fails and this takes
        // the plain ReadView::read() path.
        auto const ledger = env.current();
        RAccountRootEntry const overLedger(alice.id(), *ledger);
        BEAST_EXPECT(overLedger.exists());

        ApplyViewImpl av(&*ledger, TapNone);

        // ReadView const& binds an ApplyViewImpl just as happily, and there the
        // dynamic_cast succeeds, so this one resolves through ApplyView::peek().
        RAccountRootEntry const readOnly(alice.id(), av);
        BEAST_EXPECT(readOnly.exists());

        WAccountRootEntry writable(alice.id(), av);
        BEAST_EXPECT(writable.exists());

        // The invariant resolveEntry() exists to hold: one SLE per key per
        // view. read() would have handed back the base ledger's entry instead,
        // which is a different object.
        BEAST_EXPECT(readOnly.sle() == writable.sle());
        BEAST_EXPECT(readOnly.sle() != overLedger.sle());

        // Which is what keeps a read-only entry from going stale: a write
        // through any other entry over the same view is visible through it.
        auto const bumped = writable->getFieldU32(sfSequence) + 1;
        writable->setFieldU32(sfSequence, bumped);
        BEAST_EXPECT(readOnly->getFieldU32(sfSequence) == bumped);
    }

public:
    void
    run() override
    {
        testReadOnly();
        testAdoptSLE();
        testWritableAccessors();
        testApplyViewContextCtor();
        testWritableLifecycle();
        testConversion();
        testResolveEntryPeeks();
    }
};

BEAST_DEFINE_TESTSUITE(SLEBase, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
