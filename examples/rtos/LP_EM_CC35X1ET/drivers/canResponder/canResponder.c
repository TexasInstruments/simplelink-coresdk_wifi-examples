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
 *  ======== canResponder.c ========
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* POSIX Header files */
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

/* Driver Header files */
#include <ti/drivers/CAN.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/UART2.h>

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

/* Current event */
volatile uint32_t curEvent;

/* Current event data */
volatile uint32_t curEventData;

/* CAN event semaphore */
sem_t eventSem;

/* Forward declarations */
static void processRxMsg(void);
static void sendResponse(void);
static void printRxMsg(void);
static void handleEvent(void);

/*
 *  ======== handleEvent ========
 */
static void handleEvent(void)
{
    if (curEvent == CAN_EVENT_RX_DATA_AVAIL)
    {

#ifdef CONFIG_GPIO_LED_1
        /* Turn on LED1, indicating a message has been received */
        GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_ON);
#endif /* CONFIG_GPIO_LED_1 */

        rxEventCnt++;
        processRxMsg();
    }
    else
    {
        if (curEvent == CAN_EVENT_TX_FINISHED)
        {
            sprintf(formattedMsg, "> Response sent.\r\n\n");

#ifdef CONFIG_GPIO_LED_1

            /* Delay to allow the LED1 flash to be visible */
            usleep(1000);

            /* Turn off LED1, indicating response was sent */
            GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);

#endif /* CONFIG_GPIO_LED_1 */
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
 *  ======== sendResponse ========
 */
static void sendResponse(void)
{
    uint_fast8_t i;
    int_fast16_t status;

    /* Flip received ID bits */
    txElem.id = ~rxElem.id;
    if (rxElem.xtd == 0)
    {
        /* 11-bit standard ID */
        txElem.id &= 0x7FF;
    }
    else
    {
        /* 29-bit standard ID */
        txElem.id &= 0x1FFFFFFF;
    }

    txElem.rtr = rxElem.rtr;
    txElem.xtd = rxElem.xtd;
#ifndef CAN_SUPPORTS_DCAN
    txElem.esi = rxElem.esi;
    txElem.brs = rxElem.brs;
#endif /* CAN_SUPPORTS_DCAN */
    txElem.dlc = rxElem.dlc;
#ifndef CAN_SUPPORTS_DCAN
    txElem.fdf = rxElem.fdf;
#endif /* CAN_SUPPORTS_DCAN */
    txElem.efc = 0U;
    txElem.mm  = 2U;

    /* Flip received data bits */
    for (i = 0U; i < dlcToDataSize[rxElem.dlc]; i++)
    {
        txElem.data[i] = ~rxElem.data[i];
    }

    status = CAN_write(canHandle, &txElem);
    if (status != CAN_STATUS_SUCCESS)
    {
        /* CAN_write() failed */
        sprintf(formattedMsg, "CAN write failed!\r\n\n");
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
        while (1) {}
    }
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
        sendResponse();
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
 *  ======== responderThread ========
 * The responder thread receives CAN messages and transmits a response message
 * using the received message ID with all bits flipped and data with all bits
 * flipped.
 */
void *responderThread(void *arg0)
{
    CAN_Params canParams;
    int retc;

    retc = sem_init(&eventSem, 0, 0);
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
        sprintf(formattedMsg, "\r\nCAN Responder ready. Waiting for CAN messages...\r\n\n");
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
    }

#ifdef CONFIG_GPIO_LED_0

    /* Turn on LED0 to indicate successful initialization */
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);

#endif /* CONFIG_GPIO_LED_0 */

    /* Loop forever */
    while (1)
    {
        /* Wait until event callback semaphore is posted */
        sem_wait(&eventSem);

        /* Process the event */
        handleEvent();
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

    /* Create CAN responder thread */
    retc = pthread_create(&thread0, &attrs, responderThread, NULL);
    if (retc != 0)
    {
        /* pthread_create() failed */
        while (1) {}
    }

    return (NULL);
}
