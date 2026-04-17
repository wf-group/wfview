# Building wfserver

`wfserver` is the command-line, headless server build of wfview. It exposes
a networked radio server (for only Icom radios at this time) without any GUI, and is
intended for always-on machines such as a Raspberry Pi or a small Linux
host located near the radio. It can also be compiled on a Microsoft Windows host, but that procedure is yet to be documented. 

This document describes how to build and install `wfserver` on Ubuntu
22.04 LTS and Ubuntu 24.04 LTS. It may be adapted for use with other linux distributions. 

## Build dependencies

The table below lists every third-party library / toolchain component
required to build [wfserver.pro](wfserver.pro), together with the
`apt` package name on each supported Ubuntu release.

| Dependency            | Ubuntu 22.04 package           | Ubuntu 24.04 package           |
|-----------------------|--------------------------------|--------------------------------|
| C/C++ toolchain       | build-essential                | build-essential                |
| Git                   | git                            | git                            |
| qmake / Qt build tool | qtchooser qt5-qmake            | qmake6                         |
| Qt Core / Widgets     | qtbase5-dev                    | qt6-base-dev                   |
| Qt SerialPort         | libqt5serialport5-dev          | qt6-serialport-dev             |
| Qt Network (in base)  | qtbase5-dev                    | qt6-base-dev                   |
| Qt Multimedia         | qtmultimedia5-dev              | qt6-multimedia-dev             |
| Qt PrintSupport       | qtbase5-dev                    | qt6-base-dev                   |
| PulseAudio            | libpulse-dev                   | libpulse-dev                   |
| RtAudio               | librtaudio-dev                 | librtaudio-dev                 |
| Opus codec            | libopus-dev                    | libopus-dev                    |
| Eigen (headers)       | libeigen3-dev                  | libeigen3-dev                  |
| POSIX threads         | libc6-dev (in build-essential) | libc6-dev (in build-essential) |

Install everything in one shot:

**Ubuntu 22.04**

```bash
sudo apt update
sudo apt install build-essential git qtchooser qt5-qmake \
    qtbase5-dev libqt5serialport5-dev qtmultimedia5-dev \
    libpulse-dev librtaudio-dev libopus-dev libeigen3-dev
```

**Ubuntu 24.04**

```bash
sudo apt update
sudo apt install build-essential git qmake6 \
    qt6-base-dev qt6-serialport-dev qt6-multimedia-dev \
    libpulse-dev librtaudio-dev libopus-dev libeigen3-dev
```

## Get the source

Clone the wfview repository:

```bash
git clone https://gitlab.com/eliggett/wfview.git
```

## Build

Create a build directory **outside** of the source tree, as a sibling
of the cloned `wfview` directory:

```bash
mkdir build-wfserver
cd build-wfserver
```

Run `qmake` against [wfserver.pro](wfserver.pro), then `make`:

```bash
qmake ../wfview/wfserver.pro
make -j$(($(nproc)/2))
```

On Ubuntu 24.04, if `qmake` resolves to the Qt5 binary, use `qmake6`
instead:

```bash
qmake6 ../wfview/wfserver.pro
make -j$(($(nproc)/2))
```

## Install

From the same build directory:

```bash
sudo make install
```

This installs the `wfserver` binary to `/usr/local/bin/wfserver` and
the radio definition files to `/usr/local/share/wfview/rigs/`. Set
`PREFIX` at `qmake` time if you want a different install prefix
(for example `qmake PREFIX=/usr ../wfview/wfserver.pro`).

## First-time setup

After installation, run the interactive setup wizard to create the
server configuration (radio model, serial port, network ports,
usernames / passwords, etc.):

```bash
wfserver --setup
```

Once setup is complete, start the server normally:

```bash
wfserver
```
