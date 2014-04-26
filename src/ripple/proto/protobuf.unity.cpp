// Unity build file for libprotobuf by Vinnie Falco <vinnie.falco@gmail.com>
//

#ifdef _MSC_VER

#ifdef _MSC_VER // MSVC
# pragma warning (push)
# pragma warning (disable: 4018) // signed/unsigned mismatch
# pragma warning (disable: 4244) // conversion, possible loss of data
# pragma warning (disable: 4800) // forcing value to bool
# pragma warning (disable: 4996) // deprecated POSIX name
#endif

#include "google/protobuf/descriptor.cc"
#include "google/protobuf/descriptor.pb.cc"
#include "google/protobuf/descriptor_database.cc"
#include "google/protobuf/dynamic_message.cc"
#include "google/protobuf/extension_set.cc"
#include "google/protobuf/extension_set_heavy.cc"
#include "google/protobuf/generated_message_reflection.cc"
#include "google/protobuf/generated_message_util.cc"
#include "google/protobuf/message.cc"
#include "google/protobuf/message_lite.cc"
#include "google/protobuf/reflection_ops.cc"
#include "google/protobuf/repeated_field.cc"
#include "google/protobuf/service.cc"
#include "google/protobuf/text_format.cc"
#include "google/protobuf/unknown_field_set.cc"
#include "google/protobuf/wire_format.cc"
#include "google/protobuf/wire_format_lite.cc"

#include "google/protobuf/compiler/importer.cc"
#include "google/protobuf/compiler/parser.cc"

#include "google/protobuf/io/coded_stream.cc"
#include "google/protobuf/io/gzip_stream.cc"
#include "google/protobuf/io/tokenizer.cc"
#include "google/protobuf/io/zero_copy_stream.cc"
#include "google/protobuf/io/zero_copy_stream_impl.cc"
#include "google/protobuf/io/zero_copy_stream_impl_lite.cc"

#include "google/protobuf/stubs/common.cc"
#include "google/protobuf/stubs/once.cc"
#include "google/protobuf/stubs/stringprintf.cc"
#include "google/protobuf/stubs/structurally_valid.cc"
#include "google/protobuf/stubs/strutil.cc"
#include "google/protobuf/stubs/substitute.cc"

#ifdef _MSC_VER // MSVC
#include "google/protobuf/stubs/atomicops_internals_x86_msvc.cc"
#endif

#ifdef _MSC_VER // MSVC
# pragma warning (pop)
#endif

#endif
