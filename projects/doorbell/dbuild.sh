#!/bin/bash

function check_sdk_dir() {
    if [ ! -f $1/tools/build_tools/docker_build/dbuild.sh ]; then
        echo "sdk directory not found: $1, please set SDK_DIR environment variable"
        exit 1
    fi
}


function main() {
    SDK_DIR=${SDK_DIR:-$(realpath ../..)}

    CURRENT_DIR=$(realpath .)
    SOLUTION_DIR=${SOLUTION_DIR:-}
    if [ -z "$SOLUTION_DIR" ]; then
        echo "solution directory not found, please set SOLUTION_DIR environment variable"
        exit 1
    fi
    PROJECT_DIR=$(realpath --relative-to="$SOLUTION_DIR" "$CURRENT_DIR")
    if [ "$PROJECT_DIR" == "." ]; then
        PROJECT_DIR=
    fi
    export SOLUTION_DIR=$(realpath $SOLUTION_DIR)
    export PROJECT_DIR=$PROJECT_DIR
    DOCKER_BUILD_SRCIPT=${SDK_DIR}/dbuild.sh

    check_sdk_dir $SDK_DIR

    $DOCKER_BUILD_SRCIPT $@
}

export BK_SOLUTION_MODE=1

main $@
