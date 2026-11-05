#include <xrpl/protocol/Emitable.h>

#include <xrpl/protocol/TxFormats.h>

#include <gtest/gtest.h>

using namespace xrpl;

// Whether a contract may emit a transaction follows that transaction's emittance setting.
TEST(Emitable, follows_each_transaction_s_emittance)
{
    auto const& emitable = Emitable::getInstance();

    EXPECT_TRUE(emitable.isEmitable(ttPAYMENT));
    EXPECT_TRUE(emitable.isEmitable(ttMPTOKEN_ISSUANCE_CREATE));

    EXPECT_FALSE(emitable.isEmitable(ttREGULAR_KEY_SET));
    EXPECT_FALSE(emitable.isEmitable(ttSIGNER_LIST_SET));
    EXPECT_FALSE(emitable.isEmitable(ttACCOUNT_DELETE));
    EXPECT_FALSE(emitable.isEmitable(ttBATCH));
    EXPECT_FALSE(emitable.isEmitable(ttAMENDMENT));
}

// A type no transaction has is not one a contract may emit.
TEST(Emitable, a_type_with_no_transaction_is_not_emitable)
{
    EXPECT_FALSE(Emitable::getInstance().isEmitable(static_cast<TxType>(9'999)));
}
