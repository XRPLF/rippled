#pragma once

#include <optional>
#include <ostream>
#include <string>

namespace xrpl {

enum class KeyType {
    Secp256k1 = 0,
    Ed25519 = 1,
};

inline std::optional<KeyType>
keyTypeFromString(std::string const& s)
{
    if (s == "secp256k1")
        return KeyType::Secp256k1;

    if (s == "ed25519")
        return KeyType::Ed25519;

    return {};
}

inline char const*
to_string(KeyType type)
{
    if (type == KeyType::Secp256k1)
        return "secp256k1";

    if (type == KeyType::Ed25519)
        return "ed25519";

    return "INVALID";
}

/**
 * Writes the name of a key type to a stream.
 *
 * @param s The stream to write to.
 * @param type The key type to write.
 * @return @p s.
 */
inline std::ostream&
operator<<(std::ostream& s, KeyType type)
{
    return s << to_string(type);
}

}  // namespace xrpl
