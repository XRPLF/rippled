#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/tags.h>

#include <xrpl/json/json_value.h>

namespace xrpl::test::jtx {

/** Disable the regular key. */
json::Value
regkey(Account const& account, DisabledT);

/** Set a regular key. */
json::Value
regkey(Account const& account, Account const& signer);

}  // namespace xrpl::test::jtx
