#/bin/sh

# Invoke as "sh ./Builds/test-all.sh"
# or first make it executable ("chmod a+rx ./Builds/test-all.sh)
#   then invoke as "./Builds/test-all.sh"
#
# Build must succeed without shell aliases for this to work. 

BUILD="debug release all" 
success=""
scons ${BUILD}  && \
  for dir in ./build/*
  do
    if [ ! -x ${dir}/rippled ]
    then
      echo -e "\n\n\n${dir} is not a build dir\n\n\n"
      continue
    fi
    RUN=$( basename ${dir} )
    echo -e "\n\n\nTesting ${RUN}\n\n\n"
    RIPPLED=./build/${RUN}/rippled 
    LOG=unittest.${RUN}.log
    ${RIPPLED} --unittest | tee ${LOG} && \
      grep -q "0 failures" ${LOG} && \
        npm test --rippled=${RIPPLED} \
          || break
    success="${success} ${RUN}"
    RUN=
  done

if [ -n "${RUN}" ]
then
  echo "Failed on ${RUN}" 
fi
if [ -n "${success}" ]
then
  echo "Success on ${success}"
fi
