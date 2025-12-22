#ifndef XRPL_PROTOCOL_KEYTYPE_H_INCLUDED
#define XRPL_PROTOCOL_KEYTYPE_H_INCLUDED

#include <optional>
#include <string>

namespace xrpl {

enum class KeyType {
    secp256k1 = 0,
    ed25519 = 1,
    dilithium = 2,
};

inline std::optional<KeyType>
keyTypeFromString(std::string const& s)
{
    if (s == "secp256k1")
        return KeyType::secp256k1;

    if (s == "ed25519")
        return KeyType::ed25519;

    if (s == "dilithium")
        return KeyType::dilithium;

    return {};
}

inline char const*
to_string(KeyType type)
{
    if (type == KeyType::secp256k1)
        return "secp256k1";

    if (type == KeyType::ed25519)
        return "ed25519";
    
    if (type == KeyType::dilithium)
        return "dilithium";

    return "INVALID";
}

template <class Stream>
inline Stream&
operator<<(Stream& s, KeyType type)
{
    return s << to_string(type);
}

}  // namespace xrpl

#endif
