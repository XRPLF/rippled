#ifndef XRPL_APP_PATHS_CREDIT_H_INCLUDED
#define XRPL_APP_PATHS_CREDIT_H_INCLUDED

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/STAmount.h>

namespace ripple {

/** Calculate the maximum amount of IOUs that an account can hold
    @param ledger the ledger to check against.
    @param account the account of interest.
    @param issuer the issuer of the IOU.
    @param currency the IOU to check.
    @return The maximum amount that can be held.
*/
/** @{ */
STAmount
creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency);

IOUAmount
creditLimit2(
    ReadView const& v,
    AccountID const& acc,
    AccountID const& iss,
    Currency const& cur);
/** @} */

/** Returns the amount of IOUs issued by issuer that are held by an account
    @param ledger the ledger to check against.
    @param account the account of interest.
    @param issuer the issuer of the IOU.
    @param currency the IOU to check.
*/
/** @{ */
STAmount
creditBalance(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency);
/** @} */

}  // namespace ripple

#endif
