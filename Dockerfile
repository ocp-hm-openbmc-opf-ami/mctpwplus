FROM ubuntu:devel AS builder.base

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    meson cmake git pkg-config libgtest-dev \
    libsystemd-dev systemd \
    python3-saneyaml python3-inflection python3-mako \
    wget libboost-dev libboost-context-dev libboost-coroutine-dev \
    vim 

RUN git config --global http.proxy $http_proxy
