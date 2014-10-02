// Copyright (c) 2014, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
package org.rocksdb;

/**
 * The config for plain table sst format.
 *
 * BlockBasedTable is a RocksDB's default SST file format.
 */
public class BlockBasedTableConfig extends TableFormatConfig {

  public BlockBasedTableConfig() {
    noBlockCache_ = false;
    blockCacheSize_ = 8 * 1024 * 1024;
    blockSize_ = 4 * 1024;
    blockSizeDeviation_ = 10;
    blockRestartInterval_ = 16;
    wholeKeyFiltering_ = true;
    bitsPerKey_ = 10;
    cacheIndexAndFilterBlocks_ = false;
    hashIndexAllowCollision_ = true;
    blockCacheCompressedSize_ = 0;
  }

  /**
   * Disable block cache. If this is set to true,
   * then no block cache should be used, and the block_cache should
   * point to a nullptr object.
   * Default: false
   *
   * @param noBlockCache if use block cache
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setNoBlockCache(boolean noBlockCache) {
    noBlockCache_ = noBlockCache;
    return this;
  }

  /**
   * @return if block cache is disabled
   */
  public boolean noBlockCache() {
    return noBlockCache_;
  }

  /**
   * Set the amount of cache in bytes that will be used by RocksDB.
   * If cacheSize is non-positive, then cache will not be used.
   * DEFAULT: 8M
   *
   * @param blockCacheSize block cache size in bytes
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setBlockCacheSize(long blockCacheSize) {
    blockCacheSize_ = blockCacheSize;
    return this;
  }

  /**
   * @return block cache size in bytes
   */
  public long blockCacheSize() {
    return blockCacheSize_;
  }

  /**
   * Controls the number of shards for the block cache.
   * This is applied only if cacheSize is set to non-negative.
   *
   * @param numShardBits the number of shard bits.  The resulting
   *     number of shards would be 2 ^ numShardBits.  Any negative
   *     number means use default settings."
   * @return the reference to the current option.
   */
  public BlockBasedTableConfig setCacheNumShardBits(int blockCacheNumShardBits) {
    blockCacheNumShardBits_ = blockCacheNumShardBits;
    return this;
  }

  /**
   * Returns the number of shard bits used in the block cache.
   * The resulting number of shards would be 2 ^ (returned value).
   * Any negative number means use default settings.
   *
   * @return the number of shard bits used in the block cache.
   */
  public int cacheNumShardBits() {
    return blockCacheNumShardBits_;
  }

  /**
   * Approximate size of user data packed per block.  Note that the
   * block size specified here corresponds to uncompressed data.  The
   * actual size of the unit read from disk may be smaller if
   * compression is enabled.  This parameter can be changed dynamically.
   * Default: 4K
   *
   * @param blockSize block size in bytes
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setBlockSize(long blockSize) {
    blockSize_ = blockSize;
    return this;
  }

  /**
   * @return block size in bytes
   */
  public long blockSize() {
    return blockSize_;
  }

  /**
   * This is used to close a block before it reaches the configured
   * 'block_size'. If the percentage of free space in the current block is less
   * than this specified number and adding a new record to the block will
   * exceed the configured block size, then this block will be closed and the
   * new record will be written to the next block.
   * Default is 10.
   *
   * @param blockSizeDeviation the deviation to block size allowed
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setBlockSizeDeviation(int blockSizeDeviation) {
    blockSizeDeviation_ = blockSizeDeviation;
    return this;
  }

  /**
   * @return the hash table ratio.
   */
  public int blockSizeDeviation() {
    return blockSizeDeviation_;
  }

  /**
   * Set block restart interval
   *
   * @param restartInterval block restart interval.
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setBlockRestartInterval(int restartInterval) {
    blockRestartInterval_ = restartInterval;
    return this;
  }

  /**
   * @return block restart interval
   */
  public int blockRestartInterval() {
    return blockRestartInterval_;
  }

  /**
   * If true, place whole keys in the filter (not just prefixes).
   * This must generally be true for gets to be efficient.
   * Default: true
   *
   * @param wholeKeyFiltering if enable whole key filtering
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setWholeKeyFiltering(boolean wholeKeyFiltering) {
    wholeKeyFiltering_ = wholeKeyFiltering;
    return this;
  }

  /**
   * @return if whole key filtering is enabled
   */
  public boolean wholeKeyFiltering() {
    return wholeKeyFiltering_;
  }

  /**
   * Use the specified filter policy to reduce disk reads.
   *
   * Filter should not be disposed before options instances using this filter is
   * disposed. If dispose() function is not called, then filter object will be
   * GC'd automatically.
   *
   * Filter instance can be re-used in multiple options instances.
   *
   * @param Filter policy java instance.
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setFilterBitsPerKey(int bitsPerKey) {
    bitsPerKey_ = bitsPerKey;
    return this;
  }
  
  /**
   * Indicating if we'd put index/filter blocks to the block cache.
     If not specified, each "table reader" object will pre-load index/filter
     block during table initialization.
   * 
   * @return if index and filter blocks should be put in block cache.
   */
  public boolean cacheIndexAndFilterBlocks() {
    return cacheIndexAndFilterBlocks_;
  }
  
  /**
   * Indicating if we'd put index/filter blocks to the block cache.
     If not specified, each "table reader" object will pre-load index/filter
     block during table initialization.
   * 
   * @param index and filter blocks should be put in block cache.
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setCacheIndexAndFilterBlocks(
      boolean cacheIndexAndFilterBlocks) {
    cacheIndexAndFilterBlocks_ = cacheIndexAndFilterBlocks;
    return this;
  }
  
  /**
   * Influence the behavior when kHashSearch is used.
     if false, stores a precise prefix to block range mapping
     if true, does not store prefix and allows prefix hash collision
     (less memory consumption)
   * 
   * @return if hash collisions should be allowed.
   */
  public boolean hashIndexAllowCollision() {
    return hashIndexAllowCollision_;
  }
  
  /**
   * Influence the behavior when kHashSearch is used.
     if false, stores a precise prefix to block range mapping
     if true, does not store prefix and allows prefix hash collision
     (less memory consumption)
   * 
   * @param if hash collisions should be allowed.
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setHashIndexAllowCollision(
      boolean hashIndexAllowCollision) {
    hashIndexAllowCollision_ = hashIndexAllowCollision;
    return this;
  }
  
  /**
   * Size of compressed block cache. If 0, then block_cache_compressed is set
   * to null.
   * 
   * @return size of compressed block cache.
   */
  public long blockCacheCompressedSize() {
    return blockCacheCompressedSize_;
  }
  
  /**
   * Size of compressed block cache. If 0, then block_cache_compressed is set
   * to null.
   * 
   * @param size of compressed block cache.
   * @return the reference to the current config.
   */
  public BlockBasedTableConfig setBlockCacheCompressedSize(
      long blockCacheCompressedSize) {
    blockCacheCompressedSize_ = blockCacheCompressedSize;
    return this;
  }
  
  /**
   * Controls the number of shards for the block compressed cache.
   * This is applied only if blockCompressedCacheSize is set to non-negative.
   *
   * @return numShardBits the number of shard bits.  The resulting
   *     number of shards would be 2 ^ numShardBits.  Any negative
   *     number means use default settings.
   */
  public int blockCacheCompressedNumShardBits() {
    return blockCacheCompressedNumShardBits_;
  }
  
  /**
   * Controls the number of shards for the block compressed cache.
   * This is applied only if blockCompressedCacheSize is set to non-negative.
   *
   * @param numShardBits the number of shard bits.  The resulting
   *     number of shards would be 2 ^ numShardBits.  Any negative
   *     number means use default settings."
   * @return the reference to the current option.
   */
  public BlockBasedTableConfig setBlockCacheCompressedNumShardBits(
      int blockCacheCompressedNumShardBits) {
    blockCacheCompressedNumShardBits_ = blockCacheCompressedNumShardBits;
    return this;
  }

  @Override protected long newTableFactoryHandle() {
    return newTableFactoryHandle(noBlockCache_, blockCacheSize_,
        blockCacheNumShardBits_, blockSize_, blockSizeDeviation_,
        blockRestartInterval_, wholeKeyFiltering_, bitsPerKey_,
        cacheIndexAndFilterBlocks_, hashIndexAllowCollision_,
        blockCacheCompressedSize_, blockCacheCompressedNumShardBits_);
  }

  private native long newTableFactoryHandle(
      boolean noBlockCache, long blockCacheSize, int blockCacheNumShardBits,
      long blockSize, int blockSizeDeviation, int blockRestartInterval,
      boolean wholeKeyFiltering, int bitsPerKey,
      boolean cacheIndexAndFilterBlocks, boolean hashIndexAllowCollision,
      long blockCacheCompressedSize, int blockCacheCompressedNumShardBits);

  private boolean noBlockCache_;
  private long blockCacheSize_;
  private int blockCacheNumShardBits_;
  private long shard;
  private long blockSize_;
  private int blockSizeDeviation_;
  private int blockRestartInterval_;
  private boolean wholeKeyFiltering_;
  private int bitsPerKey_;
  private boolean cacheIndexAndFilterBlocks_;
  private boolean hashIndexAllowCollision_;
  private long blockCacheCompressedSize_;
  private int blockCacheCompressedNumShardBits_;
}
