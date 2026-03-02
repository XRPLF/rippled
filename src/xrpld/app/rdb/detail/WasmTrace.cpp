#include <xrpld/app/rdb/WasmTrace.h>

#include <boost/algorithm/string.hpp>

namespace xrpl {

std::unique_ptr<DatabaseCon>
makeWasmTraceDB(DatabaseCon::Setup const& setup, beast::Journal j)
{
    // WASM debug log database
    return std::make_unique<DatabaseCon>(setup, WasmTraceDBName, std::array<std::string, 0>(), WasmTraceDBInit, j);
}

void
addWasmTraceLogs(soci::session& session, TxID const& txId, Keylet const& keylet, std::vector<std::string> const& data)
{
    soci::transaction tr(session);

    // Convert all the info to appropriate formats
    std::string const txHex = to_string(txId);
    std::string const keyletHex = to_string(keylet.key);
    std::string const logString = boost::algorithm::join(data, "\x1F");

    // replace = because you run transactions twice: open _and_ closed ledger
    session << "INSERT OR REPLACE INTO WasmTraceLogs "
               "(TransID, ObjID, Data) VALUES "
               "(:transID, :objId, :data)",
        soci::use(txHex), soci::use(keyletHex), soci::use(logString);

    tr.commit();
}

std::map<uint256, std::vector<std::string>>
getWasmTraceByTxID(soci::session& session, TxID const& txId)
{
    std::map<uint256, std::vector<std::string>> ret;

    std::string const txHex = to_string(txId);

    std::string objHex;
    std::string logString;

    soci::statement st =
        (session.prepare << "SELECT ObjID, Data FROM WasmTraceLogs "
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

}  // namespace xrpl
