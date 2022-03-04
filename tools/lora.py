#!/usr/bin/env python3
import sys
import serial
import binascii
from collections import namedtuple
from datetime import datetime, timedelta
from enum import Enum, unique
from threading import Thread, RLock
from queue import Queue, Empty
from time import sleep
from pymitter import EventEmitter
import argparse
import base64

# Event description
#
# EVENT=0   : Module status
# EVENT=0,0 : Modem rebooted
# EVENT=0,1 : Factory reset
# EVENT=0,2 : Switched to bootloader
# EVENT=0,3 : Modem halted
#
# EVENT=1   : JOIN result
# EVENT=1,0 : JOIN failed
# EVENT=1,1 : JOIN succesful
#
# EVENT=2   : Network status
# EVENT=2,0 : No answer from server
# EVENT=2,1 : Received answer from server
# EVENT=2,2 : Retransmission


UARTConfig = namedtuple('UARTConfig', 'baudrate data_bits stop_bits parity flow_control')
RFConfig   = namedtuple('RFConfig',   'channel frequency dr_min dr_max')
Delay      = namedtuple('Delay',      'join_accept_1 join_accept_2 rx_window_1 rx_window_2')
McastAddr  = namedtuple('MCastAddr',  'index addr nwkskey appskey')


@unique
class LoRaBand(Enum):
    AS923        = 0
    AU915        = 1
    EU868        = 5
    KR920        = 6
    IN865        = 7
    US915        = 8
    US915_HYBRID = 9


@unique
class LoRaMode(Enum):
    ABP  = 0
    OTAA = 1


@unique
class LoRaNetwork(Enum):
    PRIVATE = 0
    PUBLIC  = 1


@unique
class LoRaDataRateAS923(Enum):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7

@unique
class LoRaDataRateAU915(Enum):
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
class LoRaDataRateEU868(Enum):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7

@unique
class LoRaDataRateEU868(Enum):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    SF7_250  = 6
    FSK_50   = 7

@unique
class LoRaDataRateKR920(Enum):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5

@unique
class LoRaDataRateIN860(Enum):
    SF12_125 = 0
    SF11_125 = 1
    SF10_125 = 2
    SF9_125  = 3
    SF8_125  = 4
    SF7_125  = 5
    FSK_50   = 7

@unique
class LoRaDataRateUS915(Enum):
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
class LoRaClass(Enum):
    CLASS_A = 0
    CLASS_B = 1
    CLASS_C = 2


error_messages = {
     -1 : 'Unknown command',
     -2 : 'Invalid number of parameters',
     -3 : 'Invalid parameter value(s)',
     -4 : 'Factory reset failed',
     -5 : 'Device has not joined LoRaWAN yet',
     -6 : 'Device has already joined LoRaWAN',
     -7 : 'Resource unavailable: LoRa MAC is transmitting',
     -8 : 'New firmware version must be different',
     -9 : 'Missing firmware information',
    -10 : 'Flash read/write error',
    -11 : 'Firmware update failed',
    -12 : 'Payload is too long',
    -13 : 'Only supported in ABP activation mode',
    -14 : 'Only supported in OTAA activation mode',
    -15 : 'RF band is not supported',
    -16 : 'Power value too high',
    -17 : 'Not supported in the current band',
    -18 : 'Cannot transmit due to duty cycling',
    -19 : 'Channel unavailable due to LBT or error',
    -20 : 'Too many link check requests'
}


class LoRaModule(EventEmitter):
    def __init__(self, filename, speed=19200, debug=False, on_message=None, guard=0):
        super().__init__(self)
        self.filename = filename
        self.speed = speed
        self.debug = debug
        self.guard = guard
        self.prev_at = None


    def open(self, timeout=5):
        self.ser = serial.Serial(self.filename, self.speed)
        self.ser.flushInput()
        self.ser.flushOutput()
        self.response = Queue()

        self.lock = RLock()
        self.thread = Thread(target=self.reader)
        self.thread.daemon = True
        self.thread.start()


    def close(self):
        self.ser.close()


    def read_line(self):
        line = ''
        while True:
            c = self.ser.read(1).decode()
            if len(c) == 0:
                raise Exception('No data')
            if c == '\r':
                continue
            if c == '\n':
                if len(line) == 0:
                    continue
                return line
            line += c


    def reader(self):
        try:
            while True:
                data = self.read_line()
                if self.debug:
                    print('> %s' % data)
                try:
                    if data.startswith('+EVENT'):
                        payload = data[7:]
                        if len(payload) == 0:
                            self.emit('event', [])
                        else:
                            params = tuple(map(int, payload.split(',')))
                            if len(params) != 2:
                                raise Exception('Unsupported event parameters')

                            # For each event received from the LoRa module, we
                            # generate three events. The event "event" allows
                            # the application to subscribe to all event, event
                            # "event=x" allows the application to subscribe to
                            # all events from a specific subsystem, and
                            # event=x,y allows the application to subscribe to
                            # one specific event.
                            self.emit('event', params)
                            self.emit('event=%d' % params[0], [])
                            self.emit('event=%d,%d' % params, [])
                    elif data.startswith('+ANS'):
                        self.emit('answer', tuple(map(int, data[5:].split(','))))
                    elif data.startswith('+ACK'):
                        self.emit('ack', True)
                    elif data.startswith('+NOACK'):
                        self.emit('ack', False)
                    elif data.startswith('+RECV'):
                        port, size = tuple(map(int, data[6:].split(',')))
                        data = self.ser.read(size + 2) # We use +2 here to skip an empty line sent by the modem
                        self.emit('message', port, data[2:])
                    elif data.startswith('+OK') or data.startswith('+ERR'):
                        self.response.put_nowait(data)
                    else:
                        if self.debug:
                            print('Unsupported message received: %s' % data)
                except Exception as error:
                    print('Ignoring reader thread error: %s' % error)
        finally:
            if self.debug:
                print('Terminating reader thread')


    def at(self, cmd, flush=True):
        if cmd is not None:
            cmd = '+%s' % cmd
        else:
            cmd = ''

        if self.debug:
            print('< AT%s' % cmd)

        self.ser.write(('AT%s\r' % cmd).encode())
        if flush:
            self.ser.flush()


    def wait_for_reply(self, timeout=None):
        try:
            reply = self.response.get(timeout=timeout)
        except Empty:
            raise TimeoutError('No reply received')
        try:
            if reply.startswith('+ERR'):
                errno = int(reply[5:])
                try:
                    errstr = error_messages[errno]
                except KeyError:
                    errstr = 'Unknown error'
                raise Exception('Command failed: %s (%d)' % (errstr, errno))
            elif reply.startswith('+OK'):
                if len(reply) > 3:
                    return reply[4:]
            else:
                raise Exception('Invalid response')

        finally:
            self.response.task_done()


    def invoke(self, cmd, timeout=10):
        # Implement rudimentary throttling of AT commands send to the device. It
        # appears the modem cannot properly interpret AT commands that come
        # quickly after a previous response. Thus, we wait at least 100
        # milliseconds from the previous response before sending the next AT
        # command.
        if self.guard > 0:
            now = datetime.now()
            if self.prev_at is not None:
                if now - self.prev_at < timedelta(milliseconds=self.guard):
                    sleep(self.guard / 1000)

        with self.lock:
            self.at(cmd)
            rv = self.wait_for_reply(timeout=timeout)
            self.prev_at = datetime.now()
            return rv


    def wait_for_event(self, event, timeout=None):
        q = Queue()
        cb = lambda data: q.put_nowait(data)
        self.once(event, cb)
        try:
            data = q.get(timeout=timeout)
            q.task_done()
            return data
        except Empty:
            self.off(event, cb)
            raise TimeoutError('Timed out')


    @property
    def uart(self):
        'Query AT modem UART configuration'
        reply = self.invoke('UART?').split(',')
        if len(reply) != 5:
            raise Exception('Unexpected reply to AT command UART')
        return UARTConfig(int(reply[0]), int(reply[1]), int(reply[2]), int(reply[3]), reply[4] == 1)

    @uart.setter
    def uart(self, baudrate):
        '''Set AT modem UART configuration

        Acceptable baudrates: 4800, 9600, 19200, 38400. The default value is
        9600.
        '''
        return self.invoke('UART=%d' % baudrate)

    @property
    def ver(self):
        'Return firmware version and build time'
        return self.invoke('VER?').split(',')

    @property
    def dbg(self):
        '''Return current debug level

        Levels: 0 - disabled, 1 - error, 2 - warning, 3 - debug, 4 - all
        '''
        return int(self.invoke('DBG?'))

    @dbg.setter
    def dbg(self, level):
        '''Set debug level

        Levels: 0 - disabled, 1 - error, 2 - warning, 3 - debug, 4 - all
        '''
        return self.invoke('UART=%d' % level)

    @property
    def dev(self):
        'Return hardware model'
        return self.invoke('DEV?')

    def reboot(self):
        '''Reboot the device

        Upon successful reboot the device will send an event to the host.
        '''
        with self.lock:
            self.invoke('REBOOT')
            self.wait_for_event('event=0,0')

    def factory_reset(self):
        '''Reset the device to factory defaults

        This command resets the LoRa modem to factory defaults. The only
        parameter that will not be reset is DevEUI. Upon factory reset, the
        device sends an event to the host and then automatically reboots.
        '''
        with self.lock:
            self.invoke('FACNEW')
            self.wait_for_event('event=0,1')

    @property
    def band(self):
        'Return the currently configured radio frequency band'
        return LoRaBand(int(self.invoke('BAND?')))

    @band.setter
    def band(self, val):
        '''Configure radio frequency band

        When the band is changed, the modem automatically deactivates and will
        need to be rebooted. The default band is US915.
        '''
        return self.invoke('BAND=%d' % val.value)

    @property
    def class_(self):
        'Return currently configured LoRa class mode'
        return LoRaClass(int(self.invoke('CLASS?')))

    @class_.setter
    def class_(self, val):
        '''Configure LoRa class mode

        Upon LoRa class mode change, the modem may need to be JOINed again to
        the network. The default is class A.
        '''
        return self.invoke('CLASS=%d' % val.value)

    @property
    def mode(self):
        'Return current LoRa activation mode (OTAA or ABP)'
        return LoRaMode(int(self.invoke('MODE?')))

    @mode.setter
    def mode(self, val):
        '''Configure LoRa activation mode (OTAA or ABP)

        The default is ABP.
        '''
        return self.invoke('MODE=%d' % val.value)

    @property
    def devaddr(self):
        'Return current LoRa device address'
        return self.invoke('DEVADDR?')

    @devaddr.setter
    def devaddr(self, val):
        '''Configure LoRa device address

        In the ABP activation mode, the address needs to be configured by the
        user (application). In OTAA activation mode, the address will be
        assigned by the LoRa network server upon JOIN.
        '''
        return self.invoke('DEVADDR=%s' % val)

    @property
    def deveui(self):
        'Return the LoRa device EUI'
        return self.invoke('DEVEUI?')

    @deveui.setter
    def deveui(self, val):
        '''Configure the LoRa device EUI

        Note that the value configured through this command will survive factory
        reset.
        '''
        return self.invoke('DEVEUI=%s' % val)

    @property
    def appeui(self):
        'Return currently configured LoRa AppID'
        return self.invoke('APPEUI?')

    @appeui.setter
    def appeui(self, val):
        'Configure LoRa AppID'
        return self.invoke('APPEUI=%s' % val)

    @property
    def nwkskey(self):
        'Return currently configured LoRa network session key'
        return self.invoke('NWKSKEY?')

    @nwkskey.setter
    def nwkskey(self, val):
        '''Configure LoRa network session key

        In the OTAA activation mode, the key will be automatically generated by
        the LoRa network server. The configured value must be exactly 16 bytes.
        '''
        return self.invoke('NWKSKEY=%s' % val)

    @property
    def appskey(self):
        'Return currently configured LoRa application session key'
        return self.invoke('APPSKEY?')

    @appskey.setter
    def appskey(self, val):
        '''Configure LoRa application session key

        In the OTAA activation mode, the application session key will be
        automatically generated by the LoRa network server. The configured value
        must be exactly 16 bytes.
        '''
        return self.invoke('APPSKEY=%s' % val)

    @property
    def appkey(self):
        'Return currently configured LoRa application key'
        return self.invoke('APPKEY?')

    @appkey.setter
    def appkey(self, val):
        '''Configure LoRa application key

        This key is only used during the OTAA activation mode. The configured
        value must be exactly 16 bytes.
        '''
        return self.invoke('APPKEY=%s' % val)

    def join(self, timeout=60):
        'Perform LoRa OTAA JOIN'
        with self.lock:
            self.invoke('JOIN')
            status = self.wait_for_event('event', timeout=timeout)
            if status == (1, 1):
                return
            elif status == (1, 0):
                raise Exception('JOIN failed')
            else:
                raise Exception('Unsupported event received')

    @property
    def joindc(self):
        '''Return current LoRa JOIN duty cycle setting

        This setting is only applicable to EU868, IN865, AS923, and KR920 radio
        frequency bands. Enabled by default.
        '''
        return int(self.invoke('JOINDC?')) == 1

    @joindc.setter
    def joindc(self, val):
        '''Enable or disable LoRa JOIN duty cycle

        This setting is only applicable to EU868, IN865, AS923, and KR920 radio
        frequency bands. Enabled by default.
        '''
        return self.invoke('JOINDC=%d' % (1 if val is True else 0))

    def link_check(self, opt=False, timeout=10):
        '''Perform a link check

        This command will generate LinkCheckReq MAC command and wait for
        LoRaCheckAns MAC response from the server. The parameter opt determines
        when to send the MAC command. If True, the command will be queued and
        sent with the next application message. If False, the MAC command will
        be sent immediately with empty application payload.

        Returns a tuple of two integers on success. The first element in the
        tuple represents link margin. The second element represents the number
        of gateways that heard the packet.

        None is returned if no response was received from the LoRa network.

        If the operation times out, the function will raise an exception.
        '''
        q = Queue()
        cb = lambda d: q.put_nowait(d)
        self.once('event', cb)
        self.once('answer', cb)
        try:
            with self.lock:
                self.invoke('LNCHECK=%d' % 1 if opt is True else 0)
                event = q.get(timeout=timeout)
                if event[1] == 1:
                    rc, margin, count = q.get(timeout=0.2)
                    if rc != 2:
                        raise Exception('Invalid answer code')
                    return (margin, count)
        finally:
            self.off('event', cb)
            self.off('answer', cb)

    @property
    def rfparam(self):
        '''Return a list of RF channel parameters

        This API returns a list of RFConfig named tuples. Each tuple contains a
        logical number of the RF channel, the center frequency of the channel,
        maximum data rate, and minimum data rate.
        '''
        data = tuple(self.invoke('RFPARAM?').split(';'))
        if int(data[0]) != len(data) - 1:
            raise Exception('Could not parse RFPARAM response')
        return tuple(map(lambda v: RFConfig(*tuple(map(int, v.split(',')))), data[1:]))

    @rfparam.setter
    def rfparam(self, value):
        '''Configure the parameters for a single RF channel

        The input value must be a RFConfig named tuple which contains the
        channel number, center frequency, minimum data rate, and maximum data
        rate.
        '''
        return self.invoke('RFPARAM=%d,%d,%d,%d' % (value.channel, value.frequency, value.min_dr, value.max_dr))

    @property
    def rfpower(self):
        '''Return RF power configuration

        This API returns a tuple of two integers. The first integer represents
        the power mode. If set to 0, the transmitter operates in the RFO mode.
        If set to 1, the transmitter operates in PABOOST mode.

        The second integer determines the output power of the radio transmitter.
        This value is in <0, 15>. When set to 0, the transmitter operates at
        maximum EIRP. For each subsequent value, 2 is subtracted from the
        maximum EIRP, i.e., 1 indicates that the transmitter transmits at
        maximum EIRP - 2.

        The default maximum EIRP is as follows:
          - AS923: 16 dBm
          - AU915: 30 dBm
          - EU868: 16 dBm
          - KR920: 14 dBm
          - IN865: 30 dBm
          - US915: 30 dBm
        '''
        return tuple(map(int, self.invoke('RFPOWER?').split(',')))

    @rfpower.setter
    def rfpower(self, val):
        '''Configure RF transmitter mode and transmit power

        the value must be a tuple of two integers. The first integer represents
        the power mode. If set to 0, the transmitter will operate in the RFO
        mode. If set to 1, the transmitter will operates in PABOOST mode.

        The second integer determines the output power of the radio transmitter.
        This value is in <0, 15>. When set to 0, the transmitter will operate at
        maximum EIRP. For each subsequent value, 2 is subtracted from the
        maximum EIRP, i.e., 1 indicates that the transmitter will transmit at
        maximum EIRP - 2.

        The default maximum EIRP is as follows:
          - AS923: 16 dBm
          - AU915: 30 dBm
          - EU868: 16 dBm
          - KR920: 14 dBm
          - IN865: 30 dBm
          - US915: 30 dBm
        '''
        return self.invoke('RFPOWER=%d,%d' % (val[0], val[1]))

    @property
    def nwk(self):
        'Return current public/private LoRa network setting'
        return LoRaNetwork(int(self.invoke('NWK?')))

    @nwk.setter
    def nwk(self, val):
        '''Set public/private LoRa network setting

        The default value is public.
        '''
        return self.invoke('NWK=%d' % val.value)

    @property
    def adr(self):
        'Return the current state of LoRa ADR setting'
        return self.invoke('ADR?') == '1'

    @adr.setter
    def adr(self, val):
        'Enable or disable LoRa adaptive data rate (ADR)'
        return self.invoke('ADR=%d' % (1 if val is True else 0))

    @property
    def dr(self):
        '''Return the current LoRa data rate

        Returns an integer in <0, 15>. The value can be translated to a symbolic
        name using one of the LoRaDataRate enums, depending on the currently
        selected RF band.
        '''
        return int(self.invoke('DR?'))

    @dr.setter
    def dr(self, val):
        '''Set LoRa data rate

        The value must be one of the LoRaDataRate* enums, depending on the
        currently selected RF band.
        '''
        return self.invoke('DR=%d' % val.value)

    @property
    def delay(self):
        '''Return currently configured receive window offsets

        The returned value is a Delay namedtuple which contains the delay for
        various receive windows in millseconds.
        '''
        return Delay(*map(int, self.invoke('DELAY?').split(',')))

    @delay.setter
    def delay(self, value):
        '''Configure receive window offsets

        The value must be a Delay namedtuple which provides the delay for
        various receive windows in millseconds.
        '''
        return self.invoke('DELAY=%d,%d,%d,%d' % (value.join_accept_1, value.join_accept_2, value.rx_window_1, value.rx_window_2))

    @property
    def adrack(self):
        '''Return configured ADR ACK parameters

        The returned value is a tuple of two integers. The first integer
        represents the ADR ACK limit. The default value is 64. The second
        integer represents the ADR ACK delay. The default value is 32.
        '''
        return tuple(map(int, self.invoke('ADRACK?').split(',')))

    @adrack.setter
    def adrack(self, value):
        '''Configure ADR ACK parameters

        The value must be a tuple of two integers. The first integer represents
        the ADR ACK limit. The default value is 64. The second integer
        represents the ADR ACK delay. The default value is 32.
        '''
        return self.invoke('ADRACK=%d,%d' % (value[0], value[1]))

    @property
    def rx2(self):
        '''Return the frequency and data rate of the RX2 window

        The returned value is a tuple of two integers. The first integer returns
        the frequency of the RX2 receive window. The second integer represents
        the data rate used in that window.
        '''
        return tuple(map(int, self.invoke('RX2?').split(',')))

    @rx2.setter
    def rx2(self, value):
        '''Configure the frequency and data rate of the RX2 window

        The value must be a tuple of two integers. The first integer represents
        the frequency used when receiving in the RX2 window. The second integer
        represents the data rate to be used.
        '''
        return self.invoke('RX2=%d,%d' % (value[0], value[1]))

    @property
    def dutycycle(self):
        'Return current duty cycle setting (only in EU868 band)'
        return int(self.invoke('DUTYCYCLE?')) == 1

    @dutycycle.setter
    def dutycycle(self, onoff):
        'Enable or disable duty cycling in the EU868 band'
        return self.invoke('DUTYCYCLE=%d' % (1 if onoff is True else 0))

    @property
    def sleep(self):
        'Return True if low power (sleep) mode is enabled and False otherwise'
        return int(self.invoke('SLEEP?')) == 1

    @sleep.setter
    def sleep(self, onoff):
        '''Enable or disable low power (sleep) mode

        When low power mode is enabled, the modem will automatically go to sleep
        after sending or receiving data. Activity on the UART port wakes the
        modem up.
        '''
        return self.invoke('SLEEP=%d' % (1 if onoff is True else 0))

    @property
    def port(self):
        'Return default port number for uplink messages'
        return int(self.invoke('PORT?'))

    @port.setter
    def port(self, value):
        'Configure default port number for uplink messages'
        return self.invoke('PORT=%d' % value)

    @property
    def rep(self):
        'Return the currently configured number of unconfirmed uplink message retries'
        return int(self.invoke('REP?'))

    @rep.setter
    def rep(self, val):
        '''Configure the number of unconfirmed uplink message retries

        Accepted range: 1-15. The default value is 1.
        '''
        return self.invoke('REP=%d' % val)

    @property
    def dformat(self):
        '''Return payload format used by the modem

        If the value is 0, the modem expects and returns payload encoded in
        ASCII. If the value is 1, the modem expects and returns the payload
        ecoded in hex.
        '''
        return int(self.invoke('DFORMAT?'))

    @dformat.setter
    def dformat(self, value):
        '''Configure payload format

        If set to 0, the modem will assume the payload is encoded in ASCII. If
        set to 1, the modem expects payloads encoded in hex.
        '''
        return self.invoke('DFORMAT=%d' % value)

    @property
    def to(self):
        '''Return currently configured UART port timeout

        The timeout represents the time interval within which the entire message
        payload must be transmitted by the application over the UART interface.
        The value is in milliseconds.
        '''
        return int(self.invoke('TO?'))

    @to.setter
    def to(self, value):
        '''Configure UART port timeout

        The timeout represents the time interval within which the entire message
        payload must be transmitted by the application over the UART interface.
        The value depends on the maximum size of the payload and the baudrate of
        the UART interface. It should be at least payload_size * 10 / baudrate.
        The value is in milliseconds. The default value is 1000 milliseconds.
        '''
        return self.invoke('TOP=%d' % value)

    def tx(self, data, confirmed=False, timeout=None, hex=False):
        '''Send a confirmed or unconfirmed uplink message on the default port

        Set confirmed to True to request a confirmation from the network. The
        timeout value, if set, configures the number of seconds to wait for the
        acknowledgement.
        '''
        type = 'C' if confirmed else 'U'
        with self.lock:
            self.at('%sTX %d' % (type, len(data)), flush=False)
            if hex:
                self.ser.write(binascii.hexlify(data))
            else:
                self.ser.write(data)
            self.ser.flush()
            self.wait_for_reply()
            if confirmed:
                return self.wait_for_event('ack', timeout=timeout)

    @property
    def mcast(self):
        '''Return the set of configured multicast addresses

        The returned value is a tuple of McastAddr namedtuples. Each McastAddr
        tuple consists of an index, address, network session key, and
        application session key used by the multicast address. Up to 8 multicast
        addresses can be configured in the modem.
        '''
        data = tuple(self.invoke('MCAST?').split(';'))
        if int(data[0]) != len(data) - 1:
            raise Exception('Could not parse MCAST response')
        return tuple(map(lambda v: McastAddr(*tuple(v.split(','))), data[1:]))

    @mcast.setter
    def mcast(self, value):
        '''Configure a multicast address into the modem

        The value must be a McastAddr named tuple. Up to 8 multicast addresses
        can be configured in the modem.
        '''
        return self.invoke('MCAST=%d,%s,%s,%s' % (value.index, value.addr, value.nwkskey, value.appskey))

    def ptx(self, port, data, confirmed=False, timeout=None, hex=False):
        '''Send a confirmed or unconfirmed uplink message on the given port

        Set confirmed to True to request a confirmation from the network. The
        timeout value, if set, configures the number of seconds to wait for the
        acknowledgement.
        '''
        type = 'C' if confirmed else 'U'
        with self.lock:
            self.at('P%sTX %d,%d' % (type, port, len(data)), flush=False)
            if hex:
                self.ser.write(binascii.hexlify(data))
            else:
                self.ser.write(data)
            self.ser.flush()
            self.wait_for_reply()
            if confirmed:
                return self.wait_for_event('ack', timeout=timeout)

    @property
    def frmcnt(self):
        '''Return the current value for uplink and downlink frame counters

        The returned value is a tuple of two integers. The first integer
        represents the uplink counter. The second value represents the downlink
        counter.
        '''
        return tuple(map(int, self.invoke('FRMCNT?').split(',')))

    @property
    def msize(self):
        'Return the maximum payload size for the current data rate'
        return int(self.invoke('MSIZE?'))

    @property
    def rfq(self):
        '''Return the RSSI and SNR of the last received message

        The returned value is a tuple of two integers where the first integer
        represents the RSSI and the second integer represents the SNR.
        '''
        return tuple(map(int, self.invoke('RFQ?').split(',')))

    @property
    def dwell(self):
        '''Return currently configured dwell setting for the AS923 band

        The value is a tuple of two booleans. The first boolean represents
        uplink dwell setting, the second integer represents downlink dwell
        setting.
        '''
        return tuple(map(lambda v: v == 1, map(int, self.invoke('DWELL?'))))

    @dwell.setter
    def dwell(self, value):
        '''Configure uplink and downlink dwell time for the AS923 band

        The value must be a tuple of two booleans. The first boolean configures
        the uplink dwell setting. The second boolean configures the downlink
        dwell setting.
        '''
        v = map(lambda v: 1 if v is True else 0, value)
        return self.invoke('DWELL=%d,%d' % (v[0], v[1]))

    @property
    def maxeirp(self):
        'Return currently configured maximum EIRP'
        return int(self.invoke('MAXEIRP?'))

    @maxeirp.setter
    def maxeirp(self, val):
        '''Configure maximum EIRP

        The value is in dBm. The default is 16 dBm.
        '''
        return self.invoke('MAXEIRP=%d' % val)

    @property
    def rssith(self):
        '''Return current RSSI threshold for listen before talk (LBT)

        The returned value is an integer in dBm
        '''
        return int(self.invoke('RSSITH?'))

    @rssith.setter
    def rssith(self, value):
        '''Configure RSSI threshold for listen before talk (LBT)

        The value must be an integer in dBm The default is -85 dBm.
        '''
        return self.invoke('RSSITH=%d' % value)

    @property
    def cst(self):
        '''Return carrier sensor time (CST) for listen before talk (LBT)

        This setting is only used on AS923 and KR920 bands. The returned value
        is an integer in milliseconds.
        '''
        return int(self.invoke('CST?'))

    @cst.setter
    def cst(self, value):
        '''Configure carrier sensor time (CST) for listen before talk (LBT)

        The value must be an integer in millseconds. The default value is 6
        millseconds.
        '''
        return self.invoke('CST=%d' % value)

    @property
    def backoff(self):
        '''Return the current duty cycle backoff time (only on EU868 band)

        The returned value is the backoff time in milliseconds. If the value is
        not zero, the transmitter is duty cycling and cannot transmit.
        '''
        return int(self.invoke('BACKOFF?'))

    @property
    def chmask(self):
        '''Return currently configured channel mask

        The returned value is a string in hexadecimal format, where every bit
        represents one channel. The left-most bit represents channel 0. For
        example: FF0000000000000000. The string is 18 characters long (9 bytes).
        '''
        return self.invoke('CHMASK?')

    @chmask.setter
    def chmask(self, value):
        '''Configure channel mask

        The channel mask is a hexa-decimal string where every bit represents one
        channel. The left-most bit represents channel 0. The string must be 18
        characters long (8 bytes). For example: FF0000000000000000.
        '''
        return self.invoke('CHMASK=%s' % value)

    @property
    def rtynum(self):
        'Return configured number of confirmed uplink message retries'
        return int(self.invoke('RTYNUM?'))

    @rtynum.setter
    def rtynum(self, val):
        '''Configure number of confirmed uplink message retries

        Accepted values: 1-8. The default is 8.
        '''
        return self.invoke('RTYNUM=%d' % val)

    @property
    def netid(self):
        return self.invoke('NETID?')

    @netid.setter
    def netid(self, val):
        return self.invoke('NETID=%s' % val)


    def reset(self):
        '''Initialize the LoRa modem into a known state.

        This method performs reboot, checks that the AT command interface is
        present and can be used.
        '''
        # Send CR in case there is some data in buffers
        self.ser.write('\r'.encode())
        self.ser.flush()

        # Read any input from the device until we time out
        while True:
            try:
                line = self.response.get(timeout=0.2)
                self.response.task_done()
            except Empty:
                break

        # Reboot the device and wait for it to signal that it has rebooted with
        # an event.
        self.reboot()

        # It seems we need to wait a bit for the modem to initialize after reboot
        sleep(0.1)

        # Invoke empty AT command to make sure the AT command interface is working
        self.invoke(None)

        self.dformat = False

        try:
            self.joindc = False
        except:
            pass


    def switch_band(self, band):
        # If the new band is different from the currently configured band,
        # update it and reboot the modem to apply new settings. If they are the
        # same, do nothing.
        if self.band.value != band.value:
            self.band = band
            self.reboot()


def join(device):
    device.mode = LoRaMode.OTAA
    device.join()


def trx(device, hex=False, base64=False):
    try:
        device.on('message', lambda port, data: print('%d:%s' %
            (port, base64.b64encode(data).decode() if base64 else data.encode('ascii'))))
        for line in sys.stdin:
            comps = line.rstrip('\n').split(':')
            if len(comps) == 2:
                data, confirmed = comps
                device.tx(base64.b64decode(data) if base64 else data.encode('ascii'),
                    confirmed=confirmed.lower() == 'c', hex=hex)
            elif len(comps) == 3:
                port, data, confirmed = comps
                device.ptx(int(port), base64.b64decode(data) if base64 else data.encode('ascii'),
                    confirmed=confirmed.lower() == 'c', hex=hex)
            else:
                raise Exception('Invalid input format')
    except KeyboardInterrupt:
        pass


def show_device_info(device):
    print('Model=%s' % device.dev)
    print('Firmware=%s' % device.ver[0])
    print('NetID=%s' % device.netid)
    print('DevEUI=%s' % device.deveui)
    print('AppEUI=%s' % device.appeui)
    print('AppKey=%s' % device.appkey)

    print('UART=%r' % (device.uart, ))
    #print('DBG=%d' % device.dbg)


def check_link(device):
    reply = device.link_check()
    print('link margin: %d, gateway count: %d' % reply)


def show_session_info(device):
    print('DevAddr=%s' % device.devaddr)
    print('NwkSKey=%s' % device.nwkskey)
    print('AppSKey=%s' % device.appskey)


def configure_device(device, hex=False):
    device.switch_band(LoRaBand.US915)
    sleep(1)
    device.nwk    = LoRaNetwork.PUBLIC
    device.appeui = "0101010101010101"
    device.class_ = LoRaClass.CLASS_C
    device.dr     = LoRaDataRateUS915.SF7_125
    device.adr    = True
    device.rep    = 1
    #device.rtynum = 8
    device.dformat = 1 if hex is True else 0


def main():
    parser = argparse.ArgumentParser(description='LoRa driver')
    parser.add_argument('action', type=str, nargs='?', help='Optional action to perform, one of: trx, join, link, session')
    parser.add_argument('--device', type=str, default='/dev/serial0', help='Special filename of the device')
    parser.add_argument('--speed', type=int, default=9600, help='Serial port baud rate')
    parser.add_argument('--debug', action='store_true', help='Enable debugging')
    parser.add_argument('--hex', action='store_true', help='Transmit payloads hex-encoded over ATCI')
    parser.add_argument('--base64', action='store_true', help='Transmit payloads base64-encoded over stdio (trx action)')
    parser.add_argument('--guard', type=int, default=0, help='AT command guard interval in milliseconds')
    args = parser.parse_args()

    device = LoRaModule(args.device, args.speed, debug=args.debug, guard=args.guard)

    device.open()
    device.reset()
    if args.debug:
        print('Found device type %s, firmware %s, DevEUI %s' % (device.dev, device.ver[0], device.deveui))

    configure_device(device, hex=args.hex)

    if args.action == None:
        show_device_info(device)
    elif args.action == 'trx':
        trx(device, hex=args.hex, base64=args.base64)
    elif args.action == 'join':
        join(device)
    elif args.action == 'link':
        check_link(device)
    elif args.action == 'session':
        show_session_info(device)
    else:
        raise Exception('Unsupported action')

    device.close()


if __name__ == '__main__':
    main()
