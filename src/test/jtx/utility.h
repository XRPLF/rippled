#pragma once

#include <test/jtx/Account.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STObject.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::test::jtx {

/** Thrown when parse fails. */
struct ParseError : std::logic_error
{
    template <class String>
    explicit ParseError(String const& s) : logic_error(s)
    {
    }
};

/** Convert JSON to STObject.
    This throws on failure, the JSON must be correct.
    @note Testing malformed JSON is beyond the scope of
          this set of unit test routines.
*/
STObject
parse(json::Value const& jv);

/** Sign automatically into a specific Json field of the jv object.
    @note This only works on accounts with multi-signing off.
*/
void
sign(json::Value& jv, Account const& account, json::Value& sigObject);

/** Sign automatically.
    @note This only works on accounts with multi-signing off.
*/
void
sign(json::Value& jv, Account const& account);

/** Set the fee automatically. */
void
fillFee(json::Value& jv, ReadView const& view);

/** Set the sequence number automatically. */
void
fillSeq(json::Value& jv, ReadView const& view);

/** Given an xrpld unit test rpc command, return the corresponding JSON. */
json::Value
cmdToJSONRPC(std::vector<std::string> const& args, beast::Journal j, unsigned int apiVersion);
}  // namespace xrpl::test::jtx
