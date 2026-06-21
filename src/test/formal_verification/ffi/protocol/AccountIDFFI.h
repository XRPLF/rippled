#pragma once

#include <test/formal_verification/ffi/LeanConvert.h>

#include <xrpl/protocol/AccountID.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_account_id_build(lean_object* bytes);
lean_object*
lean_account_id_bytes(lean_object* accountId);
}

namespace xrpl::test::formal_verification {

class AccountIDFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = AccountID;

    static AccountIDFFI
    build(AccountID const& x)
    {
        return AccountIDFFI(leanCall(lean_account_id_build, mkBytes(x)));
    }
    AccountID
    read() const
    {
        return fromBytes<AccountID>(leanGetBytes(lean_account_id_bytes));
    }
};

static_assert(LeanWrapper<AccountIDFFI>);

}  // namespace xrpl::test::formal_verification
