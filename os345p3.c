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
extern Semaphore* rideEmpty[NUM_CARS];			// (signal) ride over
extern Semaphore* deltaClockModify;
extern Semaphore* tics10thsec;
extern Semaphore* showPark;
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
Semaphore* giftOccupied;
Semaphore* driverDone[NUM_CARS];
Semaphore* riderDone;
Semaphore* getPassenger;
Semaphore* seatTaken;
Semaphore* getDriverMutex;
Semaphore* driverReadyToDrive;
Semaphore* needCarDriver;
Semaphore* needGiftCashier;
Semaphore* cashierAvailable;
Semaphore* cashierCheckedout;
Semaphore* driverLeftCar[NUM_CARS];

static TID timeTaskID;
extern int curTask;
static int carIDforDriver = 0;

// ***********************************************************************
// project 3 functions and tasks
void CL3_project3(int, char**);
void CL3_dc(int, char**);

int dcMonitorTask(int argc, char* argv[]);
int timeTask(int argc, char* argv[]);
void insertDeltaClock(int ticks, Semaphore* event);
int tickDeltaClockTask(int argc, char* argv[]);
int JPcarTask(int argc, char* argv[]);



// ***********************************************************************
// ********************************************************************************************
int JPvisitorTask(int argc, char* argv[]){
    //int visID = atoi(argv[1]);
    SEM_WAIT(parkMutex);SWAP;
    myPark.numOutsidePark++;SWAP;
    SEM_SIGNAL(parkMutex);SWAP;
    int waiting_ticks = 0;
    int last_tick_time = 0;
    while(1){
        
        //wait outside the park
        insertDeltaClock(rand() % 100 + 10, taskSems[curTask]); SWAP;
        SEM_WAIT(taskSems[curTask]);SWAP;
        
        
        if (semTryLock(parkOccupancy)){
            SWAP;
            //Try and get a ticket
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInPark += 1;SWAP;
            myPark.numOutsidePark -= 1;SWAP;
            myPark.numInTicketLine++;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            //wait in line
            insertDeltaClock(rand() % 50, taskSems[curTask]); SWAP;
            SEM_WAIT(taskSems[curTask]);SWAP;
            
            last_tick_time = myPark.ticks;
            // only 1 visitor at a time requests a ticket
            SEM_WAIT(getTicketMutex);		SWAP;
            {
                waiting_ticks += myPark.ticks - last_tick_time;
                last_tick_time = myPark.ticks;
                //printf("Player %d is buying a ticket\n",visID);
                // signal need ticket (produce, put hand up)
                SEM_WAIT(getDriverMutex); SWAP;
                {
                    SEM_SIGNAL(needTicket);		SWAP;
                    // wakeup a driver (produce)
                    SEM_SIGNAL(wakeupDriver);	SWAP;
                }
                SEM_SIGNAL(getDriverMutex); SWAP;
                
                // wait for driver to obtain ticket (consume)
                SEM_WAIT(ticketReady);		SWAP;
                
                // buy ticket (produce, signal driver ticket taken)
                SEM_SIGNAL(buyTicket);		SWAP;
                SEM_WAIT(parkMutex);SWAP;
                myPark.numInTicketLine--;SWAP;
                myPark.numInMuseumLine++;SWAP;
                waiting_ticks += myPark.ticks - last_tick_time;
                last_tick_time = myPark.ticks;
                SEM_SIGNAL(parkMutex);SWAP;
                
            }
            // done (produce)
            SEM_SIGNAL(getTicketMutex);		SWAP;
           
            
            
            //wait in line
            insertDeltaClock(rand() % 50, taskSems[curTask]); SWAP;
            SEM_WAIT(taskSems[curTask]);SWAP;
            
            //Wait in line for muesum
            last_tick_time = myPark.ticks;
            SEM_WAIT(muesumOccupied);SWAP;
            waiting_ticks += myPark.ticks - last_tick_time;
            last_tick_time = myPark.ticks;
            
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
            SEM_SIGNAL(muesumOccupied);SWAP;
            
            //just chill in the line
            insertDeltaClock(rand() % 50, taskSems[curTask]); SWAP;
            SEM_WAIT(taskSems[curTask]);SWAP;
            
            
            last_tick_time = myPark.ticks;
            //wait for a car to want the passenger
            SEM_WAIT(getPassenger);SWAP;
            SEM_WAIT(parkMutex);SWAP;
            waiting_ticks += myPark.ticks - last_tick_time;
            last_tick_time = myPark.ticks;
            myPark.numInCars++;SWAP;
            myPark.numInCarLine--;SWAP;
            myPark.numTicketsAvailable++;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            SEM_SIGNAL(tickets);SWAP;
            //let the car know we took the seat
            SEM_SIGNAL(seatTaken);SWAP;
            
            //wait for the ride to be over
            SEM_WAIT(riderDone);SWAP;
            
            //go to the gift shop line
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInCars--;SWAP;
            myPark.numInGiftLine++;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            //wait for the gift shop to be open
            last_tick_time = myPark.ticks;
            SEM_WAIT(giftOccupied);SWAP;
            waiting_ticks += myPark.ticks - last_tick_time;
            last_tick_time = myPark.ticks;
            
            
            //go in the gift shop
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInGiftShop++;SWAP;
            myPark.numInGiftLine--;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            
            //chill in the gift shop for some amount of time
            insertDeltaClock(rand() % 50 + 10, taskSems[curTask]);SWAP;
            SEM_WAIT(taskSems[curTask]);SWAP;
            
            last_tick_time = myPark.ticks;
            // signal need a cashier (produce, put hand up)
            SEM_WAIT(getDriverMutex); SWAP;
            {
                SEM_SIGNAL(needGiftCashier);		SWAP;
                // wakeup a driver (produce)
                SEM_SIGNAL(wakeupDriver);	SWAP;
            }
            SEM_SIGNAL(getDriverMutex); SWAP;
            
            SEM_WAIT(cashierAvailable);
            waiting_ticks += myPark.ticks - last_tick_time;
            last_tick_time = myPark.ticks;
            
            
            
            //Buy something from gift shop for some amount of time
            insertDeltaClock(rand() % 20, taskSems[curTask]);SWAP;
            SEM_WAIT(taskSems[curTask]);SWAP;
            
            SEM_WAIT(parkMutex);SWAP;
            //typical wait time is 0 to 85 ticks
            int spent_on_giftshop = rand()%3 + (100-waiting_ticks)/20;
            if (spent_on_giftshop < 1) spent_on_giftshop = 1;
            myPark.earnings += spent_on_giftshop;
            SEM_SIGNAL(parkMutex);SWAP;
  
            SEM_SIGNAL(cashierCheckedout);
            
            printf("Visitor is leaving and had to wait %d ticks total.\r\n",waiting_ticks);
            
            
            //leave the park
            SEM_WAIT(parkMutex);SWAP;
            myPark.numInGiftShop--;SWAP;
            myPark.numInPark--;SWAP;
            myPark.numExitedPark++; SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            SEM_SIGNAL(giftOccupied);SWAP;
            SEM_SIGNAL(parkOccupancy);SWAP;
            
            //go home
           
            return 0;
        }
        //printf("VISITOR %d is WAITING\n",visID);
        SWAP;
        
    }
    return 0;
}

int JPdriverTask(int argc, char* argv[]){
    int id = atoi(argv[1]);
    while(1){
        //wait for someone to wake up a driver
        SEM_WAIT(wakeupDriver);SWAP;
        
        //see what you need to do
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
            myPark.earnings += PRICE_TICKET;
            SEM_SIGNAL(parkMutex);SWAP;
            
            //let the customer know
            SEM_SIGNAL(ticketReady);SWAP;
            
            //wait for them to buy it
            SEM_WAIT(buyTicket);SWAP;
            
            //modify the park
            SEM_WAIT(parkMutex);SWAP;
            myPark.drivers[id] = 0;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            
        }
        //they need a car driver
        else if (semTryLock(needCarDriver)){
            
            SEM_WAIT(parkMutex);SWAP;
            myPark.drivers[id] = carIDforDriver + 1;SWAP;
            int carIndex = carIDforDriver;
            SEM_SIGNAL(parkMutex);SWAP;
            //printf("Driver %d is in the car index#%d\n",id+1, carIndex);
            SEM_SIGNAL(driverReadyToDrive); SWAP;
           
            
            //wait until you're done
            //printf("Driver %d waiting for car index#%d\n",id+1, carIndex);
            SEM_WAIT(driverDone[carIndex]);SWAP;
            
            
            SEM_WAIT(parkMutex);SWAP;
            myPark.drivers[id] = 0;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            //printf("Driver %d is out of the car index#%d\n",id+1, carIndex);
            SEM_SIGNAL(driverLeftCar[carIndex]); SWAP;
        }
        //they need a cashier
        else if (semTryLock(needGiftCashier)){
            SEM_WAIT(parkMutex);SWAP;
            //set to cashier mode
            myPark.drivers[id] = -2;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
            
            //printf("Driver #%i is cashier\n\r",id);
            SEM_SIGNAL(cashierAvailable);
            SEM_WAIT(cashierCheckedout);
            
            //go back to sleep
            SEM_WAIT(parkMutex);SWAP;
            myPark.drivers[id] = 0;SWAP;
            SEM_SIGNAL(parkMutex);SWAP;
        }
        //we don't know what we're supposed to do
        else{
            myPark.drivers[id] = 0;SWAP;
            
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
        //passenger 1
        SEM_WAIT(fillSeat[carID]); 		SWAP;	// wait for available seat
        
        SEM_SIGNAL(getPassenger); 		SWAP;	// signal for visitor
        SEM_WAIT(seatTaken);	 		SWAP;	// wait for visitor to reply
        SEM_SIGNAL(seatFilled[carID]); 	SWAP;	// signal ready for next seat
        
        //passenger 2
        SEM_WAIT(fillSeat[carID]); 		SWAP;	// wait for available seat
        
        SEM_SIGNAL(getPassenger); 		SWAP;	// signal for visitor
        SEM_WAIT(seatTaken);	 		SWAP;	// wait for visitor to reply
        SEM_SIGNAL(seatFilled[carID]); 	SWAP;	// signal ready for next seat
        
        //passenger 3
  
        SEM_WAIT(fillSeat[carID]); 		SWAP;	// wait for available seat
        
        SEM_SIGNAL(getPassenger); 		SWAP;	// signal for visitor
        SEM_WAIT(seatTaken);	 		SWAP;	// wait for visitor to reply//SEM_SIGNAL(passengerSeated);	SWAP:	// signal visitor in seat
        SEM_SIGNAL(seatFilled[carID]); 	SWAP;	// signal ready for next seat
        
        SEM_WAIT(getDriverMutex);SWAP;
        {
            
            
            SEM_SIGNAL(needCarDriver);	SWAP;
            carIDforDriver = carID;SWAP;
            SEM_SIGNAL(wakeupDriver);	SWAP;
            
            SEM_WAIT(driverReadyToDrive);SWAP;
            carIDforDriver = -3;SWAP;
            SEM_SIGNAL(seatFilled[carID]); 	SWAP;	// signal ready for next seat
        }
        SEM_SIGNAL(getDriverMutex); SWAP;

        
        SEM_WAIT(rideOver[carID]);		SWAP;
        //let the people know it's over
        SEM_SIGNAL(driverDone[carID]); SWAP;
        //printf("Car index %d has no driver\n",carID);
        SEM_WAIT(driverLeftCar[carID]); SWAP;
        
        //let the people out
        for (int i=0;i<=3;i++){
            SEM_SIGNAL(riderDone); SWAP;
        }
        SEM_SIGNAL(rideEmpty[carID]);
        //printf("Car %d is done\n",carID+1);
        SWAP;
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
    
    // start park
    sprintf(buf, "deltaTicker");
    newArgv[0] = buf;
    createTask( buf,				// task name
               tickDeltaClockTask,  // task
               HIGH_PRIORITY,				// task priority
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
    
    parkOccupancy = createSemaphore("parkOccupy", COUNTING, MAX_IN_PARK);	SWAP;
    seatTaken = createSemaphore("seatTaken", BINARY, 0);	SWAP;
    getPassenger = createSemaphore("getPassenger", BINARY, 0);	SWAP;
    getDriverMutex = createSemaphore("getADriver", BINARY, 1);	SWAP;
    driverReadyToDrive = createSemaphore("carDriverReady", BINARY, 0);	SWAP;
    needCarDriver = createSemaphore("needCarDriver", BINARY, 0);	SWAP;
    needGiftCashier = createSemaphore("needGiftCashier", BINARY, 0);	SWAP;
    riderDone = createSemaphore("riderDone", COUNTING, 0);	SWAP;
    cashierAvailable = createSemaphore("cashierAvailable", COUNTING, 0);	SWAP;
    cashierCheckedout = createSemaphore("cashierCheckedout", COUNTING, 0);	SWAP;
    
    giftOccupied = createSemaphore("giftOccupy", COUNTING, MAX_IN_GIFTSHOP);	SWAP;
    
    for (int i=0;i<NUM_CARS;i++){
        sprintf(buf, "driverLeft:%d",i);
        driverLeftCar[i] = createSemaphore(buf, BINARY, 0);	SWAP;
        sprintf(buf, "driverDone:%d",i);
        driverDone[i] = createSemaphore(buf, BINARY, 0);	SWAP;
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

int tickDeltaClockTask(int argc, char* argv[]){
    while(1){
        SEM_WAIT(tics10thsec);
        SEM_WAIT(deltaClockModify);
        if (delta_timer != 0){
            delta_timer->ticks_left -= 1;
            DeltaTimer* destroy_timer;
            while (delta_timer != 0 && delta_timer->ticks_left <= 0 ){
                SEM_SIGNAL(delta_timer->trigger);
                destroy_timer = delta_timer;
                delta_timer = delta_timer->next_timer;
                free(destroy_timer);
            }
            
        }
        //printf("Ticking delta clock %s\n",delta_timer->trigger->name);
        SEM_SIGNAL(deltaClockModify);
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



// ***********************************************************************
// test delta clock
int P3_checkPark(int argc, char* argv[])
{
    SEM_SIGNAL(showPark);
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
