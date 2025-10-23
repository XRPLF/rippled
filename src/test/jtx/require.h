#ifndef XRPL_TEST_JTX_REQUIRE_H_INCLUDED
#define XRPL_TEST_JTX_REQUIRE_H_INCLUDED

#include <test/jtx/requires.h>

#include <functional>
#include <vector>

namespace ripple {

namespace detail {

template <class Cond, class... Args>
inline void
require_args(test::jtx::requires_t& vec, Cond const& cond, Args const&... args)
{
    vec.push_back(cond);
    if constexpr (sizeof...(args) > 0)
        require_args(vec, args...);
}

}  // namespace detail

namespace test {
namespace jtx {

/** Compose many condition functors into one */
template <class... Args>
require_t
required(Args const&... args)
{
    requires_t vec;
    detail::require_args(vec, args...);
    return [vec](Env& env) {
        for (auto const& f : vec)
            f(env);
    };
}

/** Check a set of conditions.

    The conditions are checked after a JTx is
    applied, and only if the resulting TER
    matches the expected TER.
*/
class require
{
private:
    require_t cond_;

public:
    template <class... Args>
    require(Args const&... args) : cond_(required(args...))
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.require.emplace_back(cond_);
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
