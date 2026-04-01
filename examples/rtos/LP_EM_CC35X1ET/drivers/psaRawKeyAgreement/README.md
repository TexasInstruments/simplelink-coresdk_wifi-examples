## Example Summary

This example demonstrates using the PSA Crypto API to import private keys with
various lifetimes and perform an Elliptic-Curve Diffie-Hellman (ECDH) key
agreement where an application needs direct access to the resulting shared
secret.

## Peripherals & Pin Assignments

When this project is built, the SysConfig tool will generate the TI-Driver
configurations into the __ti_drivers_config.c__ and __ti_drivers_config.h__
files. Information on pins and resources used is present in both generated
files. Additionally, the System Configuration file (\*.syscfg) present in the
project may be opened with SysConfig's graphical user interface to determine
pins and resources used.

* `CONFIG_Display_0` - Displays execution details on the UART.
* `CONFIG_GPIO_LED_0` - Indicates driver initialization completed successfully.
* `CONFIG_GPIO_LED_1` - Indicates all operations completed successfully.

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

* Run the example.

* `CONFIG_GPIO_LED_0` turns ON to indicate driver initialization is complete.

* The target will print execution details to the UART.

* `CONFIG_GPIO_LED_1` turns ON if key agreement passes for all key lifetimes.

### Sample UART output

```text
    Starting the PSA Crypto Raw Key Agreement example.

    Provisioning Hardware Unique Key (HUK)...

    Private Key: 0x7D7DC5F71EB29DDAF80D6214632EEAE03D9058AF1FB6D22ED80BADB62BC1A534
    Peer Public Key: 0x04700C48F77F56584C5CC632CA65640DB91B6BACCE3A4DF6B42CE7CC838833D287
    DB71E509E3FD9B060DDB20BA5C51DCC5948D46FBF640DFE0441782CAB85FA4AC

    Key persistence/location: [Volatile / Local Storage]
    Key ID: 0x7ffffff1
    Calling psa_raw_key_agreement()
    Shared Secret: 0x46FC62106420FF012E54A434FBDD2D25CCC5852060561E68040DD7778997BD7B
    PASSED!

    Key persistence/location: [Default / Local Storage]
    Key ID: 0x2
    Calling psa_raw_key_agreement()
    Shared Secret: 0x46FC62106420FF012E54A434FBDD2D25CCC5852060561E68040DD7778997BD7B
    PASSED!

    Key persistence/location: [Volatile / HSM Asset Store]
    Key ID: 0x7ffffff1
    Calling psa_raw_key_agreement()
    Shared Secret: 0x46FC62106420FF012E54A434FBDD2D25CCC5852060561E68040DD7778997BD7B
    PASSED!

    Key persistence/location: [Default / HSM Asset Store]
    Key ID: 0x4
    Calling psa_raw_key_agreement()
    Shared Secret: 0x46FC62106420FF012E54A434FBDD2D25CCC5852060561E68040DD7778997BD7B
    PASSED!

    Key persistence/location: [HSM Asset Store / HSM Asset Store]
    Key ID: 0x5
    Calling psa_raw_key_agreement()
    Shared Secret: 0x46FC62106420FF012E54A434FBDD2D25CCC5852060561E68040DD7778997BD7B
    PASSED!

    DONE!
```

## Application Design Details

This application uses two threads, `mainThread` and `keyAgreementThread`, which
perform the following actions:

1. Opens the Display driver for UART output.

2. Initializes PSA Crypto API.

3. Imports a private key with a valid lifetime.

4. Performs a raw key agreement with a peer public key and the imported private
   key to compute a shared secret.

5. Validates the shared secret output matches the expected shared secret.

6. Destroys the key.

7. Repeats steps 3-6 for every valid key lifetime.

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.
