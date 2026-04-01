# Example Summary

Sample application that implements a simple command interface to an I2C target (see [cmdinterface.h]).

This example is intended for use together with the `i2ctarget` example.

## Peripherals & Pin Assignments

When this project is built, the SysConfig tool will generate the TI-Driver
configurations into the __ti_drivers_config.c__ and __ti_drivers_config.h__
files. Information on pins and resources used is present in both generated
files. Additionally, the System Configuration file (\*.syscfg) present in the
project may be opened with SysConfig's graphical user interface to determine
pins and resources used.

* `CONFIG_GPIO_LED_0` - Indicator LED.
* `CONFIG_I2C_0` - I2C bus used to communicate with the I2C target.
* `CONFIG_Display_0` - Example output.

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

Before running the example the following pins must be connected between the
I2C controller and I2C target devices, in addition to __`GND`__.

  |  I2C Controller LaunchPad   |    I2C Target LaunchPad     |
  |:---------------------------:|:---------------------------:|
  | __`CONFIG_GPIO_I2C_0_SCL`__ | __`CONFIG_GPIO_I2C_0_SCL`__ |
  | __`CONFIG_GPIO_I2C_0_SDA`__ | __`CONFIG_GPIO_I2C_0_SDA`__ |

## Example Usage

* Example output is generated through use of Display driver APIs.
  Refer to the Display driver documentation.
* Open a serial session (for example [`PuTTY`][putty-homepage]) to the appropriate COM port.
    * The COM port can be determined via Device Manager in Windows or via `ls /dev/tty*` in Linux.

The connection will have the following settings:

```text
Baud-rate:     115200
Data bits:          8
Stop bits:          1
Parity:          None
Flow Control:    None
```

Run the example.

1. __`CONFIG_GPIO_LED_0`__ turns ON to indicate the I2C driver initialization is complete.

    ```text
    Starting the i2ccontroller example
    ```

2. The I2C controller sends the `CMD_CTL_GET_STATUS` command until a valid response is received.

    ```text
    Sending GET_STATUS until target responds
    ```

3. When a valid response is received, the example displays this over UART:

    ```text
    Target responded!
    ```

4. The I2C controller now enters a loop where it will loop through 4 commands.
    1. `CMD_CTL_SET_STATUS`: Writes a status byte to the target (the status value is incremented for each iteration).
    2. `CMD_CTL_GET_STATUS`: Reads status byte from the target.
    3. `CMD_CTL_WRITE_BLOCK`: Write data block to the target.
    4. `CMD_CTL_READ_BLOCK`: Read data block from the target

    For each command, the controller displays the result over UART and the task sleeps for 1 second.

    ```text
    Sending SET_STATUS = 0
    Sending GET_STATUS
    GET_STATUS returned 0
    Sending WRITE_BLOCK 'Hi, I am the controller'
    Sending READ_BLOCK
    READ_BLOCK returned 'Hello, I am the target!'
    ```

* If the I2C communication fails, an error message describing
the failure is displayed.

    ```text
    I2C target address 0x71 not acknowledged!
    ```

## Application Design Details

This application uses one task:

`'mainThread'` - performs the following actions:

1. Opens and initializes an I2C driver object.

2. Uses the I2C driver in blocking mode to send commands over the defined command interface.

3. Prints command information via the UART.

4. The task sleeps for 1 second between each command.

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.

[putty-homepage]: http://www.putty.org "PuTTY's homepage"
[cmdinterface.h]: ./cmdinterface.h
