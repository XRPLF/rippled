#!/bin/bash
# cspell: ignore clangf
modified=$1
dir=`pwd`
clangf=clang-format-10
clangf=clang-format
if [ "$1" = "--all" ]
then
  modified=`git status|egrep "modified|new file"|egrep "(cpp|h)$" | sed -E -e 's/modified://' -e 's/new file://' -e 's/^[[:space:]]+//'`
fi
for i in $modified
do
  basedir=$(dirname "$i")
  file=$(basename "$i")
  echo "$basedir $file"
  cd $basedir
  $clangf -style=file -i "$file"
  cd $dir
done
