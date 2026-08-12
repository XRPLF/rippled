#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>
// For `TraceDataType`, which the bridge declares and this header defines.
#include <xrpl_wasm_vm_ffi_cxxbridge/lib.h>

#include <stdexcept>
#include <string_view>

namespace xrpl::test {

// `trace` returns `void` and answers the guest nothing in every case, success or failure alike.
// It is not wrapped in `guarded`: it has its own try/catch, so a throw is swallowed here rather
// than escaping a `noexcept` method. `host_calls/Trace.cpp` already renders all seven
// `TraceDataType`s through the engine; this file does not repeat that.
struct TraceDirectCall : HostContextTest
{
};

// One rendering, to show this layer forwards at all - every `TraceDataType` is
// `host_calls/Trace.cpp`'s job.
TEST_F(TraceDirectCall, MessageAndDataReachHostAsRenderedText)
{
    EXPECT_CALL(host, trace(std::string_view("note"), std::string_view("hi")));

    hostContext.trace("note", bytesOf(Bytes{'h', 'i'}), TraceDataType::AsText);
}

// The catch sits in `trace` itself, not in `guarded`. It logs at trace level, below the
// fixture's default threshold, so the threshold is lowered to observe it.
TEST_F(TraceDirectCall, HostExceptionIsSwallowedRatherThanEscaping)
{
    sink.threshold(beast::Severity::Trace);
    EXPECT_CALL(host, trace).WillOnce(testing::Throw(std::runtime_error{"trace sink came apart"}));

    hostContext.trace("note", bytesOf(Bytes{'h', 'i'}), TraceDataType::AsText);

    EXPECT_THAT(logged(), testing::HasSubstr("trace sink came apart"));
}

// The cap is on message and data together, not on data alone.
TEST_F(TraceDirectCall, MessagePlusDataPastCapIsDroppedWithoutAskingHost)
{
    Bytes const data(kMaxWasmDataLength, 0x41);
    EXPECT_CALL(host, trace).Times(0);

    hostContext.trace("x", bytesOf(data), TraceDataType::AsText);
}

}  // namespace xrpl::test
