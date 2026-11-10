// libFuzzer entry point for the xrpl_fuzz binary. Built only with -Dfuzzing=ON and kept out
// of xrpld (see cmake/XrplCore.cmake), because libFuzzer supplies main().

#include <test/fuzz/FuzzCheck.h>
#include <test/fuzz/FuzzTargets.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
xrpl::fuzz::FuzzTarget const* gTarget = nullptr;

}  // namespace

extern "C" int
LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/)
{
    char const* const name = std::getenv("XRPL_FUZZ_TARGET");  // NOLINT(concurrency-mt-unsafe)
    if (name != nullptr)
        gTarget = xrpl::fuzz::findFuzzTarget(name);

    if (gTarget == nullptr)
    {
        std::cerr << "Set XRPL_FUZZ_TARGET to one of:\n";
        for (auto const& t : xrpl::fuzz::fuzzTargets())
            std::cerr << "  " << t.name << '\n';
        std::exit(EXIT_FAILURE);  // NOLINT(concurrency-mt-unsafe)
    }
    return 0;
}

extern "C" int
LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
    try
    {
        return gTarget->run(data, size);
    }
    catch (xrpl::fuzz::OracleFailure const& f)
    {
        // Abort so libFuzzer records the input as a crash.
        std::cerr << "fuzz oracle failure in " << f.target << ": " << f.what << '\n';
        std::abort();
    }
}
