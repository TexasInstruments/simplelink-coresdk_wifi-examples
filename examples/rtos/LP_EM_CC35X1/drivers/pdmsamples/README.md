## Example Summary

This example uses the PDM driver to capture audio from a PDM MEMS microphone
and stream the decoded PCM samples to a host PC over UART. Pressing BUTTON_0
starts streaming and pressing BUTTON_1 stops it. The host receives a
continuous stream of raw PCM bytes that can be saved and played back using
any audio tool (for example, Audacity).

## Peripherals & Pin Assignments

When this project is built, the SysConfig tool will generate the TI-Driver
configurations into the __ti_drivers_config.c__ and __ti_drivers_config.h__
files. Information on pins and resources used is present in both generated
files. Additionally, the System Configuration file (\*.syscfg) present in the
project may be opened with SysConfig's graphical user interface to determine
pins and resources used.

* `CONFIG_GPIO_BUTTON_0` - Starts PDM audio streaming.
* `CONFIG_GPIO_BUTTON_1` - Stops PDM audio streaming.
* `CONFIG_GPIO_LED_0` - Red LED. Blinks on each captured buffer while streaming; solid ON on error.
* `CONFIG_GPIO_LED_1` - Green LED. Solid ON when ready to stream; turns OFF on error.
* `CONFIG_UART2_0` - Streams raw PCM data to the host at 921600 baud.
* `CONFIG_PDM_0` - Captures audio from the PDM microphone.

## BoosterPacks, Board Resources & Jumper Settings

This example uses an external
[Adafruit PDM MEMS Microphone Breakout](https://www.adafruit.com/product/3492)
wired to the PDM clock and data pins on the LaunchPad.

For board specific jumper settings, pin assignments, and resources refer to
the __Board.html__ file.

> If you're using an IDE such as Code Composer Studio (CCS) or IAR, please
refer to Board.html in your project directory for resources used and
board-specific jumper settings.

The Board.html can also be found in your SDK installation:

```text
<SDK_INSTALL_DIR>/source/ti/boards/<BOARD>
```

Connect the PDM microphone breakout to the PDM clock and data pins listed in
Board.html. Pull the microphone's channel-select pin (SEL) to GND to select
the left channel (DATA0).

## Example Usage

* Open a serial session (any tool capable of receiving raw binary data) to
  the appropriate COM port.
    * The COM port can be determined via Device Manager in Windows or via
      `ls /dev/tty*` in Linux.

The connection should have the following settings:

```text
    Baud-rate:  921600
    Data bits:       8
    Stop bits:       1
    Parity:       None
    Flow Control: None
```

* Run the example. The __Green LED turns solid ON__ to indicate the PDM driver has
  opened successfully and the firmware is idle, waiting for BUTTON_0.

* Press __BUTTON_0__ to start streaming. The __Red LED begins blinking__
  with each captured buffer while the Green LED remains solid ON. Raw PCM bytes are sent continuously over UART.

* Press __BUTTON_1__ to stop streaming. The Red LED stops blinking and the
  __Green LED remains solid ON__.

### LED state summary

| Red      | Green      | State                             |
|:--------:|:----------:|:----------------------------------|
| OFF      | ON (solid) | Idle - waiting for BUTTON_0       |
| Blinking | ON (solid) | Streaming - capturing audio       |
| ON       | OFF        | Error - PDM driver failed to open |
| OFF      | OFF        | Terminal - PDM driver closed      |

### PCM stream format

The bytes received over UART are raw, little-endian, 16-bit signed PCM
samples at 16 kHz, mono. There is no framing or header — the stream begins
immediately when `PDM_startStream` is called and ends when `PDM_stopStream`
is called. A host application can write these bytes directly into a WAV
container and open the result in Audacity or any other audio tool.

## Application Design Details

A single thread, `mainThread`, owns the full driver lifecycle:

1. Initialises `GPIO`, `PDM`, and `UART2` drivers.
2. Configures `BUTTON_0` and `BUTTON_1` as falling-edge GPIO interrupts.
   The interrupt callbacks post POSIX semaphores to signal the main thread.
3. Opens the PDM driver at 16 kHz sample rate with 16-bit PCM width.
   Two 4 KB DMA buffers are used in a ping-pong arrangement via
   `PDM_Transaction` objects. The number of buffers and their size are
   configurable, subject to two constraints:
   * __Memory__: total buffer memory must fit within available RAM.
   * __DMA max job size__: the DMA controller imposes a maximum transfer
     size per job. If `bufSize` exceeds this limit, PDM_startStream() calls
     errorCallback to notify the user about buffer size exceeding maximum
     allowable buffer size and does not start any streaming. 
4. Blocks on `semStartStream`. When BUTTON_0 is pressed the thread rebuilds
   the transaction lists, calls `PDM_startStream`, and enters the streaming
   loop.
5. In the streaming loop the thread waits on `semDataReady` with a 1-second
   timeout to remain responsive. The `readCallbackFxn` ISR callback moves
   completed transactions to the application list and posts `semDataReady`.
6. For each completed transaction the thread calls `UART2_write` in blocking
   mode to send the raw PCM bytes to the host, then returns the buffer to DMA
   via `PDM_moveTransactionToDMA`. Because `UART2_write` does not return
   until the last byte has been transmitted, no intermediate copy is required.
7. A non-blocking check on `semErrorCallback` detects PDM hardware errors
   (overflow or Manchester lock timeout). On error the LED indicates the fault
   state and streaming stops.
8. A non-blocking check on `semStopStream` handles BUTTON_1. The thread calls
   `PDM_stopStream` and returns to the idle wait.

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.
