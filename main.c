/******************************************************************************
 * File Name: main.c
 *
 * Description: This is the source code for the PSoC 4 Frequency measurement
 * using TCPWM Application for ModusToolbox.
 *
 * Related Document: See README.md 
 *
 *
 *******************************************************************************
 * Copyright 2023-2024, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

/*******************************************************************************
 * Include header files
 ********************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include <stdio.h>

/*******************************************************************************
 * Macros and Constants
 ********************************************************************************/
#define SET                         (1u)
#define RESET                       (!SET)
#define BUFFER_SIZE                 (128u)
#define MAX_COUNT                   (65536u)    /* 16 bit Counter */
#define CY_ASSERT_FAILED            (0u)

/*******************************************************************************
 * Function Prototypes
 ********************************************************************************/
static void isr_counter(void);

/*******************************************************************************
 * Global Variables
 ********************************************************************************/
cy_stc_scb_uart_context_t uart_context;
volatile uint8_t int_flag = RESET;
volatile uint32_t counter_overflow = RESET;
char buffer[BUFFER_SIZE];

/******************************************************************************
 * Switch interrupt configuration structure
 *******************************************************************************/
const cy_stc_sysint_t sysint_counter_cfg =
{
    /*.intrSrc =*/ COUNTER_IRQ,   /* Interrupt source */
    /*.intrPriority =*/ 3UL       /* Interrupt priority is 3 */
};

/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 * System entrance point. This function performs
 *  1. Initializes the BSP.
 *  2. This function initializes the peripherals to measure the frequency
 *     and display the result accordingly.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t freq = 0;
    uint32_t capture_val = 0;
    uint32_t capture_prev = 0;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board initialization failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable global interrupts */
    __enable_irq();

    /*
     * This project uses 3 TCPWM peripherals. One is set up as a Counter which is
     * used to count the rising edges. A second is also set up as a Counter which
     * generates 1 sec time window. The third is set up as a PWM for generating
     * different frequency signal and based on the Period and Compare value it
     * generates a signal with a particular frequency.
     */

    /* Initialize Counter with config set in Peripheral */
    if (CY_TCPWM_SUCCESS != Cy_TCPWM_Counter_Init(COUNTER_HW, COUNTER_NUM, &COUNTER_config))
    {
        CY_ASSERT (CY_ASSERT_FAILED);
    }

    /* Hook up and enable interrupt */
    result = Cy_SysInt_Init(&sysint_counter_cfg, isr_counter);
    if (result != CY_SYSINT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    NVIC_ClearPendingIRQ(sysint_counter_cfg.intrSrc);
    NVIC_EnableIRQ(sysint_counter_cfg.intrSrc);

    /* Initialize OneSecTimer with config set in Peripheral */
    if (CY_TCPWM_SUCCESS !=Cy_TCPWM_Counter_Init(ONE_SEC_TIMER_HW, ONE_SEC_TIMER_NUM, &ONE_SEC_TIMER_config))
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Initialize PWM_FreqGen with config set in Peripheral */
    if (CY_TCPWM_SUCCESS != Cy_TCPWM_PWM_Init(PWM_FREQ_GEN_HW, PWM_FREQ_GEN_NUM, &PWM_FREQ_GEN_config))
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable all Peripherals */
    Cy_TCPWM_Counter_Enable(COUNTER_HW, COUNTER_NUM);
    Cy_TCPWM_Counter_Enable(ONE_SEC_TIMER_HW, ONE_SEC_TIMER_NUM);
    Cy_TCPWM_PWM_Enable(PWM_FREQ_GEN_HW, PWM_FREQ_GEN_NUM);

    /* Triggers a software start on the selected TCPWMs */
    Cy_TCPWM_TriggerStart(COUNTER_HW, COUNTER_MASK);
    Cy_TCPWM_TriggerStart(ONE_SEC_TIMER_HW, ONE_SEC_TIMER_MASK);
    Cy_TCPWM_TriggerStart(PWM_FREQ_GEN_HW, PWM_FREQ_GEN_MASK);

    /* Initialize with config set in peripheral and enable the UART to display the result*/
    Cy_SCB_UART_Init(UART_HW, &UART_config, &uart_context);
    Cy_SCB_UART_Enable(UART_HW);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence to clear screen */
    Cy_SCB_UART_PutString(UART_HW, "\x1b[2J\x1b[;H");
    /* Display code example title */
    Cy_SCB_UART_PutString(UART_HW, "CE236702 - PSoC 4 - Frequency measurement using TCPWM\n\r");

    for (;;)
    {
        if (int_flag)    /* Interrupt was triggered, read the captured counter value now */
        {
            /* Get the Counter value */
            capture_val = Cy_TCPWM_Counter_GetCapture(COUNTER_HW, COUNTER_NUM);

            /* Calculate frequency */
            freq = counter_overflow * MAX_COUNT + capture_val - capture_prev;
            counter_overflow = RESET;

            capture_prev = capture_val;
            /* Clear the interrupt flag */
            int_flag = RESET;

            /* Display frequency */
            sprintf(buffer, "Frequency = %lu Hz \n\r", (unsigned long)freq);
            Cy_SCB_UART_PutString(UART_HW, buffer);
        }
    }
}

/*******************************************************************************
 * Function Name: isr_counter
 ********************************************************************************
 * Summary: Interrupt service routine for Counter interrupt. This function is
 * executed when a capture or an overflow event happens.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  None
 *******************************************************************************/
static void isr_counter(void)
{
    /* Get which event triggered the interrupt */
    uint32_t int_source = Cy_TCPWM_GetInterruptStatus(COUNTER_HW, COUNTER_NUM);

    /* Local variable used to count number of overflow events */
    static uint32_t overflow_count = 0;

    /* Clear interrupt */
    Cy_TCPWM_ClearInterrupt(COUNTER_HW, COUNTER_NUM, CY_TCPWM_INT_ON_CC_OR_TC);

    /* If the interrupt is triggered by capture event then set the flag for
     * frequency calculation
     */
    if (int_source == CY_TCPWM_INT_ON_CC)
    {
        /* Set interrupt flag */
        int_flag = SET;
        counter_overflow = overflow_count;
        overflow_count = RESET;
    }

    /* If the interrupt is triggered by an overflow event, then counting how
     * many times counter overflow happened in one second
     */
    if (int_source == CY_TCPWM_INT_ON_TC)
    {
        overflow_count++;
    }
}


/* [] END OF FILE */
