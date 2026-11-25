# Multi-stage Dockerfile for Cross-Platform Game Engine (Linux build)
# Uses the Linux build (Linux/CMakeLists.txt) — the root CMakeLists.txt is
# Windows-only (WIN32 executable, DirectX renderers, .rc resource) and will
# not compile on Linux. Builds the OpenGL renderer by default; pass
# --build-arg RENDERER=Vulkan for a Vulkan build.
# Optimized for build caching and runtime efficiency

# ─────────────────────────────────────────────────────────────────────────────
# BUILD STAGE
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:22.04 as builder

ARG RENDERER=OpenGL

ENV DEBIAN_FRONTEND=noninteractive

# Layer 1: Install base build tools (rarely changes)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Layer 2: Install development libraries (development dependencies layer)
# These are the libraries Linux/CMakeLists.txt actually links:
# OpenGL/GLEW, Vulkan + shaderc/glslang (SPIRV at build time), X11/xcb
# (windowing), ALSA (sound), plus assimp/ogg/vorbis/portaudio/ssl/curl
# used by the engine. (libglfw3-dev was removed: the Linux build does not use GLFW.)
# GLM is NOT installed here: it is vendored in include/glm/ (0.9.9.8,
# managed by Daniel) and Linux/CMakeLists.txt checks for that copy and
# never calls find_package(glm) on Linux — a system libglm-dev would be
# dead weight. FFmpeg and eSpeak-ng ARE installed: they are optional in
# CMakeLists.txt (SOFT deps in Linux/cmake-build.sh), but without them the
# image builds with MoviePlayer/ScreenRecorder/TTS silently disabled,
# unlike a host build — install them for feature parity.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgl1-mesa-dev \
    libglew-dev \
    libvulkan-dev \
    libshaderc-dev \
    glslang-tools \
    libx11-dev \
    libxcb1-dev \
    libx11-xcb-dev \
    libasound2-dev \
    libassimp-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    libogg-dev \
    libvorbis-dev \
    libportaudio-dev \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    libespeak-ng-dev \
    espeak-ng \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy the full source tree. The Linux build needs the whole tree: the
# sources live at the repo root, the Linux CMakeLists.txt in Linux/, and
# Assets/ at runtime — piecemeal COPY layers do not reproduce that layout.
COPY . .

# Configure + build with the Linux CMakeLists.txt.
# Output binary: build-linux-OpenGL/OpenGLCPGE (or build-linux-Vulkan/VulkanCPGE).
RUN cmake -S Linux -B "build-linux-${RENDERER}" \
        -DRENDERER="${RENDERER}" \
        -DCMAKE_BUILD_TYPE=Release && \
    cmake --build "build-linux-${RENDERER}" -j"$(nproc)" && \
    (cp "build-linux-${RENDERER}/OpenGLCPGE" /build/game-binary 2>/dev/null || \
     cp "build-linux-${RENDERER}/VulkanCPGE" /build/game-binary)

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
    # Vulkan runtime (optional backup)
    libvulkan1 \
    # Windowing runtime (X11 forwarding support)
    libx11-6 \
    libxcb1 \
    libxrender1 \
    libxrandr2 \
    # Audio/Multimedia runtime
    libasound2 \
    libogg0 \
    libvorbis0a \
    libportaudio2 \
    # Model support runtime
    libassimp5 \
    # Networking/crypto
    libssl3 \
    libcurl4 \
    # FFmpeg runtime (MoviePlayer/ScreenRecorder link these — the builder
    # stage installs the -dev packages, so the runtime image needs the
    # matching shared libraries or the game binary fails to start).
    # Sonames are the Ubuntu 22.04 (FFmpeg 4.4) ones.
    libavformat58 \
    libavcodec58 \
    libavutil56 \
    libswscale5 \
    libswresample3 \
    # eSpeak-ng runtime (TTSManager)
    libespeak-ng1 \
    # Utilities
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binary from builder (normalised to /build/game-binary in the
# build stage, since the name is OpenGLCPGE or VulkanCPGE by renderer)
COPY --from=builder /build/game-binary /app/game
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
