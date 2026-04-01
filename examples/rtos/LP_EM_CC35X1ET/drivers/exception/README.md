## Example Summary

This example shows how to enable exception decoding.

## Peripherals & Pin Assignments

When this project is built, the SysConfig tool will generate the TI-Driver
configurations into the __ti_drivers_config.c__ and __ti_drivers_config.h__
files. Information on pins and resources used is present in both generated
files. Additionally, the System Configuration file (\*.syscfg) present in the
project may be opened with SysConfig's graphical user interface to determine
pins and resources used.

* `CONFIG_GPIO_LED_0` - GPIO instance used to control the onboard LED.

## BoosterPacks, Board Resources & Jumper Settings

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

After the device is powered on, a fault will be generated. `CONFIG_GPIO_LED_0`
should now be turned on.

If you are using the default log sink, `LogSinkBuf`, you can read out the
log messages generated using ROV.

If you decide to change the default log sink, you should launch the `tilogger`
python tool. For installation and usage of the tool see:

```text
<SDK_INSTALL_DIR>/tools/log/tiutils/README.html
```

## Application Design Details

The example will attempt to make a function call at an illegal address. That
will trigger a fault.

The exception handler used, `Exception_handlerMax()`, will:

1. Disable interrupts.
2. Save the CPU state when the exception occurred.
3. Decode the exception.
4. Log the exception cause.
5. Log the exception CPU state when the exception occurred.
6. Log all Arm Cortex-M debug registers.
7. Call the application's implementation of `Exception_hookFxn()`.
8. Spin in place.

`Exception_hookFxn()` will check the program counter of the
`Exception_ExceptionContext` to see that it matches the expected illegal
address. If they match, the function will turn on `CONFIG_GPIO_LED_0`. This
is meant only to demonstrate how to access the CPU register state from the
hook function. In a real application, this might be replaced by saving critical
state in flash or other storage before gracefully resetting the device.

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.
