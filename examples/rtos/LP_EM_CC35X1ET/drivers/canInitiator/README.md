## Example Summary

This example demonstrates using the CAN driver to communicate with another
SimpleLink&trade; device. To run this example successfully, another
SimpleLink&trade; device running the `canResponder` example is required.

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

* Once both the canInitiator and canResponder examples are running, CAN messages
can be exchanged. From the canInitiator, press either BTN-1 or BTN-2 to transmit
a CAN message. `CONFIG_GPIO_LED_1` turns ON when the button is pressed.

All launchpads (other than LP_EM_CC35X1)

    | LaunchPad Button | CAN Message Type    |
    |:----------------:|:-------------------:|
    | BTN-1            | CAN FD with BRS     |
    | BTN-2            | Classic CAN         |

LP_EM_CC35X1

    | LaunchPad Button | CAN Message Type    |
    |:----------------:|:-------------------:|
    | BTN-1            | No effect           |
    | BTN-2            | Classic CAN         |

* The canInitiator will then wait for the canResponder to reply with the same
message with the ID and data bits flipped. `CONFIG_GPIO_LED_1` turns OFF after
the CAN message is received from the responder.

* The target will print the received CAN message details to the UART.

Sample UART output after pressing BTN-1 on the LaunchPad:

```text
    CAN Initiator ready. Waiting for button press...

    > Tx Finished. Cnt = 1

    RxMsg Cnt: 1, RxEvt Cnt: 1
    Msg ID: 0xdcba987
    TS: 0x420f
    CAN FD: 1
    DLC: 15
    BRS: 1
    ESI: 0
    Data[64]: FF FE FD FC FB FA F9 F8 F7 F6 F5 F4 F3 F2 F1 F0 EF EE ED EC EB EA
    E9 E8 E7 E6 E5 E4 E3 E2 E1 E0 DF DE DD DC DB DA D9 D8 D7 D6 D5 D4 D3 D2 D1
    D0 CF CE CD CC CB CA C9 C8 C7 C6 C5 C4 C3 C2 C1 C0

    => PASS: Received message matches expected.
```

Sample UART output after pressing BTN-2 on the LaunchPad:

```text
    > Tx Finished. Cnt = 2

    RxMsg Cnt: 2, RxEvt Cnt: 2
    Msg ID: 0x255
    TS: 0x96f5
    CAN FD: 0
    DLC: 8
    BRS: 0
    ESI: 0
    Data[8]: FF FE FD FC FB FA F9 F8

    => PASS: Received message matches expected.
```

## Application Design Details

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.
