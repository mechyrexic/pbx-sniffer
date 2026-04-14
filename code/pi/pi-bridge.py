import serial
import requests
from bs4 import BeautifulSoup
import datetime
import json

ser: serial.Serial = None

def setup_serial():

    try:
        global ser
        ser = serial.Serial(port="/dev/ttyUSB0", baudrate=115200, timeout=1, rtscts=True)
        print(f"Serial port {ser.name} opened successfully")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        exit()


def main():

    setup_serial()

    while True:

        try:
            data = ser.read_until(b']')
        except serial.SerialException as e:
            print(f"Could not read_until: {e}")

        try:
            res = requests.post("http://localhost:3000/log", json={"text": ''})
        except:
            print("Error sending request to URL")

    file.close()
    ser.close()




if __name__ == "__main__":
    main()