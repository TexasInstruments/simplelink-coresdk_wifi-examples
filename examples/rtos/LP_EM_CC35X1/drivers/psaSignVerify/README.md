## Example Summary

This example demonstrates using the PSA Crypto APIs to sign and verify
messages and hashes using the test vectors provided for various key lifetimes.
Example also shows how to provision a HUK for the HSM HW engine.

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

* `CONFIG_GPIO_LED_0` turns ON to indicate PSA Crypto init is complete

* The target will print execution details to the UART.

* `CONFIG_GPIO_LED_1` turns ON if sign/verify passes for all key lifetimes

### Sample UART output

```text
    Starting the PSA Crypto Sign & Verify example.

    Provisioning Hardware Unique Key (HUK)...

    Private Key: 0xC477F9F65C22CCE20657FAA5B2D1D8122336F851A508A1ED04E479C34985BF96
    Public Key: 0x04B7E08AFDFE94BAD3F1DC8C734798BA1C62B3A0AD1E9EA2A38201CD0889BC7A193603F7479
    59DBF7A4BB226E41928729063ADC7AE43529E61B563BBC606CC5E09

    Key persistence/location: [Volatile / Local Storage]
    Calling psa_sign_message()
    Message: 0xE1130AF6A38CCB412A9C8D13E15DBFC9E69A16385AF3C3F1E5DA954FD5E7C45FD75E2B8C366992
    28E92840C0562FBF3772F07E17F1ADD56588DD45F7450E1217AD239922DD9C32695DC71FF2424CA0DEC1321AA
    47064A044B7FE3C2B97D03CE470A592304C5EF21EED9F93DA56BB232D1EEB0035F9BF0DFAFDCC4606272B20A3
    Signed Output: 0x4C86F09A6F0F17D355E150CEC99F49CB857C6D3DA7587FFAB21B807315CEA45725FB25E8
    3E33EF711384A98133E0A7EE9B9B5791350A90E06C51F24BDBD5130E

    Calling psa_verify_message()
    PASSED!

    Calling psa_sign_hash()
    Hash: 0xA41A41A12A799548211C410C65D8133AFDE34D28BDD542E4B680CF2899C8A8C4
    Signed Output: 0x80416AE98ABB01505C1AC5AD481B534069CFEA230B2F225FD220C14613A424ACEE386787
    48D3C3119AD859F1EFA25CABFC93D29C94F8ACF82BBBE997D5938863

    Calling psa_verify_hash()
    PASSED!

    Key persistence/location: [Default / Local Storage]
    Calling psa_sign_message()
    Message: 0xE1130AF6A38CCB412A9C8D13E15DBFC9E69A16385AF3C3F1E5DA954FD5E7C45FD75E2B8C366992
    28E92840C0562FBF3772F07E17F1ADD56588DD45F7450E1217AD239922DD9C32695DC71FF2424CA0DEC1321AA
    47064A044B7FE3C2B97D03CE470A592304C5EF21EED9F93DA56BB232D1EEB0035F9BF0DFAFDCC4606272B20A3
    Signed Output: 0xF8FD0FFCF6A622C2FBDA484DC91BE6EA1E3FB4D9A91729D3B0E084FEB974DAD6A569B972
    EC5909515C3B9B3213AA5D1894D048501AC17DB6DCBE0E37A1493D20

    Calling psa_verify_message()
    PASSED!

    Calling psa_sign_hash()
    Hash: 0xA41A41A12A799548211C410C65D8133AFDE34D28BDD542E4B680CF2899C8A8C4
    Signed Output: 0x7CA116D1D8CD32B152151117A1567D499926A1871414610350FFEE40DE001F5DC546DBB5
    AD221685942A1CB8F1149B24E139593C928AE0A9C227EB599D760CF5

    Calling psa_verify_hash()
    PASSED!

    Key persistence/location: [Volatile / HSM Asset Store]
    Calling psa_sign_message()
    Message: 0xE1130AF6A38CCB412A9C8D13E15DBFC9E69A16385AF3C3F1E5DA954FD5E7C45FD75E2B8C366992
    28E92840C0562FBF3772F07E17F1ADD56588DD45F7450E1217AD239922DD9C32695DC71FF2424CA0DEC1321AA
    47064A044B7FE3C2B97D03CE470A592304C5EF21EED9F93DA56BB232D1EEB0035F9BF0DFAFDCC4606272B20A3
    Signed Output: 0xCB8DDD31C39D98F2716CEFFEF0F150D4881737222C95EADA11457FCA2E8B58EFD25933D9
    1E7BB9351B28152F5AD7EE896B95C490A482E1C437D4C74A0BE24734

    Calling psa_verify_message()
    PASSED!

    Calling psa_sign_hash()
    Hash: 0xA41A41A12A799548211C410C65D8133AFDE34D28BDD542E4B680CF2899C8A8C4
    Signed Output: 0x7039407840838C897E35E87CE4A1837F1325EE75CDCCF6CD7ECC86B557846028D6243889
    87655C106F7021710D33364C7805DE02287EBF6A1CFFE8369D867098

    Calling psa_verify_hash()
    PASSED!

    Key persistence/location: [Default / HSM Asset Store]
    Calling psa_sign_message()
    Message: 0xE1130AF6A38CCB412A9C8D13E15DBFC9E69A16385AF3C3F1E5DA954FD5E7C45FD75E2B8C366992
    28E92840C0562FBF3772F07E17F1ADD56588DD45F7450E1217AD239922DD9C32695DC71FF2424CA0DEC1321AA
    47064A044B7FE3C2B97D03CE470A592304C5EF21EED9F93DA56BB232D1EEB0035F9BF0DFAFDCC4606272B20A3
    Signed Output: 0x43CE9DB94B4577CEA49D350E7ACDC881861B7F6E7DAEA8AFEBA4AA6671735A2F7B788E0A
    B55F026AED3F0CC4A4FE66344E92D91F36F53DAA2648D8980B36362D

    Calling psa_verify_message()
    PASSED!

    Calling psa_sign_hash()
    Hash: 0xA41A41A12A799548211C410C65D8133AFDE34D28BDD542E4B680CF2899C8A8C4
    Signed Output: 0x8F530D7DD401377FD8A788DE57F730358CDA24A927389AA52ACA97AE55CEDBE3B2D0AD4F
    127736E7D80622787870FD92A83231AE3708AD426593C63E5877F5B1

    Calling psa_verify_hash()
    PASSED!

    Key persistence/location: [HSM Asset Store / HSM Asset Store]
    Calling psa_sign_message()
    Message: 0xE1130AF6A38CCB412A9C8D13E15DBFC9E69A16385AF3C3F1E5DA954FD5E7C45FD75E2B8C366992
    28E92840C0562FBF3772F07E17F1ADD56588DD45F7450E1217AD239922DD9C32695DC71FF2424CA0DEC1321AA
    47064A044B7FE3C2B97D03CE470A592304C5EF21EED9F93DA56BB232D1EEB0035F9BF0DFAFDCC4606272B20A3
    Signed Output: 0xD30CC043D596A269B30566156A1F57F23EF0128A7DC754CA823D103242A99016763CAB5C
    6B10A9F4F3989B8C2B848058735E1A6F897D17FC188389A91A7CAA40

    Calling psa_verify_message()
    PASSED!

    Calling psa_sign_hash()
    Hash: 0xA41A41A12A799548211C410C65D8133AFDE34D28BDD542E4B680CF2899C8A8C4
    Signed Output: 0x56CB4F77865035D2057C590E0E1D34A7C6EF6AE3FF7B8BCA241DF3F995A7EFBD7FBCB49D
    BD900DD72D56476F802967A894C10341DA3A82BCC97A338BAE635119

    Calling psa_verify_hash()
    PASSED!

    DONE!
```

## Application Design Details

This application uses two threads, `mainThread` and `signVerifyThread`, which
performs the following actions:

1. Opens the Display driver for UART output.

2. Initializes PSA Crypto API.

3. Imports a key with a valid lifetime.

4. Performs a sign operation on a message.

5. Performs a verification operation on the output to verify the signing.

6. Destroys the key.

7. Imports a key with a valid lifetime.

8. Performs a sign operation on a hash

9. Performs a verification operation on the output to verify the signing.

10. Destroys the key.

11. Repeats steps 3-10 for every valid key lifetime.

FreeRTOS:

* Please view the `FreeRTOSConfig.h` header file for example configuration
information.
