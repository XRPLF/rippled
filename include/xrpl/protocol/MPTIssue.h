#pragma once

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/* Adapt MPTID to provide the same interface as Issue. Enables using static
 * polymorphism by Asset and other classes. MPTID is a 192-bit concatenation
 * of a 32-bit account sequence and a 160-bit account id.
 */
class MPTIssue
{
private:
    MPTID mptID_;

public:
    MPTIssue() = default;

    MPTIssue(MPTID const& issuanceID);

    MPTIssue(std::uint32_t sequence, AccountID const& account);

    operator MPTID const&() const
    {
        return mptID_;
    }

    AccountID const&
    getIssuer() const;

    constexpr MPTID const&
    getMptID() const
    {
        return mptID_;
    }

    std::string
    getText() const;

    void
    setJson(Json::Value& jv) const;

    friend constexpr bool
    operator==(MPTIssue const& lhs, MPTIssue const& rhs);

    friend constexpr std::weak_ordering
    operator<=>(MPTIssue const& lhs, MPTIssue const& rhs);

    bool
    native() const
    {
        return false;
    }

    bool
    integral() const
    {
        return true;
    }
};

constexpr bool
operator==(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return lhs.mptID_ == rhs.mptID_;
}

constexpr std::weak_ordering
operator<=>(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return lhs.mptID_ <=> rhs.mptID_;
}

/** MPT is a non-native token.
 */
inline bool
isXRP(MPTID const&)
{
    return false;
}

inline AccountID
getMPTIssuer(MPTID const& mptid)
{
    static_assert(sizeof(MPTID) == (sizeof(std::uint32_t) + sizeof(AccountID)));
    // Extract the 20 bytes for the AccountID
    std::array<std::uint8_t, sizeof(AccountID)> bytes{};
    std::copy_n(mptid.data() + sizeof(std::uint32_t), sizeof(AccountID), bytes.begin());

    // bit_cast is a "magic" compiler intrinsic that is
    // usually optimized away to nothing in the final assembly.
    return std::bit_cast<AccountID>(bytes);
}

// Disallow temporary
inline AccountID const&
getMPTIssuer(MPTID const&&) = delete;
inline AccountID const&
getMPTIssuer(MPTID&&) = delete;

inline MPTID
noMPT()
{
    static MPTIssue const mpt{0, noAccount()};
    return mpt.getMptID();
}

inline MPTID
badMPT()
{
    static MPTIssue const mpt{0, xrpAccount()};
    return mpt.getMptID();
}

template <class Hasher>
void
hash_append(Hasher& h, MPTIssue const& r)
{
    using beast::hash_append;
    hash_append(h, r.getMptID());
}

Json::Value
to_json(MPTIssue const& mptIssue);

std::string
to_string(MPTIssue const& mptIssue);

MPTIssue
mptIssueFromJson(Json::Value const& jv);

std::ostream&
operator<<(std::ostream& os, MPTIssue const& x);

}  // namespace xrpl

namespace std {

template <>
struct hash<xrpl::MPTID> : xrpl::MPTID::hasher
{
    explicit hash() = default;
};

}  // namespace std
