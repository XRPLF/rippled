#pragma once

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <string>
#include <string_view>
#include <utility>

namespace xrpl::test {

/**
 * @brief A test account with cryptographic keys.
 *
 * Generates keys deterministically from a name, making tests reproducible.
 * The same name always produces the same AccountID and keys.
 */
class Account
{
public:
    /**
     * @brief The master account that holds all XRP in genesis.
     *
     * This account is created in the genesis ledger with all 100 billion XRP.
     * It uses the well-known seed "masterpassphrase".
     */
    static Account const kMaster;

    /**
     * @brief Create an account from a name.
     *
     * Keys are derived deterministically from the name.
     *
     * @param name Human-readable name for the account.
     * @param type Key type to use (defaults to secp256k1).
     */
    explicit Account(std::string_view name, KeyType type = KeyType::Secp256k1);

    /** @brief Return the human-readable name. */
    [[nodiscard]] std::string const&
    name() const noexcept
    {
        return name_;
    }

    /** @brief Return the AccountID. */
    [[nodiscard]] AccountID const&
    id() const noexcept
    {
        return id_;
    }

    /** @brief Return the public key. */
    [[nodiscard]] PublicKey const&
    pk() const noexcept
    {
        return keyPair_.first;
    }

    /** @brief Return the secret key. */
    [[nodiscard]] SecretKey const&
    sk() const noexcept
    {
        return keyPair_.second;
    }

    /** @brief Implicit conversion to AccountID. */
    operator AccountID const&() const noexcept
    {
        return id_;
    }

private:
    std::string name_;
    std::pair<PublicKey, SecretKey> keyPair_;
    AccountID id_;
};

}  // namespace xrpl::test
