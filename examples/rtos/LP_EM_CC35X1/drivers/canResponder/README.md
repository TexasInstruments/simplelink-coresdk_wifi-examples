## Example Summary

This example demonstrates using the CAN driver to communicate with another
SimpleLink&trade; device. To run this example successfully, another
SimpleLink&trade; device running the `canInitiator` example is required.

## Peripherals & Pin Assignments

When this project is built, the SysConfig tool will generate the TI-Driver
configurations into the __ti_drivers_config.c__ and __ti_drivers_config.h__
files. Information on pins and resources used is present in both generated
files. Additionally, the System Configuration file (\*.syscfg) present in the
project may be opened with SysConfig's graphical user interface to determine
pins and resources used.

## BoosterPacks, Board Resources & Jumper Settings

This example requires two LaunchPads with an integrated CAN controller and two
external CAN transceiver boards to communicate on the CAN bus. Kindly refer to
the board specific README file for details on what pins of the launchpad have to
be connected with the external transceiver.

NOTE: In LP_EM_CC35XX launchpad, GPIO30 is connected to DCAN_TX and GRN_LED. Also,
GPIO34 is connected to DCAN_RX and RED_LED. By default, GRN_LED and RED_LED are
disabled and CONFIG_GPIO_LED_0 and CONFIG_GPIO_LED_1 are not configured. The user
has to configure CONFIG_GPIO_LED_0 and CONFIG_GPIO_LED_1 through SysConfig before
running the example.

Initialization and configuration of the CAN transceiver is not handled by this
example and must be performed independently.

Before running the example, the CANL and CANH bus signals should be connected
between the canInitiator and canResponder transceivers with the appropriate CAN
bus termination.

For board specific jumper settings, resources and BoosterPack modifications,
refer to the __Board.html__ file.

> If you're using an IDE such as Code Composer Studio (CCS) or IAR, please
refer to Board.html in your project directory for resources used and
board-specific jumper settings.

The Board.html can also be found in your SDK installation:

```text
<SDK_INSTALL_DIR>/source/ti/boards/<BOARD>
```

## Example Usage

* Open a serial session (e.g. [`PuTTY`](http://www.putty.org/ "PuTTY's
Homepage"), etc.) to the appropriate COM port.
    * The COM port can be determined via Device Manager in Windows or via
      `ls /dev/tty*` in Linux.

The connection should have the following settings:

```text
    Baud-rate:  115200
    Data bits:       8
    Stop bits:       1
    Parity:       None
    Flow Control: None
```

* Run the example. `CONFIG_GPIO_LED_0` turns ON to indicate driver
initialization is complete.

* Once both the canInitator and canResponder examples are running, CAN messages
can be exchanged. See canInitiator README for details on initiating the
exchange.

* The canResponder waits to receive CAN messages. `CONFIG_GPIO_LED_1` turns ON
when a message is received and the received CAN message details are output to
the UART.

* When a message is received, a reply is transmitted with the ID and data bits
flipped. `CONFIG_GPIO_LED_1` turns OFF after the reply is sent.

Sample UART output after pressing BTN-1 on the canInitiator LaunchPad:

```text
    CAN Responder ready. Waiting for CAN messages...

    RxMsg Cnt: 1, RxEvt Cnt: 1
    Msg ID: 0x12345678
    TS: 0xd176
    CAN FD: 1
    DLC: 15
    BRS: 1
    ESI: 0
    Data[64]: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15
    16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E
    2F 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F

    > Response sent.
```

Sample UART output after pressing BTN-2 on the canInitiator LaunchPad:

```text
    RxMsg Cnt: 2, RxEvt Cnt: 2
    Msg ID: 0x5aa
    TS: 0x2e0b
    CAN FD: 0
    DLC: 8
    BRS: 0
    ESI: 0
    Data[8]: 00 01 02 03 04 05 06 07

    > Response sent.
```

## Application Design Details

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.
