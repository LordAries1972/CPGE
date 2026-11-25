# Docker Build

How to build and run the CPGE Linux build in a Docker container, and what is
known about it. The Dockerfile lives at the repo root; the build context
rules live in `.dockerignore`.

## What it builds

The multi-stage `Dockerfile` builds the Linux build (`Linux/CMakeLists.txt`
— the root `CMakeLists.txt` is Windows-only and will not compile on Linux)
into a minimal runtime image:

- **Builder stage** (`ubuntu:22.04`): installs the build tools and the `-dev`
  libraries, copies the source tree, configures with
  `cmake -S Linux -B build-linux-<Renderer> -DRENDERER=<Renderer>` and builds
  in Release. Output binary is `OpenGLCPGE` or `VulkanCPGE`.
- **Runtime stage** (`ubuntu:22.04`): only the runtime libraries, the built
  binary (as `/app/game`), and `Assets/` (as `/app/Assets`), run as the
  non-root user `gameuser`.

Build the OpenGL renderer (the default):

```sh
docker build -t cpge-opengl .
```

Build the Vulkan renderer instead:

```sh
docker build --build-arg RENDERER=Vulkan -t cpge-vulkan .
```

Run it (needs an X display — the engine creates a window; without one it
fails gracefully after initialisation, same as a host build):

```sh
docker run --rm -e DISPLAY="$DISPLAY" -v /tmp/.X11-unix:/tmp/.X11-unix cpge-opengl
```

## What the image contains, and why

- **GLM is vendored, not installed.** GLM 0.9.9.8 lives in `include/glm/`
  (managed by Daniel — replace `include/glm/` to update it) and
  `Linux/CMakeLists.txt` fails the configure with a `FATAL_ERROR` if
  `include/glm/glm.hpp` is missing. The Dockerfile therefore does NOT
  install `libglm-dev`, and `.dockerignore` must NOT exclude `include/`
  (see the 25 November 2026 fix below).
- **FFmpeg and eSpeak-ng are installed** (builder `-dev` packages, runtime
  shared libraries). They are optional in `CMakeLists.txt`, but without them
  the image would build with MoviePlayer / ScreenRecorder / TTS silently
  disabled, unlike a host build — so they are installed for feature parity.
  The runtime sonames are the Ubuntu 22.04 (FFmpeg 4.4) ones
  (`libavformat58`, `libavcodec58`, `libavutil56`, `libswscale5`,
  `libswresample3`, `libespeak-ng1`).
- **What `.dockerignore` leaves out:** `build/` and `build-android/` at any
  depth (live build trees, ~1.6 GB), the `Android/` Gradle project and APKs
  (~700 MB — the Linux build never touches them), `steam/` (Steam
  integration is not implemented in the tree — see Docs/About-CPGE.md
  §38), `Docs/`, `lib/` (Windows-only `.lib`/`.dll`
  files), and recording outputs (`Assets/Playbacks/*.mp4`,
  `Assets/recording_*.mp4`). It deliberately does NOT exclude the shipped
  movies: `Assets/cpge.mp4` (the SCENE_INTRO FOSS credit movie) and
  `Assets/test1.mp4` must ship in the image.

## Verification status (25 November 2026)

**Not yet verified with a real `docker build`.** The Dockerfile was
rewritten for the Linux build earlier and never built. The Phase C
verification attempt (25 November 2026, `scope/PHASE_C_DOCKER_VERIFICATION.md`)
got this far on the Linux build VM (Ubuntu 24.04, 4 CPUs, ~6 GB RAM):

- Docker was not installed. `docker.io` 29.1.3 installed cleanly from apt
  (`apt-get install -y docker.io`; `docker --version` → `29.1.3`).
- The stock daemon does NOT start in this VM: `systemctl start docker`
  fails because `dockerd` cannot set up its bridge network — the VM denies
  the netfilter calls (`iptables --wait -t nat -N DOCKER` →
  `Could not fetch rule set generation id: Permission denied (you must be root)`,
  despite running as root). The daemon starts fine with networking
  disabled: `dockerd --data-root <dir> --iptables=false --bridge=none
  --ip-forward=false` (storage driver overlayfs, BuildKit initialises,
  `docker info` answers). With no bridge, build containers have no network
  unless the build is run with `--network=host`.
- The image pull is the current blocker: `docker pull ubuntu:22.04` fails
  every time with `net/http: TLS handshake timeout` against
  `registry-1.docker.io`, even though the same host reaches the registry
  through the egress proxy in under a second with curl (HTTP 401, the
  expected unauthenticated answer) and the daemon was started with the
  proxy variables in its environment. The base image therefore cannot be
  fetched in this VM, so no `docker build` has run yet.
- Two real defects in the Dockerfile / `.dockerignore` were found by reading
  the files against the current build and fixed in the same pass (see below).
  They would both have broken a real build, so the Dockerfile is in a better
  state than before the attempt — but it is still unverified end to end.

## Fixes made 25 November 2026 (Phase C Dockerfile verification)

1. **`.dockerignore` excluded `include/` — the build could never configure.**
   GLM is vendored in `include/glm/` and `Linux/CMakeLists.txt` fails fast
   when `include/glm/glm.hpp` is absent, so excluding `include/` from the
   build context guaranteed a configure-time `FATAL_ERROR`. The `include/`
   line is removed (with a comment explaining why it must never come back);
   `lib/` stays excluded — it is Windows-only.
2. **`.dockerignore` excluded `*.mp4` — the intro movie would not ship.**
   `Assets/cpge.mp4` (SCENE_INTRO, the FOSS credit movie) and
   `Assets/test1.mp4` are shipped game assets, not recordings. The blanket
   exclusion is replaced with recording-only patterns
   (`Assets/Playbacks/*.mp4`, `Assets/recording_*.mp4`, `recording.mp4`).
3. **Context bloat:** `build-android/` (847 MB, not matched by the old
   `build/` pattern), the `Android/` project and its APKs (~700 MB, four
   APKs ~520 MB), and `*.apk` / `*.idsig` are now excluded. The Linux build
   never uses any of them.
4. **`Dockerfile`: `libglm-dev` removed** (dead weight — the vendored copy is
   authoritative and CMake never looks for a system GLM on Linux);
   **FFmpeg `-dev` packages and `libespeak-ng-dev` / `espeak-ng` added** to
   the builder stage and the matching runtime libraries to the runtime stage,
   for feature parity with a host build (otherwise movies, recording and TTS
   would be silently disabled in the image).

## How to verify (on a machine where Docker Hub is reachable)

```sh
docker build -t cpge-opengl .
docker build --build-arg RENDERER=Vulkan -t cpge-vulkan .
```

A successful build ends with both images built and the runtime image
containing `/app/game` plus `/app/Assets` (check the intro movie is there:
`docker run --rm cpge-opengl ls -l /app/Assets/cpge.mp4`). If the build
fails at configure with `Vendored GLM headers not found`, `.dockerignore`
is excluding `include/` again. On this build VM, use the flags above
(`--iptables=false --bridge=none`, `--network=host` for the build) and a
`--data-root` on the large disk — the default `/var/lib/docker` sits on a
7.5 GB root filesystem.

## Known limits

- No `docker build` has ever succeeded for this Dockerfile — on any machine.
  The first successful build is still outstanding (Daniel's machine or CI).
- The runtime image has no X server in it; running the game needs a display
  forwarded in (see the `docker run` example above) or an Xvfb session.
- The engine binary is Release; there is no debug-image variant.
- The image is `ubuntu:22.04`-based even though the host build VM runs
  Ubuntu 24.04 — the Dockerfile pins 22.04 deliberately, so a host glibc /
  toolchain upgrade cannot silently change the image.
