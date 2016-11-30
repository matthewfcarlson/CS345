// os345interrupts.c - pollInterrupts	08/08/2013
// ***********************************************************************
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// **                                                                   **
// ** The code given here is the basis for the BYU CS345 projects.      **
// ** It comes "as is" and "unwarranted."  As such, when you use part   **
// ** or all of the code, it becomes "yours" and you are responsible to **
// ** understand any algorithm or method presented.  Likewise, any      **
// ** errors or problems become your responsibility to fix.             **
// **                                                                   **
// ** NOTES:                                                            **
// ** -Comments beginning with "// ??" may require some implementation. **
// ** -Tab stops are set at every 3 spaces.                             **
// ** -The function API's in "OS345.h" should not be altered.           **
// **                                                                   **
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// ***********************************************************************

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>
#include <assert.h>

#include "os345.h"
#include "os345config.h"
#include "os345signals.h"

// **********************************************************************
//	local prototypes
//
void pollInterrupts(void);
static void keyboard_isr(void);
static void timer_isr(void);

// **********************************************************************
// **********************************************************************
// global semaphores

extern Semaphore* keyboard;				// keyboard semaphore
extern Semaphore* charReady;				// character has been entered
extern Semaphore* inBufferReady;			// input buffer ready semaphore

extern Semaphore* tics1sec;				// 1 second semaphore
extern Semaphore* tics10sec;				// 1 second semaphore
extern Semaphore* tics10thsec;				// 1/10 second semaphore

extern char inChar;				// last entered character
extern int charFlag;				// 0 => buffered input
extern int inBufIndx;				// input pointer into input buffer
extern char inBuffer[INBUF_SIZE+1];	// character input buffer

extern time_t oldTime1;					// old 1sec time
extern time_t oldTime10;					// old 1sec time
extern clock_t myClkTime;
extern clock_t myOldClkTime;

extern int pollClock;				// current clock()
extern int lastPollClock;			// last pollClock
extern void tickDeltaClock();

extern int superMode;						// system mode
extern char printBuffer[];

// Globals
const int NUM_COMMANDS_TO_TRACK = 10;
static char commandQueue[NUM_COMMANDS_TO_TRACK][INBUF_SIZE];
static int commandQueueLength[NUM_COMMANDS_TO_TRACK];
static int currentQueueSelector = 0;
static int lastCommandQueueIndex = 0;
static bool currentSelectingHistory = FALSE;
static int cursorIndex = 0;


// **********************************************************************
// **********************************************************************
// simulate asynchronous interrupts by polling events during idle loop
//
void pollInterrupts(void)
{
	// check for task monopoly
	pollClock = clock();
	assert("Timeout" && ((pollClock - lastPollClock) < MAX_CYCLES));
	lastPollClock = pollClock;

	// check for keyboard interrupt
	if ((inChar = GET_CHAR) > 0)
	{
	  keyboard_isr();
	}

	// timer interrupt
	timer_isr();
    
    //printf("%s",printBuffer);

	return;
} // end pollInterrupts

static void delete_line(){
    int i = 0;
    for (i=0;i< inBufIndx; i++){
        printf("\b");
    }
    for (i=0;i< inBufIndx; i++){
        printf(" ");
    }
    for (i=0;i< inBufIndx; i++){
        printf("\b");
    }
    //printf("\33[2K\r");
}

// Handles the up and down keys
static void history_previous(){
    int index = 0;
    
    if (currentSelectingHistory == FALSE){
        currentQueueSelector = lastCommandQueueIndex;
    }
    else{
        index = currentQueueSelector;
        if (currentQueueSelector == 0) currentQueueSelector = NUM_COMMANDS_TO_TRACK;
        currentQueueSelector --;
        if (currentQueueSelector == lastCommandQueueIndex) currentQueueSelector = index;
    }
    
    if (commandQueue[currentQueueSelector][0]){
        delete_line();
        //printf("\33[2K\r%s",commandQueue[currentQueueSelector]);
        printf("%s",commandQueue[currentQueueSelector]);
        inBufIndx = commandQueueLength[currentQueueSelector];
        cursorIndex = inBufIndx;
        strcpy(inBuffer,commandQueue[currentQueueSelector]);
    }
    
    
    currentSelectingHistory = TRUE;
}

static void history_next(){
    //int index = currentQueueSelector;
    if (currentSelectingHistory == TRUE && currentQueueSelector != lastCommandQueueIndex){
        currentQueueSelector ++;
        if (currentQueueSelector == NUM_COMMANDS_TO_TRACK) currentQueueSelector = 0;
        if (commandQueue[currentQueueSelector][0]){
            delete_line();
            //printf("\33[2K\r%s",commandQueue[currentQueueSelector]);
            printf("%s",commandQueue[currentQueueSelector]);
            inBufIndx = commandQueueLength[currentQueueSelector];
            cursorIndex = inBufIndx;
            strcpy(inBuffer,commandQueue[currentQueueSelector]);
        }

        
    }
    
}

static void history_add(){
    lastCommandQueueIndex ++;
    if (lastCommandQueueIndex == NUM_COMMANDS_TO_TRACK) lastCommandQueueIndex = 0;
    commandQueueLength[lastCommandQueueIndex] = inBufIndx;
    strcpy(commandQueue[lastCommandQueueIndex],inBuffer);
    currentSelectingHistory = FALSE;
}

static void history_print(){
    int i = 0;
    int index = lastCommandQueueIndex;
    for (i =0; i < NUM_COMMANDS_TO_TRACK; i++){
        printf("%d:\t%s\t(%d)\n",i,commandQueue[index],index);
        index ++;
        if (index == NUM_COMMANDS_TO_TRACK) index = 0;
    }
}

static void insert_char(char in){
    int i;
    if (cursorIndex>inBufIndx) cursorIndex = inBufIndx;
    
    
    
    printf("%c", in);		// echo character
    for (i=inBufIndx;i>=cursorIndex;i--){
        inBuffer[i+1] = inBuffer[i];
    }
    
    inBuffer[cursorIndex] = in;
    cursorIndex++;
    inBufIndx++;
    inBuffer[inBufIndx] = 0;
    
    for (i=cursorIndex;i<inBufIndx;i++) printf("%c",inBuffer[i]);
    for (i=cursorIndex;i<inBufIndx;i++) printf("\b");
    
    
    
}

extern bool diskMounted;					// disk has been mounted
extern int fmsAutcompleteFile(char* filename);
// **********************************************************************
// keyboard interrupt service routine
//
static void keyboard_isr()
{
    static int control_char = 0;
    int i;
    
	// assert system mode
	assert("keyboard_isr Error" && superMode);

	semSignal(charReady);					// SIGNAL(charReady) (No Swap)
	if (charFlag == 0)
	{
        if (control_char == 0)
        {
            switch (inChar)
            {
                case '\r':
                case '\n':
                {
                    history_add();
                    inBufIndx = 0;				// EOL, signal line ready
                    cursorIndex = 0;
                    semSignal(inBufferReady);	// SIGNAL(inBufferReady)
                    break;
                }
                case '\t':
                {
                    if (diskMounted){
                        //TODO: create autocomplete function
                        char filename_buffer[30];
                        int first_space = 0;
                        int autolength;
                        for (i=inBufIndx;i>=0 && first_space == 0;i--){
                            if (inBuffer[i] == ' ' && inBufIndx != i) first_space = i;
                        }
                        
                        int j = 0;
                        autolength = inBufIndx - first_space -1 ;
                        if (autolength == 0) break;
                        
                        
                        for (i=first_space+1; i < inBufIndx; i++){
                            filename_buffer[j] = inBuffer[i];
                            j++;
                        }
                        filename_buffer[j] = '*';
                        j++;
                        filename_buffer[j] = 0;
                        if (!fmsAutcompleteFile(filename_buffer)) break;
                        //erase the current input
                        for (i=0;i<autolength;i++) printf("\b");
                        printf("%s",filename_buffer);
                        j = 0;
                        for (i=first_space+1; i < 50; i++){
                            inBuffer[i] = filename_buffer[j];
                            if (inBuffer[i] == 0) break;
                            j++;
                        }
                    }
                    break;
                }
                
                case 33:
                case 127:
                case '\b':
                {
                    //backspace
                    if (cursorIndex > 0){
                        printf("\b");
                        cursorIndex--;
                        for (i = cursorIndex;i < inBufIndx; i++){
                            inBuffer[i] = inBuffer[i+1];
                            
                            if (inBuffer[i] == 0) printf(" ");
                            else printf("%c",inBuffer[i]);
                        }
                        for (i = cursorIndex;i < inBufIndx; i++) printf("\b");
                        
                        inBufIndx--;
                        
                    }
                    break;
                }

                case 0x18:						// ^x
                {
                    inBufIndx = 0;
                    cursorIndex = 0;
                    inBuffer[0] = 0;
                    sigSignal(0, mySIGINT);		// interrupt task 0
                    semSignal(inBufferReady);	// SEM_SIGNAL(inBufferReady)
                    break;
                }
                    
                case 0x17:						// ^W
                {
                    sigSignal(-1, mySIGTSTP);		// interrupt task 0
                    break;
                }
                    
                case 0x14:						// ^T
                {
                    P2_listTasks(1,0);
                    break;
                }
                case 0x11:
                case 0x10:						// ^P
                {
                    listQueues();
                    break;
                }
                    
                case 0x12:						// ^R
                {
                    
                    sigSignal(-1, mySIGCONT);		// interrupt task 0
                    break;
                }
                
                //if we get a weird key
                case 27:
                {
                    control_char = 1;
                    break;
                }

                default:
                {
                    if (inBufIndx < INBUF_SIZE){
                        insert_char(inChar);
                    }
                }
            }
            
		}
        else
        {
            //we have a control character
            if (control_char == 2){
                
                switch (inChar){
                    case 65:
                        //printf("Up arrow");
                        history_previous();
                        break;
                    case 66:
                        //printf("Down arrow");
                        history_next();
                        break;
                    case 68:
                        //left arrow
                        printf("\b");
                        if (cursorIndex > 0) cursorIndex--;
                        //history_print();
                        break;
                    case 67:
                        //printf("Right arrow");
                        printf("%c",inBuffer[cursorIndex]);
                        if (cursorIndex < inBufIndx) cursorIndex++;
                        
                        break;
                
                }
                control_char = 0;
            }
            else if (inChar == 91 && control_char == 1) control_char = 2;
            else control_char = 0;
        }
	}
	else
	{
        // single character mode
		inBufIndx = 0;
		inBuffer[inBufIndx] = 0;
	}
	return;
} // end keyboard_isr



// **********************************************************************
// timer interrupt service routine
//
static void timer_isr()
{
	time_t currentTime;						// current time

	// assert system mode
	assert("timer_isr Error" && superMode);

	// capture current time
  	time(&currentTime);

  	// one second timer
  	if ((currentTime - oldTime1) >= 1)
  	{
		// signal 1 second
  	   semSignal(tics1sec);
		oldTime1 += 1;
  	}

	// sample fine clock
	myClkTime = clock();
	if ((myClkTime - myOldClkTime) >= ONE_TENTH_SEC)
	{
		myOldClkTime = myOldClkTime + ONE_TENTH_SEC;   // update old
		semSignal(tics10thsec);
	}

	// ?? add other timer sampling/signaling code here for project 2
    
    // ten second timer
    if ((currentTime - oldTime10) >= 10)
    {
        // signal 10 second
        //printf("10 seconds at %d\n",oldTime10);
        semSignal(tics10sec);
        oldTime10 = currentTime;
    }


	return;
} // end timer_isr
