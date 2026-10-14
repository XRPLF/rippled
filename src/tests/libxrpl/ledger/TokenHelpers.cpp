#include <xrpl/ledger/helpers/TokenHelpers.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

using namespace xrpl;

namespace {

// A 25% transfer fee.
Rate const kRate125{1'250'000'000};

AccountID const kIssuer{1};

}  // namespace

TEST(TokenHelpers, add_transfer_fee)
{
    Issue const usd{toCurrency("USD"), kIssuer};
    MPTIssue const mpt{makeMptID(1, kIssuer)};

    EXPECT_EQ(addTransferFee(STAmount{usd, 80}, kRate125), STAmount(usd, 100));
    EXPECT_EQ(addTransferFee(STAmount{usd, 80}, kParityRate), STAmount(usd, 80));

    // Integral MPT: the fee on 1 unit rounds up to a whole unit.
    EXPECT_EQ(addTransferFee(STAmount{mpt, 80}, kRate125), STAmount(mpt, 100));
    EXPECT_EQ(addTransferFee(STAmount{mpt, 1}, kRate125), STAmount(mpt, 2));
    EXPECT_EQ(addTransferFee(STAmount{mpt, 1}, kParityRate), STAmount(mpt, 1));
}

TEST(TokenHelpers, subtract_transfer_fee)
{
    Issue const usd{toCurrency("USD"), kIssuer};
    MPTIssue const mpt{makeMptID(1, kIssuer)};

    EXPECT_EQ(subtractTransferFee(STAmount{usd, 100}, kRate125), STAmount(usd, 80));
    EXPECT_EQ(subtractTransferFee(STAmount{usd, 100}, kParityRate), STAmount(usd, 100));

    // Integral MPT: the payout rounds down, to zero for a single unit.
    EXPECT_EQ(subtractTransferFee(STAmount{mpt, 31}, kRate125), STAmount(mpt, 24));
    EXPECT_EQ(subtractTransferFee(STAmount{mpt, 1}, kRate125), STAmount(mpt));
    EXPECT_EQ(subtractTransferFee(STAmount{mpt, 1}, kParityRate), STAmount(mpt, 1));
}
