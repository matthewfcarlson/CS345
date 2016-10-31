// os345mmu.c - LC-3 Memory Management Unit	03/12/2015
//
//		03/12/2015	added PAGE_GET_SIZE to accessPage()
//
// **************************************************************************
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
#include <assert.h>
#include "os345.h"
#include "os345lc3.h"
#include <unistd.h>

#define DEBUG_SWAP 0
#define DEBUG_MULTI 0
#define DEBUG_CLOCK 0
#define DEBUG_PAGE 0

// ***********************************************************************
// mmu variables

//the status of a frame table entry
typedef enum frameEntryStatus{FRAME_EMPTY, DATA_FRAME, UPT_FRAME} FrameEntryStatus;
//this is the struct that keeps track of a frame entry
typedef struct frameEntry{
    FrameEntryStatus status;
    int entryAddress;
} FrameEntry;

// LC-3 memory
unsigned short int memory[LC3_MAX_MEMORY];
FrameEntry frameTable[LC3_FRAMES]; //this keeps track of each entry in the frame table

// statistics
int memAccess;						// memory accesses
int memHits;						// memory hits
int memPageFaults;					// memory faults
int totalFrames = 512;

int getFrame(int);
int getAvailableFrame(void);
extern TCB tcb[];					// task control block
extern int curTask;					// current task #
int clockIndex;
int MMUdebugMode = 0;
int clockRan = 0;
int lastFramePaged = 0;
int lastPageOut = -1;
int lastPageIn = -1;

extern jmp_buf reset_context;


//this function evaluates whether a user page table is empty or not
int canPageOutUserFrameTable(int upta){
    int upt_index, upte;
    //check all 32 entries
    for (upt_index = 0; upt_index < 64; upt_index+=2){
        upte = MEMWORD(upta+upt_index);
        //we found an entry with something in memory so we can't page it out
        if (DEFINED(upte)) return 0;
    }
    //we didn't find anything
    return 1;
}

void outputFrame(int frame){
    int address = frame << 6;
    int value = 0;
    for (int i=0;i < 64; i+= 8){
        
        printf("\n%4x: ",address+i);
        for (int j=0;j<8;j++){
            value = memory[address+i+j];
            printf(" %4x",value);
        }
    }
}

//this function outputs the current frame
void outputFrameTable(){
    int frame, entry;
    printf("\nFrame Table: Clock (%d/%d)",clockIndex,totalFrames);
    for (frame = 0; frame<LC3_FRAMES;frame++){
        if (frameTable[frame].status == FRAME_EMPTY && (frame < 192 || frame >= totalFrames)) continue;
        if (frameTable[frame].status == FRAME_EMPTY) {
            printf("\n %4d : Empty",frame);
            continue;
        }
        printf("\n%c%4d = Entry Address:0x%0x",(clockIndex==frame)?'>':' ',frame,frameTable[frame].entryAddress);
        if ((frameTable[frame].entryAddress>>6) >= 192)
            printf("(%3d) = ",frameTable[frame].entryAddress>>6);
        else
            printf("(T%2d) = ",(frameTable[frame].entryAddress>>6) - 144);
        
        switch(frameTable[frame].status){
            case UPT_FRAME: printf("UPT Frame ");break;
            case DATA_FRAME: printf("DataFrame ");break;
            default: break;
        }
        entry = MEMWORD(frameTable[frame].entryAddress);
        
        printf(REFERENCED(entry)?"R":"-");
        printf(PINNED(entry)?"P":"-");
        printf(DIRTY(entry)?"D":"-");
        entry = MEMWORD(frameTable[frame].entryAddress+1);
        if (DEFINED(entry))
            printf(" Page: %d",PAGE(entry));
    }
    printf("\n\n");
}

//outputs the RPT entries and
void outputPageTables(int tid){
    int rpt,upt;
    printf("\nClock status for tid: %d\n",tid);
    for (rpt = 0; rpt < 64; rpt+=2)
    {
        if (MEMWORD(rpt+tcb[tid].RPT) || MEMWORD(rpt+tcb[tid].RPT+1))
        {
            int RPT_addr = (rpt/2)<< 11;
            printf("(%0x to %0x)",RPT_addr, RPT_addr + (1<<11)-1);
            outPTE(" RPT=  ", rpt+tcb[tid].RPT);

            
            for(upt = 0; upt < 64; upt+=2)
            {
                int frame = FRAME(MEMWORD(rpt+tcb[tid].RPT));
                if (frame > totalFrames || frame < 192){
                    printf("\nBAD FRAME %d", frame);
                    longjmp(reset_context, POWER_DOWN_ERROR);
                }
                
                if (DEFINED(MEMWORD(rpt+tcb[tid].RPT)) &&
                    (DEFINED(MEMWORD((FRAME(MEMWORD(rpt+tcb[tid].RPT))<<6)+upt))
                     || PAGED(MEMWORD((FRAME(MEMWORD(rpt+tcb[tid].RPT))<<6)+upt+1))))
                {
                    
                    printf((clockIndex == (FRAME(MEMWORD(rpt+tcb[tid].RPT))<<6)+upt)?">":" ");
                    printf("(%0x to %0x)",RPT_addr + ((upt/2)<<6), RPT_addr + ((upt/2+1)<<6)-1);
                    
                    outPTE("  UPT=", (FRAME(MEMWORD(rpt+tcb[tid].RPT))<<6)+upt);
                }
            }
        }
    }
    printf("\n");

}


//write the page out data
//returns the page number
int pageDataFrameOut(int upta){

    int upte1,upte2,pageNumber,frame;
    
    upte1 = MEMWORD(upta);
    upte2 = MEMWORD(upta+1);
    frame = FRAME(upte1);
    pageNumber = PAGE(upte2);
    
    
    if (DEFINED(upte1)){
        //if we already have a entry in our swap space and it's dirty
        if (PAGED(upte2)){ //&& DIRTY(upte1) ){
            pageNumber = accessPage(pageNumber, frame, PAGE_OLD_WRITE);
            if (MMUdebugMode||DEBUG_SWAP) printf("Paging old frame %d to page %d\n",frame,pageNumber);
            
        }
        //otherwise we have a new page write
        else if (!DEFINED(upte2)){
            pageNumber = accessPage(0, frame, PAGE_NEW_WRITE);
            upte2 = SET_PAGED(pageNumber);
            if (MMUdebugMode||DEBUG_SWAP) printf("Paging new frame %d to page %d\n",frame,pageNumber);
        }
        lastPageOut = pageNumber;
        
        //update the user page table entry
        MEMWORD(upta) = 0;
        MEMWORD(upta+1) =  SET_PAGED(upte2);
    }
    else{
        printf("Write out to swap failed for %x\n",upta);
        return -1;
    }
    
    //clear the data in the frame
    int frameAddress = frame << 6;
    for (int i=0;i<64;i++){
        MEMWORD(frameAddress+i) = 0;
    }

    
    lastFramePaged = frame;
    
    
    return pageNumber;
}

//returns a true if viable
//returns a false if not viable
int checkPageTableEntry(int index,int mark){
    int rpta = index;
    int rpte1 = MEMWORD(rpta);
    
    //make sure we count this access
    memAccess++;
    //check whether it's in memory
    if (!DEFINED(rpte1))	{ // rpte defined
        //printf("%x is undefined \n",index);
        return 0;
    }
    
    //mark it
    if (mark) MEMWORD(rpta) = CLEAR_REF(rpte1);
    
    if (PINNED(rpte1)){
       // printf("%x is pinned\n",index);
        return 0;
    }
    else if (REFERENCED(rpte1)){ //if it's in use
        //printf("%x is referenced\n",index);
        return 0;
    }
    //otherwise it's not referenced and good to be used
    return 1;
    
}

void advanceClock(){
    clockIndex = (clockIndex+1)%totalFrames;
}

//this function gets the first available frame
//sends stuff out to swap space if we need to
int getFrame(int notme)
{
    //int rpta,rpte,upt,upta,upte,rpt;
    int entryAddress;
    int status = 0;
    int clockCycles = 0;
    int frameAddress;
    int freeFrame;
	int frame;
    
    clockRan = 0;
	
    //try and get a frame
    frame = getAvailableFrame();
	if (frame >= 0) return frame;

	// run clock
    clockRan = 1;
    
    
    if (MMUdebugMode || DEBUG_CLOCK) {
        printf("\nRUNNING CLOCK for %d",curTask);
        outputFrameTable();
        outputPageTables(curTask);
        
    }
    
    //manage the clock
    entryAddress = frameTable[clockIndex].entryAddress;
    clockCycles = 0;
    status = 0;
    if (clockIndex == notme || frameTable[clockIndex].status == FRAME_EMPTY || !checkPageTableEntry(entryAddress, 1)){
        //if we aren't already on a good address.
        while (status == 0 && clockCycles <= (LC3_FRAMES * 2)){
            advanceClock();
            if (clockIndex != notme && frameTable[clockIndex].status != FRAME_EMPTY){
                entryAddress = frameTable[clockIndex].entryAddress;
                if (checkPageTableEntry(entryAddress, 1)) status = 1;
            }
            clockCycles++;
        }
    }
    if (clockCycles >= LC3_FRAMES*2){
        printf("\nERROR: We can't find a frame to replace\n");
        outputFrameTable();
        outputPageTables(curTask);
        longjmp(reset_context, POWER_DOWN_ERROR);
        
    }
    
    frame = clockIndex;
    entryAddress = frameTable[frame].entryAddress;
    
    if (frame < 192 || frame >= totalFrames){
        printf("\nBad Clock Frame %d %d",frame,status);
        outputFrameTable();
        outputPageTables(curTask);
        return -1;
        
    }
    
    
    //page out the data
    if (pageDataFrameOut(entryAddress) == -1){
        printf("\nFailure to page out");
        return -1;
    }
    
    //if we're paging out a data frame
    
    if (frameTable[frame].status == DATA_FRAME){
        //get the number and address of frame we're pointed at
        freeFrame = frameTable[frame].entryAddress >> 6;
        frameAddress = freeFrame << 6;

        //if we can page out that table
        if (canPageOutUserFrameTable(frameAddress)){
            entryAddress = frameTable[freeFrame].entryAddress;
            //unpin the table
            MEMWORD(entryAddress) = CLEAR_PINNED(MEMWORD(entryAddress));
            if (MMUdebugMode) printf("Page Table at %d can be paged out. Setting entry at %x to %x\n",freeFrame, entryAddress,MEMWORD(entryAddress));
            
        }
    }
    
    if (frameTable[frame].status == UPT_FRAME && DEBUG_SWAP){
        printf("\nPaging out frame %d which is a user page table ",frame);
    }
    
    if (MMUdebugMode || DEBUG_SWAP || DEBUG_PAGE) printf("\nSetting %d frame to empty",frame);
    frameTable[frame].status = FRAME_EMPTY;
    
    if (frame == notme){
        printf("Trying to page out the notme frame %d\nFATAL ERROR",notme);
        longjmp(reset_context, POWER_DOWN_ERROR);
        //exit(0);
    }
    
    
    //increment the clock
    advanceClock();
    if (MMUdebugMode) outputFrameTable();
    if (MMUdebugMode) outputPageTables(curTask);
    //printf("\n");
    

	return frame;
}

// **************************************************************************
// **************************************************************************
// LC3 Memory Management Unit
// Virtual Memory Process
// **************************************************************************
//           ___________________________________Frame defined
//          / __________________________________Dirty frame
//         / / _________________________________Referenced frame
//        / / / ________________________________Pinned in memory
//       / / / /     ___________________________
//      / / / /     /                 __________frame # (0-1023) (2^10)
//     / / / /     /                 / _________page defined
//    / / / /     /                 / /       __page # (0-4096) (2^12)
//   / / / /     /                 / /       /
//  / / / /     / 	             / /       /
// F D R P - - f f|f f f f f f f f|S - - - p p p p|p p p p p p p p

#define MMU_ENABLE	TRUE

unsigned short int *getMemAdr(int va, int rwFlg)
{
	int rpta, rpte1, rpte2;
	int upta, upte1, upte2;
	int dataFrame, uptFrame;
    int didhit = 0;
    
    // turn off virtual addressing for system RAM
	if (va < 0x3000) return &memory[va];
    
    if (DEBUG_MULTI && curTask == 2) MMUdebugMode = 1;
    
    /*if ((va>>6) == (0xd593>>6) && curTask == 3){
        printf("\nWriting value to 0x%04x for tid: %d",va,curTask);
        
        MMUdebugMode = 1;
    }*/
    

    memAccess++;
	rpta = tcb[curTask].RPT + RPTI(va);		// root page table address
	rpte1 = memory[rpta];					// FDRP__ffffffffff
	rpte2 = memory[rpta+1];					// S___pppppppppppp
    if (DEFINED(rpte1))	{ // rpte defined
        //rpte defined
        memHits++;
        rpte1 = SET_DIRTY(rpte1);
    }
    else{ // rpte undefined
        // 1. get a UPT frame from memory (may have to free up frame)
        // 2. if paged out (DEFINED) load swapped page into UPT frame
        // else initialize UPT
        
        uptFrame = getFrame(-1);
        if (MMUdebugMode) printf("\nGetting frame %d for User Page Table", uptFrame);
        rpte1 = SET_DEFINED(uptFrame);
        if (PAGED(rpte2))	// UPT frame paged out - read from SWAPPAGE(rpte2) into frame
        {
            memPageFaults++;
            if (MMUdebugMode || DEBUG_PAGE) printf("\nPaging table page %d in to frame:%d",SWAPPAGE(upte2),dataFrame);
            accessPage(SWAPPAGE(rpte2), uptFrame, PAGE_READ);
            lastPageIn = uptFrame;
            rpte1 = SET_DIRTY(rpte1);
        }
        else	// define new upt frame and reference from rpt
        {
            rpte2 = 0;
            if (rwFlg == 1) rpte1 = SET_DIRTY(rpte1);
            // undefine all upte's
        }
        
        if (frameTable[uptFrame].status != FRAME_EMPTY){
            
            printf("\nNon empty frame %d is being used for page table! ",uptFrame);
            outputFrameTable();
            outputPageTables(curTask);
            longjmp(reset_context, POWER_DOWN_ERROR);
        }
        //check to make sure no other entry has it
        for (int rpt=0;rpt<64;rpt+=2){
            int check_data = MEMWORD(tcb[curTask].RPT+rpt);
            if (DEFINED(check_data) && FRAME(check_data) == uptFrame){
                printf("\nWe are already using this frame %d!",uptFrame);
                outputFrameTable();
                outputPageTables(curTask);
                longjmp(reset_context, POWER_DOWN_ERROR);
            }
        }
        
        //update the frame Table
        frameTable[uptFrame].status = UPT_FRAME;
        frameTable[uptFrame].entryAddress = rpta;


    
    }
    uptFrame = FRAME(rpte1);
    //get the frame number
    if (uptFrame < 192 || uptFrame >= totalFrames){
        printf("\nBad Page Table Frame %d",uptFrame);
        printf("\nGetting memory address for VA: %x, RP: %x, RPTI: %d, UPTI: %02x, rwFlag:%d, Frame:%x, PA:%x\n",va,tcb[curTask].RPT,RPTI(va),UPTI(va),rwFlg,FRAME(upte1),(FRAME(upte1)<<6) + FRAMEOFFSET(va));
        outputFrameTable();
        outputPageTables(curTask);
        printf("\n\nBAD FRAME\n");
        usleep(1000);
        longjmp(reset_context, POWER_DOWN_ERROR);
        //exit(0);
    }

    
    
    
    //update the root page table entries
    MEMWORD(rpta) = rpte1 = SET_DIRTY(SET_PINNED(SET_REF(rpte1)));	// set rpt frame access bit
    MEMWORD(rpta+1) = rpte2;
    
    //User page table entry
    memAccess++;
    upta = (FRAME(rpte1)<<6) + UPTI(va);
    upte1 = MEMWORD(upta);
    upte2 = MEMWORD(upta+1);
    if (DEFINED(upte1)){	// upte defined
        memHits++;
        upte1 = SET_DIRTY(upte1);
        didhit = 1;
    }
    else {	// upte undefined
        didhit = 0;
        // 1. get a physical frame (may have to free up frame) (x3000 - limit) (192 - 1023)

        dataFrame = getFrame(uptFrame); //but not the root page table
        if (MMUdebugMode||DEBUG_PAGE) printf("\nGetting frame %d for data",dataFrame);
        upte1 = SET_DEFINED(dataFrame);
        // 2. if paged out (DEFINED) load swapped page into physical frame
        if (PAGED(upte2))	// UPT frame paged out - read from SWAPPAGE(rpte2) into frame
        {
            memPageFaults++;
            if (MMUdebugMode || DEBUG_PAGE) printf("\nPaging data page %d in to frame:%d",SWAPPAGE(upte2),dataFrame);
            accessPage(SWAPPAGE(upte2), dataFrame, PAGE_READ);
            lastPageIn = uptFrame;
        }
        else	// define new upt frame and reference from rpt
        {
            upte2 = 0;
            if (MMUdebugMode) printf("\nCreating data frame %d,%x for VA: %x",dataFrame,dataFrame<<6, va);
            // undefine all upte's
        }
        
        if (frameTable[dataFrame].status != FRAME_EMPTY){
            
            printf("\nNon empty frame %d is being used for data!",dataFrame);
            outputFrameTable();
            outputPageTables(curTask);
            longjmp(reset_context, POWER_DOWN_ERROR);
        }
        
        //update the frame Table
        frameTable[dataFrame].status = DATA_FRAME;
        frameTable[dataFrame].entryAddress = upta;
        
       
     
    }
    
    //get the frame number
    dataFrame = FRAME(upte1);
    if (dataFrame < 192 || dataFrame >= totalFrames){
        printf("\nBad Data Frame %d from UPT frame %d for task %d",dataFrame,uptFrame,curTask);
        printf("\nClock: %c Hit: %c Last Frame Paged: %d LastPageOut: %d LastPageIn:%d",(clockRan)?'Y':'N', (didhit)?'Y':'N', lastFramePaged,lastPageOut, lastPageIn);
        printf("\nGetting memory address for VA: %x, RP: %x, RPTI: %d, UPTI: %02x, rwFlag:%d, Frame:%d, PA:%x\n",va,tcb[curTask].RPT,RPTI(va),UPTI(va),rwFlg,FRAME(upte1),(FRAME(upte1)<<6) + FRAMEOFFSET(va));
        outputFrameTable();
        outputPageTables(curTask);
        printf("\n\nBAD FRAME\n");
        
        longjmp(reset_context, POWER_DOWN_ERROR);
        //exit(0);
    }

    //update the user page table entry
    MEMWORD(upta) = upte1 = SET_DIRTY(SET_REF(upte1));	// set upt frame access bit
    MEMWORD(upta+1) = upte2;
    
    //printf("Getting memory address for VA: %x, RP: %x, RPTI: %d, UPTI: %02x, rwFlag:%d, Frame:%x, PA:%x\n",va,tcb[curTask].RPT,RPTI(va),UPTI(va),rwFlg,FRAME(upte1),(FRAME(upte1)<<6) + FRAMEOFFSET(va));
    
    //we know we have a hit here so get the memAccess and hits
    memAccess++;
    memHits++;
    
    if (DEBUG_MULTI && curTask == 2) MMUdebugMode = 0;
    
    
    return &memory[(FRAME(upte1)<<6) + FRAMEOFFSET(va)];	// return physical address}

} // end getMemAdr


// **************************************************************************
// **************************************************************************
// set frames available from sf to ef
//    flg = 0 -> clear all others
//        = 1 -> just add bits
//
void setFrameTableBits(int flg, int sf, int ef)
{	int i, data = 0;
	int adr = LC3_FBT-1;             // index to frame bit table
	int fmask = 0x0001;              // bit mask

	// 1024 frames in LC-3 memory
	for (i=0; i<LC3_FRAMES; i++)
	{	if (fmask & 0x0001)
		{  fmask = 0x8000;
			adr++;
			data = (flg)?MEMWORD(adr):0;
		}
		else fmask = fmask >> 1;
		// allocate frame if in range
		if ( (i >= sf) && (i < ef)) data = data | fmask;
		MEMWORD(adr) = data;
	}
	return;
} // end setFrameTableBits


// **************************************************************************
// get frame from frame bit table (else return -1)
int getAvailableFrame()
{
	int i, data = 0;
	int adr = LC3_FBT - 1;				// index to frame bit table
	int fmask = 0x0001;					// bit mask

	for (i=0; i<LC3_FRAMES; i++)		// look thru all frames
	{	if (fmask & 0x0001)
		{  fmask = 0x8000;				// move to next work
			adr++;
			data = MEMWORD(adr);
		}
		else fmask = fmask >> 1;		// next frame
		// deallocate frame and return frame #
		if (data & fmask)
		{  MEMWORD(adr) = data & ~fmask;
			return i;
		}
	}
	return -1;
} // end getAvailableFrame



// **************************************************************************
// read/write to swap space
int accessPage(int pnum, int frame, int rwnFlg)
{
	static int nextPage;						// swap page size
	static int pageReads;						// page reads
	static int pageWrites;						// page writes
    static unsigned short int swapMemory[LC3_MAX_SWAP_MEMORY];
    int i;

	if ((nextPage >= LC3_MAX_PAGE) || (pnum >= LC3_MAX_PAGE))
	{
		printf("\nVirtual Memory Space Exceeded!  (%d)", LC3_MAX_PAGE);
		exit(-4);
	}
	switch(rwnFlg)
	{
		case PAGE_INIT:                    		// init paging
			memAccess = 0;						// memory accesses
			memHits = 0;						// memory hits
			memPageFaults = 0;					// memory faults
			nextPage = 0;						// disk swap space size
			pageReads = 0;						// disk page reads
            pageWrites = 0;						// disk page writes
            totalFrames = frame + 192;
            for (i = 0;i<LC3_FRAMES;i++) frameTable[i].status = FRAME_EMPTY;
			return 0;

		case PAGE_GET_SIZE:                    	// return swap size
			return nextPage;

		case PAGE_GET_READS:                   	// return swap reads
			return pageReads;

		case PAGE_GET_WRITES:                    // return swap writes
			return pageWrites;

		case PAGE_GET_ADR:                    	// return page address
            return (int)(&swapMemory[pnum<<6]);
            break;

		case PAGE_NEW_WRITE:                   // new write (Drops thru to write old)
			pnum = nextPage++;

		case PAGE_OLD_WRITE:                   // write
			//printf("\n    (%d) Write frame %d (memory[%04x]) to page %d", p.PID, frame, frame<<6, pnum);
			memcpy(&swapMemory[pnum<<6], &memory[frame<<6], 1<<7);
            
			pageWrites++;
			return pnum;
            break;

		case PAGE_PRINT:                    	// read
			//printf("\n    (%d) Read page %d into frame %d (memory[%04x])", p.PID, pnum, frame, frame<<6);
            printf("\nPage %d", pnum);
            
            for (int ma = 0; ma < 64;)
            {
                printf("\n0x%04x:", ma);
                for (i=0; i<8; i++)
                {
                    printf(" %04x", MASKTO16BITS(swapMemory[(pnum<<6) + ma + i]));
                }
                ma+=8;
            }

			return pnum;

        case PAGE_READ:                    	// read
            //printf("\n    (%d) Read page %d into frame %d (memory[%04x])", p.PID, pnum, frame, frame<<6);
            memcpy(&memory[frame<<6], &swapMemory[pnum<<6], 1<<7);
            pageReads++;
            return pnum;
		case PAGE_FREE:                   // free page
			printf("\nPAGE_FREE not implemented");
			break;
   }
   return pnum;
} // end accessPage
