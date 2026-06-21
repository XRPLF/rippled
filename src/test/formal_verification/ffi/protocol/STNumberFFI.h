#pragma once

#include <test/formal_verification/ffi/protocol/NumberFFI.h>

#include <lean/lean.h>

#include <optional>

extern "C" {
lean_object*
lean_st_number_build(lean_object* number);
lean_object*
lean_st_number_value(lean_object* stNumber);
}

namespace xrpl::test::formal_verification {

class STNumberFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = Number;

    static STNumberFFI
    build(Number const& n)
    {
        return STNumberFFI(leanCall(lean_st_number_build, NumberFFI::build(n)));
    }
    Number
    read() const
    {
        return leanGetObj<NumberFFI>(lean_st_number_value);
    }
};

static_assert(LeanWrapper<STNumberFFI>);

}  // namespace xrpl::test::formal_verification
