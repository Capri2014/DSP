/*
 * DwMasterTr.h
 *
 *  Created on: Nov 18, 2016
 *      Author: kibaekkim
 */

#ifndef SRC_SOLVER_DANTZIGWOLFE_DWMASTERTR_H_
#define SRC_SOLVER_DANTZIGWOLFE_DWMASTERTR_H_

#include <DantzigWolfe/DwMaster.h>

class DwMasterTr: public DwMaster {
public:
    /** constructor with worker*/
	DwMasterTr(DwWorker* worker):
		DwMaster(worker),
		ncols_tr_(0),
		tr_cnt_(0),
		tr_size_(0.01),
		tr_center_(NULL) {}

    /** default destructor */
	virtual ~DwMasterTr() {
		FREE_ARRAY_PTR(tr_center_);
	}

    /** solve */
    virtual DSP_RTN_CODE solve();

protected:

	/** Generate initial columns */
	virtual DSP_RTN_CODE initialColumns() {return DSP_RTN_OK;}

    /** This creates a master problem. */
	virtual DSP_RTN_CODE createProblem();

    /** solve phase 1 */
    virtual DSP_RTN_CODE solvePhase1();

    /** solve phase 2 */
    virtual DSP_RTN_CODE solvePhase2();

    /** calculate primal objective value */
    virtual DSP_RTN_CODE calculatePrimalObjective();

    /** restore columns: adding all the columns back */
    virtual DSP_RTN_CODE restoreCols();

    /** reduce columns (e.g., reduced cost fixing) */
    virtual DSP_RTN_CODE reduceCols();

	/** update master */
	virtual DSP_RTN_CODE updateModel(
    		const double* price, /**< [in] price */
			double curLb         /**< [in] current lower bound */);

    /** termination test */
    virtual bool terminationTest(int nnewcols, int itercnt, double relgap);

protected:

	/** Check if the current dual is on the trust region boundary */
	bool isTrBoundary(const double* price);

	/** update trust region and model */
	DSP_RTN_CODE updateTrustRegion();

	int ncols_tr_; /**< number of columns for trust region */
	int tr_cnt_; /**< null step counter */
	double tr_size_; /**< trust region size */
	double* tr_center_; /**< trust region center */
};

#endif /* SRC_SOLVER_DANTZIGWOLFE_DWMASTERTR_H_ */