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
```

#### Install the development package

```shell
sudo apt install libusdr-dev
```

## Build from source

#### Clone the repository

```shell
git clone https://github.com/wavelet-lab/usdr-lib.git ./usdr-lib
cd usdr-lib
```

#### Dependencies

```shell
sudo apt install build-essential cmake python3 python3-venv python3-yaml dwarves -y
sudo apt install libusb-1.0-0-dev check dkms -y
```

#### Build

```shell
mkdir build
cd build
cmake ../src -DENABLE_TESTS=OFF
make
```

#### Build kernel module

```shell
sudo apt install linux-headers-$(uname -r)
cd ../src/lib/lowlevel/pcie_uram/driver
sudo insmod usdr_pcie_uram.ko
````
