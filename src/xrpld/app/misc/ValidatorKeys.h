#ifndef XRPL_APP_MISC_VALIDATOR_KEYS_H_INCLUDED
#define XRPL_APP_MISC_VALIDATOR_KEYS_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/UintTypes.h>

#include <string>

namespace ripple {

class Config;

/** Validator keys and manifest as set in configuration file.  Values will be
    empty if not configured as a validator or not configured with a manifest.
*/
class ValidatorKeys
{
public:
    // Group all keys in a struct. Either all keys are valid or none are.
    struct Keys
    {
        PublicKey masterPublicKey;
        PublicKey publicKey;
        SecretKey secretKey;

        Keys() = delete;
        Keys(
            PublicKey const& masterPublic_,
            PublicKey const& public_,
            SecretKey const& secret_)
            : masterPublicKey(masterPublic_)
            , publicKey(public_)
            , secretKey(secret_)
        {
        }
    };

    // Note: The existence of keys cannot be used as a proxy for checking the
    // validity of a configuration. It is possible to have a valid
    // configuration while not setting the keys, as per the constructor of
    // the ValidatorKeys class.
    std::optional<Keys> keys;
    NodeID nodeID;
    std::string manifest;
    std::uint32_t sequence = 0;

    ValidatorKeys() = delete;
    ValidatorKeys(Config const& config, beast::Journal j);

    bool
    configInvalid() const
    {
        return configInvalid_;
    }

private:
    bool configInvalid_ = false;  //< Set to true if config was invalid
};

}  // namespace ripple

#endif
