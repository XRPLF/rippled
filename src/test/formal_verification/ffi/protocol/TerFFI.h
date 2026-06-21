#pragma once

#include <xrpl/protocol/TER.h>

#include <lean/lean.h>

#include <cstdint>
#include <string>

namespace xrpl::test::formal_verification {

// The Lean `TER` inductive in constructor order (XRPL/TER.lean). This single
// list generates the enum (value = lean_obj_tag), the name lookup, and the
// rippled-TER -> tag map, so they can never drift apart.
// clang-format off
#define XRPL_LEAN_TER_CODES(X)  \
    X(tesSUCCESS)               \
    X(tecINTERNAL)              \
    X(tecAMM_INVALID_TOKENS)    \
    X(tecAMM_FAILED)            \
    X(tecUNFUNDED_AMM)          \
    X(tecNO_ENTRY)              \
    X(tecWRONG_ASSET)           \
    X(tecOBJECT_NOT_FOUND)      \
    X(tecNO_AUTH)               \
    X(tecINSUFFICIENT_RESERVE)  \
    X(tecDIR_FULL)              \
    X(tecNO_TARGET)             \
    X(tecEXPIRED)               \
    X(tecNO_PERMISSION)         \
    X(tecDUPLICATE)             \
    X(tecNO_LINE_INSUF_RESERVE) \
    X(tecHAS_OBLIGATIONS)       \
    X(tecNO_DST)                \
    X(tecDST_TAG_NEEDED)        \
    X(tecNO_LINE)               \
    X(tecFAILED_PROCESSING)     \
    X(tecPATH_DRY)              \
    X(tecINSUFFICIENT_FUNDS)    \
    X(tefINTERNAL)              \
    X(tefBAD_LEDGER)            \
    X(terNO_ACCOUNT)            \
    X(terNO_RIPPLE)             \
    X(tecFROZEN)                \
    X(tecLOCKED)                \
    X(temMALFORMED)             \
    X(temBAD_AMOUNT)            \
    X(telFAILED_PROCESSING)
// clang-format on

enum class Ter : uint8_t {
#define X(name) name,
    XRPL_LEAN_TER_CODES(X)
#undef X
};

// Name of a Lean TER tag, for failure messages.
inline std::string
terName(uint8_t code)
{
    switch (static_cast<Ter>(code))
    {
#define X(name)     \
    case Ter::name: \
        return #name;
        XRPL_LEAN_TER_CODES(X)
#undef X
    }
    return "TER(" + std::to_string(code) + ")";
}

// Map a rippled TER to the Lean tag; 0xFF if it isn't one the Lean side models.
inline uint8_t
cppTerByte(TER ter)
{
#define X(name)      \
    if (ter == name) \
        return static_cast<uint8_t>(Ter::name);
    XRPL_LEAN_TER_CODES(X)
#undef X
    return 0xFF;
}

// A Lean TER value's constructor tag (the ordinal that matches `Ter`).
inline uint8_t
terTag(lean_object* ter)
{
    return static_cast<uint8_t>(lean_obj_tag(ter));
}

#undef XRPL_LEAN_TER_CODES

}  // namespace xrpl::test::formal_verification
