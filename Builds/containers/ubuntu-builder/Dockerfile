ARG DIST_TAG=16.04
ARG GIT_COMMIT=unknown

FROM ubuntu:$DIST_TAG
LABEL git-commit=$GIT_COMMIT

# install/setup prerequisites:
COPY ubuntu-builder/ubuntu_setup.sh /tmp/
COPY shared/build_deps.sh /tmp/
COPY shared/install_cmake.sh /tmp/
RUN chmod +x /tmp/ubuntu_setup.sh && \
    chmod +x /tmp/build_deps.sh && \
    chmod +x /tmp/install_cmake.sh
RUN /tmp/ubuntu_setup.sh
RUN /tmp/install_cmake.sh
ENV PATH="/opt/local/cmake/bin:$PATH"
RUN /tmp/build_deps.sh
ENV PLANTUML_JAR="/opt/plantuml/plantuml.jar"
ENV BOOST_ROOT="/opt/local/boost"
ENV OPENSSL_ROOT="/opt/local/openssl"

# prep files for package building
RUN mkdir -m 777 -p /opt/rippled_bld/pkg
WORKDIR /opt/rippled_bld/pkg

COPY packaging/dpkg/debian /opt/rippled_bld/pkg/debian/
COPY shared/update_sources.sh ./
COPY shared/rippled.service /opt/rippled_bld/pkg/debian/

COPY packaging/dpkg/build_dpkg.sh ./
CMD ./build_dpkg.sh

