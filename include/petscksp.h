/* $Id: petscksp.h,v 1.90 2000/05/08 15:09:50 balay Exp bsmith $ */
/*
   Defines the interface functions for the Krylov subspace accelerators.
*/
#ifndef __PETSCKSP_H
#define __PETSCKSP_H
#include "petscpc.h"

#define KSP_COOKIE  PETSC_COOKIE+8

typedef struct _p_KSP*     KSP;

#define KSPRICHARDSON "richardson"
#define KSPCHEBYCHEV  "chebychev"
#define KSPCG         "cg"
#define KSPGMRES      "gmres"
#define KSPTCQMR      "tcqmr"
#define KSPBCGS       "bcgs"
#define KSPCGS        "cgs"
#define KSPTFQMR      "tfqmr"
#define KSPCR         "cr"
#define KSPLSQR       "lsqr"
#define KSPPREONLY    "preonly"
#define KSPQCG        "qcg"
#define KSPBICG       "bicg"
#define KSPFGMRES     "fgmres" 
#define KSPMINRES     "minres"
typedef char * KSPType;

EXTERN int KSPCreate(MPI_Comm,KSP *);
EXTERN int KSPSetType(KSP,KSPType);
EXTERN int KSPSetUp(KSP);
EXTERN int KSPSolve(KSP,int *);
EXTERN int KSPSolveTranspose(KSP,int *);
EXTERN int KSPDestroy(KSP);

extern FList KSPList;
EXTERN int KSPRegisterAll(char *);
EXTERN int KSPRegisterDestroy(void);

EXTERN int KSPRegister(char*,char*,char*,int(*)(KSP));
#if defined(PETSC_USE_DYNAMIC_LIBRARIES)
#define KSPRegisterDynamic(a,b,c,d) KSPRegister(a,b,c,0)
#else
#define KSPRegisterDynamic(a,b,c,d) KSPRegister(a,b,c,d)
#endif

EXTERN int KSPGetType(KSP,KSPType *);
EXTERN int KSPSetPreconditionerSide(KSP,PCSide);
EXTERN int KSPGetPreconditionerSide(KSP,PCSide*);
EXTERN int KSPGetTolerances(KSP,double*,double*,double*,int*);
EXTERN int KSPSetTolerances(KSP,double,double,double,int);
EXTERN int KSPSetComputeResidual(KSP,PetscTruth);
EXTERN int KSPSetUsePreconditionedResidual(KSP);
EXTERN int KSPSetInitialGuessNonzero(KSP);
EXTERN int KSPGetInitialGuessNonzero(KSP,PetscTruth *);
EXTERN int KSPSetComputeEigenvalues(KSP);
EXTERN int KSPSetComputeSingularValues(KSP);
EXTERN int KSPSetRhs(KSP,Vec);
EXTERN int KSPGetRhs(KSP,Vec *);
EXTERN int KSPSetSolution(KSP,Vec);
EXTERN int KSPGetSolution(KSP,Vec *);
EXTERN int KSPGetResidualNorm(KSP,double*);
EXTERN int KSPGetIterationNumber(KSP,int*);

EXTERN int KSPSetPC(KSP,PC);
EXTERN int KSPGetPC(KSP,PC*);

EXTERN int KSPSetAvoidNorms(KSP);

EXTERN int KSPSetMonitor(KSP,int (*)(KSP,int,double,void*),void *,int (*)(void*));
EXTERN int KSPClearMonitor(KSP);
EXTERN int KSPGetMonitorContext(KSP,void **);
EXTERN int KSPGetResidualHistory(KSP,double **,int *);
EXTERN int KSPSetResidualHistory(KSP,double *,int,PetscTruth);


EXTERN int KSPBuildSolution(KSP,Vec,Vec *);
EXTERN int KSPBuildResidual(KSP,Vec,Vec,Vec *);

EXTERN int KSPRichardsonSetScale(KSP,double);
EXTERN int KSPChebychevSetEigenvalues(KSP,double,double);
EXTERN int KSPComputeExtremeSingularValues(KSP,double*,double*);
EXTERN int KSPComputeEigenvalues(KSP,int,double*,double*,int *);
EXTERN int KSPComputeEigenvaluesExplicitly(KSP,int,double*,double*);

EXTERN int KSPGMRESSetRestart(KSP,int);
EXTERN int KSPGMRESSetPreAllocateVectors(KSP);
EXTERN int KSPGMRESSetOrthogonalization(KSP,int (*)(KSP,int));
EXTERN int KSPGMRESUnmodifiedGramSchmidtOrthogonalization(KSP,int);
EXTERN int KSPGMRESModifiedGramSchmidtOrthogonalization(KSP,int);
EXTERN int KSPGMRESIROrthogonalization(KSP,int);

EXTERN int KSPFGMRESModifyPCNoChange(KSP,int,int,double,void*);
EXTERN int KSPFGMRESModifyPCSLES(KSP,int,int,double,void*);
EXTERN int KSPFGMRESSetModifyPC(KSP,int (*)(KSP,int,int,double,void*),void*,int(*)(void*));

EXTERN int KSPSetFromOptions(KSP);
EXTERN int KSPSetTypeFromOptions(KSP);
EXTERN int KSPAddOptionsChecker(int (*)(KSP));

EXTERN int KSPSingularValueMonitor(KSP,int,double,void *);
EXTERN int KSPDefaultMonitor(KSP,int,double,void *);
EXTERN int KSPTrueMonitor(KSP,int,double,void *);
EXTERN int KSPDefaultSMonitor(KSP,int,double,void *);
EXTERN int KSPVecViewMonitor(KSP,int,double,void *);
EXTERN int KSPGMRESKrylovMonitor(KSP,int,double,void *);


EXTERN int KSPResidual(KSP,Vec,Vec,Vec,Vec,Vec,Vec);
EXTERN int KSPUnwindPreconditioner(KSP,Vec,Vec);
EXTERN int KSPDefaultBuildSolution(KSP,Vec,Vec*);
EXTERN int KSPDefaultBuildResidual(KSP,Vec,Vec,Vec *);

EXTERN int KSPPrintHelp(KSP);

EXTERN int KSPSetOptionsPrefix(KSP,char*);
EXTERN int KSPAppendOptionsPrefix(KSP,char*);
EXTERN int KSPGetOptionsPrefix(KSP,char**);

EXTERN int KSPView(KSP,Viewer);

typedef enum {/* converged */
              KSP_CONVERGED_RTOL               =  2,
              KSP_CONVERGED_ATOL               =  3,
              KSP_CONVERGED_ITS                =  4,
              KSP_CONVERGED_QCG_NEGATIVE_CURVE =  5,
              KSP_CONVERGED_QCG_CONSTRAINED    =  6,
              KSP_CONVERGED_STEP_LENGTH        =  7,
              /* diverged */
              KSP_DIVERGED_ITS                 = -3,
              KSP_DIVERGED_DTOL                = -4,
              KSP_DIVERGED_BREAKDOWN           = -5,
              KSP_DIVERGED_BREAKDOWN_BICG      = -6,
              KSP_DIVERGED_NONSYMMETRIC        = -7,
              KSP_DIVERGED_INDEFINITE_PC       = -8,
 
              KSP_CONVERGED_ITERATING          =  0} KSPConvergedReason;

EXTERN int KSPSetConvergenceTest(KSP,int (*)(KSP,int,double,KSPConvergedReason*,void*),void *);
EXTERN int KSPGetConvergenceContext(KSP,void **);
EXTERN int KSPDefaultConverged(KSP,int,double,KSPConvergedReason*,void *);
EXTERN int KSPSkipConverged(KSP,int,double,KSPConvergedReason*,void *);
EXTERN int KSPGetConvergedReason(KSP,KSPConvergedReason *);

EXTERN int KSPComputeExplicitOperator(KSP,Mat *);

typedef enum {KSP_CG_SYMMETRIC=1,KSP_CG_HERMITIAN=2} KSPCGType;
EXTERN int KSPCGSetType(KSP,KSPCGType);

EXTERN int PCPreSolve(PC,KSP);
EXTERN int PCPostSolve(PC,KSP);

EXTERN int KSPLGMonitorCreate(char*,char*,int,int,int,int,DrawLG*);
EXTERN int KSPLGMonitor(KSP,int,double,void*);
EXTERN int KSPLGMonitorDestroy(DrawLG);
EXTERN int KSPLGTrueMonitorCreate(MPI_Comm,char*,char*,int,int,int,int,DrawLG*);
EXTERN int KSPLGTrueMonitor(KSP,int,double,void*);
EXTERN int KSPLGTrueMonitorDestroy(DrawLG);

#endif


