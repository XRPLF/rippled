#pragma once

#include <test/formal_verification/ffi/protocol/UInt256FFI.h>

#include <xrpl/basics/base_uint.h>

#include <lean/lean.h>

#include <cstdint>

extern "C" {
lean_object*
lean_ledger_header_build(uint32_t seq, uint32_t parentCloseTime, lean_object* parentHash);
uint32_t
lean_ledger_header_seq(lean_object* header);
uint32_t
lean_ledger_header_parent_close_time(lean_object* header);
lean_object*
lean_ledger_header_parent_hash(lean_object* header);
}

namespace xrpl::test::formal_verification {

class LedgerHeaderFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    static LedgerHeaderFFI
    build(uint32_t seq, uint32_t parentCloseTime, uint256 const& parentHash)
    {
        return LedgerHeaderFFI(leanCall(
            lean_ledger_header_build, seq, parentCloseTime, UInt256FFI::build(parentHash)));
    }

    uint32_t
    seq() const
    {
        return leanGet<uint32_t>(lean_ledger_header_seq);
    }
    uint32_t
    parentCloseTime() const
    {
        return leanGet<uint32_t>(lean_ledger_header_parent_close_time);
    }
    uint256
    parentHash() const
    {
        return leanGetObj<UInt256FFI>(lean_ledger_header_parent_hash);
    }
};

}  // namespace xrpl::test::formal_verification
