/*
 * Copyright (c) 2011-2012, 2014 ARM Limited
 * Copyright (c) 2010,2022 The University of Edinburgh
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
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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

#ifndef __CPU_PRED_BPRED_UNIT_HH__
#define __CPU_PRED_BPRED_UNIT_HH__

#include <deque>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/btb.hh"
#include "cpu/pred/indirect.hh"
#include "cpu/pred/ras.hh"
#include "cpu/static_inst.hh"
#include "enums/BranchClass.hh"
#include "params/BranchPredictor.hh"
#include "sim/probe/pmu.hh"
#include "sim/sim_object.hh"

namespace gem5
{

namespace branch_prediction
{

  // enum BranchType {
  //   NoBranch = 0,
  //   Call,
  //   CallCond,
  //   CallUncond,
  //   Return,
  //   Direct,
  //   DirectCond,
  //   DirectUncond,
  //   Indirect,
  //   IndirectCond,
  //   IndirectUncond,
  //   NumBranchType
  // };


/**
 * Basically a wrapper class to hold both the branch predictor
 * and the BTB.
 */
class BPredUnit : public SimObject
{

  public:
    typedef BranchPredictorParams Params;
    typedef enums::BranchClass BranchClass;

    /**
     * @param params The params object, that has the size of the BP and BTB.
     */
    BPredUnit(const Params &p);

    void regProbePoints() override;

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /**
     * Invalidates the branch predictor state.
     */
    virtual void memInvalidate() override;

    /**
     * Predicts whether or not the instruction is a taken branch, and the
     * target of the branch if it is taken.
     * @param inst The branch instruction.
     * @param PC The predicted PC is passed back through this parameter.
     * @param tid The thread id.
     * @return Returns if the branch is taken or not.
     */
    bool predict(const StaticInstPtr &inst, const InstSeqNum &seqNum,
                 PCStateBase &pc, ThreadID tid);


    /**
     * Checks if the pre-decoded instruction matches the predicted
     * instruction type. If so update the information with the new.
     * In case the types dont match something is wrong and we need
     * to squash. (should not be the case.)
     * @param seq_num The branches sequence that we want to update.
     * @param inst The new pre-decoded branch instruction.
     * @param tid The thread id.
     * @return Returns if the update was successful.
     */
    bool updateStaticInst(const InstSeqNum &seqNum,
                          const StaticInstPtr &inst, ThreadID tid);

    // /**
    //  * Lookup the BTB to check if the given PC is a branch instruction
    //  * The BTB will also provide the static instruction information
    //  * If so will it will predict the whether the branch was taken or not
    //  * and finally set the target PC respectively.
    //  * @param PC The PC of the instruction branch instruction.
    //  * @param targetPC The predicted PC is passed back through this parameter.
    //  * @param seqNum The sequence number of the dynamic instruction
    //  * @param tid The thread id.
    //  * @return Returns if the branch is taken or not.
    //  */
    // bool predict(PCStateBase &pc, PCStateBase &pc,
    //              const InstSeqNum &seqNum, ThreadID tid);

    // @todo: Rename this function.
    virtual void uncondBranch(ThreadID tid, Addr pc, void * &bp_history) = 0;

    /**
     * Tells the branch predictor to commit any updates until the given
     * sequence number.
     * @param done_sn The sequence number to commit any older updates up until.
     * @param tid The thread id.
     */
    void update(const InstSeqNum &done_sn, ThreadID tid);

    /**
     * Squashes all outstanding updates until a given sequence number.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     * @param tid The thread id.
     */
    void squash(const InstSeqNum &squashed_sn, ThreadID tid);

    /**
     * Squashes all outstanding updates until a given sequence number, and
     * corrects that sn's update with the proper address and taken/not taken.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     * @param corr_target The correct branch target.
     * @param actually_taken The correct branch direction.
     * @param tid The thread id.
     * @param inst The static instruction that caused the misprediction
     */
    void squash(const InstSeqNum &squashed_sn,
                const PCStateBase &corr_target,
                bool actually_taken, ThreadID tid);

    void squash(const InstSeqNum &squashed_sn,
                const PCStateBase &corr_target,
                bool actually_taken, ThreadID tid,
                StaticInstPtr inst, const PCStateBase &pc);

    /**
     * @param bp_history Pointer to the history object.  The predictor
     * will need to update any state and delete the object.
     */
    virtual void squash(ThreadID tid, void *bp_history) = 0;

    /**
     * Looks up a given PC in the BP to see if it is taken or not taken.
     * @param inst_PC The PC to look up.
     * @param bp_history Pointer that will be set to an object that
     * has the branch predictor state associated with the lookup.
     * @return Whether the branch is taken or not taken.
     */
    virtual bool lookup(ThreadID tid, Addr instPC, void * &bp_history) = 0;

     /**
     * If a branch is not taken, because the BTB address is invalid or missing,
     * this function sets the appropriate counter in the global and local
     * predictors to not taken.
     * @param inst_PC The PC to look up the local predictor.
     * @param bp_history Pointer that will be set to an object that
     * has the branch predictor state associated with the lookup.
     */
    virtual void btbUpdate(ThreadID tid, Addr instPC, void * &bp_history) = 0;

    /**
     * Reset function. Set back all internal state of the direction predictor
     * Start and end can be used to define certain ranges to reset
     */
    virtual void reset(unsigned start = 0, unsigned end = 100) {}

    /**
     * Looks up a given PC in the BTB to see if a matching entry exists.
     * @param inst_PC The PC to look up.
     * @return Whether the BTB contains the given PC.
     * @param tid The thread id.
     */
    bool BTBValid(Addr instPC, ThreadID tid)
    {
        return btb->valid(tid, instPC);
    }
    bool BTBValid(PCStateBase &instPC, ThreadID tid)
    {
        return BTBValid(instPC.instAddr(), tid);
    }

    /**
     * Looks up a given PC in the BTB to get the predicted target. The PC may
     * be changed or deleted in the future, so it needs to be used immediately,
     * and/or copied for use later.
     * @param inst_PC The PC to look up.
     * @return The address of the target of the branch.
     */
    const PCStateBase *
    BTBLookup(PCStateBase &instPC, ThreadID tid)
    {
        return btb->lookup(tid, instPC.instAddr());
    }

    /**
     * Looks up a given PC in the BTB to get current static instruction
     * information. This is necessary in a decoupled frontend as
     * the information does not usually exist at that this point.
     * Only for instructions (branches) that hit in the BTB this information
     * is available as the BTB stores them together with the target.
     *
     * @param inst_PC The PC to look up.
     * @return The static instruction info of the given PC if existant.
     */
    const StaticInstPtr
    BTBLookupInst(Addr instPC, ThreadID tid)
    {
        return btb->lookupInst(tid, instPC);
    }
    const StaticInstPtr
    BTBLookupInst(PCStateBase &instPC, ThreadID tid)
    {
        return BTBLookupInst(instPC.instAddr(), tid);
    }

    // /**
    //  * Inserts a given branch PC into the
    //  *
    //  * @param inst_PC The PC to look up.
    //  * @return The static instruction info of the given PC if existant.
    //  */
    // const StaticInstPtr
    // BTBLookupInst(Addr instPC, ThreadID tid)
    // {
    //     return btb->lookupInst(tid, instPC);
    // }
    // const StaticInstPtr
    // BTBLookupInst(PCStateBase &instPC, ThreadID tid)
    // {
    //     return BTBLookupInst(instPC.instAddr(), tid);
    // }



    /**
     * Updates the BP with taken/not taken information.
     * @param inst_PC The branch's PC that will be updated.
     * @param taken Whether the branch was taken or not taken.
     * @param bp_history Pointer to the branch predictor state that is
     * associated with the branch lookup that is being updated.
     * @param squashed Set to true when this function is called during a
     * squash operation.
     * @param inst Static instruction information
     * @param corrTarget The resolved target of the branch (only needed
     * for squashed branches)
     * @todo Make this update flexible enough to handle a global predictor.
     */
    virtual void update(ThreadID tid, Addr instPC, bool taken,
                   void *bp_history, bool squashed,
                   const StaticInstPtr &inst, Addr corrTarget) = 0;
    // /**
    //  * Updates the BTB with the target of a branch.
    //  * @param inst_PC The branch's PC that will be updated.
    //  * @param target_PC The branch's target that will be added to the BTB.
    //  */
    // void
    // BTBUpdate(Addr instPC, const PCStateBase &target)
    // {
    //     btb->update(0, instPC, target);
    // }
    BranchClass getBranchClass(StaticInstPtr inst);

    std::string toStr(BranchClass type) const
    {
        return std::string(enums::BranchClassStrings[type]);
    }

    void dump();



  private:

    // Target provider type
    enum TARGET_PROVIDER
    {
        NO_TARGET = 0,
        BTB,
        RAS,
        INDIRECT,
        LAST_TARGET_PROVIDER_TYPE = INDIRECT
    };

    struct PredictorHistory
    {
        /**
         * Makes a predictor history struct that contains any
         * information needed to update the predictor, BTB, and RAS.
         */
        PredictorHistory(const InstSeqNum &seq_num, Addr instPC,
                         bool pred_taken, void *bp_history,
                         void *indirect_history, void *ras_history,
                         ThreadID _tid, BranchClass type,
                         const StaticInstPtr & inst)
            : seqNum(seq_num), pc(instPC), bpHistory(bp_history),
              indirectHistory(indirect_history), rasHistory(ras_history),
              tid(_tid), predTaken(pred_taken), type(type), inst(inst)
        {}

        PredictorHistory(const PredictorHistory &other) :
            seqNum(other.seqNum), pc(other.pc), bpHistory(other.bpHistory),
            indirectHistory(other.indirectHistory),
            rasHistory(other.rasHistory), tid(other.tid),
            predTaken(other.predTaken), usedRAS(other.usedRAS),
            wasCall(other.wasCall), wasReturn(other.wasReturn),
            wasIndirect(other.wasIndirect),
            wasPredTakenBTBHit(other.wasPredTakenBTBHit),
            wasPredTakenBTBMiss(other.wasPredTakenBTBMiss),
            wasUncond(other.wasUncond),
            target(other.target),
            // targetProvider(targetProvider),
            type(other.type),
            inst(other.inst)
        {
        }

        ~PredictorHistory()
        {
            // Verify that all histories where deleted
            // assert(bpHistory == nullptr);
            // assert(indirectHistory == nullptr);
            // assert(rasHistory == nullptr);
        }

        bool
        operator==(const PredictorHistory &entry) const
        {
            return this->seqNum == entry.seqNum;
        }

        /** The sequence number for the predictor history entry. */
        InstSeqNum seqNum;

        /** The PC associated with the sequence number. */
        Addr pc;

        /** Pointer to the history object passed back from the branch
         * predictor.  It is used to update or restore state of the
         * branch predictor.
         */
        void *bpHistory = nullptr;

        void *indirectHistory = nullptr;

        void *rasHistory = nullptr;

        /** The thread id. */
        ThreadID tid;

        /** Whether or not it was predicted taken. */
        bool predTaken;

        /** Whether or not the RAS was used. */
        bool usedRAS = false;

        /** Whether or not the instruction was a call. */
        bool wasCall = false;

        /** Whether or not the instruction was a return. */
        bool wasReturn = false;

        /** Wether this instruction was an indirect branch */
        bool wasIndirect = false;

        /** Was predicted taken and hit in BTB */
        bool wasPredTakenBTBHit = false;

        /** Was predicted taken but miss in BTB */
        bool wasPredTakenBTBMiss = false;

        /** Was unconditional control */
        bool wasUncond = false;

        /** Target of the branch. First it is predicted, and fixed later
         *  if necessary
         */
        Addr target = MaxAddr;

        BranchClass type = BranchClass::NoBranch;

        /** The branch instrction */
        StaticInstPtr inst;
    };

    typedef std::deque<PredictorHistory> History;

  public:
    /** Number of the threads for which the branch history is maintained. */
    const unsigned numThreads;

  private:
    /** Fallback to the BTB prediction in case the RAS is corrupted. */
    const unsigned fallbackBTB;


    /**
     * The per-thread predictor history. This is used to update the predictor
     * as instructions are committed, or restore it to the proper state after
     * a squash.
     */
    std::vector<History> predHist;
    // std::vecotr<PredictorHistory>

    /** The BTB. */
    BranchTargetBuffer * btb;

    /** The return address stack. */
    ReturnAddrStack * ras;

    /** The indirect target predictor. */
    IndirectPredictor * iPred;

    struct BPredUnitStats : public statistics::Group
    {
        BPredUnitStats(statistics::Group *parent, BPredUnit *bp);

        /** Stat for number of BP lookups. */
        statistics::Vector lookups;

        /** Stat for BP lookup instructions by branch type (BranchType) */
        statistics::Vector2d lookupType;

        /** Stat for final prediction of the BPU by branch type (BranchType)*/
        statistics::Vector2d predTakenType;
        statistics::Vector2d predNotTakenType;

        /** Stat for direction prediction by branch type (BranchType) */
        statistics::Vector2d dirPredTakenType;
        statistics::Vector2d dirPredNotTakenType;

        /** Stat for branches squashed by branch type (BranchType) */
        statistics::Vector2d squashType;
        /** Stat for branches mispredicted by branch type (BranchType) */
        statistics::Vector2d mispredictType;
        /** Stat for branches commited by branch type (BranchType) */
        statistics::Vector2d commitType;

        /** Stat for number of conditional branches predicted. */
        statistics::Scalar condPredicted;
        /** Stat for n of conditional branches predicted as taken. */
        statistics::Scalar condPredictedTaken;
        /** Stat for number of conditional branches predicted incorrectly. */
        statistics::Scalar condIncorrect;
        /** Stat for number of BTB lookups. */
        statistics::Scalar BTBLookups;
        /** Stat for number of BTB hits. */
        statistics::Scalar BTBHits;
        /** Stat for number for the ratio between BTB hits and BTB lookups. */
        statistics::Formula BTBHitRatio;
        /** Stat for number BTB misspredictions. No or wrong target found */
        statistics::Scalar BTBMispredicted;

        /** Stat for the number of indirect target lookups.*/
        statistics::Scalar indirectLookups;
        /** Stat for the number of indirect target hits.*/
        statistics::Scalar indirectHits;
        /** Stat for the number of indirect target misses.*/
        statistics::Scalar indirectMisses;
        /** Stat for the number of indirect target mispredictions.*/
        statistics::Scalar indirectMispredicted;

        /** Stat for the number of conditional calls*/
        statistics::Scalar indirectCall;
        /** Stat for the number of unconditional calls*/
        statistics::Scalar directCall;
        /** Stat for the number of mispredicted calls*/
        statistics::Scalar mispredictCall;

        /** Stat for the number of conditional branches mispredicted*/
        statistics::Scalar mispredictCond;
        /** Stat for the number of unconditional branches mispredicted*/
        statistics::Scalar mispredictUncond;
        /** Stat for the number of branches predicted taken but miss in BTB*/
        statistics::Scalar predTakenBTBMiss;
        /** Stat for the number of unconditional branches miss in BTB*/
        statistics::Scalar uncondBTBMiss;

        /** Stat for the number of branches predicted not taken but
         * turn out to be taken*/
        statistics::Scalar NotTakenMispredicted;
        /** Stat for the number of branches predicted taken but turn
         * out to be not taken*/
        statistics::Scalar TakenMispredicted;


    } stats;

  protected:
    /** Number of bits to shift instructions by for predictor addresses. */
    const unsigned instShiftAmt;
    /** Reset functionality. */
    const unsigned resetBTB;
    const unsigned resetStart;
    const unsigned resetEnd;

    /**
     * @{
     * @name PMU Probe points.
     */

    /**
     * Helper method to instantiate probe points belonging to this
     * object.
     *
     * @param name Name of the probe point.
     * @return A unique_ptr to the new probe point.
     */
    probing::PMUUPtr pmuProbePoint(const char *name);


    /**
     * Branches seen by the branch predictor
     *
     * @note This counter includes speculative branches.
     */
    probing::PMUUPtr ppBranches;

    /** Miss-predicted branches */
    probing::PMUUPtr ppMisses;

    /** @} */
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_BPRED_UNIT_HH__
