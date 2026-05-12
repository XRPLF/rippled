#pragma once

#include <test/jtx/Env.h>

namespace xrpl::test::jtx {

/** A transaction testing environment wrapper.
    Transactions submitted in sign-and-submit mode
    by default.
*/
class EnvSs
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
            : env_(env), jt_(std::move(jt)), loc_(loc)
        {
        }

        void
        operator()(json::Value const& params = json::ValueType::Null)
        {
            env_.signAndSubmit(jt_, params, loc_);
        }

    private:
        Env& env_;
        JTx const jt_;
        std::source_location const loc_;
    };

public:
    EnvSs(EnvSs const&) = delete;
    EnvSs&
    operator=(EnvSs const&) = delete;

    EnvSs(Env& env) : env_(env)
    {
    }

    template <class... FN>
    SignSubmitRunner
    operator()(WithSourceLocation<json::Value> jv, FN const&... fN)
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

}  // namespace xrpl::test::jtx
