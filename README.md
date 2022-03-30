# Open Firmware for Murata Type ABZ LoRa Module

The aim of this project is to develop open source firmware for the [Type ABZ](https://github.com/hardwario/lora-modem-abz/wiki/Type-ABZ-Modules) LoRa module by Murata. The firmware provides an AT command interface largely compatible with Murata's proprietary LoRa modem firmware. Although the firmware was primarily developed with the HARDWARIO [LoRa Module](https://shop.hardwario.com/lora-module/) in mind, it can be used in any Type ABZ module with an open (user-reprogrammable) microcontroller.

## Main Features

* Support for [LoRaWAN 1.0.4](https://resources.lora-alliance.org/technical-specifications/ts001-1-0-4-lorawan-l2-1-0-4-specification), [LoRaWAN 1.1](https://resources.lora-alliance.org/technical-specifications/lorawan-specification-v1-1), and regional parameters [RP2-1.0.1](https://resources.lora-alliance.org/technical-specifications/rp2-1-0-1-lorawan-regional-parameters)
* Based on the most recent version (4.6.0) of [LoRaMac-node](https://github.com/Lora-net/LoRaMac-node)
* Support for multiple regions configurable at runtime
* All persistent LoRaWAN MAC data stored in NVM (EEPROM)

[A note on LoRaWAN 1.1 compatibility](https://github.com/hardwario/lora-modem-abz/wiki/LoRaWAN-1.1-Compatibility)

## Building

You will need the the embedded gcc toolchain for ARM (arm-none-eabi), git, and make to build the firmware from the source code. First, clone the repository and initialize git submodules:
```sh
git clone https://github.com/hardwario/lora-modem-abz
cd lora-modem-abz
git submodule update --init
```
If you wish to customize the build, edit the variables at the beginning of the Makefile. Next, build the firmware:
```sh
make
```
By default the firmware is built in development mode with debugging messages and symbols enabled. To build a release version without debugging symbols or logging run `make release` instead.

## Installation
Follow the steps outlined in this [wiki page](https://github.com/hardwario/lora-modem-abz/wiki/LoRa-Module-Firmware-Replacement) to replace the proprietary firmware in HARDWARIO's [LoRa Module](https://shop.hardwario.com/lora-module/) with the open firmware.

## Documentation
* [The Things Network (TTN) Provisioning](https://github.com/hardwario/lora-modem-abz/wiki/TTN-Provisioning)

## License

The firmware open source, licensed under the terms of the Revised BSD License. It includes the [LoRaMac-node](https://github.com/Lora-net/LoRaMac-node) library licensed under the Revised BSD License and portions of the [STM32CubeL0](https://github.com/STMicroelectronics/STM32CubeL0) MCU firmware package licensed under the Revised BSD License.

See [LICENSE](LICENSE) for full details.
