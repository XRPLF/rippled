//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASICTYPES_H
#define RIPPLE_BASICTYPES_H

/** Storage for linear binary data.

    Blocks of binary data appear often in various idioms and structures.
*/
typedef std::vector <unsigned char> Blob;

/** Synchronization primitives.
    This lets us switch between tracked and untracked mutexes.
*/
#if RIPPLE_TRACK_MUTEXES
typedef TrackedMutexType <boost::mutex> RippleMutex;
typedef TrackedMutexType <boost::recursive_mutex> RippleRecursiveMutex;
#else
typedef UntrackedMutexType <boost::mutex> RippleMutex;
typedef UntrackedMutexType <boost::recursive_mutex> RippleRecursiveMutex;
#endif

typedef boost::recursive_mutex DeprecatedRecursiveMutex;
typedef DeprecatedRecursiveMutex::scoped_lock DeprecatedScopedLock;

//------------------------------------------------------------------------------

/** A container used to hold a public key in binary format. */
typedef UnsignedInteger <33> RipplePublicKey;

/** A container holding the hash of a public key in binary format. */
typedef UnsignedInteger <20> RipplePublicKeyHash;

/** A callback used to check for canceling an operation. */
typedef SharedFunction <bool(void)> CancelCallback;

#endif
