#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/OpenView.h>
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
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>

#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace xrpl {

// The entry classes have no consumers yet, and an un-instantiated class
// template is barely type-checked. Instantiate every one explicitly so the
// compiler actually checks them. Keep this block even once real call sites
// exist: it is what catches a new ledger entry type being added without its
// wrapper class, or the wrapper class existing but never actually being used.
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
    !std::is_convertible_v<OfferEntryW, AccountRootEntryR>,
    "cross-entry-type conversion must not compile");
static_assert(
    !std::is_constructible_v<AccountRootEntryR, OfferEntryW>,
    "cross-entry-type construction must not compile, even explicitly");
static_assert(
    !std::is_convertible_v<OfferEntryR, AccountRootEntryR>,
    "read-only cross-entry-type conversion must not compile");

// Nor from a type-erased writable entry, which carries no static type.
static_assert(
    !std::is_convertible_v<WritableSLE, AccountRootEntryR>,
    "generic -> typed conversion must not compile");

// The intended conversions must keep working: same type writable -> read-only,
// and typed -> generic widening.
static_assert(
    std::is_convertible_v<AccountRootEntryW, AccountRootEntryR>,
    "same-type writable -> read-only conversion must keep working");
static_assert(
    std::is_convertible_v<AccountRootEntryW, ReadOnlySLE>,
    "typed -> generic widening must keep working");

// Detection idioms for the writable interface. These have to go through a
// template parameter: a requires-expression over a concrete type is checked
// eagerly, so spelling the calls out inline would be a hard error rather than
// the `false` the assertions below want.
template <typename T>
concept HasMutableRawSle = requires(T& t) { t.mutableRawSle(); };

template <typename T>
concept HasApplyView = requires(T& t) { t.applyView(); };

namespace test {

TEST(SLEBaseTests, ReadOnly)
{
    TxTest env;
    Account const alice("alice");
    Account const bob("bob");
    env.createAccount(alice, XRP(10'000));

    AccountRootEntryR const absent(bob.id(), env.getClosedLedger());
    EXPECT_FALSE(absent.exists());
    EXPECT_FALSE(static_cast<bool>(absent));
    // A typed entry knows its entry type even with nothing to read.
    EXPECT_EQ(absent.type(), ltACCOUNT_ROOT);

    AccountRootEntryR const present(alice.id(), env.getClosedLedger());
    EXPECT_TRUE(present.exists());
    EXPECT_TRUE(static_cast<bool>(present));
    EXPECT_EQ(present.key(), keylet::account(alice.id()).key);
    EXPECT_EQ(present.type(), ltACCOUNT_ROOT);
    EXPECT_EQ(present.keylet().type, ltACCOUNT_ROOT);
    EXPECT_EQ(present->getType(), ltACCOUNT_ROOT);
    EXPECT_EQ((*present).getType(), ltACCOUNT_ROOT);
    EXPECT_EQ(&present.readView(), &env.getClosedLedger());
}

TEST(SLEBaseTests, AdoptSLE)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10'000));

    auto const sle = env.getClosedLedger().read(keylet::account(alice.id()));
    ASSERT_NE(sle, nullptr);

    AccountRootEntryR const adopted(sle, env.getClosedLedger());
    EXPECT_TRUE(adopted.exists());
    EXPECT_EQ(adopted.rawSle(), sle);
    EXPECT_EQ(adopted.key(), keylet::account(alice.id()).key);
    EXPECT_EQ(adopted.type(), ltACCOUNT_ROOT);
    // keylet() reports the SLE's own type, not the entry's static binding, so
    // it stays truthful in a Release build where the constructor's
    // entry-type assert is compiled out.
    EXPECT_EQ(adopted.keylet().type, ltACCOUNT_ROOT);

    // Adopting a null SLE is allowed: the assert only fires on a
    // type mismatch, and a null pointer has no type to mismatch.
    AccountRootEntryR const empty(SLE::const_pointer{}, env.getClosedLedger());
    EXPECT_FALSE(empty.exists());
    EXPECT_EQ(empty.type(), ltACCOUNT_ROOT);

    // A generic entry adopting the same SLE has to read the type back.
    ReadOnlySLE const generic(sle, env.getClosedLedger());
    EXPECT_TRUE(generic.exists());
    EXPECT_EQ(generic.type(), ltACCOUNT_ROOT);
    EXPECT_EQ(generic.keylet().type, ltACCOUNT_ROOT);

    // There is deliberately no writable equivalent.
    static_assert(
        !std::is_constructible_v<AccountRootEntryW, SLE::pointer, ApplyView&>,
        "writable entries must not be constructible from a bare SLE");
}

TEST(SLEBaseTests, WritableAccessors)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10'000));

    ApplyViewImpl av(&env.getClosedLedger(), TapNone);
    beast::Journal const j{beast::Journal::getNullSink()};

    AccountRootEntryW account(alice.id(), av, j);
    EXPECT_TRUE(account.exists());
    EXPECT_EQ(account.mutableRawSle(), account.rawSle());
    EXPECT_EQ(&account.applyView(), &av);
    EXPECT_EQ(&account.readView(), static_cast<ReadView const*>(&av));
    EXPECT_EQ(&account.journal().sink(), &j.sink());

    // The mutable dereference operators reach the same entry.
    EXPECT_EQ(account.operator->(), account.rawSle().get());
    EXPECT_EQ(&*account, account.rawSle().get());

    // Everything handing out mutable access is non-const, so a const
    // writable entry is as inert as a read-only one.
    static_assert(HasMutableRawSle<AccountRootEntryW>);
    static_assert(HasApplyView<AccountRootEntryW>);
    static_assert(
        !HasMutableRawSle<AccountRootEntryW const>,
        "mutableRawSle() must not be callable on a const writable entry");
    static_assert(
        !HasApplyView<AccountRootEntryW const>,
        "applyView() must not be callable on a const writable entry");

    // Read-only entries do not have the writable interface at all.
    static_assert(
        !HasMutableRawSle<AccountRootEntryR>,
        "mutableRawSle() must not exist on a read-only entry");
    static_assert(
        !HasApplyView<AccountRootEntryR>, "applyView() must not exist on a read-only entry");
}

TEST(SLEBaseTests, ApplyViewContextCtor)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10'000));

    ApplyViewImpl av(&env.getClosedLedger(), TapNone);
    beast::Journal const j{beast::Journal::getNullSink()};

    transactions::AccountSetBuilder builder{alice.id()};
    builder.setSequence(env.getAccountRoot(alice.id()).getSequence());
    builder.setFee(XRPAmount(10));
    auto const tx = builder.build(alice.pk(), alice.sk()).getSTTx();
    ASSERT_NE(tx, nullptr);
    ApplyViewContext const ctx{.view = av, .tx = *tx};

    // Delegates to the (Keylet, ApplyView&) constructor; ctx.tx is not
    // retained, so this must be indistinguishable from building from
    // ctx.view directly.
    AccountRootEntryW fromCtx(keylet::account(alice.id()), ctx, j);
    EXPECT_TRUE(fromCtx.exists());
    EXPECT_EQ(&fromCtx.applyView(), &av);
    EXPECT_EQ(fromCtx.key(), keylet::account(alice.id()).key);

    AccountRootEntryW const fromView(keylet::account(alice.id()), av, j);
    EXPECT_EQ(fromCtx.rawSle(), fromView.rawSle());
}

TEST(SLEBaseTests, WritableLifecycle)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10'000));

    // A view we never apply, so nothing here reaches the ledger.
    ApplyViewImpl av(&env.getClosedLedger(), TapNone);

    // Entry that does not exist yet: newSLE() -> insert().
    {
        TicketEntryW ticket(keylet::ticket(alice.id(), SeqProxy::rawTicket(1)), av);
        EXPECT_FALSE(ticket.exists());
        EXPECT_EQ(ticket.key(), keylet::ticket(alice.id(), SeqProxy::rawTicket(1)).key);
        EXPECT_EQ(ticket.type(), ltTICKET);
        EXPECT_EQ(ticket.keylet().type, ltTICKET);

        ticket.newSLE();
        EXPECT_TRUE(ticket.exists());
        ticket.insert();
        ticket.update();

        // Erasing an entry inserted in this same view drops it outright.
        ticket.erase();
        EXPECT_FALSE(ticket.exists());
    }

    // Entry that already exists. ApplyStateTable::erase() keeps holding
    // this exact SLE and builds the DeletedNode's FinalFields from it, so
    // the entry must drop its pointer or a later write would silently
    // land in transaction metadata.
    {
        AccountRootEntryW account(alice.id(), av);
        EXPECT_TRUE(account.exists());

        account.erase();
        EXPECT_FALSE(account.exists());
    }
}

TEST(SLEBaseTests, Conversion)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10'000));

    ApplyViewImpl av(&env.getClosedLedger(), TapNone);

    AccountRootEntryW const writable(alice.id(), av);
    EXPECT_TRUE(writable.exists());

    AccountRootEntryR const readOnly = writable;
    EXPECT_TRUE(readOnly.exists());
    EXPECT_EQ(readOnly.rawSle(), writable.rawSle());

    ReadOnlySLE const generic = writable;
    EXPECT_TRUE(generic.exists());
    EXPECT_EQ(generic.rawSle(), writable.rawSle());
    // A generic entry has to read the type back out of the SLE.
    EXPECT_EQ(generic.type(), ltACCOUNT_ROOT);
}

TEST(SLEBaseTests, ResolveEntryPeeks)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10'000));

    // getOpenLedger() is an OpenView, which derives from ReadView but not
    // from ApplyView, so resolveEntry's dynamic_cast fails and this takes
    // the plain ReadView::read() path.
    OpenView const& ledger = env.getOpenLedger();
    AccountRootEntryR const overLedger(alice.id(), ledger);
    EXPECT_TRUE(overLedger.exists());

    ApplyViewImpl av(&ledger, TapNone);

    // ReadView const& binds an ApplyViewImpl just as happily, and there the
    // dynamic_cast succeeds, so this one resolves through ApplyView::peek().
    AccountRootEntryR const readOnly(alice.id(), av);
    EXPECT_TRUE(readOnly.exists());

    AccountRootEntryW writable(alice.id(), av);
    EXPECT_TRUE(writable.exists());

    // The invariant resolveEntry() exists to hold: one SLE per key per
    // view. read() would have handed back the base ledger's entry instead,
    // which is a different object.
    EXPECT_EQ(readOnly.rawSle(), writable.rawSle());
    EXPECT_NE(readOnly.rawSle(), overLedger.rawSle());

    // Which is what keeps a read-only entry from going stale: a write
    // through any other entry over the same view is visible through it.
    auto const bumped = writable->getFieldU32(sfSequence) + 1;
    writable->setFieldU32(sfSequence, bumped);
    EXPECT_EQ(readOnly->getFieldU32(sfSequence), bumped);
}

TEST(SLEBaseTests, ThrowsOnMissingEntry)
{
    TxTest const env;
    Account const bob("bob");

    // A generic read-only entry has no static type to fall back on, so
    // type() must read it off the (absent) SLE and throw.
    ReadOnlySLE const absent(keylet::account(bob.id()), env.getClosedLedger());
    EXPECT_FALSE(absent.exists());
    EXPECT_THROW(std::ignore = absent.type(), std::logic_error);

    // A per-type read-only entry always knows its type, but keylet() and
    // key() still have to derive the ledger key from the SLE.
    AccountRootEntryR const missing(bob.id(), env.getClosedLedger());
    EXPECT_FALSE(missing.exists());
    EXPECT_THROW(std::ignore = missing.key(), std::logic_error);
}

}  // namespace test
}  // namespace xrpl
