# Prerequisites

## Windows

- Install [Visual Studio 2019 Community Edition](https://aka.ms/vs/16/release/vs_Community.exe)
- Install cygwin with MSVC make and nasm using PowerShell:

  ```powershell
  Invoke-WebRequest https://cygwin.com/setup-x86_64.exe -OutFile C:\setup.exe
  & C:\setup.exe -BNqdDLXgnO -s http://mirrors.kernel.org/sourceware/cygwin/ -l C:\cygwin-packages -R C:\cygwin -P `
    autoconf,automake,bison,cabextract,doxygen,flex,gettext-devel,`
    git,gnupg,gperf,libxml2-devel,libpng12-devel,make,mintty,openssh,`
    openssl,patch,perl,pkg-config,readline,rsync,unzip,wget,`
    zip,perl-Archive-Zip,perl-Font-TTF,perl-IO-String,python,python3

  Invoke-WebRequest https://dev-www.libreoffice.org/bin/cygwin/make-4.2.1-msvc.exe -OutFile C:\cygwin\usr\local\bin\make.exe
  Invoke-WebRequest https://www.nasm.us/pub/nasm/releasebuilds/2.11.06/win32/nasm-2.11.06-win32.zip -OutFile C:\cygwin\nasm.zip
  Expand-Archive -LiteralPath C:\cygwin\nasm.zip -DestinationPath C:\cygwin
  Move-Item -Path C:\cygwin\nasm-2.11.06\nasm.exe -Destination C:\cygwin\usr\local\bin\nasm.exe
  ```

- Add `C:\cygwin\vs-cygwin.bat` containing:

```
@echo off
setlocal enableextensions
set TERM==
set ORIGIN=%CD%
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" x64
cd /d %~dp0
.\bin\bash.exe --login -i -c "cd \"$ORIGIN\"; CHERE_INVOKING=1 exec bash --login -i"
```

- Create a shortcut to `vc-cygwin.bat` called and change the `Start in:` location to `libreofficekit\libreoffice-core`

- For all development purposes, use this shortcut, otherwise things won't work as expected

## Linux

- Install dependencies using apt:

```shell
sudo apt-get install git build-essential zip nasm python3 python3-dev \
  autoconf gperf xsltproc libxml2-utils libxrandr-dev libx11-dev bison \
  flex libgtk-3-dev libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev \
  libavahi-client-dev libxt-dev
```

## macOS

- [Download and install Xcode 13.4.1](https://developer.apple.com/services-account/download?path=/Developer_Tools/Xcode_13.4.1/Xcode_13.4.1.xip)
  - Open Xcode after installing and agree to the EULA
- [Install MacPorts](https://www.macports.org/install.php)
- Install dependencies using Mac ports:

```shell
port install nasm autoconf automake gperf gpatch
```

# Building

- Change to the `libreoffice-core` directory: `cd libreoffice-core`
- Configure using autogen for the appropriate platform:
  - Windows

```shell
./autogen.sh --with-distro=LOKit-Windows
```

- Linux

```shell
./autogen.sh --with-distro=LOKit-Linux
```

- Mac

```shell
./autogen.sh --with-distro=LOKit-Mac
```

- Run `make build-nocheck`

The result will be in `instdir`
