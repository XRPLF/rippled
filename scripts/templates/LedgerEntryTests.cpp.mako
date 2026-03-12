// Auto-generated unit tests for ledger entry ${name}
<%
    required_fields = [f for f in fields if f["requirement"] == "soeREQUIRED"]
    optional_fields = [f for f in fields if f["requirement"] != "soeREQUIRED"]

    def canonical_expr(field):
        return f"canonical_{field['stiSuffix']}()"

    # Pick a wrong ledger entry to test type mismatch
    # Use Ticket as it has minimal required fields (just Account)
    if name != "Ticket":
        wrong_le_include = "Ticket"
    else:
        wrong_le_include = "Check"
%>

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/${name}.h>
#include <xrpl/protocol_autogen/ledger_entries/${wrong_le_include}.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(${name}Tests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

% for field in fields:
    auto const ${field["paramName"]}Value = ${canonical_expr(field)};
% endfor

    ${name}Builder builder{
% for i, field in enumerate(required_fields):
        ${field["paramName"]}Value${"," if i < len(required_fields) - 1 else ""}
% endfor
    };

% for field in optional_fields:
    builder.set${field["name"][2:]}(${field["paramName"]}Value);
% endfor

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

% for field in required_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actual = entry.get${field["name"][2:]}();
        expectEqualField(expected, actual, "${field["name"]}");
    }

% endfor
% for field in optional_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actualOpt = entry.get${field["name"][2:]}();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "${field["name"]}");
        EXPECT_TRUE(entry.has${field["name"][2:]}());
    }

% endfor
    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(${name}Tests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

% for field in fields:
    auto const ${field["paramName"]}Value = ${canonical_expr(field)};
% endfor

    auto sle = std::make_shared<SLE>(${name}::entryType, index);

% for field in fields:
% if field.get("stiSuffix") == "ISSUE":
    sle->at(${field["name"]}) = STIssue(${field["name"]}, ${field["paramName"]}Value);
% elif field["typeData"].get("setter_use_brackets"):
    sle->at(${field["name"]}) = ${field["paramName"]}Value;
% else:
    sle->${field["typeData"]["setter_method"]}(${field["name"]}, ${field["paramName"]}Value);
% endif
% endfor

    ${name}Builder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    ${name} entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

% for field in required_fields:
    {
        auto const& expected = ${field["paramName"]}Value;

        auto const fromSle = entryFromSle.get${field["name"][2:]}();
        auto const fromBuilder = entryFromBuilder.get${field["name"][2:]}();

        expectEqualField(expected, fromSle, "${field["name"]}");
        expectEqualField(expected, fromBuilder, "${field["name"]}");
    }

% endfor
% for field in optional_fields:
    {
        auto const& expected = ${field["paramName"]}Value;

        auto const fromSleOpt = entryFromSle.get${field["name"][2:]}();
        auto const fromBuilderOpt = entryFromBuilder.get${field["name"][2:]}();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "${field["name"]}");
        expectEqualField(expected, *fromBuilderOpt, "${field["name"]}");
    }

% endfor
    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(${name}Tests, WrapperThrowsOnWrongEntryType)
{
    uint256 const index{3u};

    // Build a valid ledger entry of a different type
    // Ticket requires: Account, OwnerNode, TicketSequence, PreviousTxnID, PreviousTxnLgrSeq
    // Check requires: Account, Destination, SendMax, Sequence, OwnerNode, DestinationNode, PreviousTxnID, PreviousTxnLgrSeq
% if wrong_le_include == "Ticket":
    ${wrong_le_include}Builder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
% else:
    ${wrong_le_include}Builder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_ACCOUNT(),
        canonical_AMOUNT(),
        canonical_UINT32(),
        canonical_UINT64(),
        canonical_UINT64(),
        canonical_UINT256(),
        canonical_UINT32()};
% endif
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(${name}{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(${name}Tests, BuilderThrowsOnWrongEntryType)
{
    uint256 const index{4u};

    // Build a valid ledger entry of a different type
% if wrong_le_include == "Ticket":
    ${wrong_le_include}Builder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
% else:
    ${wrong_le_include}Builder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_ACCOUNT(),
        canonical_AMOUNT(),
        canonical_UINT32(),
        canonical_UINT64(),
        canonical_UINT64(),
        canonical_UINT256(),
        canonical_UINT32()};
% endif
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(${name}Builder{wrongEntry.getSle()}, std::runtime_error);
}

% if optional_fields:
// 5) Build with only required fields and verify optional fields return nullopt.
TEST(${name}Tests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

% for field in required_fields:
    auto const ${field["paramName"]}Value = ${canonical_expr(field)};
% endfor

    ${name}Builder builder{
% for i, field in enumerate(required_fields):
        ${field["paramName"]}Value${"," if i < len(required_fields) - 1 else ""}
% endfor
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
% for field in optional_fields:
    EXPECT_FALSE(entry.has${field["name"][2:]}());
    EXPECT_FALSE(entry.get${field["name"][2:]}().has_value());
% endfor
}
% endif
}
