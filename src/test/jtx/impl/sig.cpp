#include <test/jtx/sig.h>
#include <test/jtx/utility.h>

namespace ripple {
namespace test {
namespace jtx {

void
sig::operator()(Env&, JTx& jt) const
{
    if (!manual_)
        return;
    if (!subField_)
        jt.fill_sig = false;
    if (account_)
    {
        // VFALCO Inefficient pre-C++14
        auto const account = *account_;
        auto callback = [subField = subField_, account](Env&, JTx& jtx) {
            // Where to put the signature. Supports sfCounterPartySignature.
            auto& sigObject = subField ? jtx[*subField] : jtx.jv;

            jtx::sign(jtx.jv, account, sigObject);
        };
        if (!subField_)
            jt.mainSigners.emplace_back(callback);
        else
            jt.postSigners.emplace_back(callback);
    }
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
