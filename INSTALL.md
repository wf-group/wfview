# How to install wfview

### 1. Install prerequisites:
(Note, some packages may have slightly different version numbers, this should be ok for minor differences.)
~~~
sudo apt-get install build-essential
sudo apt-get install qt5-qmake
sudo apt-get install qt5-default
sudo apt-get install libqt5core5a
sudo apt-get install qtbase5-dev
sudo apt-get install libqt5serialport5 libqt5serialport5-dev
sudo apt-get install libqt5multimedia5
sudo apt-get install libqt5multimedia5-plugins
sudo apt-get install qtmultimedia5-dev
sudo apt-get install git 
sudo apt-get install libopus-dev
sudo apt-get install libeigen3-dev
sudo apt-get install portaudio19-dev
sudo apt-get install librtaudio-dev
sudo apt-get install libhidapi-dev libqt5gamepad5-dev
sudo apt-get install libudev-dev
~~~

optional for those that want to work on the code using the QT Creator IDE: 
~~~
sudo apt-get install qtcreator qtcreator-doc
~~~

### 2. Clone wfview to a local directory on your computer:
~~~ 
cd ~/Documents
git clone https://github.com/wf-group/wfview.git
~~~

### 3. Create a build directory, compile, and install:
If you want to change the default install path from `/usr/local` to a different prefix (e.g. `/opt`), you must call `qmake ../wfview/wfview.pro PREFIX=/opt`

~~~
mkdir build
cd build
qmake ../wfview/wfview.pro
make -j
sudo make install
~~~

### 4. You can now launch wfview, either from the terminal or from your desktop environment. If you encounter issues using the serial port, run the following command: 
~~~

if you are using the wireless 705 or any networked rig like the 7610, 7800, 785x, there is no need to use USB so below is not needed.

sudo chown $USER /dev/ttyUSB*

Note, on most linux systems, you just need to add your user to the dialout group, which is persistent and more secure:

~~~
sudo usermod -aG dialout $USER 
~~~
(don't forget to log out and log in)

~~~

### opensuse/sles/tumbleweed install
---

install wfview on suse 15.3 & up, sles 15.x or tumbleweed; this was done on a clean install/updated OS. 


There are two options, QT5 (out of the box)  and QT6 (almost out of the box)


for qt5 and 6:

~~~
sudo zypper in --type pattern devel_basis
~~~

for qt5 only:

~~~
sudo zypper in libQt5Widgets-devel libqt5-qtbase-common-devel libqt5-qtserialport-devel libQt5SerialPort5 libqt5-qtmultimedia-devel lv2-devel libopus-devel eigen3-devel libQt5Xml-devel portaud
io-devel rtaudio-devel libqt5-qtgamepad-devel libQt5Gamepad5 libqt5-qtwebsockets-devel libqt5-qtgamepad-devel portaudio-devel rtaudio-devel libhidapi-devel libopus-devel eigen3-devel portaudio-devel

mkdir -p ~/src/build
cd ~/src
git clone https://github.com/wf-group/wfview.git
cd build
qmake-qt5 ../wfview/wfview.pro
make
sudo make install
~~~


for qt6 only:
~~~
sudo zypper in qt6-base-common-devel qt6-gui-devel qt6-serialport-devel qt6-multimedia-devel qt6-websockets-devel qt6-widgets-devel rtaudio-devel libhidapi-devel qt6-xml-devel libopus-devel eigen3-devel portaudio-devel
~~~

Now:

~~~

mkdir -p ~/src/build
cd ~/src
git clone https://github.com/wf-group/wfview.git
cd build
qmake6 ../wfview/wfview.pro
make
sudo make install
~~~



---

### Fedora install ###
---
Tested under Fedora 41 Workstation.

Install dependencies:

```bash
dnf install -y qt6-qtbase-devel qt6-qtmultimedia-devel qt6-serialport-devel \
  qt6-qtwebsocket-devel opus-devel eigen3-devel \
  portaudio-devel rtaudio-devel hidapi-devel systemd-devel
```

When done, create a build area, clone the repo, build and install:

```bash
mkdir -p $HOME/src && cd $HOME/src
git clone https://github.com/wf-group/wfview.git
cd wfview
mkdir -p build && cd build
qmake ../wfview.pro
make -j4
sudo make install
```

wfview is now installed in /usr/local/bin and you can start it from launcher or from command line with command `wfview`.

# How to configure your RC-28 knob under Linux

To use RC-28 knob you need to add udev rules, please execute as root:

~~~
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="0c26", ATTRS{idProduct}=="001e", MODE="0666"' >> /etc/udev/rules.d/99-ham-wfview.rules
udevadm control --reload-rules && udevadm trigger
~~~
---
