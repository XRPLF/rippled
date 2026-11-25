#pragma once

#include <xrpl/protocol/Serializer.h>

#include <exception>
#include <memory>
#include <string>

namespace xrpl::fuzz {

/**
 * @brief A fuzz oracle found an input that breaks a property of the code under test.
 *
 * Deliberately not derived from std::exception, so the catch blocks that swallow
 * ordinary parse errors in a target cannot swallow it.
 */
struct OracleFailure
{
    std::string target;
    std::string what;
};

/**
 * @brief Throw OracleFailure unless @p cond holds.
 */
inline void
check(bool cond, char const* target, char const* what)
{
    if (!cond)
        throw OracleFailure{.target = target, .what = what};
}

/**
 * @brief Call @p fn; if it throws std::exception, return a value-initialized result instead.
 *
 * Rejecting malformed input by throwing is the expected behaviour of the code under test,
 * so targets use this to turn a parse error into "nothing to check". OracleFailure is not
 * a std::exception and passes through.
 */
template <class Fn>
auto
tryOrEmpty(Fn&& fn) -> decltype(fn())
{
    try
    {
        return fn();
    }
    catch (std::exception const&)
    {
        return {};
    }
}

/**
 * @brief Check that serialization reaches a fixed point after one parse.
 *
 * The parser accepts fields in any order and re-serialization sorts them, so the
 * original bytes need not equal ser(parse(bytes)). The property checked here is that
 * ser(parse(ser(parse(bytes)))) == ser(parse(bytes)): the object's own serialization
 * parses, and parsing it changes nothing.
 *
 * @param first The object parsed from the fuzz input.
 * @param parse Builds a std::shared_ptr<T> from a SerialIter.
 * @param target Target name reported in an OracleFailure.
 * @return The object re-parsed from first's serialization.
 */
template <class T, class Parse>
std::shared_ptr<T const>
checkSerializationFixedPoint(T const& first, Parse&& parse, char const* target)
{
    auto const s1 = first.getSerializer();
    std::shared_ptr<T const> const second = tryOrEmpty([&] {
        SerialIter sit{s1.slice()};
        return std::shared_ptr<T const>{parse(sit)};
    });
    check(second != nullptr, target, "own serialization does not parse");
    check(
        second->getSerializer().slice() == s1.slice(),
        target,
        "serialization is not a fixed point");
    return second;
}

}  // namespace xrpl::fuzz
