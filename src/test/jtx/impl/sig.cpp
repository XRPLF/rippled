#include <test/jtx/sig.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/utility.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/Sign.h>

namespace xrpl::test::jtx {

void
Sig::operator()(Env&, JTx& jt) const
{
    if (!manual_)
        return;
    if (subField_ == nullptr)
        jt.fillSig = false;
    if (account_)
    {
        // VFALCO Inefficient pre-C++14
        auto const account = *account_;
        auto callback = [subField = subField_, account](Env& env, JTx& jtx) {
            // Where to put the signature. Supports sfCounterPartySignature and sfSponsorSignature.
            auto& sigObject = subField ? jtx[*subField] : jtx.jv;

            jtx::sign(
                jtx.jv,
                account,
                sigObject,
                signingPrefix(jtx::signatureRole(subField), false, env.current()->rules()));
        };
        if (subField_ == nullptr)
        {
            jt.mainSigners.emplace_back(callback);
        }
        else
        {
            jt.postSigners.emplace_back(callback);
        }
    }
}

}  // namespace xrpl::test::jtx
