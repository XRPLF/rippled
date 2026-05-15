#pragma once

#include <test/jtx/Env.h>

#include <xrpl/json/json_value.h>

namespace xrpl::test::jtx {

/** Inject raw JSON. */
class Json
{
private:
    ::json::Value jv_;

public:
    explicit Json(std::string const&);

    explicit Json(char const*);

    explicit Json(::json::Value);

    template <class T>
    Json(::json::StaticString const& key, T const& value)
    {
        jv_[key] = value;
    }

    template <class T>
    Json(std::string const& key, T const& value)
    {
        jv_[key] = value;
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
