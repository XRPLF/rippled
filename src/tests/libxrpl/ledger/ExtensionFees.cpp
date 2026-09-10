#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/XRPAmount.h>

#include <gtest/gtest.h>
#include <helpers/TestFamily.h>

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

namespace xrpl::test {

namespace {

// Unlike every protocol constant, so an assertion cannot pass by chance.
constexpr std::uint32_t kLocalGasLimit{123'457};
constexpr std::uint32_t kLocalBytecodeSizeLimit{23'457};
constexpr std::uint32_t kLocalGasPrice{7'654'321};

// Stands in for FeeSetup::toFees(): local configuration.
Fees
localFees()
{
    return Fees{
        XRPAmount{10},
        XRPAmount{10'000'000},
        XRPAmount{2'000'000},
        kLocalGasLimit,
        kLocalBytecodeSizeLimit,
        kLocalGasPrice};
}

}  // namespace

class ExtensionFees : public ::testing::Test
{
protected:
    std::unordered_set<uint256, beast::Uhash<>> presets_;
    TestFamily family_{beast::Journal{beast::Journal::getNullSink()}};

    std::shared_ptr<Ledger>
    makeGenesis(std::vector<uint256> const& amendments)
    {
        presets_.insert(amendments.begin(), amendments.end());
        return std::make_shared<Ledger>(
            kCreateGenesis, Rules{presets_}, localFees(), amendments, family_);
    }

    // Closed successor with FeeSettings rewritten by `edit`; setImmutable runs
    // Ledger::setup, so fees() is what the resolution produced.
    template <typename F>
    std::shared_ptr<Ledger>
    successorWithFeeSettings(Ledger const& parent, F&& edit)
    {
        auto ledger = std::make_shared<Ledger>(parent, NetClock::time_point{});
        auto sle = std::make_shared<SLE>(*ledger->read(keylet::feeSettings()));
        edit(*sle);
        ledger->rawReplace(sle);
        ledger->setImmutable();
        return ledger;
    }
};

// test how Ledger::setup decides the three extension fee fields

// Genesis is the one place local configuration legitimately reaches the ledger.
TEST_F(ExtensionFees, GenesisWritesTheConfiguredValues)
{
    auto const ledger = makeGenesis({featureSmartEscrow});

    EXPECT_EQ(ledger->fees().gasLimit, kLocalGasLimit);
    EXPECT_EQ(ledger->fees().bytecodeSizeLimit, kLocalBytecodeSizeLimit);
    EXPECT_EQ(ledger->fees().gasPrice, kLocalGasPrice);
}

// Absent must resolve to the protocol constant, never to the constructed Fees,
// which every caller builds from local configuration.
TEST_F(ExtensionFees, AbsentFieldsResolveToTheProtocolConstants)
{
    auto const genesis = makeGenesis({featureSmartEscrow});

    auto const ledger = successorWithFeeSettings(*genesis, [](SLE& sle) {
        sle.makeFieldAbsent(sfGasLimit);
        sle.makeFieldAbsent(sfBytecodeSizeLimit);
        sle.makeFieldAbsent(sfGasPrice);
    });

    ASSERT_TRUE(ledger->rules().enabled(featureSmartEscrow));

    EXPECT_EQ(ledger->fees().gasLimit, kDefaultGasLimit);
    EXPECT_EQ(ledger->fees().bytecodeSizeLimit, kDefaultBytecodeSizeLimit);
    EXPECT_EQ(ledger->fees().gasPrice, kDefaultGasPrice);

    EXPECT_NE(ledger->fees().gasLimit, kLocalGasLimit);
    EXPECT_NE(ledger->fees().bytecodeSizeLimit, kLocalBytecodeSizeLimit);
    EXPECT_NE(ledger->fees().gasPrice, kLocalGasPrice);
}

// The fallback only fills gaps; a present field still governs.
TEST_F(ExtensionFees, PresentFieldsAreTakenFromTheLedger)
{
    constexpr std::uint32_t kVotedGasLimit{987'654};
    constexpr std::uint32_t kVotedBytecodeSizeLimit{87'654};
    constexpr std::uint32_t kVotedGasPrice{76'543};

    auto const genesis = makeGenesis({featureSmartEscrow});

    auto const ledger = successorWithFeeSettings(*genesis, [&](SLE& sle) {
        sle.at(sfGasLimit) = kVotedGasLimit;
        sle.at(sfBytecodeSizeLimit) = kVotedBytecodeSizeLimit;
        sle.at(sfGasPrice) = kVotedGasPrice;
    });

    EXPECT_EQ(ledger->fees().gasLimit, kVotedGasLimit);
    EXPECT_EQ(ledger->fees().bytecodeSizeLimit, kVotedBytecodeSizeLimit);
    EXPECT_EQ(ledger->fees().gasPrice, kVotedGasPrice);
}

// An explicit zero is the kill switch, not absence.
TEST_F(ExtensionFees, AnExplicitZeroIsNotTreatedAsAbsent)
{
    auto const genesis = makeGenesis({featureSmartEscrow});

    auto const ledger = successorWithFeeSettings(*genesis, [](SLE& sle) {
        sle.at(sfGasLimit) = 0u;
        sle.at(sfBytecodeSizeLimit) = 0u;
    });

    EXPECT_EQ(ledger->fees().gasLimit, 0u);
    EXPECT_EQ(ledger->fees().bytecodeSizeLimit, 0u);
}

}  // namespace xrpl::test
