# usdr-lib

uSDR software libraries, driver and utilities

## Installation from packages

### Ubuntu 20.04, 22.04, 24.04

#### Add the repository

```shell
sudo add-apt-repository ppa:wavelet-lab/usdr-lib
sudo apt update
```

#### Install the tools

```shell
sudo apt install usdr-tools
```

#### SoapySDR plugin

```shell
sudo apt install soapysdr-module-usdr
```

#### PCIe driver

```shell
sudo apt install usdr-dkms
sudo modprobe usdr_pcie_uram
```

#### Install the development package

```shell
sudo apt install libusdr-dev
```

### Ubuntu 18.04, Debian 12

#### Download

Go to [releases page](https://github.com/wavelet-lab/usdr-lib/releases) and download the corresponding archive.

* Ubuntu 18.04(amd64): `usdr_0.9.11~bionic0.amd64.tar`
* Ubuntu 18.04(arm64): `usdr_0.9.11~bionic0.arm64.tar`
* Debian 12(amd64): `usdr_0.9.11~bookworm0.amd64.tar`
* Debian 12(arm64): `usdr_0.9.11~bookworm0.arm64.tar`

Unpack the archive.

#### Install package

```shell
sudo apt install libusb-1.0-0 libsoapysdr0.8 dkms
sudo dpkg -i *.deb
```

## Build from source

#### Clone the repository

```shell
git clone https://github.com/wavelet-lab/usdr-lib.git
cd usdr-lib
```

#### Dependencies

##### Ubuntu 18.04

```shell
sudo apt install build-essential dwarves -y
sudo apt install libusb-1.0-0-dev check dkms curl -y

# Install fresh version of Cmake
apt-get install libssl-dev -y
curl https://cmake.org/files/v3.28/cmake-3.28.3.tar.gz -o cmake-3.28.3.tar.gz
tar xvf cmake-3.28.3.tar.gz
cd cmake-3.28.3
./bootstrap
make
make install
update-alternatives --install /usr/bin/cmake cmake /usr/local/bin/cmake 10

# Install Python3.8
apt-get install python3.8 python3.8-distutils -y
update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.8 10
curl https://bootstrap.pypa.io/get-pip.py | python3.8
python3.8 -m pip install pyyaml
```

##### Ubuntu 20.04, 22.04, 24.04, Debian 12

```shell
sudo apt install build-essential cmake python3 python3-venv python3-yaml dwarves -y
sudo apt install libusb-1.0-0-dev check dkms -y
```

#### Build

```shell
mkdir build
cd build
cmake ../src
make
```

#### Build kernel module

```shell
sudo apt install linux-headers-$(uname -r)
cd ../src/lib/lowlevel/pcie_uram/driver
make
sudo insmod usdr_pcie_uram.ko
````
