#ifndef XRPL_TEST_JTX_UTILITY_H_INCLUDED
#define XRPL_TEST_JTX_UTILITY_H_INCLUDED

#include <test/jtx/Account.h>

#include <xrpld/app/ledger/Ledger.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STObject.h>

#include <stdexcept>

namespace ripple {
namespace test {
namespace jtx {

/** Thrown when parse fails. */
struct parse_error : std::logic_error
{
    template <class String>
    explicit parse_error(String const& s) : logic_error(s)
    {
    }
};

/** Convert JSON to STObject.
    This throws on failure, the JSON must be correct.
    @note Testing malformed JSON is beyond the scope of
          this set of unit test routines.
*/
STObject
parse(Json::Value const& jv);

/** Sign automatically into a specific Json field of the jv object.
    @note This only works on accounts with multi-signing off.
*/
void
sign(Json::Value& jv, Account const& account, Json::Value& sigObject);

/** Sign automatically.
    @note This only works on accounts with multi-signing off.
*/
void
sign(Json::Value& jv, Account const& account);

/** Set the fee automatically. */
void
fill_fee(Json::Value& jv, ReadView const& view);

/** Set the sequence number automatically. */
void
fill_seq(Json::Value& jv, ReadView const& view);

/** Given a rippled unit test rpc command, return the corresponding JSON. */
Json::Value
cmdToJSONRPC(
    std::vector<std::string> const& args,
    beast::Journal j,
    unsigned int apiVersion);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
