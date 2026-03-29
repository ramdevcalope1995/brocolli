# Multi-stage build for Brocolli
# Stage 1: Build environment
FROM ubuntu:22.04 as builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    cmake \
    pkg-config \
    libssl-dev \
    libcurl4-openssl-dev \
    libseccomp-dev \
    sqlite3 \
    libsqlite3-dev \
    python3 \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

# Clone and build Kore framework
WORKDIR /tmp
RUN git clone https://github.com/jorisvink/kore.git && \
    cd kore && \
    make && \
    make install

# Copy Brocolli source
COPY . /brocolli
WORKDIR /brocolli

# Build Brocolli
RUN make clean && make

# Stage 2: Runtime environment
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libcurl4 \
    libseccomp2 \
    sqlite3 \
    chromium-browser \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy compiled Brocolli from builder
COPY --from=builder /brocolli /app
COPY --from=builder /usr/local/lib/libkore* /usr/local/lib/
COPY --from=builder /usr/local/include/kore /usr/local/include/kore

# Create necessary directories
RUN mkdir -p /app/data /app/logs

# Expose ports
# 8888: Brocolli API
# 18800: QuantClaw WebSocket Gateway (if running locally)
# 18801: QuantClaw HTTP API / Dashboard
EXPOSE 8888 18800 18801

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8888/health || exit 1

# Default command
CMD ["kore", "-c", "conf/brocolli.conf"]
