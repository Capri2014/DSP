/*
 * DwWorker.cpp
 *
 *  Created on: Aug 27, 2016
 *      Author: kibaekkim
 */

//#define DSP_DEBUG

#include "cplex.h"
/** Coin */
#include "OsiCbcSolverInterface.hpp"
#include "OsiCpxSolverInterface.hpp"
/** Dsp */
#include "Utility/DspMacros.h"
#include "Utility/DspUtility.h"
#include "Utility/DspRtnCodes.h"
#include "Solver/DantzigWolfe/DwWorker.h"
#include "Model/TssModel.h"

#define SI OsiCpxSolverInterface

DwWorker::DwWorker(DecModel * model, DspParams * par, DspMessage * message) :
		model_(model),
		par_(par),
		message_(message),
		si_(NULL),
		sub_objs_(NULL) {

	/** parameters */
	parProcIdxSize_ = par_->getIntPtrParamSize("ARR_PROC_IDX");
	parProcIdx_     = par_->getIntPtrParam("ARR_PROC_IDX");
	DSPdebugMessage("Created parameters, DwWorker.\n");

	/** number of total subproblems */
	nsubprobs_ = parProcIdxSize_;

	/** create subproblem solver */
	//sub_ = new DwSub();

	/** create solver interface */
	si_ = new OsiSolverInterface* [parProcIdxSize_];
	for (int i = 0; i < parProcIdxSize_; ++i)
		si_[i] = new SI();

	/** subproblem objective coefficients */
	sub_objs_ = new double* [parProcIdxSize_];
	sub_clbd_ = new double* [parProcIdxSize_];
	sub_cubd_ = new double* [parProcIdxSize_];
	coupled_ = new bool* [parProcIdxSize_];
	for (int i = 0; i < parProcIdxSize_; ++i) {
		sub_objs_[i] = NULL;
		sub_clbd_[i] = NULL;
		sub_cubd_[i] = NULL;

		/** indicate whether columns are coupled with the master or not. */
		coupled_[i] = new bool [model_->getNumCouplingCols()];
		CoinFillN(coupled_[i], model_->getNumCouplingCols(), false);

		int nccols = model_->getNumSubproblemCouplingCols(parProcIdx_[i]);
		const int* ccols = model_->getSubproblemCouplingColIndices(parProcIdx_[i]);
		//DSPdebugMessage("Subproblem(%d) coupling columns:\n", parProcIdx_[i]);
		//DSPdebug(DspMessage::printArray(nccols, ccols));
		for (int j = 0; j < nccols; ++j)
			coupled_[i][ccols[j]] = true;
	}

	/** create subproblems */
	DSP_RTN_CHECK_THROW(createSubproblems());
}

DwWorker::~DwWorker() {
	FREE_2D_PTR(parProcIdxSize_, si_);
	//FREE_PTR(sub_);
	FREE_2D_ARRAY_PTR(parProcIdxSize_, sub_objs_);
	FREE_2D_ARRAY_PTR(parProcIdxSize_, sub_clbd_);
	FREE_2D_ARRAY_PTR(parProcIdxSize_, sub_cubd_);
	FREE_2D_ARRAY_PTR(parProcIdxSize_, coupled_);
}

DSP_RTN_CODE DwWorker::createSubproblems() {
#define FREE_MEMORY        \
	FREE_PTR(mat);    \
	FREE_ARRAY_PTR(ctype); \
	FREE_ARRAY_PTR(rlbd);  \
	FREE_ARRAY_PTR(rubd);

	TssModel* tss = NULL;
	CoinPackedMatrix* mat = NULL;
	char* ctype = NULL;
	double* rlbd = NULL;
	double* rubd = NULL;

	BGN_TRY_CATCH

	if (model_->isStochastic())
		tss = dynamic_cast<TssModel*>(model_);

	for (int s = 0; s < parProcIdxSize_; ++s) {
		if (model_->isStochastic()) {
			DSP_RTN_CHECK_RTN_CODE(
					model_->decompose(1, &parProcIdx_[s], 0, NULL, NULL, NULL,
							mat, sub_clbd_[s], sub_cubd_[s], ctype, sub_objs_[s], rlbd, rubd));
			for (int j = 0; j < tss->getNumCols(0); ++j)
				sub_objs_[s][j] *= tss->getProbability()[j];
		} else {
			DSP_RTN_CHECK_RTN_CODE(
					model_->copySubprob(parProcIdx_[s], mat, sub_clbd_[s], sub_cubd_[s], ctype, sub_objs_[s], rlbd, rubd));

			/** fix zeros for non-coupling columns */
			for (int j = 0; j < model_->getNumCouplingCols(); ++j) {
				if (coupled_[s][j] == false) {
					sub_clbd_[s][j] = 0.0;
					sub_cubd_[s][j] = 0.0;
					sub_objs_[s][j] = 0.0;
				}
			}
		}
		//DSPdebugMessage("sub_objs_[%d]:\n", parProcIdx_[s]);
		//DSPdebug(DspMessage::printArray(model_->getNumCouplingCols(), sub_objs_[s]));

		/** load problem to si */
		si_[s]->loadProblem(*mat, sub_clbd_[s], sub_cubd_[s], sub_objs_[s], rlbd, rubd);

		/** set integers */
		int nintegers = 0;
		for (int j = 0; j < si_[s]->getNumCols(); ++j) {
			if (ctype[j] != 'C') {
				si_[s]->setInteger(j);
				nintegers++;
			}
		}


		/** quiet */
		//dynamic_cast<SI*>(si_[s])->getModelPtr()->setLogLevel(1);

		si_[s]->messageHandler()->setLogLevel(0);
		si_[s]->initialSolve();

		if (nintegers > 0)
			dynamic_cast<OsiCpxSolverInterface*>(si_[s])->switchToMIP();

		OsiCpxSolverInterface* cpx = dynamic_cast<OsiCpxSolverInterface*>(si_[s]);
		if (cpx)
			CPXsetintparam(cpx->getEnvironmentPtr(), CPX_PARAM_THREADS, 1);

#ifdef DSP_DEBUG1
		if (s >= 0) {
			/** write MPS */
			char ofname[128];
			sprintf(ofname, "sub%d", s);
			DSPdebugMessage("Writing MPS file: %s\n", ofname);
			si_[s]->writeMps(ofname);
		}
#endif

		/** free memory */
		FREE_MEMORY
	}

	END_TRY_CATCH_RTN(FREE_MEMORY,DSP_RTN_ERR)

	FREE_MEMORY

	return DSP_RTN_OK;
#undef FREE_MEMORY
}

/** generate variables */
DSP_RTN_CODE DwWorker::generateCols(
		int phase,                           /**< [in] phase of the master */
		const double* piA,                   /**< [in] piA */
		std::vector<int>& indices,           /**< [out] subproblem indices */
		std::vector<int>& statuses,          /**< [out] solution status */
		std::vector<double>& cxs,            /**< [out] solution times original objective coefficients */
		std::vector<double>& objs,           /**< [out] subproblem objective values */
		std::vector<CoinPackedVector*>& sols /**< [out] subproblem coupling column solutions */) {
	CoinError::printErrors_ = true;

	/** subproblem objective and solution */
	double cx;
	double objval;
	CoinPackedVector* sol = NULL;
	TssModel* tss = NULL;

	BGN_TRY_CATCH

	/** adjust objective function */
	DSP_RTN_CHECK_RTN_CODE(adjustObjFunction(phase, piA));

	/** solve subproblems */
	DSP_RTN_CHECK_RTN_CODE(solveSubproblems());

	/** cleanup */
	indices.clear();
	statuses.clear();
	cxs.clear();
	objs.clear();
	for (unsigned i = 0; i < sols.size(); ++i)
		FREE_PTR(sols[i]);
	sols.clear();

	if (model_->isStochastic())
		tss = dynamic_cast<TssModel*>(model_);

	for (int s = 0; s < parProcIdxSize_; ++s) {
		int sind = parProcIdx_[s];

		/** add subproblem index */
		indices.push_back(sind);

		/** store solution status */
		int status;
		convertCoinToDspStatus(si_[s], status);
		DSPdebugMessage("sind %d status %d\n", sind, status);
		statuses.push_back(status);

		if (si_[s]->isProvenOptimal() || si_[s]->isProvenDualInfeasible()) {
			sol = new CoinPackedVector;
			sol->reserve(si_[s]->getNumCols());

			if (si_[s]->isProvenDualInfeasible()) {
				/** retrieve ray if unbounded */
				std::vector<double*> rays = si_[s]->getPrimalRays(1);
				if (rays.size() == 0 || rays[0] == NULL)
					throw CoinError("No primal ray is available.", "generateCols", "DwWorker");
				double* ray = rays[0];
				rays[0] = NULL;

				/** subproblem objective value */
				cx = 0.0;
				objval = 0.0;
				for (int j = 0; j < si_[s]->getNumCols(); ++j) {
					cx += sub_objs_[s][j] * ray[j];
					objval += si_[s]->getObjCoefficients()[j] * ray[j];
				}

				/** subproblem coupling solution */
				for (int j = 0; j < si_[s]->getNumCols(); ++j) {
					double xval = ray[j];
					if (fabs(xval) > 1.0e-8) {
						if (model_->isStochastic()) {
							if (j < tss->getNumCols(0))
								sol->insert(sind * tss->getNumCols(0) + j, xval);
							else
								sol->insert((tss->getNumScenarios()-1) * tss->getNumCols(0) + sind * tss->getNumCols(1) + j, xval);
						} else
							sol->insert(j, xval);
					}
				}

				/** free ray */
				FREE_ARRAY_PTR(ray);
			} else if (si_[s]->isProvenOptimal()){
				const double* x = si_[s]->getColSolution();

				/** subproblem objective value */
				objval = si_[s]->getObjValue();
				cx = 0.0;
				for (int j = 0; j < si_[s]->getNumCols(); ++j)
					cx += sub_objs_[s][j] * x[j];
				DSPdebugMessage("Subprob %d: objval %e, cx %e\n", sind, objval, cx);

				/** subproblem coupling solution */
				for (int j = 0; j < si_[s]->getNumCols(); ++j) {
					double xval = x[j];
					if (fabs(xval) > 1.0e-8) {
						if (model_->isStochastic()) {
							if (j < tss->getNumCols(0))
								sol->insert(sind * tss->getNumCols(0) + j, xval);
							else
								sol->insert((tss->getNumScenarios()-1) * tss->getNumCols(0) + sind * tss->getNumCols(1) + j, xval);
						} else
							sol->insert(j, xval);
					}
				}
			}

			/** store objective and solution */
			cxs.push_back(cx);
			objs.push_back(objval);
			sols.push_back(sol);
			sol = NULL;
		} else {
			DSPdebugMessage("Subproblem %d: status %d\n", sind, status);
			/** store dummies */
			cxs.push_back(0.0);
			objs.push_back(0.0);
			sols.push_back(new CoinPackedVector);
		}
	}

	/** reset subproblems */
	DSP_RTN_CHECK_RTN_CODE(resetSubproblems());

	END_TRY_CATCH_RTN(;,DSP_RTN_ERR)

	return DSP_RTN_OK;
}

DSP_RTN_CODE DwWorker::adjustObjFunction(
		int phase,        /**< [in] phase of the master */
		const double* piA /**< [in] dual variable times the constraint matrix */) {

	TssModel* tss = NULL;
	int ncols_first_stage, ncols_second_stage, nscen;

	BGN_TRY_CATCH

	if (model_->isStochastic()) {
		tss = dynamic_cast<TssModel*>(model_);
		ncols_first_stage = tss->getNumCols(0);
		ncols_second_stage = tss->getNumCols(1);
		nscen = tss->getNumScenarios();
	}

	//DSPdebugMessage("adjustObjFunction is in phase %d.\n", phase);
	for (int s = 0; s < parProcIdxSize_; ++s) {
		/** actual subproblem index */
		int sind = parProcIdx_[s];

		/** set new objective coefficients */
		if (model_->isStochastic()) {
			if (phase == 1) {
				for (int j = 0; j < ncols_first_stage; ++j)
					si_[s]->setObjCoeff(j, -piA[sind * ncols_first_stage + j]);
				for (int j = ncols_first_stage; j < si_[s]->getNumCols(); ++j)
					si_[s]->setObjCoeff(j, -piA[(nscen-1) * ncols_first_stage + sind * ncols_second_stage + j]);
			} else if (phase == 2) {
				for (int j = 0; j < ncols_first_stage; ++j)
					si_[s]->setObjCoeff(j, sub_objs_[s][j] - piA[sind * ncols_first_stage + j]);
				for (int j = ncols_first_stage; j < si_[s]->getNumCols(); ++j)
					si_[s]->setObjCoeff(j, sub_objs_[s][j] - piA[(nscen-1) * ncols_first_stage + sind * ncols_second_stage + j]);
			}
		} else {
			if (phase == 1) {
				for (int j = 0; j < si_[s]->getNumCols(); ++j)
					si_[s]->setObjCoeff(j, -piA[j]);
			} else if (phase == 2) {
				for (int j = 0; j < si_[s]->getNumCols(); ++j)
					si_[s]->setObjCoeff(j, sub_objs_[s][j] - piA[j]);
			}
		}
//		DSPdebugMessage("[Phase %d] Set objective coefficients for subproblem %d:\n", phase, sind);
//		DSPdebug(DspMessage::printArray(si_[s]->getNumCols(), si_[s]->getObjCoefficients()));
	}

	END_TRY_CATCH_RTN(;,DSP_RTN_ERR)

	return DSP_RTN_OK;
}

/** set column lower bounds */
void DwWorker::setColBounds(int j, double lb, double ub) {
	if (model_->isStochastic()) {
		TssModel* tss = dynamic_cast<TssModel*>(model_);
		for (int s = 0; s < parProcIdxSize_; ++s) {
			if (j < tss->getNumScenarios() * tss->getNumCols(0)) {
				si_[s]->setColBounds(j % tss->getNumCols(0), lb, ub);
				//DSPdebugMessage("subproblem %d changed column bounds: %d [%e %e]\n", parProcIdx_[s], j % tss->getNumCols(0), lb, ub);
			} else {
				int jj = j - tss->getNumScenarios() * tss->getNumCols(0);
				si_[s]->setColBounds(tss->getNumCols(0) + jj % tss->getNumCols(1), lb, ub);
				//DSPdebugMessage("subproblem %d changed column bounds: %d [%e %e]\n", parProcIdx_[s], tss->getNumCols(0) + jj % tss->getNumCols(1), lb, ub);
			}
		}
	} else {
		for (int s = 0; s < parProcIdxSize_; ++s) {
			if (coupled_[s][j] == true) {
				si_[s]->setColBounds(j, lb, ub);
				//DSPdebugMessage("subproblem %d changed column bounds: %d [%e %e]\n", parProcIdx_[s], j, lb, ub);
			}
		}
	}
}

void DwWorker::setColBounds(int size, const int* indices, const double* lbs, const double* ubs) {
	for (int i = 0; i < size; ++i)
		setColBounds(indices[i], lbs[i], ubs[i]);
}

DSP_RTN_CODE DwWorker::solveSubproblems() {
	int status;

	BGN_TRY_CATCH

	/** TODO: That's it? Dual infeasible??? */
	for (int s = 0; s < parProcIdxSize_; ++s) {
		/** reset problem status */
		const OsiCbcSolverInterface* cbc = dynamic_cast<OsiCbcSolverInterface*>(si_[s]);
		if (cbc)
			cbc->getModelPtr()->setProblemStatus(-1);

		/** solve LP relaxation */
		si_[s]->resolve();
#ifdef DSP_DEBUG
		convertCoinToDspStatus(si_[s], status);
		DSPdebugMessage("LP relaxation subproblem %d status %d\n", parProcIdx_[s], status);
#endif

		/** do branch-and-bound if there are integer variables */
		if (si_[s]->getNumIntegers() > 0 && si_[s]->isProvenOptimal()) {
//			OsiCpxSolverInterface* cpx = dynamic_cast<OsiCpxSolverInterface*>(si_[s]);
//			if (cpx)
//				CPXsetdblparam(cpx->getEnvironmentPtr(), CPX_PARAM_TILIM, 300);
			/** solve */
			si_[s]->branchAndBound();
//			DSPdebugMessage("Subproblem(%d) solution:\n", parProcIdx_[s]);
//			DSPdebug(DspMessage::printArray(si_[s]->getNumCols(), si_[s]->getColSolution()));

#ifdef DSP_DEBUG
			convertCoinToDspStatus(si_[s], status);
			DSPdebugMessage("MILP subproblem %d status %d\n", parProcIdx_[s], status);
#endif
		} else if (si_[s]->isProvenDualInfeasible()) {
			/** If primal unbounded, ray may not be immediately available.
			 * But, it becomes available if it is solved one more time.
			 * This is probably because the resolve() above behaved as initialSolve(),
			 * in which case presolve determines unboundedness without solve.
			 */
			si_[s]->resolve();
			DSPdebug(si_[s]->writeLp("sub"));
		}
	}

	END_TRY_CATCH_RTN(;,DSP_RTN_ERR)

	return DSP_RTN_OK;
}

DSP_RTN_CODE DwWorker::resetSubproblems() {
	BGN_TRY_CATCH
	/** restore bounds */
	for (int s = 0; s < parProcIdxSize_; ++s) {
		for (int j = 0; j < si_[s]->getNumCols(); ++j)
			si_[s]->setColBounds(j, sub_clbd_[s][j], sub_cubd_[s][j]);
	}
	END_TRY_CATCH_RTN(;,DSP_RTN_ERR)
	return DSP_RTN_OK;
}