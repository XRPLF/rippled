#pragma once

#include <test/formal_verification/ffi/LeanObjectFFI.h>

#include <lean/lean.h>

#include <cstdint>

extern "C" {
lean_object*
lean_rules_empty(lean_object* unit);
lean_object*
lean_rules_all(lean_object* unit);
lean_object*
lean_rules_enable(lean_object* rules, uint8_t code);
}

namespace xrpl::test::formal_verification {

inline LeanObjectFFI
rulesAll()
{
    return LeanObjectFFI(lean_rules_all(lean_box(0)));
}
inline LeanObjectFFI
rulesEmpty()
{
    return LeanObjectFFI(lean_rules_empty(lean_box(0)));
}

}  // namespace xrpl::test::formal_verification
