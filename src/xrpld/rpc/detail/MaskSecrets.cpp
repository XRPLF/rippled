#include <xrpld/rpc/detail/MaskSecrets.h>

#include <xrpl/basics/CredentialFields.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace xrpl::rpc {

namespace {

// kCredentialFields cannot name jss entries, since its module sits below jss.h.
constexpr std::array<std::string_view, kCredentialFields.size()> kJssCredentialFields{
    jss::admin_password.cStr(),
    jss::admin_user.cStr(),
    jss::master_key.cStr(),
    jss::master_seed.cStr(),
    jss::master_seed_hex.cStr(),
    jss::passphrase.cStr(),
    jss::password.cStr(),
    jss::secret.cStr(),
    jss::seed.cStr(),
    jss::seed_hex.cStr(),
    jss::url_password.cStr(),
    jss::url_username.cStr(),
    jss::username.cStr(),
    jss::validation_key.cStr(),
    jss::validation_private_key.cStr(),
    jss::validation_seed.cStr(),
};
static_assert(
    std::ranges::equal(kCredentialFields, kJssCredentialFields),
    "kCredentialFields must name the same fields as their jss entries, in the same order");

/**
 * Reports whether @p name is one of kCredentialFields.
 *
 * An array element's memberName is empty, which no credential is spelled as,
 * so an element is never a credential itself and is descended into.
 *
 * @param name The member name to look up.
 * @return True if @p name is a credential-bearing field.
 */
bool
isCredential(std::string_view name)
{
    return !name.empty() && std::ranges::find(kCredentialFields, name) != kCredentialFields.end();
}

/**
 * Replaces every credential-bearing field of @p value in place, at every depth.
 *
 * @param value The object or array to mask. Any other value is left alone.
 */
void
maskInPlace(json::Value& value)
{
    if (!value.isObject() && !value.isArray())
        return;

    for (auto it = value.begin(); it != value.end(); ++it)
    {
        // A credential is replaced whatever it holds, so it is not descended into.
        if (isCredential(it.memberName()))
        {
            *it = json::Value{"<masked>"};
        }
        else
        {
            maskInPlace(*it);
        }
    }
}

}  // namespace

bool
hasSecret(json::Value const& value)
{
    if (!value.isObject() && !value.isArray())
        return false;

    for (auto it = value.begin(); it != value.end(); ++it)
    {
        if (isCredential(it.memberName()) || hasSecret(*it))
            return true;
    }
    return false;
}

json::Value
maskSecrets(json::Value const& request)
{
    auto masked = request;
    maskInPlace(masked);
    return masked;
}

std::string
loggable(json::Value const& value)
{
    // The copy the mask takes is paid only when there is a credential to replace.
    auto text = hasSecret(value) ? to_string(maskSecrets(value)) : to_string(value);
    if (text.size() > kMaxLoggedChars)
        text.resize(kMaxLoggedChars);
    return text;
}

}  // namespace xrpl::rpc
