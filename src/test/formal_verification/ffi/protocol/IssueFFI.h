#pragma once

#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/CurrencyFFI.h>

#include <xrpl/protocol/Issue.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_issue_build(lean_object* currency, lean_object* account);
lean_object*
lean_issue_currency(lean_object* issue);
lean_object*
lean_issue_account(lean_object* issue);
}

namespace xrpl::test::formal_verification {

class IssueFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = Issue;

    static IssueFFI
    build(Issue const& i)
    {
        return IssueFFI(leanCall(
            lean_issue_build, CurrencyFFI::build(i.currency), AccountIDFFI::build(i.account)));
    }

    Issue
    read() const
    {
        Currency currency = leanGetObj<CurrencyFFI>(lean_issue_currency);
        AccountID account = leanGetObj<AccountIDFFI>(lean_issue_account);
        return Issue{currency, account};
    }
};

static_assert(LeanWrapper<IssueFFI>);

}  // namespace xrpl::test::formal_verification
