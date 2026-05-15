#include <xrpld/app/rdb/PeerFinder.h>

#include <xrpld/peerfinder/detail/Store.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/rdb/SociDB.h>

#include <boost/optional/optional.hpp>  // IWYU pragma: keep

#include <soci/into.h>
#include <soci/session.h>
#include <soci/statement.h>
#include <soci/transaction.h>
#include <soci/use.h>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

namespace xrpl {

void
initPeerFinderDB(soci::session& session, BasicConfig const& config, beast::Journal j)
{
    DBConfig const sociConfig(config, "peerfinder");
    sociConfig.open(session);

    JLOG(j.info()) << "Opening database at '" << sociConfig.connectionString() << "'";

    soci::transaction tr(session);
    session << "PRAGMA encoding=\"UTF-8\";";

    session << "CREATE TABLE IF NOT EXISTS SchemaVersion ( "
               "  name             TEXT PRIMARY KEY, "
               "  version          INTEGER"
               ");";

    session << "CREATE TABLE IF NOT EXISTS PeerFinder_BootstrapCache ( "
               "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
               "  address  TEXT UNIQUE NOT NULL, "
               "  valence  INTEGER"
               ");";

    session << "CREATE INDEX IF NOT EXISTS "
               "  PeerFinder_BootstrapCache_Index ON "
               "PeerFinder_BootstrapCache "
               "  (  "
               "    address "
               "  ); ";

    tr.commit();
}

void
updatePeerFinderDB(soci::session& session, int currentSchemaVersion, beast::Journal j)
{
    soci::transaction tr(session);
    // get version
    int version(0);
    {
        // SOCI requires a boost::optional (not std::optional) parameter.
        boost::optional<int> vO;
        session << "SELECT "
                   "  version "
                   "FROM SchemaVersion WHERE "
                   "  name = 'PeerFinder';",
            soci::into(vO);

        version = vO.value_or(0);

        JLOG(j.info()) << "Opened version " << version << " database";
    }

    {
        if (version < currentSchemaVersion)
        {
            JLOG(j.info()) << "Updating database to version " << currentSchemaVersion;
        }
        else if (version > currentSchemaVersion)
        {
            Throw<std::runtime_error>("The PeerFinder database version is higher than expected");
        }
    }

    if (version < 4)
    {
        //
        // Remove the "uptime" column from the bootstrap table
        //

        session << "CREATE TABLE IF NOT EXISTS "
                   "PeerFinder_BootstrapCache_Next ( "
                   "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "  address  TEXT UNIQUE NOT NULL, "
                   "  valence  INTEGER"
                   ");";

        session << "CREATE INDEX IF NOT EXISTS "
                   "  PeerFinder_BootstrapCache_Next_Index ON "
                   "    PeerFinder_BootstrapCache_Next "
                   "  ( address ); ";

        std::size_t count = 0;
        session << "SELECT COUNT(*) FROM PeerFinder_BootstrapCache;", soci::into(count);

        std::vector<PeerFinder::Store::Entry> list;

        {
            list.reserve(count);
            std::string s;
            int valence = 0;
            soci::statement st =
                (session.prepare << "SELECT "
                                    " address, "
                                    " valence "
                                    "FROM PeerFinder_BootstrapCache;",
                 soci::into(s),
                 soci::into(valence));

            st.execute();
            while (st.fetch())
            {
                PeerFinder::Store::Entry entry;
                entry.endpoint = beast::IP::Endpoint::fromString(s);
                if (!isUnspecified(entry.endpoint))
                {
                    entry.valence = valence;
                    list.push_back(entry);
                }
                else
                {
                    JLOG(j.error()) << "Bad address string '" << s << "' in Bootcache table";
                }
            }
        }

        if (!list.empty())
        {
            std::vector<std::string> s;
            std::vector<int> valence;
            s.reserve(list.size());
            valence.reserve(list.size());

            for (auto iter(list.cbegin()); iter != list.cend(); ++iter)
            {
                s.emplace_back(to_string(iter->endpoint));
                valence.emplace_back(iter->valence);
            }

            session << "INSERT INTO PeerFinder_BootstrapCache_Next ( "
                       "  address, "
                       "  valence "
                       ") VALUES ( "
                       "  :s, :valence"
                       ");",
                soci::use(s), soci::use(valence);
        }

        session << "DROP TABLE IF EXISTS PeerFinder_BootstrapCache;";

        session << "DROP INDEX IF EXISTS PeerFinder_BootstrapCache_Index;";

        session << "ALTER TABLE PeerFinder_BootstrapCache_Next "
                   "  RENAME TO PeerFinder_BootstrapCache;";

        session << "CREATE INDEX IF NOT EXISTS "
                   "  PeerFinder_BootstrapCache_Index ON "
                   "PeerFinder_BootstrapCache "
                   "  (  "
                   "    address "
                   "  ); ";
    }

    if (version < 3)
    {
        //
        // Remove legacy endpoints from the schema
        //

        session << "DROP TABLE IF EXISTS LegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinderLegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints_Index;";
    }

    {
        int const v(currentSchemaVersion);
        session << "INSERT OR REPLACE INTO SchemaVersion ("
                   "   name "
                   "  ,version "
                   ") VALUES ( "
                   "  'PeerFinder', :version "
                   ");",
            soci::use(v);
    }

    tr.commit();
}

void
readPeerFinderDB(soci::session& session, std::function<void(std::string const&, int)> const& func)
{
    std::string s;
    int valence = 0;
    soci::statement st =
        (session.prepare << "SELECT "
                            " address, "
                            " valence "
                            "FROM PeerFinder_BootstrapCache;",
         soci::into(s),
         soci::into(valence));

    st.execute();
    while (st.fetch())
    {
        func(s, valence);
    }
}

void
savePeerFinderDB(soci::session& session, std::vector<PeerFinder::Store::Entry> const& v)
{
    soci::transaction tr(session);
    session << "DELETE FROM PeerFinder_BootstrapCache;";

    if (!v.empty())
    {
        std::vector<std::string> s;
        std::vector<int> valence;
        s.reserve(v.size());
        valence.reserve(v.size());

        for (auto const& e : v)
        {
            s.emplace_back(to_string(e.endpoint));
            valence.emplace_back(e.valence);
        }

        session << "INSERT INTO PeerFinder_BootstrapCache ( "
                   "  address, "
                   "  valence "
                   ") VALUES ( "
                   "  :s, :valence "
                   ");",
            soci::use(s), soci::use(valence);
    }

    tr.commit();
}

}  // namespace xrpl
