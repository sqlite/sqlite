FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /sqlite
RUN apt-get update && apt-get install -y git
RUN git clone https://github.com/sqlite/sqlite.git .
RUN git checkout version-3.45.1
RUN apt-get install -y \
autoconf \
automake \
bison \
libtool \
make \
tcl \
pkg-config \
gcc \
g++ \
tcl-dev \
zlib1g-dev \
tcl8.6-dev \
tar
RUN cd /sqlite \
&& git fetch \
&& git checkout version-3.45.1 \
&& ./configure \
&& make \
&& make sqlite3.c \
&& make devtest
EXPOSE 8080
CMD ["/bin/bash"]
