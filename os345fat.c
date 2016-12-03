// os345fat.c - file management system
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
//
//		11/19/2011	moved getNextDirEntry to P6
//
// ***********************************************************************
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <setjmp.h>
#include <assert.h>
#include "os345.h"
#include "os345fat.h"

// ***********************************************************************
// ***********************************************************************
//	functions to implement in Project 6
//
int fmsCloseFile(int);
int fmsDefineFile(char*, int);
int fmsDeleteFile(char*);
int fmsOpenFile(char*, int);
int fmsReadFile(int, char*, int);
int fmsSeekFile(int, int);
int fmsWriteFile(int, char*, int);

// ***********************************************************************
// ***********************************************************************
//	Support functions available in os345p6.c
//
extern int fmsGetDirEntry(char* fileName, DirEntry* dirEntry);
extern int fmsGetNextDirEntry(int *dirNum, char* mask, DirEntry* dirEntry, int dir);

extern int fmsGetNextEmptyDirEntry(int *dirNum, int dir, int* dirSector, int entriesNeeded);
extern int fmsUpdateDirEntry(int dirNum, int dir, DirEntry* entry);
extern int fmsMount(char* fileName, void* ramDisk);
extern int fmsGetDirEntrySector(int dir, int dirNum, int* sector);


extern void setFatEntry(int FATindex, unsigned short FAT12ClusEntryVal, unsigned char* FAT);
extern unsigned short getFatEntry(int FATindex, unsigned char* FATtable);

extern int fmsMask(char* mask, char* name, char* ext);
extern void setDirTimeDate(DirEntry* dir);
extern int isValidFileName(char* fileName);
extern void printDirectoryEntry(DirEntry*);
extern void fmsError(int);

extern int fmsReadSector(void* buffer, int sectorNumber);
extern int fmsWriteSector(void* buffer, int sectorNumber);


// ***********************************************************************
// ***********************************************************************
// fms variables
//
// RAM disk
unsigned char RAMDisk[SECTORS_PER_DISK * BYTES_PER_SECTOR];

// File Allocation Tables (FAT1 & FAT2)
unsigned char FAT1[NUM_FAT_SECTORS * BYTES_PER_SECTOR];
unsigned char FAT2[NUM_FAT_SECTORS * BYTES_PER_SECTOR];

char dirPath[128];							// current directory path
FDEntry OFTable[NFILES];					// open file table
unsigned int currOpenFiles = 0;

extern bool diskMounted;					// disk has been mounted
extern TCB tcb[];							// task control block
extern int curTask;							// current task #


// ***********************************************************************
// ***********************************************************************
// This function finds the first empty FAT entry in the table
// returns the index of the fat table
int fmsGetNextFreeFatEntry(uint16* index){
    int i;
    unsigned short entry;
    for (i=2;i<ENTRIES_PER_FAT;i++)
    {
        entry = getFatEntry(i, FAT1);
        *index = i;
        if (entry == 0) return 0;
    }
    return ERR65;
}



// ***********************************************************************
// ***********************************************************************
// This function autcompletes the filename given with the first directory entry in the current directory
int fmsAutcompleteFile(char* filename){
    DirEntry dirEntry;
    int error, i,j, index = 0;
    uint16 cdir = tcb[0].cdir;
    error = fmsGetNextDirEntry(&index, filename, &dirEntry, cdir);
    if (error) {
        printf("\a");
        return 0;
    }
    for (i=0;i<8;i++){
        filename[i] = dirEntry.name[i];
        if (filename[i] == 0 || filename[i] == ' ') break;
    }
    if (dirEntry.extension[0] != ' '){
        filename[i] = '.';
        ++i;
        for (j=0;j<3;j++){
            filename[i+j] = dirEntry.extension[j];
            if (filename[i+j] == 0 || filename[i+j] == ' ') break;
        }
        filename[i+j] = 0;
    }
    else{
        filename[i] = 0;
    }
    return 1;
    
    
}

void fmsUpdateEntryTime(DirEntry* entry){
    //figure out the time and date
    time_t a;
    struct tm *b;
    FATDate *d;
    FATTime *t;
    
    // capture local time and date
    time(&a);
    b = localtime(&a);                  // get local time
    
    d = (FATDate*)&(entry->date);     // point to date w/in dir entry
    d->year = b->tm_year + 1900 - 1980; // update year
    d->month = b->tm_mon;               // update month
    d->day = b->tm_mday;                // update day
    
    t = (FATTime*)&(entry->time);     // point to time w/in dir entry
    t->hour = b->tm_hour;               // update hour
    t->min = b->tm_min;                 // update minute
    t->sec = b->tm_sec;                 // update second
    
    // convert back
    struct tm xx, *x = &xx;
    
    x->tm_wday = 0;
    x->tm_yday = 0;
    x->tm_isdst = 0;
    
    d = (FATDate*)&(entry->date);     // point to date w/in dir entry
    x->tm_year = d->year + 1980 - 1900; // update year
    x->tm_mon = d->month;               // update month
    x->tm_mday = d->day;                // update day
    
    t = (FATTime*)&(entry->time);     // point to time w/in dir entry
    x->tm_hour = t->hour;               // update hour
    x->tm_min = t->min;                 // update minute
    x->tm_sec = t->sec;                 // update second
}


// ***********************************************************************
// ***********************************************************************
// This function closes the open file specified by fileDescriptor.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
//	Return 0 for success, otherwise, return the error number.
//
int fmsCloseFile(int fileDescriptor)
{
	
    FDEntry* fd = &OFTable[fileDescriptor];
    char filename[13];
    int i,j,error;
    DirEntry entry;
    
    //check if the FD is already closed
    if (fd->name[0] == 0) return ERR63;
    
    j=0;
    for (i=0;i<8;i++)
    {
        if (fd->name[i] == ' ') continue;
        filename[j++] = fd->name[i];
    }
    filename[j++] = '.';
    for (i=0;i<3;i++)
    {
        if (fd->extension[i] == ' ') continue;
        filename[j++] = fd->extension[i];
    }
    filename[j] = 0;
    
    
    //get the director entry
    int dirnum = 0,dirSector;
    error = fmsGetNextDirEntry(&dirnum,filename, &entry, CDIR);
    if (error) return error;
    dirnum--;
    error = fmsGetDirEntrySector(CDIR, dirnum, &dirSector);
    if (error) return error;
    
    //save the buffer if we need to
    if (fd->flags & BUFFER_ALTERED && fd->currentCluster != 0){ //if we've altered the buffer
        //write it back out
        //printf("(Writing out sector %d)", fd->currentCluster);
        if ((error = fmsWriteSector(fd->buffer, C_2_S(fd->currentCluster)))){
            return error;
        }
        fd->flags |= FILE_ALTERED; //set the file altered flag
        fd->flags &= ~BUFFER_ALTERED; //clear the altered flag
    }
    
    //update time if the file has been altered
    if (fd->flags & FILE_ALTERED)
        fmsUpdateEntryTime(&entry);
    
    
    
    entry.startCluster = fd->startCluster;
    entry.fileSize = fd->fileSize;
    
    error = fmsUpdateDirEntry(dirSector, dirnum, &entry);
    if (error) return error;
    
    //close the entry
    fd->name[0] = 0;
    fd->startCluster = 0;
    currOpenFiles--;
    
	return 0;
} // end fmsCloseFile



// ***********************************************************************
// ***********************************************************************
// If attribute=DIRECTORY, this function creates a new directory
// file directoryName in the current directory.
// The directory entries "." and ".." are also defined.
// It is an error to try and create a directory that already exists.
//
// else, this function creates a new file fileName in the current directory.
// It is an error to try and create a file that already exists.
// The start cluster field should be initialized to cluster 0.  In FAT-12,
// files of size 0 should point to cluster 0 (otherwise chkdsk should report an error).
// Remember to change the start cluster field from 0 to a free cluster when writing to the
// file.
//
// Return 0 for success, otherwise, return the error number.
//
int fmsDefineFile(char* fileName, int attribute)
{
    int i,j,fileNameLength,error,longFileName =0;
    DirEntry entry;
    fileNameLength = (int)strlen(fileName);
    
    error = fmsGetDirEntry(fileName, &entry);
    if (!error){
        return ERR60;
    }
    entry.attributes = attribute;
    entry.startCluster = 0;
    entry.fileSize = 0;
    
    
    
    if (!isValidFileName(fileName)) return ERR50;
    
    
    for (i=0;i<fileNameLength;i++){
        if (fileName[i] >= 'a' && fileName[i] <= 'z'){
            fileName[i] -=32;
        }
    }
    
    
    //figure out the name
    for (i=0,j=0;i<8;i++){
        
        if (fileName[j] == '.' || fileName[j] == 0){
            entry.name[i] = ' ';
        }
        else
            entry.name[i] = fileName[j++];
        
    }
    
    if (fileName[j] != 0 && fileName[j] != '.'){
        //we have a long file name
        printf("LONG NAME DETECTED");
        longFileName = 1;
        entry.name[i-2]='~';
        while (fileName[j] != '.' && fileName[j] != 0) j++;
    }
    else if (fileName[j] == '.') {
        j++;
    }
    
    
    for (i=0;i<3;i++){
        if (fileName[j] == '.' || fileName[j] == 0){
            entry.extension[i] = ' ';
        }
        else
            entry.extension[i] = fileName[j++];
        
    }

    //find the next entry
    int dirNum=0,dirSector=0, entriesNeeded=1;
    if (strlen(fileName) > 11){
        entriesNeeded = (int)(strlen(fileName)-11) / 13 + 2;
    }
    
    printf("  We need %d",entriesNeeded);
    
    error = fmsGetNextEmptyDirEntry(&dirNum,CDIR,&dirSector,longFileName+1);
    if (error) {
        printf("Unable to find empty entry %d in %d,%d",error,dirNum,dirSector);
        return error;
    }
    
    //check if we're a directory
    if (attribute & DIRECTORY){
        uint16 dirCluster;
        error = fmsGetNextFreeFatEntry(&dirCluster);
        
        if (error < 0) return error;
        //set as the end of the chaim
        setFatEntry(dirCluster, FAT_EOC, FAT1);
        setFatEntry(dirCluster, FAT_EOC, FAT2);
        entry.startCluster = dirCluster;
        
        //setup directory
        DirEntry currDirectory;
        for (i=0;i<8;i++) currDirectory.name[i] = ' ';
        for (i=0;i<3;i++) currDirectory.extension[i] = ' ';
        currDirectory.name[0] = '.';
        currDirectory.time = entry.time;
        currDirectory.date = entry.date;
        currDirectory.startCluster = dirCluster;
        currDirectory.attributes = DIRECTORY;
        
        //make sure we can get back
        DirEntry prevDirectory;
        for (i=0;i<8;i++) prevDirectory.name[i] = ' ';
        for (i=0;i<3;i++) prevDirectory.extension[i] = ' ';
        prevDirectory.name[0] = '.';
        prevDirectory.name[1] = '.';
        prevDirectory.time = entry.time;
        prevDirectory.date = entry.date;
        prevDirectory.startCluster = CDIR;
        prevDirectory.attributes = DIRECTORY;
        
        fmsUpdateDirEntry(C_2_S(dirCluster), 0, &currDirectory);
        fmsUpdateDirEntry(C_2_S(dirCluster), 1, &prevDirectory);
    }
    
    fmsUpdateEntryTime(&entry);
    
    error = fmsUpdateDirEntry(dirSector, dirNum, &entry);
    if (error) return error;
    
    if (longFileName){
        //update the required number of entries to support long names
    }
    
    
    
	return 0;
} // end fmsDefineFile



// ***********************************************************************
// ***********************************************************************
// This function deletes the file fileName from the current director.
// The file name should be marked with an "E5" as the first character and the chained
// clusters in FAT 1 reallocated (cleared to 0).
// Return 0 for success; otherwise, return the error number.
//
int fmsDeleteFile(char* fileName)
{
    DirEntry entry;
    int error;
    int dirNum=0;
    int dirSector;
    //fmsGetNextDirEntry(int *dirNum, char* mask, DirEntry* dirEntry, int dir)
    error = fmsGetNextDirEntry(&dirNum,fileName, &entry, CDIR);
    if (error){
        return ERR61;
    }
    
    //set the file to deleted
    entry.name[0] = 0xE5;
    
    //Clear fat entries
    int fatEntry = entry.startCluster;
    int fatIndex = entry.startCluster;
    while (fatEntry != FAT_EOC && fatEntry != 0){
        fatEntry = getFatEntry(fatIndex, FAT1);
        setFatEntry(fatIndex, 0, FAT1);
        fatIndex = fatEntry;
    }
    
    dirNum--;
    error = fmsGetDirEntrySector(CDIR, dirNum, &dirSector);
    if (error) return error;
    error = fmsUpdateDirEntry(dirSector, dirNum, &entry);
    if (error) return error;
    
    
	return 0;
} // end fmsDeleteFile





// ***********************************************************************
// ***********************************************************************
// This function opens the file fileName for access as specified by rwMode.
// It is an error to try to open a file that does not exist.
// The open mode rwMode is defined as follows:
//    0 - Read access only.
//       The file pointer is initialized to the beginning of the file.
//       Writing to this file is not allowed.
//    1 - Write access only.
//       The file pointer is initialized to the beginning of the file.
//       Reading from this file is not allowed.
//    2 - Append access.
//       The file pointer is moved to the end of the file.
//       Reading from this file is not allowed.
//    3 - Read/Write access.
//       The file pointer is initialized to the beginning of the file.
//       Both read and writing to the file is allowed.
// A maximum of 32 files may be open at any one time.
// If successful, return a file descriptor that is used in calling subsequent file
// handling functions; otherwise, return the error number.
//
int fmsOpenFile(char* fileName, int rwMode)
{
    DirEntry dirEntry;
    int index = 0;
    int error = 0;
    
    if (!diskMounted)
    {
        fmsError(ERR72);
        return 0;
    }
    //check if filename is valid
    int valid_name = isValidFileName(fileName);
    if (valid_name == 0) return ERR50;
    
    //check if we already have too many files open
    if (currOpenFiles == NFILES) return ERR70;
    
    //Find directory entry
    error = fmsGetDirEntry(fileName, &dirEntry);
    if (error){
        return ERR61;
    }
    
    //Check permission
    //if we're trying to write and it's read only
    if (dirEntry.attributes & READ_ONLY && rwMode != 0){
        return ERR84;
    }
    
    if (dirEntry.attributes & DIRECTORY){
        return ERR51;
    }
    /*Invalid File Name”, “File Not Defined”, “File Already open”, “Too Many Files Open”, “File Space Full”*/
    
    //Create a channel (file slot, handle)
    
    //find the first open file slot
    int open_slot = -1;
    for (int fd = 0; fd <= NFILES; fd++)
    {
        if (OFTable[fd].name[0] == 0 && open_slot == -1){
            open_slot = fd;
        }
        //check if the file is already open
        //qif (OFTable[fd].name != 0 && )
    }
    if (open_slot == -1) return ERR70; //we didn't find a slot
    
    FDEntry* entry = &OFTable[open_slot];
    //set the attributes
    entry->startCluster = dirEntry.startCluster;
    entry->currentCluster = 0;
    entry->fileSize = (rwMode == OPEN_WRITE)?0:dirEntry.fileSize;
    
    entry->pid = curTask;
    
    //other attributes
    entry->attributes = dirEntry.attributes;
    entry->mode = rwMode;
    entry->directoryCluster = CDIR;
    entry->fileIndex = (rwMode!=2)?0:dirEntry.fileSize;
    
    //string copy
    for (int i=0;i<8;i++) entry->name[i] = dirEntry.name[i];
    for (int i=0;i<3;i++) entry->extension[i] = dirEntry.extension[i];
    
    //append mode
    if (rwMode & OPEN_APPEND){
        fmsSeekFile(open_slot, entry->fileSize);
    }
    
    currOpenFiles++;
    return open_slot;
} // end fmsOpenFile



// ***********************************************************************
// ***********************************************************************
// This function reads nBytes bytes from the open file specified by fileDescriptor into
// memory pointed to by buffer.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// After each read, the file pointer is advanced.
// Return the number of bytes successfully read (if > 0) or return an error number.
// (If you are already at the end of the file, return EOF error.  ie. you should never
// return a 0.)
//
int fmsReadFile(int fileDescriptor, char* buffer, int nBytes)
{
    int error;
    int nextCluster;
    unsigned int bytesLeft,bufferIndex;
    int numBytesRead = 0;
    /*Errors
     “File Not Open”
     “Invalid File Descriptor”
     “End-of-File”
     “Illegal Access”
     Always reads from transaction buffer
     Watch out for sector boundaries
     Byte oriented (translates to cluster blocking)
     */
    FDEntry* fd = &OFTable[fileDescriptor];
    if (fd->name[0] == 0) return ERR52;
    
    //check that we aren't write or append
    if (fd->mode == OPEN_WRITE || fd->mode == OPEN_APPEND) return ERR85;
    
    
    //load the nBytes
    while (nBytes > 0){
        //setup the next cluster
        nextCluster = fd->currentCluster;
        
        if (fd->fileSize == fd->fileIndex) //check if we reached the end of the file
            return numBytesRead ? numBytesRead : ERR66; //if so return an EOF or the number of bytes we've output
        bufferIndex = fd->fileIndex % BYTES_PER_SECTOR; //figure out where we are in the sector
        if (bufferIndex == 0 && (fd->fileIndex ||!fd->currentCluster)){ //if we're at a sector boundary
            if (fd->currentCluster == 0){ //load the first cluster
                if (fd->startCluster == 0) return ERR66;
                nextCluster = fd->startCluster;
                fd->fileIndex = 0;
            }
            else{ //otherwise we're loading the next cluster
                nextCluster = getFatEntry(fd->currentCluster, FAT1);
                //printf("\nLoading new cluster %d",nextCluster);
                if (nextCluster == FAT_EOC) return numBytesRead;
                
            }
            if (fd->flags & BUFFER_ALTERED){ //if we've altered the buffer
                //write it back out
                if ((error = fmsWriteSector(fd->buffer, C_2_S(fd->currentCluster)))){
                    return error;
                }
                fd->flags &= ~BUFFER_ALTERED; //clear the altered flag
            }
            if ((error = fmsReadSector(fd->buffer, C_2_S(nextCluster)))){
                return error;
            }
            //set our current cluster to the one we just read from
            fd->currentCluster = nextCluster;
            
        }
        
        bytesLeft = BYTES_PER_SECTOR - bufferIndex;
        if (bytesLeft > nBytes) bytesLeft = nBytes;
        if (bytesLeft > (fd->fileSize - fd->fileIndex)){
            bytesLeft = fd->fileSize - fd->fileIndex;
        }
        
        //do the memory copy
        memcpy(buffer, &fd->buffer[bufferIndex], bytesLeft);
        
        fd->fileIndex += bytesLeft;
        buffer += bytesLeft;
        numBytesRead += bytesLeft;
        
        nBytes -= bytesLeft;
    }
    
    
	return numBytesRead;
} // end fmsReadFile



// ***********************************************************************
// ***********************************************************************
// This function changes the current file pointer of the open file specified by
// fileDescriptor to the new file position specified by index.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// The file position may not be positioned beyond the end of the file.
// Return the new position in the file if successful; otherwise, return the error number.
//
int fmsSeekFile(int fileDescriptor, int index)
{
    int error;
    /*Errors
     “File Not Open”
     “Invalid File Descriptor”
     “End-of-File”
     “Illegal Access”
     Always reads from transaction buffer
     Watch out for sector boundaries
     Byte oriented (translates to cluster blocking)
     */
    FDEntry* fd = &OFTable[fileDescriptor];
    if (fd->name[0] == 0) return ERR52;
    
    if (index > fd->fileSize) index = fd->fileSize;
    if (index < 0) index = 0;
    
    if (fd->flags & BUFFER_ALTERED && fd->currentCluster != 0){ //if we've altered the buffer
        //write it back out
        //printf("(Writing out sector %d)", fd->currentCluster);
        if ((error = fmsWriteSector(fd->buffer, C_2_S(fd->currentCluster)))){
            return error;
        }
        fd->flags |= FILE_ALTERED; //set the file altered flag
        fd->flags &= ~BUFFER_ALTERED; //clear the altered flag
    }
    
    //start at the beginning
    fd->fileIndex = 0;
    fd->currentCluster = fd->startCluster;
    
    while (index != fd->fileIndex){
        if (fd->currentCluster == 0) return ERR66;
        int distance = (index - fd->fileIndex);
        //we need to go there
        if (distance >= BYTES_PER_SECTOR){
            printf("\nGoing from cluster %d",fd->currentCluster);
            fd->currentCluster = getFatEntry(fd->currentCluster, FAT1);
            printf("to %d",fd->currentCluster);
            fd->fileIndex += BYTES_PER_SECTOR;
        }
        else
        {
            fd->fileIndex += distance;
        }
        
    }
    
    //load the sector into the buffer
    error = fmsReadSector(fd->buffer, C_2_S(fd->currentCluster));
    if (error) return error;
    
    
    return fd->fileIndex;
} // end fmsSeekFile



// ***********************************************************************
// ***********************************************************************
// This function writes nBytes bytes to the open file specified by fileDescriptor from
// memory pointed to by buffer.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// Writing is always "overwriting" not "inserting" in the file and always writes forward
// from the current file pointer position.
// Return the number of bytes successfully written; otherwise, return the error number.
//
int fmsWriteFile(int fileDescriptor, char* buffer, int nBytes)
{
    
    int bytesOutput = 0;
    int bufferIndex;
    int error;
    uint16 nextCluster;
    int bytesLeft;
    /*Errors
     “File Not Open”
     “Invalid File Descriptor”
     “End-of-File”
     “Illegal Access”
     Always reads from transaction buffer
     Watch out for sector boundaries
     Byte oriented (translates to cluster blocking)
     */
    FDEntry* fd = &OFTable[fileDescriptor];
    if (fd->name[0] == 0) return ERR52;
    
    //printf("\nWriting %d bytes to FD # %d %s: ",nBytes, fileDescriptor,fd->name);
    for (int i=0;i<nBytes;i++) printf("%c",buffer[i]);
    
    if (nBytes == 0) return ERR66;
    
    //check that we aren't in read only mode
    if (fd->mode == OPEN_READ) {
        printf("\nFD is in read-only mode");
        return ERR85;
    }
    
    //load the nBytes
    while (nBytes > 0){
        //setup the next cluster
        nextCluster = fd->currentCluster;
        
        bufferIndex = fd->fileIndex % BYTES_PER_SECTOR; //figure out where we are in the sector
        if (bufferIndex == 0 && (fd->fileIndex ||!fd->currentCluster))
        { //if we're at a sector boundary
            
            if (fd->currentCluster == 0){ //load the first cluster
                if (fd->startCluster == 0){
                    
                    error = fmsGetNextFreeFatEntry(&fd->startCluster);
                    if (error < 0) return error;
                    fd->fileSize += nBytes;
                    //set as the end of the chaim
                    setFatEntry(fd->startCluster, FAT_EOC, FAT1);
                    setFatEntry(fd->startCluster, FAT_EOC, FAT2);
                }
                nextCluster = fd->startCluster;
                fd->fileIndex = 0;
            }
            if (fd->fileIndex != 0){ //otherwise we're loading the next cluster
                nextCluster = getFatEntry(fd->currentCluster, FAT1);
                if (nextCluster == FAT_EOC) {
                    //figure out the next cluster
                    //printf("Get a new cluster and link it");
                    error = fmsGetNextFreeFatEntry(&nextCluster);
                    if (error < 0) return error;
                    fd->fileSize += nBytes;
                    setFatEntry(fd->currentCluster, nextCluster, FAT1);
                    setFatEntry(fd->currentCluster, nextCluster, FAT2);
                    setFatEntry(nextCluster, FAT_EOC, FAT1);
                    setFatEntry(nextCluster, FAT_EOC, FAT2);
                    
                }
            }
            if (fd->flags & BUFFER_ALTERED && fd->currentCluster != 0){ //if we've altered the buffer
                //write it back out
                //printf("(Writing out sector %d)", fd->currentCluster);
                if ((error = fmsWriteSector(fd->buffer, C_2_S(fd->currentCluster)))){
                    return error;
                }
                fd->flags |= FILE_ALTERED; //set the file altered flag
                fd->flags &= ~BUFFER_ALTERED; //clear the altered flag
            }
            if ((error = fmsReadSector(fd->buffer, C_2_S(nextCluster)))){
                return error;
            }
            //set our current cluster to the one we just read from
            fd->currentCluster = nextCluster;
        }
        
        bytesLeft = BYTES_PER_SECTOR - bufferIndex;
        if (bytesLeft > nBytes) bytesLeft = nBytes;
        
        //do the memory copy
        fd->flags = fd->flags | BUFFER_ALTERED;
        memcpy(&fd->buffer[bufferIndex],buffer, bytesLeft);
        
        
        fd->fileIndex += bytesLeft;
        buffer += bytesLeft;
        nBytes -= bytesLeft;
        bytesOutput += bytesLeft;
        
        if (fd->fileIndex > fd->fileSize) fd->fileSize = fd->fileIndex;
        
    }
    return bytesOutput;
    
} // end fmsWriteFile
