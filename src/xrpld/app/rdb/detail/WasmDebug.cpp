//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/rdb/WasmDebug.h>

#include <boost/algorithm/string.hpp>

namespace ripple {

std::unique_ptr<DatabaseCon>
makeWasmDebugDB(DatabaseCon::Setup const& setup, beast::Journal j)
{
    // WASM debug log database
    return std::make_unique<DatabaseCon>(
        setup,
        WasmDebugDBName,
        std::array<std::string, 0>(),
        WasmDebugDBInit,
        j);
}

void
addWasmDebugLogs(
    soci::session& session,
    TxID const& txId,
    Keylet const& keylet,
    std::vector<std::string> const& data)
{
    XRPL_ASSERT(session.is_open(), "ripple::addWasmDebugLogs : open session");
    soci::transaction tr(session);

    // Convert all the info to appropriate formats
    std::string const txHex = to_string(txId);
    std::string const keyletHex = to_string(keylet.key);
    std::string const logString = boost::algorithm::join(data, "\x1F");

    // replace = because you run transactions twice: open _and_ closed ledger
    session << "INSERT OR REPLACE INTO WasmDebugLogs "
               "(TransID, ObjID, Data) VALUES "
               "(:transID, :objId, :data)",
        soci::use(txHex), soci::use(keyletHex), soci::use(logString);

    tr.commit();
}

std::map<uint256, std::vector<std::string>>
getWasmDebugByTxID(soci::session& session, TxID const& txId)
{
    XRPL_ASSERT(session.is_open(), "ripple::addWasmDebugLogs : open session");
    std::map<uint256, std::vector<std::string>> ret;

    std::string const txHex = to_string(txId);

    std::string objHex;
    std::string logString;

    soci::statement st =
        (session.prepare << "SELECT ObjID, Data FROM WasmDebugLogs "
                            "WHERE TransID = :txId",
         soci::use(txHex),
         soci::into(objHex),
         soci::into(logString));

    st.execute();

    while (st.fetch())
    {
        uint256 objId;
        if (objId.parseHex(objHex))
        {
            std::vector<std::string> logs;
            boost::algorithm::split(logs, logString, boost::is_any_of("\x1F"));
            ret.emplace(objId, logs);
        }
    }

    return ret;
}

}  // namespace ripple
