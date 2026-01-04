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
 *  ======== psaSignVerify.c ========
 *  This example uses the PSA Crypto API to sign and verify ECDSA signatures
 *  using a test vector from NIST example.
 *
 *  The example also demonstrates how to provision HUK from HSM engine.
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

#define THREAD_STACK_SIZE 3072
#define PRIVATE_KEY_SIZE  32
#define PUBLIC_KEY_SIZE   65
#define MAX_MESSAGE_SIZE  256
#define SIGNATURE_SIZE    133
#define HASH_SIZE         32
#define MSG_BUFFER_SIZE   272

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

typedef struct
{
    /* http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/ECDSA_Prime.pdf */
    uint8_t privateKey[PRIVATE_KEY_SIZE];
    uint8_t publicKey[PUBLIC_KEY_SIZE];
    size_t privateKeyLength;
    size_t publicKeyLength;
    uint8_t message[MAX_MESSAGE_SIZE];
    size_t messageLength;
    uint8_t hash[HASH_SIZE];
    uint8_t signature[SIGNATURE_SIZE];
    psa_ecc_family_t curveFamily;
    size_t keyBits;
    psa_algorithm_t alg;
} ecdsaTestCase;

static ecdsaTestCase signTestVectors[] = {
    {
        /* http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/ECDSA_Prime.pdf */
        .privateKey = {0xC4, 0x77, 0xF9, 0xF6, 0x5C, 0x22, 0xCC, 0xE2, 0x06, 0x57, 0xFA, 0xA5, 0xB2, 0xD1, 0xD8, 0x12,
                       0x23, 0x36, 0xF8, 0x51, 0xA5, 0x08, 0xA1, 0xED, 0x04, 0xE4, 0x79, 0xC3, 0x49, 0x85, 0xBF, 0x96},
        .privateKeyLength = 32,
        .publicKey =
            {
                /* clang-format off */
                0x04,
                /* X */
                0xB7, 0xE0, 0x8A, 0xFD, 0xFE, 0x94, 0xBA, 0xD3, 0xF1, 0xDC, 0x8C, 0x73, 0x47, 0x98, 0xBA, 0x1C,
                0x62, 0xB3, 0xA0, 0xAD, 0x1E, 0x9E, 0xA2, 0xA3, 0x82, 0x01, 0xCD, 0x08, 0x89, 0xBC, 0x7A, 0x19,
                /* Y */
                0x36, 0x03, 0xF7, 0x47, 0x95, 0x9D, 0xBF, 0x7A, 0x4B, 0xB2, 0x26, 0xE4, 0x19, 0x28, 0x72, 0x90,
                0x63, 0xAD, 0xC7, 0xAE, 0x43, 0x52, 0x9E, 0x61, 0xB5, 0x63, 0xBB, 0xC6, 0x06, 0xCC, 0x5E, 0x09
                /* clang-format on */
            },
        .publicKeyLength = 65,
        /* Arbitrary message (not from NIST example) */
        .message = {0xe1, 0x13, 0x0a, 0xf6, 0xa3, 0x8c, 0xcb, 0x41, 0x2a, 0x9c, 0x8d, 0x13, 0xe1, 0x5d, 0xbf, 0xc9,
                    0xe6, 0x9a, 0x16, 0x38, 0x5a, 0xf3, 0xc3, 0xf1, 0xe5, 0xda, 0x95, 0x4f, 0xd5, 0xe7, 0xc4, 0x5f,
                    0xd7, 0x5e, 0x2b, 0x8c, 0x36, 0x69, 0x92, 0x28, 0xe9, 0x28, 0x40, 0xc0, 0x56, 0x2f, 0xbf, 0x37,
                    0x72, 0xf0, 0x7e, 0x17, 0xf1, 0xad, 0xd5, 0x65, 0x88, 0xdd, 0x45, 0xf7, 0x45, 0x0e, 0x12, 0x17,
                    0xad, 0x23, 0x99, 0x22, 0xdd, 0x9c, 0x32, 0x69, 0x5d, 0xc7, 0x1f, 0xf2, 0x42, 0x4c, 0xa0, 0xde,
                    0xc1, 0x32, 0x1a, 0xa4, 0x70, 0x64, 0xa0, 0x44, 0xb7, 0xfe, 0x3c, 0x2b, 0x97, 0xd0, 0x3c, 0xe4,
                    0x70, 0xa5, 0x92, 0x30, 0x4c, 0x5e, 0xf2, 0x1e, 0xed, 0x9f, 0x93, 0xda, 0x56, 0xbb, 0x23, 0x2d,
                    0x1e, 0xeb, 0x00, 0x35, 0xf9, 0xbf, 0x0d, 0xfa, 0xfd, 0xcc, 0x46, 0x06, 0x27, 0x2b, 0x20, 0xa3},
        .messageLength = 128,
        /* This is a random hash used to test hash signing. It is not a hash of the above message */
        .hash        = {0xA4, 0x1A, 0x41, 0xA1, 0x2A, 0x79, 0x95, 0x48, 0x21, 0x1C, 0x41, 0x0C, 0x65, 0xD8, 0x13, 0x3A,
                        0xFD, 0xE3, 0x4D, 0x28, 0xBD, 0xD5, 0x42, 0xE4, 0xB6, 0x80, 0xCF, 0x28, 0x99, 0xC8, 0xA8, 0xC4},
        .signature   = {0},
        .curveFamily = PSA_ECC_FAMILY_SECP_R1,
        .keyBits     = 256,
        .alg         = PSA_ALG_ECDSA(PSA_ALG_SHA_256),
    },
};

enum keyType
{
    KEYPAIR,
    PUBLIC,
};

uint8_t signTestVectorCount = sizeof(signTestVectors) / sizeof(ecdsaTestCase);
uint8_t sign_output[SIGNATURE_SIZE];
int_fast8_t passCnt = 0U;

#define EXPECTED_PASS_CNT ((lifetimeCnt * signTestVectorCount) * 2)

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
    for (i = 0U; i < len; i++)
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
 *  ======== setupKeyID ========
 *  Set up the key ID for the key attributes. If the persistence is
 *  volatile, we don't need to set a key ID.
 */
static void setupKeyID(psa_key_id_t *keyID, psa_key_attributes_t *attributes, uint32_t persistence)
{
    if (persistence != PSA_KEY_PERSISTENCE_VOLATILE)
    {
        *keyID = PSA_KEY_ID_USER_MIN;
        psa_set_key_id(attributes, *keyID);

        /* Precaution to destroy any previous key with same Key IDs, only for testing */
        (void)psa_destroy_key(*keyID);
    }
}

/*
 *  ======== setKeyAttributes ========
 *  Set up the key attributes for the key to be imported. This includes
 *  setting the algorithm, key bits, key type, usage flags and lifetime.
 */
static void setKeyAttributes(uint8_t i,
                             ecdsaTestCase testVectors[],
                             psa_key_usage_t usage,
                             enum keyType type,
                             psa_key_lifetime_t lifetime,
                             psa_key_attributes_t *attributes)
{
    /* Init key attributes */
    psa_algorithm_t alg = testVectors[i].alg;
    size_t keyBits      = testVectors[i].keyBits;
    size_t keyType      = (type == KEYPAIR) ? PSA_KEY_TYPE_ECC_KEY_PAIR(testVectors[i].curveFamily)
                                            : PSA_KEY_TYPE_ECC_PUBLIC_KEY(testVectors[i].curveFamily);

    if (testVectors[i].alg != PSA_ALG_PURE_EDDSA)
    {
        alg |= PSA_ALG_HASH_MASK; /* Allow any hash type for all except EDDSA */
    }

    psa_set_key_algorithm(attributes, alg);
    psa_set_key_bits(attributes, keyBits);
    psa_set_key_type(attributes, keyType);
    psa_set_key_usage_flags(attributes, usage);
    psa_set_key_lifetime(attributes, lifetime);
}

/*
 *  ======== verifyMessage ========
 *  Verify the signature of a message using the public key imported from the
 *  test vectors. The function imports the public key, verifies the signature
 *  and then destroys the key.
 */
static void verifyMessage(uint8_t i,
                          ecdsaTestCase testVectors[],
                          uint8_t *signature,
                          size_t signature_length,
                          psa_key_lifetime_t lifetime,
                          psa_status_t expectedStatus)
{
    psa_status_t status;
    psa_status_t verifyStatus;
    /* Init key attributes */
    psa_key_id_t publicKeyID;
    psa_key_attributes_t publicKeyAttributes = PSA_KEY_ATTRIBUTES_INIT;
    size_t publicKeyBytes;

    setKeyAttributes(i, testVectors, PSA_KEY_USAGE_VERIFY_MESSAGE, PUBLIC, lifetime, &publicKeyAttributes);

    setupKeyID(&publicKeyID, &publicKeyAttributes, PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime));

    /* Import the public key */
    publicKeyBytes = PSA_EXPORT_KEY_OUTPUT_SIZE(psa_get_key_type(&publicKeyAttributes), testVectors[i].keyBits);
    status         = psa_import_key(&publicKeyAttributes, testVectors[i].publicKey, publicKeyBytes, &publicKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "verifyMessage: psa_import_key failed! Status = %d\n", status);
        return;
    }

    Display_printf(display, 0U, 0U, "Calling psa_verify_message()");

    verifyStatus = psa_verify_message(publicKeyID,
                                      testVectors[i].alg,
                                      testVectors[i].message,
                                      testVectors[i].messageLength,
                                      signature,
                                      signature_length);
    if (verifyStatus != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "verifyMessage: psa_verify_message failed! Status = %d\n", status);
        passCnt--;
    }

    status = psa_destroy_key(publicKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "verifyMessage: psa_destroy_key failed. Status = %d\n", status);
        passCnt--;
    }
    else
    {
        /* Print passed if both verify message and destroy key are successful */
        if (verifyStatus == PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "PASSED!\n");
        }
    }
}

/*
 *  ======== verifyHash ========
 *  Verify the signature of a hash using the public key imported from the
 *  test vectors. The function imports the public key, verifies the signature
 *  and then destroys the key.
 */
static void verifyHash(uint8_t i,
                       ecdsaTestCase testVectors[],
                       uint8_t *signature,
                       size_t signature_length,
                       psa_key_lifetime_t lifetime,
                       psa_status_t expectedStatus)
{
    psa_status_t status;
    psa_status_t verifyStatus;
    psa_key_id_t publicKeyID;
    psa_algorithm_t alg                      = testVectors[i].alg;
    psa_key_attributes_t publicKeyAttributes = PSA_KEY_ATTRIBUTES_INIT;
    size_t publicKeyBytes;

    setKeyAttributes(i, testVectors, PSA_KEY_USAGE_VERIFY_HASH, PUBLIC, lifetime, &publicKeyAttributes);

    setupKeyID(&publicKeyID, &publicKeyAttributes, PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime));

    psa_get_key_type(&publicKeyAttributes);

    /* Import the public key */
    publicKeyBytes = PSA_EXPORT_KEY_OUTPUT_SIZE(psa_get_key_type(&publicKeyAttributes), testVectors[i].keyBits);

    status = psa_import_key(&publicKeyAttributes, testVectors[i].publicKey, publicKeyBytes, &publicKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "verifyHash: psa_import_key failed! Status = %d\n", status);
        return;
    }

    Display_printf(display, 0U, 0U, "Calling psa_verify_hash()");

    verifyStatus = psa_verify_hash(publicKeyID,
                                   alg,
                                   testVectors[i].hash,
                                   PSA_HASH_LENGTH(PSA_ALG_SIGN_GET_HASH(alg)),
                                   signature,
                                   signature_length);

    if (verifyStatus != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "verifyHash: psa_verify_hash failed! Status = %d\n", status);
        passCnt--;
    }

    status = psa_destroy_key(publicKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "verifyHash: psa_destroy_key failed! Status = %d\n", status);
        passCnt--;
    }
    else
    {
        /* Print passed if both verify hash and destroy key are successful */
        if (verifyStatus == PSA_SUCCESS)
        {
            Display_printf(display, 0U, 0U, "PASSED!\n");
        }
    }
}

/*
 *  ======== signMessage ========
 *  Test the psa_sign_message() API using the test vectors provided in the
 *  test vectors table above. The function imports the private key, signs the
 *  message and then verifies the signature using the public key imported from
 *  the test vectors.
 */
static void signMessage(uint32_t in_persistence, uint32_t in_location, uint8_t vectorIndex)
{
    psa_algorithm_t alg;
    psa_key_attributes_t privateKeyAttributes;
    psa_key_id_t privateKeyID;
    psa_key_lifetime_t lifetime = PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(in_persistence, in_location);
    psa_key_usage_t keyUsage    = PSA_KEY_USAGE_SIGN_MESSAGE;
    psa_status_t status;
    psa_status_t signStatus;
    size_t keyBits;
    size_t localPublicKeyLength;
    size_t outputLength;
    uint8_t localPublicKey[SIGNATURE_SIZE];

    /* Init key attributes */
    alg                  = signTestVectors[vectorIndex].alg;
    keyBits              = signTestVectors[vectorIndex].keyBits;
    privateKeyAttributes = PSA_KEY_ATTRIBUTES_INIT;

    if (alg == PSA_ALG_PURE_EDDSA)
    {
        keyUsage |= PSA_KEY_USAGE_DERIVE;
    }

    setKeyAttributes(vectorIndex, signTestVectors, keyUsage, KEYPAIR, lifetime, &privateKeyAttributes);

    setupKeyID(&privateKeyID, &privateKeyAttributes, PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime));

    /* Import the private key */
    status = psa_import_key(&privateKeyAttributes,
                            signTestVectors[vectorIndex].privateKey,
                            PSA_BITS_TO_BYTES(keyBits),
                            &privateKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "signMessage: Failed to import key. Status = %d\n", status);
        return;
    }

    /* Use the psa_export_public_key() API to generate and return public key material */
    localPublicKeyLength = sizeof(localPublicKey);
    status               = psa_export_public_key(privateKeyID, localPublicKey, localPublicKeyLength, &outputLength);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "signMessage: psa_export_public_key failed. Status = %d\n", status);
        return;
    }

    Display_printf(display, 0U, 0U, "Calling psa_sign_message()");

    signStatus = psa_sign_message(privateKeyID,
                                  alg,
                                  signTestVectors[vectorIndex].message,
                                  signTestVectors[vectorIndex].messageLength,
                                  sign_output,
                                  sizeof(sign_output),
                                  &outputLength);
    if (signStatus != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "signMessage: psa_sign_message failed. Status = %d\n", status);
    }
    else
    {
        printByteArray(display,
                       "Message: 0x",
                       signTestVectors[vectorIndex].message,
                       signTestVectors[vectorIndex].messageLength);
        printByteArray(display, "Signed Output: 0x", sign_output, outputLength);
        passCnt++;
    }

    status = psa_destroy_key(privateKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "signMessage: psa_destroy_key failed. Status = %d\n", status);
        passCnt--;
    }

    if (signStatus == PSA_SUCCESS)
    {
        verifyMessage(vectorIndex, signTestVectors, sign_output, outputLength, lifetime, PSA_SUCCESS);
    }
}

/*
 *  ======== signHash ========
 *  Test the psa_sign_hash() API using the test vectors provided in the
 *  test vectors table above. The function imports the private key, signs the
 *  hash and then verifies the signature using the public key imported from
 *  the test vectors.
 */
static void signHash(uint32_t in_persistence, uint32_t in_location, uint8_t vectorIndex)
{
    psa_algorithm_t alg;
    psa_key_attributes_t privateKeyAttributes;
    psa_key_id_t privateKeyID;
    psa_key_lifetime_t lifetime = PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(in_persistence, in_location);
    psa_status_t status;
    psa_status_t signStatus;
    size_t keyBits;
    size_t outputLength;
    size_t hashLength;

    /* psa_sign_hash and psa_verify_hash do not support EdDSA */
    if (signTestVectors[vectorIndex].alg == PSA_ALG_PURE_EDDSA)
    {
        return;
    }

    /* Init key attributes */
    alg                  = signTestVectors[vectorIndex].alg;
    keyBits              = signTestVectors[vectorIndex].keyBits;
    privateKeyAttributes = PSA_KEY_ATTRIBUTES_INIT;

    setKeyAttributes(vectorIndex, signTestVectors, PSA_KEY_USAGE_SIGN_HASH, KEYPAIR, lifetime, &privateKeyAttributes);

    setupKeyID(&privateKeyID, &privateKeyAttributes, PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime));

    /* Import the private key */
    status = psa_import_key(&privateKeyAttributes,
                            signTestVectors[vectorIndex].privateKey,
                            PSA_BITS_TO_BYTES(keyBits),
                            &privateKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "signHash: Failed to import key. Status = %d\n", status);
        return;
    }

    hashLength = PSA_HASH_LENGTH(PSA_ALG_SIGN_GET_HASH(alg));

    Display_printf(display, 0U, 0U, "Calling psa_sign_hash()");

    signStatus = psa_sign_hash(privateKeyID,
                               alg,
                               signTestVectors[vectorIndex].hash,
                               hashLength,
                               sign_output,
                               sizeof(sign_output),
                               &outputLength);
    if (signStatus != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "signHash: psa_sign_hash failed Status = %d\n", status);
    }
    else
    {
        printByteArray(display, "Hash: 0x", signTestVectors[vectorIndex].hash, hashLength);
        printByteArray(display, "Signed Output: 0x", sign_output, outputLength);
        passCnt++;
    }

    status = psa_destroy_key(privateKeyID);
    if (status != PSA_SUCCESS)
    {
        Display_printf(display, 0U, 0U, "signHash: psa_destroy_key failed. Status = %d\n", status);
        passCnt--;
    }

    if (signStatus == PSA_SUCCESS)
    {
        verifyHash(vectorIndex, signTestVectors, sign_output, outputLength, lifetime, PSA_SUCCESS);
    }
}

/*
 * ========= signVerifyThread ========
 * This thread will sign and verify messages and hashes using the test vector
 * provided in the array above. It will use different key lifetimes
 * for each test.
 */
static void *signVerifyThread(void *arg0)
{
    psa_key_lifetime_t lifetime;
    uint_fast8_t i;
    uint_fast8_t j;

    for (j = 0U; j < signTestVectorCount; j++)
    {
        /* Print the encryption inputs */
        printByteArray(display, "Private Key: 0x", signTestVectors[j].privateKey, signTestVectors[j].privateKeyLength);
        printByteArray(display, "Public Key: 0x", signTestVectors[j].publicKey, signTestVectors[j].publicKeyLength);
        Display_printf(display, 0U, 0U, "");

        /* Sign & Verify Message with different key lifetimes.
         * Sign & Verify Hash with different key lifetimes.
         */
        for (i = 0U; i < lifetimeCnt; i++)
        {
            lifetime = lifetimes[i];
            printKeyLifetime(lifetime);

            signMessage(PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime), PSA_KEY_LIFETIME_GET_LOCATION(lifetime), j);
            signHash(PSA_KEY_LIFETIME_GET_PERSISTENCE(lifetime), PSA_KEY_LIFETIME_GET_LOCATION(lifetime), j);
        }
    }

    if (passCnt == EXPECTED_PASS_CNT)
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

    Display_printf(display, 0U, 0U, "\nStarting the PSA Crypto Sign & Verify example.\n");

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
    retc = pthread_create(&thread0, &attrs, signVerifyThread, NULL);
    if (retc != 0)
    {
        /* pthread_create() failed */
        while (1) {}
    }

    return (NULL);
}
