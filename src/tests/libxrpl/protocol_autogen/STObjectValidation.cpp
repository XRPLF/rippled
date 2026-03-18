#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol_autogen/STObjectValidation.h>

#include <gtest/gtest.h>

namespace xrpl {
TEST(STObjectValidation, validate_required_field)
{
    SOTemplate format{{sfFlags, soeREQUIRED}};
    STObject obj(sfGeneric);
    obj.setFieldU32(sfFlags, 0);
    EXPECT_TRUE(protocol_autogen::validateSTObject(obj, format));
}

TEST(STObjectValidation, validate_missing_required_field)
{
    SOTemplate format{{sfFlags, soeREQUIRED}};
    STObject obj(sfGeneric);
    EXPECT_FALSE(protocol_autogen::validateSTObject(obj, format));
}

TEST(STObjectValidation, validate_optional_field)
{
    SOTemplate format{{sfFlags, soeOPTIONAL}};
    STObject obj(sfGeneric);
    obj.setFieldU32(sfFlags, 0);
    EXPECT_TRUE(protocol_autogen::validateSTObject(obj, format));
}

TEST(STObjectValidation, validate_missing_optional_field)
{
    SOTemplate format{{sfFlags, soeOPTIONAL}};
    STObject obj(sfGeneric);
    EXPECT_TRUE(protocol_autogen::validateSTObject(obj, format));
}

TEST(STObjectValidation, validate_mpt_amount_supported)
{
    SOTemplate format{{sfAmount, soeREQUIRED, soeMPTSupported}};
    STObject obj(sfGeneric);
    obj.setFieldAmount(sfAmount, STAmount{MPTAmount{Number{1}}, MPTIssue{}});
    EXPECT_TRUE(protocol_autogen::validateSTObject(obj, format));
}

TEST(STObjectValidation, validate_mpt_amount_not_supported)
{
    SOTemplate format{{sfAmount, soeREQUIRED, soeMPTNotSupported}};
    STObject obj(sfGeneric);
    obj.setFieldAmount(sfAmount, STAmount{MPTAmount{Number{1}}, MPTIssue{}});
    EXPECT_FALSE(protocol_autogen::validateSTObject(obj, format));
}

TEST(STObjectValidation, validate_mpt_issue_supported)
{
    SOTemplate format{{sfAsset, soeREQUIRED, soeMPTSupported}};
    STObject obj(sfGeneric);
    obj.setFieldIssue(sfAsset, STIssue{sfAsset, MPTIssue{}});
    EXPECT_TRUE(protocol_autogen::validateSTObject(obj, format));
}

TEST(STObjectValidation, validate_mpt_issue_not_supported)
{
    SOTemplate format{{sfAsset, soeREQUIRED, soeMPTNotSupported}};
    STObject obj(sfGeneric);
    obj.setFieldIssue(sfAsset, STIssue{sfAsset, MPTIssue{}});
    EXPECT_FALSE(protocol_autogen::validateSTObject(obj, format));
}
}  // namespace xrpl
