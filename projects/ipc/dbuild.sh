#!/bin/bash

export BK_SOLUTION_MODE=1
export SOLUTION_DIR=$(realpath ../..)

function check_sdk_dir() {
    if [ ! -f $1/tools/build_tools/docker_build/dbuild.sh ]; then
        echo "sdk directory not found: $1, please set SDK_DIR environment variable"
        exit 1
    fi
}


function main() {
    SDK_DIR=${SDK_DIR:-$(realpath ../..)}

    CURRENT_DIR=$(realpath .)
    PROJECT_DIR=$(realpath --relative-to="$SOLUTION_DIR" "$CURRENT_DIR")
    if [ "$PROJECT_DIR" == "." ]; then
        PROJECT_DIR=
    fi
    
    export PROJECT_DIR=$PROJECT_DIR
    DOCKER_BUILD_SRCIPT=${SDK_DIR}/dbuild.sh

    check_sdk_dir $SDK_DIR

    $DOCKER_BUILD_SRCIPT $@
}

main $@
