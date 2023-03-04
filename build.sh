#!/usr/bin/env bash

set -u
set -e

export ROOT_PATH="$(cd $(dirname $0); echo $PWD)"
OUTPUT_DIRECTORY="$ROOT_PATH/output"
EXPORT_DIRECTORY=""

CONFIGURATION="Release"
BUILD_PLATFORM="Any CPU"
CLEAN=false
PACKAGES=false
NIGHTLY=false
PORTABLE=false
TFM="net6.0"
PARAMS=()
CUSTOM_PROP=

function print_help() {
  echo "Usage: $0 [-cdvspnt] [-b properties-file.csproj]"
  echo ""
  echo "-c                                clean instead of building"
  echo "-d                                build Debug configuration"
  echo "-v                                verbose output"
  echo "-p                                create packages after building"
  echo "-n                                create nightly packages after building"
  echo "-t                                create a portable package (experimental, Linux only)"
  echo "-b                                custom build properties file"
  echo "-o                                custom output directory"
}

while getopts "cdvpnstbo:-:" opt
do
  case $opt in
    c)
      CLEAN=true
      ;;
    d)
      CONFIGURATION="Debug"
      ;;
    v)
      PARAMS+=(verbosity:detailed)
      ;;
    p)
      PACKAGES=true
      ;;
    n)
      NIGHTLY=true
      PACKAGES=true
      ;;
    t)
      PORTABLE=true
      ;;
    b)
      CUSTOM_PROP=$OPTARG
      ;;
    o)
      EXPORT_DIRECTORY=$OPTARG
      echo "Setting the output directory to $EXPORT_DIRECTORY"
      ;;
    -)
      case $OPTARG in
        *)
          print_help
          exit 1
          ;;
      esac
      ;;
    \?)
      print_help
      exit 1
      ;;
  esac
done
shift "$((OPTIND-1))"

if [ -n "${PLATFORM:-}" ]
then
    echo "PLATFORM environment variable is currently set to: >>$PLATFORM<<"
    echo "This might cause problems during the build."
    echo "Please clear it with:"
    echo ""
    echo "    unset PLATFORM"
    echo ""
    echo " and run the build script again."

    exit 1
fi


. "${ROOT_PATH}/tools/common.sh"

BUILD_TARGET=Headless
PARAMS+=(p:GUI_DISABLED=true)
TARGET="`get_path \"$PWD/Renode.sln\"`"
CURRENT_PATH=.

# Verify Mono and mcs version on Linux and macOS
if ! [ -x "$(command -v mcs)" ]
then
    MINIMUM_MONO=`get_min_mono_version`
    echo "mcs not found. Renode requries Mono $MINIMUM_MONO or newer. Please refer to documentation for installation instructions. Exiting!"
    exit 1
fi

verify_mono_version

# Copy properties file according to the running OS
mkdir -p "$OUTPUT_DIRECTORY"
if [ -n "${CUSTOM_PROP}" ]; then
    PROP_FILE=$CUSTOM_PROP
else
    PROP_FILE="$CURRENT_PATH/src/Infrastructure/src/Emulator/Cores/linux-properties.csproj"
fi
cp "$PROP_FILE" "$OUTPUT_DIRECTORY/properties.csproj"

# Assets files are not deleted during `dotnet clean`, as it would confuse intellisense per comment in https://github.com/NuGet/Home/issues/7368#issuecomment-457411014,
# but we need to delete them to build Renode again for .NETFramework since `project.assets.json` doesn't play well if project files share the same directory.
# If `Renode_NET.sln` is picked for OmniSharp, it will trigger reanalysis of the project after removing assets files.
# We don't remove these files as part of `clean` target, because other intermediate files are well separated between .NET and .NETFramework
# and enforcing `clean` every time before rebuilding would slow down the build process on both frameworks.
find $ROOT_PATH -type f -name 'project.assets.json' -delete

# Build CCTask in Release configuration
CCTASK_OUTPUT=`mktemp`
CCTASK_BUILD_ARGS=(p:Configuration=Release p:Platform="\"$BUILD_PLATFORM\"")
set +e
CCTASK_SLN=CCTask.sln

eval "$CS_COMPILER $(build_args_helper "${CCTASK_BUILD_ARGS[@]}") $(get_path $ROOT_PATH/lib/cctask/$CCTASK_SLN)" 2>&1 > $CCTASK_OUTPUT

if [ $? -ne 0 ]; then
    cat $CCTASK_OUTPUT
    rm $CCTASK_OUTPUT
    exit 1
fi
rm $CCTASK_OUTPUT
set -e

# clean instead of building
if $CLEAN
then
    PARAMS+=(t:Clean)
    for conf in Debug Release
    do
      for build_target in Headless
      do
        $CS_COMPILER $(build_args_helper ${PARAMS[@]}) $(build_args_helper p:Configuration=${conf}${build_target}) "$TARGET"
      done
      rm -fr $OUTPUT_DIRECTORY/bin/$conf
    done
    exit 0
fi

# check weak implementations of core libraries
pushd "$ROOT_PATH/tools/building" > /dev/null
./check_weak_implementations.sh
popd > /dev/null

PARAMS+=(p:Configuration=${CONFIGURATION}${BUILD_TARGET} p:GenerateFullPaths=true p:Platform="\"$BUILD_PLATFORM\"")

# build
eval "$CS_COMPILER $(build_args_helper "${PARAMS[@]}") $TARGET"

# copy llvm library
cp src/Infrastructure/src/Emulator/Peripherals/bin/$CONFIGURATION/libllvm-disas.* output/bin/$CONFIGURATION

# build packages after successful compilation
params=""

if [ $CONFIGURATION == "Debug" ]
then
    params="$params -d"
fi

if [ -n "$EXPORT_DIRECTORY" ]
then
    $ROOT_PATH/tools/packaging/export_${DETECTED_OS}_workdir.sh $EXPORT_DIRECTORY $params
    echo "Renode built to $EXPORT_DIRECTORY"
fi

if $PACKAGES
then
    if $NIGHTLY
    then
      params="$params -n"
    fi

    $ROOT_PATH/tools/packaging/make_${DETECTED_OS}_packages.sh $params
    $ROOT_PATH/tools/packaging/make_source_package.sh $params
fi

if $PORTABLE
then
    $ROOT_PATH/tools/packaging/make_linux_portable.sh $params
fi
