#include <xrpl/telemetry/Redaction.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <string>

using namespace xrpl;
using namespace xrpl::telemetry;

// Empty input must produce empty output (edge / negative path).
TEST(Redaction, empty_input_returns_empty)
{
    EXPECT_EQ(redactAccount(""), "");
}

// Success path: assert the EXACT expected token. The value is the first
// 16 chars of sha512Half(addr) rendered as lowercase hex, computed once
// and hard-coded here so any change to the hashing recipe is caught.
TEST(Redaction, known_account_exact_hash)
{
    EXPECT_EQ(redactAccount("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"), "a513895f49311f54");
}

// Structural invariants: length, lowercase-hex alphabet, and stability.
TEST(Redaction, token_is_16_lowercase_hex_and_stable)
{
    auto const token = redactAccount("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
    EXPECT_EQ(token.size(), 16u);
    EXPECT_TRUE(std::ranges::all_of(token, [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    }));
    // Deterministic: hashing the same input twice yields the same token.
    EXPECT_EQ(token, redactAccount("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"));
}

// Distinct inputs must produce distinct tokens (no accidental constant).
TEST(Redaction, distinct_inputs_distinct_tokens)
{
    EXPECT_NE(
        redactAccount("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"),
        redactAccount("rDifferentAccount1234567890abcdefgh"));
}
