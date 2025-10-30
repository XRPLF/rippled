#ifndef XRPL_TEST_JTX_MULTISIGN_H_INCLUDED
#define XRPL_TEST_JTX_MULTISIGN_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/SignerUtils.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>

#include <concepts>
#include <cstdint>
#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** A signer in a SignerList */
struct signer
{
    std::uint32_t weight;
    Account account;
    std::optional<uint256> tag;

    signer(
        Account account_,
        std::uint32_t weight_ = 1,
        std::optional<uint256> tag_ = std::nullopt)
        : weight(weight_), account(std::move(account_)), tag(std::move(tag_))
    {
    }
};

Json::Value
signers(
    Account const& account,
    std::uint32_t quorum,
    std::vector<signer> const& v);

/** Remove a signer list. */
Json::Value
signers(Account const& account, none_t);

//------------------------------------------------------------------------------

/** Set a multisignature on a JTx. */
class msig
{
public:
    std::vector<Reg> signers;
    /** Alternative transaction object field in which to place the signer list.
     *
     * subField is only supported if an account_ is provided as well.
     */
    SField const* const subField = nullptr;
    /// Used solely as a convenience placeholder for ctors that do _not_ specify
    /// a subfield.
    static constexpr SField* const topLevel = nullptr;

    msig(SField const* subField_, std::vector<Reg> signers_)
        : signers(std::move(signers_)), subField(subField_)
    {
        sortSigners(signers);
    }

    msig(SField const& subField_, std::vector<Reg> signers_)
        : msig{&subField_, signers_}
    {
    }

    msig(std::vector<Reg> signers_) : msig(topLevel, signers_)
    {
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit msig(SField const* subField_, AccountType&& a0, Accounts&&... aN)
        : msig{
              subField_,
              std::vector<Reg>{
                  std::forward<AccountType>(a0),
                  std::forward<Accounts>(aN)...}}
    {
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit msig(SField const& subField_, AccountType&& a0, Accounts&&... aN)
        : msig{
              &subField_,
              std::vector<Reg>{
                  std::forward<AccountType>(a0),
                  std::forward<Accounts>(aN)...}}
    {
    }

    template <class AccountType, class... Accounts>
        requires(
            std::convertible_to<AccountType, Reg> &&
            !std::is_same_v<AccountType, SField*>)
    explicit msig(AccountType&& a0, Accounts&&... aN)
        : msig{
              topLevel,
              std::vector<Reg>{
                  std::forward<AccountType>(a0),
                  std::forward<Accounts>(aN)...}}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

//------------------------------------------------------------------------------

/** The number of signer lists matches. */
using siglists = owner_count<ltSIGNER_LIST>;

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
