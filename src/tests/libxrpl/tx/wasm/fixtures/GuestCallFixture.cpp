#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl::test {

namespace {

using Kind = GuestCallTest::Arg::Kind;

// The wasm value types `args` lowers to, and the constants that spell it at the call site.
// Both switch over the same kinds in the same order, which is what keeps a module's declared
// signature and the arguments it passes from disagreeing. Neither carries a `default`, so a
// kind added to `Arg` is a `-Wswitch` warning in both rather than a silent `i32`.
std::string
paramList(std::vector<GuestCallTest::Arg> const& args)
{
    std::string params;
    for (auto const& arg : args)
    {
        switch (arg.kind)
        {
            case Kind::Scalar32:
                params += " i32";
                break;
            case Kind::Scalar64:
                params += " i64";
                break;
            case Kind::Region:
            case Kind::OutRegion:
                params += " i32 i32";
                break;
        }
    }
    return params;
}

std::string
argList(std::vector<GuestCallTest::Arg> const& args)
{
    std::string constants;
    for (auto const& arg : args)
    {
        switch (arg.kind)
        {
            case Kind::Scalar32:
                constants += std::format(" (i32.const {})", arg.value);
                break;
            case Kind::Scalar64:
                constants += std::format(" (i64.const {})", arg.value);
                break;
            case Kind::Region:
            case Kind::OutRegion:
                constants += std::format(" (i32.const {}) (i32.const {})", arg.value, arg.len);
                break;
        }
    }
    return constants;
}

std::string
dataSegments(std::vector<GuestCallTest::Memory> const& memory)
{
    std::string segments;
    for (auto const& seed : memory)
    {
        segments +=
            std::format("  (data (i32.const {}) \"{}\")\n", seed.at, watEscaped(seed.bytes));
    }
    return segments;
}

}  // namespace

std::string
GuestCallTest::hostCallWat(
    std::string_view importName,
    std::vector<Arg> const& args,
    std::vector<Memory> const& memory,
    Answer answer)
{
    auto const isOut = [](Arg const& arg) { return arg.kind == Arg::Kind::OutRegion; };
    auto const out = std::ranges::find_if(args, isOut);

    // Nothing else catches this: a second out region still fits the declared arity, and
    // only the first is read back.
    EXPECT_LE(std::ranges::count_if(args, isOut), 1)
        << "hostCallWat reads back one out region; write the module by hand for two";

    // `if` rather than `select`, which evaluates both arms: the load would trap for the
    // tests whose out pointer is deliberately out of bounds, and those runs are meant to
    // return the engine's code without reaching a load at all.
    auto const body = answer == Answer::Status || out == args.end()
        ? std::format("(call $f{})", argList(args))
        : std::format(
              R"wat((local $n i32)
    (local.set $n (call $f{0}))
    (if (result i32) (i32.lt_s (local.get $n) (i32.const 0))
      (then (local.get $n))
      (else (i32.load (i32.const {1})))))wat",
              argList(args),
              out->value);

    return std::format(
        R"wat(
(module
  (import "host_lib" "{0}" (func $f (param{1}) (result i32)))
  (memory (export "memory") 1)
{2}  (func (export "{3}") (result i32)
    {4}))
)wat",
        importName,
        paramList(args),
        dataSegments(memory),
        escrowFunctionName,
        body);
}

std::int32_t
GuestCallTest::hostAnswer(std::string_view wat)
{
    auto const outcome = run(wat);
    if (!outcome)
    {
        ADD_FAILURE() << "the run did not complete: " << transToken(outcome.error().ter)
                      << "; logged: " << logged();
        return 0;
    }
    return outcome->result;
}

}  // namespace xrpl::test
