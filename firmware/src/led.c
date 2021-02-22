/*
    \file   led.c

    \brief  Manage board LED's

    (c) 2018 Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip software and any
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party
    license terms applicable to your use of third party software (including open source software) that
    may accompany Microchip software.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
    FOR A PARTICULAR PURPOSE.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
    SOFTWARE.
*/

#include <stdbool.h>
#include "led.h"
#include "debug_print.h"

led_status_t led_status;

#define LEDS_TEST_INTERVAL	50L
#define LED_100ms_INTERVAL  100L
#define LED_400ms_INTERVAL  400L
#define LED_ON_INTERVAL     200L
#define LEDS_HOLD_INTERVAL	2000L

void blink_task(uintptr_t context);
SYS_TIME_HANDLE blinkTimer_blue   = SYS_TIME_HANDLE_INVALID;
SYS_TIME_HANDLE blinkTimer_green  = SYS_TIME_HANDLE_INVALID;
SYS_TIME_HANDLE blinkTimer_yellow = SYS_TIME_HANDLE_INVALID;
SYS_TIME_HANDLE blinkTimer_red    = SYS_TIME_HANDLE_INVALID;

const char* debug_led_state[] =
{
    "Off",
    "On",
    "Blink(Fast)",
    "N/A",
    "Blink(Slow)"
};

// static bool ledForDefaultCredentials = false;
// static bool ledHeld = false;

void LED_MSDelay(uint32_t ms);

// void yellow_task(void);
// void red_task(void);
// void defaultCredentials_task(void);
// void softAp_task(void);

// SYS_TIME_HANDLE yellow_taskHandle   = SYS_TIME_HANDLE_INVALID;
// SYS_TIME_HANDLE red_taskHandle      = SYS_TIME_HANDLE_INVALID;
// SYS_TIME_HANDLE softAp_taskHandle   = SYS_TIME_HANDLE_INVALID;
// SYS_TIME_HANDLE defaultCredentials_taskHandle = SYS_TIME_HANDLE_INVALID;

// volatile bool yellow_taskTmrExpired     = false;
// volatile bool red_taskTmrExpired        = false;
// volatile bool softAp_taskTmrExpired     = false;
// volatile bool defaultCredentials_taskTmrExpired = false;

// void yellow_taskcb(uintptr_t context)
// {
//     yellow_taskTmrExpired = true;
// }
// void red_taskcb(uintptr_t context)
// {
//     red_taskTmrExpired = true;
// }
// void defaultCredentials_taskcb(uintptr_t context)
// {
//     defaultCredentials_taskTmrExpired = true;
// }
// void softAp_taskcb(uintptr_t context)
// {
//     softAp_taskTmrExpired = true;
// }

static void testSequence (uint8_t ledState)
{
    if(LED_OFF == ledState){
        LED_BLUE_SetHigh_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
        LED_GREEN_SetHigh_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
        LED_YELLOW_SetHigh_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
        LED_RED_SetHigh_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
    } else {
        LED_BLUE_SetLow_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
        LED_GREEN_SetLow_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
        LED_YELLOW_SetLow_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
        LED_RED_SetLow_EX();
        LED_MSDelay(LEDS_TEST_INTERVAL);
    }
}

void LED_test(void)
{
	testSequence(LED_ON);
	testSequence(LED_OFF);
}

void LED_init(void)
{
    led_status.change_flag.AsUSHORT = 0;
    led_status.state_flag.AsUSHORT = 0;
    led_status.state_flag.blue = LED_STATE_OFF;
    LED_BLUE_SetHigh_EX();
    led_status.state_flag.green = LED_STATE_OFF;
    LED_GREEN_SetHigh_EX();
    led_status.state_flag.yellow = LED_STATE_OFF;
    LED_YELLOW_SetHigh_EX();
    led_status.state_flag.red = LED_STATE_OFF;
    LED_RED_SetHigh_EX();
}

// void yellow_task(void)
// {
//    LED_YELLOW_SetHigh_EX();
//    ledHeld = false;
// }

// void red_task(void)
// {
// 	LED_RED_SetHigh_EX();
// }

// void softAp_task(void)
// {
//     LED_BLUE_Toggle();
// }

// void defaultCredentials_task(void)
// {
//     LED_GREEN_Toggle();
// }

// void LED_flashYellow(void)
// {
//     if (ledHeld == false)
//     {
//         LED_YELLOW_SetLow_EX();
//         yellow_taskHandle = SYS_TIME_CallbackRegisterMS(yellow_taskcb, 0, 200, SYS_TIME_SINGLE);
//     }
// }

// void LED_holdYellowOn(bool holdHigh)
// {
//     if (holdHigh == true)
//     {
//         LED_YELLOW_SetLow_EX();
//     }
//     else
//     {
//         LED_YELLOW_SetHigh_EX();
//     }
//     // Re-Use yellow_timer task
//     ledHeld = true;
//     yellow_taskHandle = SYS_TIME_CallbackRegisterMS(yellow_taskcb, 0, LEDS_HOLD_INTERVAL, SYS_TIME_SINGLE);
// }

// void LED_flashRed(void)
// {
//    LED_RED_SetLow_EX();
//    red_taskHandle = SYS_TIME_CallbackRegisterMS(red_taskcb, 0, LED_ON_INTERVAL, SYS_TIME_SINGLE);
// }

// void LED_blinkingBlue(bool amBlinking)
// {
//     if (amBlinking == true)
//     {
//         softAp_taskHandle = SYS_TIME_CallbackRegisterMS(softAp_taskcb, 0, LED_ON_INTERVAL, SYS_TIME_PERIODIC);
//     }
//     else
//     {
//         SYS_TIME_TimerStop(softAp_taskHandle);
//     }
// }

// void LED_startBlinkingGreen(void)
// {
//     defaultCredentials_taskHandle = SYS_TIME_CallbackRegisterMS(defaultCredentials_taskcb, 0, LED_ON_INTERVAL, SYS_TIME_PERIODIC);
//     ledForDefaultCredentials = true;
// }

// void LED_stopBlinkingGreen(void)
// {
//     if (ledForDefaultCredentials == true)
//     {
//         SYS_TIME_TimerStop(defaultCredentials_taskHandle);
//         ledForDefaultCredentials = false;
//     }
// }

// bool LED_isBlinkingGreen (void)
// {
//     return ledForDefaultCredentials;
// }

void LED_MSDelay(uint32_t ms)
{
    SYS_TIME_HANDLE tmrHandle = SYS_TIME_HANDLE_INVALID;

    if (SYS_TIME_SUCCESS != SYS_TIME_DelayMS(ms, &tmrHandle))
    {
        return;
    }

    while (true != SYS_TIME_DelayIsComplete(tmrHandle))
    {
    }
}

// void LED_sched(void)
// {
//     if(yellow_taskTmrExpired == true) {
//        yellow_taskTmrExpired = false;
//        yellow_task();
//     }
//     if(red_taskTmrExpired == true) {
//         red_taskTmrExpired = false; 
//         red_task();
//     }
//     if(softAp_taskTmrExpired == true) {
//         softAp_taskTmrExpired = false; 
//         softAp_task();
//     }
//     if(defaultCredentials_taskTmrExpired == true) {
//         defaultCredentials_taskTmrExpired = false; 
//         defaultCredentials_task();
//     }        
// }

void blink_task(uintptr_t context)
{
    led_number_t led = (led_number_t)context;

    switch (led)
    {
        case LED_BLUE:
            LED_BLUE_Toggle_EX(); /* toggle LED_BLUE output */
            break;
        case LED_GREEN:
            LED_GREEN_Toggle_EX(); /* toggle LED_GREEN output */
            break;
        case LED_YELLOW:
            LED_YELLOW_Toggle_EX(); /* toggle LED_YELLOW output */
            break;
        case LED_RED:
            LED_RED_Toggle_EX(); /* toggle LED_RED output */
            break;
    }

    return;
}

/*************************************************
*
* Blue LED
*
*************************************************/

void LED_SetBlue(led_set_state_t newState)
{
    debug_printInfo("LED(B): %s => %s", debug_led_state[led_status.state_flag.blue], debug_led_state[newState]);

    if (led_status.state_flag.blue == newState)
    {
        return;
    }

    switch ((int32_t)led_status.state_flag.blue)
    {
        case LED_STATE_OFF:
        case LED_STATE_HOLD:
            if ((newState & (LED_STATE_BLINK_FAST | LED_STATE_BLINK_SLOW)) != 0)
            {
                blinkTimer_blue = SYS_TIME_CallbackRegisterMS(blink_task, LED_BLUE, LED_ON_INTERVAL, SYS_TIME_PERIODIC);
            }

            break;

        case LED_STATE_BLINK_FAST:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_blue);
            }
            break;

        case LED_STATE_BLINK_SLOW:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_blue);
            }

            break;

        default:
            break;
    }

    if (newState == LED_STATE_HOLD)
    {
        LED_BLUE_SetLow_EX();
    }
    else if (newState == LED_STATE_OFF)
    {
        LED_BLUE_SetHigh_EX();
    }

    led_status.state_flag.blue = newState;
    led_status.change_flag.blue = 1;
}

/*************************************************
*
* Green LED
*
*************************************************/

void LED_SetGreen(led_set_state_t newState)
{
    debug_printInfo("LED(G): %s => %s", debug_led_state[led_status.state_flag.green], debug_led_state[newState]);

    if (led_status.state_flag.green == newState)
    {
        return;
    }

    switch ((int32_t)led_status.state_flag.green)
    {
        case LED_STATE_OFF:
        case LED_STATE_HOLD:
            if ((newState & (LED_STATE_BLINK_FAST | LED_STATE_BLINK_SLOW)) != 0)
            {
                blinkTimer_green = SYS_TIME_CallbackRegisterMS(blink_task, LED_GREEN, LED_ON_INTERVAL, SYS_TIME_PERIODIC);
            }

            break;

        case LED_STATE_BLINK_FAST:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_green);
            }
            break;

        case LED_STATE_BLINK_SLOW:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_green);
            }

            break;

        default:
            break;
    }

    if (newState == LED_STATE_HOLD)
    {
        LED_GREEN_SetLow_EX();
    }
    else if (newState == LED_STATE_OFF)
    {
        LED_GREEN_SetHigh_EX();
    }

    led_status.state_flag.green = newState;
    led_status.change_flag.green = 1;
}

/*************************************************
*
* Yellow LED
*
*************************************************/

void LED_SetYellow(led_set_state_t newState)
{
    debug_printInfo("LED(Y): %s => %s", debug_led_state[led_status.state_flag.yellow], debug_led_state[newState]);

    if (led_status.state_flag.yellow == newState)
    {
        return;
    }

    switch ((int32_t)led_status.state_flag.yellow)
    {
        case LED_STATE_OFF:
        case LED_STATE_HOLD:
            if ((newState & (LED_STATE_BLINK_FAST | LED_STATE_BLINK_SLOW)) != 0)
            {
                blinkTimer_yellow = SYS_TIME_CallbackRegisterMS(blink_task, LED_YELLOW, LED_ON_INTERVAL, SYS_TIME_PERIODIC);
            }

            break;

        case LED_STATE_BLINK_FAST:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_yellow);
            }
            break;

        case LED_STATE_BLINK_SLOW:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_yellow);
            }

            break;

        default:
            break;
    }

    if (newState == LED_STATE_HOLD)
    {
        LED_YELLOW_SetLow_EX();
    }
    else if (newState == LED_STATE_OFF)
    {
        LED_YELLOW_SetHigh_EX();
    }

    led_status.state_flag.yellow = newState;
    led_status.change_flag.yellow = 1;
}

/*************************************************
*
* Red LED
*
*************************************************/

void LED_SetRed(led_set_state_t newState)
{
    debug_printInfo("LED(R): %s => %s", debug_led_state[led_status.state_flag.red], debug_led_state[newState]);

    if (led_status.state_flag.red == newState)
    {
        return;
    }

    switch ((int32_t)led_status.state_flag.red)
    {
        case LED_STATE_OFF:
        case LED_STATE_HOLD:
            if ((newState & (LED_STATE_BLINK_FAST | LED_STATE_BLINK_SLOW)) != 0)
            {
                blinkTimer_red = SYS_TIME_CallbackRegisterMS(blink_task, LED_RED, LED_ON_INTERVAL, SYS_TIME_PERIODIC);
            }

            break;

        case LED_STATE_BLINK_FAST:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_red);
            }
            break;

        case LED_STATE_BLINK_SLOW:

            if (newState == LED_STATE_HOLD || newState == LED_STATE_OFF)
            {
                SYS_TIME_TimerStop(blinkTimer_red);
            }

            break;

        default:
            break;
    }

    if (newState == LED_STATE_HOLD)
    {
        LED_RED_SetLow_EX();
    }
    else if (newState == LED_STATE_OFF)
    {
        LED_RED_SetHigh_EX();
    }

    led_status.state_flag.red = newState;
    led_status.change_flag.red = 1;
}
