#include <test/jtx/check.h>

#include <test/jtx/Account.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx::check {

// Cash a check requiring that a specific amount be delivered.
json::Value
cash(jtx::Account const& dest, uint256 const& checkId, STAmount const& amount)
{
    json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfAmount.jsonName] = amount.getJson(JsonOptions::Values::None);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    return jv;
}

// Cash a check requiring that at least a minimum amount be delivered.
json::Value
cash(jtx::Account const& dest, uint256 const& checkId, DeliverMin const& atLeast)
{
    json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfDeliverMin.jsonName] = atLeast.value.getJson(JsonOptions::Values::None);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    return jv;
}

// Cancel a check.
json::Value
cancel(jtx::Account const& dest, uint256 const& checkId)
{
    json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCancel;
    return jv;
}

}  // namespace xrpl::test::jtx::check
