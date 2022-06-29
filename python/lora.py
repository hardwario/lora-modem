#!/usr/bin/env python
#
# Python library for the Murata TypeABZ LoRaWAN modem
#
# Copyright (c) 2022 Jan Janak <jan@janakj.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#   1. Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
#
#   2. Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
#   3. Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import annotations
import sys
import re
import serial # type: ignore
import binascii
from abc import ABC
from functools import lru_cache
from collections import namedtuple
from contextlib import contextmanager
from typing import Optional, Tuple, Union, List, Any
from datetime import datetime, timedelta
from enum import Enum, unique, auto
from threading import Thread, RLock
from queue import Queue, Empty
from time import sleep
from pymitter import EventEmitter # type: ignore

# The following imports are needed for the command line interface
import os
import click
import secrets
from textwrap import dedent
from tabulate import tabulate
from typing import Callable
from base64 import b64encode, b64decode


machine_readable = False
show_keys = False


@unique
class EventType(Enum):
    MODULE  = 0
    JOIN    = 1
    NETWORK = 2

@unique
class ModuleEventSubtype(Enum):
    BOOT       = 0
    FACNEW     = 1
    BOOTLOADER = 2
    HALT       = 3

@unique
class JoinEventSubtype(Enum):
    FAILED    = 0
    SUCCEEDED = 1

@unique
class NetworkEventSubtype(Enum):
    NO_ANSWER      = 0
    ANSWER         = 1
    RETRANSMISSION = 2

EventSubtype = Union[ModuleEventSubtype, JoinEventSubtype, NetworkEventSubtype]


UARTConfig = namedtuple('UARTConfig', 'baudrate data_bits stop_bits parity flow_control')
RFConfig   = namedtuple('RFConfig',   'channel frequency min_dr max_dr')
Delay      = namedtuple('Delay',      'join_accept_1 join_accept_2 rx_window_1 rx_window_2')
McastAddr  = namedtuple('McastAddr',  'index addr nwkskey appskey')
Channel    = namedtuple('Channel',    'enabled freq alt_rx1 min_dr max_dr subband')


class ModemError(Exception):
    def __init__(self, message, errno):
        super().__init__(message)
        self.errno = errno


class UnknownCommand(ModemError):
    def __init__(self, message):
        super().__init__(message, -1)


class JoinFailed(Exception):
    def __init__(self, message):
        super().__init__(message)


class CLangEnum(Enum):
    def _generate_next_value_(name, start, count, last_values):
        if len(last_values) == 0: return 0
        return last_values[-1] + 1


@unique
class LoRaRegion(CLangEnum):
    AS923 = auto()
    AU915 = auto()
    CN470 = auto()
    CN779 = auto()
    EU433 = auto()
    EU868 = auto()
    KR920 = auto()
    IN865 = auto()
    US915 = auto()
    RU864 = auto()

    def __str__(self):
        return self.name


@unique
class LoRaMode(Enum):
    ABP  = 0
    OTAA = 1

    def __str__(self):
        return self.name


@unique
class LoRaNetwork(Enum):
    PRIVATE = 0
    PUBLIC  = 1

    def __str__(self):
        return self.name.lower()


class LoRaDataRate(Enum):
    def __str__(self):
        return self.name


@unique
class LoRaDataRateAS923(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7

@unique
class LoRaDataRateAU915(LoRaDataRate):
    SF12_125  = 0
    SF11_125  = 1
    SF10_125  = 2
    SF9_125   = 3
    SF8_125   = 4
    SF7_125   = 5
    SF8_500   = 6
    SF12_500  = 8
    SF11_500  = 9
    SF10_500  = 10
    SF9_500   = 11
    SF8_500_2 = 12
    SF7_500   = 13

@unique
class LoRaDataRateCN470(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_500  = 6
    FSK_50   = 7

@unique
class LoRaDataRateCN779(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7

@unique
class LoRaDataRateEU433(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7

@unique
class LoRaDataRateEU868(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7

@unique
class LoRaDataRateKR920(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5

@unique
class LoRaDataRateIN865(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    FSK_50   = 7

@unique
class LoRaDataRateUS915(LoRaDataRate):
    SF10_125  = 0
    SF9_125   = 1
    SF8_125   = 2
    SF7_125   = 3
    SF8_500   = 4
    SF12_500  = 8
    SF11_500  = 9
    SF10_500  = 10
    SF9_500   = 11
    SF8_500_2 = 12
    SF7_500   = 13

@unique
class LoRaDataRateRU864(LoRaDataRate):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7


def region_to_data_rate(value: str | LoRaRegion) -> LoRaDataRate:
    if isinstance(value, LoRaRegion):
        value = value.name
    else:
        value = value.upper()
    return globals()[f'LoRaDataRate{value}']



@unique
class LoRaClass(Enum):
    A = 0
    B = 1
    C = 2

    def __str__(self):
        return self.name


@unique
class Errno(Enum):
    OK                =   0
    ERR_UNKNOWN_CMD   =  -1
    ERR_PARAM_NO      =  -2
    ERR_PARAM         =  -3
    ERR_FACNEW_FAILED =  -4
    ERR_NO_JOIN       =  -5
    ERR_JOINED        =  -6
    ERR_BUSY          =  -7
    ERR_VERSION       =  -8
    ERR_MISSING_INFO  =  -9
    ERR_FLASH_ERROR   = -10
    ERR_UPDATE_FAILED = -11
    ERR_PAYLOAD_LONG  = -12
    ERR_NO_ABP        = -13
    ERR_NO_OTAA       = -14
    ERR_BAND          = -15
    ERR_POWER         = -16
    ERR_UNSUPPORTED   = -17
    ERR_DUTYCYCLE     = -18
    ERR_NO_CHANNEL    = -19
    ERR_TOO_MANY      = -20


@unique
class LogLevel(Enum):
    DISABLED = 0
    ERROR    = 1
    WARNING  = 2
    DEBUG    = 3
    ALL      = 4

    def __str__(self):
        return self.name.lower()


@unique
class DataFormat(Enum):
    BINARY = 0
    HEXADECIMAL = 1

    def __str__(self):
        return self.name.lower()


error_messages = {
    Errno.ERR_UNKNOWN_CMD.value   : 'Unknown command',
    Errno.ERR_PARAM_NO.value      : 'Invalid number of parameters',
    Errno.ERR_PARAM.value         : 'Invalid parameter value(s)',
    Errno.ERR_FACNEW_FAILED.value : 'Factory reset failed',
    Errno.ERR_NO_JOIN.value       : 'Device has not joined LoRaWAN yet',
    Errno.ERR_JOINED.value        : 'Device has already joined LoRaWAN',
    Errno.ERR_BUSY.value          : 'Resource unavailable: LoRa MAC is transmitting',
    Errno.ERR_VERSION.value       : 'New firmware version must be different',
    Errno.ERR_MISSING_INFO.value  : 'Missing firmware information',
    Errno.ERR_FLASH_ERROR.value   : 'Flash read/write error',
    Errno.ERR_UPDATE_FAILED.value : 'Firmware update failed',
    Errno.ERR_PAYLOAD_LONG.value  : 'Payload is too long',
    Errno.ERR_NO_ABP.value        : 'Only supported in ABP activation mode',
    Errno.ERR_NO_OTAA.value       : 'Only supported in OTAA activation mode',
    Errno.ERR_BAND.value          : 'Region is not supported',
    Errno.ERR_POWER.value         : 'Power value too high',
    Errno.ERR_UNSUPPORTED.value   : 'Not supported in the current region',
    Errno.ERR_DUTYCYCLE.value     : 'Cannot transmit due to duty cycling',
    Errno.ERR_NO_CHANNEL.value    : 'Channel unavailable due to LBT or error',
    Errno.ERR_TOO_MANY.value      : 'Too many link check requests'
}


def raise_for_error(response):
    errno = int(response[5:])
    try:
        errstr = error_messages[errno]
    except KeyError:
        errstr = 'Unknown error'

    if errno == -1:
        raise UnknownCommand(f'Unsupported command')
    else:
        raise ModemError(f'Command failed: {errstr} ({errno})', errno)


class TypeABZ(EventEmitter):
    port: serial.Serial | None
    prev_at: datetime | None

    def __init__(self, pathname: str, verbose: bool = False, guard: Optional[float] = None):
        super().__init__()
        self.pathname = pathname
        self.verbose = verbose
        self.hide_value = False
        self.guard = guard
        self.prev_at = None
        self.port = None

    def __str__(self):
        return self.pathname

    @property  # type: ignore
    @contextmanager
    def secret(self):
        try:
            self.hide_value = True
            yield
        finally:
            self.hide_value = False

    def detect_baud_rate(self, speeds=[9600, 19200, 38400, 4800], timeout=0.3) -> Optional[int]:
        if self.port is not None:
            raise Exception('Baudrate detection must be performed before the device is open')

        for speed in speeds:
            with serial.Serial(self.pathname, speed, timeout=timeout) as port:
                port.reset_input_buffer()
                port.reset_output_buffer()
                port.write(b'\rAT\r')
                if self.verbose:
                    print(f'< AT @ {speed}')
                port.flush()

                for c in b'+OK\r':
                    d = port.read(1)
                    if not len(d) or ord(d) != c:
                        if self.verbose:
                            print(f'! Incorrect response @ {speed}')
                        break
                else:
                    if self.verbose:
                        print(f'> +OK @ {speed}')
                    return speed
        return None

    def open(self, speed: int):
        self.speed = speed
        self.port = serial.Serial(self.pathname, speed)

        self.port.flush()
        self.port.reset_input_buffer()
        self.port.reset_output_buffer()
        self.response: "Queue[bytes]" = Queue()

        self.lock = RLock()
        self.thread = Thread(target=self.reader)
        self.thread.daemon = True
        self.thread.start()

    def close(self):
        self.port.close()

    def read_line(self) -> bytes:
        assert self.port is not None

        line: bytes = b''
        while True:
            c = self.port.read(1)
            if len(c) == 0:
                raise Exception('No data')
            if c == b'\r':
                continue
            if c == b'\n':
                if len(line) == 0:
                    continue
                return line
            line += c

    def reader(self):
        try:
            while True:
                try:
                    data = self.read_line()
                except:
                    break

                if self.verbose:
                    if self.hide_value:
                        msg = re.sub(b'^(.*)=.+$', b'\\1=<redacted>', data)
                    else:
                        msg = data
                    print(f'> {msg.decode("ascii", errors="replace")}')

                try:
                    if data.startswith(b'+EVENT'):
                        payload = data[7:]
                        if len(payload) == 0:
                            self.emit('event')
                        else:
                            params = tuple(map(int, payload.split(b',')))
                            if len(params) != 2:
                                raise Exception('Unsupported event parameters')

                            # For each event received from the LoRa module, we
                            # generate three events. The event "event" allows
                            # the application to subscribe to all event, event
                            # "event=x" allows the application to subscribe to
                            # all events from a specific subsystem, and
                            # event=x,y allows the application to subscribe to
                            # one specific event.
                            self.emit('event', *params)
                            self.emit(f'event={params[0]}', params[1])
                            self.emit(f'event={params[0]},{params[1]}')
                    elif data.startswith(b'+ANS'):
                        self.emit('answer', *tuple(map(int, data[5:].split(b','))))
                    elif data.startswith(b'+ACK'):
                        self.emit('ack', True)
                    elif data.startswith(b'+NOACK'):
                        self.emit('ack', False)
                    elif data.startswith(b'+RECV'):
                        port, size = tuple(map(int, data[6:].split(b',')))
                        # We use +2 here to skip an empty line sent by the modem
                        data = self.port.read(size + 2)
                        # The message is passed to the event callback as bytes
                        self.emit('message', port, data[2:])
                    else:
                        self.response.put_nowait(data)
                except Exception as error:
                    print(f'Ignoring reader thread error: {error}')
        finally:
            if self.verbose:
                print('Terminating reader thread')

    def send(self, cmd: bytes, flush=True):
        assert self.port is not None

        if self.verbose:
            if self.hide_value:
                msg = re.sub(b'^(.*)=.+$', b'\\1=<redacted>', cmd)
            else:
                msg = cmd

            print(f'< AT{msg.decode("ascii", errors="replace")}')

        self.port.write(b'AT' + cmd + b'\r')
        if flush:
            self.port.flush()

    def read_inline_response(self, timeout: float = None):
        try:
            response = self.response.get(timeout=timeout)
        except Empty:
            raise TimeoutError('No response received')
        try:
            if response.startswith(b'+ERR'):
                raise_for_error(response)
            elif response.startswith(b'+OK'):
                if len(response) > 3:
                    return response[4:]
            else:
                raise Exception('Invalid response')

        finally:
            self.response.task_done()

    def read_response(self, timeout: float = None):
        body: List[bytes] = []
        while True:
            try:
                line = self.response.get(timeout=timeout)
            except Empty:
                raise TimeoutError('Incomplete response received')
            else:
                try:
                    if len(body) == 0 and line.startswith(b'+ERR'):
                        raise_for_error(line)
                    elif line == b'+OK':
                        return b'\n'.join(body)
                    body.append(line)
                finally:
                    self.response.task_done()

    def AT(self, cmd: str = '', timeout: Optional[float] = 5, wait=True, inline=True, flush=True, encoding='ascii'):
        # Implement rudimentary throttling of AT commands send to the device. It
        # appears the original modem firmware cannot properly interpret AT
        # commands that come quickly after a previous response. Thus, if the
        # guard is enabled, we wait at least 100 milliseconds from the previous
        # response before sending the next AT command.
        if self.guard is not None:
            now = datetime.now()
            if self.prev_at is not None:
                if now - self.prev_at < timedelta(seconds=self.guard):
                    sleep(self.guard)

        rv = None
        with self.lock:
            # We intentionally do not add the errors keyword parameters to the
            # following encode function to alert the user if they use
            # incompatible encoding in their AT commands. The encode function
            # will raise an error in that case.
            self.send(cmd.encode(encoding), flush=flush)
            if wait:
                if inline:
                    rv = self.read_inline_response(timeout=timeout)
                else:
                    rv = self.read_response(timeout=timeout)

                # We assume the responses generated by the ATCI are encoded in
                # ASCII by default with no characters above 127. The encoding
                # can be changed with the encoding parameters. We also do not
                # want to raise an error no matter what the modem sends, hence
                # the errors keyword argument.
                if rv is not None:
                    rv = rv.decode(encoding, errors='replace')
            self.prev_at = datetime.now()
            return rv

    def wait_for_event(self, event: str, timeout: Optional[float] = None):
        q: "Queue[tuple]" = Queue()

        cb = lambda *params: q.put_nowait(params)
        self.once(event, cb)
        try:
            data = q.get(timeout=timeout)
            q.task_done()
            return data
        except Empty:
            self.off(event, cb)
            raise TimeoutError('Timed out')

    def flush(self):
        # Send CR in case there is some data in the modem's buffers
        self.port.write(b'\r')
        self.port.flush()
        self.port.reset_input_buffer()
        self.port.reset_output_buffer()

        # Read any input from the device until we time out.
        while True:
            try:
                self.response.get(timeout=0.2)
                self.response.task_done()
            except Empty:
                break


class ATCI(ABC):
    modem: TypeABZ

    def __init__(self, modem: TypeABZ):
        super().__setattr__('modem', modem)

    def __str__(self):
        return str(self.modem)

    # The following methods restrict the set of properties returned with dir()
    # to properties that implement AT commands and implement case-insensitive
    # access to those commands.

    @lru_cache(maxsize=None)
    def settings(self, cls=None, case=False) -> dict[str, Any]:
        props: "dict[str, Any]" = {}

        if cls is None:
            cls = self.__class__

        if cls.__base__ is not None:
            for name, value in self.settings(cls.__base__, case).items():
                props[name if case else name.lower()] = value

        for name, value in cls.__dict__.items():
            if not name.startswith('_') and isinstance(value, property):
                props[name if case else name.lower()] = value

        return props

    def __dir__(self):
        return self.settings(case=True).keys()

    def __getattr__(self, name):
        props = dir(self)

        for n in [name, name.lower(), name.upper()]:
            if n in props: return getattr(self, n)

        raise AttributeError(f'Unsupported setting')

    def __setattr__(self, name, value):
        props = dir(self)

        for n in [name, name.lower(), name.upper()]:
            if n in props: return super().__setattr__(n, value)

        raise AttributeError(f'Unsupported setting')

    def on(self, *args, **kwargs):
        return self.modem.on(*args, **kwargs)

    def of(self, *args, **kwargs):
        return self.modem.on(*args, **kwargs)


class MurataModem(ATCI):
    def reset(self):
        '''Initialize the LoRa modem into a known state.

        This method performs reboot, checks that the AT command interface is
        present and can be used.
        '''
        # Flush the AT command interface to maximize the likelyhood that the
        # following reboot command succeeds.
        self.modem.flush()

        # Reboot the device and wait for it to signal that it has rebooted. The
        # reboot applies any parameters that may have been changed before if the
        # application forgot to reboot the modem (e.g., baud rate).
        self.reboot()

        # Invoke empty AT command to make sure the AT command interface is still
        # working after reboot
        self.modem.AT()

    @property
    def uart(self):
        '''Return current configuration of the AT interface UART port.

        This property returns an UARTConfig object that contains parameters such
        as the baud rate, number of data bits, number of stop bits, and parity
        mode.

        The default configuration after factory reset is 1900 8N1.
        '''
        reply = self.modem.AT('+UART?').split(',')
        if len(reply) != 5:
            raise Exception('Unexpected reply to AT+UART')
        return UARTConfig(int(reply[0]), int(reply[1]), int(reply[2]), int(reply[3]), reply[4] == 1)

    @uart.setter
    def uart(self, value: UARTConfig | int):
        '''Configure the baud rate of the AT interface UART port.

        This property can only be used to configure the baud rate of the port.
        Other parameters such as data bits, parity, or stop bits cannot be
        configured. Only the following baud rate values are supported: 4800,
        9600, 19200, 38400. The configured value is permanently stored in NVM
        (EEPROM). The modem will switch to the newly configured baud rate after
        reboot.

        The default configuration of the UART port after factory reset is 19200
        8N1.
        '''
        if isinstance(value, tuple):
            value = UARTConfig(*value)

        self.modem.AT(f'+UART={value.baudrate if isinstance(value, UARTConfig) else value}')

    @property
    def ver(self):
        '''Return the version and build date of Murata Modem firmware.

        This property returns a tuple of two strings, where the first string is
        the version and the second string is the build date of Murata Modem.
        '''
        return self.modem.AT('+VER?').split(',')

    @property
    def dev(self):
        '''Return the model of the Type ABZ module hardware.

        This property always returns the string "ABZ".
        '''
        return self.modem.AT('+DEV?')

    model = dev

    def reboot(self):
        '''Restart the modem.

        This command can be used to restart the modem. The method blocks until
        the modem restarts.
        '''
        with self.modem.lock:
            self.modem.AT('+REBOOT')
            self.modem.wait_for_event('event=0,0')

    def facnew(self):
        '''Re-initialize all modem parameters to factory defaults.

        Upon restoring all parameters, the modem automatically performs a
        reboot. The method blocks until the modem has been restarted.
        '''
        with self.modem.lock:
            self.modem.AT('+FACNEW')
            self.modem.wait_for_event('event=0,1')

    factory_reset = facnew

    @property
    def band(self):
        '''Return the currently active region (RF band).

        Note: The RF transceiver in the Type ABZ module only supports 868 MHz
        and 915 MHz RF bands. Thus, only regions that use the two RF bands can
        be used.
        '''
        value = int(self.modem.AT('+BAND?'))
        try:
            value = LoRaRegion(value)
        except ValueError:
            pass
        return value

    @band.setter
    def band(self, value: str | LoRaRegion | int):
        '''Configure the region to be used by the modem.

        The following table summarizes the regions supported by the firmware.
        ID     0     1     2     3     4     5     6     7     8     9
        Region AS923 AU915 CN470 CN779 EU433 EU868 KR920 IN865 US915 RU864

        Note: The transceiver in the Type ABZ module only supports 868 MHz and
        915 MHz RF bands. Thus, the module will not be able to physically
        transmit in, e.g., EU433,  even though you could select the region in
        the firmware.

        When you select the same region as the currently active region, the
        modem will only respond with +OK. When you select a different region,
        the modem will perform a partial factory reset indicated by +EVENT=0,1,
        followed by a reboot indicated by +EVENT=0,0. A partial factory reset
        configures factory defaults for most LoRaWAN-related parameters, but
        leaves security keys, the DevNonce value, and system parameters such as
        the UART port baud rate unmodified.
        '''
        if type(value) == str:
            value = LoRaRegion[value.upper()]

        if isinstance(value, LoRaRegion):
            value = value.value

        wait = self.band.value != value
        with self.modem.lock:
            self.modem.AT(f'+BAND={value}')
            if wait:
                self.modem.wait_for_event('event=0,0')

    region = band

    # The following property is capitalized because "class" is a reserved word
    # in Python.
    @property
    def CLASS(self):
        '''Return currently configured LoRaWAN device class.

        The device class determines the bahavior of the LoRa receiver. In class
        A the receiver is only active during two short receive windows after
        each uplink transmission. In class B the receiver is periodically
        activated to receive downlinks from the network. In class C the receiver
        is active permanently, allowing the device to receive network downlinks
        at any time.
        '''
        value = int(self.modem.AT('+CLASS?'))
        try:
            value = LoRaClass(value)
        except ValueError:
            pass
        return value

    @CLASS.setter
    def CLASS(self, value: str | LoRaClass | int):
        '''Configure LoRaWAN device class.

        The device class determines the bahavior of the LoRa receiver. In class
        A the receiver is only active during two short receive windows after
        each uplink transmission. In class B the receiver is periodically
        activated to receive downlinks from the network. In class C the receiver
        is active permanently, allowing the device to receive network downlinks
        at any time.

        Please note that this property can be only used to switch between
        classes A and C. Switching to class B is currently not supported. The
        LoRaWAN class of the device can only be switched while the device is
        idle, i.e., when there is no uplink or downlink in progress.
        '''
        if type(value) == str:
            value = LoRaClass[value.upper()]

        if isinstance(value, LoRaClass):
            value = value.value

        self.modem.AT(f'+CLASS={value}')

    @property
    def mode(self):
        '''Return current LoRaWAN network activation mode.

        A LoRaWAN device can operate either in the over the air activation
        (OTAA) mode, or in the activation by provisioning (APB) mode. The
        default mode upon factory reset is ABP.
        '''
        value = int(self.modem.AT('+MODE?'))
        try:
            value = LoRaMode(value)
        except ValueError:
            pass
        return value

    @mode.setter
    def mode(self, value: str | LoRaMode | int):
        '''Configure LoRaWAN network activation mode.

        The value 1 switches the modem into the over the air activation (OTAA)
        mode. The value 0 switches the modem into the activation by provisioning
        (ABP) mode. The default mode upon factory reset is ABP.
        '''
        if type(value) == str:
            value = LoRaMode[value.upper()]

        if isinstance(value, LoRaMode):
            value = value.value

        self.modem.AT(f'+MODE={value}')

    @property
    def devaddr(self) -> str:
        '''Return the LoRaWAN device address (DevAddr) assigned to the modem.

        The DevAddr is a 32-bit number that consists of two parts: an address
        prefix and a network address. The address prefix is unique to each
        LoRaWAN network and is centrally allocated by the LoRa Alliance.

        In OTAA mode, the device address is assigned by the network server upon
        Join. In ABP mode, the device address must be manually configured using
        this property.

        The DevAddr is encoded in a hexadecimal format with the most significant
        byte (MSB) transmitted first. Thus, the address prefix is encoded first.

        Upon factory reset, and whenever the DevAddr consists of all zeroes, the
        modem generates a random DevAddr.
        '''
        return self.modem.AT('+DEVADDR?')

    @devaddr.setter
    def devaddr(self, value: str):
        '''Configure the LoRaWAN device address (DevAddr).

        The DevAddr is a 32-bit number that consists of two parts: an address
        prefix and a network address. The address prefix is unique to each
        LoRaWAN network and is centrally allocated by the LoRa Alliance. If you
        need to manually configure the DevAddr, select an address within the
        range assigned to your network. If your network does not have an address
        prefix, or if you don't know it, you can select a DevAddr from the two
        ranges allocated for experimental or private nodes:

          1. 00000000/7 : 00000000 - 01ffffff
          2. 02000000/7 : 02000000 - 03ffffff

        In OTAA mode, the device address is assigned by the network server upon
        Join. In ABP mode, the device address must be manually configured using
        this command.

        You can use the following Python snippet to generate a random DevAddr
        that falls into the second range from above:

          import random
          print(format(random.randint(0x02000000, 0x03ffffff), '08X'))

        The DevAddr is encoded in a hexadecimal format with the most significant
        byte (MSB) transmitted first. Thus, the address prefix is encoded first.

        Upon factory reset, and whenever the DevAddr consists of all zeroes, the
        modem generates a random DevAddr in the second range from above during
        start.
        '''
        self.modem.AT(f'+DEVADDR={value.upper()}')

    dev_addr = devaddr

    @property
    def deveui(self) -> str:
        '''Return LoRaWAN device EUI (DevEUI).

        The DevEUI is a 64-bit globally unique extended identifier assigned to
        the device by the manufacturer. The DevEUI is encoded in a hexadecimal
        format.
        '''
        return self.modem.AT('+DEVEUI?')

    @deveui.setter
    def deveui(self, value: str):
        '''Configure the LoRaWAN device EUI (DevEUI).

        The DevEUI is a 64-bit globally unique extended identifier assigned to
        the device by the manufacturer. The DevEUI encoded in a hexadecimal
        format. Note: The value configured through this setter will be preserved
        during factory reset, i.e., the DevEUI will NOT revert to the original
        value derived from the MCU unique identifier.
        '''
        self.modem.AT(f'+DEVEUI={value.upper()}')

    dev_eui = deveui

    @property
    def appeui(self) -> str:
        '''Return the LoRaWAN AppEUI (JoinEUI).

        In recent LoRaWAN specifications, the parameter was renamed to JoinEUI.
        The AppEUI is a 64-bit globally unique identifier that identifiers the
        join server (or the application server). This value is meant to identify
        the server that shares the device's root application (AppKey) and
        network (NwkKey) keys. The AppEUI/JoinEUI is encoded in a hexadecimal
        format.

        The default value is 0101010101010101.
        '''
        return self.modem.AT('+APPEUI?')

    @appeui.setter
    def appeui(self, value: str):
        '''Configure LoRaWAN AppEUI (JoinEUI).

        In recent LoRaWAN specifications, the parameter was renamed to JoinEUI.
        The AppEUI is a 64-bit globally unique identifier that identifiers the
        join server (or the application server). This value is meant to identify
        the server that shares the device's root application (AppKey) and
        network (NwkKey) keys. The AppEUI/JoinEUI is encoded in a hexadecimal
        format.

        The default value is 0101010101010101.
        '''
        self.modem.AT(f'+APPEUI={value.upper()}')

    app_eui = appeui

    @property
    def nwkskey(self) -> str:
        '''Return LoRaWAN 1.0 network session key (NwkSKey).

        The network session key is a 128-bit symmetric key that secures
        communication between the device and the network server. In OTAA mode,
        the key is automatically negotiated between the device and the network
        server during Join. In ABP mode, a unique key must be provided by the
        application during provisioning.

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('+NWKSKEY?')

    @nwkskey.setter
    def nwkskey(self, value: str):
        '''Set LoRaWAN 1.0 network session key (NwkSKey).

        The network session key is a 128-bit symmetric key that secures
        communication between the device and the network server. In OTAA mode,
        the key is automatically negotiated between the device and the network
        server during Join. Thus, there is no need for the application to set
        this property. In ABP mode, a unique key must be provided by the
        application during provisioning.

        You can generate a new random NwkSKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'+NWKSKEY={value.upper()}')

    nwk_s_key = nwkskey

    @property
    def appskey(self) -> str:
        '''Return LoRaWAN application session key (AppSKey).

        The application session key is a 128-bit symmetric key that secures
        communication between the device and the application server. In OTAA
        mode, the key automatically negotiated between the device and the
        network during Join. In ABP mode, a unique key must be provided by the
        application during provisioning.

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('+APPSKEY?')

    @appskey.setter
    def appskey(self, value: str):
        '''Set LoRaWAN application session key (AppSKey).

        The application session key is a 128-bit symmetric key that secures
        communication between the device and the application server. In OTAA
        mode, the key automatically negotiated between the device and the
        network during Join. Thus, there is no need for the application to use
        this setter. In ABP mode, a unique key must be provided by the
        application during provisioning.

        You can generate a new random NwkSKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'+APPSKEY={value.upper()}')

    app_s_key = appskey

    @property
    def appkey(self) -> str:
        '''Return LoRaWAN 1.0 root application key (AppKey).

        The AppKey is a 128-bit symmetric key that is used to derive an
        application session key (AppSKey) during LoRaWAN OTAA Join.

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('+APPKEY?')

    @appkey.setter
    def appkey(self, value: str):
        '''Set LoRaWAN 1.0 root application key (AppKey).

        The AppKey is a 128-bit symmetric key that is used to derive an
        application session key (AppSKey) during LoRaWAN OTAA Join.

        You can generate a new random AppKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'+APPKEY={value.upper()}')

    app_key = appkey

    def join(self, timeout=120):
        '''Join LoRaWAN network in over-the-air-activation (OTAA) mode.

        If the modem is in OTAA mode (AT+MODE=1), this command sends a Join
        LoRaWAN request to the network (Join Server). The Join request will be
        sent with data rate 0. If the modem is in ABP mode (AT+MODE=0), this
        command does nothing.

        This is an asynchronous command. The modem responds with +OK or +ERR
        immediately. An ok response indicates that the Join request was
        successfully enqueued and could be sent. An error response indicates
        that the modem could not send a Join request at this time, e.g., due to
        duty cycling restrictions or some other operation in progress.

        The methods returns upon receiving a JoinAccept answer from the Join
        Server. If no JoinAnswer is received, the method raises an exception
        instead to indicate that the request timed out.

        Note: Join requests are subject to additional duty cycling restrictions
        even in regions that otherwise do not use duty cycling. See the
        documentation for the propert "joindc" for more details.
        '''
        with self.modem.lock:
            self.modem.AT('+JOIN')
            status = self.modem.wait_for_event('event', timeout=timeout)
            if status == (1, 1):
                return
            elif status == (1, 0):
                raise JoinFailed('OTAA Join failed')
            else:
                raise Exception('Unsupported event received')

    @property
    def joindc(self):
        '''Return the current value of OTAA Join duty cycling setting.

        Uplink messages sent synchronously by a large number of devices in
        response to a common external event, e.g., network outage, have the
        potential to trigger a network-wide synchronization, also known as the
        thundering herd problem. In LoRaWAN networks, OTAA Join is one of such
        messages. A large number of devices transmitting (and retransmitting)
        OTAA Join requests synchronously could overload the available radio
        channels or the LoRaWAN network server.

        The LoRaWAN specification in Section "7 Retransmissions back-off"
        defines two mechanisms designed to mitigate RF or network overload:

          1. Additional duty cycling of Join requests;
          2. Join retransmission randomization.

        Join requests transmitted by LoRaWAN devices are subject to additional
        duty cycling restrictions, in addition to any duty cycling already
        enforced by the region (band). The additional duty cycling restrictions
        placed on Join request transmissions can be summarized as follows:

          1. The aggregate transmission time in the 1st hour since device start
             must not exceed 36 seconds;
          2. The aggregate transmission time between the 2nd and 11th hour since
             device start must not exceed 36 seconds;
          3. The aggregate transmission time over the most recent 24 hours after
             the 11th hour must not exceed 8.7 seconds.

        When retransmitting a Join request, LoRaWAN-compliant devices are also
        required to postpone the retransmission by a small randomized delay to
        prevent network-wide synchronized transmissions.

        This property is set to True if Join duty cycling is enabled in the
        device and False otherwise.

        Join duty cycling is enabled by default in all regions, irrespective of
        whether the region (band) enforces RF duty cycling. This settings is
        independent of AT+DUTYCYCLE. Disabling one does not disable the other
        and vice versa.
        '''
        return int(self.modem.AT('+JOINDC?')) == 1

    @joindc.setter
    def joindc(self, value: bool | int):
        '''Configure OTAA Join duty cycling setting.

        Uplink messages sent synchronously by a large number of devices in
        response to a common external event, e.g., network outage, have the
        potential to trigger a network-wide synchronization, also known as the
        thundering herd problem. In LoRaWAN networks, OTAA Join is one of such
        messages. A large number of devices transmitting (and retransmitting)
        OTAA Join requests synchronously could overload the available radio
        channels or the LoRaWAN network server.

        The LoRaWAN specification in Section "7 Retransmissions back-off"
        defines two mechanisms designed to mitigate RF or network overload:

          1. Additional duty cycling of Join requests;
          2. Join retransmission randomization.

        Join requests transmitted by LoRaWAN devices are subject to additional
        duty cycling restrictions, in addition to any duty cycling already
        enforced by the region (band). The additional duty cycling restrictions
        placed on Join request transmissions can be summarized as follows:

          1. The aggregate transmission time in the 1st hour since device start
             must not exceed 36 seconds;
          2. The aggregate transmission time between the 2nd and 11th hour since
             device start must not exceed 36 seconds;
          3. The aggregate transmission time over the most recent 24 hours after
             the 11th hour must not exceed 8.7 seconds.

        When retransmitting a Join request, LoRaWAN-compliant devices are also
        required to postpone the retransmission by a small randomized delay to
        prevent network-wide synchronized transmissions.

        This setter can be used to enable or disable the additional Join duty
        cycling restrictions. Note: The command only controls Join request duty
        cycling; it does not influence retransmission randomization which is
        always in effect.

        Warning: All LoRaWAN-compliant devices must implement Join duty cycling
        and retransmission randomization. Please exercise caution when disabling
        Join duty cycling.

        Join duty cycling is enabled by default in all regions, irrespective of
        whether the region (band) enforces RF duty cycling. This settings is
        independent of AT+DUTYCYCLE. Disabling one does not disable the other
        and vice versa.
        '''
        if type(value) == bool:
            value = 1 if value is True else 0
        self.modem.AT(f'+JOINDC={value}')

    join_dc = joindc
    join_duty_cycle = joindc

    def lncheck(self, piggyback = False, timeout: float = 10):
        '''Perform a link check between the modem and the network.

        This command sends a LoRaWAN LinkCheckReq MAC command to the network
        server. The command can be sent either immediately or together with the
        next regular uplink transmission. By default, when the command is
        invoked with piggyback=False, the modem sends the LinkCheckReq MAC
        command immediately in a dedicated unconfirmed uplink to port 0 with no
        payload. If you wish to send the request as part of the next regular
        uplink instead, invoke the command with piggyback=True.

        The method block until a LinkCheckAns message is received from the
        network server. The method returns a tuple of two integers. The first
        integer represents the link margin. the second integer represents the
        number of gateways that heard the LinCheckReq uplink. If no answer is
        received within the timeout, the method raises an exception.

        The margin value represent the strength of the device's signal relative
        to the demodulation floor at the gateway. The value is dB. A value of 0
        indicates that the gateway can barely receive uplinks from the device. A
        value of 12 indicates that the uplink was received 12 dB above the
        demodulation floor. The range of the margin value is 0 to 254.

        The gateway count value represents the number of gateways that
        successfully received the uplink carrying the LinkCheckReq MAC command.
        If multiple gateways received the uplink, this margin value discussed in
        the previous paragraph represents the margin of the gateway selected by
        the network server for downlinks to the device, usually the gateway with
        the best margin.

        Note: Link check requests are not retransmitted. The modem will send
        only one link check request for each invocation of the method.
        '''
        q: "Queue[tuple]" = Queue()
        cb = lambda *params: q.put_nowait(params)
        self.modem.once('event', cb)
        self.modem.once('answer', cb)
        try:
            with self.modem.lock:
                self.modem.AT(f'+LNCHECK={1 if piggyback is True else 0}')
                event = q.get(timeout=timeout)
                if event[1] == 1:
                    rc, margin, count = q.get(timeout=0.2)
                    if rc != 2:
                        raise Exception('Invalid answer code')
                    return (margin, count)
        finally:
            self.modem.off('event', cb)
            self.modem.off('answer', cb)

    link_check = lncheck

    @property
    def rfparam(self):
        '''Not yet implemented.
        '''
        data = tuple(self.modem.AT('+RFPARAM?').split(';'))
        if int(data[0]) != len(data) - 1:
            raise Exception('Could not parse RFPARAM response')
        return tuple(map(lambda v: RFConfig(*tuple(map(int, v.split(',')))), data[1:]))

    @rfparam.setter
    def rfparam(self, value: RFConfig):
        '''Not yet implemented.
        '''
        self.modem.AT(f'+RFPARAM={value.channel},{value.frequency},{value.min_dr},{value.max_dr}')

    rf_param = rfparam

    @property
    def rfpower(self):
        '''Return the currently configured RF output power.

        The property returns a tuple of two integers: (mode, index). The first
        value represents the transmitter mode (0 for RFO, 1 for PABOOST). In the
        open firmware, the mode value is always 0 (the open firmware selects the
        appropriate mode automatically). The meaning of the index value is
        region specific.

        In the US915 region, the index value represents the RF output power
        relative to the fixed maximum of 30 dBm. Please note that in the US915
        region the maximum allowed RF output power also depends on the number of
        active channels (channel mask) and the selected data rate. Thus, the
        actual RF output power may actually be lower than the values listed in
        the following table. With SF8/500kHz the RF output power is limisted to
        25 dBm. With less than 50 channels active, the output power is further
        capped to 21 dBm.

        In all other regions, the index values represents an RF output power
        relative to the MaxEIRP value configured through the property maxeirp.
        The range is from 0 to 14. In regions with a lower maximum power, higher
        index values are not supported. Only up to 2 dBm.

        Upon factory reset, the device is configured for the EU868 region, with
        maximum EIRP set to 16 dBm, and with an RFPOWER index of 1. This
        configuration translates to the default transmit power of 14 dBm (25
        mW).
        '''
        return tuple(map(int, self.modem.AT('+RFPOWER?').split(',')))

    @rfpower.setter
    def rfpower(self, value: Tuple[int, int]):
        '''Set the RF output power.

        The value must be a tuple of two integers: (mode, index). The first
        value controls the transmitter mode (0 for RFO, 1 for PABOOST). In the
        open firmware, the transmitter mode is selected automatically and this
        value is unused, but the syntax has been retained for compatibility with
        the original firmware. The meaning of the index value is region
        specific.

        In the US915 region, the index value selects the maximum RF output power
        relative to the fixed maximum of 30 dBm. Please note that in the US915
        region the maximum allowed RF output power also depends on the number of
        active channels (channel mask) and the selected data rate. Thus, the
        actual RF output power may actually be lower than the values listed in
        the following table. With SF8/500kHz the RF output power is limisted to
        25 dBm. With less than 50 channels active, the output power is further
        capped to 21 dBm.

        In all other regions, the index values selects an RF output power
        relative to the MaxEIRP value configured through the property maxeirp.
        Please note that not all values may be available in all regions.
        '''
        self.modem.AT(f'+RFPOWER={value[0]},{value[1]}')

    rf_power = rfpower

    @property
    def nwk(self):
        '''Return the public/private LoRaWAN network setting value.

        Each LoRa packets begins with a preamble used to synchronize the
        transmitter and receiver. The preamble includes a synchronization word
        which indicates the network(s) the packet is intended for. One
        synchronization word has been standardized for LoRaWAN networks, both
        public such as The Things Network and private such as commercial LoRaWAN
        networks. Networks that are not LoRaWAN-compatible should use a
        different synchronization word. A different synchronization word ensures
        that packets destined to LoRaWAN will not unnecessarily wake up private
        networks operating in the same region and channels and vice versa.

        This property determines which synchronization word is used by the
        device. A value of 1 indicates the synchronization word for LoRaWAN is
        used (0x34). A value of 0 indicates the synchronization word for private
        networks (0x12) is used.

        The default value upon factor reset is 1.
        '''
        value = int(self.modem.AT('+NWK?'))
        try:
            value = LoRaNetwork(value)
        except ValueError:
            pass
        return value

    @nwk.setter
    def nwk(self, value: str | LoRaNetwork | bool | int):
        '''Configure the preamble synchronization word to be used by the device.

        Each LoRa packets begins with a preamble used to synchronize the
        transmitter and receiver. The preamble includes a synchronization word
        which indicates the network(s) the packet is intended for. One
        synchronization word has been standardized for LoRaWAN networks, both
        public such as The Things Network and private such as commercial LoRaWAN
        networks. Networks that are not LoRaWAN-compatible should use a
        different synchronization word. A different synchronization word ensures
        that packets destined to LoRaWAN will not unnecessarily wake up private
        networks operating in the same region and channels and vice versa.

        This property can be used to configure the synchronization word used by
        the device. A value of 1 selects the synchronization word for LoRaWAN
        (0x34). A value of 0 selects the synchronization word used for private
        networks (0x12). In most cases, if you are connecting to
        LoRaWAN-compatible network, you should use 1 here. This is also the
        default value.
        '''
        if type(value) == str:
            value = LoRaNetwork[value.upper()]

        if isinstance(value, LoRaNetwork):
            value = value.value

        if type(value) == bool:
            value = 1 if value else 0

        self.modem.AT(f'+NWK={value}')

    network = nwk

    @property
    def adr(self):
        '''Return whether adaptive data rate (ADR) is enabled.

        When True, the device has ADR enabled for uplinks and will adjust the
        data rate, transmit power, and number of retransmissions based on ADR
        parameters received from the network server. When False, ADR requests to
        adjust these parameters from the network server are ignored.

        The default value upon factory reset is True (ADR enabled).
        '''
        return self.modem.AT('+ADR?') == '1'

    @adr.setter
    def adr(self, value: bool | int):
        '''Enable or disable adaptive data rate (ADR).

        When set to True, the device indicates support for ADR to the network
        server and will adjust the data rate, transmit power, and number of
        retransmissions based on ADR requests from the network server. When set
        to False, ADR requests to adjust these parameters from the network
        server are ignored.

        The default value upon factory reset is True (ADR enabled).
        '''
        if type(value) == bool:
            value = 1 if value is True else 0

        self.modem.AT(f'+ADR={value}')

    adaptive_data_rate = adr

    def parse_data_rate(self, value: str | LoRaDataRate | int) -> int:
        if isinstance(value, str):
            value = region_to_data_rate(self.region)[value.upper()] # type: ignore

        if isinstance(value, LoRaDataRate):
            value = value.value

        assert type(value) == int
        return value

    @property
    def dr(self):
        '''Return the data rate currently used for uplink messages.

        The returned value is an integer index within the range of 0 (inclusive)
        to 15 (inclusive). Each index value maps to a combination of modulation
        type, spread factor, and channel bandwidth. The mappings are defined in
        LoRaWAN regional parameters.

        Note: When ADR is enabled, the network server may change the value
        configured through this property.

        The default value upon factory reset is 0.
        '''
        value = int(self.modem.AT('+DR?'))
        try:
            value = region_to_data_rate(self.region)(value)
        except ValueError:
            pass
        return value

    @dr.setter
    def dr(self, value: str | LoRaDataRate | int):
        '''Configure the data rate for uplink messages.

        The value is an integer index within the range of 0 (inclusive) to 15
        (inclusive). Each index value maps to a combination of modulation type,
        spread factor, and channel bandwidth. The mappings are defined in
        LoRaWAN regional parameters.

        Note: When ADR is enabled, the network server may change the value
        configured through this property.

        The default value upon factory reset is 0.
        '''
        value = self.parse_data_rate(value)
        self.modem.AT(f'+DR={value}')

    data_rate = dr

    @property
    def delay(self):
        '''Return receive window time offsets.

        This property can be used to query the time offset of the receive window
        1 (RX1), receive window 2 (RX2), JoinAccept window 1 (JoinAccept RX1),
        and JoinAccept window 2 (JoinAccept RX2). The times are encoded into a
        tuple as follows: (<RX1>,<RX2>,<JoinAccept RX1>,<JoinAccept RX2>). All
        times represent the number of milliseconds since the end of the
        preceeding uplink transmission.

        The network server can change these values remotely using the
        RXTimingSetupReq MAC command.

        The default value upon factory reset is (5000,6000,1000,2000).
        '''
        return Delay(*map(int, self.modem.AT('+DELAY?').split(',')))

    @delay.setter
    def delay(self, value: str | Delay | tuple):
        '''Configure receive window time offsets.

        Set the time offset of the receive window 1 (RX1), receive window 2
        (RX2), JoinAccept window 1 (JoinAccept RX1), and JoinAccept window 2
        (JoinAccept RX2). The times must be encoded into a tuple as follows:
        (<RX1>,<RX2>,<JoinAccept RX1>,<JoinAccept RX2>). All times represent the
        number of milliseconds since the end of the preceeding uplink
        transmission.

        The network server can change these values remotely using the
        RXTimingSetupReq MAC command.

        Note 1: If you change any of the values manually, make sure to
        communicate the new values to the network server out of band, otherwise
        your device may not be able to communicate with the network.

        Note 2: The RX2 value must be greater than RX1. The JoinAccept RX2 value
        must be greater than JoinAccept RX1.

        The default value upon factory reset is (5000,6000,1000,2000).
        '''
        if type(value) == str:
            value = tuple(map(lambda s: int(s.strip()), value.split(',')))

        if len(value) != 4:
            raise Exception('Invalid value for setting delay')

        self.modem.AT(f'+DELAY={value[0]},{value[1]},{value[2]},{value[3]}')

    @property
    def adrack(self):
        '''Not yet implemented.
        '''
        return tuple(map(int, self.modem.AT('+ADRACK?').split(',')))

    @adrack.setter
    def adrack(self, value: Tuple[int, int]):
        '''Not yet implemented.
        '''
        self.modem.AT(f'+ADRACK={value[0]},{value[1]}')

    adr_ack = adrack

    @property
    def rx2(self):
        '''Return the channel frequency and data rate of receive window 2 (RX2).

        This property returns a tuple (<frequency>,<datarate_index>). The
        frequency value is in Hz. The datarate_index value is one of the data
        rate index values available for the currently selected region.

        The network server can update these values remotely through the
        RXParamSetupReq MAC command.

        The default value upon factory reset is (869525000,0) (the default
        region is EU868).
        '''
        freq, dr = map(int, self.modem.AT('+RX2?').split(','))
        try:
            dr = region_to_data_rate(self.region)(dr)
        except ValueError:
            pass
        return (freq, dr)

    @rx2.setter
    def rx2(self, value: Tuple[int, str | LoRaDataRate | int]):
        '''Set the channel frequency and data rate of receive window 2 (RX2).

        This property expects a tuple (<frequency>,<datarate_index>). The
        frequency value is in Hz. The datarate_index value is one of the data
        rate index values available for the currently selected region.

        The network server can update these values remotely through the
        RXParamSetupReq MAC command.

        The default value upon factory reset is (869525000,0) (the default
        region is EU868).

        TODO: Add support for str datarate_index type
        '''
        dr = self.parse_data_rate(value[1])
        self.modem.AT(f'+RX2={int(value[0])},{dr}')

    @property
    def dutycycle(self):
        '''Return whether duty cycling is enabled.

        The default value depends on the currently active region. It is True in
        regions that enforce duty cycling and False in regions that do not use
        duty cycling.
        '''
        return int(self.modem.AT('+DUTYCYCLE?')) == 1

    @dutycycle.setter
    def dutycycle(self, value: bool | int):
        '''Configure duty cycling in regions that enforce duty cycling.

        If your device operates in a region that enforces duty cycling, this
        property can be used to disable duty cycling. Please note that the
        property cannot be used to enable duty cycling in regions that do not
        enforce it.

        The default value depends on the currently active region. It is True in
        regions that enforce duty cycling and False in regions that do not use
        duty cycling.
        '''
        if type(value) == bool:
            value = 1 if value is True else 0
        self.modem.AT(f'+DUTYCYCLE={value}')

    duty_cycle = dutycycle

    @property
    def sleep(self):
        '''Return whether the modem enters a low-power sleep mode when idle.

        When set to True (the default), the MCU will go to sleep. When set to
        False, the MCU will never sleep.

        This setting only affects the MCU. It does not prevent the SX1276
        transceiver from being put to sleep.

        Warning: Disabling sleep will have a negative impact on the battery life
        of the device. This property is intended for development and debugging
        purposes only. During normal operation it should always be True.
        '''
        return int(self.modem.AT('+SLEEP?')) == 1

    @sleep.setter
    def sleep(self, value: bool | int):
        '''Configure whether the modem should go to sleep mode when idle.

        When set to True (the default), the MCU will go to sleep. When set to
        False, the MCU will never sleep.

        This property only affects the MCU. It does not prevent the SX1276
        transceiver from being put to sleep.

        Warning: Disabling sleep will have a negative impact on the battery life
        of the device. This setting is intended for development and debugging
        purposes only. During normal operation it should always be on.
        '''
        if type(value) == bool:
            value = 1 if value is True else 0

        self.modem.AT(f'+SLEEP={value}')

    @property
    def port(self):
        '''Return the default port number for uplinks.

        The methods `utx` and `ctx` send an uplink to the default port number
        configured with this property.

        The default port number is 2.
        '''
        return int(self.modem.AT('+PORT?'))

    @port.setter
    def port(self, value: int):
        '''Set the default port number for uplinks.

        The methods `utx` and `ctx` send an uplink to the default port number
        configured with this property.

        The value must be in <1, 222>. The default port number is 2.
        '''
        self.modem.AT(f'+PORT={value}')

    @property
    def rep(self):
        '''Return the number of unconfirmed uplink transmissions.

        Each unconfirmed uplink is transmitted up to the number of times
        configured through this property. The retransmissions stop as soon as
        the device receives a confirmation (any downlink) from the network
        server.

        Note: This parameter overrides the number of retransmissions remotely
        configured by the network server through adaptive data rate (ADR) when
        ADR is active.

        The default value is 1.
        '''
        return int(self.modem.AT('+REP?'))

    @rep.setter
    def rep(self, value: int):
        '''Set the number of unconfirmed uplink transmissions.

        Each unconfirmed uplink is transmitted up to the number of times
        configured through this property. The retransmissions stop as soon as
        the device receives a confirmation (any downlink) from the network
        server.

        Note: This parameter overrides the number of retransmissions remotely
        configured by the network server through adaptive data rate (ADR) when
        ADR is active.

        The configured value must be in <1, 15>. The default value is 1.
        '''
        self.modem.AT(f'+REP={value}')

    @property
    def dformat(self):
        '''Return the message payload format used by the modem.

        This property return the message payload format expected by AT commands
        that send uplinks such at AT+PUTX and AT+PCTX, and generated by the
        event notifications for received downlinks. If set to 0, the modem
        expects and sends message payloads in binary form. If set to 1, the
        modem expects and sends message payloads encoded in a hexadecimal
        format. The hexadecimal format is primarily useful for applications or
        transports that cannot handle full 8-bit ASCII data.

        Note: With the hexadecimal format turned on, the payload length in
        AT+PUTX, AT+PCTX, AT+UTX, AT+CTX, and received messages is still in
        bytes, even though twice as many characters need to be sent over the
        UART port (two hexadecimal characters for each byte).

        The default value is 0 (binary format).
        '''
        value = int(self.modem.AT('+DFORMAT?'))
        try:
            value = DataFormat(value)
        except ValueError:
            pass
        return value

    @dformat.setter
    def dformat(self, value: str | DataFormat | int):
        '''Set the format of message payload.

        This property controls the message payload format expected by AT
        commands that send uplinks such at AT+PUTX and AT+PCTX, and generated by
        the event notification +RECV for received downlinks. If this parameter
        is set to 0, the modem expects and sends message payloads in binary
        form. If set to 1, the modem expects and sends message payloads encoded
        in a hexadecimal format. The hexadecimal format is primarily useful for
        applications or transports that cannot handle full 8-bit ASCII data.

        Note: With the hexadecimal format turned on, the payload length in
        AT+PUTX, AT+PCTX, AT+UTX, AT+CTX, and +RECV is still in bytes, even
        though twice as many characters need to be sent over the UART port (two
        hexadecimal characters for each byte).

        The default value is 0 (binary format).
        '''
        if type(value) == str:
            value = DataFormat[value.upper()]

        if isinstance(value, DataFormat):
            value = value.value

        self.modem.AT(f'+DFORMAT={value}')

    data_encoding = dformat

    @property
    def to(self):
        '''Return message payload transmission timeout.

        The AT commands AT+UTX, AT+CTX, AT+PUTX, AT+PCTX all expect that the
        application transmits the uplink payload immediately after the AT
        command. To prevent the modem from locking up due to transmission
        errors, it expects that the entire payload arrives within the time
        configured through this AT command. If not, the modem responds with +OK
        and sends an incomplete uplink with only the data that arrived within
        the time.

        The minimum value for this parameter is a function of the baud rate of
        the UART port, the maximum size of the payload, whether or not
        hexadecimal payload encoding is in use, and on the current LoRaWAN data
        rate.

        The value must be in the range <1, 65535> (milliseconds). The default
        value is 1000 milliseconds.
        '''
        return int(self.modem.AT('+TO?'))

    @to.setter
    def to(self, value: int):
        '''Set message payload transmission timeout.

        The AT commands AT+UTX, AT+CTX, AT+PUTX, AT+PCTX all expect that the
        application transmits the uplink payload immediately after the AT
        command. To prevent the modem from locking up due to transmission
        errors, it expects that the entire payload arrives within the time
        configured through this AT command. If not, the modem responds with +OK
        and sends an incomplete uplink with only the data that arrived within
        the time.

        The minimum value for this parameter is a function of the baud rate of
        the UART port, the maximum size of the payload, whether or not
        hexadecimal payload encoding is in use, and on the current LoRaWAN data
        rate.

        The value must be in the range <1, 65535> (milliseconds). The default
        value is 1000 milliseconds.
        '''
        self.modem.AT(f'+TO={value}')

    def utx(self, data: bytes, timeout: float = None, hex = False):
        '''Send unconfirmed uplink message to the LoRaWAN network.

        The uplink will be transmitted up to `rep` times. The modem notifies the
        application of each retransmission. Any downlink message received by the
        modem from the network stops the retransmissions. Note that no +ACK or
        +NOACK will be generated even if an acknowledgement is received from the
        network. The value of `rep` overrides retransmission count configured by
        the network server through ADR.

        The uplink will be sent to the port number configured through the
        property `port`. The default port number is 2. If you wish to send the
        uplink to a different port number, use the method 'putx' instead.

        If `dformat` is set to 1, the application is expected to trasmit the
        payload encoded in a hexadecimal format and the parameter hex must be
        set to True.

        The maximum size of the payload depends on the current LoRaWAN data
        rate. The size could be reduced further if the modem needs to piggyback
        any MAC commands onto the uplink. If the payload does not fit in the
        uplink, the modem responds with +ERR=-12. If any MAC commands waited to
        be piggybacked, the modem internally sends an empty uplink to "flush"
        the MAC commands even when it returns +ERR=-12 to the application. This
        is to ensure that the next uplink transmission will have as much space
        as possible dedicated to application payload.
        '''
        self.tx(data, confirmed=False, timeout=timeout, hex=hex)

    def ctx(self, data: bytes, timeout: float = None, hex = False) -> bool:
        '''Send confirmed uplink message to the LoRaWAN network.

        The uplink will be transmitted up to `rtynum` times. The modem notifies
        the application of each retransmission by +EVENT=2,2. Any downlink
        message received by the modem from the network stops the retransmissions
        and the modem sends +ACK to the application. If no acknowledgement is
        received after `rtynum` transmissions, the modem generates +NOACK. The
        value of `rtynum` overrides retransmission count configured by the
        network server through ADR.

        The uplink will be sent to the port number configured through the
        property `port`. The default port number is 2. If you wish to send the
        uplink to a different port number, use the method `pctx` instead.

        If `dformat` is set to 1, the application is expected to trasmit the
        payload encoded in a hexadecimal format. The parameter hex must be set
        to True in this case.

        The maximum size of the payload depends on the current LoRaWAN data
        rate. The size could be reduced further if the modem needs to piggyback
        any MAC commands onto the uplink. If the payload does not fit in the
        uplink, the modem responds with +ERR=-12. If any MAC commands waited to
        be piggybacked, the modem internally sends an empty uplink to "flush"
        the MAC commands even when it returns +ERR=-12 to the application. This
        is to ensure that the next uplink transmission will have as much space
        as possible dedicated to application payload.
        '''
        rv = self.tx(data, confirmed=True, timeout=timeout, hex=hex)
        assert rv is not None
        return rv

    def tx(self, data: bytes, confirmed = False, timeout: float = None, hex = False) -> Optional[bool]:
        assert self.modem.port is not None
        type = 'C' if confirmed else 'U'
        with self.modem.lock:
            self.modem.AT(f'+{type}TX {len(data)}', wait=False, flush=False)
            if hex:
                self.modem.port.write(binascii.hexlify(data))
            else:
                self.modem.port.write(data)
            self.modem.port.flush()
            self.modem.read_inline_response()
            if confirmed:
                # The +ACK +NOACK events carry one boolean value (True for +ACK,
                # False for +NOACK)
                return self.modem.wait_for_event('ack', timeout=timeout)[0]
            else:
                return None

    @property
    def mcast(self):
        '''Not yet implemented.
        '''
        data = tuple(self.modem.AT('+MCAST?').split(';'))
        if int(data[0]) != len(data) - 1:
            raise Exception('Could not parse MCAST response')
        return tuple(map(lambda v: McastAddr(*tuple(v.split(','))), data[1:]))

    @mcast.setter
    def mcast(self, value: McastAddr):
        '''Not yet implemented.
        '''
        self.modem.AT(f'+MCAST={value.index},{value.addr},{value.nwkskey},{value.appskey}')

    multicast = mcast

    def putx(self, port: int, data: bytes, timeout: float = None, hex = False):
        '''Send unconfirmed uplink message to the LoRaWAN network.

        The uplink will be transmitted up to `rep` times. The modem notifies the
        application of each retransmission by +EVENT=2,2. Any downlink message
        received by the modem from the network stops the retransmissions. Note
        that no +ACK or +NOACK will be generated even if an acknowledgement is
        received from the network. The value of `rep` overrides retransmission
        count configured by the network server through ADR.

        If `dformat` is set to 1, the application is expected to trasmit the
        payload encoded in a hexadecimal format. The parameter `hex` must be set
        to True in this case.

        The maximum size of the payload depends on the current LoRaWAN data
        rate. The size could be reduced further if the modem needs to piggyback
        any MAC commands onto the uplink. If the payload does not fit in the
        uplink, the modem responds with +ERR=-12. If any MAC commands waited to
        be piggybacked, the modem internally sends an empty uplink to "flush"
        the MAC commands even when it returns +ERR=-12 to the application. This
        is to ensure that the next uplink transmission will have as much space
        as possible dedicated to application payload.
        '''
        self.ptx(port, data, confirmed=False, timeout=timeout, hex=hex)

    def pctx(self, port: int, data: bytes, timeout: float = None, hex = False) -> bool:
        '''Send confirmed uplink message to the LoRaWAN network.

        The uplink will be transmitted up to `rtynum` times. The modem notifies
        the application of each retransmission by +EVENT=2,2. Any downlink
        message received by the modem from the network stops the retransmissions
        and the modem sends +ACK to the application. If no acknowledgement is
        received after `rtynum` transmissions, the modem generates +NOACK. The
        value of `rtynum` overrides retransmission count configured by the
        network server through ADR.

        If `dformat` is set to 1, the application is expected to trasmit the
        payload encoded in a hexadecimal format. The parameter `hex` must be set
        to True in this case.

        The maximum size of the payload depends on the current LoRaWAN data
        rate. The size could be reduced further if the modem needs to piggyback
        any MAC commands onto the uplink. If the payload does not fit in the
        uplink, the modem responds with +ERR=-12. If any MAC commands waited to
        be piggybacked, the modem internally sends an empty uplink to "flush"
        the MAC commands even when it returns +ERR=-12 to the application. This
        is to ensure that the next uplink transmission will have as much space
        as possible dedicated to application payload.
        '''
        rv = self.ptx(port, data, confirmed=True, timeout=timeout, hex=hex)
        assert rv is not None
        return rv

    def ptx(self, port: int, data: bytes, confirmed = False, timeout: float = None, hex = False) -> Optional[bool]:
        assert self.modem.port is not None
        type = 'C' if confirmed else 'U'
        with self.modem.lock:
            self.modem.AT(f'+P{type}TX {port},{len(data)}', wait=False, flush=False)
            if hex:
                self.modem.port.write(binascii.hexlify(data))
            else:
                self.modem.port.write(data)
            self.modem.port.flush()
            self.modem.read_inline_response()
            if confirmed:
                # The +ACK +NOACK events carry one boolean value (True for +ACK,
                # False for +NOACK)
                return self.modem.wait_for_event('ack', timeout=timeout)[0]
            else:
                return None

    @property
    def frmcnt(self):
        '''Return current uplink and downlink frame counters.

        The property value is a tuple (<uplink>,<downlink>). Each counter is an
        unsigned 32-bit integer. The downlink counter is selected based on the
        LoRaWAN protocol version used by the network server for the device. If
        your device uses LoRaWAN 1.0 then the downlink counter contains the
        value of FCntDown which increments for all downlinks in LoRaWAN 1.0. If
        your device uses LoRaWAN 1.1 then the downlink counter contains the
        value of AFCntDown which is only incremented for downlinks with a
        non-zero port number, i.e., downlink messages carrying application
        payload.
        '''
        return tuple(map(int, self.modem.AT('+FRMCNT?').split(',')))

    frame_counters = frmcnt

    @property
    def msize(self):
        '''Return the maximum length of payload for the current data rate.

        This property returns the maximum length of payload in bytes that can be
        sent in uplinks with the current data rate. The value will be generally
        between 11 and 242 bytes. Please note that the returned value does not
        take into account MAC commands scheduled to be piggy-backed onto the
        next uplink. Thus, if stack has MAC commands scheduled to be sent, you
        may still receive an error even if your payload fits into the length
        returned by this property. In that case, the stack will send an empty
        uplink (with only the MAC commands) internally. Thus, if you try to
        resend your payload, the second attempt should succeed.
        '''
        return int(self.modem.AT('+MSIZE?'))

    message_size = msize

    @property
    def rfq(self):
        '''Return the RSSI and SNR of the most recently received packet.

        This property returns a tuple of two integers: (<rssi>,<snr>). The
        <rssi> value is a measure of the power of the incoming RF signal
        expressed in the unit dBm, measured at the input of the LoRa receiver.
        The greater the value, the stronger the signal from the gateway. The
        <snr> value represents the ratio of the signal power to the channel
        noise power expressed in the unit dB. Note that since LoRa can receive
        packets below the noise floor, the SNR value can be negative.

        The returned values correspond to the most recently received packet.
        This can be any packet: application downlink, JoinAccept, ADR request,
        device status request, or confirmed uplink acknowledgement.
        '''
        return tuple(map(int, self.modem.AT('+RFQ?').split(',')))

    @property
    def dwell(self):
        '''Return whether the modem respects LoRaWAN dwell time.

        This property can be used to query dwell time enforcement in the AS923
        and AU915 regions. The returned value is a tuple of two booleans
        (<uplink>,<downlink>), where uplink and downlink can be either False
        (disabled) or True (enabled). Please note that the network server can
        also configure this parameter remotely via a MAC command sent to the
        device.

        The default value is region-specific.
        '''
        return tuple(map(lambda v: v == 1, map(int, self.modem.AT('+DWELL?').split(','))))

    @dwell.setter
    def dwell(self, value: Tuple[int | bool, int | bool]):
        '''Configure LoRaWAN dwell time enforcement in selected regions.

        This setter can be used to enable or disable the LoRaWAN dwell time
        enforcement in the AS923 and AU915 regions. The value is a tuple of two
        booleans (<uplink>,<downlink>), where uplink and downlink can be either
        False (disabled) or True (enabled). Please note that the network server
        can also configure this parameter remotely via a MAC command sent to the
        device.

        The default value is region-specific.
        '''
        def convert(value):
            if type(value) == bool:
                return 1 if value is True else 0
            else:
                return value

        if not isinstance(value, tuple) or len(value) != 2:
            raise Exception('Invalid value for setting dwell')

        v = tuple(map(convert, value))
        self.modem.AT(f'+DWELL={v[0]},{v[1]}')

    @property
    def maxeirp(self):
        '''Return the upper limit on RF output power.

        Please note that this parameter is only effective in regions other than
        US915. It is ignored in US915.

        This property can be used to inspect the upper limit on RF output power.
        This is the maximum RF output power that the LoRa transmitter will not
        be allowed to exceed. The value is expressed in the units of dBm. Please
        note that the value of this property can be remotely changed by the
        network server using a MAC command or ADR. The default value is region
        and channel plan specific.

        When calculating the RF output power for an uplink transmission, the
        modem subtracts the value configured through `rfpower` and antenna gain
        (2.15 dBi by default) from the MaxEIRP value configured through this
        property. In US915, these values are subtracted from the fixed value 30
        dBm and the resulting value is further limited according to the selected
        data rate and the number of active channels (channel mask). The
        resulting RF output power is then programmed into the LoRa transmitter.

        Note that the MaxEIRP can be larger than the actual maximum transmit
        power of the LoRa transmitter, i.e., there is no guarantee that the
        transmitter will be capable of transmitting with MaxEIRP.
        '''
        return int(self.modem.AT('+MAXEIRP?'))

    @maxeirp.setter
    def maxeirp(self, value: int):
        '''Set the upper limit on RF output power.

        Please note that this parameter is only effective in regions other than
        US915. It is ignored in US915.

        This setter can be used to set the upper limit on RF output power. This
        is the maximum RF output power that the LoRa transmitter will not be
        allowed to exceed. The value is expressed in the units of dBm. Please
        note that the value of this parameter can be remotely changed by the
        network server using a MAC command or ADR. The default value is region
        and channel plan specific.

        When calculating the RF output power for an uplink transmission, the
        modem subtracts the value configured through `rfpower` and antenna gain
        (2.15 dBi by default) from the MaxEIRP value configured through this
        parameter. In US915, these values are subtracted from the fixed value 30
        dBm and the resulting value is further limited according to the selected
        data rate and the number of active channels (channel mask). The
        resulting RF output power is then programmed into the LoRa transmitter.

        Note that the MaxEIRP can be larger than the actual maximum transmit
        power of the LoRa transmitter, i.e., there is no guarantee that the
        transmitter will be capable of transmitting with MaxEIRP.

        The setter updates both the current and default MaxEIRP values. The
        default MaxEIRP is the value that will be used when ADR is inactive or
        disabled.
        '''
        self.modem.AT(f'+MAXEIRP={value}')

    max_eirp = maxeirp

    @property
    def rssith(self):
        '''Not yet implemented.
        '''
        return int(self.modem.AT('+RSSITH?'))

    @rssith.setter
    def rssith(self, value: int):
        '''Not yet implemented.
        '''
        self.modem.AT(f'+RSSITH={value}')

    rssi_threshold = rssith

    @property
    def cst(self):
        '''Not yet implemented.
        '''
        return int(self.modem.AT('+CST?'))

    @cst.setter
    def cst(self, value: int):
        '''Not yet implemented.
        '''
        self.modem.AT(f'+CST={value}')

    carrier_sense_threshold = cst

    @property
    def backoff(self):
        '''Not yet implemented.
        '''
        return int(self.modem.AT('+BACKOFF?'))

    @property
    def chmask(self) -> str:
        '''Return LoRaWAN channel mask.

        The LoRaWAN channel mask determines the set of channels the modem is
        allowed to use to transmit uplinks. The channel mask is represented with
        a hexadecimal string where each bit corresponds to one channel. When the
        bit is set to 1, the corresponding channel is enabled. When the bit is
        set to 0, the corresponding channel is disabled. The length of the
        string is fixed and depends on the region (see the below table). The
        left-most byte in the string represents channels 0-8. The least
        significant bit within the byte represents channel 0. Thus, the string
        030000000000000000000000 enables channels 0 and 1 only. A valid channel
        mask must have at least two channels enabled.

        Please note that the channel mask can be updated by the network server
        in a JoinAns response.

        The length and default value of the channel mask depends on the active
        region.
        '''
        return self.modem.AT('+CHMASK?')

    @chmask.setter
    def chmask(self, value: str):
        '''Configure LoRaWAN channel mask.

        The LoRaWAN channel mask determines the set of channels the modem is
        allowed to use to transmit uplinks. The channel mask is represented with
        a hexadecimal string where each bit corresponds to one channel. When the
        bit is set to 1, the corresponding channel is enabled. When the bit is
        set to 0, the corresponding channel is disabled. The length of the
        string is fixed and depends on the region (see the below table). The
        left-most byte in the string represents channels 0-8. The least
        significant bit within the byte represents channel 0. Thus, the string
        030000000000000000000000 enables channels 0 and 1 only. A valid channel
        mask must have at least two channels enabled.

        Please note that the channel mask can be updated by the network server
        in a JoinAns response.

        The length and default value of the channel mask depends on the active
        region.
        '''
        self.modem.AT(f'+CHMASK={value.upper()}')

    channel_mask = chmask

    @property
    def rtynum(self):
        '''Return the number of confirmed uplink transmissions.

        Each confirmed uplink is transmitted up to `rtynum` number of times. The
        retransmissions stop as soon as the device receives a confirmation from
        the network server.

        Note: This parameter overrides the number of retransmissions remotely
        configured by the network server through adaptive data rate (ADR) when
        ADR is active.

        The default value upon factory reset is 8.
        '''
        return int(self.modem.AT('+RTYNUM?'))

    @rtynum.setter
    def rtynum(self, value: int):
        '''Set the number of confirmed uplink transmissions.

        Each confirmed uplink is transmitted up to `rtynum` number of times. The
        retransmissions stop as soon as the device receives a confirmation from
        the network server.

        Note: This parameter overrides the number of retransmissions remotely
        configured by the network server through adaptive data rate (ADR) when
        ADR is active.

        The configured value must be in the range <1, 15>. The default value
        upon factory reset is 8.
        '''
        self.modem.AT(f'+RTYNUM={value}')

    rty_num = rtynum

    @property
    def netid(self) -> str:
        '''Return LoRaWAN network ID (NetID).

        The NetID is a 24-bit integer. The value is encoded as a 32-bit integer
        in a hexadecimal format with the most significant byte (MSB) transmitted
        first.

        The default value upon factory reset is 00000000.

        This parameter is primarily used in LoRaWAN roaming. When a device
        wishes to Join its home network while it is roaming, i.e., the Join
        uplink will be picked up by a forwarding network, the device needs to
        specify the NetID in the Join request in order for the forwarding
        network to forward the message to the network server in the device's
        home network.

        Upon OTAA Join, the ID of the device's home network is encoded in
        DevAddr which is assigned to the device by its home network. Thus, the
        forwarding network knows where to forward the uplink based on the
        device's DevAddr and NetID is no longer used.
        '''
        return self.modem.AT('+NETID?')

    @netid.setter
    def netid(self, val: str):
        '''Configure LoRaWAN network ID (NetID).

        The NetID is a 24-bit integer. The value is encoded as a 32-bit integer
        in a hexadecimal format with the most significant byte (MSB) transmitted
        first.

        The default value upon factory reset is 00000000.

        This parameter is primarily used in LoRaWAN roaming. When a device
        wishes to Join its home network while it is roaming, i.e., the Join
        uplink will be picked up by a forwarding network, the device needs to
        specify the NetID in the Join request in order for the forwarding
        network to forward the message to the network server in the device's
        home network.

        Upon OTAA Join, the ID of the device's home network is encoded in
        DevAddr which is assigned to the device by its home network. Thus, the
        forwarding network knows where to forward the uplink based on the
        device's DevAddr and NetID is no longer used.
        '''
        self.modem.AT(f'+NETID={val.upper()}')

    net_id = netid


class OpenLoRaModem(MurataModem):
    @property
    def version(self):
        '''Return extended firmware version information.

        This property returns extended version information about the modem
        firmware in the form of a dictionary of attribute-value pairs:

        `firmware_version`. This attribute contains the version of the modem
        firmware. The version string has the following format:
        v1.0.2-10-g08e86e66 (modified). v1.0.2 represents the firmware release
        version. If the firmware is based on unreleased code from the git
        repository, a suffix like -10-g08e86e66 will be present. The suffix
        includes the number of commits since the most recent release and the git
        commit id of the most recent commit (following g). The optional
        (modified) suffix indicates that the git clone the firmware was built
        from contained local (uncommitted) modifications.

        `compatibility_version`. This attribute represents the Murata Modem
        firmware version emulated by the open firmware. The open firmware
        developed in this project aims to be AT-command compatible with Murata
        Modem firmware shipped with Type ABZ modules.

        `build_date`. This attribute represents the build date and time of the
        firmware.

        `loramac_version`. This attribute represents the version of the embedded
        LoRaMac-node library. The string has the same format as the modem
        version string. The number of commits and the git commit id of the most
        recent commit represent custom modifications applied to vanilla
        LoRaMac-node in our fork of the library.

        `lorawan11_version`. This attribute represents the most recent LoRaWAN
        1.1 (or newer) protocol version supported by the modem. This is the
        protocol version that will be used when the modem is joined to a LoRaWAN
        1.1 (or newer) network server.

        `lorawan10_version`. This property represents the most recent LoRaWAN
        1.0 protocol version supported by the modem. This is the protocol
        version that will be used when modem modem is joined to a network server
        that only supports LoRaWAN 1.0.

        `abp_lorawan_version`. Since the modem cannot negotiate a
        mutually-supported LoRaWAN protocol version with the network server, the
        protocol version to be used in ABP mode must be manually configured.
        This property represents the LoRaWAN protocol version the modem will use
        in ABP mode.

        `regional_parameters`. This property represents the version of the
        LoRaWAN Regional Parameters specification supported by the modem.

        `supported_regions`. This property lists all regions enabled in the
        modem firmware. Please note that this does not necessarily mean that all
        the listed regions are supported by the hardware (LoRa radio). If a
        region is listed here, its regional parameters are included in the modem
        firmware and the region can be activated. It does NOT mean that the LoRa
        radio can operate in the band.

        `build_type`. This property shows the build (compilation) mode of the
        firmware. It will be set to "debug" if the firmware has been compiled in
        debugging mode and to "release" if the firmware has been compiled in
        release mode. If the compilation mode cannot be determined, e.g., in
        custom builds, the string will be set to "?".
        '''
        ver = self.ver
        rv = { 'compatibility_version': ver[0], 'build_date': ver[1] }
        try:
            ver = self.modem.AT('$VER?').split(',')
            if len(ver) != 9:
                raise Exception('Unexpected response to AT$VER')
            rv['firmware_version'] = ver[0]
            rv['build_date'] = ver[1]
            rv['loramac_version'] = ver[2]
            rv['lorawan11_version'] = ver[3]
            rv['lorawan10_version'] = ver[4]
            rv['abp_lorawan_version'] = ver[5]
            rv['regional_parameters'] = ver[6]
            rv['supported_regions'] = ver[7]
            rv['build_type'] = ver[8]
        except UnknownCommand:
            pass
        return rv

    def reboot(self, hard=False):
        '''Restart the modem.

        This command can be used to restart the modem. By default (when invoked
        without any arguments), the command performs a clean restart, where the
        modem first waits for all active tasks to complete and then it schedules
        the restart.

        Pass hard=True to the method to perform a hard restart. In this case,
        the modem will restart immediately without waiting for anything.

        The method blocks until the modem has been restarted.
        '''
        with self.modem.lock:
            self.modem.AT(f'+REBOOT{" 1" if hard else ""}', wait=not hard)
            self.modem.wait_for_event('event=0,0')

    def facnew(self, reset_devnonce=False, reset_deveui=False):
        '''Reset the modem to factory defaults.

        Restore all modem settings to factory defaults. Upon completing factory
        reset, the modem automatically restarts. This method blocks until the
        restart has restarted.

        Since v1.1.0, factory reset does NOT reset the LoRaWAN DevNonce value by
        default. This ensures that the device can rejoin the network with an
        OTAA Join upon factory reset. If you wish to reset the DevNonce value to
        zero, pass reset_devnonce=True to the method.

        Since v1.1.1, factory reset does NOT reset the DevEUI value by default.
        To reset DevEUI, pass reset_deveui=True to the method.
        '''
        flags: int = 0
        if reset_devnonce: flags |= (1 << 0)
        if reset_deveui:   flags |= (1 << 1)

        with self.modem.lock:
            self.modem.AT(f'+FACNEW{f" {flags}" if flags else ""}')
            self.modem.wait_for_event('event=0,1')

    factory_reset = facnew

    @property
    def joineui(self) -> str:
        '''Return LoRaWAN JoinEUI.

        The Join EUI is a 64-bit globally unique identifier that identifiers the
        join server (or the application server). This value is meant to identify
        the server that shares the device's root application (AppKey) and
        network (NwkKey) keys. The JoinEUI is encoded in a hexadecimal format.

        The default value is 0101010101010101.
        '''
        return self.modem.AT('$JOINEUI?')

    @joineui.setter
    def joineui(self, value: str):
        '''Configure LoRaWAN JoinEUI.

        The JoinEUI is a 64-bit globally unique identifier that identifiers the
        join server (or the application server). This value is meant to identify
        the server that shares the device's root application (AppKey) and
        network (NwkKey) keys. The JoinEUI is encoded in a hexadecimal format.

        The default value is 0101010101010101.
        '''
        self.modem.AT(f'+JOINEUI={value.upper()}')

    join_eui = joineui

    @property
    def nwkskey_10(self):
        '''Return LoRaWAN 1.0 network session key (NwkSKey).

        Note: This is a LoRaWAN 1.0 compatibility property. It returns the value
        of FNwkSIntKey.
        '''
        return self.nwkskey

    @nwkskey_10.setter
    def nwkskey_10(self, value: str):
        '''Set LoRaWAN 1.0 network session key (NwkSKey).

        Note: This is a LoRaWAN 1.0 compatiblity property. The provided value
        will be set to FNwkSIntKey, SNwkSIntKey, and NwkSEncKey.
        '''
        self.nwkskey = value

    nwk_s_key_10 = nwkskey_10

    @property
    def appkey_10(self):
        return self.appkey

    @appkey_10.setter
    def appkey_10(self, value: str):
        '''Set LoRaWAN AppKey and NwkKey.

        Note: This is a LoRaWAN 1.0 compatiblity property. The provided value
        will be set to AppKey and NwkKey.
        '''
        self.appkey = value

    app_key_10 = appkey_10

    @property
    def appkey(self) -> str:
        '''Return LoRaWAN root application key (AppKey).

        The AppKey is a 128-bit symmetric key that is used to derive an
        application session key (AppSKey) during LoRaWAN OTAA Join.

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('$APPKEY?')

    @appkey.setter
    def appkey(self, value: str):
        '''Set LoRaWAN root application key (AppKey).

        The AppKey is a 128-bit symmetric key that is used to derive an
        application session key (AppSKey) during LoRaWAN OTAA Join.

        You can generate a new random AppKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'$APPKEY={value.upper()}')

    app_key = appkey # type: ignore

    def join(self, timeout = 120, retransmissions: int = None):
        '''Join LoRaWAN network in over-the-air-activation (OTAA) mode.

        If the modem is in OTAA mode (AT+MODE=1), this command sends a Join
        LoRaWAN request to the network (Join Server). The Join request will be
        sent with data rate 0. If the modem is in ABP mode (AT+MODE=0), this
        command does nothing.

        This is an asynchronous command. The modem responds with +OK or +ERR
        immediately. An ok response indicates that the Join request was
        successfully enqueued and could be sent. An error response indicates
        that the modem could not send a Join request at this time, e.g., due to
        duty cycling restrictions or some other operation in progress.

        Upon receiving a Join accept from the Join Server, the modem sends
        +EVENT=1,1. If the Join Server does not respond in time, the modem sends
        +EVENT=1,0 to the application to indicate that the Join request timed
        out.

        Since lora-modem-abz v1.1.0, Join requests are retransmitted up to eight
        times. Thus, a single AT+JOIN can generate up to nine Join transmissions
        in total. This also means that it will take longer to receive an
        +EVENT=1,{0,1}, up to a minute if no Join answer is received from the
        network. The default number of Join transmissions is set to nine to make
        sure that the Join is transmitted at least once in each eight-channel
        subband in regions that use all 64 channels such as US915, plus one
        extra transmission in the 500 kHz channel subband.

        Please note that the number of retransmissions configured via AT+REP and
        AT+RTYNUM do not apply to OTAA Joins. The application can control the
        maximum number of Join retransmissions via an optional parameter to
        AT+JOIN. To disable retransmissions use AT+JOIN 0. To only retransmit
        once use AT+JOIN 1, etc. The maximum number of retransmissions that can
        be configured via the optional parameter is 15.

        The modem applies a small randomly generated delay before each
        retransmission in accordance with Section 7 of LoRaWAN Specification
        v1.1. The random number generator is seeded from the LoRa radio which
        generates a random seed on each device. The random delay is between 100
        ms and 500 ms.

        Note: Join requests are subject to additional duty cycling restrictions
        even in regions that otherwise do not use duty cycling. See the
        documentation for AT+JOINDC for more details.
        '''
        with self.modem.lock:
            self.modem.AT(f'+JOIN{f" {retransmissions}" if retransmissions is not None else ""}')
            status = self.modem.wait_for_event('event', timeout=timeout)
            if status == (1, 1):
                return
            elif status == (1, 0):
                raise JoinFailed('OTAA Join failed')
            else:
                raise Exception('Unsupported event received')

    @property
    def rfpower(self):
        return tuple(map(int, self.modem.AT('$RFPOWER?').split(',')))

    @rfpower.setter
    def rfpower(self, value: Tuple[int, ...]):
        if isinstance(value, tuple):
            if len(value) == 4:
                self.modem.AT(f'$RFPOWER={value[0]},{value[1]},{value[2]},{value[3]}')
                return
            elif len(value) == 2:
                # super().rfpower = value
                super(self.__class__, self.__class__).rfpower.fset(self, value) # type: ignore
                return
        raise Exception('Invalid rf_power value')

    rf_power = rfpower # type: ignore

    @property
    def dr(self):
        enum = region_to_data_rate(self.region)

        def create(value):
            value = int(value)
            try:
                value = enum(value)
            except ValueError:
                pass
            return value

        return tuple(map(create, self.modem.AT('$DR?').split(',')))

    @dr.setter
    def dr(self, value: (str | tuple | LoRaDataRate | int) | Tuple[str | LoRaDataRate | int, str | LoRaDataRate | int]):
        if type(value) == str:
            value = tuple(map(lambda s: s.strip(), value.split(',')))

        if not isinstance(value, tuple):
            value = (value,)

        if len(value) == 2:
            self.modem.AT(f'+DR={self.parse_data_rate(value[0])},{self.parse_data_rate(value[1])}')
        elif len(value) == 1:
            # super().dr = value
            super(self.__class__, self.__class__).dr.fset(self, value[0]) # type: ignore
        else:
            raise Exception('Unsupported data rate value')

    data_rate = dr # type: ignore

    @property
    def rx2(self):
        value = tuple(map(int, self.modem.AT('$RX2?').split(',')))

        enum = region_to_data_rate(self.region)
        try:
            dr1 = enum(value[1])
        except ValueError:
            pass

        try:
            dr2 = enum(value[3])
        except ValueError:
            pass

        return (value[0], dr1, value[2], dr2)

    @rx2.setter
    def rx2(self, value: str | Tuple[str, ...] | Tuple[int, str | LoRaDataRate | int] | Tuple[int, str | LoRaDataRate | int, int, str | LoRaDataRate | int]):
        if type(value) == str:
            value = tuple(map(lambda s: s.strip(), value.split(',')))

        if len(value) == 2:
            # super().rx2 = value
            super(self.__class__, self.__class__).rx2.fset(self, value) # type: ignore
        elif len(value) == 4:
            self.modem.AT(f'$RX2={int(value[0])},{self.parse_data_rate(value[1])},{int(value[2])},{self.parse_data_rate(value[3])}') # type: ignore
        else:
            raise Exception('Invalid rx2 setting value')

    @property
    def chmask(self):
        return tuple(self.modem.AT('$CHMASK?').split(','))

    @chmask.setter
    def chmask(self, value: str | Tuple[str, ...]):
        if type(value) == str:
            value = tuple(map(lambda s: s.strip(), value.split(',')))

        if len(value) == 1:
            # super().chmask = value
            super(self.__class__, self.__class__).chmask.fset(self, value[0].upper()) # type: ignore
        elif len(value) == 2:
            self.modem.AT(f'$CHMASK={value[0].upper()},{value[1].upper()}')
        else:
            raise Exception('Invalid channel mask value')

    channel_mask = chmask # type: ignore

    @property
    def loglevel(self):
        '''Return current log level

        Levels: 0 - disabled, 1 - error, 2 - warning, 3 - debug, 4 - all
        '''
        value = int(self.modem.AT('$LOGLEVEL?'))
        try:
            value = LogLevel(value)
        except ValueError:
            pass
        return value

    @loglevel.setter
    def loglevel(self, value: str | LogLevel | int):
        '''Set log level

        Levels: 0 - disabled, 1 - error, 2 - warning, 3 - debug, 4 - all
        '''
        if type(value) == str:
            value = LogLevel[value.upper()]

        if isinstance(value, LogLevel):
            value = value.value

        self.modem.AT(f'$LOGLEVEL={value}')

    log_level = loglevel

    def halt(self):
        '''Halt the modem.

        Upon receiving this command, the modem will stop processing AT commands
        and incoming LoRa messages and will enter a low-power mode. The modem
        must be rebooted via the external reset pin or power-cycled to resume
        operation. The modem sends +EVENT=0,3 to the application prior to
        halting.

        This method blocks until the modem has been halted.
        '''
        with self.modem.lock:
            self.modem.AT('$HALT')
            self.modem.wait_for_event('event=0,3')

    @property
    def dbg(self):
        '''Show debugging information.

        This is an internal property meant primarily to aid debugging. The value
        shows whether or not the MCU is allowed to enter the low-power Sleep or
        Stop mode, as well as the state of the LoRa transceiver.

        Note: The corresponding AT command used by this property is only
        available in debug builds of the firmware.
        '''
        return self.modem.AT('$DBG', inline=False)

    debug = dbg

    @property
    def cert(self):
        '''Return whether LoRaWAN certification port is enabled.

        Port 224 is reserved in the LoRaWAN specification for device
        certification, i.e., compliance with the LoRaWAN specification. This
        property can be used to query support for port 224 in the modem.

        The certification port is disabled by default.
        '''
        return int(self.modem.AT('$CERT?')) == 1

    @cert.setter
    def cert(self, value: bool | int):
        '''Enable or disable LoRaWAN certification port.

        Port 224 is reserved in the LoRaWAN specification for device
        certification, i.e., compliance with the LoRaWAN specification. This
        setter can be used to enable or disable support for port 224 in the
        modem.

        The certification port is disabled by default.
        '''
        if type(value) == bool:
            value = 1 if value is True else 0

        self.modem.AT(f'$CERT={value}')

    certification_port = cert

    @property
    def channels(self):
        '''Return a list of available channels and their parameters.

        This property provides additional information about each channel
        currently configured in the modem. The output is provided line-by-line,
        with one channel per line, and is terminated with +OK. The format of
        each line is as follows: $CHANNELS: <enabled>,<frequency>,<RX1
        frequency>,<min data rate>,<max data rate>,<band>, where:

          * <enabled> is 1 if the channel is enabled and 0 if it is disabled;
          * <frequency> represents the center frequency of the channel in Hz;
          * <RX1 frequency> will be non-zero if an alternative RX1 frequency has
            been defined for the channel;
          * <min data rate> represents the minimum data rate allowed on the
            channel;
          * <max data rate> represents the maximum data rate allowed on the
            channel;
          * <band> represents the logical band within the region the channel
            belongs to.

        Please note that the output may not include all channels. The channels
        that are not enabled upon factory reset will have their center frequency
        initially set to zero and such channels will be omitted from the output.
        They will show up once re-enabled.

        AT$CHANNELS?
        $CHANNELS: 1,868100000,0,0,5,1
        $CHANNELS: 1,868300000,0,0,5,1
        $CHANNELS: 1,868500000,0,0,5,1
        +OK
        '''
        v = self.modem.AT('$CHANNELS?', inline=False)
        v = re.sub(r'^\$CHANNELS: ', '', v, flags=re.MULTILINE)

        def make_channel(enabled, freq, rx1_freq, min_dr, max_dr, subband):
            return Channel(enabled == '1', int(freq), None if rx1_freq == '0' else int(rx1_freq), int(min_dr), int(max_dr), int(subband))

        v = map(lambda line: make_channel(*line.split(',')), v.split('\n'))
        return list(v)

    @property
    def nwkkey(self) -> str:
        '''Return LoRaWAN 1.1 root network key (NwkKey).

        The NwkKey is a 128-bit symmetric key that is used to derive a network
        session key (NwkSKey) during LoRaWAN OTAA Join.

        This property is designed for LoRaWAN 1.1 activation. It does not touch
        the AppKey. If you wish to configure both NwkKey and AppKey keys
        simultaenously for LoRaWAN 1.0 activation, use AT+APPKEY instead.

        You can generate a new random NwkKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('$NWKKEY?')

    @nwkkey.setter
    def nwkkey(self, value: str):
        '''Set LoRaWAN 1.1 root network key (NwkKey).

        The NwkKey is a 128-bit symmetric key that is used to derive a network
        session key (NwkSKey) during LoRaWAN OTAA Join.

        This property is designed for LoRaWAN 1.1 activation. It does not touch
        the AppKey. If you wish to configure both NwkKey and AppKey keys
        simultaenously for LoRaWAN 1.0 activation, use AT+APPKEY instead.

        You can generate a new random NwkKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'$NWKKEY={value.upper()}')

    nwk_key = nwkkey

    @property
    def fnwksintkey(self) -> str:
        '''Return LoRaWAN 1.1 forwarding network session integrity key (FNwkSIntKey).

        In LoRaWAN 1.1, the key used to check the integrity of LoRaWAN messages
        was split into two: FNwkSIntKey and SNwkSIntKey. This arrangement
        enables LoRaWAN network roaming where the device communicates with its
        "serving" LoRaWAN network by the way of a "forwarding" LoRaWAN network.
        The forwarding network then uses the FNwkSIntKey to verify the integrity
        (MIC) of uplink messages before it forwards the message to the serving
        network. The serving network then uses the SNwkSIncKey to verify the
        integrity of the network and to compute the MIC of downlink messages
        sent to the device.

        The FNwkSIntKey is a 128-bit symmetric key. In LoRaWAN 1.1 OTAA
        activation, the FNwkSIntKey is automatically derived upon a successful
        Join. In ABP activation the key must be manually configured into the
        device and the network.

        You can generate a new random FNwkSIntKey with OpenSSL as follows:
            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('$FNWKSINTKEY?')

    @fnwksintkey.setter
    def fnwksintkey(self, value: str):
        '''Set LoRaWAN 1.1 forwarding network session integrity key (FNwkSIntKey).

        In LoRaWAN 1.1, the key used to check the integrity of LoRaWAN messages
        was split into two: FNwkSIntKey and SNwkSIntKey. This arrangement
        enables LoRaWAN network roaming where the device communicates with its
        "serving" LoRaWAN network by the way of a "forwarding" LoRaWAN network.
        The forwarding network then uses the FNwkSIntKey to verify the integrity
        (MIC) of uplink messages before it forwards the message to the serving
        network. The serving network then uses the SNwkSIncKey to verify the
        integrity of the network and to compute the MIC of downlink messages
        sent to the device.

        The FNwkSIntKey is a 128-bit symmetric key. In LoRaWAN 1.1 OTAA
        activation, the FNwkSIntKey is automatically derived upon a successful
        Join. In ABP activation the key must be manually configured into the
        device and the network.

        You can generate a new random FNwkSIntKey with OpenSSL as follows:
            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'$FNWKSINTKEY={value.upper()}')

    f_nwk_s_int_key = fnwksintkey

    @property
    def snwksintkey(self) -> str:
        '''Return LoRaWAN 1.1 serving network session integrity key (SNwkSIntKey).

        In LoRaWAN 1.1, the key used to check the integrity of LoRaWAN messages
        was split into two: FNwkSIntKey and SNwkSIntKey. This arrangement
        enables LoRaWAN network roaming where the device communicates with its
        "serving" LoRaWAN network by the way of a "forwarding" LoRaWAN network.
        The forwarding network then uses the FNwkSIntKey to verify the integrity
        (MIC) of uplink messages before it forwards the message to the serving
        network. The serving network then uses the SNwkSIncKey to verify the
        integrity of the network and to compute the MIC of downlink messages
        sent to the device.

        The SNwkSIntKey is a 128-bit symmetric key. In LoRaWAN 1.1 OTAA
        activation, the SNwkSIntKey is automatically derived upon a successful
        Join. In ABP activation the key must be manually configured into the
        device and the network.

        You can generate a new random SNwkSIntKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('$SNWKSINTKEY?')

    @snwksintkey.setter
    def snwksintkey(self, value: str):
        '''Set LoRaWAN 1.1 serving network session integrity key (SNwkSIntKey).

        In LoRaWAN 1.1, the key used to check the integrity of LoRaWAN messages
        was split into two: FNwkSIntKey and SNwkSIntKey. This arrangement
        enables LoRaWAN network roaming where the device communicates with its
        "serving" LoRaWAN network by the way of a "forwarding" LoRaWAN network.
        The forwarding network then uses the FNwkSIntKey to verify the integrity
        (MIC) of uplink messages before it forwards the message to the serving
        network. The serving network then uses the SNwkSIncKey to verify the
        integrity of the network and to compute the MIC of downlink messages
        sent to the device.

        The SNwkSIntKey is a 128-bit symmetric key. In LoRaWAN 1.1 OTAA
        activation, the SNwkSIntKey is automatically derived upon a successful
        Join. In ABP activation the key must be manually configured into the
        device and the network.

        You can generate a new random SNwkSIntKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'$SNWKSINTKEY={value.upper()}')

    s_nwk_s_int_key = snwksintkey

    @property
    def nwksenckey(self) -> str:
        '''Return LoRaWAN 1.1 network session encryption key (NwkSEncKey).

        In LoRaWAN 1.1, the network session encryption key (NwkSEncKey) is used
        to encrypt and decrypt MAC commands sent in FOpts or FRMPayload fields
        of a LoRaWAN message, i.e., in LoRaWAN messages with port 0 that contain
        no application payload.

        The NwkSEncKey is a 128-bit symmetric key. In LoRaWAN 1.1 OTAA
        activation, the NwkSEncKey is automatically derived upon a successful
        Join. In ABP activation the key must be manually configured into the
        device and the network.

        You can generate a new random NwkSEncKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            return self.modem.AT('$NWKSENCKEY?')

    @nwksenckey.setter
    def nwksenckey(self, value: str):
        '''Return LoRaWAN 1.1 network session encryption key (NwkSEncKey).

        In LoRaWAN 1.1, the network session encryption key (NwkSEncKey) is used
        to encrypt and decrypt MAC commands sent in FOpts or FRMPayload fields
        of a LoRaWAN message, i.e., in LoRaWAN messages with port 0 that contain
        no application payload.

        The NwkSEncKey is a 128-bit symmetric key. In LoRaWAN 1.1 OTAA
        activation, the NwkSEncKey is automatically derived upon a successful
        Join. In ABP activation the key must be manually configured into the
        device and the network.

        You can generate a new random NwkSEncKey with OpenSSL as follows:

            openssl enc -aes-128-cbc -k secret -P -md sha1 2>/dev/null | \
                grep ^key= | cut -d = -f 2

        The key is encoded in a hexadecimal format. The default value after
        factory reset is 2B7E151628AED2A6ABF7158809CF4F3C.
        '''
        with self.modem.secret:
            self.modem.AT(f'$NWKSENCKEY={value.upper()}')

    nwk_s_enc_key = nwksenckey

    @property
    def session(self):
        '''Return information about the active LoRaWAN network session.

        This property returns the parameters of the currently active LoRaWAN
        network association, including the network type, activation mode,
        LoRaWAN protocol version, network ID, and device ID.

        The property `network_type` represents the network type and is either
        "public" or "private". The property `activation_mode` represents the
        LoRaWAN activation mode and can be one of "None", "OTAA", "ABP", "?".
        The property `lorawan_version` represents the LoRaWAN protocol version
        being used. The property `net_id` is the ID of the network. The property
        `dev_addr` is the address of the device (DevAddr).
        '''
        rv = {}
        data = self.modem.AT('$SESSION?').split(',')

        if len(data) < 2:
            raise Exception('Unexpected response to AT$SESSION')

        rv['network_type'] = data[0]
        rv['activation_mode'] = data[1]

        if len(data) > 2:
            if len(data) != 5:
                raise Exception('Unexpected response to AT$SESSION')

            rv['lorawan_version'] = data[2]
            rv['net_id'] = data[3]
            rv['dev_addr'] = data[4]
        return rv


def uartconfig_to_str(uart):
    if uart.parity == 0:
        parity = 'N'
    elif uart.parity == 1:
        parity = 'E'
    elif uart.parity == 2:
        parity = 'O'
    else:
        parity = f'{uart.parity}'

    return f'{uart.baudrate} {uart.data_bits}{parity}{uart.stop_bits}'


def render(data):
    if machine_readable:
        click.echo('\n'.join(map(lambda v: ';'.join(map(str, v)), data)))
    else:
        click.echo(tabulate(data, tablefmt="psql"))


def random_key():
    return secrets.token_hex(16).upper()


@click.group(invoke_without_command=True)
@click.option('--port', '-p', help='Pathname to the serial port', show_default=True)
@click.option('--baudrate', '-b', type=int, default=None, help='Serial port baud rate [default: detect]')
@click.option('--reset', '-r', default=False, is_flag=True, help='Reset the modem before issuing any AT commands.')
@click.option('--verbose', '-v', default=False, is_flag=True, help='Show all AT communication.')
@click.option('--guard', '-g', type=int, default=None, help='AT command guard interval [s]')
@click.option('--machine', '-m', default=False, is_flag=True, help='Produce machine-readable output.')
@click.option('--show-keys', '-k', 'with_keys', default=False, is_flag=True, help='Show security keys.')
@click.pass_context
def cli(ctx, port, baudrate, reset, verbose, guard, machine, with_keys):
    '''Command line interface to the Murata TypeABZ LoRaWAN modem.

    This tool provides a number of commands for managing Murata TypeABZ
    LoRaWAN modems. Most of the available commands require the open firmware
    and cannot be used with the original Murata Modem firmware. The
    following command groups are available (invoke each command with --help
    to obtain more information):

    \b
    * Obtain modem information: device, network, state
    * Configure the modem: get, set, reset, reboot
    * Manage security keys: keys, keygen
    * Perform network operations: join, link, trx

    Configure the modem's serial port filename with the command line option
    -p or via the environment variable PORT. The tool tries to auto-detect
    the modem's baud rate unless you explicitly configure a baud rate with
    -b. If you wish to reset the modem before use, add the command line
    option -r.

    The tool produces human-readable output by default. You can switch to
    machine-readable output with the command line option -m. The command
    line option -v activates a debugging mode which additionally shows all
    AT commands sent to the modem and all responses received from the modem.

    Use the command line option -k to also include LoRaWAN security keys in
    the output of the commands device and network. They keys are ommited
    from the output by default for security reasons.
    '''
    global machine_readable, show_keys
    machine_readable = machine
    show_keys = with_keys

    @lru_cache(maxsize=None)
    def get_modem():
        nonlocal port, baudrate
        port = port or os.environ.get('PORT', None)
        if port is None:
            click.echo('Error: Please specify a serial port', err=True)
            sys.exit(1)

        device = TypeABZ(port, verbose=verbose, guard=guard)
        if baudrate is None:
            baudrate = device.detect_baud_rate()
            if baudrate is None:
                click.echo('Error: Could not detect serial port speed', err=True)
                sys.exit(1)

        device.open(baudrate)

        modem = OpenLoRaModem(device)

        if reset:
            modem.reset()

        ctx.call_on_close(device.close)
        return modem

    try:
        ctx.obj = get_modem

        if ctx.invoked_subcommand is None:
            ctx.invoke(device)
            ctx.invoke(network)
            ctx.invoke(state)
    except KeyboardInterrupt:
        pass


@cli.command()
@click.pass_obj
def device(get_modem: Callable[[], OpenLoRaModem]):
    '''Show basic modem information.

    This command obtains the basic information from the LoRa modem such as
    device model, firmware version, or supported LoRaWAN protocol versions.
    It displays the collected information in a table that looks as follows:

    \b
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

    The device's root LoRaWAN security keys are omitted from the output by
    default for security reasons. Pass the command line option -k
    (--show-keys) to the parent command to include the root security keys
    (AppKey, NwkKey) in the above table.
    '''
    modem = get_modem()

    if not machine_readable:
        click.echo(f"Device information for modem {modem}:")

    ver = modem.version
    data = [
        ['Port configuration',  uartconfig_to_str(modem.UART) ],
        ['Device model',        modem.model ],
        ['Firmware version',    f'{ver["firmware_version"]} [LoRaMac-node {ver["loramac_version"]}]'],
        ['Data encoding',       modem.data_encoding],
        ['LoRaWAN version',     f'{ver["lorawan11_version"]} / {ver["lorawan10_version"]} ({ver["abp_lorawan_version"]} for ABP)'],
        ['Regional parameters', ver["regional_parameters"]],
        ['Supported regions',   ver["supported_regions"]],
        ['Device EUI',          modem.DevEUI]]

    if show_keys:
        data.append(['AppKey', modem.AppKey])
        data.append(['NwkKey', modem.NwkKey])

    render(data)


@cli.command()
@click.pass_obj
def network(get_modem: Callable[[], OpenLoRaModem]):
    '''Show current network activation parameters.

    This command displays information about the modem's current network
    activation. The modem can be in one of three activation state:

    \b
    1. Not activated on any LoRaWAN network (None)
    2. Activated by provisioning (ABP)
    3. Activated over the air (OTAA)

    Upon factory reset, the modem is in ABP mode with the default set of
    LoRaWAN security keys. This does not mean, however, that the modem can
    communicate on any LoRaWAN network. In most cases, the modem will need
    to be Joined to a LoRaWAN network, or it may need to be provisioned with
    a different set of ABP keys, matching the keys provisioned for the modem
    in the LoRaWAN network.

    The information about the current network activation is summarized in a
    table that is shown below. The amount of information included in the
    table depends on the current activation state.

    The row "Protocol version" shows the negotiated protocol version in OTAA
    mode and a manually configured protocol version in the ABP mode. The ABP
    mode does not support LoRaWAN protocol version negotiation.

    The row "Device address" shows the device address assigned to the modem
    by the network server in OTAA mode. If the modem operates in ABP mode,
    this row shows the device address manually configured by the user.

    \b
    Network activation information for modem /dev/serial0:
    +------------------+------------------+
    | Network type     | public           |
    | Activation       | OTAA             |
    | Network ID       | 00000013         |
    | Join EUI         | 0101010101010101 |
    | Protocol version | LoRaWAN 1.1.1    |
    | Device address   | 260C4493         |
    +------------------+------------------+

    The device's session LoRaWAN security keys are omitted from the output
    by default for security reasons. Pass the command line option -k
    (--show-keys) to the parent command to include the session keys in the
    above table.
    '''
    modem = get_modem()

    if not machine_readable:
        click.echo(f"Network activation information for modem {modem}:")

    sess = modem.session
    data = [
        ['Network type', sess['network_type']],
        ['Activation',   sess['activation_mode']]]

    if sess['activation_mode'] != 'None':
        data.append(['Network ID',   sess['net_id']])

    if sess['activation_mode'] != 'ABP':
        data.append(['Join EUI', modem.JoinEUI])

    if sess['activation_mode'] != 'None':
        data.append(['Protocol version', f"LoRaWAN {sess['lorawan_version']}"])
        data.append(['Device address', sess['dev_addr']])

    if show_keys:
        data.append(['AppSKey', modem.AppSKey])
        if 'lorawan_version' not in sess or sess['lorawan_version'].startswith('1.1'):
            data.append(['FNwkSIntKey', modem.FNwkSIntKey])
            data.append(['SNwkSIntKey', modem.SNwkSIntKey])
            data.append(['NwkSEncKey',  modem.NwkSEncKey])
        else:
            data.append(['NwkSKey', modem.NwkSKey_10])

    render(data)


@cli.command()
@click.pass_obj
def state(get_modem: Callable[[], OpenLoRaModem]):
    '''Show the current modem state.

    This command can be used to obtain a summary of the current state of the
    modem. The summary includes information about the modem's region,
    LoRaWAN class, channel mask, data rate, uplink and downlink counters,
    receive windows, and many others.

    With adaptive data rate (ADR) enabled, some of the parameters can be
    remotely configured by the LoRaWAN network server. In that case, the
    summary shows the value configured for the parameter by the network
    server via ADR. With ADR disabled or inactive, these parameters are
    typically configured to values provided by the regional parameters for
    the current region.

    Many of these parameters can also be configured by the user via the
    command "set".

    \b
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
    | Uplink frame counter      | 1                                                         |
    | Downlink frame counter    | 0                                                         |
    | Last downlink RSSI        | -107 dBm                                                  |
    | Last downlink SNR         | -5 dB                                                     |
    | RX1 window                | Delay: 5000 ms                                            |
    | RX2 window                | Delay: 6000 ms, Frequency: 923.3 MHz, Data rate: SF12_500 |
    | Join response windows     | RX1: 5000 ms, RX2: 6000 ms                                |
    +---------------------------+-----------------------------------------------------------+
    '''
    modem = get_modem()

    if not machine_readable:
        click.echo(f"Current state of modem {modem}:")

    region = modem.band
    rssi, snr = modem.rfq
    uplink, downlink = modem.frmcnt
    delay = modem.delay
    rx2 = modem.rx2

    data = [
        ['Current region',            region.name],
        ['LoRaWAN class',             modem.CLASS.name],
        ['Channel mask',              modem.chmask[0]],
        ['Data rate',                 modem.dr[0]],
        ['Maximum message size',      f'{modem.message_size} B'],
        ['RF power index',            modem.rfpower[0]],
        ['ADR enabled',               modem.adr],
        ['Duty cycling enabled',      modem.duty_cycle],
        ['Join duty cycling enabled', modem.join_duty_cycle],
        ['Maximum EIRP',              f'{modem.max_eirp} dBm'],
        ['Uplink frame counter',      uplink],
        ['Downlink frame counter',    downlink],
        ['Last downlink RSSI',        f'{rssi} dBm'],
        ['Last downlink SNR',         f'{snr} dB'],
        ['RX1 window',                f'Delay: {delay.rx_window_1} ms'],
        ['RX2 window',                f'Delay: {delay.rx_window_2} ms, Frequency: {rx2[0] / 1000000} MHz, Data rate: {rx2[1]}'],
        ['Join response windows',     f'RX1: {delay.join_accept_1} ms, RX2: {delay.join_accept_2} ms']]
    render(data)


@cli.command()
@click.pass_obj
def link(get_modem: Callable[[], OpenLoRaModem]):
    '''Check the radio link.

    This command sends a link check request to the LoRaWAN network server.
    The network server sends a response back to the modem that includes the
    number of gateways that heard the request and the margin of the uplink
    radio link in dB. This information is then shown in a table together
    with the RSSI and SNR parameters of the response (downlink):

    \b
    Checking the radio link for modem /dev/serial0...done.
    +--------------------+----------+
    | Gateway count      | 1        |
    | Uplink margin      | 17 dB    |
    | Last downlink RSSI | -107 dBm |
    | Last downlink SNR  | -5 dB    |
    +--------------------+----------+

    The margin value represent the strength of the device's signal relative
    to the demodulation floor at the gateway. A value of 0 indicates that
    the gateway can barely receive uplinks from the device. A value of 12
    indicates that the uplink was received 12 dB above the demodulation
    floor. The range of the margin value is 0 to 254.

    The gateway count value represents the number of gateways that
    successfully received the link check request. If multiple gateways
    received the request, this margin value represents the margin of the
    gateway selected by the network server for downlinks to the device,
    usually the gateway with the best margin.

    The RSSI value is a measure of the power of the incoming RF signal from
    the gateway expressed in the unit dBm, measured at the input of the LoRa
    receiver. The greater the value, the stronger the signal from the
    gateway.

    The SNR value represents the ratio of the gateway's signal power to the
    channel noise power expressed in the unit dB. Note that since LoRa can
    receive packets below the noise floor, the SNR value can be negative.

    Note: Link check requests are not retransmitted. The modem will send
    only one link check request for each invocation of the command.
    '''
    modem = get_modem()

    if not machine_readable:
        click.echo(f"Checking the radio link for modem {modem}...", nl=False)

    reply = modem.link_check()

    if not machine_readable:
        click.echo('done.' if reply is not None else 'no response.')

    if reply is not None:
        rssi, snr = modem.rfq
        data = [
            ['Gateway count', reply[1]],
            ['Uplink margin', f'{reply[0]} dB'],
            ['Last downlink RSSI', f'{rssi} dBm'],
            ['Last downlink SNR',  f'{snr} dB']]
        render(data)
    else:
        sys.exit(1)


@cli.command()
@click.option('--encoding', '-e', default='bin', type=click.Choice(['bin', 'base64', 'hex']), help='Select an encoding for message data.', show_default=True)
@click.option('--delimiter', '-d', type=str, default=',', help='Select the delimiter character.', show_default=True)
@click.option('--message', '-m', type=str, help='Send the given message and exit.')
@click.pass_obj
def trx(get_modem: Callable[[], OpenLoRaModem], encoding, delimiter: str, message: str | None):
    '''Transmit and receive LoRaWAN messages.

    This command can be used to send LoRaWAN uplink messages and receive
    downlink messages. The tool receives messages to be transmitted on the
    standard input and transmits received downlinks over the standard output.

    To transmit a single uplink without waiting for downlinks, pass the message
    to the command with the option -m (--message). The message format passed to
    -m is the same as the format described below (without the LF character). If
    the uplink is confirmed, the command waits for the confirmation.

    Each message is transmitted over the standard input/output on a separate
    line. Lines are separated with the line feed (LF) character (ASCII 10). Each
    line consists of fields separated with the delimiter character ',':

    [<flags>],[<port>],<message><LF>

    The delimiter character is configurable with -d (--delimiter). When both
    fields <flags> and <port> are present, their order is interchangeable.

    The field <flags> is optional on standard input (uplink) and is always
    ommitted on standard output (downlink). When set to '!', the uplink message
    will be transmitted as confirmed. When omitted or set to '?', the uplink
    message will be transmitted as unconfirmed.

    The field <port> denotes the LoRaWAN port of the message. The value must be
    an integer in the range <1, 222>. If omitted, the default port number
    configured in the modem will be used (2 by default). The <port> field is
    always present in downlinks (lines printed to standard output).

    For example, to send a confirmed message "ping" to port 64, the application
    would write to standard input:

    !,64,ping

    If the network sends a "pong" response to port 64, the following line will
    be written by the tool to standard output:

    64,pong

    The tool supports three encoding formats for messages, selectable with the
    command line option -e (--encoding): binary (the default), base64, and
    hexadecimal. The selected encoding applies to both uplink and downlink
    messages.

    In the default binary encoding (shown in the above examples), certain
    characters must be escaped if they are presented in the message. Line feed
    (LF) characters must be escaped with '\\n'. The delimiter character, if
    present in the message, must be escaped with '\\xhh', where <hh> is the
    delimiter's ASCII value in a hexadecimal format. Thus, the default delimiter
    ',' is escaped as '\\x2c'. The sequences '\\n' and '\\x' in the message must
    be escaped as '\\\\n' and '\\\\x', respectively. If the backslash character
    is followed by another character, it does not need to be escaped.

    No message escaping is necessary with the base64 and hexadecimal message
    encodings. If you reconfigure the delimiter character, make sure to select a
    character outside of the base64 and hexadecimal alphabets.
    '''
    modem = get_modem()

    hex = modem.data_encoding == 1
    delim = delimiter.encode('utf-8')
    if len(delim) != 1:
        raise Exception('Delimiter must be a single-byte character')

    def escape(data: bytes):
        state = 0 # 0: start, 1: escape
        delim = ord(delimiter)
        rv = bytearray()
        for c in data:
            if state == 0:
                if c == ord('\n'):
                    rv.extend(b'\\n')
                elif c == delim:
                    rv.extend(b'\\x%02x' % c)
                elif c == ord('\\'):
                    state = 1
                else:
                    rv.append(c)
            elif state == 1:
                if c == ord('n') or c == ord('x'):
                    rv.extend(b'\\\\n')
                else:
                    rv.extend(b'\\')
                state = 0
            else:
                raise Exception('Bug: Invalid state')
        return rv

    def unescape(data: bytes):
        hc = 0
        state = 0 # 0: start, 1: escape, 2: hex1, 3: hex2
        rv = bytearray()
        for c in data:
            if state == 0:
                if c == ord('\\'):
                    state = 1
                else:
                    rv.append(c)
            elif state == 1:
                if c == ord('n'):
                    rv.extend(b'\n')
                    state = 0
                elif c == ord('x'):
                    state = 2
                elif c == ord('\\'):
                    rv.extend(b'\\')
                    state = 0
                else:
                    rv.extend(b'\\')
                    rv.append(c)
                    state = 0
            elif state == 2:
                hc = c
                state = 3
            elif state == 3:
                rv.extend(binascii.unhexlify(chr(hc) + chr(c)))
                state = 0
            else:
                raise Exception('Bug: Invalid state')
        if state != 0:
            raise Exception('Unterminated escape sequence')
        return rv

    def decode(data: bytes):
        if encoding == 'bin':
            return unescape(data)
        elif encoding == 'base64':
            return b64decode(data)
        elif encoding == 'hex':
            return binascii.unhexlify(data)
        else:
            raise Exception(f'Unsupported encoding {encoding}')

    def encode(data: bytes):
        if encoding == 'bin':
            return escape(data)
        elif encoding == 'hex':
            return binascii.hexlify(data)
        elif encoding == 'base64':
            return b64encode(data)
        else:
            raise Exception(f'Unsupported encoding {encoding}')

    def on_message(port: int, data: bytes):
        sys.stdout.buffer.write(f'{port}{delimiter}'.encode('utf-8'))
        sys.stdout.buffer.write(encode(data))
        sys.stdout.buffer.write(b'\n')
        sys.stdout.buffer.flush()

    modem.on('message', on_message)

    def send(message: bytes):
        comps = message.split(delim)
        if len(comps) == 1:
            data = comps[0]
            modem.utx(decode(data), hex=hex)
        elif len(comps) == 2:
            confirm_or_port, data = comps
            if confirm_or_port == b'!' or confirm_or_port == b'?':
                modem.tx(decode(data), confirmed=confirm_or_port == b'!', hex=hex)
            else:
                modem.putx(int(confirm_or_port), decode(data), hex=hex)
        elif len(comps) == 3:
            a, b, data = comps
            if a == b'!' or a == b'?':
                confirm, port = a, b
            else:
                confirm, port = b, a
            modem.ptx(int(port), decode(data), confirmed=confirm == b'!', hex=hex)
        else:
            raise Exception('Invalid input format')

    if message is not None:
        send(message.encode('utf-8'))
    else:
        for line in sys.stdin.buffer:
            send(line.rstrip(b'\n'))


@cli.command()
@click.option('--hard', '-r', default=False, is_flag=True, help='Perform hard reboot.')
@click.pass_obj
def reboot(get_modem: Callable[[], OpenLoRaModem], hard):
    '''Restart the modem.

    This command restarts (reboots) the modem. By default, the command performs
    a graceful restart, waiting for any active tasks within the modem to finish.

    If you wish to restart the modem immediately, use the option -h (--hard).
    The modem will restart as soon as it receives the command without waiting
    for anything. Use this variant sparingly as some of the data normally
    persisted into the modem's non-volatile memory (NVM) may be lost.
    '''
    modem = get_modem()

    if not machine_readable:
        click.echo(f"Rebooting modem {modem}...", nl=False)
    modem.reboot(hard=hard)
    if not machine_readable:
        click.echo(f"done")


@cli.command()
@click.option('--region', '-r', type=str, default=None, help='Switch to the region (band) if necessary.')
@click.option('--public/--private', '-p/-P', 'network', default=None, help='Select public or private network.')
@click.option('--join-eui', '-j', type=str, default=None, help='Set Join server EUI (JoinEUI).')
@click.option('--channel-mask', '-c', type=str, default=None, help='Update the channel mask before Join.')
@click.option('--duty-cycle/--no-duty-cycle', '-D/-d', default=None, help='Enable or disable Join duty cycling.')
@click.option('--retransmissions', '-R', type=int, default=None, help='Limit the number of Join retransmissions.')
@click.option('--rx1', type=int, default=None, help='Configure the JoinAccept RX1 window.')
@click.option('--rx2', type=int, default=None, help='Configure the JoinAccept RX2 window.')
@click.option('--timeout', '-t', type=int, default=None, help='Limit total Join retransmission time [s].')
@click.pass_obj
def join(get_modem: Callable[[], OpenLoRaModem], region, network, join_eui, duty_cycle, channel_mask, rx1, rx2, retransmissions, timeout):
    '''Perform a LoRaWAN OTAA Join.

    Switch the modem to over-the-air (OTAA) activation mode if necessary and
    perform an OTAA Join. This command will keep running until either a Join
    response has been received from the Join server, or until the operation
    times out which may take up to two minutes.

    If the modem has been reset to factory defaults, you may need to switch the
    modem to the correct region first. You can use the command line option -r
    (--region) to accomplish that. The option takes the case-insensitive region
    name as value, e.g., us915 for the US band. The default region upon factory
    reset is eu868.

    The options -p (--public) and -P (--private) control the preamble
    synchronization word, i.e., whether the modem is joining a public or private
    network. In most cases, if you are joining a LoRaWAN network provided by
    somebody else, you would probably want to use -p (the default). If the
    network is part of your infrastructure and you know that it operates in a
    private mode, use -P. LoRaWAN networks such as The Things Network operate in
    the public mode.

    In regions that utilize all 64 channels such as us915 you may want to
    consider restricting the set of channels on which the Join request will be
    transmitted via the -c (--channel-mask) command line option. For example,
    most The Things Network gateways in the us915 region listen on at most eight
    channels simultaneously (sub-band 2 or channels 9-16), although the band
    consists of 64 channels in total. Restricting Join transmissions to the
    corresponding sub-band improves the success rate and makes the join command
    finish faster.

    Several other OTAA Join related parameters such as the JoinEUI, RX1/2 window
    delays, and join duty cycling are configurable via separate command line
    options.
    '''
    modem = get_modem()

    if region is not None:
        try:
            region = LoRaRegion[region.upper()]
        except KeyError:
            try:
                region = int(region)
            except ValueError:
                click.echo('Error: Invalid region', err=True)
                sys.exit(1)
        modem.band = region

    if network is not None:
        modem.nwk = 1 if network else 0

    if join_eui is not None:
        modem.JoinEUI = join_eui

    if channel_mask is not None:
        modem.chmask = channel_mask

    if rx1 is not None or rx2 is not None:
        delay = modem.delay
        modem.delay = Delay(
            rx1 if rx1 is not None else delay.join_accept_1,
            rx2 if rx2 is not None else delay.join_accept_2,
            delay.rx_window_1, delay.rx_window_2)

    if duty_cycle is not None:
        modem.joindc = duty_cycle

    modem.mode = LoRaMode.OTAA

    kwargs = {}
    if timeout is not None:
        kwargs['timeout'] = timeout

    if retransmissions is not None:
        kwargs['retransmissions'] = retransmissions

    if not machine_readable:
        click.echo(f"Joining LoRaWAN network via modem {modem}...", nl=False)

    try:
        modem.join(**kwargs)
    except JoinFailed:
        if not machine_readable:
            click.echo(f"failed.")
            sys.exit(1)
    else:
        if not machine_readable:
            click.echo(f"succeeded.")


@cli.command()
@click.option('--yes', '-y', default=False, is_flag=True, prompt='Reset the modem to factory defaults?', help='Do not ask for confirmation.')
@click.option('--reset-devnonce', '-n', default=False, is_flag=True, help='Also reset DevNonce.')
@click.option('--reset-deveui', '-e', default=False, is_flag=True, help='Also reset DevEUI.')
@click.pass_obj
def reset(get_modem: Callable[[], OpenLoRaModem], yes, reset_devnonce, reset_deveui):
    '''Reset the modem to factory defaults.

    This command restores the values of all modem settings to their original
    values with one exception: the DevNonce value will be preserved (see below).

    To prevent accidental resets, the command asks the user for explicit
    confirmation. You can skip the confirmation with the command line option -y
    (--yes).

    The factory reset operation preserves the LoRaWAN DevNonce value by default.
    DevNonce is a counter used to secure OTAA Join requests. For security
    reasons, DevNonce values must never by reused by the same device (modem).
    The LoRaWAN network server remembers the highest DevNonce value used by each
    device. Join requests with a lower DevNonce value will be discarded. To make
    sure OTAA Joins continue working across factory resets, the modem remembers
    the most recently used value across factory resets.

    If you also wish to reset the DevNonce value, pass the command line option
    -n (--reset-devnonce) to this command. If you use -n, you may also need to
    delete and re-add the device in the LoRaWAN network. Without this, OTAA Join
    requests sent by the device after factory reset will most likely fail.

    The factory reset operation also preserves the DevEUI by default. If you
    wish to also reset the DevEUI to the default value generated from the
    modem's MCU unique identifier, pass the command line option -e
    (--reset-deveui) to this command.
    '''
    if not yes:
        if not machine_readable:
            click.echo('Aborting.')
            return

    modem = get_modem()

    if not machine_readable:
        click.echo(f"Resetting modem {modem} to factory defaults...", nl=False)

    modem.factory_reset(reset_devnonce=reset_devnonce, reset_deveui=reset_deveui)

    if not machine_readable:
        click.echo(f"done.")


@cli.command()
@click.argument('names', nargs=-1)
@click.option('--all', '-a', default=False, is_flag=True, help="Get the names and values of all supported settings (implies -l).")
@click.option('--names-only', '-n', default=False, is_flag=True, help="Get the names of all settings (implies -a).")
@click.option('--long', '-l', default=False, is_flag=True, help="Also output the setting's name (automatically enabled with -a).")
@click.pass_obj
def get(get_modem: Callable[[], OpenLoRaModem], names, all, long, names_only):
    '''Retrieve modem setting(s).

    This command can be used to retrieve the value of one or more modem
    settings. To get the values of specific settings, simply provide their names
    as additional arguments on the command line. Setting names are case-insensitive.

    \b
    $ lora get ver adr
    ['1.1.06', 'Jun 20 2022 00:25:27']
    True

    The command prints the current value for each setting on a separate line.
    The command line option -l (--long) will also display the name of each
    setting in the output:

    \b
    $ lora get -l ver adr
    ver=['1.1.06', 'Jun 20 2022 00:25:27']
    adr=True

    To display the names of all setting names understood by the command line
    tool, run the get command with -n (--names-only). This will produce a long
    list of setting names. Not all displayed settings may be supported by your
    modem firmware.

    \b
    $ lora get -n
    adaptive_data_rate
    adr_ack
    app_eui
    app_key
    ...

    To display the names and current values of all modem's settings, use -a
    (--all). With this option, the command will retrieve the current values of
    all settings from the modem. This may take a while.

    \b
    $ lora get -a
    adaptive_data_rate=True
    app_eui=0101010101010101
    app_key=2597CE51C66970D58342FF12E6A4B60B
    app_key_10=2597CE51C66970D58342FF12E6A4B60B
    app_s_key=5F566536C3B0E314BD3AD558315135B1
    certification_port=False
    channel_mask=0700,0700

    Note: Many settings are available under longer (more descriptive) alias
    names. For example, the setting name adaptive_data_rate is an alias for the
    name adr. Typically, the short variant corresponds to the name of the
    corresponding AT command sans the AT+ or AT$ prefix.
    '''
    modem = get_modem()

    if names_only:
        all = True

    if all:
        if len(names):
            click.echo("Explicit setting names and --all are mutually exclusive", err=True)
            sys.exit(1)

        # Automatically enable the long mode if we are listing all settings,
        # otherwise the application would not be able to match setting names
        # to values.
        long=True

        props: "dict[int, str]" = {}
        for n, v in modem.settings().items():
            if len(n) >= len(props.get(id(v), n)):
                props[id(v)] = n

        names = sorted(props.values())
        if names_only:
            for name in names: click.echo(name)
            return
    else:
        if len(names) == 0:
            click.echo("Please either provide a setting name or use --all", err=True)
            sys.exit(1)

    for name in names:
        orig_name = name
        n = name.lower()
        if n.startswith('at+') or n.startswith('at$'):
            name = name[3:]
        try:
            value = getattr(modem, name)
        except AttributeError as e:
            click.echo(f'Error while getting "{orig_name}": {e}', err=True)
            sys.exit(1)
        except UnknownCommand:
            if not all:
                click.echo(f'Error: The modem does not implement "{orig_name}"', err=True)
                sys.exit(1)
        else:
            if isinstance(value, tuple or list):
                value = ','.join(map(str, value))
            if long:
                click.echo(f'{orig_name}={value}')
            else:
                click.echo(f'{value}')


@cli.command()
@click.argument('arguments', nargs=-1)
@click.pass_obj
def set(get_modem: Callable[[], OpenLoRaModem], arguments):
    '''Update modem setting(s).

    This command can be used to update the value of one or more modem settings.
    If wish to update only one setting, you can specify the name of the setting
    and the new value on the command line as follows:

        lora set adr True

    To support values that contain whitespace, the command automatically folds
    the second and all remaining arguments into the value, for example, in the
    following command line:

        lora set rx2 869525000, SF12_125, 869525000, SF12_125

    the value will become "869525000, SF12_125, 869525000, SF12_125".

    If you wish to update multiple settings, leave the command line empty and
    provide the setting names and new values on standard input, one setting at a
    line:

        cat << "END" | lora set
        port 2
        adr True
        END

    The command also supports an alternative syntax where the setting name and
    value are delimited with the '=' character:

        lora set rx2=869525000,SF12_125,869525000,SF12_125

    or via the standard input:

        cat << "END" | lora set
        port=2
        adr=True
        END

    The alternative syntax is primarily designed to make it possible to paste
    the settings copied from the get command into the set command.

    Note: Not all modem settings are writable. Some are read-only and cannot be
    updated.
    '''
    def process_setting(setting):
        n = setting.find('=')
        if n == -1:
            n = setting.find(' ')
            if n == -1:
                click.echo(f'Missing value for setting {setting}', err=True)
                sys.exit(1)
        name = setting[:n].strip()
        value = setting[n + 1:].strip()

        n = name.lower()
        if n.startswith('at+') or n.startswith('at$'):
            name = name[3:]

        try:
            # First, try to evaluate the given value as a Python symbol. This
            # will make it possible for the user to directly specify values such
            # as LoRaRegion.US915.
            value = eval(value)
        except (NameError, SyntaxError):
            try:
                # If the above evaluation failed, evaluate the value as a
                # string. This allows the user to use strings directly on the
                # command line without the need to use backslashed quotes.
                value = eval(f'"{value}"')
            except Exception as e:
                click.echo('Error: Invalid setting value: {e}', err=True)
                sys.exit(1)

        try:
            setattr(modem, name, value)
        except AttributeError as e:
            click.echo(f'Error while setting "{name}": {e}', err=True)
            sys.exit(1)

    modem = get_modem()

    if len(arguments):
        process_setting(' '.join(arguments))
    else:
        for line in sys.stdin:
            process_setting(line.rstrip('\n'))



@cli.command()
@click.option('--protocol', '-p', default='1.1', type=click.Choice(['1.0', '1.1']), help='LoRaWAN protocol version', show_default=True)
@click.pass_obj
def keys(get_modem: Callable[[], OpenLoRaModem], protocol):
    '''Show current LoRaWAN security keys.

    This command displays the various security keys currently configured in the
    modem. Since the LoRaWAN protocol changed its security architecture in
    LoRaWAN 1.1, this command supports two variants: one for LoRaWAN 1.1.x and
    another for LoRaWAN 1.0.x.

    By default, the keys command displays the security keys for LoRaWAN 1.1.x.
    The output looks as follows:

    \b
    ├─ AppKey=2B7E151628AED2A6ABF7158809CF4F3C
    │  └─ AppSKey=2B7E151628AED2A6ABF7158809CF4F3C
    └─ NwkKey=2B7E151628AED2A6ABF7158809CF4F3C
        ├─ FNwkSIntKey=2B7E151628AED2A6ABF7158809CF4F3C
        ├─ SNwkSIntKey=2B7E151628AED2A6ABF7158809CF4F3C
        └─ NwkSEncKey=2B7E151628AED2A6ABF7158809CF4F3C

    AppKey and NwkKey are so-called root security keys. AppKey secures the
    traffic between the modem and the LoRaWAN application server. NwkKey secures
    the traffic between the modem and LoRaWAN network servers. Upon factory
    reset, both keys are set to the default value shown above. The default value
    is insecure and new random keys must be generated for each modem before it
    is used for the first time (see the command keygen).

    AppSKey is the application session key. This key protects the traffic
    between the modem and the application server. In OTAA mode, this key is
    automatically derived from AppKey during OTAA Join. In ABP mode, a randomly
    generated key must be manually configured by the application or user.

    The keys FNwkSIntKey and SNwkSIntKey ensure the integrity of network traffic
    between the modem and network servers. In LoRaWAN 1.1, each message is
    signed with both keys so that two independent LoRaWAN networks, each having
    access to only one key, could independently verify the integrity of the
    message. This scheme is designed to enable LoRaWAN network roaming.

    The key NwkSEncKey encrypts the network traffic between the modem and its
    "serving" (home) LoRaWAN network. This key is only used to encrypt network
    management messages. Messages with application payload are encrypted with
    AppSKey instead.

    In OTAA mode, all three keys are automatically derived from NwkKey. In ABP
    mode, the keys must be randomly generated and manually provisioned by the
    application.

    If your network uses LoRaWAN 1.1, pass -p 1.0 to the keys command to display
    LoRaWAN 1.0.x security keys:

    \b
    └─ AppKey=2B7E151628AED2A6ABF7158809CF4F3C
        ├─ AppSKey=2B7E151628AED2A6ABF7158809CF4F3C
        └─ NwkSKey=2B7E151628AED2A6ABF7158809CF4F3C

    The above output shows the default keys automatically provisioned into the
    modem upon factory reset. Here, AppKey is the single root security key. In
    OTAA mode, AppSKey and NwkSKey are automatically derived from the root key
    during OTAA Join. In ABP mode, the two session keys must be randomly
    generated and provisioned by the application.

    NwkSKey protects (encrypts and authenticates) all network management traffic
    between the modem and the LoRaWAN network server. AppSKey protects
    application-layer traffic between the modem and the application server.
    '''
    modem = get_modem()

    if protocol == '1.1':
        text = dedent(f'''
            ├─ AppKey={modem.appkey}
            │  └─ AppSKey={modem.appskey}
            └─ NwkKey={modem.nwkkey}
                ├─ FNwkSIntKey={modem.fnwksintkey}
                ├─ SNwkSIntKey={modem.snwksintkey}
                └─ NwkSEncKey={modem.nwksenckey}''').strip()
    elif protocol == '1.0':
        text = dedent(f'''
            └─ AppKey={modem.appkey_10}
                ├─ AppSKey={modem.appskey}
                └─ NwkSKey={modem.nwkskey_10}''').strip()
    else:
        click.echo('Error: Unsupported LoRaWAN protocol version', err=True)
        sys.exit(1)

    if machine_readable:
        text = re.sub(r'[ \t─│├└]', '', text, flags=re.UNICODE)
    click.echo(text)


@cli.command()
@click.option('--protocol', '-p', default='1.1', type=click.Choice(['1.0', '1.1']), help='LoRaWAN protocol version', show_default=True)
@click.option('--old', '-o', default=False, is_flag=True, help='Also show old (previous) keys.')
@click.option('--silent', '-s', default=False, is_flag=True, help='Do not show the newly generated keys.')
@click.pass_obj
def keygen(get_modem: Callable[[], OpenLoRaModem], protocol, silent, old):
    '''Generate new random LoRaWAN security keys.

    This command generates new random security keys and provisions the keys into
    the modem. This command generates all keys, including the various session
    keys to make the modem readily usable in APB mode. In OTAA mode, the session
    keys generated by this command will be overwritten as soon as the device
    performs a successful OTAA Join.

    The newly generated keys will be printed to the terminal as follows:

    \b
    New security keys for modem /dev/serial0:
    +-------------+----------------------------------+
    | AppKey      | F5DC35403146D5A3898C06BC75F7ECF5 |
    | NwkKey      | 28191E8CAAF3EF8B0897C473A7B30843 |
    | AppSKey     | 60109A0B7498F78E0589A2E0C42D00CC |
    | FNwkSIntKey | 63704A18155D5F8240534BEB22A1EE62 |
    | SNwkSIntKey | 9050E7F1B6C2AF5232E516384C1E9696 |
    | NwkSEncKey  | 9FE445220EF3A761ACC1C2323194739B |
    +-------------+----------------------------------+

    The command generates LoRaWAN 1.1 security keys by default. Use the command
    line option -p 1.0 to generate LoRaWAN 1.0 security keys instead:

    \b
    New security keys for modem /dev/serial0:
    +---------+----------------------------------+
    | AppKey  | 065485391CD83A7373262DDB40F7A684 |
    | AppSKey | 87E4D92BA32D17A80DCB4328EEE0052F |
    | NwkSKey | 432916D1AFE59C0A57677566C17ED07D |
    +---------+----------------------------------+

    The command line option -s (--silent) will keep the newly generated keys
    hidden from the terminal. You can retrieve the keys from the modem later
    using the command "keys".

    If you also wish to show the old security keys before they are ovewritten,
    use the option -o (--old).

    Note: This command uses the Python package secrets to generate secure random
    keys.
    '''
    modem = get_modem()

    if protocol == '1.1':
        if old:
            old_keys = [
                ['AppKey',      modem.appkey],
                ['NwkKey',      modem.nwkkey],
                ['AppSKey',     modem.appskey],
                ['FNwkSIntKey', modem.fnwksintkey],
                ['SNwkSIntKey', modem.snwksintkey],
                ['NwkSEncKey',  modem.nwksenckey]]

        modem.appkey = random_key()
        modem.nwkkey = random_key()
        modem.appskey = random_key()
        modem.fnwksintkey = random_key()
        modem.snwksintkey = random_key()
        modem.nwksenckey = random_key()
        data = [
            ['AppKey',      modem.appkey],
            ['NwkKey',      modem.nwkkey],
            ['AppSKey',     modem.appskey],
            ['FNwkSIntKey', modem.fnwksintkey],
            ['SNwkSIntKey', modem.snwksintkey],
            ['NwkSEncKey',  modem.nwksenckey]]
    elif protocol == '1.0':
        if old:
            old_keys = [
                ['AppKey',  modem.appkey_10],
                ['AppSKey', modem.appskey],
                ['NwkSKey', modem.nwkskey]]

        modem.appkey_10 = random_key()
        modem.appskey = random_key()
        modem.nwkskey_10 = random_key()
        data = [
            ['AppKey',  modem.appkey_10],
            ['AppSKey', modem.appskey],
            ['NwkSKey', modem.nwkskey]]
    else:
        click.echo('Error: Unsupported LoRaWAN protocol version', err=True)
        sys.exit(1)

    if old:
        click.echo(f'Previous security keys for modem {modem}:')
        render(old_keys)

    if not silent:
        click.echo(f'New security keys for modem {modem}:')
        render(data)


if __name__ == '__main__':
    cli()
