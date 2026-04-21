#include <test/jtx/check.h>

#include <test/jtx/Account.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx::check {

// Cash a check requiring that a specific amount be delivered.
Json::Value
cash(jtx::Account const& dest, uint256 const& checkId, STAmount const& amount)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfAmount.jsonName] = amount.getJson(JsonOptions::none);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    return jv;
}

// Cash a check requiring that at least a minimum amount be delivered.
Json::Value
cash(jtx::Account const& dest, uint256 const& checkId, DeliverMin const& atLeast)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfDeliverMin.jsonName] = atLeast.value.getJson(JsonOptions::none);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    return jv;
}

// Cancel a check.
Json::Value
cancel(jtx::Account const& dest, uint256 const& checkId)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCancel;
    return jv;
}

}  // namespace xrpl::test::jtx::check
