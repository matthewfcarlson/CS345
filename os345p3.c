// os345p3.c - Jurassic Park
// ***********************************************************************
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// **                                                                   **
// ** The code given here is the basis for the CS345 projects.          **
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
#include <time.h>
#include <stdlib.h>
#include "os345.h"
#include "os345park.h"

// ***********************************************************************
// project 3 variables

// Jurassic Park
extern JPARK myPark;
extern Semaphore* parkMutex;						// protect park access
extern Semaphore* fillSeat[NUM_CARS];			// (signal) seat ready to fill
extern Semaphore* seatFilled[NUM_CARS];		// (wait) passenger seated
extern Semaphore* rideOver[NUM_CARS];			// (signal) ride over
extern Semaphore* deltaClockModify;
extern Semaphore* taskSems[MAX_TASKS];		// task semaphore

typedef struct delta_timer{
    Semaphore* trigger;
    int ticks_left;
    struct delta_timer* next_timer;
} DeltaTimer;

DeltaTimer* delta_timer = 0;
Semaphore* tickets;
Semaphore* parkOccupancy;
Semaphore* getTicketMutex;
Semaphore* needTicket;
Semaphore* wakeupDriver;
Semaphore* ticketReady;
Semaphore* buyTicket;
Semaphore* muesumOccupied;
Semaphore* driverDone;
Semaphore* riderDone[NUM_SEATS];
Semaphore* getPassenger;
Semaphore* seatTaken;
static TID timeTaskID;
extern int curTask;

// ***********************************************************************
// project 3 functions and tasks
void CL3_project3(int, char**);
void CL3_dc(int, char**);

int dcMonitorTask(int argc, char* argv[]);
int timeTask(int argc, char* argv[]);
void insertDeltaClock(int ticks, Semaphore* event);
void tickDeltaClock();
int JPcarTask(int argc, char* argv[]);



// ***********************************************************************
// ********************************************************************************************
int JPvisitorTask(int argc, char* argv[]){
    int visID = atoi(argv[1]);
    myPark.numOutsidePark++;
    while(1){
        insertDeltaClock(rand() % 100 + 10, taskSems[curTask]);SWAP;
        SEM_WAIT(taskSems[curTask]);SWAP;
        if (semTryLock(parkOccupancy)){
            SWAP;
            //Try and get a ticket
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInPark += 1;SWAP;
            myPark.numOutsidePark -= 1;SWAP;
            myPark.numInTicketLine++;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            // only 1 visitor at a time requests a ticket
            SEM_WAIT(getTicketMutex);		SWAP;
            {
                //printf("Player %d is buying a ticket\n",visID);
                // signal need ticket (produce, put hand up)
                SEM_SIGNAL(needTicket);		SWAP;
                
                // wakeup a driver (produce)
                SEM_SIGNAL(wakeupDriver);	SWAP;
                
                // wait for driver to obtain ticket (consume)
                SEM_WAIT(ticketReady);		SWAP;
                
                // buy ticket (produce, signal driver ticket taken)
                SEM_SIGNAL(buyTicket);		SWAP;
                SEM_WAIT(parkMutex);SWAP;
                myPark.numInTicketLine--;SWAP;
                myPark.numInMuseumLine++;SWAP;
                SEM_SIGNAL(parkMutex);SWAP;
            }
            // done (produce)
            SEM_SIGNAL(getTicketMutex);		SWAP;
           
            
            //Wait in line for muesum
            SEM_WAIT(muesumOccupied);SWAP;
            //let the park know we're in the museum
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInMuseum++;SWAP;
            myPark.numInMuseumLine--;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            //chill in the muesum for some amount of time
            insertDeltaClock(rand() % 50 + 10, taskSems[curTask]);SWAP;
            SEM_WAIT(taskSems[curTask]);SWAP;
            
            //get in the car line
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInMuseum--;SWAP;
            myPark.numInCarLine++;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            SEM_SIGNAL(muesumOccupied);
            
            //wait for a car to want the passenger
            SEM_WAIT(getPassenger);SWAP;
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInCars++;
            myPark.numInCarLine--;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            //let the car know we took the seat
            SEM_SIGNAL(seatTaken);SWAP;
            
           
           
            while(1) SWAP;
            return 0;
        }
        //printf("VISITOR %d is WAITING\n",visID);
        SWAP;
        
    }
    return 0;
}

int JPdriverTask(int argc, char* argv[]){
    int id = atoi(argv[1]) + 1;
    while(1){
        //wait for someone to wake up a driver
        SEM_WAIT(wakeupDriver);SWAP;
        //printf("WAKING UP DRIVER %d",id);
        if (semTryLock(needTicket)){
            //set your state
            SEM_WAIT(parkMutex);SWAP;
            myPark.drivers[id] = -1;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            //wait for a ticket to be available
            SEM_WAIT(tickets);SWAP;
            
            //modify the park
            SEM_WAIT(parkMutex);SWAP;
            myPark.numTicketsAvailable--;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            //let the customer know
            SEM_SIGNAL(ticketReady);
            
            //wait for them to buy it
            SEM_WAIT(buyTicket);SWAP;
            
            //modify the park
            SEM_WAIT(parkMutex);SWAP;
            myPark.drivers[id] = 0;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
        }
        SWAP;

    }
    return 0;
}
// Handles the cars
int JPcarTask(int argc, char* argv[])
{
    int carID = atoi(argv[1]);
    printf("Car: %d",carID);
    while(1){
        
        SEM_WAIT(fillSeat[carID]); 		SWAP;	// wait for available seat
        
        SEM_SIGNAL(getPassenger); 		SWAP;	// signal for visitor
        SEM_WAIT(seatTaken);	 		SWAP;	// wait for visitor to reply
        //SEM_SIGNAL(passengerSeated);	SWAP:	// signal visitor in seat
        
        //... save passenger ride over semaphore ...
        //
        
        // if last passenger, get driver
        {
            //SEM_WAIT(needDriverMutex);	SWAP;
            
            // wakeup attendant
            //SEM_SIGNAL(wakeupDriver);	SWAP;
            
            //... save driver ride over semaphore ...
            
            // got driver (mutex)
            //SEM_SIGNAL(needDriverMutex);	SWAP;
        }
        
        SEM_SIGNAL(seatFilled[carID]); 	SWAP;	// signal ready for next seat
        
        // if car full, wait until ride over
        SEM_WAIT(rideOver[carID]);		SWAP;
        
        //let the people know it's over
        SEM_SIGNAL(driverDone);
        for (int i=0;i<=3;i++)
            SEM_SIGNAL(riderDone[i]);
    }
    return 0;
} // end timeTask

// ***********************************************************************
// project3 command
int P3_project3(int argc, char* argv[])
{
	char buf[32];
    char buf2[32];
	char* newArgv[2];
    srand(time(NULL));

	// start park
	sprintf(buf, "jurassicPark");
	newArgv[0] = buf;
	createTask( buf,				// task name
		jurassicTask,				// task
		MED_PRIORITY,				// task priority
		1,								// task count
		newArgv);					// task argument

	// wait for park to get initialized...
	while (!parkMutex) SWAP;
	printf("\nStart Jurassic Park...");

	//?? create car, driver, and visitor tasks here
    tickets = createSemaphore("tickets", COUNTING, MAX_TICKETS);	SWAP;
    muesumOccupied = createSemaphore("muesumOccupy", COUNTING, MAX_IN_MUSEUM);	SWAP;
    getTicketMutex = createSemaphore("getTickets", BINARY, 1);	SWAP;
    needTicket = createSemaphore("needTicket", BINARY, 0);	SWAP;
    wakeupDriver = createSemaphore("wakeupDriver", BINARY, 0);	SWAP;
    ticketReady = createSemaphore("ticketReady", BINARY, 0);	SWAP;
    buyTicket = createSemaphore("buyTicket", BINARY, 0);	SWAP;
    driverDone = createSemaphore("driverDone", BINARY, 0);	SWAP;
    parkOccupancy = createSemaphore("parkOccupy", COUNTING, MAX_IN_PARK);	SWAP;
    seatTaken = createSemaphore("seatTaken", BINARY, 0);	SWAP;
    getPassenger = createSemaphore("getPassenger", BINARY, 0);	SWAP;
    
    for (int i=0;i<NUM_SEATS;i++){
        riderDone[i] = createSemaphore("riderDone", BINARY, 0);	SWAP;
        SWAP;
    }
    
    for (int i=0;i<NUM_CARS;i++){
        sprintf(buf, "parkCar:%d",i);
        sprintf(buf2, "%d",i);
        newArgv[0] = buf;
        newArgv[1] = buf2;
        createTask( buf,				// task name
                   JPcarTask,				// task
                   MED_PRIORITY,				// task priority
                   1,								// task count
                   newArgv);					// task argument
        SWAP;
    }
    for (int i=0;i<NUM_VISITORS;i++){
        sprintf(buf, "parkVis:%d",i);
        sprintf(buf2, "%d",i);
        newArgv[0] = buf;
        newArgv[1] = buf2;
        createTask( buf,				// task name
                   JPvisitorTask,				// task
                   MED_PRIORITY,				// task priority
                   1,								// task count
                   newArgv);					// task argument
        SWAP;
    }
    
    for (int i=0;i<NUM_DRIVERS;i++){
        sprintf(buf, "parkDriver:%d",i);
        sprintf(buf2, "%d",i);
        newArgv[0] = buf;
        newArgv[1] = buf2;
        createTask( buf,				// task name
                   JPdriverTask,				// task
                   MED_PRIORITY,				// task priority
                   1,								// task count
                   newArgv);					// task argument
        SWAP;
    }

	return 0;
} // end project3

void tickDeltaClock(){
    //We can assume we are in supermode so no need to wait
    //SEM_WAIT(deltaClockModify);
    if (delta_timer == 0){
        return;
    }
    //printf("Ticking delta clock %s\n",delta_timer->trigger->name);
    delta_timer->ticks_left -= 1;
    DeltaTimer* destroy_timer;
    while (delta_timer != 0 && delta_timer->ticks_left <= 0 ){
        SEM_SIGNAL(delta_timer->trigger);
        destroy_timer = delta_timer;
        delta_timer = delta_timer->next_timer;
        free(destroy_timer);
    }
    
}

//removes a semaphore from the delta clock
void deleteFromDeltaClock(Semaphore* event){
    //TODO: Deletes from the delta clock
    SEM_WAIT(deltaClockModify);
    printf("TODO: Delete from delta clock for %s\n",event->name);
    DeltaTimer* pointer = delta_timer;
    int delta = 0;
    while (delta_timer->trigger == event){
        delta += delta_timer->ticks_left;
        pointer = delta_timer;
        delta_timer = delta_timer->next_timer;
        free(pointer);
    }
    
    pointer = delta_timer;
    DeltaTimer* prev = pointer;
    
    pointer->ticks_left+= delta;
    delta = 0;
    while (pointer != 0){
        if (pointer->trigger == event){
            delta = pointer->ticks_left;
            prev->next_timer = pointer->next_timer;
            prev->next_timer->ticks_left += delta;
            
            free(pointer);
            pointer = prev;
        }
        prev = pointer;
        pointer = pointer->next_timer;
    }
    SEM_SIGNAL(deltaClockModify);
}

//creates a delta clock
void insertDeltaClock(int ticks, Semaphore* event){
    SEM_WAIT(deltaClockModify);
    DeltaTimer* new_timer = malloc(sizeof(DeltaTimer));
    //printf("\nCreating delta timer: %s at %d\n",event->name,ticks);

    new_timer->next_timer = 0;
    new_timer->ticks_left = ticks;
    new_timer->trigger = event;
    
    //if we have an empty list
    if (delta_timer == 0){
        delta_timer = new_timer;
        SEM_SIGNAL(deltaClockModify);
        return;
    }
    int delta = 0;
    DeltaTimer* pointer = delta_timer;
    //if we should add it first
    if (delta_timer->ticks_left >= ticks){
        new_timer->next_timer = delta_timer;
        delta_timer = new_timer;
        delta = ticks;
    }
    //if we should add it to the end of a one element list
    else if (delta_timer->next_timer == 0){
        new_timer->ticks_left -= delta_timer->ticks_left;
        delta_timer->next_timer = new_timer;
        
    }
    else{
        DeltaTimer* prev_timer = pointer;
        int current_tick_count = 0;
        while (pointer != 0 && current_tick_count + pointer->ticks_left <= ticks){
            prev_timer = pointer;
            current_tick_count += pointer->ticks_left;
            //printf("Checking %s, count: %d, prev: %s\n",pointer->trigger->name, current_tick_count, prev_timer->trigger->name);
            pointer = pointer->next_timer;
            
        }
        //printf("Inserting after %s\n",prev_timer->trigger->name);
        prev_timer->next_timer = new_timer;
        new_timer->ticks_left -= current_tick_count;
        delta = new_timer->ticks_left;
        new_timer->next_timer = pointer;
    }
    
    pointer = new_timer->next_timer;
    while (delta >= 0 && pointer != 0){
        if (pointer->ticks_left >= delta) {
            pointer->ticks_left -= delta;
            delta = 0;
        }
        else{
            delta -= pointer->ticks_left;
            pointer->ticks_left = 0;
            
        }
        pointer = pointer->next_timer;
    }
    SEM_SIGNAL(deltaClockModify);
    
    
}

// ***********************************************************************
// ***********************************************************************
// delta clock command
int P3_dc(int argc, char* argv[])
{
	printf("\nDelta Clock\n");
    DeltaTimer* current_timer = delta_timer;
    while(current_timer != 0){
        printf("%s: %i\n",current_timer->trigger->name,current_timer->ticks_left);
        current_timer = current_timer->next_timer;
        SWAP
    }
	return 0;
} // end CL3_dc





// ***********************************************************************
// test delta clock
int P3_tdc(int argc, char* argv[])
{
	createTask( "DC Test",			// task name
		dcMonitorTask,		// task
		10,					// task priority
		argc,					// task arguments
		argv);

	timeTaskID = createTask( "Time",		// task name
		timeTask,	// task
		10,			// task priority
		argc,			// task arguments
		argv);
	return 0;
} // end P3_tdc


extern Semaphore* tics1sec;
// ***********************************************************************
// monitor the delta clock task
int dcMonitorTask(int argc, char* argv[])
{
	int i, flg;
	char buf[32];
    Semaphore* event[10];
    // create some test times for event[0-9]
	int ttime[10] = {
		90, 300, 50, 170, 340, 300, 50, 300, 40, 110	};
    //8,  2,  6,  0,  9,   3,   1,   7,   5,    4
    //40, 50, 50, 90, 110, 170, 300, 300, 300,  340
    //Sould be 40, 10, 0, 40, 20, 60, 130, 0, 0, 40

	for (i=0; i<10; i++)
	{
		sprintf(buf, "event[%d]", i);
        event[i] = createSemaphore(buf, BINARY, 0);
		insertDeltaClock(ttime[i], event[i]);
	}
	P3_dc(0,0);

	while (delta_timer != 0)
	{
		SEM_WAIT(tics1sec)
		flg = 0;
		for (i=0; i<10; i++)
		{
			if (event[i]->state ==1)			{
					printf("\n  event[%d] signaled", i);
					event[i]->state = 0;
					flg = 1;
				}
		}
		if (flg) P3_dc(0,0);
	}
	printf("\nNo more events in Delta Clock");

	// kill dcMonitorTask
	//tcb[timeTaskID].state = S_EXIT;
	return 0;
} // end dcMonitorTask


// ********************************************************************************************
// display time every tics1sec
int timeTask(int argc, char* argv[])
{
	char svtime[64];						// ascii current time
	while (1)
	{
		SEM_WAIT(tics1sec)
		printf("\nTime = %s", myTime(svtime));
	}
	return 0;
} // end timeTask
