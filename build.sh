#!/bin/bash
set -e

REGISTRY=$1
NAME=$(cat package.json | jq -r '.name')
TAG=$(cat package.json | jq -r '.version')
IMAGE=$REGISTRY/$NAME:$TAG

docker build . --platform linux/amd64 --secret id=npmrc,src=$HOME/.npmrc -t $IMAGE
docker push $IMAGE