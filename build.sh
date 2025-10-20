#!/bin/bash
set -e

NAME=$(cat package.json | jq -r '.name')
TAG=$(cat package.json | jq -r '.version')
IMAGE=europe-docker.pkg.dev/smartness-artifact-registry/docker/$NAME:$TAG

echo "Building image $IMAGE"
docker build . --platform linux/amd64 --secret id=npmrc,src=$HOME/.npmrc -t $IMAGE
echo "Built image $IMAGE"

echo "Pushing image $IMAGE"
docker push $IMAGE
echo "Pushed image $IMAGE"
