/*
 * Copyright (c) 2024-2025, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== psaRawKeyAgreement.c ========
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* POSIX Header files */
#include <pthread.h>

/* PSA Crypto header file */
#include <third_party/mbedtls/include/psa/crypto.h>

/* Driver Header files */
#include <ti/display/Display.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/cryptoutils/hsm/HSMXXF3.h>

#include <ti/devices/DeviceFamily.h>

/* Driver configuration */
#include "ti_drivers_config.h"

#define THREAD_STACK_SIZE 1536

/* Array of valid PSA key lifetimes */
static const psa_key_lifetime_t lifetimes[] = {
    PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(PSA_KEY_PERSISTENCE_VOLATILE, PSA_KEY_LOCATION_LOCAL_STORAGE),
    PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(PSA_KEY_PERSISTENCE_DEFAULT, PSA_KEY_LOCATION_LOCAL_STORAGE),
#if ((DeviceFamily_PARENT == DeviceFamily_PARENT_CC27XX) || (DeviceFamily_PARENT == DeviceFamily_PARENT_CC35XX))
    PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(PSA_KEY_PERSISTENCE_VOLATILE, PSA_KEY_LOCATION_HSM_ASSET_STORE),
    PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(PSA_KEY_PERSISTENCE_DEFAULT, PSA_KEY_LOCATION_HSM_ASSET_STORE),
    PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(PSA_KEY_PERSISTENCE_HSM_ASSET_STORE,
                                                   PSA_KEY_LOCATION_HSM_ASSET_STORE),
#endif
};

static const size_t lifetimeCnt = sizeof(lifetimes) / sizeof(psa_key_lifetime_t);

#define BITS_TO_BYTES(bits) (((bits) + 7u) / 8u)

#define MAX_CURVE_LENGTH_BYTES 66 /* P-521 */
#define MAX_PUB_KEY_BYTES      ((MAX_CURVE_LENGTH_BYTES * 2) + 1)

#if (DeviceFamily_PARENT == DeviceFamily_PARENT_CC35XX)
    /* For CC35XX device, HSM module cannot access Flash directly.
     * Need to place plaintext in Data RAM, where HSM's DMA can access.
     */
    #define _CONST
#else
    #define _CONST const
#endif

typedef struct
{
    uint8_t privateKey[MAX_CURVE_LENGTH_BYTES];
    uint8_t peerPublicKey[MAX_PUB_KEY_BYTES];
    uint8_t expectedSharedSecret[MAX_PUB_KEY_BYTES];
    psa_ecc_family_t curveFamily;
    size_t curveBits;
} keyAgreementTestVector;

static _CONST keyAgreementTestVector testVector = {
    /* P-256 Count = 0 from CAVS 14.1 ECC CDH Primitive (SP800-56A
     * Section 5.7.1.2) Test Information for "testecccdh".
     */
    .privateKey =
        {
            0x7D, 0x7D, 0xC5, 0xF7, 0x1E, 0xB2, 0x9D, 0xDA, 0xF8, 0x0D, 0x62, 0x14, 0x63, 0x2E, 0xEA, 0xE0,
            0x3D, 0x90, 0x58, 0xAF, 0x1F, 0xB6, 0xD2, 0x2E, 0xD8, 0x0B, 0xAD, 0xB6, 0x2B, 0xC1, 0xA5, 0x34,
        },
    .peerPublicKey =
        {
            /* clang-format off */
            0x04, /* Uncompressed point format prefix byte */
            /* X */
            0x70, 0x0C, 0x48, 0xF7, 0x7F, 0x56, 0x58, 0x4C, 0x5C, 0xC6, 0x32, 0xCA, 0x65, 0x64, 0x0D, 0xB9,
            0x1B, 0x6B, 0xAC, 0xCE, 0x3A, 0x4D, 0xF6, 0xB4, 0x2C, 0xE7, 0xCC, 0x83, 0x88, 0x33, 0xD2, 0x87,
            /* Y */
            0xDB, 0x71, 0xE5, 0x09, 0xE3, 0xFD, 0x9B, 0x06, 0x0D, 0xDB, 0x20, 0xBA, 0x5C, 0x51, 0xDC, 0xC5,
            0x94, 0x8D, 0x46, 0xFB, 0xF6, 0x40, 0xDF, 0xE0, 0x44, 0x17, 0x82, 0xCA, 0xB8, 0x5F, 0xA4, 0xAC,
            /* clang-format on */
        },
    .expectedSharedSecret =
        {
            /* raw encoded x-coordinate */
            0x46, 0xFC, 0x62, 0x10, 0x64, 0x20, 0xFF, 0x01, 0x2E, 0x54, 0xA4, 0x34, 0xFB, 0xDD, 0x2D, 0x25,
            0xCC, 0xC5, 0x85, 0x20, 0x60, 0x56, 0x1E, 0x68, 0x04, 0x0D, 0xD7, 0x77, 0x89, 0x97, 0xBD, 0x7B,
        },
    .curveFamily = PSA_ECC_FAMILY_SECP_R1,
    .curveBits   = 256,
};

#define MSG_BUFFER_SIZE 256
static char msgBuf[MSG_BUFFER_SIZE];

static Display_Handle display;

/*
 *  ======== printByteArray ========
 */
static void printByteArray(Display_Handle display, const char *desc, const uint8_t *array, size_t len)
{
    char *dest = &msgBuf[0];
    size_t i;

    strcpy(dest, desc);
    dest += strlen(desc);

    /* Format the message as printable hex */
    for (i = 0; i < len; i++)
    {
        sprintf(dest + (i * 2), "%02X", *(array + i));
    }

    Display_printf(display, 0U, 0U, msgBuf);
}

/*
 *  ======== printKeyLifetime ========
 */
static void printKeyLifetime(psa_key_lifetime_t lifetime)
{
    char *dest                        = &msgBuf[0];
    psa_key_persistence_t persistence = PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime);
    psa_key_location_t location       = PSA_KEY_LIFETIME_GET_LOCATION(lifetime);

    strcpy(dest, "Key persistence/location: ");
    dest = &msgBuf[0] + strlen(msgBuf);

    if (persistence == PSA_KEY_PERSISTENCE_VOLATILE)
    {
        strcpy(dest, "[Volatile / ");
    }
    else if (persistence == PSA_KEY_PERSISTENCE_DEFAULT)
    {
        strcpy(dest, "[Default / ");
    }
#if ((DeviceFamily_PARENT == DeviceFamily_PARENT_CC27XX) || (DeviceFamily_PARENT == DeviceFamily_PARENT_CC35XX))
    else if (persistence == PSA_KEY_PERSISTENCE_HSM_ASSET_STORE)
    {
        strcpy(dest, "[HSM Asset Store / ");
    }
#endif

    dest = &msgBuf[0] + strlen(msgBuf);

    if (location == PSA_KEY_LOCATION_LOCAL_STORAGE)
    {
        strcpy(dest, "Local Storage]");
    }
#if ((DeviceFamily_PARENT == DeviceFamily_PARENT_CC27XX) || (DeviceFamily_PARENT == DeviceFamily_PARENT_CC35XX))
    else if (location == PSA_KEY_LOCATION_HSM_ASSET_STORE)
    {
        strcpy(dest, "HSM Asset Store]");
    }
#endif

    Display_printf(display, 0U, 0U, msgBuf);
}

/*
 *  ======== keyAgreementThread ========
 */
static void *keyAgreementThread(void *arg0)
{
    psa_key_attributes_t attributes;
    psa_key_id_t privateKeyID;
    psa_key_lifetime_t lifetime;
    psa_status_t status;
    size_t expectedSharedSecretLength;
    size_t outputLength;
    size_t peerKeyLength;
    size_t privateKeyLength;
    size_t ssIndex;
    uint_fast8_t i;
    uint_fast8_t passCnt = 0U;
    uint8_t sharedSecret[MAX_CURVE_LENGTH_BYTES];

    peerKeyLength              = PSA_KEY_EXPORT_ECC_PUBLIC_KEY_MAX_SIZE(testVector.curveBits);
    privateKeyLength           = BITS_TO_BYTES(testVector.curveBits);
    expectedSharedSecretLength = BITS_TO_BYTES(testVector.curveBits);

    /* Print the key agreement inputs */
    printByteArray(display, "Private Key: 0x", testVector.privateKey, privateKeyLength);
    printByteArray(display, "Peer Public Key: 0x", testVector.peerPublicKey, peerKeyLength);
    Display_printf(display, 0U, 0U, "");

    /* Loop for all valid key lifetimes */
    for (i = 0U; i < lifetimeCnt; i++)
    {
        lifetime = lifetimes[i];

        /* Init private key attributes */
        attributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
        psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(testVector.curveFamily));
        psa_set_key_bits(&attributes, testVector.curveBits);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
        psa_set_key_lifetime(&attributes, lifetime);

        if (PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime) != PSA_KEY_PERSISTENCE_VOLATILE)
        {
            /* Set the key ID for non-volatile keys, Range:[PSA_KEY_ID_USER_MIN - PSA_KEY_ID_USER_MAX] */
            privateKeyID = PSA_KEY_ID_USER_MIN + i;
            psa_set_key_id(&attributes, privateKeyID);

            /* Attempt to delete the key. To ensure psa_import_key() works everytime. */
            status = psa_destroy_key(privateKeyID);

            if (status == PSA_SUCCESS)
            {
                Display_printf(display,
                               0U,
                               0U,
                               "Destroyed previously existing key with same ID. Status = %d\n",
                               status);
            }
            else if (status == PSA_ERROR_DOES_NOT_EXIST)
            {
                /* Expected case, no need to output anything */
            }
        }

        printKeyLifetime(lifetime);

        /* Import the private key */
        status = psa_import_key(&attributes, testVector.privateKey, privateKeyLength, &privateKeyID);

        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_import_key() failed. Status = %d\n", status);

            if (status == PSA_ERROR_ALREADY_EXISTS)
            {
                /* Attempt to delete the existing key. So, next run could be successful. */
                status = psa_destroy_key(privateKeyID);
                Display_printf(display, 0U, 0U, "Destroy Key: psa_destroy_key() called. Status = %d\n", status);
            }

            /* Skip to next key lifetime if key import fails */
            continue;
        }

        /* Retrieve the updated key attributes */
        status = psa_get_key_attributes(privateKeyID, &attributes);

        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_get_key_attributes() failed. Status = %d\n", status);
        }

        Display_printf(display, 0U, 0U, "Key ID: 0x%x", psa_get_key_id(&attributes));

        /* Zero the shared secret output buffer */
        (void)memset(&sharedSecret[0], 0, sizeof(sharedSecret));

        Display_printf(display, 0U, 0U, "Calling psa_raw_key_agreement()");

        /* Compute the shared secret using ECDH */
        status = psa_raw_key_agreement(PSA_ALG_ECDH,
                                       privateKeyID,
                                       testVector.peerPublicKey,
                                       peerKeyLength,
                                       sharedSecret,
                                       sizeof(sharedSecret),
                                       &outputLength);

        if (status == PSA_SUCCESS)
        {
            printByteArray(display, "Shared Secret: 0x", sharedSecret, outputLength);

            if (outputLength == expectedSharedSecretLength)
            {
                /* Verify shared secret output matches expected */
                for (ssIndex = 0; ssIndex < expectedSharedSecretLength; ssIndex++)
                {
                    if (sharedSecret[ssIndex] != testVector.expectedSharedSecret[ssIndex])
                    {
                        Display_printf(display,
                                       0U,
                                       0U,
                                       "Error: sharedSecret[%d] = 0x%02x does not match expected 0x%02x\n",
                                       ssIndex,
                                       sharedSecret[ssIndex],
                                       testVector.expectedSharedSecret[ssIndex]);

                        status = PSA_ERROR_GENERIC_ERROR;
                        break;
                    }
                }
            }
            else
            {
                /* Returned shared secret output length did not match expected */
                status = PSA_ERROR_GENERIC_ERROR;
            }

            if (status == PSA_SUCCESS)
            {
                passCnt++;
                Display_printf(display, 0U, 0U, "PASSED!\n");
            }
        }
        else
        {
            Display_printf(display, 0U, 0U, "Error: psa_raw_key_agreement() failed. Status = %d\n", status);
        }

        /* Destroy the key that was imported */
        status = psa_destroy_key(privateKeyID);

        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_destroy_key() failed. Status = %d\n", status);
        }
    }

    Display_printf(display, 0U, 0U, "DONE!\n");

    if (passCnt == lifetimeCnt)
    {
        /* Turn on LED1 to indicate key agreement with all key lifetimes passed */
        GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_ON);
    }

    return (NULL);
}

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    int retc;
    int_fast16_t ret;
    psa_status_t status;
    pthread_attr_t attrs;
    pthread_t thread0;
    struct sched_param priParam;

    /* Initialize display driver */
    Display_init();

    /* Open the display for output */
    display = Display_open(Display_Type_UART, NULL);
    if (display == NULL)
    {
        /* Failed to open display driver */
        while (1) {}
    }

    Display_printf(display, 0U, 0U, "\nStarting the PSA Crypto Raw Key Agreement example.\n");

    /* Initialize PSA Crypto */
    status = psa_crypto_init();
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "Error: psa_crypto_init() failed. Status = %d\n", status);
        while (1) {}
    }

    Display_printf(display, 0U, 0U, "Provisioning Hardware Unique Key (HUK)...\n");

    /* Provision the HW Unique Key needed to store key blobs */
    ret = HSMXXF3_provisionHUK();
    if (ret != HSMXXF3_STATUS_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "Error: HSMXXF3_provisionHUK() failed. Status = %d\n", ret);
        while (1) {}
    }

    /* Turn on LED0 to indicate successful initialization */
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);

    /* Set priority and stack size attributes */
    priParam.sched_priority = 1;

    retc = pthread_attr_init(&attrs);
    retc |= pthread_attr_setschedparam(&attrs, &priParam);
    retc |= pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    retc |= pthread_attr_setstacksize(&attrs, THREAD_STACK_SIZE);
    if (retc != 0)
    {
        /* Failed to set thread attributes */
        while (1) {}
    }

    /* Create encrypt thread */
    retc = pthread_create(&thread0, &attrs, keyAgreementThread, NULL);
    if (retc != 0)
    {
        /* pthread_create() failed */
        while (1) {}
    }

    return (NULL);
}
