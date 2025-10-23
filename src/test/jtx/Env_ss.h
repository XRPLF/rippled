#ifndef XRPL_TEST_JTX_ENV_SS_H_INCLUDED
#define XRPL_TEST_JTX_ENV_SS_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
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

        SignSubmitRunner(Env& env, JTx&& jt) : env_(env), jt_(jt)
        {
        }

        void
        operator()(Json::Value const& params = Json::nullValue)
        {
            env_.sign_and_submit(jt_, params);
        }

    private:
        Env& env_;
        JTx const jt_;
    };

public:
    Env_ss(Env_ss const&) = delete;
    Env_ss&
    operator=(Env_ss const&) = delete;

    Env_ss(Env& env) : env_(env)
    {
    }

    template <class JsonValue, class... FN>
    SignSubmitRunner
    operator()(JsonValue&& jv, FN const&... fN)
    {
        auto jtx = env_.jt(std::forward<JsonValue>(jv), fN...);
        return SignSubmitRunner(env_, std::move(jtx));
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
