#include <xrpl/telemetry/TxAccountSpanNames.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFormats.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Contract tests for the account-field attribute keys of the tx.process span.
 *
 * The key set is a cross-component contract: Tempo span-filter tags, the
 * naming CI check and dashboards read these exact strings. A transaction type
 * that gains an account-typed field without a key would silently emit nothing
 * for it, so the completeness tests derive the required set from TxFormats
 * and from the SField registry rather than from a copied list.
 */

using namespace xrpl;
using namespace xrpl::telemetry;

namespace {

// Every account-typed field that any transaction format carries at top level,
// including the common fields shared by all formats.
std::set<SField const*>
accountFieldsInTransactionFormats()
{
    std::set<SField const*> fields;
    for (auto const& format : TxFormats::getInstance())
    {
        for (auto const& element : format.getSOTemplate())
        {
            if (element.sField().fieldType == STI_ACCOUNT)
                fields.insert(&element.sField());
        }
    }
    return fields;
}

// Every account-typed field the protocol defines, whether or not a transaction
// carries it.
std::set<SField const*>
allAccountFields()
{
    std::set<SField const*> fields;
    for (auto const& [code, field] : SField::getKnownCodeToField())
    {
        if (field->fieldType == STI_ACCOUNT)
            fields.insert(field);
    }
    return fields;
}

}  // namespace

TEST(TxAccountSpanNames, every_account_field_a_transaction_can_carry_has_a_key)
{
    auto const fields = accountFieldsInTransactionFormats();
    // Setup: the walk over TxFormats found the one field every transaction has.
    ASSERT_TRUE(fields.contains(&sfAccount));

    for (auto const* field : fields)
    {
        EXPECT_TRUE(accountFieldAttributeKey(*field).has_value())
            << "no attribute key for " << field->getName();
    }
}

TEST(TxAccountSpanNames, account_fields_no_transaction_carries_have_no_key)
{
    auto const carried = accountFieldsInTransactionFormats();
    auto const all = allAccountFields();
    // Setup: the registry holds more account fields than transactions carry.
    ASSERT_TRUE(all.contains(&sfLowSponsor));
    ASSERT_FALSE(carried.contains(&sfLowSponsor));

    for (auto const* field : all)
    {
        if (!carried.contains(field))
            EXPECT_EQ(accountFieldAttributeKey(*field), std::nullopt) << field->getName();
    }
}

TEST(TxAccountSpanNames, every_key_is_tx_plus_the_field_name_in_lower_snake_case)
{
    for (auto const* field : accountFieldsInTransactionFormats())
    {
        auto const maybeKey = accountFieldAttributeKey(*field);
        ASSERT_TRUE(maybeKey.has_value()) << field->getName();
        auto const key = maybeKey.value_or(std::string_view{});

        // Shape: tx_ prefix, then lower_snake_case with no empty segment.
        EXPECT_TRUE(key.starts_with("tx_")) << key;
        EXPECT_FALSE(key.ends_with('_')) << key;
        EXPECT_EQ(key.find("__"), std::string_view::npos) << key;
        EXPECT_TRUE(std::ranges::all_of(key, [](unsigned char c) {
            return std::islower(c) != 0 || std::isdigit(c) != 0 || c == '_';
        })) << key;

        // Content: the key with prefix and underscores removed is the field's
        // JSON name lowercased. Catches a misspelt or swapped key.
        std::string flattened(key.substr(3));
        std::erase(flattened, '_');
        std::string lowered = field->getName();
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        EXPECT_EQ(flattened, lowered) << key;
    }
}

// The published contract: every carried field and its exact key. Literals are
// deliberate here; the point is to pin underscore placement, which the shape
// test above cannot see.
TEST(TxAccountSpanNames, exact_key_for_every_carried_field)
{
    std::vector<std::pair<SField const*, std::string_view>> const expected = {
        {&sfAccount, "tx_account"},
        {&sfDestination, "tx_destination"},
        {&sfOwner, "tx_owner"},
        {&sfIssuer, "tx_issuer"},
        {&sfAuthorize, "tx_authorize"},
        {&sfUnauthorize, "tx_unauthorize"},
        {&sfRegularKey, "tx_regular_key"},
        {&sfNFTokenMinter, "tx_nftoken_minter"},
        {&sfHolder, "tx_holder"},
        {&sfDelegate, "tx_delegate"},
        {&sfSponsor, "tx_sponsor"},
        {&sfSponsee, "tx_sponsee"},
        {&sfCounterparty, "tx_counterparty"},
        {&sfCounterpartySponsor, "tx_counterparty_sponsor"},
        {&sfSubject, "tx_subject"},
        {&sfOtherChainSource, "tx_other_chain_source"},
        {&sfOtherChainDestination, "tx_other_chain_destination"},
        {&sfAttestationSignerAccount, "tx_attestation_signer_account"},
        {&sfAttestationRewardAccount, "tx_attestation_reward_account"},
    };
    // Setup: this list and the TxFormats walk must name the same fields, or a
    // row is missing here.
    std::set<SField const*> listed;
    for (auto const& [field, key] : expected)
        listed.insert(field);
    ASSERT_EQ(listed, accountFieldsInTransactionFormats());

    for (auto const& [field, key] : expected)
        EXPECT_EQ(accountFieldAttributeKey(*field).value_or(""), key) << field->getName();
}

TEST(TxAccountSpanNames, non_account_fields_have_no_key)
{
    EXPECT_EQ(accountFieldAttributeKey(sfFee), std::nullopt);
    EXPECT_EQ(accountFieldAttributeKey(sfSequence), std::nullopt);
    EXPECT_EQ(accountFieldAttributeKey(sfInvalid), std::nullopt);
}
