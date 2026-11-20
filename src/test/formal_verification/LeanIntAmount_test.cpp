#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/XRPAmount.h>

#include <lean/lean.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>

// Compiled from formal_verification/ and linked into xrpld.
extern "C" {
void
lean_initialize_runtime_module();
lean_object*
initialize_XRPL_XRPL_FFI_FFI(std::uint8_t builtin);
// Lean passes an Int64 as its bits in a uint64_t.
std::uint64_t
lean_int_amount_signum(std::uint64_t value);
}

namespace xrpl::test {

// Checks that the Lean model of XRPAmount::signum agrees with the C++.
class LeanIntAmount_test : public beast::unit_test::Suite
{
    // The Lean runtime and the model's modules must be initialized once
    // before any Lean function is called.
    static bool
    initializeLean()
    {
        lean_initialize_runtime_module();
        lean_object* result = initialize_XRPL_XRPL_FFI_FFI(1);
        bool const ok = lean_io_result_is_ok(result);
        if (!ok)
            lean_io_result_show_error(result);
        lean_dec(result);
        if (ok)
            lean_io_mark_end_initialization();
        return ok;
    }

    void
    checkSignum(std::int64_t drops)
    {
        auto const lean =
            static_cast<std::int64_t>(lean_int_amount_signum(static_cast<std::uint64_t>(drops)));
        BEAST_EXPECT(lean == XRPAmount{drops}.signum());
    }

    void
    testSignum()
    {
        testcase("signum");

        using Limits = std::numeric_limits<std::int64_t>;
        for (std::int64_t const drops :
             std::initializer_list<std::int64_t>{Limits::min(), -1, 0, 1, Limits::max()})
            checkSignum(drops);

        // Fixed seed, so a failure is reproducible.
        std::mt19937_64 rng{0};
        std::uniform_int_distribution<std::int64_t> dist{Limits::min(), Limits::max()};
        for (int i = 0; i < 1000; ++i)
            checkSignum(dist(rng));
    }

public:
    void
    run() override
    {
        if (!BEAST_EXPECT(initializeLean()))
            return;
        testSignum();
    }
};

BEAST_DEFINE_TESTSUITE(LeanIntAmount, formal_verification, xrpl);

}  // namespace xrpl::test
