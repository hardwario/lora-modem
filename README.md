# Open LoRaWAN Modem for Murata Type ABZ Module

This project develops an open-source LoRaWAN modem firmware for the Type ABZ wireless module by Murata. The firmware provides an [AT command interface](https://github.com/hardwario/lora-modem/wiki/AT-Command-Interface) backward compatible with Murata Modem, Murata's proprietary LoRaWAN firmware. The firmware can be used on all Type ABZ [variants](https://github.com/hardwario/lora-modem/wiki/Type-ABZ-Modules) with an open (user-programmable) microcontroller (also known as OpenMCU).

## Main Features

* Support for [LoRaWAN 1.0.4](https://resources.lora-alliance.org/technical-specifications/ts001-1-0-4-lorawan-l2-1-0-4-specification), [LoRaWAN 1.1](https://resources.lora-alliance.org/technical-specifications/lorawan-specification-v1-1) ([note on compatibility](https://github.com/hardwario/lora-modem/wiki/LoRaWAN-1.1-Compatibility)), and regional parameters [RP2-1.0.3](https://resources.lora-alliance.org/technical-specifications/rp2-1-0-3-lorawan-regional-parameters)
* Based on the most recent version (4.7.0) of [LoRaMac-node](https://github.com/Lora-net/LoRaMac-node)
* Support for multiple regions configurable at runtime
* All persistent LoRaWAN MAC data stored in NVM (EEPROM)
* Very low [power consumption](https://github.com/hardwario/lora-modem/wiki/Power-Consumption) (1.4 uA when idle)

The project also provides a high-level [Python library and command line tool](./python) for managing Type ABZ LoRa modems. See this [README](./python/README.md) for more information.

## Installation

Binary firmware images for several platforms embedding the Type ABZ module are available from the [Releases](https://github.com/hardwario/lora-modem/releases) page. We generate a pre-configured firmware [variant](https://github.com/hardwario/lora-modem/wiki/Supported-Platforms) for each of the following platforms:
  * HARDWARIO [LoRa Module](https://shop.hardwario.com/lora-module/)
  * HARDWARIO [Chester](https://www.hardwario.com/chester/)
  * Arduino [MKR WAN 1300](https://store-usa.arduino.cc/products/arduino-mkr-wan-1300-lora-connectivity)
  * Arduino [MKR WAN 1310](https://store.arduino.cc/products/arduino-mkr-wan-1310)
  * ST [B-L072Z-LRWAN1](https://www.st.com/en/evaluation-tools/b-l072z-lrwan1.html) Discovery Kit

The [STM32 Cube Programmer](https://www.st.com/en/development-tools/stm32cubeprog.html) or the HARDWARIO [firmware flashing tool](https://tower.hardwario.com/en/latest/tools/hardwario-firmware-flashing-tool/) can be used to flash the firmware into the Type ABZ module. Steps to flash the firmware into HARDWARIO Tower LoRa Module are described in the [wiki](https://github.com/hardwario/lora-modem/wiki/LoRa-Module-Firmware-Update). Firmware update tool for the Arduino MKR WAN 1310 can be found [here](https://github.com/disk91/mkr1310_openLoRaModem_fw_update).

## Building

You will need the embedded gcc toolchain for ARM (arm-none-eabi), `git`, and `make` to build your own firmware binary from the source code. First, clone the repository and initialize git submodules:
```sh
git clone https://github.com/hardwario/lora-modem
cd lora-modem
git submodule update --init
```
If you wish to customize the build, edit the variables at the beginning of the Makefile (or override them at the `make` command line). Then run `make` to build the firmware in release mode:
```sh
make
```
If you wish to build a development version with logging and debugging enabled, run `make debug` instead. *Please note that development builds have higher [idle power consumption](https://github.com/hardwario/lora-modem/wiki/Power-Consumption) than release builds.*

## Documentation
* [The Things Network (TTN) provisioning](https://github.com/hardwario/lora-modem/wiki/TTN-Provisioning)
* [AT command interface](https://github.com/hardwario/lora-modem/wiki/AT-Command-Interface)
* Other [similar projects](https://github.com/hardwario/lora-modem/wiki/Related-Work)

Additional documentation and notes can be found in the [wiki](https://github.com/hardwario/lora-modem/wiki).

## Contributing

[Bug reports](https://github.com/hardwario/lora-modem/issues), [improvement suggestions](https://github.com/hardwario/lora-modem/issues), [pull requests](https://github.com/hardwario/lora-modem/pulls), and updates to the [documentation in the wiki](https://github.com/hardwario/lora-modem/wiki) would be greatly appreciated!

## License

The firmware open source, licensed under the terms of the Revised BSD License. It includes the [LoRaMac-node](https://github.com/Lora-net/LoRaMac-node) library licensed under the Revised BSD License and portions of the [STM32CubeL0](https://github.com/STMicroelectronics/STM32CubeL0) MCU firmware package licensed under the Revised BSD License.

See [LICENSE](LICENSE) for full details.
