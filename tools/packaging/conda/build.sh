#! /bin/bash

set -x

install -D /usr/bin/realpath $BUILD_PREFIX/bin/realpath
install -D /bin/sed $BUILD_PREFIX/bin/sed

# Install gtk-sharp2
install -D /usr/lib/cli/gtk-sharp-2.0/gtk-sharp.dll* $BUILD_PREFIX/lib/mono/4.5-api/
install -D /usr/lib/cli/glib-sharp-2.0/glib-sharp.dll* $BUILD_PREFIX/lib/mono/4.5-api/
install -D /usr/lib/cli/atk-sharp-2.0/atk-sharp.dll* $BUILD_PREFIX/lib/mono/4.5-api/
install -D /usr/lib/cli/gdk-sharp-2.0/gdk-sharp.dll* $BUILD_PREFIX/lib/mono/4.5-api/
install -D /usr/lib/cli/pango-sharp-2.0/pango-sharp.dll* $BUILD_PREFIX/lib/mono/4.5-api/


mkdir -p $PREFIX/opt/renode/bin
cp /usr/lib/cli/gtk-sharp-2.0/gtk-sharp.dll* $PREFIX/opt/renode/bin/
cp /usr/lib/cli/glib-sharp-2.0/glib-sharp.dll* $PREFIX/opt/renode/bin/
cp /usr/lib/cli/atk-sharp-2.0/atk-sharp.dll* $PREFIX/opt/renode/bin/
cp /usr/lib/cli/gdk-sharp-2.0/gdk-sharp.dll* $PREFIX/opt/renode/bin/
cp /usr/lib/cli/pango-sharp-2.0/pango-sharp.dll* $PREFIX/opt/renode/bin/

mkdir -p $PREFIX/lib/
install -D /usr/lib/cli/gtk-sharp-2.0/libgtksharpglue-2.so $PREFIX/lib/libgtksharpglue-2.so
install -D /usr/lib/cli/gdk-sharp-2.0/libgdksharpglue-2.so $PREFIX/lib/libgdksharpglue-2.so
install -D /usr/lib/cli/glib-sharp-2.0/libglibsharpglue-2.so $PREFIX/lib/libglibsharpglue-2.so
install -D /usr/lib/x86_64-linux-gnu/libdl.so $PREFIX/lib/libdl.so
install -D /usr/lib/x86_64-linux-gnu/gtk-2.0/modules/libatk-bridge.so $PREFIX/lib/libatk-bridge.so

sed -i 's/\/usr\/lib\/cli\/.*-sharp-2.0\///g' $PREFIX/opt/renode/bin/*.dll.config

./build.sh

mkdir -p $PREFIX/opt/renode/bin
mkdir -p $PREFIX/opt/renode/scripts
mkdir -p $PREFIX/opt/renode/platforms
mkdir -p $PREFIX/opt/renode/tests
mkdir -p $PREFIX/opt/renode/licenses


cp .renode-root $PREFIX/opt/renode/
cp -r output/bin/Release/* $PREFIX/opt/renode/bin/
cp -r scripts/* $PREFIX/opt/renode/scripts/
cp -r platforms/* $PREFIX/opt/renode/platforms/
cp -r tests/* $PREFIX/opt/renode/tests/

#copy the licenses
#some files already include the library name
find ./src/Infrastructure/src/Emulator ./lib ./tools/packaging/macos -iname "*-license" -exec cp {} $PREFIX/opt/renode/licenses/ \;

#others will need a parent directory name.
find ./src/Infrastructure ./lib/resources -iname "license" -print0 |\
    while IFS= read -r -d $'\0' file
do
    full_dirname=${file%/*}
    dirname=${full_dirname##*/}
    cp $file $PREFIX/opt/renode/licenses/$dirname-license
done

mkdir -p $PREFIX/bin/

echo -e '#!/bin/bash\n\nmono $MONO_OPTIONS $CONDA_PREFIX/opt/renode/bin/Renode.exe "$@"' > $PREFIX/bin/renode
