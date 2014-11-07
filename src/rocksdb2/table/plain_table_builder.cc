//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_LITE
#include "table/plain_table_builder.h"

#include <string>
#include <assert.h>
#include <map>

#include "rocksdb/comparator.h"
#include "rocksdb/env.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "table/plain_table_factory.h"
#include "db/dbformat.h"
#include "table/block_builder.h"
#include "table/bloom_block.h"
#include "table/plain_table_index.h"
#include "table/filter_block.h"
#include "table/format.h"
#include "table/meta_blocks.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "util/stop_watch.h"

namespace rocksdb {

namespace {

// a utility that helps writing block content to the file
//   @offset will advance if @block_contents was successfully written.
//   @block_handle the block handle this particular block.
Status WriteBlock(
    const Slice& block_contents,
    WritableFile* file,
    uint64_t* offset,
    BlockHandle* block_handle) {
  block_handle->set_offset(*offset);
  block_handle->set_size(block_contents.size());
  Status s = file->Append(block_contents);

  if (s.ok()) {
    *offset += block_contents.size();
  }
  return s;
}

}  // namespace

// kPlainTableMagicNumber was picked by running
//    echo rocksdb.table.plain | sha1sum
// and taking the leading 64 bits.
extern const uint64_t kPlainTableMagicNumber = 0x8242229663bf9564ull;
extern const uint64_t kLegacyPlainTableMagicNumber = 0x4f3418eb7a8f13b8ull;

PlainTableBuilder::PlainTableBuilder(
    const Options& options, WritableFile* file, uint32_t user_key_len,
    EncodingType encoding_type, size_t index_sparseness,
    uint32_t bloom_bits_per_key, uint32_t num_probes, size_t huge_page_tlb_size,
    double hash_table_ratio, bool store_index_in_file)
    : options_(options),
      bloom_block_(num_probes),
      file_(file),
      bloom_bits_per_key_(bloom_bits_per_key),
      huge_page_tlb_size_(huge_page_tlb_size),
      encoder_(encoding_type, user_key_len, options.prefix_extractor.get(),
               index_sparseness),
      store_index_in_file_(store_index_in_file),
      prefix_extractor_(options.prefix_extractor.get()) {
  // Build index block and save it in the file if hash_table_ratio > 0
  if (store_index_in_file_) {
    assert(hash_table_ratio > 0 || IsTotalOrderMode());
    index_builder_.reset(
        new PlainTableIndexBuilder(&arena_, options, index_sparseness,
                                   hash_table_ratio, huge_page_tlb_size_));
    assert(bloom_bits_per_key_ > 0);
    properties_.user_collected_properties
        [PlainTablePropertyNames::kBloomVersion] = "1";  // For future use
  }

  properties_.fixed_key_len = user_key_len;

  // for plain table, we put all the data in a big chuck.
  properties_.num_data_blocks = 1;
  // Fill it later if store_index_in_file_ == true
  properties_.index_size = 0;
  properties_.filter_size = 0;
  // To support roll-back to previous version, now still use version 0 for
  // plain encoding.
  properties_.format_version = (encoding_type == kPlain) ? 0 : 1;

  if (options_.prefix_extractor) {
    properties_.user_collected_properties
        [PlainTablePropertyNames::kPrefixExtractorName] =
        options_.prefix_extractor->Name();
  }

  std::string val;
  PutFixed32(&val, static_cast<uint32_t>(encoder_.GetEncodingType()));
  properties_.user_collected_properties
      [PlainTablePropertyNames::kEncodingType] = val;

  for (auto& collector_factories :
       options.table_properties_collector_factories) {
    table_properties_collectors_.emplace_back(
        collector_factories->CreateTablePropertiesCollector());
  }
}

PlainTableBuilder::~PlainTableBuilder() {
}

void PlainTableBuilder::Add(const Slice& key, const Slice& value) {
  // temp buffer for metadata bytes between key and value.
  char meta_bytes_buf[6];
  size_t meta_bytes_buf_size = 0;

  ParsedInternalKey internal_key;
  ParseInternalKey(key, &internal_key);

  // Store key hash
  if (store_index_in_file_) {
    if (options_.prefix_extractor.get() == nullptr) {
      keys_or_prefixes_hashes_.push_back(GetSliceHash(internal_key.user_key));
    } else {
      Slice prefix =
          options_.prefix_extractor->Transform(internal_key.user_key);
      keys_or_prefixes_hashes_.push_back(GetSliceHash(prefix));
    }
  }

  // Write value
  auto prev_offset = offset_;
  // Write out the key
  encoder_.AppendKey(key, file_, &offset_, meta_bytes_buf,
                     &meta_bytes_buf_size);
  if (SaveIndexInFile()) {
    index_builder_->AddKeyPrefix(GetPrefix(internal_key), prev_offset);
  }

  // Write value length
  int value_size = value.size();
  char* end_ptr =
      EncodeVarint32(meta_bytes_buf + meta_bytes_buf_size, value_size);
  assert(end_ptr <= meta_bytes_buf + sizeof(meta_bytes_buf));
  meta_bytes_buf_size = end_ptr - meta_bytes_buf;
  file_->Append(Slice(meta_bytes_buf, meta_bytes_buf_size));

  // Write value
  file_->Append(value);
  offset_ += value_size + meta_bytes_buf_size;

  properties_.num_entries++;
  properties_.raw_key_size += key.size();
  properties_.raw_value_size += value.size();

  // notify property collectors
  NotifyCollectTableCollectorsOnAdd(key, value, table_properties_collectors_,
                                    options_.info_log.get());
}

Status PlainTableBuilder::status() const { return status_; }

Status PlainTableBuilder::Finish() {
  assert(!closed_);
  closed_ = true;

  properties_.data_size = offset_;

  //  Write the following blocks
  //  1. [meta block: bloom] - optional
  //  2. [meta block: index] - optional
  //  3. [meta block: properties]
  //  4. [metaindex block]
  //  5. [footer]

  MetaIndexBuilder meta_index_builer;

  if (store_index_in_file_ && (properties_.num_entries > 0)) {
    bloom_block_.SetTotalBits(
        &arena_, properties_.num_entries * bloom_bits_per_key_,
        options_.bloom_locality, huge_page_tlb_size_, options_.info_log.get());

    PutVarint32(&properties_.user_collected_properties
                     [PlainTablePropertyNames::kNumBloomBlocks],
                bloom_block_.GetNumBlocks());

    bloom_block_.AddKeysHashes(keys_or_prefixes_hashes_);
    BlockHandle bloom_block_handle;
    auto finish_result = bloom_block_.Finish();

    properties_.filter_size = finish_result.size();
    auto s = WriteBlock(finish_result, file_, &offset_, &bloom_block_handle);

    if (!s.ok()) {
      return s;
    }

    BlockHandle index_block_handle;
    finish_result = index_builder_->Finish();

    properties_.index_size = finish_result.size();
    s = WriteBlock(finish_result, file_, &offset_, &index_block_handle);

    if (!s.ok()) {
      return s;
    }

    meta_index_builer.Add(BloomBlockBuilder::kBloomBlock, bloom_block_handle);
    meta_index_builer.Add(PlainTableIndexBuilder::kPlainTableIndexBlock,
                          index_block_handle);
  }

  // Calculate bloom block size and index block size
  PropertyBlockBuilder property_block_builder;
  // -- Add basic properties
  property_block_builder.AddTableProperty(properties_);

  property_block_builder.Add(properties_.user_collected_properties);

  // -- Add user collected properties
  NotifyCollectTableCollectorsOnFinish(table_properties_collectors_,
                                       options_.info_log.get(),
                                       &property_block_builder);

  // -- Write property block
  BlockHandle property_block_handle;
  auto s = WriteBlock(
      property_block_builder.Finish(),
      file_,
      &offset_,
      &property_block_handle
  );
  if (!s.ok()) {
    return s;
  }
  meta_index_builer.Add(kPropertiesBlock, property_block_handle);

  // -- write metaindex block
  BlockHandle metaindex_block_handle;
  s = WriteBlock(
      meta_index_builer.Finish(),
      file_,
      &offset_,
      &metaindex_block_handle
  );
  if (!s.ok()) {
    return s;
  }

  // Write Footer
  // no need to write out new footer if we're using default checksum
  Footer footer(kLegacyPlainTableMagicNumber);
  footer.set_metaindex_handle(metaindex_block_handle);
  footer.set_index_handle(BlockHandle::NullBlockHandle());
  std::string footer_encoding;
  footer.EncodeTo(&footer_encoding);
  s = file_->Append(footer_encoding);
  if (s.ok()) {
    offset_ += footer_encoding.size();
  }

  return s;
}

void PlainTableBuilder::Abandon() {
  closed_ = true;
}

uint64_t PlainTableBuilder::NumEntries() const {
  return properties_.num_entries;
}

uint64_t PlainTableBuilder::FileSize() const {
  return offset_;
}

}  // namespace rocksdb
#endif  // ROCKSDB_LITE
