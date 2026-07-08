# Multi-stage Dockerfile for Cross-Platform Game Engine
# Optimized for build caching and runtime efficiency
# Build stage: compile the game engine with OpenGL support for Linux

# ─────────────────────────────────────────────────────────────────────────────
# BUILD STAGE
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:22.04 as builder

ENV DEBIAN_FRONTEND=noninteractive

# Layer 1: Install base build tools (rarely changes)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Layer 2: Install development libraries (development dependencies layer)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgl1-mesa-dev \
    libglew-dev \
    libglfw3-dev \
    libvulkan-dev \
    glslang-tools \
    libassimp-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    libogg-dev \
    libvorbis-dev \
    libportaudio-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Layer 3: Copy build configuration files early (stable, cached)
COPY CMakeLists.txt .
COPY cmake/ ./cmake/ 2>/dev/null || true

# Layer 4: Copy headers and static data (stable, cached if unchanged)
COPY include/ ./include/ 2>/dev/null || true
COPY *.h *.hpp ./ 2>/dev/null || true
COPY Assets/ ./Assets/ 2>/dev/null || true

# Layer 5: Configure CMake (cached until CMakeLists or includes change)
RUN mkdir -p build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DRENDERER=OpenGL \
    -DCMAKE_INSTALL_PREFIX=/usr/local 2>&1 | grep -E "(Configuring|Renderer|Game name|Output exe|Vulkan|OpenGL)"

# Layer 6: Copy source code (most likely to change frequently)
COPY *.cpp *.c ./ 2>/dev/null || true
COPY *.rc ./ 2>/dev/null || true
COPY D3D12/ ./D3D12/ 2>/dev/null || true
COPY Linux/ ./Linux/ 2>/dev/null || true
COPY Metal/ ./Metal/ 2>/dev/null || true

# Layer 7: Compile the project (invalidates only on source changes)
RUN cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DRENDERER=OpenGL && \
    make -j$(nproc) 2>&1 | tail -30

# ─────────────────────────────────────────────────────────────────────────────
# RUNTIME STAGE: Minimal image with only what's needed to run the binary
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install only essential runtime libraries (minimal image)
RUN apt-get update && apt-get install -y --no-install-recommends \
    # OpenGL runtime
    libgl1-mesa-glx \
    libglew2.2 \
    libglfw3 \
    # Vulkan runtime (optional backup)
    libvulkan1 \
    # Audio/Multimedia runtime
    libogg0 \
    libvorbis0a \
    libportaudio2 \
    # Model support runtime
    libassimp5 \
    # Networking/crypto
    libssl3 \
    libcurl4 \
    # System libraries for GUI (X11 forwarding support)
    libxrender1 \
    libxrandr2 \
    # Utilities
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binary from builder
COPY --from=builder /build/build/OpenGLCPGE /app/game
COPY --from=builder /build/Assets /app/Assets

# Create non-root user for security
RUN useradd -m -u 1000 gameuser && chown -R gameuser:gameuser /app

USER gameuser

# Expose port for game networking features
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD [ -x /app/game ] && echo "ok" || exit 1

# Run the game engine
ENTRYPOINT ["/app/game"]
CMD []
