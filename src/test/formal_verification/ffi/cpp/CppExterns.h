#pragma once

#include <lean/lean.h>

#include <cstdint>

// C++ crypto primitives the Lean model imports via @[extern]. Inputs are borrowed (@&).
extern "C" {
lean_object*
cpp_sha_512_half(lean_object* bytes);
lean_object*
cpp_pseudo_account_address_hash(uint16_t i, lean_object* parentHash, lean_object* pseudoOwnerKey);
}
