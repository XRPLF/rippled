set -e

mkdir -p build/docker/
cp cfg/rippled-example.cfg build/clang.debug/rippled build/docker/
cp Builds/Docker/Dockerfile-testnet build/docker/Dockerfile
mv build/docker/rippled-example.cfg build/docker/rippled.cfg
strip build/docker/rippled
docker build -t ripple/rippled:$CIRCLE_SHA1 build/docker/
docker tag ripple/rippled:$CIRCLE_SHA1 ripple/rippled:latest

if [ -z "$CIRCLE_PR_NUMBER" ]; then
  docker tag ripple/rippled:$CIRCLE_SHA1 ripple/rippled:$CIRCLE_BRANCH
fi
