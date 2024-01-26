# CHANGELOG.md

## 0.2.4

Features:

  - add options to version output

## 0.2.3

Features:

  - fix crc comparison

## 0.2.2

Features:

  - add option for deep sleep

## 0.2.1

Features:

  - add option for wifi powersave mode

## 0.2.0

Features:

  - send data every 10 seconds

## 0.1.13

Features:

  - add crc to sent message
  - receiver outputs result of crc comparison (RECV:ADC;UUID;MAC;CRC)
    - CRC = (crc_recv == crc_calc)

## 0.1.12

Features:

  - receiver receives sent data corretly (ADC;UUID;MAC)

## 0.1.11

Bug-Fixes:

  - change sent data structure for sender (WIP)

## 0.1.10

Bug-Fixes:

  - fix length of sent data to not being of size int but of size of sent data

## 0.1.9

Bug-Fixes:

  - potential fix for sending data

## 0.1.8

Features:

  - change mac address format to XX-XX-XX-XX-XX-XX

## 0.1.7

Features:

  - add mac address after uuid

## 0.1.6

Features:

  - add uuid after adc value to identify it clearly

## 0.1.5

Features:

  - add log output messages to sender

## 0.1.4

Features:

  - sender/receiver now output their type and their version number to serial for identification

## 0.1.3

Features:

  - sender now outputs adc value over serial before sending it over ESPNow

## 0.1.2

Features:

  - add receiver code to receive sent data from sensor

## 0.1.0

Features:

  - create PWM pulse for sensor
  - read ADC value from sensor
  - transmit value over ESP-NOW to receiver node
