#!/bin/bash
set -e

REGISTRY=$1
NAME=$(cat server/server.json | jq -r '.name')
TAG=$(cat server/server.json | jq -r '.version')
IMAGE=$REGISTRY/$NAME:$TAG

docker build . --platform linux/amd64 -t $IMAGE
docker push $IMAGE
#docker tag $IMAGE $REGISTRY/$NAME:latest
#docker push $REGISTRY/$NAME:latest