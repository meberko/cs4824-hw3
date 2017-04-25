#include <algorithm>
#include <iostream>
#include <map>
#include "pin.H"
#include <set>
#include <stdio.h>
#include <stdint.h>

#define MAX_THREAD_ID   32
#define BLOCK_MASK      ~((unsigned long)63)
#define WORD_MASK       60
#define WORDS_PER_BLOCK 16


KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "sharing.out", "file name for falsely-shared cache block list");

std::map< uintptr_t,std::set<int> > tracker;
PIN_MUTEX* addr_m = new PIN_MUTEX;
std::vector<uintptr_t> addrs = std::vector<uintptr_t>();

// This analysis routine is called on every memory reference.
VOID MemRef(THREADID tid, VOID* addr) {
    VOID* block = (void*)(((uintptr_t)addr & BLOCK_MASK)>>6);
    VOID* word  = (void*)(((uintptr_t)addr &  WORD_MASK)>>2);
    uintptr_t tracker_addr = ((uintptr_t)block<<4)|((uintptr_t)word);
    PIN_MutexLock(addr_m);
    addrs.push_back(tracker_addr);
    PIN_MutexUnlock(addr_m);

    // If this address hasn't been touched yet
    PIN_MutexLock(addr_m);
    if(tracker.find(tracker_addr)==tracker.end()) tracker[tracker_addr] = std::set<int>();
    tracker[tracker_addr].insert(tid);
    PIN_MutexUnlock(addr_m);
}

// Note: Instrumentation function adapted from ManualExamples/pinatrace.cpp
// It is called for each Trace and instruments reads and writes
VOID Trace(TRACE trace, VOID *v) {
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            // For this instruction, get the number of operands!
            UINT32 memOperands = INS_MemoryOperandCount(ins);
            //cout << "memOperands=" << memOperands << endl;
            for (UINT32 memOp = 0; memOp < memOperands; memOp++){
                if (INS_MemoryOperandIsRead(ins, memOp)) {
                    //cout << "INS: " << INS_Disassemble(ins) << endl;
                    INS_InsertPredicatedCall(ins,
                            IPOINT_BEFORE,
                            (AFUNPTR)MemRef,
                    	    IARG_THREAD_ID,
                    	    IARG_MEMORYOP_EA,
                            memOp,
                    	    IARG_END);
                }
                if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    INS_InsertPredicatedCall(ins,
                            IPOINT_BEFORE,
                            (AFUNPTR)MemRef,
                    	    IARG_THREAD_ID,
                    	    IARG_MEMORYOP_EA,
                            memOp,
                    	    IARG_END);
                }
            }
        }
    }
}

VOID Fini(INT32 code, VOID *v) {
    std::vector<uintptr_t>::iterator it;
    printf("Fini!\n");
    printf("addrs size: %d\n", (int)addrs.size());
    std::sort(addrs.begin(), addrs.end());

    int truesh_detected = 0, falsesh = 0;
    unsigned long currblock = 0;
    std::set<int> touched_this_block;

    // Iterate through 2^(32)
    for(it=addrs.begin(); it!=addrs.end(); it++) {
        unsigned long prevblock = currblock;
        currblock = *it/16;
        int sameblock = (prevblock==currblock);

        // True sharing detection, always runs
        if((int)tracker[*it].size()>1) truesh_detected = 1;

        // Check if still in same block
        if(sameblock) {
            std::set<int>::iterator sit;
            // If a thread has accessed this word
            if(!tracker[*it].empty()) {
                for(sit=tracker[*it].begin(); sit!=tracker[*it].end(); sit++) {
                    printf("%d\n", *sit);
                    touched_this_block.insert(*sit);
                }
            }
        }

        // New block
        else {
            //printf("%lu\n",currblock);
            if(touched_this_block.size() > 1 && !truesh_detected) falsesh++;
            touched_this_block.clear();
            truesh_detected = 0;
        }
    }
    printf("Falsely Shared: %d\n",falsesh);

}

INT32 Usage(){
    PIN_ERROR("This Pintool prints list of falsely-shared cache blocks\n"
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

int main(int argc, char *argv[])
{
    PIN_MutexInit(addr_m);

    if (PIN_Init(argc, argv)) return Usage();

    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
