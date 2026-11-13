// libFuzzer entry point for the xrpl_fuzz binary. Built only with -Dfuzzing=ON and kept out
// of xrpld (see cmake/XrplCore.cmake), because libFuzzer supplies main().

#include <test/fuzz/FuzzCheck.h>
#include <test/fuzz/FuzzTargets.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
xrpl::fuzz::FuzzTarget const* gTarget = nullptr;

// Write the target's seeds into dir, so libFuzzer loads them when it reads that corpus.
void
writeSeeds(xrpl::fuzz::FuzzTarget const& target, std::filesystem::path const& dir)
{
    std::filesystem::create_directories(dir);
    auto const seeds = target.seeds();
    for (std::size_t i = 0; i < seeds.size(); ++i)
    {
        auto const path = dir / ("seed-" + std::to_string(i));
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(
            reinterpret_cast<char const*>(seeds[i].data()),
            static_cast<std::streamsize>(seeds[i].size()));
        if (!out)
        {
            std::cerr << "cannot write seed " << path << '\n';
            std::exit(EXIT_FAILURE);  // NOLINT(concurrency-mt-unsafe)
        }
    }
}

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

    // libFuzzer reads its corpus directories after this function returns.
    if (char const* const dir = std::getenv("XRPL_FUZZ_SEED_DIR"))  // NOLINT(concurrency-mt-unsafe)
        writeSeeds(*gTarget, dir);
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
