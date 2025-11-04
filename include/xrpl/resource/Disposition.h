#ifndef XRPL_RESOURCE_DISPOSITION_H_INCLUDED
#define XRPL_RESOURCE_DISPOSITION_H_INCLUDED

namespace ripple {
namespace Resource {

/** The disposition of a consumer after applying a load charge. */
enum Disposition {
    /** No action required. */
    ok

    /** Consumer should be warned that consumption is high. */
    ,
    warn

    /** Consumer should be disconnected for excess consumption. */
    ,
    drop
};

}  // namespace Resource
}  // namespace ripple

#endif
