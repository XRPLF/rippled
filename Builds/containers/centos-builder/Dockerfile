FROM centos:7
ARG GIT_COMMIT=unknown
ARG CI_USE=false

LABEL git-commit=$GIT_COMMIT

COPY centos-builder/centos_setup.sh /tmp/
COPY shared/build_deps.sh /tmp/
COPY shared/install_cmake.sh /tmp/
COPY centos-builder/extras.sh /tmp/
COPY shared/install_boost.sh /tmp/
RUN chmod +x /tmp/centos_setup.sh && \
    chmod +x /tmp/build_deps.sh && \
    chmod +x /tmp/install_boost.sh && \
    chmod +x /tmp/install_cmake.sh && \
    chmod +x /tmp/extras.sh
RUN /tmp/centos_setup.sh
RUN /tmp/install_cmake.sh
ENV PATH="/opt/local/cmake/bin:$PATH"
RUN source scl_source enable devtoolset-7 python27 && \
     /tmp/build_deps.sh
ENV BOOST_ROOT="/opt/local/boost/_INSTALLED_"
ENV PLANTUML_JAR="/opt/plantuml/plantuml.jar"
ENV OPENSSL_ROOT="/opt/local/openssl"
ENV GDB_ROOT="/opt/local/gdb"
RUN source scl_source enable devtoolset-7 python27 && \
     /tmp/extras.sh

# prep files for package building
RUN mkdir -m 777 -p /opt/rippled_bld/pkg
WORKDIR /opt/rippled_bld/pkg

COPY packaging/rpm/rippled.spec ./
COPY shared/update_sources.sh ./
RUN mkdir -m 777 ./rpmbuild
RUN mkdir -m 777 ./rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

COPY packaging/rpm/build_rpm.sh ./
CMD ./build_rpm.sh


