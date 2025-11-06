#ifndef XRPL_TEST_JTX_SIG_H_INCLUDED
#define XRPL_TEST_JTX_SIG_H_INCLUDED

#include <test/jtx/Env.h>

#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** Set the regular signature on a JTx.
    @note For multisign, use msig.
*/
class sig
{
private:
    bool manual_ = true;
    /** Alternative transaction object field in which to place the signature.
     *
     * subField is only supported if an account_ is provided as well.
     */
    SField const* const subField_ = nullptr;
    /** Account that will generate the signature.
     *
     * If not provided, no signature will be added by this helper. See also
     * Env::autofill_sig.
     */
    std::optional<Account> account_;
    /// Used solely as a convenience placeholder for ctors that do _not_ specify
    /// a subfield.
    static constexpr SField* const topLevel = nullptr;

public:
    explicit sig(autofill_t) : manual_(false)
    {
    }

    explicit sig(none_t)
    {
    }

    explicit sig(SField const* subField, Account const& account)
        : subField_(subField), account_(account)
    {
    }

    explicit sig(SField const& subField, Account const& account)
        : sig(&subField, account)
    {
    }

    explicit sig(Account const& account) : sig(topLevel, account)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
