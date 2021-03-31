from bcf.flasher.serialport.ftdi import SerialPort
import click


def read(serial):
    data = b''
    while serial:
        data += serial.read(1)
        if data.endswith(b'\r\n\r\n'):
            return data


@click.command()
@click.option('-b', '--baudrate', default=9600, type=int)
@click.option('-c', '--count', default=100, type=int)
@click.argument('device')
def main(device, baudrate, count):
    serial = SerialPort(device, baudrate)
    error_cnt = 0

    for i in range(count):
        serial.write(b'AT\r')
        if read(serial) != b'+OK\r\n\r\n':
            error_cnt += 1

    print('error_cnt', error_cnt)


if __name__ == '__main__':
    main()
