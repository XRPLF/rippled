#!/bin/bash
echo "$BASH_VERSION"
#regex='^(.*?/)ripple_([^/]+?)$'
#find ./src -type f -mindepth 1 -maxdepth 6 -print0 | while IFS= read -r -d '' f;
#do
#    if [[ $f =~ $regex ]]; then
#        echo "$f"
#    fi
#done

#find . -type f -name 'ripple_*' -exec sh -c 'f={}; n=${f##*/ripple_}; echo "$f" "${f%/*}/$n"' \;
find . -name 'ripple_*' -type f -exec sh -c 'f={}; n=${f##*/ripple_}; mv "$f" "${f%/*}/$n"' \;






#find src/ripple* -type f -regex '^.\*/ripple_([^/]+)$' -mindepth 1 -maxdepth 6 -print0 | while IFS= read -r -d '' f;
#"^(.*?/)ripple_([^/]+?)$"
#find ./src -type f -regex '^\(.*?/\)ripple_\([^/]+?\)$' -mindepth 1 -maxdepth 6 -print0 | while IFS= read -r -d '' f;
#regex = "^(.*?/)ripple_([^/]+?)$"
#find ./src -type f -mindepth 1 -maxdepth 6 -print0 | while IFS= read -r -d '' f;
#find './ripple_*\.*' -type f -mindepth 1 -maxdepth 6 -print0 | while IFS= read -r -d '' f;
#do
#    echo "$f"
#done
#for f in '**/ripple_*';
#do
#n=${f##*/ripple_};
#mv "$f" "${f%/*}$n";
#echo "$f"
#done
#find . -name 'ripple_*' -exec sh -c 'f={}; n=${f##*/ripple_}; mv "$f" "${f%/*}$n"'