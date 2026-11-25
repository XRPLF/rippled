#pragma once

#include <array>
#include <string_view>

namespace xrpl {

/**
 * Fields that carry a credential, in a request or in a reply.
 *
 * The one list for the RPC masker and for the fallback scrubber of the log.
 * Literals, since jss.h sits above this module. MaskSecrets.cpp asserts at
 * compile time that each one equals its jss entry, which is the name all
 * other code uses, so a misspelled literal here does not build.
 *
 * `username` and `password` are `subscribe`'s deprecated spellings of
 * `url_username` and `url_password`, which is why a name identifying rather
 * than authenticating is on the list. The `master_` and `validation_` names
 * come from `wallet_propose` and `validation_create` replies, and a reply is
 * logged like a request. `master_key` is also the public key that
 * `validator_info` and the `manifests` and `validations` streams report, and
 * it is masked with the rest.
 */
inline constexpr std::array<std::string_view, 16> kCredentialFields{
    "admin_password",
    "admin_user",
    "master_key",
    "master_seed",
    "master_seed_hex",
    "passphrase",
    "password",
    "secret",
    "seed",
    "seed_hex",
    "url_password",
    "url_username",
    "username",
    "validation_key",
    "validation_private_key",
    "validation_seed",
};

}  // namespace xrpl
