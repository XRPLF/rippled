FROM ubuntu:16.04
MAINTAINER Mathilde Ffrench

RUN apt-get update && apt-get -y upgrade && \
    apt-get -y install openssl libprotobuf9v5 libboost-coroutine1.58.0 libboost-filesystem1.58.0 libboost-program-options1.58.0 libboost-regex1.58.0 && \
    mkdir /RIPPLED && groupadd -g 1000 rippled && useradd -g 1000 -d /RIPPLED -u 1000 rippled

VOLUME /RIPPLED/bin
VOLUME /RIPPLED/etc
VOLUME /RIPPLED/data
WORKDIR /RIPPLED

EXPOSE 5005/tcp

USER rippled

ENTRYPOINT ["/RIPPLED/bin/rippled", "--conf", "/RIPPLED/etc/rippled.cfg"]
