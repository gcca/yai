FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential g++ ninja-build libssl-dev libbz2-dev libicu-dev python3-dev wget \
    python3-dev python3-pip python3-venv \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /

RUN wget https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-b2-nodocs.tar.gz && \
    tar -xzf boost-1.87.0-b2-nodocs.tar.gz && \
    cd boost-1.87.0 && \
    ./bootstrap.sh --with-toolset=gcc --prefix=/boost_install && \
    ./b2 -j$(nproc) toolset=gcc cxxstd=23 link=shared threading=multi variant=release --with-system --with-json && \
    ./b2 install --prefix=/boost_install && \
    rm -rf boost-1.87.0-b2-nodocs.tar.gz boost-1.87.0

WORKDIR /app

COPY yai_ext/ ./yai_ext/
COPY setup.py .

ENV BOOST_ROOT=/boost_install

RUN python3 -m venv /venv \
    && . /venv/bin/activate \
    && pip install setuptools wheel \
    && python3 setup.py build_ext --inplace

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    python3 \
    python3-pip \
    python3-venv \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

COPY --from=builder /app/yai_chat*.so /app
COPY --from=builder /venv /venv
COPY --from=builder /boost_install /boost

RUN . /venv/bin/activate \
    && pip install poetry \
    && poetry config virtualenvs.create false
RUN . /venv/bin/activate && poetry install --without dev

EXPOSE 8000

ENTRYPOINT ["/app/scripts/entrypoint"]
