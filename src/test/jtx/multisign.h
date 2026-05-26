#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/SignerUtils.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>

#include <concepts>
#include <cstdint>
#include <optional>

namespace xrpl::test::jtx {

/** A signer in a SignerList */
struct Signer
{
    std::uint32_t weight;
    Account account;
    std::optional<uint256> tag;

    Signer(Account account, std::uint32_t weight = 1, std::optional<uint256> tag = std::nullopt)
        : weight(weight), account(std::move(account)), tag(tag)
    {
    }
};

json::Value
signers(Account const& account, std::uint32_t quorum, std::vector<Signer> const& v);

/** Remove a signer list. */
json::Value
signers(Account const& account, NoneT);

//------------------------------------------------------------------------------

/** Set a multisignature on a JTx. */
class Msig
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
    static constexpr SField const* kTopLevel = nullptr;

    Msig(SField const* subField, std::vector<Reg> signers)
        : signers(std::move(signers)), subField(subField)
    {
        sortSigners(this->signers);
    }

    Msig(SField const& subField, std::vector<Reg> signers) : Msig{&subField, signers}
    {
    }

    Msig(std::vector<Reg> signers) : Msig(kTopLevel, signers)
    {
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit Msig(SField const* subField, AccountType&& a0, Accounts&&... aN)
        : Msig{
              subField,
              std::vector<Reg>{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}}
    {
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit Msig(SField const& subField, AccountType&& a0, Accounts&&... aN)
        : Msig{
              &subField,
              std::vector<Reg>{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}}
    {
    }

    template <class AccountType, class... Accounts>
        requires(std::convertible_to<AccountType, Reg> && !std::is_same_v<AccountType, SField*>)
    explicit Msig(AccountType&& a0, Accounts&&... aN)
        : Msig{
              kTopLevel,
              std::vector<Reg>{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

//------------------------------------------------------------------------------

/** The number of signer lists matches. */
using siglists = OwnerCount<ltSIGNER_LIST>;

}  // namespace xrpl::test::jtx
