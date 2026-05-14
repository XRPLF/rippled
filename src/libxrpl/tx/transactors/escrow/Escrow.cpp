/** @file
 *  Build-system anchor for the escrow transactor module.
 *
 *  This file is intentionally empty. All escrow logic is distributed across
 *  three sibling translation units:
 *
 *  - `EscrowCreate.cpp`  — locks XRP or tokens into a new escrow SLE, with
 *    optional time-based release and/or a crypto-condition fulfillment
 *    requirement. Contains the canonical module-level comment for the entire
 *    escrow subsystem.
 *  - `EscrowFinish.cpp`  — releases escrowed funds to the destination after
 *    time and/or fulfillment conditions are met; validates and caches
 *    crypto-conditions via the hash router.
 *  - `EscrowCancel.cpp`  — returns escrowed funds to the originator after the
 *    cancel-after deadline has passed; no fee is applied on return.
 *
 *  The file exists as a structural artifact of the build system: either
 *  CMake required a translation unit when escrow logic was first introduced,
 *  or a refactor that split a monolithic `Escrow.cpp` into three specialized
 *  transactors left this source behind.
 *
 *  @see EscrowCreate.h, EscrowFinish.h, EscrowCancel.h
 */
