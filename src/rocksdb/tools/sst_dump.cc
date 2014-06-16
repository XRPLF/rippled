//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//

#include <map>
#include <string>
#include <vector>
#include <inttypes.h>

#include "db/dbformat.h"
#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/iterator.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/table.h"
#include "rocksdb/table_properties.h"
#include "table/block_based_table_factory.h"
#include "table/plain_table_factory.h"
#include "table/meta_blocks.h"
#include "table/block.h"
#include "table/block_builder.h"
#include "table/format.h"
#include "util/ldb_cmd.h"
#include "util/random.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace rocksdb {

class SstFileReader {
 public:
  explicit SstFileReader(const std::string& file_name,
                         bool verify_checksum,
                         bool output_hex);

  Status ReadSequential(bool print_kv,
                        uint64_t read_num,
                        bool has_from,
                        const std::string& from_key,
                        bool has_to,
                        const std::string& to_key);

  Status ReadTableProperties(
      std::shared_ptr<const TableProperties>* table_properties);
  uint64_t GetReadNumber() { return read_num_; }

 private:
  Status NewTableReader(const std::string& file_path);
  Status SetTableOptionsByMagicNumber(uint64_t table_magic_number,
                                      RandomAccessFile* file,
                                      uint64_t file_size);

  std::string file_name_;
  uint64_t read_num_;
  bool verify_checksum_;
  bool output_hex_;
  EnvOptions soptions_;

  Status init_result_;
  unique_ptr<TableReader> table_reader_;
  unique_ptr<RandomAccessFile> file_;
  // options_ and internal_comparator_ will also be used in
  // ReadSequential internally (specifically, seek-related operations)
  Options options_;
  InternalKeyComparator internal_comparator_;
};

SstFileReader::SstFileReader(const std::string& file_path,
                             bool verify_checksum,
                             bool output_hex)
    :file_name_(file_path), read_num_(0), verify_checksum_(verify_checksum),
    output_hex_(output_hex), internal_comparator_(BytewiseComparator()) {
  fprintf(stdout, "Process %s\n", file_path.c_str());

  init_result_ = NewTableReader(file_name_);
}

extern uint64_t kBlockBasedTableMagicNumber;
extern uint64_t kPlainTableMagicNumber;

Status SstFileReader::NewTableReader(const std::string& file_path) {
  uint64_t magic_number;

  // read table magic number
  Footer footer;

  unique_ptr<RandomAccessFile> file;
  uint64_t file_size;
  Status s = options_.env->NewRandomAccessFile(file_path, &file_, soptions_);
  if (s.ok()) {
    s = options_.env->GetFileSize(file_path, &file_size);
  }
  if (s.ok()) {
    s = ReadFooterFromFile(file_.get(), file_size, &footer);
  }
  if (s.ok()) {
    magic_number = footer.table_magic_number();
  }

  if (s.ok()) {
    if (magic_number == kPlainTableMagicNumber) {
      soptions_.use_mmap_reads = true;
    }
    options_.comparator = &internal_comparator_;
    s = SetTableOptionsByMagicNumber(magic_number, file_.get(), file_size);
  }

  if (s.ok()) {
    s = options_.table_factory->NewTableReader(
        options_, soptions_, internal_comparator_, std::move(file_), file_size,
        &table_reader_);
  }
  return s;
}

Status SstFileReader::SetTableOptionsByMagicNumber(uint64_t table_magic_number,
                                                   RandomAccessFile* file,
                                                   uint64_t file_size) {
  TableProperties* table_properties;
  Status s = rocksdb::ReadTableProperties(file, file_size, table_magic_number,
                                          options_.env, options_.info_log.get(),
                                          &table_properties);
  if (!s.ok()) {
    return s;
  }
  std::unique_ptr<TableProperties> props_guard(table_properties);

  if (table_magic_number == kBlockBasedTableMagicNumber) {
    options_.table_factory = std::make_shared<BlockBasedTableFactory>();
    fprintf(stdout, "Sst file format: block-based\n");
  } else if (table_magic_number == kPlainTableMagicNumber) {
    options_.allow_mmap_reads = true;
    options_.table_factory = std::make_shared<PlainTableFactory>(
        table_properties->fixed_key_len, 2, 0.8);
    options_.prefix_extractor.reset(NewNoopTransform());
    fprintf(stdout, "Sst file format: plain table\n");
  } else {
    char error_msg_buffer[80];
    snprintf(error_msg_buffer, sizeof(error_msg_buffer) - 1,
             "Unsupported table magic number --- %lx",
             (long)table_magic_number);
    return Status::InvalidArgument(error_msg_buffer);
  }

  return Status::OK();
}

Status SstFileReader::ReadSequential(bool print_kv,
                                     uint64_t read_num,
                                     bool has_from,
                                     const std::string& from_key,
                                     bool has_to,
                                     const std::string& to_key) {
  if (!table_reader_) {
    return init_result_;
  }

  Iterator* iter = table_reader_->NewIterator(ReadOptions(verify_checksum_,
                                                         false));
  uint64_t i = 0;
  if (has_from) {
    InternalKey ikey(from_key, kMaxSequenceNumber, kValueTypeForSeek);
    iter->Seek(ikey.Encode());
  } else {
    iter->SeekToFirst();
  }
  for (; iter->Valid(); iter->Next()) {
    Slice key = iter->key();
    Slice value = iter->value();
    ++i;
    if (read_num > 0 && i > read_num)
      break;

    ParsedInternalKey ikey;
    if (!ParseInternalKey(key, &ikey)) {
      std::cerr << "Internal Key ["
                << key.ToString(true /* in hex*/)
                << "] parse error!\n";
      continue;
    }

    // If end marker was specified, we stop before it
    if (has_to && BytewiseComparator()->Compare(ikey.user_key, to_key) >= 0) {
      break;
    }

    if (print_kv) {
      fprintf(stdout, "%s => %s\n",
          ikey.DebugString(output_hex_).c_str(),
          value.ToString(output_hex_).c_str());
    }
  }

  read_num_ += i;

  Status ret = iter->status();
  delete iter;
  return ret;
}

Status SstFileReader::ReadTableProperties(
    std::shared_ptr<const TableProperties>* table_properties) {
  if (!table_reader_) {
    return init_result_;
  }

  *table_properties = table_reader_->GetTableProperties();
  return init_result_;
}

}  // namespace rocksdb

static void print_help() {
  fprintf(stderr,
      "sst_dump [--command=check|scan] [--verify_checksum] "
      "--file=data_dir_OR_sst_file"
      " [--output_hex]"
      " [--input_key_hex]"
      " [--from=<user_key>]"
      " [--to=<user_key>]"
      " [--read_num=NUM]"
      " [--show_properties]\n");
}

namespace {
string HexToString(const string& str) {
  string parsed;
  if (str[0] != '0' || str[1] != 'x') {
    fprintf(stderr, "Invalid hex input %s.  Must start with 0x\n",
            str.c_str());
    throw "Invalid hex input";
  }

  for (unsigned int i = 2; i < str.length();) {
    int c;
    sscanf(str.c_str() + i, "%2X", &c);
    parsed.push_back(c);
    i += 2;
  }
  return parsed;
}
}  // namespace

int main(int argc, char** argv) {
  const char* dir_or_file = nullptr;
  uint64_t read_num = -1;
  std::string command;

  char junk;
  uint64_t n;
  bool verify_checksum = false;
  bool output_hex = false;
  bool input_key_hex = false;
  bool has_from = false;
  bool has_to = false;
  bool show_properties = false;
  std::string from_key;
  std::string to_key;
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--file=", 7) == 0) {
      dir_or_file = argv[i] + 7;
    } else if (strcmp(argv[i], "--output_hex") == 0) {
      output_hex = true;
    } else if (strcmp(argv[i], "--input_key_hex") == 0) {
      input_key_hex = true;
    } else if (sscanf(argv[i],
               "--read_num=%lu%c",
               (unsigned long*)&n, &junk) == 1) {
      read_num = n;
    } else if (strcmp(argv[i], "--verify_checksum") == 0) {
      verify_checksum = true;
    } else if (strncmp(argv[i], "--command=", 10) == 0) {
      command = argv[i] + 10;
    } else if (strncmp(argv[i], "--from=", 7) == 0) {
      from_key = argv[i] + 7;
      has_from = true;
    } else if (strncmp(argv[i], "--to=", 5) == 0) {
      to_key = argv[i] + 5;
      has_to = true;
    } else if (strcmp(argv[i], "--show_properties") == 0) {
      show_properties = true;
    } else {
      print_help();
      exit(1);
    }
  }


  if (input_key_hex) {
    if (has_from) {
      from_key = HexToString(from_key);
    }
    if (has_to) {
      to_key = HexToString(to_key);
    }
  }

  if (dir_or_file == nullptr) {
    print_help();
    exit(1);
  }

  std::vector<std::string> filenames;
  rocksdb::Env* env = rocksdb::Env::Default();
  rocksdb::Status st = env->GetChildren(dir_or_file, &filenames);
  bool dir = true;
  if (!st.ok()) {
    filenames.clear();
    filenames.push_back(dir_or_file);
    dir = false;
  }

  fprintf(stdout, "from [%s] to [%s]\n",
      rocksdb::Slice(from_key).ToString(true).c_str(),
      rocksdb::Slice(to_key).ToString(true).c_str());

  uint64_t total_read = 0;
  for (size_t i = 0; i < filenames.size(); i++) {
    std::string filename = filenames.at(i);
    if (filename.length() <= 4 ||
        filename.rfind(".sst") != filename.length() - 4) {
      // ignore
      continue;
    }
    if (dir) {
      filename = std::string(dir_or_file) + "/" + filename;
    }
    rocksdb::SstFileReader reader(filename, verify_checksum,
                                  output_hex);
    rocksdb::Status st;
    // scan all files in give file path.
    if (command == "" || command == "scan" || command == "check") {
      st = reader.ReadSequential(command != "check",
                                 read_num > 0 ? (read_num - total_read) :
                                                read_num,
                                 has_from, from_key, has_to, to_key);
      if (!st.ok()) {
        fprintf(stderr, "%s: %s\n", filename.c_str(),
            st.ToString().c_str());
      }
      total_read += reader.GetReadNumber();
      if (read_num > 0 && total_read > read_num) {
        break;
      }
    }
    if (show_properties) {
      std::shared_ptr<const rocksdb::TableProperties> table_properties;
      st = reader.ReadTableProperties(&table_properties);
      if (!st.ok()) {
        fprintf(stderr, "%s: %s\n", filename.c_str(), st.ToString().c_str());
      } else {
        fprintf(stdout,
                "Table Properties:\n"
                "------------------------------\n"
                "  %s",
                table_properties->ToString("\n  ", ": ").c_str());
        fprintf(stdout, "# deleted keys: %zd\n",
                rocksdb::GetDeletedKeys(
                    table_properties->user_collected_properties));
      }
    }
  }
}
