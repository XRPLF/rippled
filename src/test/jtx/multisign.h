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

/** A Signer in a SignerList */
struct Signer
{
    std::uint32_t weight;
    Account account;
    std::optional<uint256> tag;

    Signer(Account acct, std::uint32_t wt = 1, std::optional<uint256> t = std::nullopt)
        : weight(wt), account(std::move(acct)), tag(std::move(t))
    {
    }
};

Json::Value
signers(Account const& account, std::uint32_t quorum, std::vector<Signer> const& v);

/** Remove a Signer list. */
Json::Value
signers(Account const& account, NoneT);

//------------------------------------------------------------------------------

/** Set a multisignature on a JTx. */
class Msig
{
public:
    std::vector<Reg> signers;
    /** Alternative transaction object field in which to place the Signer list.
     *
     * subField is only supported if an account_ is provided as well.
     */
    SField const* const subField = nullptr;
    /// Used solely as a convenience placeholder for ctors that do _not_ specify
    /// a subfield.
    static constexpr SField* const kTOP_LEVEL = nullptr;

    Msig(SField const* sf, std::vector<Reg> s) : signers(std::move(s)), subField(sf)
    {
        sortSigners(signers);
    }

    Msig(SField const& sf, std::vector<Reg> s) : Msig{&sf, s}
    {
    }

    Msig(std::vector<Reg> s) : Msig(kTOP_LEVEL, s)
    {
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit Msig(SField const* sf, AccountType&& a0, Accounts&&... aN)
        : Msig{sf, std::vector<Reg>{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}}
    {
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit Msig(SField const& sf, AccountType&& a0, Accounts&&... aN)
        : Msig{&sf, std::vector<Reg>{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}}
    {
    }

    template <class AccountType, class... Accounts>
        requires(std::convertible_to<AccountType, Reg> && !std::is_same_v<AccountType, SField*>)
    explicit Msig(AccountType&& a0, Accounts&&... aN)
        : Msig{
              kTOP_LEVEL,
              std::vector<Reg>{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

//------------------------------------------------------------------------------

/** The number of Signer lists matches. */
using siglists = OwnerCount<ltSIGNER_LIST>;

}  // namespace xrpl::test::jtx
