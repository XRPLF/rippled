/** @file
 *  Empty stub — interface superseded by AssetCache.
 *
 *  `RippleLineCache` formerly provided a per-ledger, in-memory cache of
 *  `RippleState` (trust-line) objects for the pathfinding subsystem.  The
 *  entire interface and implementation have been replaced by `AssetCache`,
 *  which adds MPT caching and a `LineDirection` filter to `getRippleLines()`.
 *
 *  This header declares nothing.  Two translation units still `#include` it
 *  (`Pathfinder.cpp` and `PathRequestManager.h`); those includes are inert.
 *  The file is retained only to avoid disturbing the build system during the
 *  refactoring that produced `AssetCache`.
 *
 *  @see AssetCache
 */
