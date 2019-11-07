ARG DIST_TAG=16.04
FROM ubuntu:$DIST_TAG
ARG GIT_COMMIT=unknown
ARG CI_USE=false
LABEL git-commit=$GIT_COMMIT

# install/setup prerequisites:
COPY ubuntu-builder/ubuntu_setup.sh /tmp/
COPY shared/build_deps.sh /tmp/
COPY shared/install_cmake.sh /tmp/
COPY shared/install_boost.sh /tmp/
RUN chmod +x /tmp/ubuntu_setup.sh && \
    chmod +x /tmp/build_deps.sh && \
    chmod +x /tmp/install_boost.sh && \
    chmod +x /tmp/install_cmake.sh
RUN /tmp/ubuntu_setup.sh

RUN /tmp/install_cmake.sh 3.16.0-rc3 /opt/local/cmake-3.16
RUN ln -s /opt/local/cmake-3.16 /opt/local/cmake
ENV PATH="/opt/local/cmake/bin:$PATH"
# also install min supported cmake for testing
RUN if [ "${CI_USE}" = true ] ; then /tmp/install_cmake.sh 3.9.0 /opt/local/cmake-3.9; fi

RUN /tmp/build_deps.sh
ENV PLANTUML_JAR="/opt/plantuml/plantuml.jar"
ENV BOOST_ROOT="/opt/local/boost/_INSTALLED_"
ENV OPENSSL_ROOT="/opt/local/openssl"

# prep files for package building
RUN mkdir -m 777 -p /opt/rippled_bld/pkg
WORKDIR /opt/rippled_bld/pkg

COPY packaging/dpkg/debian /opt/rippled_bld/pkg/debian/
COPY shared/update_sources.sh ./
COPY shared/rippled.service /opt/rippled_bld/pkg/debian/

COPY packaging/dpkg/build_dpkg.sh ./
RUN update-alternatives --set gcc /usr/bin/gcc-8

CMD ./build_dpkg.sh

