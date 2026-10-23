#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <memory>

namespace xrpl::test {

namespace {

/**
 * A LoanBroker SLE that is never written to the ledger. checkLoanBrokerDomain
 * only reads the flags and DomainID, so nothing else needs to be populated.
 */
SLE::pointer
makeBroker(EntryTestEnv const& e, bool isPrivate)
{
    auto sle = std::make_shared<SLE>(keylet::loanBroker(e.someID()));
    if (isPrivate)
        sle->setFlag(lsfLoanBrokerPrivate);
    return sle;
}

}  // namespace

TEST(LendingDomainTests, public_broker_skips_check)
{
    EntryTestEnv e;
    beast::Journal const j{beast::Journal::getNullSink()};

    // No DomainID either, which would fail on a private broker.
    auto const broker = makeBroker(e, false);
    EXPECT_EQ(checkLoanBrokerDomain(e.apply(), broker, e.alice.id(), j), TER{tesSUCCESS});
}

TEST(LendingDomainTests, private_broker_without_domain_is_not_authorised)
{
    EntryTestEnv e;
    beast::Journal const j{beast::Journal::getNullSink()};

    // A private broker may be created without a DomainID, and an update can
    // clear it. Nobody can borrow from such a broker until one is set.
    auto const broker = makeBroker(e, true);
    EXPECT_EQ(checkLoanBrokerDomain(e.apply(), broker, e.alice.id(), j), TER{tecNO_AUTH});
}

TEST(LendingDomainTests, private_broker_with_missing_domain_object)
{
    EntryTestEnv e;
    beast::Journal const j{beast::Journal::getNullSink()};

    // The DomainID points at nothing, so verifyValidDomain cannot find the
    // PermissionedDomain entry. That error is passed through unchanged.
    auto const broker = makeBroker(e, true);
    broker->setFieldH256(sfDomainID, e.someID());
    EXPECT_EQ(checkLoanBrokerDomain(e.apply(), broker, e.alice.id(), j), TER{tecOBJECT_NOT_FOUND});
}

}  // namespace xrpl::test
