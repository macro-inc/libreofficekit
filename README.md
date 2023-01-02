# Prerequisites

## Windows

*TODO*: Write up Windows build instructions based off of what's in release-windows.yml

## Linux

- Install dependencies using apt:
```shell
sudo apt-get install git build-essential zip libkrb5-dev nasm graphviz python3 python3-dev autoconf libfontconfig1-dev gperf libxslt1-dev xsltproc libxml2-utils libxrandr-dev libx11-dev bison flex libgtk-3-dev libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libnss3-dev libavahi-client-dev libxt-dev 
```

## macOS
- [Download and install Xcode 13.4.1](https://developer.apple.com/services-account/download?path=/Developer_Tools/Xcode_13.4.1/Xcode_13.4.1.xip)
  - Open Xcode after installing and agree to the EULA
- [Install brew](https://brew.sh)
- Install dependencies using brew:
``` shell
brew install nasm autoconf automake gperf
```

# Building

- Change to the `libreoffice-core` directory: `cd libreoffice-core`
- Configure using autogen for the appropriate platform:
  - Windows
``` shell
./autogen.sh --with-distro=LOKit-Windows
```
  - Linux
``` shell
./autogen.sh --with-distro=LOKit-Linux
```
  - Mac
``` shell
./autogen.sh --with-distro=LOKit-Mac
```
- Run `make build-nocheck`

The result will be in `instdir`
