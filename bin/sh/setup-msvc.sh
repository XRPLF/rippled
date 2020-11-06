
# NOTE: must be sourced from a shell so it can export vars
# Will pass all command line params through to vcvarsall.bat

cat << BATCH > ./getenv.bat
CALL %*
ENV
BATCH

if echo "$-" | grep -q "x"
then
  # Don't output commands
  set +x
  restorex=1
fi
while read line ; do
  IFS='"' read x path arg <<<"${line}"
  if [ -f "${path}" ] ; then
    echo "FOUND: $path"
    vcenv=$( ./getenv.bat "${path}" ${arg} )
    export VCINSTALLDIR=$( echo "${vcenv}" | grep "^VCINSTALLDIR=" | sed -E "s/^VCINSTALLDIR=//g")
    if [ "${VCINSTALLDIR}" != "" ] ; then
      echo "USING ${VCINSTALLDIR}"
      export LIB=$( echo "${vcenv}" | grep "^LIB=" | sed -E "s/^LIB=//g")
      export LIBPATH=$(echo "${vcenv}" | grep "^LIBPATH=" | sed -E "s/^LIBPATH=//g")
      export INCLUDE=$(echo "${vcenv}" | grep "^INCLUDE=" | sed -E "s/^INCLUDE=//g")
      ADDPATH=$(echo "${vcenv}" | grep "^PATH=" | sed -E "s/^PATH=//g")
      for var in LIB LIBPATH INCLUDE ADDPATH
      do
        echo "${var}: ${!var}"
      done
      export PATH="${ADDPATH}:${PATH}"
      break
    fi
  fi
done <<EOL
"C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Auxiliary/Build/vcvarsall.bat" x64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvarsall.bat" x64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/VC/Auxiliary/Build/vcvarsall.bat" x64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/VC/Auxiliary/Build/vcvarsall.bat" x64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio/2017/Community/VC/Auxiliary/Build/vcvarsall.bat" x64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio/2017/Enterprise/VC/Auxiliary/Build/vcvarsall.bat" x64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio 15.0/VC/vcvarsall.bat" amd64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio 14.0/VC/vcvarsall.bat" amd64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio 13.0/VC/vcvarsall.bat" amd64 ${@}
"C:/Program Files (x86)/Microsoft Visual Studio 12.0/VC/vcvarsall.bat" amd64 ${@}
EOL
# TODO: update the list above as needed to support newer versions of msvc tools

rm -f getenv.bat

if [ "${VCINSTALLDIR}" = "" ] ; then
  echo "No compatible visual studio found!"
fi

if [[ -v restorex ]]
then
  set -x
fi
