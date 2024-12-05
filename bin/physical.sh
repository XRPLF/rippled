#!/bin/bash

set -o errexit

marker_base=985c80fbc6131f3a8cedd0da7e8af98dfceb13c7
marker_commit=${1:-${marker_base}}

if [ $(git merge-base ${marker_commit} ${marker_base}) != ${marker_base} ]; then
  echo "first marker commit not an ancestor: ${marker_commit}"
  exit 1
fi

if [ $(git merge-base ${marker_commit} HEAD) != $(git rev-parse --verify ${marker_commit}) ]; then
  echo "given marker commit not an ancestor: ${marker_commit}"
  exit 1
fi

if [ -e Builds/CMake ]; then
  echo move CMake
  git mv Builds/CMake cmake
  git add --update .
  git commit -m 'Move CMake directory' --author 'Pretty Printer <cpp@ripple.com>'
fi

if [ -e src/ripple ]; then

  echo move protocol buffers
  mkdir -p include/xrpl
  if [ -e src/ripple/proto ]; then
    git mv src/ripple/proto include/xrpl
  fi

  extract_list() {
    git show ${marker_commit}:Builds/CMake/RippledCore.cmake | \
    awk "/END ${1}/ { p = 0 } p && /src\/ripple/; /BEGIN ${1}/ { p = 1 }" | \
    sed -e 's#src/ripple/##' -e 's#[^a-z]\+$##'
  }

  move_files() {
    oldroot="$1"; shift
    newroot="$1"; shift
    detail="$1"; shift
    files=("$@")
    for file in ${files[@]}; do
      if [ ! -e ${oldroot}/${file} ]; then
        continue
      fi
      dir=$(dirname ${file})
      if [ $(basename ${dir}) == 'details' ]; then
        dir=$(dirname ${dir})
      fi
      if [ $(basename ${dir}) == 'impl' ]; then
        dir="$(dirname ${dir})/${detail}"
      fi
      mkdir -p ${newroot}/${dir}
      git mv ${oldroot}/${file} ${newroot}/${dir}
    done
  }

  echo move libxrpl headers
  files=$(extract_list 'LIBXRPL HEADERS')
  files+=(
    basics/SlabAllocator.h

    beast/asio/io_latency_probe.h
    beast/container/aged_container.h
    beast/container/aged_container_utility.h
    beast/container/aged_map.h
    beast/container/aged_multimap.h
    beast/container/aged_multiset.h
    beast/container/aged_set.h
    beast/container/aged_unordered_map.h
    beast/container/aged_unordered_multimap.h
    beast/container/aged_unordered_multiset.h
    beast/container/aged_unordered_set.h
    beast/container/detail/aged_associative_container.h
    beast/container/detail/aged_container_iterator.h
    beast/container/detail/aged_ordered_container.h
    beast/container/detail/aged_unordered_container.h
    beast/container/detail/empty_base_optimization.h
    beast/core/LockFreeStack.h
    beast/insight/Collector.h
    beast/insight/Counter.h
    beast/insight/CounterImpl.h
    beast/insight/Event.h
    beast/insight/EventImpl.h
    beast/insight/Gauge.h
    beast/insight/GaugeImpl.h
    beast/insight/Group.h
    beast/insight/Groups.h
    beast/insight/Hook.h
    beast/insight/HookImpl.h
    beast/insight/Insight.h
    beast/insight/Meter.h
    beast/insight/MeterImpl.h
    beast/insight/NullCollector.h
    beast/insight/StatsDCollector.h
    beast/test/fail_counter.h
    beast/test/fail_stream.h
    beast/test/pipe_stream.h
    beast/test/sig_wait.h
    beast/test/string_iostream.h
    beast/test/string_istream.h
    beast/test/string_ostream.h
    beast/test/test_allocator.h
    beast/test/yield_to.h
    beast/utility/hash_pair.h
    beast/utility/maybe_const.h
    beast/utility/temp_dir.h

    # included by only json/impl/json_assert.h
    json/json_errors.h

    protocol/PayChan.h
    protocol/RippleLedgerHash.h
    protocol/messages.h
    protocol/st.h
  )
  files+=(
    basics/README.md
    crypto/README.md
    json/README.md
    protocol/README.md
    resource/README.md
  )
  move_files src/ripple include/xrpl detail ${files[@]}

  echo move libxrpl sources
  files=$(extract_list 'LIBXRPL SOURCES')
  move_files src/ripple src/libxrpl "" ${files[@]}

  echo check leftovers
  dirs=$(cd include/xrpl; ls -d */)
  dirs=$(cd src/ripple; ls -d ${dirs} 2>/dev/null || true)
  files="$(cd src/ripple; find ${dirs} -type f)"
  if [ -n "${files}" ]; then
    echo "leftover files:"
    echo ${files}
    exit
  fi

  echo remove empty directories
  empty_dirs="$(cd src/ripple; find ${dirs} -depth -type d)"
  for dir in ${empty_dirs[@]}; do
    if [ -e ${dir} ]; then
      rmdir ${dir}
    fi
  done

  echo move xrpld sources
  files=$(
    extract_list 'XRPLD SOURCES'
    cd src/ripple
    find * -regex '.*\.\(h\|ipp\|md\|pu\|uml\|png\)'
  )
  move_files src/ripple src/xrpld detail ${files[@]}

  files="$(cd src/ripple; find . -type f)"
  if [ -n "${files}" ]; then
    echo "leftover files:"
    echo ${files}
    exit
  fi

fi

rm -rf src/ripple

echo rename .hpp to .h
find include src -name '*.hpp' -exec bash -c 'f="{}"; git mv "${f}" "${f%hpp}h"' \;

echo move PerfLog.h
if [ -e include/xrpl/basics/PerfLog.h ]; then
  git mv include/xrpl/basics/PerfLog.h src/xrpld/perflog
fi

# Make sure all protobuf includes have the correct prefix.
protobuf_replace='s:^#include\s*["<].*org/xrpl\([^">]\+\)[">]:#include <xrpl/proto/org/xrpl\1>:'
# Make sure first-party includes use angle brackets and .h extension.
ripple_replace='s:include\s*["<]ripple/\(.*\)\.h\(pp\)\?[">]:include <ripple/\1.h>:'
beast_replace='s:include\s*<beast/:include <xrpl/beast/:'
# Rename impl directories to detail.
impl_rename='s:\(<xrpl.*\)/impl\(/details\)\?/:\1/detail/:'

echo rewrite includes in libxrpl
find include/xrpl src/libxrpl -type f -exec sed -i \
  -e "${protobuf_replace}" \
  -e "${ripple_replace}" \
  -e "${beast_replace}" \
  -e 's:^#include <ripple/:#include <xrpl/:' \
  -e "${impl_rename}" \
  {} +

echo rewrite includes in xrpld
# # https://www.baeldung.com/linux/join-multiple-lines
libxrpl_dirs="$(cd include/xrpl; ls -d1 */ | sed 's:/$::')"
# libxrpl_dirs='a\nb\nc\n'
readarray -t libxrpl_dirs <<< "${libxrpl_dirs}"
# libxrpl_dirs=(a b c)
libxrpl_dirs=$(printf -v txt '%s\\|' "${libxrpl_dirs[@]}"; echo "${txt%\\|}")
# libxrpl_dirs='a\|b\|c'
find src/xrpld src/test -type f -exec sed -i \
  -e "${protobuf_replace}" \
  -e "${ripple_replace}" \
  -e "${beast_replace}" \
  -e "s:^#include <ripple/basics/PerfLog.h>:#include <xrpld/perflog/PerfLog.h>:" \
  -e "s:^#include <ripple/\(${libxrpl_dirs}\)/:#include <xrpl/\1/:" \
  -e 's:^#include <ripple/:#include <xrpld/:' \
  -e "${impl_rename}" \
  {} +

git commit -m 'Rearrange sources' --author 'Pretty Printer <cpp@ripple.com>'
find include src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.ipp' \) -exec clang-format-10 -i {} +
git add --update .
git commit -m 'Rewrite includes' --author 'Pretty Printer <cpp@ripple.com>'
./Builds/levelization/levelization.sh
git add --update .
git commit -m 'Recompute loops' --author 'Pretty Printer <cpp@ripple.com>'
