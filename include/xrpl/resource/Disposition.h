#pragma once

namespace xrpl::Resource {

/** The disposition of a consumer after applying a load charge. */
enum class Disposition {
    /** No action required. */
    Ok

    /** Consumer should be warned that consumption is high. */
    ,
    Warn

    /** Consumer should be disconnected for excess consumption. */
    ,
    Drop
};

}  // namespace xrpl::Resource
