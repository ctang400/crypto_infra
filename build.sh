#!/bin/bash
COLOR_RESET='\e[0m';
COLOR_RED='\e[1;31m';
PROGNAME=${0##*/}
CURDIR="$( cd "$(dirname "$0")"; pwd -P)"
BUILD_TYPE="Release"
BUILD_DIR="build"
LIBNANO_SOURCE=""

if which nproc; then
    # Linux
    CORES="$(nproc --all)"
elif which sysctl; then
    # MacOS
    CORES="$(sysctl -n hw.logicalcpu)"
else
    CORES=2
fi
JOBS=$(($CORES))

usage() {

cat <<EOF

  Usage: $PROGNAME [options]

  Options:

    -h, --help           Display this help and exit
    -c, --clean          Clean build
    -d, --debug          Build with debug mode
    -j, --jobs           Use N cores to build
    -t, --test           Build and run all unit tests
    --coverage           Generate coverage report
    --libnano <path>    Build using provided libnano source code path

EOF
}

clean() {
    rm -rf "build/" "build_d/" ".coverage/" "coverage.info"
}

test() {
    printf "\0llll;31m Test command not implemented.\n\033[0m"
    exit 1
}


while (( "$#" )); do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            BUILD_DIR="build_d"
            shift
            ;;
        -c|--clean)
            clean
            shift
            ;;
        -t|--test)
            BUILD_TYPE="Debug"
            BUILD_DIR="build_d"
            test="True"
            shift
            ;;
        --coverage)
            coverage="True"
            shift
            ;;
        #--libnano)
        #   LIBNANO_SOURCE="$2"
        #    shift 2
        #    ;;
        -*|--*=)
            echo "Invalid arguments"
            exit 1
            ;;
        *)
            break
            ;;
    esac
done

BUILD_OPTIONS="-DCMAKE_EXPORT_COMPILE_COMMANDS=YES -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCONDA_PREFIX=${CONDA_PREFIX}"
if [ "${LIBNANO_SOURCE}" != "" ]; then
    if [ ! -f "${BUILD_DIR}/DLIBNANO_SOURCE" ]; then
        echo -e "${COLOR_RED}Perform clean since build source changed.${COLOR_RESET}"
        clean
    fi
    BUILD_OPTIONS="${BUILD_OPTIONS} -DLIBNANO_SOURCE=${LIBNANO_SOURCE}"
    mkdir -p "${BUILD_DIR}"
    touch "${BUILD_DIR}/DLIBNANO_SOURCE"
else
    if [ -f "${BUILD_DIR}/DLIBNANO_SOURCE" ]; then
        echo -e "${COLOR_RED}Perform clean since build source changed.${COLOR_RESET}"
        clean
    fi
fi

if [ ! -z "${CONDA_PREFIX}" ]; then
    export CXX=$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-g++
    export CC=$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-gcc
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
if cmake .. ${BUILD_OPTIONS}; then
    if ! make -j "${JOBS}"; then
        exit 1
    fi

    if [ "${test}" == "True" ]; then
        test $coverage
    fi
    cd "$CURDIR"
else
    cd "$CURDIR"
    exit 1
fi