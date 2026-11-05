#pragma once

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/WasmFixture.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>

// What a contract's `host_calls` tests share: the guest-side bytes each call carries.
//
// These modules hand-write the ABI — literal regions, literal lengths — so they hold the
// host to the wire rather than to whatever an SDK happens to send.

namespace xrpl::test {

struct ContractCallTest : MockVmTest
{
    // The module under test. One import, one entry point that calls it.
    [[nodiscard]] virtual std::string
    wat() const = 0;

    std::expected<EscrowResult, WasmTER>
    callHost(std::string_view entryPoint = escrowFunctionName)
    {
        return run(wat(), kAmpleGas, entryPoint);
    }

    // The contract's return value, which for these modules is what the host answered — or
    // its negative error code. Fails the test if the run did not complete.
    std::int32_t
    hostAnswer(std::string_view entryPoint = escrowFunctionName)
    {
        auto const outcome = callHost(entryPoint);
        if (!outcome)
        {
            ADD_FAILURE() << "the run did not complete: " << transToken(outcome.error().ter)
                          << "; logged: " << logged();
            return 0;
        }
        return outcome->result;
    }

    // A 20-byte account the guest holds as bytes. Distinctive at both ends, so a region read
    // at the wrong offset or the wrong length cannot match it.
    static AccountID
    account()
    {
        AccountID id;
        id.begin()[0] = 0xae;
        id.begin()[19] = 0xea;
        return id;
    }

    // Bytes as a WAT data-segment string.
    static std::string
    escaped(Bytes const& bytes)
    {
        std::string out;
        for (auto const byte : bytes)
            out += std::format("\\{:02x}", byte);
        return out;
    }

    static std::string
    escapedAccount()
    {
        auto const id = account();
        return escaped(Bytes{id.begin(), id.end()});
    }

    // The bytes a guest writes for a `set_data_*` value: its one-byte type, then its
    // serialization.
    static Bytes
    valueBytes(STJson::Value const& value)
    {
        Bytes wire{static_cast<std::uint8_t>(value->getSType())};
        Serializer s;
        value->add(s);
        wire.insert(wire.end(), s.peekData().begin(), s.peekData().end());
        return wire;
    }
};

}  // namespace xrpl::test
