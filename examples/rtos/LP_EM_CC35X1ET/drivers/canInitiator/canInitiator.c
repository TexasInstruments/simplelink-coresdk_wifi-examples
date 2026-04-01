/*
 * Copyright (c) 2024-2026, Texas Instruments Incorporated
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
 *  ======== canInitiator.c ========
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* POSIX Header files */
#include <pthread.h>
#include <semaphore.h>

/* Driver Header files */
#include <ti/drivers/CAN.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/UART2.h>
#include <ti/drivers/apps/Button.h>

/* Driver configuration */
#include "ti_drivers_config.h"

#define THREAD_STACK_SIZE 1024

/* Defines */
#define MAX_MSG_LENGTH 512
#define DLC_TABLE_SIZE 16

#define CAN_EVENT_MASK                                                                                               \
    (CAN_EVENT_RX_DATA_AVAIL | CAN_EVENT_TX_FINISHED | CAN_EVENT_BUS_ON | CAN_EVENT_BUS_OFF | CAN_EVENT_ERR_ACTIVE | \
     CAN_EVENT_ERR_PASSIVE | CAN_EVENT_RX_FIFO_MSG_LOST | CAN_EVENT_RX_RING_BUFFER_FULL |                            \
     CAN_EVENT_BIT_ERR_UNCORRECTED | CAN_EVENT_SPI_XFER_ERROR)

/* Payload bytes indexed by Data Length Code (DLC) field. */
static const uint32_t dlcToDataSize[DLC_TABLE_SIZE] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

/* The following globals are not designated as 'static' to allow CCS IDE access */

/* CAN handle */
CAN_Handle canHandle;

/* UART2 handle */
UART2_Handle uart2Handle;

/* Formatted message buffer */
char formattedMsg[MAX_MSG_LENGTH];

/* Rx and Tx buffer elements */
CAN_RxBufElement rxElem;
CAN_TxBufElement txElem;

/* Received msg count */
uint32_t rxMsgCnt = 0U;

/* Event callback count */
volatile uint32_t rxEventCnt = 0U;
volatile uint32_t txEventCnt = 0U;

/* Current event */
volatile uint32_t curEvent;

/* Current event data */
volatile uint32_t curEventData;

/* CAN event semaphore */
sem_t eventSem;

/* Receive message semaphore */
sem_t rxSem;

/* Button press semaphore */
sem_t buttonSem;

/* Button driver parameters. */
Button_Params button0Params;
Button_Params button1Params;

/* Button driver handles. */
Button_Handle button0Handle;
Button_Handle button1Handle;

/* Flag to Tx CAN FD message with BRS */
volatile bool sendCANFD;

/* Forward declarations */
static void processRxMsg(void);
static void printRxMsg(void);
static void handleEvent(void);
static void verifyMsg(void);
static void txTestMsg(uint32_t id, uint32_t extID, uint32_t dlc, uint32_t fdFormat, uint32_t brsEnable);

/*
 *  ======== handleEvent ========
 */
static void handleEvent(void)
{
    if (curEvent == CAN_EVENT_RX_DATA_AVAIL)
    {
        rxEventCnt++;
        processRxMsg();

#ifdef CONFIG_GPIO_LED_1
        /* Turn off LED1, indicating response was received */
        GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);
#endif /* CONFIG_GPIO_LED_1*/
    }
    else
    {
        if (curEvent == CAN_EVENT_TX_FINISHED)
        {
            txEventCnt++;
            sprintf(formattedMsg, "> Tx Finished. Cnt = %u\r\n\n", (unsigned int)txEventCnt);
        }
        else if (curEvent == CAN_EVENT_BUS_ON)
        {
            sprintf(formattedMsg, "> Bus On\r\n\n");
        }
        else if (curEvent == CAN_EVENT_BUS_OFF)
        {
            sprintf(formattedMsg, "> Bus Off\r\n\n");
        }
        else if (curEvent == CAN_EVENT_ERR_ACTIVE)
        {
            sprintf(formattedMsg, "> Error Active\r\n\n");
        }
        else if (curEvent == CAN_EVENT_ERR_PASSIVE)
        {
            sprintf(formattedMsg, "> Error Passive\r\n\n");
        }
        else if (curEvent == CAN_EVENT_RX_FIFO_MSG_LOST)
        {
            sprintf(formattedMsg, "> Rx FIFO %u message lost\r\n\n", (unsigned int)curEventData);
        }
        else if (curEvent == CAN_EVENT_RX_RING_BUFFER_FULL)
        {
            sprintf(formattedMsg, "> Rx ring buffer full: Cnt = %u\r\n\n", (unsigned int)curEventData);
        }
        else if (curEvent == CAN_EVENT_BIT_ERR_UNCORRECTED)
        {
            sprintf(formattedMsg, "> Uncorrected bit error\r\n\n");
        }
        else if (curEvent == CAN_EVENT_SPI_XFER_ERROR)
        {
            sprintf(formattedMsg, "> SPI transfer error: status = 0x%x\r\n\n", (unsigned int)curEventData);
        }
        else
        {
            sprintf(formattedMsg, "> Undefined event\r\n\n");
        }

        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
    }
}

/*
 *  ======== printRxMsg ========
 */
static void printRxMsg(void)
{
    uint_fast8_t dataLen;
    uint_fast8_t i;

    sprintf(formattedMsg, "Msg ID: 0x%x\r\n", (unsigned int)rxElem.id);

    sprintf(formattedMsg + strlen(formattedMsg), "TS: 0x%04x\r\n", rxElem.rxts);

#ifndef CAN_SUPPORTS_DCAN

    sprintf(formattedMsg + strlen(formattedMsg), "CAN FD: %u\r\n", rxElem.fdf);

#endif /* CAN_SUPPORTS_DCAN */

    sprintf(formattedMsg + strlen(formattedMsg), "DLC: %u\r\n", rxElem.dlc);

#ifndef CAN_SUPPORTS_DCAN

    sprintf(formattedMsg + strlen(formattedMsg), "BRS: %u\r\n", rxElem.brs);

#endif /* CAN_SUPPORTS_DCAN */

    sprintf(formattedMsg + strlen(formattedMsg), "ESI: %u\r\n", rxElem.esi);

    if (rxElem.dlc < DLC_TABLE_SIZE)
    {
        dataLen = dlcToDataSize[rxElem.dlc];

        sprintf(formattedMsg + strlen(formattedMsg), "Data[%lu]: ", (unsigned long)dataLen);

        /* Format the message as printable hex */
        for (i = 0U; i < dataLen; i++)
        {
            sprintf(formattedMsg + strlen(formattedMsg), "%02X ", rxElem.data[i]);
        }

        sprintf(formattedMsg + strlen(formattedMsg), "\r\n\n");
    }

    UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
}

/*
 *  ======== verifyMsg ========
 *  Verifies the Rx'ed message is the same as the Tx'ed message except with the
 *  ID and data bits flipped.
 */
static void verifyMsg(void)
{
    bool verifyErr = false;
    uint_fast8_t dataLen;
    uint_fast8_t i;
    uint32_t expectedID;

    /* Flip transmitted ID bits */
    expectedID = ~txElem.id;
    if (txElem.xtd == 0)
    {
        /* 11-bit standard ID */
        expectedID &= 0x7FF;
    }
    else
    {
        /* 29-bit standard ID */
        expectedID &= 0x1FFFFFFF;
    }

    if (rxElem.id != expectedID)
    {
        sprintf(formattedMsg,
                "=> FAIL: Received ID: 0x%x does not match expected ID: 0x%x!\r\n\n",
                (unsigned int)rxElem.id,
                (unsigned int)expectedID);
        verifyErr = true;
    }
#ifndef CAN_SUPPORTS_DCAN
    else if (rxElem.fdf != txElem.fdf)
    {
        sprintf(formattedMsg,
                "=> FAIL: Received FDF: %u does not match transmitted FDF: %u!\r\n\n",
                (unsigned int)rxElem.fdf,
                (unsigned int)txElem.fdf);
        verifyErr = true;
    }
#endif /* CAN_SUPPORTS_DCAN */
    else if (rxElem.dlc != txElem.dlc)
    {
        sprintf(formattedMsg,
                "=> FAIL: Received DLC: %u does not match transmitted DLC: %u!\r\n\n",
                (unsigned int)rxElem.dlc,
                (unsigned int)txElem.dlc);
        verifyErr = true;
    }
    else if (rxElem.dlc < DLC_TABLE_SIZE)
    {
        dataLen = dlcToDataSize[rxElem.dlc];

        for (i = 0U; i < dataLen; i++)
        {
            if (rxElem.data[i] != (uint8_t)~txElem.data[i])
            {
                sprintf(formattedMsg,
                        "=> FAIL: Received data[%u]: 0x%02x does not match expected data: 0x%02x!\r\n\n",
                        i,
                        rxElem.data[i],
                        (uint8_t)~txElem.data[i]);
                verifyErr = true;
                break;
            }
        }
    }

    if (!verifyErr)
    {
        sprintf(formattedMsg, "=> PASS: Received message matches expected.\r\n\n");
    }

    UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
}

/*
 *  ======== processRxMsg ========
 */
static void processRxMsg(void)
{
    /* Read all available CAN messages */
    while (CAN_read(canHandle, &rxElem) == CAN_STATUS_SUCCESS)
    {
        rxMsgCnt++;

        sprintf(formattedMsg, "RxMsg Cnt: %u, RxEvt Cnt: %u\r\n", (unsigned int)rxMsgCnt, (unsigned int)rxEventCnt);
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);

        printRxMsg();
        verifyMsg();
        sem_post(&rxSem);
    }
}

/*
 *  ======== eventCallback ========
 */
static void eventCallback(CAN_Handle handle, uint32_t event, uint32_t data, void *userArg)
{
    curEvent     = event;
    curEventData = data;
    sem_post(&eventSem);
}

/*
 *  ======== txTestMsg ========
 */
static void txTestMsg(uint32_t id, uint32_t extID, uint32_t dlc, uint32_t fdFormat, uint32_t brsEnable)
{
    uint_fast8_t i;
    int_fast16_t status;

    txElem.id  = id;
    txElem.rtr = 0U;
    txElem.xtd = extID;
#ifndef CAN_SUPPORTS_DCAN
    txElem.esi = 0U;
    txElem.brs = brsEnable;
#endif /* CAN_SUPPORTS_DCAN */
    txElem.dlc = dlc;
#ifndef CAN_SUPPORTS_DCAN
    txElem.fdf = fdFormat;
#endif /* CAN_SUPPORTS_DCAN */
    txElem.efc = 0U;
    txElem.mm  = 1U;

    for (i = 0U; i < dlcToDataSize[txElem.dlc]; i++)
    {
        txElem.data[i] = i;
    }

    status = CAN_write(canHandle, &txElem);
    if (status != CAN_STATUS_SUCCESS)
    {
        /* CAN_write() failed */
        while (1) {}
    }
}

/*
 * ======== buttonPressedCallback ========
 */

static void buttonPressedCallback(Button_Handle buttonHandle, Button_EventMask buttonEvents)
{
    switch (buttonEvents)
    {
        case Button_EV_CLICKED:

#ifndef CAN_SUPPORTS_DCAN
            if (((Button_HWAttrs *)buttonHandle->hwAttrs)->gpioIndex == CONFIG_GPIO_BUTTON_0_INPUT)
            {
                sendCANFD = true;
            }
            else
            {
                sendCANFD = false;
            }
#endif /* CAN_SUPPORTS_DCAN */

            sem_post(&buttonSem);
            break;

        default:
            break;
    }
}

/*
 *  ======== initiatorThread ========
 * The initiator thread transmits a CAN message when the user presses a button
 * on the LaunchPad and waits for a response.
 */
void *initiatorThread(void *arg0)
{
    CAN_Params canParams;
    int retc;

    retc = sem_init(&buttonSem, 0, 0);
    if (retc != 0)
    {
        /* sem_init() failed */
        while (1) {}
    }

    retc = sem_init(&rxSem, 0, 0);
    if (retc != 0)
    {
        /* sem_init() failed */
        while (1) {}
    }

    /* Open CAN driver with default configuration */
    CAN_Params_init(&canParams);
    canParams.eventCbk  = eventCallback;
    canParams.eventMask = CAN_EVENT_MASK;

    canHandle = CAN_open(CONFIG_CAN_0, &canParams);
    if (canHandle == NULL)
    {
        /* CAN_open() failed */
        sprintf(formattedMsg, "\r\nError opening CAN driver!\r\n");
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
        while (1) {}
    }
    else
    {
        sprintf(formattedMsg, "\r\nCAN Initiator ready. Waiting for button press...\r\n\n");
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
    }

#ifdef CONFIG_BUTTON_0
    Button_Params_init(&button0Params);
#endif

    Button_Params_init(&button1Params);

#ifdef CONFIG_BUTTON_0
    button0Params.buttonCallback              = buttonPressedCallback;
    button0Params.buttonEventMask             = Button_EV_CLICKED;
    button0Params.debounceDuration            = 100;
    button0Params.longPressDuration           = 10000;
    button0Params.doublePressDetectiontimeout = 10000;
#endif /* CONFIG_BUTTON_0 */

    button1Params.buttonCallback              = buttonPressedCallback;
    button1Params.buttonEventMask             = Button_EV_CLICKED;
    button1Params.debounceDuration            = 100;
    button1Params.longPressDuration           = 10000;
    button1Params.doublePressDetectiontimeout = 10000;

#ifdef CONFIG_BUTTON_0
    button0Handle = Button_open(CONFIG_BUTTON_0, &button0Params);

    if (button0Handle == NULL)
    {
        while (1) {};
    }

#endif /* CONFIG_BUTTON_0 */

    button1Handle = Button_open(CONFIG_BUTTON_1, &button1Params);

    if (button1Handle == NULL)
    {
        while (1) {};
    }

    /* Loop forever */
    while (1)
    {
        /* Wait until button press callback semaphore is posted */
        sem_wait(&buttonSem);

        if (sendCANFD)
        {
            /* Tx CAN FD message with bit rate switching */
            txTestMsg(0x12345678, 1U, CAN_DLC_64B, 1U, 1U);
        }
        else
        {
            /* Tx classic CAN message */
            txTestMsg(0x5AA, 0U, CAN_DLC_8B, 0U, 0U);
        }

        /* Wait until response is received */
        sem_wait(&rxSem);
    }
}

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    int retc;
    pthread_attr_t attrs;
    pthread_t thread0;
    struct sched_param priParam;
    UART2_Params uart2Params;

    UART2_Params_init(&uart2Params);
    uart2Params.writeMode = UART2_Mode_NONBLOCKING;

    uart2Handle = UART2_open(CONFIG_UART2_0, &uart2Params);

    if (uart2Handle == NULL)
    {
        /* UART2_open() failed */
        while (1) {}
    }

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

    /* Create CAN initiator thread */
    retc = pthread_create(&thread0, &attrs, initiatorThread, NULL);
    if (retc != 0)
    {
        /* pthread_create() failed */
        while (1) {}
    }

    retc = sem_init(&eventSem, 0, 0);
    if (retc != 0)
    {
        /* sem_init() failed */
        while (1) {}
    }

    /* Loop forever */
    while (1)
    {
        /* Wait until event callback semaphore is posted */
        sem_wait(&eventSem);

        /* Process the event */
        handleEvent();
    }
}
