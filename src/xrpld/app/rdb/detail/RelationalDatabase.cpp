#include <xrpld/app/main/Application.h>
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/core/ConfigSections.h>

namespace ripple {

extern std::unique_ptr<RelationalDatabase>
getSQLiteDatabase(Application& app, Config const& config, JobQueue& jobQueue);

std::unique_ptr<RelationalDatabase>
RelationalDatabase::init(
    Application& app,
    Config const& config,
    JobQueue& jobQueue)
{
    bool use_sqlite = false;

    Section const& rdb_section{config.section(SECTION_RELATIONAL_DB)};
    if (!rdb_section.empty())
    {
        if (boost::iequals(get(rdb_section, "backend"), "sqlite"))
        {
            use_sqlite = true;
        }
        else
        {
            Throw<std::runtime_error>(
                "Invalid rdb_section backend value: " +
                get(rdb_section, "backend"));
        }
    }
    else
    {
        use_sqlite = true;
    }

    if (use_sqlite)
    {
        return getSQLiteDatabase(app, config, jobQueue);
    }

    return std::unique_ptr<RelationalDatabase>();
}

}  // namespace ripple
