# Build image for NF2FS
# 
FROM ubuntu:22.04 AS sysbase

ENV DEPS="build-essential"
RUN apt-get update && apt-get install -y $DEPS

ADD emulated_environment /emulated_environment

