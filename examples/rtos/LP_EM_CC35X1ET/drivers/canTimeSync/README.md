## Example Summary

This example demonstrates using the CAN driver to transmit or receive a message
for time synchronization with another SimpleLink&trade; device. This example is
designed to be used with a second SimpleLink&trade; device which is also running
this example.

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
between the transceivers with the appropriate CAN bus termination.

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

* Once example is running on both LaunchPads, CAN messages can be transmitted
from either LaunchPad to the other LaunchPad. Press either BTN-1 or BTN-2 to
transmit a CAN message with a zero byte payload and a specific message ID.

    | LaunchPad Button | CAN Message ID          |
    |:----------------:|:-----------------------:|
    | BTN-1            | Time Sync ID (0x2)      |
    | BTN-2            | Non-Time Sync ID  (0x3) |

* When a message with the Time Sync ID is transmitted or received, the example
will determine when the Start Of Frame (SOF) occurred and then toggle
`CONFIG_GPIO_LED_1` 500us after the SOF. A logic analyzer can be connected to
the CAN TXD/RXD and LED to measure the timing.

* The target will print any received CAN message details to the UART.

### Sample UART output after pressing BTN-1 on the LaunchPad_1

LaunchPad_1 (Transmitter):

```text
    CAN Time Sync ready.
    Press BTN-1 to send time sync msg or BTN-2 to send a regular msg...

    Sending time sync message...

    > Tx Finished. Cnt = 1

    > Tx Event. TXTS = 0x1814, SOF time = 0x024a29b2
```

LaunchPad_2 (Receiver):

```text
    CAN Time Sync ready.
    Press BTN-1 to send time sync msg or BTN-2 to send a regular msg...

    > Time Sync msg Rx'ed. RXTS = 0xae8f, SOF time = 0x015c97b0

    RxMsg Cnt: 1, RxEvt Cnt: 1
    Msg ID: 0x2
    TS: 0xae8f
    CAN FD: 1
    DLC: 0
    BRS: 1
    ESI: 0
    Data[0]:
```

### Sample UART output after pressing BTN-2 on the LaunchPad_1

LaunchPad_1 (Transmitter):

```text
    Sending regular message...

    > Tx Finished. Cnt = 2
```

LaunchPad_2 (Receiver):

```text
    RxMsg Cnt: 2, RxEvt Cnt: 2
    Msg ID: 0x3
    TS: 0x675f
    CAN FD: 1
    DLC: 0
    BRS: 1
    ESI: 0
    Data[0]:
```

## Application Design Details

This application uses one thread, `timeSyncTxThread` , which blocks on a button
press: BTN-1 for transmitting a message with the time sync ID or BTN-2 for
transmitting a message with a regular ID.

An event callback, `eventCallback`, is registered with the CAN driver for
handling various events. Notably, the reception of CAN messages (and associated
Rx timestamps) and the notification of successful transmission of CAN messages
with Event FIFO Control (EFC) bit set (and associated Tx timestamps). These
timestamps are converted to system time and used to toggle a LED exactly 500us
after the Start Of Frame occurs for the CAN time sync message.

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.
