#!/bin/sh

OSTYPE=$(uname)
cd $(dirname $0)
cd ..

if [ "${OSTYPE}" != "Darwin" ]; then
    echo "[obs-ssp - Error] macOS install dependencies script can be run on Darwin-type OS only."
    exit 1
fi

HAS_BREW=$(type brew 2>/dev/null)

if [ "${HAS_BREW}" = "" ]; then
    echo "[obs-ssp - Error] Please install Homebrew (https://www.brew.sh/) to build obs-ssp on macOS."
    exit 1
fi

# OBS Studio deps
echo "[obs-ssp] Updating Homebrew.."
brew update >/dev/null

echo "[obs-ssp] Checking installed Homebrew formulas.."
BREW_PACKAGES=$(brew list)
BREW_DEPENDENCIES="jack speexdsp ccache swig mbedtls"

for DEPENDENCY in ${BREW_DEPENDENCIES}; do
    if echo "${BREW_PACKAGES}" | grep -q "^${DEPENDENCY}\$"; then
        echo "[obs-ssp] Upgrading OBS-Studio dependency '${DEPENDENCY}'.."
        brew upgrade ${DEPENDENCY} 2>/dev/null
    else
        echo "[obs-ssp] Installing OBS-Studio dependency '${DEPENDENCY}'.."
        brew install ${DEPENDENCY} 2>/dev/null
    fi
done

# qt deps

install_qt_deps() {
    echo "[obs-ssp] Installing obs-ssp dependency 'QT 5.14.1'.."
    echo "Download..."
    curl --progress-bar -L -C - -O https://github.com/obsproject/obs-deps/releases/download/${2}/macos-qt-${1}-${2}.tar.gz
    echo "Unpack..."
    tar -xf ./macos-qt-${1}-${2}.tar.gz -C /tmp
    xattr -r -d com.apple.quarantine /tmp/obsdeps
}

install_qt_deps 5.14.1 2020-08-30

install_obs_deps() {
    echo "Setting up pre-built macOS OBS dependencies v${1}"
    echo "Download..."
    curl --progress-bar -L -C - -O https://github.com/obsproject/obs-deps/releases/download/${1}/macos-deps-${1}.tar.gz
    echo "Unpack..."
    tar -xf ./macos-deps-${1}.tar.gz -C /tmp
}

install_obs_deps 2020-08-30

# Fetch and install Packages app
# =!= NOTICE =!=
# Installs a LaunchDaemon under /Library/LaunchDaemons/fr.whitebox.packages.build.dispatcher.plist
# =!= NOTICE =!=

HAS_PACKAGES=$(type packagesbuild 2>/dev/null)

if [ "${HAS_PACKAGES}" = "" ]; then
    echo "[obs-ssp] Installing Packaging app (might require password due to 'sudo').."
    curl -o './Packages.pkg' --retry-connrefused -s --retry-delay 1 'https://s3-us-west-2.amazonaws.com/obs-nightly/Packages.pkg'
    sudo installer -pkg ./Packages.pkg -target /
fi
