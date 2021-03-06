# Glasshouse

This project shows how to configure ADC1 and read the voltage connected to GPIO pin.

## How to use

### Hardware Required

* A development board with ESP32 SoC (e.g., ESP32-DevKitC, ESP-WROVER-KIT, etc.)
* A USB cable for power supply and programming

In this example, we use `ADC_UNIT_1` by default, we need to connect a voltage source (0 ~ 3.3v) to GPIO34. If another ADC unit is selected in your application, you need to change the GPIO pin (please refer to Chapter 4.11 of the `ESP32 Technical Reference Manual`).

### Configure the project

```
make menuconfig
```

* Set serial port under Serial Flasher Options.

### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
make -j4 flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Example Output

Running this example, you will see the following log output on the serial monitor:

```
Raw: 486   	Soil moisture: 10.00    Voltage: 189mV
Raw: 435   	Soil moisture: 24.00    Voltage: 177mV
Raw: 18   	Soil moisture: 98.00    Voltage: 79mV
```

## Troubleshooting

* program upload failure

    * Hardware connection is not correct: run `make monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

For any technical queries, please open an [issue](https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.
