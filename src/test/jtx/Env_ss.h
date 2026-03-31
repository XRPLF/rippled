#pragma once

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

/** A transaction testing environment wrapper.
    Transactions submitted in sign-and-submit mode
    by default.
*/
class Env_ss
{
private:
    Env& env_;

private:
    class SignSubmitRunner
    {
    public:
        SignSubmitRunner(SignSubmitRunner&&) = default;
        SignSubmitRunner&
        operator=(SignSubmitRunner&&) = delete;

        SignSubmitRunner(Env& env, JTx&& jt, std::source_location loc)
            : env_(env), jt_(jt), loc_(loc)
        {
        }

        void
        operator()(Json::Value const& params = Json::nullValue)
        {
            env_.sign_and_submit(jt_, params, loc_);
        }

    private:
        Env& env_;
        JTx const jt_;
        std::source_location const loc_;
    };

public:
    Env_ss(Env_ss const&) = delete;
    Env_ss&
    operator=(Env_ss const&) = delete;

    Env_ss(Env& env) : env_(env)
    {
    }

    template <class... FN>
    SignSubmitRunner
    operator()(WithSourceLocation<Json::Value> jv, FN const&... fN)
    {
        auto jtx = env_.jt(std::move(jv.value), fN...);
        return SignSubmitRunner(env_, std::move(jtx), jv.loc);
    }

    template <class... FN>
    SignSubmitRunner
    operator()(WithSourceLocation<JTx> jv, FN const&... fN)
    {
        auto jtx = env_.jt(std::move(jv.value), fN...);
        return SignSubmitRunner(env_, std::move(jtx), jv.loc);
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
