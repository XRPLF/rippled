ARG DIST_TAG=20.04
FROM ubuntu:$DIST_TAG
ARG GIT_COMMIT=unknown
ARG CI_USE=false
LABEL git-commit=$GIT_COMMIT

# install/setup prerequisites:
COPY ubuntu-builder/ubuntu_setup.sh /tmp/
COPY shared/install_cmake.sh /tmp/
RUN chmod +x /tmp/ubuntu_setup.sh && \
    chmod +x /tmp/install_cmake.sh
RUN /tmp/ubuntu_setup.sh

RUN /tmp/install_cmake.sh 3.16.3 /opt/local/cmake-3.16
RUN ln -s /opt/local/cmake-3.16 /opt/local/cmake
ENV PATH="/opt/local/cmake/bin:$PATH"

# prep files for package building
RUN update-alternatives --set gcc /usr/bin/gcc-11
RUN mkdir -m 777 -p /opt/rippled_bld/pkg/
WORKDIR /opt/rippled_bld/pkg

COPY packaging/dpkg/build_dpkg.sh ./
CMD ./build_dpkg.sh
