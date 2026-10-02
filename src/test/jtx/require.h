#pragma once

#include <test/jtx/JTx.h>
#include <test/jtx/requires.h>

#include <functional>
#include <vector>

namespace xrpl {

namespace detail {

template <class Cond, class... Args>
inline void
requireArgs(test::jtx::RequiresT& vec, Cond const& cond, Args const&... args)
{
    vec.push_back(cond);
    if constexpr (sizeof...(args) > 0)
        requireArgs(vec, args...);
}

}  // namespace detail

namespace test::jtx {

/**
 * Compose many condition functors into one
 */
template <class... Args>
RequireT
required(Args const&... args)
{
    RequiresT vec;
    detail::requireArgs(vec, args...);
    return [vec](Env& env) {
        for (auto const& f : vec)
            f(env);
    };
}

/**
 * Check a set of conditions.
 *
 * The conditions are checked after a JTx is
 * applied, and only if the resulting TER
 * matches the expected TER.
 */
class Require
{
private:
    RequireT cond_;

public:
    template <class... Args>
    Require(Args const&... args) : cond_(required(args...))
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.require.emplace_back(cond_);
    }
};

}  // namespace test::jtx

}  // namespace xrpl
