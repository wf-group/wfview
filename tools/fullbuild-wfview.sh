#!/usr/bin/env bash
### ### ### ### ### ### ###
###
### TO USE: 
###  1. Download this file to ~/Downloads
###  2. Open a terminal
###  3. Type these commands (without the $)
###  $ cd ~/Downloads
###  $ chmod +x fullbuild-wfview.sh
###  $ ./fullbuild-wfview.sh
###  
### ### ### ### ### ### ###
branch="unset"
if (($# < 1))
then
	branch="master"
else
	branch=$1
fi

total_ram_mb=$(free -m | grep Mem: | awk '{print $2}')
ram_threshold=4096
if (( total_ram_mb < ram_threshold )); then
    NCORES=1
    echo "Low-RAM machine"
else
    num_cores=$(nproc)
    NCORES=$(( (num_cores + 1) / 2 ))
    echo "Total CPU cores detected: ${num_cores}"
fi

echo "Using ${NCORES} core(s) for the build step."

echo "Using source code branch $branch"
echo ""
echo "This script will download dependencies, build, and install wfview. "
echo "It is designed for debian-based systems and "
echo "makes use of the apt command to satisfy dependencies. "
echo
echo "If it has been a while since this script was downloaded,"
echo "or if there are build errors, please use this command to"
echo "download a newer version of this script:"
echo
echo "wget https://raw.githubusercontent.com/wf-group/wfview/master/tools/fullbuild-wfview.sh -O fullbuild-wfview.sh; chmod 755 fullbuild-wfview.sh"
echo
echo "The 'sudo' command is used to run some commands as root."
echo "It (the sudo command) will ask for your password during this process."
echo "You should look at the source of this script if you have any doubts."
echo 
echo "Do you wish to install dependencies first? "
echo "If this is your first time building wfview,"
echo "or, if you have not done so in a while,"
echo "please select 'y', otherwise, press 'n'."
echo "If you are not sure, select 'y' to be safe."
echo
read -p "Press Y to install dependencies (y/n): " -n 1 -r
echo   
if [[ $REPLY =~ ^[Yy]$ ]]
then
    E=0;
    sudo apt-get -y install build-essential || exit -1
    sudo apt-get -y install git || exit -1
    sudo apt-get -y install libhidapi-dev || exit -1
    sudo apt-get -y install libopus-dev || exit -1
    sudo apt-get -y install libeigen3-dev || exit -1
    sudo apt-get -y install libportaudio2 libportaudiocpp0 || exit -1
    sudo apt-get -y install portaudio19-dev || exit -1
    sudo apt-get -y install librtaudio-dev librtaudio6 || exit -1
    sudo apt-get -y install libudev-dev || exit -1

    echo "Installing Qt build dependencies. Qt6 will be used by default."
    if sudo apt-get -y install qt6-base-dev qt6-base-dev-tools libqt6serialport6-dev qtmultimedia6-dev libqt6websockets6-dev; then
        echo "Qt6 dependencies installed."
    else
        echo "Qt6 dependency install failed. Falling back to Qt5 dependencies."
        sudo apt-get -y install qt5-qmake || exit -1
        sudo apt-get -y install libqt5core5a || exit -1
        sudo apt-get -y install qtbase5-dev || exit -1
        sudo apt-get -y install libqt5serialport5 libqt5serialport5-dev || exit -1
        sudo apt-get -y install libqt5multimedia5 || exit -1
        sudo apt-get -y install libqt5multimedia5-plugins || exit -1
        sudo apt-get -y install qtmultimedia5-dev || exit -1
        sudo apt-get -y install libqt5gamepad5 libqt5gamepad5-dev || exit -1
        sudo apt-get -y install libqt5websockets5 libqt5websockets5-dev || exit -1
    fi

    echo "Almost done. Now we will install libqcustomplot."
    echo "One of these two commands will fail, which is ok. "
    echo "Only one of the next two commands need to work."

    sudo apt-get -y install libqcustomplot1.3 libqcustomplot-doc libqcustomplot-dev || ((E=E+1))
    sudo apt-get -y install libqcustomplot2.0 libqcustomplot-doc libqcustomplot-dev || ((E=E+1))
    sudo apt-get -y install libqcustomplot2.1 libqcustomplot-dev || ((E=E+1))

    if [ "$E" -eq 3 ]; then
        echo "Neither version of qcustomplot could be installed."
        echo "One of the three supported versions (1.3,  2.0, or 2.1) must be installed to continue."
        exit -1
    else
        echo "Installing the required qcustomplot was successful."
    fi

    echo "Done installing dependencies."
else
    echo "Skipping dependency install steps. "
fi

read -p "Press enter to download wfview's source code."

SRCDIR=wfview--`date +%Y%m%d--%H-%M-%S`

echo "Now downloading the latest source code from the branch $branch"
echo "You can specify an alternate branch name as the argument to this script if you wish."
echo "The files will be downloaded into a directory named: $SRCDIR"

mkdir $SRCDIR

if [ $? -ne 0 ]; then
    echo "Error making download directory."
    exit -1
fi

cd $SRCDIR

### BRANCH SELECTION ###
# You may specify alternate branches using the first argument to this script
# example:
# ./fullbuild-wfview.sh audio-fixes
# the master branch is selected by default. 
# see here for a list of branches: https://github.com/wf-group/wfview/branches
git clone --depth 1 https://github.com/wf-group/wfview.git -b $branch

if [ $? -ne 0 ]; then
    echo "Error cloning source."
    echo "The git clone command failed to download wfview. "
    exit -1
fi

cd wfview

echo "wfview source log: "
git shortlog
echo "revision: "
git rev-parse --short HEAD
git rev-parse --long HEAD
git rev-parse --long HEAD > /tmp/build.log
echo

git submodule init

if [ $? -ne 0 ]; then
    echo "Error initializing submodules."
    echo "The git submodule init command failed. "
    exit -1
fi

git submodule update

if [ $? -ne 0 ]; then
    echo "Error updating submodules."
    echo "The git submodule update command failed. "
    exit -1
fi

cd ..

echo "Creating build directory 'build':"

mkdir build

if [ $? -ne 0 ]; then
    echo "Error making build directory."
    exit -1
fi


cd build
echo "The build process may take a few minutes."
read -p "Press enter to start. "

echo "Starting build process."

QMAKE_BIN=""
for candidate in qmake6 qt6-qmake /usr/lib/qt6/bin/qmake qmake qmake-qt5 /usr/lib/qt5/bin/qmake
do
    if command -v "$candidate" >/dev/null 2>&1; then
        QMAKE_BIN="$candidate"
        break
    elif [ -x "$candidate" ]; then
        QMAKE_BIN="$candidate"
        break
    fi
done

if [ -z "$QMAKE_BIN" ]; then
    echo "Could not find qmake. Please install Qt6 or Qt5 development packages."
    exit -1
fi

echo "Using qmake command: $QMAKE_BIN"
"$QMAKE_BIN" ../wfview/wfview.pro

if [ $? -ne 0 ]; then
    echo "Error in qmake step."
    exit -1
fi

set -o pipefail
time make -j${NCORES} 2>&1 | tee -a /tmp/build.log

if [ $? -ne 0 ]; then
    echo "Error in make step."
    echo "wfview was not compiled."
    echo "Please consider posting the error(s) to https://forum.wfview.org/"
    echo "The error text was logged to /tmp/build.log"
    read -p "Press enter to send the log to termbin (recommended). Press control-c to cancel."
    echo "Stand by, sending log to termbin: "
    cat /tmp/build.log | nc termbin.com 9999
    echo "Please provide that URL to the wfview team."
    exit -1
fi

echo
read -p "Press Y to install wfview into your system (Y/n): " -n 1 -r
echo   

if [[ $REPLY =~ ^[Yy]$ ]]
then
    echo "Now installing: "
    sudo make install
    echo "Done running script."
else
    echo "Did not install. Program may be manually installed by running \"sudo make install\" from the build folder."
fi
