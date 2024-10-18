FROM ubuntu:22.04 as builder

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        apt-transport-https \
        build-essential \
        ca-certificates \
        clang \
        ccache \
        cmake \
        curl \
        git \
        gnupg \
        libcurl4-openssl-dev \
        libglfw3-dev \
        libicu-dev \
        libjpeg-turbo8-dev \
        libpng-dev \
        libsqlite3-dev \
        libuv1-dev \
        libwebp-dev \
        ninja-build \
        pkg-config \
        sudo \
        wget \
        xvfb \
        xauth && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY ./src /app/src
COPY ./maplibre-native /app/maplibre-native
COPY ./CMakeLists.txt /app/CMakeLists.txt


RUN cmake -B build -GNinja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DMLN_WITH_CLANG_TIDY=OFF \
    -DMLN_WITH_COVERAGE=OFF \
    -DMLN_DRAWABLE_RENDERER=ON \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON

RUN cd build && ninja -j $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null)


FROM ubuntu:22.04 as runner

WORKDIR /app

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates \
        libglfw3 \
        libicu70 \
        libjpeg-turbo8 \
        libpng16-16 \
        libsqlite3-0 \
        libuv1 \
        libwebp7 \
        libcurl4-openssl-dev \
        libglfw3-dev \
        libicu-dev \
        libjpeg-turbo8-dev \
        libpng-dev \
        libsqlite3-dev \
        libuv1-dev \
        libwebp-dev \
        libopengl0 \
        xvfb \
        xauth && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/bin/tilerender /app/tilerender
RUN chmod +x /app/tilerender

ENV PATH="/app:${PATH}"
ENV DISPLAY=:99

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

RUN mkdir -p /data
WORKDIR /data

ENTRYPOINT ["/entrypoint.sh", "tilerender"]

