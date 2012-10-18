/**
 *
 * dcf77 module
 *
 * author: ondrejh dot ck at gmail dot com
 * date: 18.10.2012
 *
 * uses: timer A1 interrupt
 *       input
 *
 * it should do (todo):
 *      1ms strobing of input signal
 *      coarse synchronization of input char decoder
 *      input char decoding with fine sync. and hold over
 *      minute block decoding and verifiing
 *
 **/

/// include section
#include <msp430g2553.h>
#include <stdbool.h>
#include "dcf77.h" // self

// input init (pull up resistor)
//#define DCF77_INPUT_INIT() {P1DIR&=~BIT7;P1OUT|=BIT7;P1REN|=BIT7;}
#define DCF77_INPUT_INIT() {P1DIR&=~BIT7;P1OUT|=BIT7;}
// input reading (result true if input pulled down)
#define DCF77_INPUT() (((P1IN&BIT7)==0)?true:false)
// timer interval
#define DCF77_STROBE_TIMER_INTERVAL 125 // 1kHz (1MHz/8 input clock)

#define DCF77_LED 1
#if DCF77_LED
    #define DCF77_LED_INIT() {P1DIR|=0x01;P1OUT&=~0x01;}
    #define DCF77_LED_ON() {P1OUT|=0x01;}
    #define DCF77_LED_OFF() {P1OUT&=~0x01;}
    #define DCF77_LED_SWAP() {P1OUT^=0x01;}
#else
    #define DCF77_LED_INIT() {}
    #define DCF77_LED_ON() {}
    #define DCF77_LED_OFF() {}
    #define DCF77_LED_SWAP() {}
#endif

// symbol detection timing
#define DCF77_DETECT_PERIOD (int)1000
#define DCF77_S0_PERIOD (int)100
#define DCF77_S1_PERIOD (int)200
// fine synchronization offset (in dcf77 timer ticks)
#define DCF77_FINESYNC_OFFSET 10
// minimul quality of signal (out of 1000)
#define DCF77_MIN_SIGNAL_QUALITY 800
#define DCF77_MAX_HOLD_SYMBOLS 30

// dcf strobe variables
typedef enum {DCF77SYNC_COARSE,DCF77SYNC_FINE,DCF77SYNC_HOLD} dcf77_sync_mode_type;
dcf77_sync_mode_type dcf77_sync_mode = DCF77SYNC_COARSE;

typedef enum {DCF77_SYMBOL_NONE,DCF77_SYMBOL_0,DCF77_SYMBOL_1,DCF77_SYMBOL_MINUTE} dcf77_symbol_type;

// detector context type
typedef struct {
    int cnt; // counter
    dcf77_symbol_type sym; // last symbol buffer
    int sigQcnt,sigQ; // signal quality
    bool ready; // just detected flag

    int s0cnt,s1cnt,sMcnt; // symbol counters (very internal)
} dcf77_detector_context;

// function finding index and value of the biggest value from three values
int find_biggest(int val0, int val1, int val2, int *val)
{
    int i=0;
    int theMost = val0;
    if (val1>theMost) {theMost=val1;i=1;};
    if (val2>theMost) {theMost=val2;i=2;};
    *val = theMost;
    return i;
}

// reset dcf detector context
void dcf77_reset_context(dcf77_detector_context *detector,int offset)
{
    detector->cnt = offset;
    detector->sigQcnt = 0;
    detector->ready = false;
    detector->s0cnt = 0;
    detector->s1cnt = 0;
    detector->sMcnt = 0;
}

// dcf signal detect function
void dcf77_detect(dcf77_detector_context *detector, bool signal)
{
    // if cnt full, reset context
    if (detector->cnt>=DCF77_DETECT_PERIOD) dcf77_reset_context(detector,0);

    // test symbols
    if (detector->cnt<DCF77_S0_PERIOD) // logic 0 (<100ms)
    {
        if (signal==true)
        {
            detector->s0cnt++;
            detector->s1cnt++;
        }
        else
        {
            detector->sMcnt++;
        }
    }
    else if (detector->cnt<DCF77_S1_PERIOD) // logic 1 (<200ms)
    {
        if (signal==true)
        {
            detector->s1cnt++;
        }
        else
        {
            detector->s0cnt++;
            detector->sMcnt++;
        }
    }
    else // signal quality (rest of strobed signal)
    {
        detector->sigQcnt+=signal?0:1;
    }

    detector->cnt++;
    if (detector->cnt>=DCF77_DETECT_PERIOD) // it should be all
    {
        int symQ;
        int symI = find_biggest(detector->s0cnt,detector->s1cnt,detector->sMcnt,&symQ);
        // save overall signal quality value (symbol and pause)
        detector->sigQ = detector->sigQcnt+symQ;
        // decode symbol
        switch (symI)
        {
            case 0: detector->sym=DCF77_SYMBOL_0; break;
            case 1: detector->sym=DCF77_SYMBOL_1; break;
            case 2: detector->sym=DCF77_SYMBOL_MINUTE; break;
            default: detector->sym=DCF77_SYMBOL_NONE; break;
        }
        // rise it's ready flag
        detector->ready=true;
    }
}

// strobe function
void dcf77_strobe(void)
{
    // dcf detectors (3 for fine synchronization: sooner, now, later)
    static dcf77_detector_context detector[3];

    static bool last_dcf77sig = false;
    bool dcf77sig = DCF77_INPUT();
    int i;

    static int hold_counter = 0;


    // coarse synchronization
    if (dcf77_sync_mode == DCF77SYNC_COARSE)
    {
        if (detector[0].ready==true)
        {
            if ((detector[0].sym!=DCF77_SYMBOL_MINUTE)&&(detector[0].sigQ>=DCF77_MIN_SIGNAL_QUALITY))
            {
                dcf77_sync_mode=DCF77SYNC_FINE;
                DCF77_LED_ON();
            }
        }
        else
        {
            if ((dcf77sig!=last_dcf77sig)&&dcf77sig) // detect rising edge
            {
                // reset contexts
                dcf77_reset_context(&detector[0],+DCF77_FINESYNC_OFFSET);
                dcf77_reset_context(&detector[1],0);
                dcf77_reset_context(&detector[2],-DCF77_FINESYNC_OFFSET);
                //DCF77_LED_SWAP(); // debug
            }
        }
    }

    // detection and decoding
    for (i=0;i<3;i++) dcf77_detect(&detector[i],dcf77sig); // detection
    //if (detector[1].ready==true) dcf77_decode(detector[1].sym);

    // fine synchronization
    if (dcf77_sync_mode==DCF77SYNC_FINE)
    {
        if (detector[1].ready==true)
        {
            if (detector[1].sigQ<DCF77_MIN_SIGNAL_QUALITY)
            {
                dcf77_sync_mode=DCF77SYNC_HOLD;
                hold_counter=0;
            }
        }
    }

    // hold over
    if (dcf77_sync_mode==DCF77SYNC_HOLD)
    {
        if (detector[1].ready==true)
        {
            DCF77_LED_SWAP();
            if (detector[1].sigQ>=DCF77_MIN_SIGNAL_QUALITY)
            {
                if (detector[1].sym!=DCF77_SYMBOL_MINUTE)
                {
                    dcf77_sync_mode=DCF77SYNC_FINE;
                    DCF77_LED_ON();
                }
            }
            else
            {
                hold_counter++;
                if (hold_counter>DCF77_MAX_HOLD_SYMBOLS)
                {
                    dcf77_sync_mode=DCF77SYNC_COARSE;
                    DCF77_LED_OFF();
                }
            }
        }
    }

    // save last input (for coarse sync.)
    last_dcf77sig = dcf77sig;
}

/// module initialization function
// timer and input init
void dcf77_init(void)
{
    // input init
    DCF77_INPUT_INIT();
    DCF77_LED_INIT(); // debug led (en/dis by DCF77_LED macro value)
    //DCF77_LED_ON();

    // timer init
	TA1CCTL0 = CCIE;				// CCR0 interrupt enabled
	TA1CCR0 = DCF77_STROBE_TIMER_INTERVAL;
	TA1CTL = TASSEL_2 + MC_2 + ID_3;	// SMCLK, contmode, fosc/8
}

// Timer A1 interrupt service routine
#pragma vector=TIMER1_A0_VECTOR
__interrupt void Timer1 (void)
{
    TA1CCR0 += DCF77_STROBE_TIMER_INTERVAL;	// Add Offset to CCR0

    dcf77_strobe();
    //if (DCF77_INPUT()) {DCF77_LED_ON();}
    //else DCF77_LED_OFF();
}