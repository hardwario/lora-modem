# Open LoRaWAN Modem for Murata Type ABZ Module

This project develops an open-source LoRaWAN modem firmware for the [Type ABZ](https://github.com/hardwario/lora-modem-abz/wiki/Type-ABZ-Modules) wireless module by Murata. The firmware provides an AT command interface backward compatible with Murata Modem, Murata's proprietary LoRaWAN firmware. The firmware can be used on all Type ABZ variants with an open (user-reprogrammable) microcontroller (also known as OpenMCU).

We provide binary firmware images tailored to the following platforms:
  * HARDWARIO [LoRa Module](https://shop.hardwario.com/lora-module/)
  * HARDWARIO [Chester](https://www.hardwario.com/chester/)
  * Arduino [MKR WAN 1300](https://store-usa.arduino.cc/products/arduino-mkr-wan-1300-lora-connectivity)
  * Arduino [MKR WAN 1310](https://store.arduino.cc/products/arduino-mkr-wan-1310)
  * ST [B-L072Z-LRWAN1](https://www.st.com/en/evaluation-tools/b-l072z-lrwan1.html) Discovery Kit

Although all above platforms embed the same Type ABZ module, there are slight differences in the configuration of the module's I/O ports.

## Main Features

* Support for [LoRaWAN 1.0.4](https://resources.lora-alliance.org/technical-specifications/ts001-1-0-4-lorawan-l2-1-0-4-specification), [LoRaWAN 1.1](https://resources.lora-alliance.org/technical-specifications/lorawan-specification-v1-1), and regional parameters [RP2-1.0.3](https://resources.lora-alliance.org/technical-specifications/rp2-1-0-3-lorawan-regional-parameters)
* Based on the most recent version (4.7.0) of [LoRaMac-node](https://github.com/Lora-net/LoRaMac-node)
* Support for multiple regions configurable at runtime
* All persistent LoRaWAN MAC data stored in NVM (EEPROM)
* Very low power consumption (1.4 uA when idle)

[A note on LoRaWAN 1.1 compatibility](https://github.com/hardwario/lora-modem-abz/wiki/LoRaWAN-1.1-Compatibility)

The project also provides a [Python library and command line tool](./python) for managing Type ABZ LoRa modems. See this [README](./python/README.md) for more information.

## Building

You will need the the embedded gcc toolchain for ARM (arm-none-eabi), git, and make to build the firmware from the source code. First, clone the repository and initialize git submodules:
```sh
git clone https://github.com/hardwario/lora-modem-abz
cd lora-modem-abz
git submodule update --init
```
If you wish to customize the build, edit the variables at the beginning of the Makefile. Next, build the firmware in release mode:
```sh
make release
```
If you wish to build a development version with logging and debugging enabled, run `make debug` instead. Running `make` without any arguments builds the release version by default. *Please note that development builds have higher [idle power consumption](https://github.com/hardwario/lora-modem-abz/wiki/Power-Consumption) than release builds.*

## Installation
Follow the steps outlined in this [wiki page](https://github.com/hardwario/lora-modem-abz/wiki/LoRa-Module-Firmware-Replacement) to replace the proprietary firmware in HARDWARIO's [LoRa Module](https://shop.hardwario.com/lora-module/) with the open firmware.

## Documentation
* [The Things Network (TTN) provisioning](https://github.com/hardwario/lora-modem-abz/wiki/TTN-Provisioning)
* [AT command interface](https://github.com/hardwario/lora-modem-abz/wiki/AT-Command-Interface)
Additional documentation and notes can be found in the [wiki](https://github.com/hardwario/lora-modem-abz/wiki).

## Contributing

[Bug reports](https://github.com/hardwario/lora-modem-abz/issues), [improvement suggestions](https://github.com/hardwario/lora-modem-abz/issues), [pull requests](https://github.com/hardwario/lora-modem-abz/pulls), and updates to the [documentation in the wiki](https://github.com/hardwario/lora-modem-abz/wiki) would be greatly appreciated!

## License

The firmware open source, licensed under the terms of the Revised BSD License. It includes the [LoRaMac-node](https://github.com/Lora-net/LoRaMac-node) library licensed under the Revised BSD License and portions of the [STM32CubeL0](https://github.com/STMicroelectronics/STM32CubeL0) MCU firmware package licensed under the Revised BSD License.

See [LICENSE](LICENSE) for full details.
