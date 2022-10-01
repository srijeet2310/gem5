/*
 * Copyright (c) 2010-2012, 2014 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __CPU_O3_FETCH_HH__
#define __CPU_O3_FETCH_HH__

#include "arch/generic/decoder.hh"
#include "arch/generic/mmu.hh"
#include "arch/generic/pcstate.hh"
#include "base/statistics.hh"
#include "base/refcnt.hh"
#include "config/the_isa.hh"
#include "cpu/o3/comm.hh"
#include "cpu/o3/dyn_inst_ptr.hh"
#include "cpu/o3/limits.hh"
#include "cpu/pc_event.hh"
#include "cpu/pred/bpred_unit.hh"
#include "cpu/timebuf.hh"
#include "cpu/translation.hh"
#include "enums/SMTFetchPolicy.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "sim/eventq.hh"
#include "sim/probe/probe.hh"

namespace gem5
{

struct BaseO3CPUParams;

namespace o3
{

class CPU;

/**
 * Fetch class handles both single threaded and SMT fetch. Its
 * width is specified by the parameters; each cycle it tries to fetch
 * that many instructions. It supports using a branch predictor to
 * predict direction and targets.
 * It supports the idling functionality of the CPU by indicating to
 * the CPU when it is active and inactive.
 */
class Fetch
{
  public:
    /**
     * IcachePort class for instruction fetch.
     */
    class IcachePort : public RequestPort
    {
      protected:
        /** Pointer to fetch. */
        Fetch *fetch;

      public:
        /** Default constructor. */
        IcachePort(Fetch *_fetch, CPU *_cpu);

      protected:

        /** Timing version of receive.  Handles setting fetch to the
         * proper status to start fetching. */
        virtual bool recvTimingResp(PacketPtr pkt);

        /** Handles doing a retry of a failed fetch. */
        virtual void recvReqRetry();
    };

    class FetchTranslation : public BaseMMU::Translation
    {
      protected:
        Fetch *fetch;

      public:
        FetchTranslation(Fetch *_fetch) : fetch(_fetch) {}

        void markDelayed() {}

        void
        finish(const Fault &fault, const RequestPtr &req,
            gem5::ThreadContext *tc, BaseMMU::Mode mode)
        {
            assert(mode == BaseMMU::Execute);
            fetch->finishTranslation(fault, req);
            delete this;
        }
    };

  private:
    /* Event to delay delivery of a fetch translation result in case of
     * a fault and the nop to carry the fault cannot be generated
     * immediately */
    class FinishTranslationEvent : public Event
    {
      private:
        Fetch *fetch;
        Fault fault;
        RequestPtr req;

      public:
        FinishTranslationEvent(Fetch *_fetch)
            : fetch(_fetch), req(nullptr)
        {}

        void setFault(Fault _fault) { fault = _fault; }
        void setReq(const RequestPtr &_req) { req = _req; }

        /** Process the delayed finish translation */
        void
        process()
        {
            assert(fetch->numInst < fetch->fetchWidth);
            fetch->finishTranslation(fault, req);
        }

        const char *
        description() const
        {
            return "CPU FetchFinishTranslation";
        }
      };

  public:
    /** Overall fetch status. Used to determine if the CPU can
     * deschedule itsef due to a lack of activity.
     */
    enum FetchStatus
    {
        Active,
        Inactive
    };

    enum FTQStatus
    {
        FTQActive,
        FTQSquash,
        FTQFull,
        FTQInactive
    };

    /** Individual thread status. */
    enum ThreadStatus
    {
        Running,
        Idle,
        Squashing,
        Blocked,
        Fetching,
        TrapPending,
        QuiescePending,
        ItlbWait,
        IcacheWaitResponse,
        IcacheWaitRetry,
        IcacheAccessComplete,
        FTQEmpty,
        NoGoodAddr
    };

  private:
    /** Fetch status. */
    FetchStatus _status;

    /** Per-thread status. */
    ThreadStatus fetchStatus[MaxThreads];
    FTQStatus ftqStatus[MaxThreads];

    /** Fetch policy. */
    SMTFetchPolicy fetchPolicy;

    /** List that has the threads organized by priority. */
    std::list<ThreadID> priorityList;

    /** Probe points. */
    ProbePointArg<DynInstPtr> *ppFetch;
    /** To probe when a fetch request is successfully sent. */
    ProbePointArg<RequestPtr> *ppFetchRequestSent;

  public:
    /** Fetch constructor. */
    Fetch(CPU *_cpu, const BaseO3CPUParams &params);

    /** Returns the name of fetch. */
    std::string name() const;


    /** Registers probes. */
    void regProbePoints();

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *time_buffer);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr);

    /** Initialize stage. */
    void startupStage();

    /** Clear all thread-specific states*/
    void clearStates(ThreadID tid);

    /** Handles retrying the fetch access. */
    void recvReqRetry();

    /** Processes cache completion event. */
    void processCacheCompletion(PacketPtr pkt);

    /** Resume after a drain. */
    void drainResume();

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /**
     * Stall the fetch stage after reaching a safe drain point.
     *
     * The CPU uses this method to stop fetching instructions from a
     * thread that has been drained. The drain stall is different from
     * all other stalls in that it is signaled instantly from the
     * commit stage (without the normal communication delay) when it
     * has reached a safe point to drain from.
     */
    void drainStall(ThreadID tid);

    /** Tells fetch to wake up from a quiesce instruction. */
    void wakeFromQuiesce();

    /** For priority-based fetch policies, need to keep update priorityList */
    void deactivateThread(ThreadID tid);
  private:
    /** Reset this pipeline stage */
    void resetStage();

    /** Changes the status of this stage to active, and indicates this
     * to the CPU.
     */
    void switchToActive();

    /** Changes the status of this stage to inactive, and indicates
     * this to the CPU.
     */
    void switchToInactive();



    /********************************************************************
     *
     * Decoupled Frontend functionality
     *
     * In a decoupled frontend the Branch predictor Unit
     * is not directly queried by Fetch once it pre-decodes
     * a branch. Instead BPU and Fetch is separated and
     * connected via a queue fetch target queue (FTQ). The BPU generates
     * fetch targets (basic block addresses (BB)) and inserts them in
     * the queue. Fetch will consume the addresses and read them from the
     * I-cache.
     * The advantages are that it (1) cuts the critical path and (2)
     * allow a precise, BPU guided prefetching of the fetch targets.
     *
     * For determining next PC addresses the BPU relies on the BTB.
     *
     *
     *
     *******************************************************************/
    //

    class BasicBlock
    {
      public:
        BasicBlock(ThreadID _tid, const PCStateBase &_start_pc, InstSeqNum seqNum);
        // ~BasicBlock();
      private:
        /* Start address of the basic block */
        std::unique_ptr<PCStateBase> startPC;

        /* End address of the basic block */
        std::unique_ptr<PCStateBase> endPC;

        /** The thread id. */
        const ThreadID tid;

      public:
        /* Start address of the basic block */
        Addr startAddress() { return startPC->instAddr(); }

        /* End address of the basic block */
        Addr endAddress() { return (endPC) ? endPC->instAddr() : MaxAddr; }

        /* Basic Block size */
        unsigned size() { return endAddress() - startAddress(); }

        bool isInBB(Addr addr) {
            return addr >= startAddress() && addr < endAddress();
        }

        bool isTerminal(Addr addr) {
            return addr == endAddress();
        }

        bool isTerminalBranch(Addr addr) {
            return (addr == endAddress()) && is_branch;
        }

        bool hasExceeded(Addr addr) {
            return addr > endAddress();
        }



        ThreadID getTid() { return tid; }

        /* List of sequence numbers created for the BB */
        std::list<InstSeqNum> seqNumbers;
        InstSeqNum startSeqNum;
        unsigned seq_num_iter = 0;
        InstSeqNum brSeqNum;


        // /** The branch that terminate the BB */
        // DynInstPtr terminalBranch;

        // std::unique_ptr<PCStateBase> predPC;
        std::unique_ptr<PCStateBase> predPC;
        /** Set/Read the predicted target of the terminal branch. */
        void setPredTarg(const PCStateBase &pred_pc) { set(predPC, pred_pc); }
        const PCStateBase &readPredTarg() { return *predPC; }

        const PCStateBase &readStartPC() { return *startPC; }
        const PCStateBase &readEndPC() { return *endPC; }

        /** Whether the determining instruction is a branch
         *  In case its a branch if it is taken or not.*/
        bool is_branch;
        bool taken;

        // std::unique_ptr<PCStateBase> targetAddr;

        void addSeqNum(InstSeqNum seqNum) { seqNumbers.push_back(seqNum); }

        InstSeqNum getNextSeqNum() {
            seq_num_iter++;
            assert((startSeqNum + seq_num_iter) < brSeqNum);
            return startSeqNum + seq_num_iter;
        }

        void addTerminal(const PCStateBase &br_pc, InstSeqNum seq,
                          bool _is_branch, bool pred_taken,
                          const PCStateBase &pred_pc);
        void addTerminalNoBranch(const PCStateBase &br_pc, InstSeqNum seq,
                               bool pred_taken, const PCStateBase &pred_pc);
    };

    struct FetchTarget
    {
        /* Start address of the basic block */
        std::unique_ptr<PCStateBase> bbStartAddress;

        /* End address of the basic block */
        std::unique_ptr<PCStateBase> bbEndAddress;

        /* Basic Block size */
        unsigned bbSize;

        /** The thread id. */
        ThreadID tid;

        /* Whether the determining branch is taken or not.*/
        bool taken;
        std::unique_ptr<PCStateBase> targetAddr;

      // FetchTarget(Addr _addr = 0, bool
      //     _control = false, bool _taken = false)
      //   : addr(_addr), control(_control), taken(_taken) {};
      // FetchTarget(const FetchTarget &other)
      //   : addr(other.addr), control(other.control), taken(other.taken) {};
    };



    typedef std::shared_ptr<BasicBlock> BasicBlockPtr;
    // using BasicBlockPtr = BasicBlock*;

   typedef std::deque<BasicBlockPtr> FetchTargetQueue;

   FetchTargetQueue ftq[MaxThreads];
    const unsigned ftqSize = 8;

    void dumpFTQ(ThreadID tid);
    bool updateFTQStatus(ThreadID tid);


    BasicBlockPtr basicBlockProduce[MaxThreads];
    BasicBlockPtr basicBlockConsume[MaxThreads];


    bool ftqValid(ThreadID tid, bool &status_change) {
        // If the FTQ is empty wait unit its filled upis available.
        if (ftq[tid].empty()){
            // DPRINTF(Fetch, "[tid:%i] FTQ is empty\n", tid);
            fetchStatus[tid] = FTQEmpty;
            status_change = true;
            return false;
        }
        return true;
    }


    /** Feed the fetch target queue.
     */
    void produceFetchTargets(bool &status_change);
    BasicBlockPtr getCurrentFetchTarget(ThreadID tid, bool &status_change);

    /** The decoupled PC used by the BPU to generate fetch targets
     *
     */
    class BpuPCState : public GenericISA::UPCState<1>
    {
      protected:
        using Base = GenericISA::UPCState<1>;
        // uint8_t _size;

      public:
        // BpuPCState(const BpuPCState &other) : Base(other), _size(other._size) {}
        BpuPCState(const BpuPCState &other) : Base(other) {}

        BpuPCState &operator=(const BpuPCState &other) = default;
        BpuPCState() {}
        explicit BpuPCState(Addr val) { set(val); }
        void advance() override
        {
            Base::advance();
        }
        void
        update(const PCStateBase &other) override
        {
            Base::update(other);
        }
    };

    // using BpuPCState = GenericISA::UPCState<1>;
    typedef std::unique_ptr<BpuPCState> BpuPCStatePtr;

    std::unique_ptr<PCStateBase> bpuPC[MaxThreads];

    // Addr decoupledOffset[MaxThreads];

    DynInstPtr buildInstPlaceholder(ThreadID tid, StaticInstPtr staticInst,
                            const PCStateBase &this_pc);

    DynInstPtr fillInstPlaceholder(const DynInstPtr &src, StaticInstPtr staticInst,
        StaticInstPtr curMacroop, const PCStateBase &this_pc,
        const PCStateBase &next_pc, bool trace);


    DynInstPtr getInstrFromBB(ThreadID tid, StaticInstPtr staticInst,
            StaticInstPtr curMacroop, const PCStateBase &this_pc,
            PCStateBase &next_pc);















    /**
     * Searches the BTB to see if the current PC is a branch or not.
     * If so the branch predictor is checked as well if the next PC should be
     * either next PC+=MachInst or a branch target.
     * @param next_PC Next PC variable passed in by reference.  It is
     * expected to be set to the current PC; it will be updated with what
     * the next PC will be.
     * @param next_NPC Used for ISAs which use delay slots.
     * @return Whether or not a branch was predicted as taken.
     */
    bool searchBTBAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &pc);



    /////////////////////////////////////////////



    /**
     * Looks up in the branch predictor to see if the next PC should be
     * either next PC+=MachInst or a branch target.
     * @param next_PC Next PC variable passed in by reference.  It is
     * expected to be set to the current PC; it will be updated with what
     * the next PC will be.
     * @param next_NPC Used for ISAs which use delay slots.
     * @return Whether or not a branch was predicted as taken.
     */
    bool lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &pc);

    /**
     * Fetches the cache line that contains the fetch PC.  Returns any
     * fault that happened.  Puts the data into the class variable
     * fetchBuffer, which may not hold the entire fetched cache line.
     * @param vaddr The memory address that is being fetched from.
     * @param ret_fault The fault reference that will be set to the result of
     * the icache access.
     * @param tid Thread id.
     * @param pc The actual PC of the current instruction.
     * @return Any fault that occured.
     */
    bool fetchCacheLine(Addr vaddr, ThreadID tid, Addr pc);
    void finishTranslation(const Fault &fault, const RequestPtr &mem_req);


    /** Check if an interrupt is pending and that we need to handle
     */
    bool checkInterrupt(Addr pc) { return interruptPending; }

    /** Squashes a specific thread and resets the PC. */
    void doSquash(const PCStateBase &new_pc, const DynInstPtr squashInst,
            ThreadID tid);

    /** Squashes the FTQ for a specific thread and resets the PC. */
    void doFTQSquash(const PCStateBase &new_pc, ThreadID tid);

    /** Squashes a specific thread and resets the PC. Also tells the CPU to
     * remove any instructions between fetch and decode
     *  that should be sqaushed.
     */
    void squashFromDecode(const PCStateBase &new_pc,
                          const DynInstPtr squashInst,
                          const InstSeqNum seq_num, ThreadID tid);

    /** Checks if a thread is stalled. */
    bool checkStall(ThreadID tid) const;

    /** Updates overall fetch stage status; to be called at the end of each
     * cycle. */
    FetchStatus updateFetchStatus();

  public:
    /** Squashes a specific thread and resets the PC. Also tells the CPU to
     * remove any instructions that are not in the ROB. The source of this
     * squash should be the commit stage.
     */
    void squash(const PCStateBase &new_pc, const InstSeqNum seq_num,
                DynInstPtr squashInst, ThreadID tid);

    /** Ticks the fetch stage, processing all inputs signals and fetching
     * as many instructions as possible.
     */
    void tick();

    /** Checks all input signals and updates the status as necessary.
     *  @return: Returns if the status has changed due to input signals.
     */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Does the actual fetching of instructions and passing them on to the
     * next stage.
     * @param status_change fetch() sets this variable if there was a status
     * change (ie switching to IcacheMissStall).
     */
    void fetch(bool &status_change);

    /** Align a PC to the start of a fetch buffer block. */
    Addr fetchBufferAlignPC(Addr addr)
    {
        return (addr & ~(fetchBufferMask));
    }

    /** The decoder. */
    InstDecoder *decoder[MaxThreads];

    RequestPort &getInstPort() { return icachePort; }

  private:
    DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
            StaticInstPtr curMacroop, const PCStateBase &this_pc,
            const PCStateBase &next_pc, InstSeqNum seq = 0,
            bool insert_IQ = true, bool trace = false);

    /** Returns the appropriate thread to fetch, given the fetch policy. */
    ThreadID getFetchingThread();

    /** Returns the appropriate thread to fetch using a round robin policy. */
    ThreadID roundRobin();

    /** Returns the appropriate thread to fetch using the IQ count policy. */
    ThreadID iqCount();

    /** Returns the appropriate thread to fetch using the LSQ count policy. */
    ThreadID lsqCount();

    /** Returns the appropriate thread to fetch using the branch count
     * policy. */
    ThreadID branchCount();

    /** Pipeline the next I-cache access to the current one. */
    void pipelineIcacheAccesses(ThreadID tid);

    /** Profile the reasons of fetch stall. */
    void profileStall(ThreadID tid);

  private:
    /** Pointer to the O3CPU. */
    CPU *cpu;

    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get decode's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDecode;

    /** Wire to get rename's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromRename;

    /** Wire to get iew's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromIEW;

    /** Wire to get commit's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    //Might be annoying how this name is different than the queue.
    /** Wire used to write any information heading to decode. */
    TimeBuffer<FetchStruct>::wire toDecode;

    /** BPredUnit. */
    branch_prediction::BPredUnit *branchPred;

    std::unique_ptr<PCStateBase> pc[MaxThreads];

    Addr fetchOffset[MaxThreads];

    StaticInstPtr macroop[MaxThreads];

    /** Can the fetch stage redirect from an interrupt on this instruction? */
    bool delayedCommit[MaxThreads];

    /** Memory request used to access cache. */
    RequestPtr memReq[MaxThreads];

    /** Variable that tracks if fetch has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Tracks how many instructions has been fetched this cycle. */
    int numInst;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool decode;
        bool drain;
    };

    /** Tracks which stages are telling fetch to stall. */
    Stalls stalls[MaxThreads];

    /** Decode to fetch delay. */
    Cycles decodeToFetchDelay;

    /** Rename to fetch delay. */
    Cycles renameToFetchDelay;

    /** IEW to fetch delay. */
    Cycles iewToFetchDelay;

    /** Commit to fetch delay. */
    Cycles commitToFetchDelay;

    /** The width of fetch in instructions. */
    unsigned fetchWidth;

    /** The width of decode in instructions. */
    unsigned decodeWidth;

    /** Is the cache blocked?  If so no threads can access it. */
    bool cacheBlocked;

    /** The packet that is waiting to be retried. */
    PacketPtr retryPkt;

    /** The thread that is waiting on the cache to tell fetch to retry. */
    ThreadID retryTid;

    /** Cache block size. */
    unsigned int cacheBlkSize;

    /** The size of the fetch buffer in bytes. The fetch buffer
     *  itself may be smaller than a cache line.
     */
    unsigned fetchBufferSize;

    /** Mask to align a fetch address to a fetch buffer boundary. */
    Addr fetchBufferMask;

    /** The fetch data that is being fetched and buffered. */
    uint8_t *fetchBuffer[MaxThreads];

    /** The PC of the first instruction loaded into the fetch buffer. */
    Addr fetchBufferPC[MaxThreads];

    /** The size of the fetch queue in micro-ops */
    unsigned fetchQueueSize;

    /** Queue of fetched instructions. Per-thread to prevent HoL blocking. */
    std::deque<DynInstPtr> fetchQueue[MaxThreads];

    /** Whether or not the fetch buffer data is valid. */
    bool fetchBufferValid[MaxThreads];

    /** Size of instructions. */
    int instSize;

    /** Icache stall statistics. */
    Counter lastIcacheStall[MaxThreads];

    /** List of Active Threads */
    std::list<ThreadID> *activeThreads;

    /** List of Active FTQ Threads */
    std::list<ThreadID> *activeFTQThreads;

    /** Number of threads. */
    ThreadID numThreads;

    /** Number of threads that are actively fetching. */
    ThreadID numFetchingThreads;

    /** Thread ID being fetched. */
    ThreadID threadFetched;

    /** Checks if there is an interrupt pending.  If there is, fetch
     * must stop once it is not fetching PAL instructions.
     */
    bool interruptPending;

    /** Instruction port. Note that it has to appear after the fetch stage. */
    IcachePort icachePort;

    /** Set to true if a pipelined I-cache request should be issued. */
    bool issuePipelinedIfetch[MaxThreads];

    /** Event used to delay fault generation of translation faults */
    FinishTranslationEvent finishTranslationEvent;

  protected:
    struct FetchStatGroup : public statistics::Group
    {
        FetchStatGroup(CPU *cpu, Fetch *fetch);
        // @todo: Consider making these
        // vectors and tracking on a per thread basis.
        /** Stat for total number of cycles stalled due to an icache miss. */
        statistics::Scalar icacheStallCycles;
        /** Stat for total number of cycles stalled due to an icache miss.
         * while the CPU is waiting for new instructions */
        statistics::Scalar feIcacheStallCycles;
        /** Stat for total number of fetched instructions. */
        statistics::Scalar insts;
        /** Total number of fetched branches. */
        statistics::Scalar branches;
        /** Stat for total number of predicted branches. */
        statistics::Scalar predictedBranches;
        /** Stat for total number of cycles spent fetching. */
        statistics::Scalar cycles;
        /** Stat for total number of cycles spent squashing. */
        statistics::Scalar squashCycles;
        /** Stat for total number of cycles spent waiting for translation */
        statistics::Scalar tlbCycles;
        /** Stat for total number of cycles spent waiting for FTQ to fill. */
        statistics::Scalar ftqStallCycles;
        /** Stat for total number of cycles
         *  spent blocked due to other stages in
         * the pipeline.
         */
        statistics::Scalar idleCycles;
        /** Total number of cycles spent blocked. */
        statistics::Scalar blockedCycles;
        /** Total number of cycles spent in any other state. */
        statistics::Scalar miscStallCycles;
        /** Total number of cycles spent in waiting for drains. */
        statistics::Scalar pendingDrainCycles;
        /** Total number of stall cycles caused by no active threads to run. */
        statistics::Scalar noActiveThreadStallCycles;
        /** Total number of stall cycles caused by pending traps. */
        statistics::Scalar pendingTrapStallCycles;
        /** Total number of stall cycles
         *  caused by pending quiesce instructions. */
        statistics::Scalar pendingQuiesceStallCycles;
        /** Total number of stall cycles caused by I-cache wait retrys. */
        statistics::Scalar icacheWaitRetryStallCycles;
        /** Stat for total number of fetched cache lines. */
        statistics::Scalar cacheLines;
        /** Total number of outstanding icache accesses that were dropped
         * due to a squash.
         */
        statistics::Scalar icacheSquashes;
        /** Total number of outstanding tlb accesses that were dropped
         * due to a squash.
         */
        statistics::Scalar tlbSquashes;
        /** Distribution of number of instructions fetched each cycle. */
        statistics::Distribution nisnDist;
        /** Rate of how often fetch was idle. */
        statistics::Formula idleRate;
        /** Number of branch fetches per cycle. */
        statistics::Formula branchRate;
        /** Number of instruction fetched per cycle. */
        statistics::Formula rate;
    } fetchStats;
};

} // namespace o3
} // namespace gem5

#endif //__CPU_O3_FETCH_HH__
