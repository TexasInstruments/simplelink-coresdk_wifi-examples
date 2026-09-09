/*
 * Copyright (c) 2026 Texas Instruments Incorporated
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
 *  ======== pdmsamples.c ========
 *
 *  This example demonstrates button-controlled PDM audio streaming with
 *  raw PCM data forwarded to a host application over UART.
 *
 *  Button 0 (BUTTON_0): Press to start PDM streaming.
 *  Button 1 (BUTTON_1): Press to stop  PDM streaming.
 *
 *  UART write strategy:
 *
 *    UART2_Mode_BLOCKING is used. UART2_write() blocks until the entire
 *    transfer is complete (EOT interrupt), so consecutive writes are
 *    naturally serialized and no semaphore guard is required.
 *
 *    For PCM data, mainThread passes the PDM buffer pointer directly to
 *    UART2_write().  Because the call does not return until the last bit
 *    has left the shift register, the PDM buffer is safe to return to DMA
 *    immediately after UART2_write() returns, with no intermediate copy.
 *
 *  LED color status (LED_RED=LED0, LED_GREEN=LED1):
 *
 *    Green solid       : Device ready to stream (idle, waiting for BUTTON_0)
 *    Red blinking +
 *      Green solid    : Streaming active - Red blinks per buffer, Green stays on
 *    Red solid        : Error (hardware or initialization failure)
 */

/* Standard C library headers */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

/* POSIX headers */
#include <semaphore.h>
#include <time.h>

/* Other TI driver headers */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/PDM.h>
#include <ti/drivers/UART2.h>

/* Driver configuration */
#include "ti_drivers_config.h"

/* PDM Sample Configuration */
#define PDM_BUFFER_SIZE_BYTES 4096                  /* 4KB buffer size */
#define PDM_NUM_BUFS          2                     /* Number of buffers */
#define PDM_SAMPLE_RATE       PDM_SAMPLING_RATE_16K /* 16 kHz sampling rate */
#define PDM_PCM_WIDTH         16                    /* valid widths: 8, 16, 24*/

/* Semaphores for PDM inter-thread signalling */
static sem_t semDataReady;
static sem_t semErrorCallback;
static sem_t semStartStream;
static sem_t semStopStream;

/* PDM DMA buffers */
static uint32_t buf1[PDM_BUFFER_SIZE_BYTES / sizeof(uint32_t)];
static uint32_t buf2[PDM_BUFFER_SIZE_BYTES / sizeof(uint32_t)];
static uint32_t *pdmBufList[PDM_NUM_BUFS] = {buf1, buf2};

/* PDM transactions */
static PDM_Transaction pdmTransaction1;
static PDM_Transaction pdmTransaction2;
static PDM_Transaction *pdmTransactionList[PDM_NUM_BUFS] = {&pdmTransaction1, &pdmTransaction2};

/* PDM driver handle */
static PDM_Handle pdmHandle;

/* UART handle */
static UART2_Handle uartHandle;

static List_List dmaList;
static List_List appList;

static volatile uint32_t bufferCount      = 0;
static volatile int_fast16_t pdmErrorCode = 0;

/*
 *  ======== gpioStartFxn ========
 *  GPIO interrupt callback for BUTTON_0 (start streaming).
 */
static void gpioStartFxn(uint_least8_t index)
{
    sem_post(&semStartStream);
}

/*
 *  ======== gpioStopFxn ========
 *  GPIO interrupt callback for BUTTON_1 (stop streaming).
 */
static void gpioStopFxn(uint_least8_t index)
{
    sem_post(&semStopStream);
}

/*
 *  ======== errorCallbackFxn ========
 *  Called when PDM hardware errors occur (overflow/underflow) or Manchester
 *  lock timeout.
 */
static void errorCallbackFxn(PDM_Handle handle, int_fast16_t status, PDM_Transaction *transactionPtr)
{
    /* Store error code for main thread to read after sem_post wakes it */
    pdmErrorCode = status;
    sem_post(&semErrorCallback);
}

/*
 *  ======== readCallbackFxn ========
 *  Called when a PDM transaction completes (ISR context).
 *  Main Thread returns the transaction to dmaList after application
 *  completes its processing.
 *
 *  Only LED_GREEN is toggled here to show buffer activity.  LED_RED is
 *  owned by the main thread for state indication and must
 *  not be touched from this callback.
 */
static void readCallbackFxn(PDM_Handle handle, int_fast16_t status, PDM_Transaction *transactionPtr)
{
    if (status == PDM_TRANSACTION_SUCCESS)
    {
        /* Move finished transaction from dmaList to appList.
         * The driver handles the critical section internally.
         */
        PDM_moveTransactionToApp(handle, transactionPtr);

        /* Signal main thread that data is ready */
        sem_post(&semDataReady);

        /* Toggle Red to show per-buffer activity */
        GPIO_toggle(CONFIG_GPIO_LED_0);
    }
}

/*
 *  ======== sendToHost ========
 *  Issue a blocking UART write of the PCM buffer.
 *  UART2_write() does not return until the last byte has left the shift
 *  register, so the PDM buffer is safe to return to DMA immediately after
 *  this function returns.
 */
static void sendToHost(uint32_t *buffer, uint32_t byteCount)
{
    bufferCount++;
    UART2_write(uartHandle, buffer, byteCount, NULL);
}

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    int retc;
    uint8_t bufIdx;
    PDM_Params pdmParams;
    UART2_Params uartParams;
    struct timespec ts;
    PDM_Transaction *transaction;
    bool isStreaming = false;

    /* Call driver init functions */
    GPIO_init();
    PDM_init();

    /* Open back-channel UART in blocking mode.
     * UART2_write() blocks until the last bit has left the shift register,
     * so consecutive writes are naturally serialized with no semaphore guard.
     * Connect at 921600 baudrate on the COM port that appears when the
     * board is plugged in via USB.
     */

    UART2_Params_init(&uartParams);
    uartParams.baudRate = 921600;
    uartHandle          = UART2_open(CONFIG_UART2_0, &uartParams);

    /* Configure Red and Green LED channels as outputs, initially off */
    GPIO_setConfig(CONFIG_GPIO_LED_0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_LED_1, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);

    /* Configure buttons as falling-edge interrupt inputs with pull-ups */
    GPIO_setConfig(CONFIG_GPIO_BUTTON_0, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);
    GPIO_setConfig(CONFIG_GPIO_BUTTON_1, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

    /* Register button callbacks */
    GPIO_setCallback(CONFIG_GPIO_BUTTON_0, gpioStartFxn);
    GPIO_setCallback(CONFIG_GPIO_BUTTON_1, gpioStopFxn);

    /* Enable button interrupts */
    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);
    GPIO_enableInt(CONFIG_GPIO_BUTTON_1);

    /* Initialize PDM semaphores */
    retc = sem_init(&semDataReady, 0, 0);
    if (retc == -1)
    {
        while (1) {}
    }

    retc = sem_init(&semErrorCallback, 0, 0);
    if (retc == -1)
    {
        while (1) {}
    }

    retc = sem_init(&semStartStream, 0, 0);
    if (retc == -1)
    {
        while (1) {}
    }

    retc = sem_init(&semStopStream, 0, 0);
    if (retc == -1)
    {
        while (1) {}
    }

    /* Initialize PDM parameters */
    PDM_Params_init(&pdmParams);
    pdmParams.samplingRate  = PDM_SAMPLE_RATE;
    pdmParams.pcmWidth      = PDM_PCM_WIDTH;
    pdmParams.readCallback  = readCallbackFxn;
    pdmParams.errorCallback = errorCallbackFxn;

    /* Open PDM driver */
    pdmHandle = PDM_open(&pdmParams);
    if (pdmHandle == NULL)
    {
        (void)PDM_getOpenError();
        /* Error: Red solid */
        GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);
        GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);

        while (1) {}
    }

    /* Ready: Green solid */
    GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_ON);

    /* Main event loop */
    while (1)
    {
        if (!isStreaming)
        {
            /* Wait for start button press - block indefinitely */
            sem_wait(&semStartStream);

            /* Rebuild transaction lists before each new streaming session.
             * After PDM_stopStream, the driver may have delivered an in-flight
             * transaction to appList with stale bytesTransferred and internal
             * state from the previous session.  Clearing both lists and calling
             * PDM_Transaction_init on every transaction resets those fields to
             * clean defaults so PDM_startStream sees a fully fresh dmaList.
             */
            List_clearList(&dmaList);
            List_clearList(&appList);

            for (bufIdx = 0; bufIdx < PDM_NUM_BUFS; bufIdx++)
            {
                PDM_Transaction_init(pdmTransactionList[bufIdx]);
                pdmTransactionList[bufIdx]->bufPtr  = pdmBufList[bufIdx];
                pdmTransactionList[bufIdx]->bufSize = PDM_BUFFER_SIZE_BYTES;
                List_put(&dmaList, (List_Elem *)pdmTransactionList[bufIdx]);
            }

            PDM_setTransactionLists(pdmHandle, &dmaList, &appList);

            /* Discard any BUTTON_1 presses that arrived while idle so they
             * do not immediately stop the session that is about to start.
             */
            while (sem_trywait(&semStopStream) == 0) {}

            /* Reset statistics for the new session */
            bufferCount = 0;

            /* Streaming: Red blinks + Green solid - ensure Red starts OFF for blink pattern */
            GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);

            PDM_startStream(pdmHandle);

            isStreaming = true;
        }
        else
        {
            /* Poll for: new data, stop button, or PDM error.
             * Use a 1-second timeout so the loop stays responsive.
             */
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;

            retc = sem_timedwait(&semDataReady, &ts);
            if (retc == -1 && errno != ETIMEDOUT)
            {
                break;
            }

            /* Drain all completed transactions, write PCM to host, and
             * return the PDM buffer to DMA immediately after the blocking
             * write completes.
             */
            while ((transaction = (PDM_Transaction *)List_head(&appList)) != NULL)
            {
                sendToHost(transaction->bufPtr, transaction->bytesTransferred);
                PDM_moveTransactionToDMA(pdmHandle, transaction);
            }

            /* Check for PDM hardware error (non-blocking) */
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  = 0;
            ts.tv_nsec = 0;

            if (sem_timedwait(&semErrorCallback, &ts) == 0)
            {
                /* Discard any BUTTON_0 presses that arrived during streaming
                 * so the idle loop does not immediately restart the session.
                 */
                while (sem_trywait(&semStartStream) == 0) {}

                /* Error: Red solid */
                GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);
                GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);

                isStreaming = false;
                continue;
            }

            /* Check for stop button press (non-blocking) */
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  = 0;
            ts.tv_nsec = 0;

            if (sem_timedwait(&semStopStream, &ts) == 0)
            {
                PDM_stopStream(pdmHandle);

                /* Discard any BUTTON_0 presses that arrived during streaming
                 * so the idle loop does not immediately restart the session.
                 */
                while (sem_trywait(&semStartStream) == 0) {}

                /* Ready: Green solid */
                GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);

                isStreaming = false;
            }
        }
    }

    /* Should not reach here during normal operation */
    PDM_close(pdmHandle);

    /* Terminal: both LEDs off */
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
    GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);

    sem_destroy(&semDataReady);
    sem_destroy(&semErrorCallback);
    sem_destroy(&semStartStream);
    sem_destroy(&semStopStream);

    return NULL;
}
