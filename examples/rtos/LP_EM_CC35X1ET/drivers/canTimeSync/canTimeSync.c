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
 *  ======== canTimeSync.c ========
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

#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(inc/hw_memmap.h)
#include DeviceFamily_constructPath(inc/hw_systim.h)
#include DeviceFamily_constructPath(inc/hw_types.h)

/* Driver configuration */
#include "ti_drivers_config.h"

/* Defines */
#define THREAD_STACK_SIZE    1024U
#define UART_MSG_BUFFER_SIZE 512U

/* Message ID for time sync messages */
#define CAN_TIME_SYNC_MSG_ID 0x2

/* Message ID for non-time sync messages */
#define CAN_NON_TIME_SYNC_MSG_ID (CAN_TIME_SYNC_MSG_ID + 1U)

/* Number of microseconds after the time sync message Start Of Frame to toggle LED1 */
#define SOF_TO_LED_TOGGLE_USEC 500U

/* Microseconds to 250ns system timer tick conversion macro */
#define USEC_TO_SYSTIM(usec) (usec * 4U) /* Four 250ns system timer ticks per 1us */

/* Picoseconds to 250ns system timer tick conversion macro */
#define PSEC_TO_SYSTIM(psec) ((psec * 4U) / 1000000U) /* Four 250ns system timer ticks per 1 million ps */

/* External timestamp counter rate is the Host System Clock (96 MHz) divided by
 * the timestamp prescaler. A timestamp prescaler of 24 was chosen to match the
 * system timer resolution of 250ns.
 */
#define CANCC27XX_EXT_TIMESTAMP_PRESCALER 24U

#define CAN_EVENT_MASK                                                                                      \
    (CAN_EVENT_RX_DATA_AVAIL | CAN_EVENT_TX_FINISHED | CAN_EVENT_TX_EVENT_AVAIL | CAN_EVENT_TX_EVENT_LOST | \
     CAN_EVENT_BUS_ON | CAN_EVENT_BUS_OFF | CAN_EVENT_ERR_ACTIVE | CAN_EVENT_ERR_PASSIVE |                  \
     CAN_EVENT_RX_FIFO_MSG_LOST | CAN_EVENT_RX_RING_BUFFER_FULL | CAN_EVENT_BIT_ERR_UNCORRECTED)

#define DLC_TABLE_SIZE 16U

/* Payload bytes indexed by Data Length Code (DLC) field. */
static const uint32_t dlcToDataSize[DLC_TABLE_SIZE] =
    {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U};

/* The following globals are not designated as 'static' to allow debug access */

/* CAN handle */
CAN_Handle canHandle;

/* UART2 handle */
UART2_Handle uart2Handle;

/* Formatted message buffer */
char formattedMsg[UART_MSG_BUFFER_SIZE];

/* Rx and Tx buffer elements */
CAN_RxBufElement rxElem;
CAN_TxBufElement txElem;

/* Tx Event element */
CAN_TxEventElement txEventelem;

/* Button driver parameters. */
Button_Params button0Params;
Button_Params button1Params;

/* Button handlers. */
Button_Handle button0Handle;
Button_Handle button1Handle;

/* Received msg count */
uint32_t rxMsgCnt = 0U;

/* Event callback count */
volatile uint32_t rxEventCnt     = 0U;
volatile uint32_t txEventCnt     = 0U;
volatile uint32_t txEventLostCnt = 0U;

/* Start Of Frame to Tx/Rx timestamp delay */
uint32_t sofToTimestampDelay;

/* Message transmit complete semaphore */
sem_t txCompleteSem;

/* Button press semaphore */
sem_t buttonSem;

/* Flag to Tx CAN FD message with EFC (Event FIFO Control) */
volatile bool efcEnable;

/* Forward declarations */
static void eventCallback(CAN_Handle handle, uint32_t curEvent, uint32_t curEventData, void *userArg);
static void handleTimeSyncRx(void);
static void handleTxEvent(void);
static void printRxMsg(void);
static void processRxMsg(void);
static void txTestMsg(uint32_t id, uint32_t efc, uint32_t dlc, uint32_t brsEnable);

/*
 *  ======== eventCallback ========
 */
static void eventCallback(CAN_Handle handle, uint32_t curEvent, uint32_t curEventData, void *userArg)
{
    if (curEvent == CAN_EVENT_RX_DATA_AVAIL)
    {
        rxEventCnt++;
        processRxMsg();
    }
    else if (curEvent == CAN_EVENT_TX_EVENT_AVAIL)
    {
        handleTxEvent();
    }
    else
    {
        if (curEvent == CAN_EVENT_TX_FINISHED)
        {
            txEventCnt++;
            sprintf(formattedMsg, "> Tx Finished. Cnt = %u\r\n\n", (unsigned int)txEventCnt);
            sem_post(&txCompleteSem);
        }
        else if (curEvent == CAN_EVENT_TX_EVENT_LOST)
        {
            txEventLostCnt++;
            sprintf(formattedMsg, "> Tx Event Lost. Cnt = %u\r\n\n", (unsigned int)txEventLostCnt);
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
 *  ======== handleTxEvent ========
 */
static void handleTxEvent(void)
{
    int_fast16_t status;
    uint16_t tscv;
    uint16_t txts;
    uint16_t txtsToTscvDelta;
    uint32_t sofTime;
    uint32_t systim;
    uint32_t targetTime;
    uintptr_t hwiKey;

    hwiKey = HwiP_disable();

    /* Read current system time */
    systim = HWREG(SYSTIM_BASE + SYSTIM_O_TIME250N);

#ifndef CAN_SUPPORTS_DCAN

    /* Get current timestamp counter value */
    tscv = MCAN_getTimestampCounter();

#else

    tscv = DCAN_getTimestampCounter();

#endif /* (DeviceFamily_PARENT != DeviceFamily_PARENT_CC35XX) */

    /* Read Tx Event element */
    status = CAN_readTxEvent(canHandle, &txEventelem);

    if (status != CAN_STATUS_SUCCESS)
    {
        sprintf(formattedMsg, "> Failed to read Tx Event\r\n\n");
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
        return;
    }

    if (txEventelem.id != CAN_TIME_SYNC_MSG_ID)
    {
        sprintf(formattedMsg, "> Unexpected time sync msg ID: 0x%x\r\n\n", (unsigned int)txEventelem.id);
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
        return;
    }

    /* Read the Tx timestamp */
    txts = txEventelem.txts;

    /* Determine the time from the Tx timestamp to where the live
     * timestamp counter value was read in the event callback.
     */
    txtsToTscvDelta = tscv - txts;

    /* Calculate the SOF time in system time domain. Since both the
     * System Timer and CAN external timestamp have 250ns resolution, no
     * scaling of the txtsToTscvDelta is necessary.
     */
    sofTime = systim - txtsToTscvDelta - sofToTimestampDelay;

    /* Calculate a target time after the SOF */
    targetTime = sofTime + USEC_TO_SYSTIM(SOF_TO_LED_TOGGLE_USEC);

    /* For simplicity, this example polls until the target system time
     * is reached.
     */
    while (HWREG(SYSTIM_BASE + SYSTIM_O_TIME250N) < targetTime) {}

#ifdef CONFIG_GPIO_LED_1

    /* Toggle LED1 */
    GPIO_toggle(CONFIG_GPIO_LED_1);

#endif /* CONFIG_GPIO_LED_1 */

    HwiP_restore(hwiKey);

    sprintf(formattedMsg,
            "> Tx Event. TXTS = 0x%04x, SOF time = 0x%08x\r\n\n",
            (unsigned int)txts,
            (unsigned int)sofTime);
    UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
}

/*
 *  ======== handleTimeSyncRx ========
 */
static void handleTimeSyncRx(void)
{
    uint16_t rxts;
    uint16_t rxtsToTscvDelta;
    uint16_t tscv;
    uint32_t sofTime;
    uint32_t systim;
    uint32_t targetTime;
    uintptr_t hwiKey;

    hwiKey = HwiP_disable();

    /* Read current system time */
    systim = HWREG(SYSTIM_BASE + SYSTIM_O_TIME250N);

#ifndef CAN_SUPPORTS_DCAN

    /* Get current timestamp counter value */
    tscv = MCAN_getTimestampCounter();

#else

    tscv = DCAN_getTimestampCounter();

#endif /* CAN_SUPPORTS_DCAN */

    /* Read the Rx timestamp */
    rxts = rxElem.rxts;

    /* Determine the time from the Rx timestamp to where the live
     * timestamp counter value was read in the event callback.
     */
    rxtsToTscvDelta = tscv - rxts;

    /* Calculate the SOF time in system time domain. Since both the
     * System Timer and CAN external timestamp have 250ns resolution, no
     * scaling of the rxtsToTscvDelta is necessary.
     */
    sofTime = systim - rxtsToTscvDelta - sofToTimestampDelay;

    /* Calculate a target time after the SOF */
    targetTime = sofTime + USEC_TO_SYSTIM(SOF_TO_LED_TOGGLE_USEC);

    /* For simplicity, this example polls until the target system time
     * is reached.
     */
    while (HWREG(SYSTIM_BASE + SYSTIM_O_TIME250N) < targetTime) {}

#ifdef CONFIG_GPIO_LED_1

    /* Toggle LED1 */
    GPIO_toggle(CONFIG_GPIO_LED_1);

#endif /* CONFIG_GPIO_LED_1 */

    HwiP_restore(hwiKey);

    sprintf(formattedMsg,
            "> Time Sync msg Rx'ed. RXTS = 0x%04x, SOF time = 0x%08x\r\n\n",
            (unsigned int)rxts,
            (unsigned int)sofTime);
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

        if (rxElem.id == CAN_TIME_SYNC_MSG_ID)
        {
            handleTimeSyncRx();
        }

        sprintf(formattedMsg, "RxMsg Cnt: %u, RxEvt Cnt: %u\r\n", (unsigned int)rxMsgCnt, (unsigned int)rxEventCnt);
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);

        printRxMsg();
    }
}

/*
 *  ======== txTestMsg ========
 */
static void txTestMsg(uint32_t id, uint32_t efc, uint32_t dlc, uint32_t brsEnable)
{
    uint_fast8_t i;
    int_fast16_t status;

    txElem.id  = id;
    txElem.rtr = 0U;
    txElem.xtd = 1U;
#ifndef CAN_SUPPORTS_DCAN
    txElem.esi = 0U;
    txElem.brs = brsEnable;
#endif /* CAN_SUPPORTS_DCAN */
    txElem.dlc = dlc;
#ifndef CAN_SUPPORTS_DCAN
    txElem.fdf = 1U;
#endif /* CAN_SUPPORTS_DCAN */
    txElem.efc = efc;
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

            if (((Button_HWAttrs *)buttonHandle->hwAttrs)->gpioIndex == CONFIG_GPIO_BUTTON_0_INPUT)
            {
                efcEnable = true;
            }
            else
            {
                efcEnable = false;
            }
            sem_post(&buttonSem);
            break;

        default:
            break;
    }
}

/*
 *  ======== mainThread ========
 * This thread transmits a CAN message with zero bytes of payload when the user
 * presses one of the LaunchPad buttons. If BTN-1 is pressed, the message is
 * sent with a "time sync" message ID. If BTN-2 is pressed, the message is sent
 * with a "regular" message ID.
 */
void *mainThread(void *arg0)
{
    CAN_Params canParams;
    CAN_BitTimingParams bitTiming;
    int retc;
    UART2_Params uart2Params;
    uint32_t clkFreqKhz;
    uint32_t clkPeriod;
    uint32_t tq;

    UART2_Params_init(&uart2Params);
    uart2Params.writeMode = UART2_Mode_NONBLOCKING;

    uart2Handle = UART2_open(CONFIG_UART2_0, &uart2Params);

    if (uart2Handle == NULL)
    {
        /* UART2_open() failed */
        while (1) {}
    }

    retc = sem_init(&buttonSem, 0, 0);
    if (retc != 0)
    {
        /* sem_init() failed */
        while (1) {}
    }

    retc = sem_init(&txCompleteSem, 0, 0);
    if (retc != 0)
    {
        /* sem_init() failed */
        while (1) {}
    }

    /* Initialize CAN driver params */
    CAN_Params_init(&canParams);
    canParams.tsPrescaler = CANCC27XX_EXT_TIMESTAMP_PRESCALER;
    canParams.eventCbk    = eventCallback;
    canParams.eventMask   = CAN_EVENT_MASK;

    /* Open the CAN driver */
    canHandle = CAN_open(CONFIG_CAN_0, &canParams);
    if (canHandle == NULL)
    {
        /* CAN_open() failed */
        sprintf(formattedMsg, "\r\nError opening CAN driver!\r\n");
        UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);
        while (1) {}
    }

    CAN_getBitTiming(canHandle, &bitTiming, &clkFreqKhz);

    /* Calculate the CAN functional clock period in picoseconds */
    clkPeriod = 1000000000U / clkFreqKhz;

    /* Determine the Time Quantum in picoseconds.
     * Note: Add 1 to nomRatePrescaler to get functional value.
     */
    tq = clkPeriod * (bitTiming.nomRatePrescaler + 1U);

    /* Calculate the Start Of Frame to Tx/Rx timestamp delta value in picoseconds.
     * The formula is based on RTL simulation is:
     *     (6 * CAN_FUNCTIONAL_CLK_PERIOD) + (TSEG1 * tq)
     * where TSEG1 is the number of time quantum before the sampling point:
     * Prop_Seg + Phase_Seg1
     *
     * Note: Add 1 to nomTimeSeg1 to get functional value
     */
    sofToTimestampDelay = (6U * clkPeriod) + ((bitTiming.nomTimeSeg1 + 1U) * tq);

    /* Convert the Start Of Frame to Tx timestamp delta value to system time domain */
    sofToTimestampDelay = PSEC_TO_SYSTIM(sofToTimestampDelay);

#ifdef CONFIG_GPIO_LED_0

    /* Turn on LED0 to indicate successful initialization */
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);

#endif /* CONFIG_GPIO_LED_0 */

    Button_Params_init(&button0Params);
    Button_Params_init(&button1Params);

    button0Params.buttonCallback              = buttonPressedCallback;
    button0Params.buttonEventMask             = Button_EV_CLICKED;
    button0Params.debounceDuration            = 100;
    button0Params.longPressDuration           = 10000;
    button0Params.doublePressDetectiontimeout = 10000;

    button1Params.buttonCallback              = buttonPressedCallback;
    button1Params.buttonEventMask             = Button_EV_CLICKED;
    button1Params.debounceDuration            = 100;
    button1Params.longPressDuration           = 10000;
    button1Params.doublePressDetectiontimeout = 10000;

    button0Handle = Button_open(CONFIG_BUTTON_0, &button0Params);
    button1Handle = Button_open(CONFIG_BUTTON_1, &button1Params);

    if (button0Handle == NULL)
    {
        while (1) {};
    }

    if (button1Handle == NULL)
    {
        while (1) {};
    }

    sprintf(formattedMsg,
            "\r\nCAN Time Sync ready.\r\nPress BTN-1 to send time sync msg or BTN-2 to send a regular msg...\r\n\n");
    UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);

    /* Loop forever */
    while (1)
    {
        /* Wait until button press callback semaphore is posted */
        sem_wait(&buttonSem);

        if (efcEnable)
        {
            sprintf(formattedMsg, "\r\nSending time sync message...\r\n\n");
            UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);

#ifndef CAN_SUPPORTS_DCAN
            /* Tx CAN FD message with time sync msg ID and EFC */
            txTestMsg(CAN_TIME_SYNC_MSG_ID, 1U, CAN_DLC_0B, 1U);
#else
            /* Tx CAN message with time sync msg ID and EFC */
            txTestMsg(CAN_TIME_SYNC_MSG_ID, 1U, CAN_DLC_0B, 0U);
#endif /* (DeviceFamily_PARENT != DeviceFamily_PARENT_CC35XX) */
        }
        else
        {
            sprintf(formattedMsg, "\r\nSending regular message...\r\n\n");
            UART2_write(uart2Handle, formattedMsg, strlen(formattedMsg), NULL);

#ifndef CAN_SUPPORTS_DCAN
            /* Tx CAN FD message with non-time sync msg ID without EFC */
            txTestMsg(CAN_NON_TIME_SYNC_MSG_ID, 0U, CAN_DLC_0B, 1U);
#else
            /* Tx CAN message with non-time sync msg ID without EFC */
            txTestMsg(CAN_NON_TIME_SYNC_MSG_ID, 0U, CAN_DLC_0B, 0U);
#endif /* CAN_SUPPORTS_DCAN */
        }

        /* Wait until Tx is completed */
        sem_wait(&txCompleteSem);
    }
}
