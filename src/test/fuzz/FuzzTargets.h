#pragma once

#include <xrpl/basics/Blob.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace xrpl::fuzz {

/**
 * @brief One fuzz target: a named function that consumes an arbitrary byte buffer.
 *
 * A target returns 0 for every input it handles, including inputs the parser rejects.
 * It reports a bug by crashing (sanitizer report, XRPL_ASSERT abort) or by throwing
 * OracleFailure. The same function runs under libFuzzer (FuzzMain.cpp) and under the
 * replay unit test (Fuzz_test.cpp).
 *
 * Seeds are valid inputs built in code, so libFuzzer starts from inputs the parser
 * accepts. They are rebuilt on every run and never drift from the formats they exercise.
 */
struct FuzzTarget
{
    std::string_view name;
    int (*run)(std::uint8_t const* data, std::size_t size);
    std::vector<Blob> (*seeds)();
};

/**
 * @brief Every registered fuzz target, in a stable order.
 */
std::span<FuzzTarget const>
fuzzTargets();

/**
 * @brief Look up a target by name.
 *
 * @return The target, or nullptr if no target has that name.
 */
FuzzTarget const*
findFuzzTarget(std::string_view name);

int
fuzzSerialIter(std::uint8_t const* data, std::size_t size);

std::vector<Blob>
serialIterSeeds();

int
fuzzSTValidation(std::uint8_t const* data, std::size_t size);

std::vector<Blob>
stValidationSeeds();

}  // namespace xrpl::fuzz
