FROM ubuntu:16.04
MAINTAINER Mathilde Ffrench mathilde.ffrench@echinopsii.net

RUN apt-get update && \
    apt-get -y upgrade && \
    apt-get -y install scons pkg-config protobuf-compiler libprotobuf-dev libssl-dev wget libboost-coroutine1.58.0 libboost-filesystem1.58.0 libboost-program-options1.58.0 libboost-regex1.58.0 libboost1.58-all-dev && \
    groupadd -g 1000 rippled && useradd -g 1000 -d /RIPPLED -u 1000 rippled

VOLUME /RIPPLED

ADD docker-build-ripple.sh /

USER rippled

ENTRYPOINT /docker-build-ripple.sh
