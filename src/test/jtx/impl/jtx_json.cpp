#include <test/jtx/jtx_json.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/utility.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>

#include <string>
#include <utility>

namespace xrpl::test::jtx {

Json::Json(std::string const& s)
{
    if (!json::Reader().parse(s, jv_))
        Throw<ParseError>("bad json");
}

Json::Json(char const* s) : Json(std::string(s))
{
}

Json::Json(json::Value jv) : jv_(std::move(jv))
{
}

void
Json::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    for (auto iter = jv_.begin(); iter != jv_.end(); ++iter)
        jv[iter.key().asString()] = *iter;
}

}  // namespace xrpl::test::jtx
