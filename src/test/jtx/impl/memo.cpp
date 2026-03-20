#include <test/jtx/memo.h>

namespace xrpl {
namespace test {
namespace jtx {

void
Memo::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoData"] = strHex(data_);
    m["MemoFormat"] = strHex(format_);
    m["MemoType"] = strHex(type_);
}

void
MemoData::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoData"] = strHex(s_);
}

void
MemoFormat::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoFormat"] = strHex(s_);
}

void
MemoType::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoType"] = strHex(s_);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
