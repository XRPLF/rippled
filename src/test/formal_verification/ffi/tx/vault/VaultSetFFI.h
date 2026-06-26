#pragma once

#include <test/formal_verification/ffi/LeanObjectFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/STNumberFFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>

extern "C" {
lean_object*
lean_vault_set_tx_build(
    lean_object* txID,
    lean_object* account,
    int64_t fee,
    uint32_t sequence,
    uint32_t flags,
    lean_object* vaultID,
    lean_object* data,
    lean_object* assetsMaximum,
    lean_object* domainID);
}

namespace xrpl::test::formal_verification {

// Build a ConcreteTx (VaultSet) in one call, ready to pass to processTx.
inline LeanObjectFFI
buildVaultSetTx(
    uint256 const& txID,
    AccountID const& account,
    int64_t fee,
    uint32_t sequence,
    uint32_t flags,
    uint256 const& vaultID,
    std::optional<Blob> const& data,
    std::optional<Number> const& assetsMaximum,
    std::optional<uint256> const& domainID)
{
    return LeanObjectFFI(lean_vault_set_tx_build(
        UInt256FFI::build(txID).give(),
        AccountIDFFI::build(account).give(),
        fee,
        sequence,
        flags,
        UInt256FFI::build(vaultID).give(),
        leanOptBytes(data),
        leanOptHandle<STNumberFFI>(assetsMaximum),
        leanOptHandle<UInt256FFI>(domainID)));
}

}  // namespace xrpl::test::formal_verification
