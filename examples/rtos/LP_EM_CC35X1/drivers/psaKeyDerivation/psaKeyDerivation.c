/*
 * Copyright (c) 2025, Texas Instruments Incorporated
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
 *  ======== psaKeyDerivation.c ========
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

#define THREAD_STACK_SIZE 4096

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

#define KEY_DERIVATION_LOOP_CNT 3

static const uint8_t context[] = "ThisIsAContextOfSufficientLength";
static const uint8_t label[]   = "ThisIsALabelOfSufficientLengthToSatisfyTheHSMRequirement";

#define MSG_BUFFER_SIZE 128

static char msgBuf[MSG_BUFFER_SIZE];
static Display_Handle display;

#define BASE_KEY_SIZE    16 /* bytes */
#define DERIVED_KEY_SIZE 32 /* bytes */
#define PLAINTEXT_LENGTH 16 /* Must be a multiple of block size for AES-ECB */

static uint8_t baseKey[BASE_KEY_SIZE] =
    {0xf9, 0xfd, 0xca, 0x4a, 0xc6, 0x4f, 0xe7, 0xf0, 0x14, 0xde, 0x0f, 0x43, 0x03, 0x9c, 0x75, 0x71};

static uint8_t plaintext[PLAINTEXT_LENGTH] =
    {0xa2, 0x65, 0x48, 0x0c, 0xa8, 0x8d, 0x5f, 0x53, 0x6d, 0xb0, 0xdc, 0x6a, 0xbc, 0x40, 0xfa, 0xf0};

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

    strcpy(dest, "Base Key persistence/location: ");
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
 *  ======== setupKeyDerivation ========
 */
static psa_status_t setupKeyDerivation(psa_key_derivation_operation_t *derivation,
                                       psa_algorithm_t alg,
                                       psa_key_id_t baseKeyID)
{
    psa_status_t status;
    size_t capacity;

    /* Initialize the key derivation operation */
    *derivation = psa_key_derivation_operation_init();

    /* Set up the key derivation operation */
    status = psa_key_derivation_setup(derivation, alg);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "Error: psa_key_derivation_setup() failed. Status = %d\n", status);
        return status;
    }

    /* Provide the input key for derivation */
    status = psa_key_derivation_input_key(derivation, PSA_KEY_DERIVATION_INPUT_SECRET, baseKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "Error: psa_key_derivation_input_key() failed. Status = %d\n", status);
        return status;
    }

    /* Set input label */
    status = psa_key_derivation_input_bytes(derivation, PSA_KEY_DERIVATION_INPUT_LABEL, label, sizeof(label));
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "Error: psa_key_derivation_input_bytes() failed. Status = %d\n", status);
        return status;
    }

    /* Set input context. When deriving from the HUK or from the TKDK, the HSM
     * automatically appends a context. Therefore, we should not add the context
     * provided by the application.
     */
    if ((baseKeyID != PSA_KEY_ID_HSM_HUK) && (baseKeyID != PSA_KEY_ID_HSM_TKDK))
    {
        status = psa_key_derivation_input_bytes(derivation, PSA_KEY_DERIVATION_INPUT_CONTEXT, context, sizeof(context));
        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_key_derivation_input_bytes() failed. Status = %d\n", status);
            return status;
        }
    }

    status = psa_key_derivation_get_capacity(derivation, &capacity);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "Error: psa_key_derivation_get_capacity() failed. Status = %d\n", status);
    }
    else
    {
        Display_printf(display, 0U, 0U, "Initial capacity is 0x%x bytes", capacity);
    }

    return status;
}

/*
 *  ======== derivationThread ========
 */
static void *derivationThread(void *arg0)
{
    psa_key_attributes_t attributes;
    psa_key_attributes_t derivedKeyAttributes;
    psa_key_derivation_operation_t derivation;
    psa_key_id_t baseKeyID;
    psa_key_id_t derivedKeyID;
    psa_key_lifetime_t lifetime;
    psa_status_t status;
    size_t capacity;
    size_t decryptOutputLength;
    size_t encryptOutputLength;
    size_t expectedCapacity;
    uint_fast8_t i;
    uint_fast8_t idx;
    uint_fast8_t j;
    uint_fast8_t passCnt = 0U;
    uint8_t ciphertext[PLAINTEXT_LENGTH];
    uint8_t decryptedPlaintext[PLAINTEXT_LENGTH];

    /* Print the encryption inputs */
    printByteArray(display, "Plaintext: 0x", plaintext, sizeof(plaintext));
    Display_printf(display, 0U, 0U, "");

    /* Loop for all valid key lifetimes */
    for (i = 0U; i < lifetimeCnt; i++)
    {
        lifetime = lifetimes[i];

        /* Init key attributes */
        attributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_algorithm(&attributes, PSA_ALG_SP800_108_COUNTER_CMAC);
        psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
        psa_set_key_bits(&attributes, PSA_BYTES_TO_BITS(sizeof(baseKey)));
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
        psa_set_key_lifetime(&attributes, lifetime);

        if (PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime) != PSA_KEY_PERSISTENCE_VOLATILE)
        {
            /* Set the base key ID for non-volatile keys, Range:[PSA_KEY_ID_USER_MIN - (PSA_KEY_ID_USER_MAX-2)] */
            baseKeyID = PSA_KEY_ID_USER_MIN + i;
            psa_set_key_id(&attributes, baseKeyID);

            /* Attempt to delete the key to ensure psa_import_key() works
             * everytime. Ignore return value as it may fail if the key does not
             * exist.
             */
            (void)psa_destroy_key(baseKeyID);
        }

        printKeyLifetime(lifetime);

        /* Import the base key to be used for derivation */
        status = psa_import_key(&attributes, baseKey, sizeof(baseKey), &baseKeyID);

        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_import_key() failed. Status = %d\n", status);

            /* Skip to next key lifetime if key import fails */
            continue;
        }

        /* Retrieve the updated key attributes */
        status = psa_get_key_attributes(baseKeyID, &attributes);

        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_get_key_attributes() failed. Status = %d\n", status);
        }

        Display_printf(display, 0U, 0U, "Base Key ID: 0x%x", psa_get_key_id(&attributes));

        Display_printf(display, 0U, 0U, "\nStarting key derivation...\n");

        status = setupKeyDerivation(&derivation, PSA_ALG_SP800_108_COUNTER_CMAC, baseKeyID);

        if (status != PSA_SUCCESS)
        {
            return (NULL);
        }

        /* Set a limit on the amount of data that can be output from the key
         * derivation operation. This step is optional but done to demonstrate
         * usage of the API.
         */
        expectedCapacity = KEY_DERIVATION_LOOP_CNT * DERIVED_KEY_SIZE;
        Display_printf(display, 0U, 0U, "Setting capacity to %d bytes\n", expectedCapacity);
        status = psa_key_derivation_set_capacity(&derivation, expectedCapacity);

        /* Read capacity to verify it was set */
        status = psa_key_derivation_get_capacity(&derivation, &capacity);
        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_key_derivation_get_capacity() failed. Status = %d\n", status);
            return (NULL);
        }

        if (capacity != expectedCapacity)
        {
            Display_printf(display, 0U, 0U, "Error: Capacity is not set to expected value!\n");
            return (NULL);
        }

        /* Setup new key attributes */
        derivedKeyAttributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_algorithm(&derivedKeyAttributes, PSA_ALG_ECB_NO_PADDING);
        psa_set_key_bits(&derivedKeyAttributes, PSA_BYTES_TO_BITS(DERIVED_KEY_SIZE));
        psa_set_key_type(&derivedKeyAttributes, PSA_KEY_TYPE_AES);
        psa_set_key_usage_flags(&derivedKeyAttributes, (PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT));
        /* Derived keys can only be produced with Asset Store location and Asset Store persistence */
        psa_set_key_lifetime(&derivedKeyAttributes,
                             PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(PSA_KEY_PERSISTENCE_HSM_ASSET_STORE,
                                                                            PSA_KEY_LOCATION_HSM_ASSET_STORE));
        for (j = 0U; j < KEY_DERIVATION_LOOP_CNT; j++)
        {
            /* Set Key ID for derived key */
            derivedKeyID = PSA_KEY_ID_USER_MIN + (10 * (i + 1)) + j;
            psa_set_key_id(&derivedKeyAttributes, derivedKeyID);

            /* Perform the key derivation to output HSM asset */
            status = psa_key_derivation_output_key(&derivedKeyAttributes, &derivation, &derivedKeyID);
            if (status != PSA_SUCCESS)
            {
                Display_printf(display, 0U, 0U, "Error: psa_key_derivation_output_key() failed. Status = %d\n", status);
                return (NULL);
            }

            expectedCapacity -= DERIVED_KEY_SIZE;

            /* Retrieve the updated key attributes */
            status = psa_get_key_attributes(derivedKeyID, &derivedKeyAttributes);

            if (status != PSA_SUCCESS)
            {
                Display_printf(display, 0U, 0U, "Error: psa_get_key_attributes() failed. Status = %d\n", status);
                return (NULL);
            }

            Display_printf(display, 0U, 0U, "Derived Key ID: %d", psa_get_key_id(&derivedKeyAttributes));

            /* Verify the capacity was reduced by the correct amount */
            status = psa_key_derivation_get_capacity(&derivation, &capacity);
            if (status == PSA_SUCCESS)
            {
                Display_printf(display, 0U, 0U, "Remaining capacity: %d bytes", capacity);
                if (capacity != expectedCapacity)
                {
                    Display_printf(display, 0U, 0U, "Error: Remaining capacity is not set to expected value!\n");
                    return (NULL);
                }
            }

            /* Zero the ciphertext output buffer */
            (void)memset(&ciphertext[0], 0, sizeof(ciphertext));

            Display_printf(display, 0U, 0U, "Calling psa_cipher_encrypt()");

            /* Encrypt plaintext using derived key */
            status = psa_cipher_encrypt(derivedKeyID,
                                        PSA_ALG_ECB_NO_PADDING,
                                        plaintext,
                                        sizeof(plaintext),
                                        ciphertext,
                                        sizeof(ciphertext),
                                        &encryptOutputLength);

            if (status == PSA_SUCCESS)
            {
                printByteArray(display, "Ciphertext: 0x", ciphertext, encryptOutputLength);
            }
            else
            {
                Display_printf(display, 0U, 0U, "Error: psa_cipher_encrypt() failed. Status = %d\n", status);
            }

            if (encryptOutputLength != sizeof(plaintext))
            {
                Display_printf(display, 0U, 0U, "Error: Output length does not match expected ciphertext length!\n");
                return (NULL);
            }

            /* Zero the output buffer */
            (void)memset(&decryptedPlaintext[0], 0, sizeof(decryptedPlaintext));

            Display_printf(display, 0U, 0U, "Calling psa_cipher_decrypt()");

            /* Decrypt ciphertext using derived key */
            status = psa_cipher_decrypt(derivedKeyID,
                                        PSA_ALG_ECB_NO_PADDING,
                                        ciphertext,
                                        encryptOutputLength,
                                        decryptedPlaintext,
                                        sizeof(decryptedPlaintext),
                                        &decryptOutputLength);

            if (status == PSA_SUCCESS)
            {
                printByteArray(display, "Decrypted Plaintext: 0x", decryptedPlaintext, decryptOutputLength);

                /* Verify the decrypted plaintext matches the original plaintext */
                for (idx = 0U; idx < sizeof(plaintext); idx++)
                {
                    if (decryptedPlaintext[idx] != plaintext[idx])
                    {
                        Display_printf(display,
                                       0U,
                                       0U,
                                       "Error: decryptedPlaintext[%d] = 0x%02x does not match expected 0x%02x\n",
                                       idx,
                                       decryptedPlaintext[idx],
                                       plaintext[idx]);
                        status = PSA_ERROR_GENERIC_ERROR;
                        break;
                    }

                    if (idx == (sizeof(plaintext) - 1U))
                    {
                        Display_printf(display, 0U, 0U, "PASSED!\n");
                        passCnt++;
                    }
                }
            }
            else
            {
                Display_printf(display, 0U, 0U, "Error: psa_cipher_decrypt() failed. Status = %d\n", status);
            }

            /* Destroy the derived key as there are only a limited number of HSM Assets available */
            status = psa_destroy_key(derivedKeyID);
        }

        /* Destroy the base key */
        status = psa_destroy_key(baseKeyID);

        if (status != PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "Error: psa_destroy_key() failed. Status = %d\n", status);
        }
    }

    if (passCnt == (lifetimeCnt * KEY_DERIVATION_LOOP_CNT))
    {
        Display_printf(display, 0U, 0U, "DONE!\n");
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

    Display_printf(display, 0U, 0U, "\nStarting the PSA Crypto Key Derivation example.\n");

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
    retc = pthread_create(&thread0, &attrs, derivationThread, NULL);
    if (retc != 0)
    {
        /* pthread_create() failed */
        while (1) {}
    }

    return (NULL);
}
