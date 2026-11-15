#!/bin/sh
# Build the Debian Trixie test image and run the smoke tests in a container.
#
#   ./docker/test-trixie.sh              # cached build
#   ./docker/test-trixie.sh --no-cache   # force fresh build
#   ./docker/test-trixie.sh --shell      # interactive shell in the container
set -e
cd "$(dirname "$0")/.."

IMAGE=evidence-trixie-test
BUILD_ARGS=""
RUN_ARGS=""
for a in "$@"; do
  case "$a" in
    --no-cache) BUILD_ARGS="--no-cache" ;;
    --shell)    RUN_ARGS="-it --entrypoint /bin/bash" ;;
    *) echo "unknown option: $a" >&2; exit 1 ;;
  esac
done

docker build $BUILD_ARGS -f docker/Dockerfile.trixie -t "$IMAGE" .
if [ -n "$RUN_ARGS" ]; then
  docker run --rm $RUN_ARGS "$IMAGE"
else
  docker run --rm "$IMAGE"
fi
