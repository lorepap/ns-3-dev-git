#!/bin/sh

NAME=ns3-dev-latest
RESULTS_DIR=/student/lpappone/ns3-simulations-anomaly
TARGET_RESULTS_DIR=/home/ns-3-dev/simulations
SOURCE_DIR=/student/lpappone/research-projects/ns-3-dev-git-1
TARGET_DIR=/home/ns-3-dev
IMAGE=gcc:latest

docker run -d -it --name $NAME \
--mount type=bind,source="$SOURCE_DIR",target=$TARGET_DIR \
--mount type=bind,source="$RESULTS_DIR",target=$TARGET_RESULTS_DIR $IMAGE
