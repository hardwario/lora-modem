# Python library for the Murata TypeABZ LoRa modem

This project provides a Python support library for working with the Murata TypeABZ LoRaWAN modem. The modem communicates with the host over an AT command interface. The Python library abstracts away the AT command interface and provides an easier to use high-level API. The library provides a Python module that can be embedded into a larger Python application and a command line tool called `lora` that can be used to manage the modem from the terminal. The Python module supports the original Murata Modem firmware shipped with some TypeABZ modules, as well as the open LoRaWAN firmware from the [lora-modem-abz](https://github.com/hardwario/lora-modem-abz) Github repository. The command line tool only works with the open firmware.

## Installation
You can install the library with pip from PyPI as follows:
```sh
pip install --upgrade lora-modem-abz
```
Alternatively, you can also install the library from the Github repository as follows:
```
git clone https://github.com/hardwario/lora-modem-abz
cd lora-modem-abz/python
pip install --editable . 
```

## Usage from Python
The basic usage from a Python program looks as follows:
```python
from lora import TypeABZ, OpenLoRaModem, MurataModem

# Create an instance of the TypeABZ device
device = TypeABZ('/dev/ttyUSB0')

# Try to detect the serial port baud rate used by the device
baudrate = device.detect_baud_rate()
if baudrate is None:
    raise SystemExit('Could not detect modem baud rate')

# Open the serial port and connect to the TypeABZ device
device.open(baudrate)
try:
    # Create an API instance depending on the firmware
    # Use MurataModem instead if your module has the original firmware
    modem = OpenLoRaModem(device)
    
    # Show fimware version
    print(modem.version)
    
    # Send an unconfirmed uplink to the default port
    # The message must be a bytes value, not str
    modem.utx(b'ping')
finally:
    # Close the serial port
    device.close()
```
The class `TypeABZ` represents the physical modem device. The classes `OpenLoRaModem` and `MurataModem` then implement a particular version of the modem API. Tha class `OpenLoRaModem` has been designed for the open firmware from the [lora-modem-abz](https://github.com/hardwario/lora-modem-abz) Github. The class `MurataModem` has been designed for the original Murata Modem firmware pre-installed on some TypeABZ modules. Please refer to the documentation in `lora.py` for more information.

## Command Line Tool

*Note: The command line tool only works with the open modem firmware.*

The library provides a command line tool installed under the name `lora`. The tool can be used to interact with TypeABZ LoRaWAN modems from shell scripts and the terminal. To invoke the tool, pass the pathname to the modem's serial port either via the environment variable PORT, or via the command line option `-p`. Without any arguments, the tool displays information about the selected modem:
```
$ lora -p /dev/serial0 
Device information for modem /dev/serial0:
+---------------------+-------------------------------------------------------------------+
| Port configuration  | 19200 8N1                                                         |
| Device model        | ABZ                                                               |
| Firmware version    | v1.1.1-43-gf86592d2 (modified) [LoRaMac-node v4.6.0-23-g50155c55] |
| Data encoding       | binary                                                            |
| LoRaWAN version     | 1.1.1 / 1.0.4 (1.0.4 for ABP)                                     |
| Regional parameters | RP002-1.0.3                                                       |
| Supported regions   | AS923 AU915 CN470 CN779 EU433 EU868 IN865 KR920 RU864 US915       |
| Device EUI          | 323838377B308503                                                  |
+---------------------+-------------------------------------------------------------------+
Network activation information for modem /dev/serial0:
+------------------+------------------+
| Network type     | public           |
| Activation       | OTAA             |
| Network ID       | 00000013         |
| Join EUI         | 0101010101010101 |
| Protocol version | LoRaWAN 1.1.1    |
| Device address   | 260C56AC         |
+------------------+------------------+
Current state of modem /dev/serial0:
+---------------------------+-----------------------------------------------------------+
| Current region            | US915                                                     |
| LoRaWAN class             | A                                                         |
| Channel mask              | 00FF00000000000000000000                                  |
| Data rate                 | SF10_125                                                  |
| Maximum message size      | 11 B                                                      |
| RF power index            | 0                                                         |
| ADR enabled               | True                                                      |
| Duty cycling enabled      | False                                                     |
| Join duty cycling enabled | True                                                      |
| Maximum EIRP              | 32 dBm                                                    |
| Uplink frame counter      | 2                                                         |
| Downlink frame counter    | 0                                                         |
| Last downlink RSSI        | -105 dBm                                                  |
| Last downlink SNR         | -4 dB                                                     |
| RX1 window                | Delay: 5000 ms                                            |
| RX2 window                | Delay: 6000 ms, Frequency: 923.3 MHz, Data rate: SF12_500 |
| Join response windows     | RX1: 5000 ms, RX2: 6000 ms                                |
+---------------------------+-----------------------------------------------------------+
```
To see the full list of supported commands, run `lora --help`:
```
...
Commands:
  device   Show basic modem information.
  get      Retrieve modem setting(s).
  join     Perform a LoRaWAN OTAA Join.
  keygen   Generate new random LoRaWAN security keys.
  keys     Show current LoRaWAN security keys.
  link     Check the radio link.
  network  Show current network activation parameters.
  reboot   Restart the modem.
  reset    Reset the modem to factory defaults.
  set      Update modem setting.
  state    Show the current modem state.
  trx      Transmit and receive LoRaWAN messages.
```
Run `lora <command> --help` to see the built-in documentation for each command.

## License

The library is licensed under the terms of the Revised BSD License. See [LICENSE](https://github.com/hardwario/lora-modem-abz/blob/main/python/LICENSE) for full details.

Copyright (c) 2022 Jan Janak \<jan@janakj.org\>
