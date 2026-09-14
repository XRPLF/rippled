#include <xrpl/protocol/STXChainBridge.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace xrpl;

namespace {

// Built from raw bytes rather than base58 so the test does not depend on
// hand-computed checksums.
AccountID
account(std::string_view hex)
{
    AccountID id;
    EXPECT_TRUE(id.parseHex(hex));
    return id;
}

}  // namespace

// getText() builds its string from eight substitutions of the same type, so a
// transposed pair would still compile and still type check. Pin the output so
// the field/value pairing is actually verified.
TEST(STXChainBridge, getTextPairsEachFieldWithItsValue)
{
    auto const lockingDoor = account("0102030405060708090A0B0C0D0E0F1011121314");
    auto const issuingDoor = account("14131211100F0E0D0C0B0A090807060504030201");

    auto const lockingIssue = xrpIssue();
    Issue const issuingIssue{toCurrency("USD"), issuingDoor};

    STXChainBridge const bridge{lockingDoor, lockingIssue, issuingDoor, issuingIssue};

    std::string const expected = "{ LockingChainDoor = " + toBase58(lockingDoor) +
        ", LockingChainIssue = " + lockingIssue.getText() +
        ", IssuingChainDoor = " + toBase58(issuingDoor) +
        ", IssuingChainIssue = " + issuingIssue.getText() + " }";

    EXPECT_EQ(bridge.getText(), expected);
}

TEST(STXChainBridge, getTextOnADefaultBridge)
{
    STXChainBridge const bridge;
    auto const text = bridge.getText();

    // The outer braces are literal, and the four field names appear in
    // declaration order regardless of the values.
    EXPECT_TRUE(text.starts_with("{ LockingChainDoor = "));
    EXPECT_TRUE(text.ends_with(" }"));
    EXPECT_LT(text.find("LockingChainIssue"), text.find("IssuingChainDoor"));
    EXPECT_LT(text.find("IssuingChainDoor"), text.find("IssuingChainIssue"));
}
