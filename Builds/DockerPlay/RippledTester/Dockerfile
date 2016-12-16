FROM ubuntu:16.04
MAINTAINER Mathilde Ffrench

RUN apt-get update && apt-get -y upgrade && apt-get -y install curl && \
    curl -sL https://deb.nodesource.com/setup_6.x | bash -; \
    apt-get -y install openssl libprotobuf9v5 libboost-coroutine1.58.0 libboost-filesystem1.58.0 libboost-program-options1.58.0 libboost-regex1.58.0 nodejs && \ 
    groupadd -g 1000 rippled && useradd -g 1000 -d /RIPPLED -u 1000 rippled

VOLUME /RIPPLED
WORKDIR /RIPPLED

ADD rippled-test.sh /

USER rippled

ENTRYPOINT /rippled-test.sh
