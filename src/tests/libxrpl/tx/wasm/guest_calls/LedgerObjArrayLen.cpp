#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// le_arr_len — a cache slot and a field code in, the count answered directly.
//
// Both are bare `i32`s on the wire, so the slot and the field code are values that cannot be
// confused for one another: a swap in `CxxHost`'s forward would otherwise be invisible.
struct LedgerObjArrayLenGuest : HostCallTest
{
    static constexpr std::int32_t kCount = 5;
    static constexpr std::int32_t kSlot = 7;
    static constexpr std::int32_t kNegativeSlot = -3;

    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);

    // A real field code, so the shim's `SField` lookup has something to find.
    static Arg
    field()
    {
        return Arg::scalar(sfBalance.getCode());
    }

    [[nodiscard]] static std::string
    watFor(Arg cacheIdxArg, Arg fieldArg)
    {
        return hostCallWat("le_arr_len", {cacheIdxArg, fieldArg});
    }
};

// The shim turns the guest's `i32` into the `SField` the C++ interface takes; asserting on
// the argument is what pins that translation rather than assuming it.
TEST_F(LedgerObjArrayLenGuest, SlotAndFieldCodeReachHostInOrder)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(kSlot, testing::Ref(sfBalance)))
        .WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(kCacheIdx, field())), kCount);
}

TEST_F(LedgerObjArrayLenGuest, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjArrayLen).Times(0);

    // A type nothing is registered under.
    auto const wat = watFor(kCacheIdx, Arg::scalar(0x7fff'0000));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

// The slot is the one scalar the ABI carries as signed, so a negative one reaches the host as
// itself and is the host's to refuse.
TEST_F(LedgerObjArrayLenGuest, NegativeSlotCrossesVerbatim)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(kNegativeSlot, testing::Ref(sfBalance)))
        .WillOnce(Return(std::unexpected(HostFunctionError::SlotOutRange)));

    auto const wat = watFor(Arg::scalar(kNegativeSlot), field());
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::SlotOutRange));
}

// `NoArray` is what a field that is not an array actually answers, so it stands for the host
// error axis here rather than an arbitrary code.
TEST_F(LedgerObjArrayLenGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(kSlot, testing::Ref(sfBalance)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(hostAnswer(watFor(kCacheIdx, field())), hfErrorToInt(HostFunctionError::NoArray));
}

// `guarded` turns the throw into `InternalFatal`, which the engine treats as fatal rather
// than passing back: the run ends, and the guest never resumes to read it.
TEST_F(LedgerObjArrayLenGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(kSlot, testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj array len came apart"}));

    auto const outcome = callHost(watFor(kCacheIdx, field()));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjArrayLen"));
}

}  // namespace xrpl::test
