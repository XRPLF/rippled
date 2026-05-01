#pragma once

namespace xrpl::Resource {

/** The disposition of a consumer after applying a load charge. */
enum class Disposition {
    /** No action required. */
    ok

    /** Consumer should be warned that consumption is high. */
    ,
    warn

    /** Consumer should be disconnected for excess consumption. */
    ,
    drop
};

}  // namespace xrpl::Resource
