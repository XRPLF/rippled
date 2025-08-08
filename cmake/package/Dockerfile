FROM ubuntu:focal AS packager
# FROM python:3.11-slim AS packager

# pip install pkg_tools
# contains the signing and copyting to repo scripts
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install --yes \
    build-essential  \
    cmake \
    curl \
    git \
    jq \
    python-is-python3 \
    python3-pip \
    rpm \
    vim
