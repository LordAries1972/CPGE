#!/bin/bash
# Docker build and run script for Cross-Platform Game Engine
# Usage: ./docker-build.sh [build|run|compose]

set -e

IMAGE_NAME="cpge-game-engine"
IMAGE_TAG="latest"
CONTAINER_NAME="cpge-game"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_usage() {
    echo "Cross-Platform Game Engine Docker Build Helper"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  build          Build the Docker image"
    echo "  run            Run the game engine in a container"
    echo "  compose-up     Start services with docker-compose"
    echo "  compose-down   Stop services with docker-compose"
    echo "  shell          Open a shell in the container (for debugging)"
    echo "  logs           View logs from the running container"
    echo "  clean          Remove the image and stopped containers"
    echo ""
    echo "Examples:"
    echo "  $0 build"
    echo "  $0 run"
    echo "  $0 compose-up"
}

build() {
    echo -e "${YELLOW}Building Docker image: ${IMAGE_NAME}:${IMAGE_TAG}${NC}"
    docker build \
        -t "${IMAGE_NAME}:${IMAGE_TAG}" \
        -f Dockerfile \
        --progress=plain \
        .
    echo -e "${GREEN}✓ Build complete${NC}"
}

run() {
    echo -e "${YELLOW}Running container: ${CONTAINER_NAME}${NC}"
    docker run \
        --rm \
        -it \
        --name "${CONTAINER_NAME}" \
        -e LIBGL_ALWAYS_INDIRECT=1 \
        -v "$(pwd)/Assets:/app/Assets:ro" \
        "${IMAGE_NAME}:${IMAGE_TAG}"
}

compose_up() {
    echo -e "${YELLOW}Starting services with docker-compose${NC}"
    docker compose up -d
    echo -e "${GREEN}✓ Services started${NC}"
    docker compose ps
}

compose_down() {
    echo -e "${YELLOW}Stopping services with docker-compose${NC}"
    docker compose down
    echo -e "${GREEN}✓ Services stopped${NC}"
}

shell() {
    echo -e "${YELLOW}Opening shell in container${NC}"
    docker run \
        --rm \
        -it \
        -v "$(pwd)/Assets:/app/Assets:ro" \
        "${IMAGE_NAME}:${IMAGE_TAG}" \
        /bin/bash
}

logs() {
    echo -e "${YELLOW}Container logs (most recent):${NC}"
    docker logs -f "${CONTAINER_NAME}" 2>/dev/null || echo "Container '${CONTAINER_NAME}' not found. Use 'docker ps' to see running containers."
}

clean() {
    echo -e "${YELLOW}Cleaning up Docker artifacts${NC}"
    docker compose down 2>/dev/null || true
    docker stop "${CONTAINER_NAME}" 2>/dev/null || true
    docker rmi "${IMAGE_NAME}:${IMAGE_TAG}" 2>/dev/null || echo "Image not found"
    echo -e "${GREEN}✓ Cleanup complete${NC}"
}

# Main
case "${1:-}" in
    build)
        build
        ;;
    run)
        build && run
        ;;
    compose-up)
        compose_up
        ;;
    compose-down)
        compose_down
        ;;
    shell)
        shell
        ;;
    logs)
        logs
        ;;
    clean)
        clean
        ;;
    *)
        print_usage
        exit 0
        ;;
esac
