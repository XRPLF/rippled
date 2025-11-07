#include <test/jtx/jtx_json.h>
#include <test/jtx/utility.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_reader.h>

namespace ripple {
namespace test {
namespace jtx {

json::json(std::string const& s)
{
    if (!Json::Reader().parse(s, jv_))
        Throw<parse_error>("bad json");
}

json::json(char const* s) : json(std::string(s))
{
}

json::json(Json::Value jv) : jv_(std::move(jv))
{
}

void
json::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    for (auto iter = jv_.begin(); iter != jv_.end(); ++iter)
        jv[iter.key().asString()] = *iter;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
