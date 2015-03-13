#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "$BASH_SOURCE")" ; pwd)

# args: wrong filename, correct filename
# expects to be run in Engine folder
CreateLinkIfNoneExists()
{
    WrongName=$1
    CorrectName=$2

    pushd `dirname $CorrectName` > /dev/null
    if [ ! -f `basename $CorrectName` ] && [ -f $WrongName ]; then
      echo "$WrongName -> $CorrectName"
      ln -sf $WrongName `basename $CorrectName`
    fi
    popd > /dev/null
}


# main
set -e

TOP_DIR=$(cd $SCRIPT_DIR/../../.. ; pwd)
cd ${TOP_DIR}

IS_GITHUB_BUILD=true
if [ -e Build/PerforceBuild.txt ]; then
  IS_GITHUB_BUILD=false
fi

if [ -e /etc/os-release ]; then
  source /etc/os-release
  # Ubuntu/Debian/Mint
  if [[ "$ID" == "ubuntu" ]] || [[ "$ID_LIKE" == "ubuntu" ]] || [[ "$ID" == "debian" ]] || [[ "$ID_LIKE" == "debian" ]]; then
    # Install all necessary dependencies
    DEPS="mono-xbuild \
      mono-dmcs \
      libmono-microsoft-build-tasks-v4.0-4.0-cil \
      libmono-system-data-datasetextensions4.0-cil
      libmono-system-web-extensions4.0-cil
      libmono-system-management4.0-cil
      libmono-system-xml-linq4.0-cil
      libmono-corlib4.0-cil
      libqt4-dev
      dos2unix
      cmake
      "

    for DEP in $DEPS; do
      if ! dpkg -s $DEP > /dev/null 2>&1; then
        echo "Attempting installation of missing package: $DEP"
        set -x
        sudo apt-get install -y $DEP
        set +x
      fi
    done
  fi
  
  # openSUSE/SLED/SLES
  if [[ "$ID" == "opensuse" ]] || [[ "$ID_LIKE" == "suse" ]]; then
    # Install all necessary dependencies
    DEPS="mono-core
      mono-devel
      libqt4-devel
      dos2unix
      cmake
      "

    for DEP in $DEPS; do
      if ! rpm -q $DEP > /dev/null 2>&1; then
        echo "Attempting installation of missing package: $DEP"
        set -x
        sudo zypper -n install $DEP
        set +x
      fi
    done
  fi

  # Arch Linux
  if [[ "$ID" == "arch" ]] || [[ "$ID_LIKE" == "arch" ]]; then
    DEPS="clang mono python sdl2 qt4 dos2unix cmake"
    MISSING=false
    for DEP in $DEPS; do
      if ! pacman -Qs $DEP > /dev/null 2>&1; then
        MISSING=true
        break
      fi
    done
    if [ "$MISSING" = true ]; then
      echo "Attempting to install missing packages: $DEPS"
      set -x
      sudo pacman -Sy --needed --noconfirm $DEPS
      set +x
    fi
  fi
fi

echo 
if [ "$IS_GITHUB_BUILD" = true ]; then
	echo Github build
	echo Checking / downloading the latest archives
	Build/BatchFiles/Linux/GitDependencies.sh --prompt "$@"
else
	echo Perforce build
	echo Assuming availability of up to date third-party libraries
fi

# Fixes for case sensitive filesystem.
echo Fixing inconsistent case in filenames.
for BASE in Content/Editor/Slate Content/Slate Documentation/Source/Shared/Icons; do
  find $BASE -name "*.PNG" | while read PNG_UPPER; do
    png_lower="$(echo "$PNG_UPPER" | sed 's/.PNG$/.png/')"
    if [ ! -f $png_lower ]; then
      PNG_UPPER=$(basename $PNG_UPPER)
      echo "$png_lower -> $PNG_UPPER"
      # link, and not move, to make it usable with Perforce workspaces
      ln -sf `basename "$PNG_UPPER"` "$png_lower"
    fi
  done
done

CreateLinkIfNoneExists ../../engine/shaders/Fxaa3_11.usf  ../Engine/Shaders/Fxaa3_11.usf
CreateLinkIfNoneExists ../../Engine/shaders/Fxaa3_11.usf  ../Engine/Shaders/Fxaa3_11.usf

echo
pushd Build/BatchFiles/Linux > /dev/null
./BuildThirdParty.sh
popd > /dev/null

touch Build/OneTimeSetupPerformed
