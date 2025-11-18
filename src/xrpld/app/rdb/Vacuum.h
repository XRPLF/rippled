#ifndef XRPL_APP_RDB_VACUUM_H_INCLUDED
#define XRPL_APP_RDB_VACUUM_H_INCLUDED

#include <xrpld/core/DatabaseCon.h>

namespace ripple {

/**
 * @brief doVacuumDB Creates, initialises, and performs cleanup on a database.
 * @param setup Path to the database and other opening parameters.
 * @param j Journal.
 * @return True if the vacuum process completed successfully.
 */
bool
doVacuumDB(DatabaseCon::Setup const& setup, beast::Journal j);

}  // namespace ripple

#endif
