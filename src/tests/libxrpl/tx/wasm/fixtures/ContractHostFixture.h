#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>
#include <memory>

// The GTest layer over `ContractLedger`, holding only what needs the framework. Everything
// that builds a ledger or a host is in `ContractLedger.h`, which the benchmarks can share.

namespace xrpl::test {

// A `ContractLedger` with GTest's lifecycle attached.
struct ContractHostFixture : testing::Test, ContractLedger
{
};

// -------------------------------------------------------------------------------------
// Contract data values.
//
// A value the contract stores is an `STJson::Value` — a `shared_ptr<STBase>` — and these
// build the ones a test compares against. The `SField` each carries does not travel with
// the value: `STJson` writes a value's type byte and its serialization, and reads it back
// under a field of its own choosing, so any field of the right type will do.
// -------------------------------------------------------------------------------------

inline STJson::Value
u8(std::uint8_t value)
{
    return std::make_shared<STUInt8>(sfCloseResolution, value);
}

inline STJson::Value
u16(std::uint16_t value)
{
    return std::make_shared<STUInt16>(sfTransferFee, value);
}

inline STJson::Value
u32(std::uint32_t value)
{
    return std::make_shared<STUInt32>(sfSequence, value);
}

inline STJson::Value
u64(std::uint64_t value)
{
    return std::make_shared<STUInt64>(sfIndexNext, value);
}

inline STJson::Value
acct(AccountID const& value)
{
    return std::make_shared<STAccount>(sfAccount, value);
}

// A value's canonical serialization, which is what a `get_data_*` call answers — the same
// bytes the guest wrote, without the type byte that preceded them.
inline Bytes
serialization(STJson::Value const& value)
{
    Serializer s;
    value->add(s);
    return Bytes{s.peekData().begin(), s.peekData().end()};
}

// The bytes a guest writes for `value`: its one-byte `SerializedTypeID`, then its
// serialization. The inverse of what `HostContext` decodes.
inline Bytes
valueWire(STJson::Value const& value)
{
    Bytes wire{static_cast<std::uint8_t>(value->getSType())};
    auto const bytes = serialization(value);
    wire.insert(wire.end(), bytes.begin(), bytes.end());
    return wire;
}

}  // namespace xrpl::test
