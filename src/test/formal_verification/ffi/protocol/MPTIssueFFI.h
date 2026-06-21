#pragma once

#include <test/formal_verification/ffi/protocol/MptIdFFI.h>

#include <xrpl/protocol/MPTIssue.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_mpt_issue_build(lean_object* mptId);
lean_object*
lean_mpt_issue_mpt_id(lean_object* mptIssue);
}

namespace xrpl::test::formal_verification {

class MPTIssueFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = MPTIssue;

    static MPTIssueFFI
    build(MPTIssue const& m)
    {
        return MPTIssueFFI(leanCall(lean_mpt_issue_build, MptIdFFI::build(m.getMptID())));
    }

    MPTIssue
    read() const
    {
        return MPTIssue{leanGetObj<MptIdFFI>(lean_mpt_issue_mpt_id)};
    }
};

static_assert(LeanWrapper<MPTIssueFFI>);

}  // namespace xrpl::test::formal_verification
