#!/bin/bash

set -e
cd $(dirname $0)
cd ..

OSTYPE=$(uname)

if [ "${OSTYPE}" != "Darwin" ]; then
    echo "[obs-ssp - Error] macOS package script can be run on Darwin-type OS only."
    exit 1
fi

echo "[obs-ssp] Preparing package build"
export QT_CELLAR_PREFIX="$(/usr/bin/find /usr/local/Cellar/qt -d 1 | sort -t '.' -k 1,1n -k 2,2n -k 3,3n | tail -n 1)"

GIT_HASH=$(git rev-parse --short HEAD)
GIT_BRANCH_OR_TAG=$(git name-rev --name-only HEAD | awk -F/ '{print $NF}')

VERSION="$GIT_HASH-$GIT_BRANCH_OR_TAG"

FILENAME_UNSIGNED="obs-ssp-$VERSION-Unsigned.pkg"
FILENAME="obs-ssp-$VERSION.pkg"

echo "[obs-ssp] Modifying obs-ssp.so"
install_name_tool \
	-change /tmp/obsdeps/lib/QtWidgets.framework/Versions/5/QtWidgets \
		@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets \
	-change /tmp/obsdeps/lib/QtGui.framework/Versions/5/QtGui \
		@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui \
	-change /tmp/obsdeps/lib/QtCore.framework/Versions/5/QtCore \
		@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore \
	-change /tmp/obsdeps/lib/QtNetwork.framework/Versions/5/QtNetwork \
		@executable_path/../Frameworks/QtNetwork.framework/Versions/5/QtNetwork \
	-change /tmp/obsdeps/lib/libavcodec.58.dylib \
		@executable_path/../Frameworks/libavcodec.58.dylib \
	-change /tmp/obsdeps/lib/libavutil.56.dylib \
		@executable_path/../Frameworks/libavutil.56.dylib \
	./build/obs-ssp.so

# Check if replacement worked
echo "[obs-ssp] Dependencies for obs-ssp"
otool -L ./build/obs-ssp.so

if [[ "$RELEASE_MODE" == "True" ]]; then
	echo "[obs-ssp] Signing plugin binary: obs-ssp.so"
	codesign --sign "$CODE_SIGNING_IDENTITY" ./build/obs-ssp.so
else
	echo "[obs-ssp] Skipped plugin codesigning"
fi

if [ ! -f "installer/libssp.dylib" ]; then
  wget -c --retry-connrefused --waitretry=1 -P installer \
    https://github.com/imaginevision/libssp/raw/fa0affd3858049a7773995c38bb454f989e7c8f4/lib/mac/libssp.dylib
fi

echo "[obs-ssp] Actual package build"
packagesbuild ./installer/obs-ssp.pkgproj

echo "[obs-ssp] Renaming obs-ssp.pkg to $FILENAME"
mkdir -p release
mv ./installer/build/obs-ssp.pkg ./release/$FILENAME_UNSIGNED

if [[ "$RELEASE_MODE" == "True" ]]; then
	echo "[obs-ssp] Signing installer: $FILENAME"
	productsign \
		--sign "$INSTALLER_SIGNING_IDENTITY" \
		./release/$FILENAME_UNSIGNED \
		./release/$FILENAME
	rm ./release/$FILENAME_UNSIGNED

	echo "[obs-ssp] Submitting installer $FILENAME for notarization"
	zip -r ./release/$FILENAME.zip ./release/$FILENAME
	UPLOAD_RESULT=$(xcrun altool \
		--notarize-app \
		--primary-bundle-id "fr.palakis.obs-ssp" \
		--username "$AC_USERNAME" \
		--password "$AC_PASSWORD" \
		--asc-provider "$AC_PROVIDER_SHORTNAME" \
		--file "./release/$FILENAME.zip")
	rm ./release/$FILENAME.zip

	REQUEST_UUID=$(echo $UPLOAD_RESULT | awk -F ' = ' '/RequestUUID/ {print $2}')
	echo "Request UUID: $REQUEST_UUID"

	echo "[obs-ssp] Wait for notarization result"
	# Pieces of code borrowed from rednoah/notarized-app
	while sleep 30 && date; do
		CHECK_RESULT=$(xcrun altool \
			--notarization-info "$REQUEST_UUID" \
			--username "$AC_USERNAME" \
			--password "$AC_PASSWORD" \
			--asc-provider "$AC_PROVIDER_SHORTNAME")
		echo $CHECK_RESULT

		if ! grep -q "Status: in progress" <<< "$CHECK_RESULT"; then
			echo "[obs-ssp] Staple ticket to installer: $FILENAME"
			xcrun stapler staple ./release/$FILENAME
			break
		fi
	done
else
	echo "[obs-ssp] Skipped installer codesigning and notarization"
fi
