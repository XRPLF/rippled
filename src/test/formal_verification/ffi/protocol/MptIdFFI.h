#pragma once

#include <test/formal_verification/ffi/LeanConvert.h>

#include <xrpl/protocol/MPTIssue.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_mpt_id_build(lean_object* bytes);
lean_object*
lean_mpt_id_bytes(lean_object* mptId);
}

namespace xrpl::test::formal_verification {

class MptIdFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = MPTID;

    static MptIdFFI
    build(MPTID const& x)
    {
        return MptIdFFI(leanCall(lean_mpt_id_build, mkBytes(x)));
    }
    MPTID
    read() const
    {
        return fromBytes<MPTID>(leanGetBytes(lean_mpt_id_bytes));
    }
};

static_assert(LeanWrapper<MptIdFFI>);

}  // namespace xrpl::test::formal_verification
