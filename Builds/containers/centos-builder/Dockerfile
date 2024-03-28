FROM rippleci/centos:7
ARG GIT_COMMIT=unknown
ARG CI_USE=false

LABEL git-commit=$GIT_COMMIT

COPY centos-builder/centos_setup.sh /tmp/
COPY shared/install_cmake.sh /tmp/
RUN chmod +x /tmp/centos_setup.sh && \
    chmod +x /tmp/install_cmake.sh
RUN /tmp/centos_setup.sh

RUN /tmp/install_cmake.sh 3.16.3 /opt/local/cmake-3.16
RUN ln -s /opt/local/cmake-3.16 /opt/local/cmake
ENV PATH="/opt/local/cmake/bin:$PATH"
# TODO: Install latest CMake for testing
RUN if [ "${CI_USE}" = true ] ; then /tmp/install_cmake.sh 3.16.3 /opt/local/cmake-3.16; fi

RUN mkdir -m 777 -p /opt/rippled_bld/pkg

WORKDIR /opt/rippled_bld/pkg
RUN mkdir -m 777 ./rpmbuild
RUN mkdir -m 777 ./rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

COPY packaging/rpm/build_rpm.sh ./
CMD ./build_rpm.sh
