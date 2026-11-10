#include <test/fuzz/FuzzCheck.h>
#include <test/fuzz/FuzzTargets.h>

#include <xrpl/beast/unit_test/suite.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#ifndef XRPL_FUZZ_CORPUS_DIR
#error "XRPL_FUZZ_CORPUS_DIR must name the src/test/fuzz/corpus directory"
#endif

namespace xrpl::fuzz {

/**
 * @brief Replay the checked-in fuzz corpus through every fuzz target.
 *
 * Each target's corpus directory holds seed inputs and minimized inputs from past
 * findings. Replaying them in the normal unit-test run keeps fixed bugs fixed on every
 * compiler and platform, without libFuzzer.
 */
class FuzzCorpus_test : public beast::unit_test::Suite
{
    static std::vector<std::uint8_t>
    readFile(std::filesystem::path const& path)
    {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    void
    replay(FuzzTarget const& target)
    {
        testcase(std::string(target.name));

        auto const dir = std::filesystem::path(XRPL_FUZZ_CORPUS_DIR) / target.name;
        if (!BEAST_EXPECTS(std::filesystem::is_directory(dir), "missing corpus " + dir.string()))
            return;

        std::size_t replayed = 0;
        for (auto const& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;

            auto const input = readFile(entry.path());
            try
            {
                BEAST_EXPECT(target.run(input.data(), input.size()) == 0);
            }
            catch (OracleFailure const& f)
            {
                fail(entry.path().filename().string() + ": " + f.what);
            }
            ++replayed;
        }
        BEAST_EXPECTS(replayed > 0, "empty corpus " + dir.string());
    }

public:
    void
    run() override
    {
        for (auto const& target : fuzzTargets())
            replay(target);
    }
};

BEAST_DEFINE_TESTSUITE(FuzzCorpus, fuzz, xrpl);

}  // namespace xrpl::fuzz
