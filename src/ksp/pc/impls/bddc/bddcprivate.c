#include <../src/ksp/pc/impls/bddc/bddc.h>
#include <../src/ksp/pc/impls/bddc/bddcprivate.h>
#include <petscblaslapack.h>

static PetscErrorCode PCBDDCMatMultTranspose_Private(Mat A, Vec x, Vec y);
static PetscErrorCode PCBDDCMatMult_Private(Mat A, Vec x, Vec y);

#undef __FUNCT__
#define __FUNCT__ "PCBDDCAdaptiveSelection"
PetscErrorCode PCBDDCAdaptiveSelection(PC pc)
{
  PC_BDDC*        pcbddc = (PC_BDDC*)pc->data;
  PCBDDCSubSchurs sub_schurs = pcbddc->sub_schurs;
  PetscBLASInt    B_dummyint,B_neigs,B_ierr,B_lwork;
  PetscBLASInt    *B_iwork,*B_ifail;
  PetscScalar     *work,lwork;
  PetscScalar     *St,*S,*eigv;
  PetscScalar     *Sarray,*Starray;
  PetscReal       *eigs,thresh;
  PetscInt        i,nmax,nmin,nv,cum,mss,cum2,cumarray,maxneigs;
  PetscBool       allocated_S_St;
#if defined(PETSC_USE_COMPLEX)
  PetscReal       *rwork;
#endif
  PetscErrorCode  ierr;

  PetscFunctionBegin;
  if (!sub_schurs->use_mumps) {
    SETERRQ(PetscObjectComm((PetscObject)pc),PETSC_ERR_SUP,"Adaptive selection of constraints requires MUMPS");
  }

  if (pcbddc->dbg_flag) {
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Check adaptive selection of constraints\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedAllow(pcbddc->dbg_viewer,PETSC_TRUE);CHKERRQ(ierr);
  }

  if (pcbddc->dbg_flag) {
    PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d cc %d (%d,%d).\n",PetscGlobalRank,sub_schurs->n_subs,sub_schurs->is_hermitian,sub_schurs->is_posdef);
  }

  if (sub_schurs->n_subs && (!sub_schurs->is_hermitian || !sub_schurs->is_posdef)) {
    SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"Adaptive selection not yet implemented for general matrix pencils (herm %d, posdef %d)\n",sub_schurs->is_hermitian,sub_schurs->is_posdef);
  }

  /* max size of subsets */
  mss = 0;
  for (i=0;i<sub_schurs->n_subs;i++) {
    if (PetscBTLookup(sub_schurs->computed_Stilda_subs,i)) {
      PetscInt subset_size;
      ierr = ISGetLocalSize(sub_schurs->is_subs[i],&subset_size);CHKERRQ(ierr);
      mss = PetscMax(mss,subset_size);
    }
  }

  /* min/max and threshold */
  nmax = pcbddc->adaptive_nmax > 0 ? pcbddc->adaptive_nmax : mss;
  nmin = pcbddc->adaptive_nmin > 0 ? pcbddc->adaptive_nmin : 0;
  nmax = PetscMax(nmin,nmax);
  allocated_S_St = PETSC_FALSE; 
  if (nmin) {
    allocated_S_St = PETSC_TRUE; 
  }

  /* allocate lapack workspace */
  cum = cum2 = 0;
  maxneigs = 0;
  for (i=0;i<sub_schurs->n_subs;i++) {
    if (PetscBTLookup(sub_schurs->computed_Stilda_subs,i)) {
      PetscInt n,subset_size;

      ierr = ISGetLocalSize(sub_schurs->is_subs[i],&subset_size);CHKERRQ(ierr);
      n = PetscMin(subset_size,nmax);
      cum += subset_size*n;
      cum2 += n;
      maxneigs = PetscMax(maxneigs,n);
    }
  }
  if (mss) {
    if (sub_schurs->is_hermitian && sub_schurs->is_posdef) {
      PetscBLASInt B_itype = 1;
      PetscBLASInt B_N = mss;
      PetscReal    zero = 0.0;
      PetscReal    eps = 0.0; /* dlamch? */

      B_lwork = -1;
      S = NULL;
      St = NULL;
      eigs = NULL;
      eigv = NULL;
      B_iwork = NULL;
      B_ifail = NULL;
      thresh = 1.0;
      ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
      PetscStackCallBLAS("LAPACKsygvx",LAPACKsygvx_(&B_itype,"V","V","L",&B_N,St,&B_N,S,&B_N,&zero,&thresh,&B_dummyint,&B_dummyint,&eps,&B_neigs,eigs,eigv,&B_N,&lwork,&B_lwork,rwork,B_iwork,B_ifail,&B_ierr));
#else
      PetscStackCallBLAS("LAPACKsygvx",LAPACKsygvx_(&B_itype,"V","V","L",&B_N,St,&B_N,S,&B_N,&zero,&thresh,&B_dummyint,&B_dummyint,&eps,&B_neigs,eigs,eigv,&B_N,&lwork,&B_lwork,B_iwork,B_ifail,&B_ierr));
#endif
      if (B_ierr != 0) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in query to SYGVX Lapack routine %d",(int)B_ierr);
      ierr = PetscFPTrapPop();CHKERRQ(ierr);
    } else {
        /* TODO */
    }
  } else {
    lwork = 0;
  }

  nv = 0;
  if (sub_schurs->is_Ej_com && pcbddc->use_vertices) { /* complement of subsets, each entry is a vertex */
    ierr = ISGetLocalSize(sub_schurs->is_Ej_com,&nv);CHKERRQ(ierr);
  }
  ierr = PetscBLASIntCast((PetscInt)PetscRealPart(lwork),&B_lwork);CHKERRQ(ierr);
  if (allocated_S_St) {
    ierr = PetscMalloc2(mss*mss,&S,mss*mss,&St);CHKERRQ(ierr);
  }
  ierr = PetscMalloc5(mss*mss,&eigv,mss,&eigs,B_lwork,&work,5*mss,&B_iwork,mss,&B_ifail);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
  ierr = PetscMalloc1(7*mss,&rwork);CHKERRQ(ierr);
#endif
  ierr = PetscMalloc4(nv+sub_schurs->n_subs,&pcbddc->adaptive_constraints_n,
                      nv+cum2+1,&pcbddc->adaptive_constraints_ptrs,
                      nv+cum,&pcbddc->adaptive_constraints_idxs,
                      nv+cum,&pcbddc->adaptive_constraints_data);CHKERRQ(ierr);
  ierr = PetscMemzero(pcbddc->adaptive_constraints_n,(nv+sub_schurs->n_subs)*sizeof(PetscInt));CHKERRQ(ierr);

  maxneigs = 0;
  cum = cum2 = cumarray = 0;
  if (sub_schurs->is_Ej_com && pcbddc->use_vertices) {
    const PetscInt *idxs;

    ierr = ISGetIndices(sub_schurs->is_Ej_com,&idxs);CHKERRQ(ierr);
    for (cum=0;cum<nv;cum++) {
      pcbddc->adaptive_constraints_n[cum] = 1;
      pcbddc->adaptive_constraints_idxs[cum] = idxs[cum];
      pcbddc->adaptive_constraints_ptrs[cum] = cum;
      pcbddc->adaptive_constraints_data[cum] = 1.0;
    }
    cum2 = cum;
    ierr = ISRestoreIndices(sub_schurs->is_Ej_com,&idxs);CHKERRQ(ierr);
  }

  if (mss) { /* multilevel */
    ierr = MatSeqAIJGetArray(sub_schurs->sum_S_Ej_inv_all,&Sarray);CHKERRQ(ierr);
    ierr = MatSeqAIJGetArray(sub_schurs->sum_S_Ej_tilda_all,&Starray);CHKERRQ(ierr);
  }

  for (i=0;i<sub_schurs->n_subs;i++) {
    PetscInt j,subset_size;

    ierr = ISGetLocalSize(sub_schurs->is_subs[i],&subset_size);CHKERRQ(ierr);
    if (PetscBTLookup(sub_schurs->computed_Stilda_subs,i)) {
      const PetscInt *idxs;
      PetscReal      infty = PETSC_MAX_REAL;
      PetscInt       eigs_start = 0;
      PetscBLASInt   B_N;

      ierr = PetscBLASIntCast(subset_size,&B_N);CHKERRQ(ierr);
      if (allocated_S_St) { /* S and S_t should be copied since we could need them later */
        if (sub_schurs->is_hermitian) {
          PetscInt j;
          for (j=0;j<subset_size;j++) {
            ierr = PetscMemcpy(S+j*(subset_size+1),Sarray+cumarray+j*(subset_size+1),(subset_size-j)*sizeof(PetscScalar));CHKERRQ(ierr);
          }
          for (j=0;j<subset_size;j++) {
            ierr = PetscMemcpy(St+j*(subset_size+1),Starray+cumarray+j*(subset_size+1),(subset_size-j)*sizeof(PetscScalar));CHKERRQ(ierr);
          }
        } else {
          ierr = PetscMemcpy(S,Sarray+cumarray,subset_size*subset_size*sizeof(PetscScalar));CHKERRQ(ierr);
          ierr = PetscMemcpy(St,Starray+cumarray,subset_size*subset_size*sizeof(PetscScalar));CHKERRQ(ierr);
        }
      } else {
        S = Sarray + cumarray;
        St = Starray + cumarray;
      }

      /* Threshold: this is an heuristic for edges */
      ierr = ISGetIndices(sub_schurs->is_subs[i],&idxs);CHKERRQ(ierr);
      thresh = pcbddc->mat_graph->count[idxs[0]]*pcbddc->adaptive_threshold;

      if (sub_schurs->is_hermitian && sub_schurs->is_posdef) {
        PetscBLASInt B_itype = 1;
        PetscBLASInt B_IL, B_IU;
        PetscReal    eps = -1.0; /* dlamch? */
        PetscInt     nmin_s;

        /* ask for eigenvalues larger than thresh */
        if (pcbddc->dbg_flag) {
          PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Computing for sub %d/%d %d %d.\n",i,sub_schurs->n_subs,subset_size,pcbddc->mat_graph->count[idxs[0]]);
        }
        ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
        PetscStackCallBLAS("LAPACKsygvx",LAPACKsygvx_(&B_itype,"V","V","L",&B_N,St,&B_N,S,&B_N,&thresh,&infty,&B_IL,&B_IU,&eps,&B_neigs,eigs,eigv,&B_N,work,&B_lwork,rwork,B_iwork,B_ifail,&B_ierr));
#else
        PetscStackCallBLAS("LAPACKsygvx",LAPACKsygvx_(&B_itype,"V","V","L",&B_N,St,&B_N,S,&B_N,&thresh,&infty,&B_IL,&B_IU,&eps,&B_neigs,eigs,eigv,&B_N,work,&B_lwork,B_iwork,B_ifail,&B_ierr));
#endif
        ierr = PetscFPTrapPop();CHKERRQ(ierr);
        if (B_ierr) {
          if (B_ierr < 0 ) {
            SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in SYGVX Lapack routine: illegal value for argument %d",-(int)B_ierr);
          } else if (B_ierr <= B_N) {
            SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in SYGVX Lapack routine: %d eigenvalues failed to converge",(int)B_ierr);
          } else {
            SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in SYGVX Lapack routine: leading minor of order %d is not positive definite",(int)B_ierr-B_N-1);
          }
        }

        if (B_neigs > nmax) {
          if (pcbddc->dbg_flag) {
            PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"   found %d eigs, more than maximum required %d.\n",B_neigs,nmax);
          }
          eigs_start = B_neigs -nmax;
          B_neigs = nmax;
        }

        nmin_s = PetscMin(nmin,B_N);
        if (B_neigs < nmin_s) {
          PetscBLASInt B_neigs2;

          B_IU = B_N - B_neigs;
          B_IL = B_N - nmin_s + 1;
          if (pcbddc->dbg_flag) {
            PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"   found %d eigs, less than minimum required %d. Asking for %d to %d incl (fortran like)\n",B_neigs,nmin,B_IL,B_IU);
          }
          if (sub_schurs->is_hermitian) {
            PetscInt j;
            for (j=0;j<subset_size;j++) {
              ierr = PetscMemcpy(S+j*(subset_size+1),Sarray+cumarray+j*(subset_size+1),(subset_size-j)*sizeof(PetscScalar));CHKERRQ(ierr);
            }
            for (j=0;j<subset_size;j++) {
              ierr = PetscMemcpy(St+j*(subset_size+1),Starray+cumarray+j*(subset_size+1),(subset_size-j)*sizeof(PetscScalar));CHKERRQ(ierr);
            }
          } else {
            ierr = PetscMemcpy(S,Sarray+cumarray,subset_size*subset_size*sizeof(PetscScalar));CHKERRQ(ierr);
            ierr = PetscMemcpy(St,Starray+cumarray,subset_size*subset_size*sizeof(PetscScalar));CHKERRQ(ierr);
          }
          ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
          PetscStackCallBLAS("LAPACKsygvx",LAPACKsygvx_(&B_itype,"V","I","L",&B_N,St,&B_N,S,&B_N,&thresh,&infty,&B_IL,&B_IU,&eps,&B_neigs2,eigs+B_neigs,eigv+B_neigs*subset_size,&B_N,work,&B_lwork,rwork,B_iwork,B_ifail,&B_ierr));
#else
          PetscStackCallBLAS("LAPACKsygvx",LAPACKsygvx_(&B_itype,"V","I","L",&B_N,St,&B_N,S,&B_N,&thresh,&infty,&B_IL,&B_IU,&eps,&B_neigs2,eigs+B_neigs,eigv+B_neigs*subset_size,&B_N,work,&B_lwork,B_iwork,B_ifail,&B_ierr));
#endif
          ierr = PetscFPTrapPop();CHKERRQ(ierr);
          B_neigs += B_neigs2;
        }
        if (B_ierr) {
          if (B_ierr < 0 ) {
            SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in SYGVX Lapack routine: illegal value for argument %d",-(int)B_ierr);
          } else if (B_ierr <= B_N) {
            SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in SYGVX Lapack routine: %d eigenvalues failed to converge",(int)B_ierr);
          } else {
            SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in SYGVX Lapack routine: leading minor of order %d is not positive definite",(int)B_ierr-B_N-1);
          }
        }
        if (pcbddc->dbg_flag) {
          PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"   -> Got %d eigs\n",B_neigs);
          for (j=0;j<B_neigs;j++) {
            if (eigs[j] == 0.0) {
              PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"     Inf\n");
            } else {
              PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"     %1.6e\n",eigs[j+eigs_start]);
            }
          }
        }
      } else {
          /* TODO */
      }

      maxneigs = PetscMax(B_neigs,maxneigs);
      pcbddc->adaptive_constraints_n[i+nv] = B_neigs;
      ierr = PetscMemcpy(pcbddc->adaptive_constraints_data+cum2,eigv+eigs_start*subset_size,B_neigs*subset_size*sizeof(PetscScalar));CHKERRQ(ierr);

      if (pcbddc->dbg_flag > 1) {
        PetscInt ii;
        for (ii=0;ii<B_neigs;ii++) {
          PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"   -> Eigenvector %d/%d (%d)\n",ii,B_neigs,B_N);
          for (j=0;j<B_N;j++) {
            PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"       %1.4e\n",pcbddc->adaptive_constraints_data[ii*subset_size+j+cum2]);
          }
        }
      }
      for (j=0;j<B_neigs;j++) {
#if 0
        {
          PetscBLASInt Blas_N,Blas_one = 1.0;
          PetscScalar norm;
          ierr = PetscBLASIntCast(subset_size,&Blas_N);CHKERRQ(ierr);
          PetscStackCallBLAS("BLASdot",norm = BLASdot_(&Blas_N,pcbddc->adaptive_constraints_data+cum2,&Blas_one,pcbddc->adaptive_constraints_data+cum2,&Blas_one));
          if (pcbddc->adaptive_constraints_data[cum2] > 0.0) {
            norm = 1.0/PetscSqrtReal(PetscRealPart(norm));
          } else {
            norm = -1.0/PetscSqrtReal(PetscRealPart(norm));
          }
          PetscStackCallBLAS("BLASscal",BLASscal_(&Blas_N,&norm,pcbddc->adaptive_constraints_data+cum2,&Blas_one));
        }
#endif
        ierr = PetscMemcpy(pcbddc->adaptive_constraints_idxs+cum2,idxs,subset_size*sizeof(PetscInt));CHKERRQ(ierr);
        pcbddc->adaptive_constraints_ptrs[cum++] = cum2;
        cum2 += subset_size;
      }
      ierr = ISRestoreIndices(sub_schurs->is_subs[i],&idxs);CHKERRQ(ierr);
    }
    /* shift for next computation */
    cumarray += subset_size*subset_size;
  }
  if (pcbddc->dbg_flag) {
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
  }
  pcbddc->adaptive_constraints_ptrs[cum] = cum2;

  if (mss) {
    ierr = MatSeqAIJRestoreArray(sub_schurs->sum_S_Ej_inv_all,&Sarray);CHKERRQ(ierr);
    ierr = MatSeqAIJRestoreArray(sub_schurs->sum_S_Ej_tilda_all,&Starray);CHKERRQ(ierr);
    /* destroy matrices (junk) */
    ierr = MatDestroy(&sub_schurs->sum_S_Ej_inv_all);CHKERRQ(ierr);
    ierr = MatDestroy(&sub_schurs->sum_S_Ej_tilda_all);CHKERRQ(ierr);
  }
  if (allocated_S_St) {
    ierr = PetscFree2(S,St);CHKERRQ(ierr);
  }
  ierr = PetscFree5(eigv,eigs,work,B_iwork,B_ifail);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
  ierr = PetscFree(rwork);CHKERRQ(ierr);
#endif
  if (pcbddc->dbg_flag) {
    PetscInt maxneigs_r;
    ierr = MPI_Allreduce(&maxneigs,&maxneigs_r,1,MPIU_INT,MPI_MAX,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);
    ierr = PetscPrintf(PetscObjectComm((PetscObject)pc),"Maximum number of constraints per cc %d\n",maxneigs_r);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSetUpSolvers"
PetscErrorCode PCBDDCSetUpSolvers(PC pc)
{
  PC_BDDC*       pcbddc = (PC_BDDC*)pc->data;
  PetscScalar    *coarse_submat_vals;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* Setup local scatters R_to_B and (optionally) R_to_D */
  /* PCBDDCSetUpLocalWorkVectors should be called first! */
  ierr = PCBDDCSetUpLocalScatters(pc);CHKERRQ(ierr);

  /* Setup local neumann solver ksp_R */
  /* PCBDDCSetUpLocalScatters should be called first! */
  ierr = PCBDDCSetUpLocalSolvers(pc,PETSC_FALSE,PETSC_TRUE);CHKERRQ(ierr);

  /* Change global null space passed in by the user if change of basis has been requested */
  if (pcbddc->NullSpace && pcbddc->ChangeOfBasisMatrix) {
    ierr = PCBDDCNullSpaceAdaptGlobal(pc);CHKERRQ(ierr);
  }

  /*
     Setup local correction and local part of coarse basis.
     Gives back the dense local part of the coarse matrix in column major ordering
  */
  ierr = PCBDDCSetUpCorrection(pc,&coarse_submat_vals);CHKERRQ(ierr);

  /* Compute total number of coarse nodes and setup coarse solver */
  ierr = PCBDDCSetUpCoarseSolver(pc,coarse_submat_vals);CHKERRQ(ierr);

  /* free */
  ierr = PetscFree(coarse_submat_vals);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCResetCustomization"
PetscErrorCode PCBDDCResetCustomization(PC pc)
{
  PC_BDDC        *pcbddc = (PC_BDDC*)pc->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PCBDDCGraphResetCSR(pcbddc->mat_graph);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->user_primal_vertices);CHKERRQ(ierr);
  ierr = MatNullSpaceDestroy(&pcbddc->NullSpace);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->NeumannBoundaries);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->NeumannBoundariesLocal);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->DirichletBoundaries);CHKERRQ(ierr);
  ierr = MatNullSpaceDestroy(&pcbddc->onearnullspace);CHKERRQ(ierr);
  ierr = PetscFree(pcbddc->onearnullvecs_state);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->DirichletBoundariesLocal);CHKERRQ(ierr);
  ierr = PCBDDCSetDofsSplitting(pc,0,NULL);CHKERRQ(ierr);
  ierr = PCBDDCSetDofsSplittingLocal(pc,0,NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCResetTopography"
PetscErrorCode PCBDDCResetTopography(PC pc)
{
  PC_BDDC        *pcbddc = (PC_BDDC*)pc->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatDestroy(&pcbddc->user_ChangeOfBasisMatrix);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->ChangeOfBasisMatrix);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->ConstraintMatrix);CHKERRQ(ierr);
  ierr = PCBDDCGraphReset(pcbddc->mat_graph);CHKERRQ(ierr);
  ierr = PCBDDCSubSchursReset(pcbddc->sub_schurs);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCResetSolvers"
PetscErrorCode PCBDDCResetSolvers(PC pc)
{
  PC_BDDC        *pcbddc = (PC_BDDC*)pc->data;
  PetscScalar    *array;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecDestroy(&pcbddc->coarse_vec);CHKERRQ(ierr);
  if (pcbddc->coarse_phi_B) {
    ierr = MatDenseGetArray(pcbddc->coarse_phi_B,&array);CHKERRQ(ierr);
    ierr = PetscFree(array);CHKERRQ(ierr);
  }
  ierr = MatDestroy(&pcbddc->coarse_phi_B);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->coarse_phi_D);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->coarse_psi_B);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->coarse_psi_D);CHKERRQ(ierr);
  ierr = VecDestroy(&pcbddc->vec1_P);CHKERRQ(ierr);
  ierr = VecDestroy(&pcbddc->vec1_C);CHKERRQ(ierr);
  if (pcbddc->local_auxmat2) {
    ierr = MatDenseGetArray(pcbddc->local_auxmat2,&array);CHKERRQ(ierr);
    ierr = PetscFree(array);CHKERRQ(ierr);
  }
  ierr = MatDestroy(&pcbddc->local_auxmat2);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->local_auxmat1);CHKERRQ(ierr);
  ierr = VecDestroy(&pcbddc->vec1_R);CHKERRQ(ierr);
  ierr = VecDestroy(&pcbddc->vec2_R);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->is_R_local);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&pcbddc->R_to_B);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&pcbddc->R_to_D);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&pcbddc->coarse_loc_to_glob);CHKERRQ(ierr);
  ierr = KSPDestroy(&pcbddc->ksp_D);CHKERRQ(ierr);
  ierr = KSPDestroy(&pcbddc->ksp_R);CHKERRQ(ierr);
  ierr = KSPDestroy(&pcbddc->coarse_ksp);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->local_mat);CHKERRQ(ierr);
  ierr = PetscFree(pcbddc->primal_indices_local_idxs);CHKERRQ(ierr);
  ierr = PetscFree(pcbddc->global_primal_indices);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->coarse_subassembling);CHKERRQ(ierr);
  ierr = ISDestroy(&pcbddc->coarse_subassembling_init);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSetUpLocalWorkVectors"
PetscErrorCode PCBDDCSetUpLocalWorkVectors(PC pc)
{
  PC_BDDC        *pcbddc = (PC_BDDC*)pc->data;
  PC_IS          *pcis = (PC_IS*)pc->data;
  VecType        impVecType;
  PetscInt       n_constraints,n_R,old_size;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (!pcbddc->ConstraintMatrix) {
    SETERRQ(PetscObjectComm((PetscObject)pc),PETSC_ERR_PLIB,"BDDC Constraint matrix has not been created");
  }
  /* get sizes */
  n_constraints = pcbddc->local_primal_size - pcbddc->n_actual_vertices;
  n_R = pcis->n-pcbddc->n_actual_vertices;
  ierr = VecGetType(pcis->vec1_N,&impVecType);CHKERRQ(ierr);
  /* local work vectors (try to avoid unneeded work)*/
  /* R nodes */
  old_size = -1;
  if (pcbddc->vec1_R) {
    ierr = VecGetSize(pcbddc->vec1_R,&old_size);CHKERRQ(ierr);
  }
  if (n_R != old_size) {
    ierr = VecDestroy(&pcbddc->vec1_R);CHKERRQ(ierr);
    ierr = VecDestroy(&pcbddc->vec2_R);CHKERRQ(ierr);
    ierr = VecCreate(PetscObjectComm((PetscObject)pcis->vec1_N),&pcbddc->vec1_R);CHKERRQ(ierr);
    ierr = VecSetSizes(pcbddc->vec1_R,PETSC_DECIDE,n_R);CHKERRQ(ierr);
    ierr = VecSetType(pcbddc->vec1_R,impVecType);CHKERRQ(ierr);
    ierr = VecDuplicate(pcbddc->vec1_R,&pcbddc->vec2_R);CHKERRQ(ierr);
  }
  /* local primal dofs */
  old_size = -1;
  if (pcbddc->vec1_P) {
    ierr = VecGetSize(pcbddc->vec1_P,&old_size);CHKERRQ(ierr);
  }
  if (pcbddc->local_primal_size != old_size) {
    ierr = VecDestroy(&pcbddc->vec1_P);CHKERRQ(ierr);
    ierr = VecCreate(PetscObjectComm((PetscObject)pcis->vec1_N),&pcbddc->vec1_P);CHKERRQ(ierr);
    ierr = VecSetSizes(pcbddc->vec1_P,PETSC_DECIDE,pcbddc->local_primal_size);CHKERRQ(ierr);
    ierr = VecSetType(pcbddc->vec1_P,impVecType);CHKERRQ(ierr);
  }
  /* local explicit constraints */
  old_size = -1;
  if (pcbddc->vec1_C) {
    ierr = VecGetSize(pcbddc->vec1_C,&old_size);CHKERRQ(ierr);
  }
  if (n_constraints && n_constraints != old_size) {
    ierr = VecDestroy(&pcbddc->vec1_C);CHKERRQ(ierr);
    ierr = VecCreate(PetscObjectComm((PetscObject)pcis->vec1_N),&pcbddc->vec1_C);CHKERRQ(ierr);
    ierr = VecSetSizes(pcbddc->vec1_C,PETSC_DECIDE,n_constraints);CHKERRQ(ierr);
    ierr = VecSetType(pcbddc->vec1_C,impVecType);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSetUpCorrection"
PetscErrorCode PCBDDCSetUpCorrection(PC pc, PetscScalar **coarse_submat_vals_n)
{
  PetscErrorCode         ierr;
  /* pointers to pcis and pcbddc */
  PC_IS*                 pcis = (PC_IS*)pc->data;
  PC_BDDC*               pcbddc = (PC_BDDC*)pc->data;
  /* submatrices of local problem */
  Mat                    A_RV,A_VR,A_VV;
  /* submatrices of local coarse problem */
  Mat                    S_VV,S_CV,S_VC,S_CC;
  /* working matrices */
  Mat                    C_CR;
  /* additional working stuff */
  PC                     pc_R;
  Mat                    F;
  PetscBool              isLU,isCHOL,isILU;

  PetscScalar            *coarse_submat_vals; /* TODO: use a PETSc matrix */
  PetscScalar            *work;
  PetscInt               *idx_V_B;
  PetscInt               n,n_vertices,n_constraints;
  PetscInt               i,n_R,n_D,n_B;
  PetscBool              unsymmetric_check;
  /* matrix type (vector type propagated downstream from vec1_C and local matrix type) */
  MatType                impMatType;
  /* some shortcuts to scalars */
  PetscScalar            one=1.0,m_one=-1.0;

  PetscFunctionBegin;
  /* get number of vertices (corners plus constraints with change of basis)
     pcbddc->n_actual_vertices stores the actual number of vertices, pcbddc->n_vertices the number of corners computed */
  n_vertices = pcbddc->n_actual_vertices;
  n_constraints = pcbddc->local_primal_size-n_vertices;
  /* Set Non-overlapping dimensions */
  n_B = pcis->n_B; n_D = pcis->n - n_B;
  n_R = pcis->n-n_vertices;

  /* Set types for local objects needed by BDDC precondtioner */
  impMatType = MATSEQDENSE;

  /* vertices in boundary numbering */
  ierr = PetscMalloc1(n_vertices,&idx_V_B);CHKERRQ(ierr);
  ierr = ISGlobalToLocalMappingApply(pcis->BtoNmap,IS_GTOLM_DROP,n_vertices,pcbddc->primal_indices_local_idxs,&i,idx_V_B);CHKERRQ(ierr);
  if (i != n_vertices) {
    SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_PLIB,"Error in boundary numbering for BDDC vertices! %d != %d\n",n_vertices,i);
  }

  /* Subdomain contribution (Non-overlapping) to coarse matrix  */
  ierr = PetscMalloc1(pcbddc->local_primal_size*pcbddc->local_primal_size,&coarse_submat_vals);CHKERRQ(ierr);
  ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_vertices,n_vertices,coarse_submat_vals,&S_VV);CHKERRQ(ierr);
  ierr = MatSeqDenseSetLDA(S_VV,pcbddc->local_primal_size);CHKERRQ(ierr);
  ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_constraints,n_vertices,coarse_submat_vals+n_vertices,&S_CV);CHKERRQ(ierr);
  ierr = MatSeqDenseSetLDA(S_CV,pcbddc->local_primal_size);CHKERRQ(ierr);
  ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_vertices,n_constraints,coarse_submat_vals+pcbddc->local_primal_size*n_vertices,&S_VC);CHKERRQ(ierr);
  ierr = MatSeqDenseSetLDA(S_VC,pcbddc->local_primal_size);CHKERRQ(ierr);
  ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_constraints,n_constraints,coarse_submat_vals+(pcbddc->local_primal_size+1)*n_vertices,&S_CC);CHKERRQ(ierr);
  ierr = MatSeqDenseSetLDA(S_CC,pcbddc->local_primal_size);CHKERRQ(ierr);

  unsymmetric_check = PETSC_FALSE;
  /* allocate workspace */
  n = 0;
  if (n_constraints) {
    n += n_R*n_constraints;
  }
  if (n_vertices) {
    n = PetscMax(2*n_R*n_vertices,n);
  }
  if (!pcbddc->issym) {
    n = PetscMax(2*n_R*pcbddc->local_primal_size,n);
    unsymmetric_check = PETSC_TRUE;
  }
  ierr = PetscMalloc1(n,&work);CHKERRQ(ierr);

  /* determine if can use MatSolve routines instead of calling KSPSolve on ksp_R */
  ierr = KSPGetPC(pcbddc->ksp_R,&pc_R);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)pc_R,PCLU,&isLU);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)pc_R,PCILU,&isILU);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)pc_R,PCCHOLESKY,&isCHOL);CHKERRQ(ierr);
  if (isLU || isILU || isCHOL) {
    ierr = PCFactorGetMatrix(pc_R,&F);CHKERRQ(ierr);
  } else {
    F = NULL;
  }

  /* Precompute stuffs needed for preprocessing and application of BDDC*/
  if (n_constraints) {
    Mat M1,M2,M3;
    IS  is_aux;
    /* see if we can save some allocations */
    if (pcbddc->local_auxmat2) {
      PetscInt on_R,on_constraints;
      ierr = MatGetSize(pcbddc->local_auxmat2,&on_R,&on_constraints);CHKERRQ(ierr);
      if (on_R != n_R || on_constraints != n_constraints) {
        PetscScalar *marray;

        ierr = MatDenseGetArray(pcbddc->local_auxmat2,&marray);CHKERRQ(ierr);
        ierr = PetscFree(marray);CHKERRQ(ierr);
        ierr = MatDestroy(&pcbddc->local_auxmat2);CHKERRQ(ierr);
        ierr = MatDestroy(&pcbddc->local_auxmat1);CHKERRQ(ierr);
      }
    }
    /* auxiliary matrices */
    if (!pcbddc->local_auxmat2) {
      PetscScalar *marray;

      ierr = PetscMalloc1(2*n_R*n_constraints,&marray);CHKERRQ(ierr);
      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_constraints,marray,&pcbddc->local_auxmat2);CHKERRQ(ierr);
      marray += n_R*n_constraints;
      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_constraints,n_R,marray,&pcbddc->local_auxmat1);CHKERRQ(ierr);
    }

    /* Extract constraints on R nodes: C_{CR}  */
    ierr = ISCreateStride(PETSC_COMM_SELF,n_constraints,n_vertices,1,&is_aux);CHKERRQ(ierr);
    ierr = MatGetSubMatrix(pcbddc->ConstraintMatrix,is_aux,pcbddc->is_R_local,MAT_INITIAL_MATRIX,&C_CR);CHKERRQ(ierr);
    ierr = ISDestroy(&is_aux);CHKERRQ(ierr);

    /* Assemble local_auxmat2 = - A_{RR}^{-1} C^T_{CR} needed by BDDC application */
    ierr = PetscMemzero(work,n_R*n_constraints*sizeof(PetscScalar));CHKERRQ(ierr);
    for (i=0;i<n_constraints;i++) {
      const PetscScalar *row_cmat_values;
      const PetscInt    *row_cmat_indices;
      PetscInt          size_of_constraint,j;

      ierr = MatGetRow(C_CR,i,&size_of_constraint,&row_cmat_indices,&row_cmat_values);CHKERRQ(ierr);
      for (j=0;j<size_of_constraint;j++) {
        work[row_cmat_indices[j]+i*n_R] = -row_cmat_values[j];
      }
      ierr = MatRestoreRow(C_CR,i,&size_of_constraint,&row_cmat_indices,&row_cmat_values);CHKERRQ(ierr);
    }
    if (F) {
      Mat B;

      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_constraints,work,&B);CHKERRQ(ierr);
      ierr = MatMatSolve(F,B,pcbddc->local_auxmat2);CHKERRQ(ierr);
      ierr = MatDestroy(&B);CHKERRQ(ierr);
    } else {
      PetscScalar *xarray;
      ierr = MatDenseGetArray(pcbddc->local_auxmat2,&xarray);CHKERRQ(ierr);
      for (i=0;i<n_constraints;i++) {
        ierr = VecPlaceArray(pcbddc->vec1_R,work+i*n_R);CHKERRQ(ierr);
        ierr = VecPlaceArray(pcbddc->vec2_R,xarray+i*n_R);CHKERRQ(ierr);
        ierr = KSPSolve(pcbddc->ksp_R,pcbddc->vec1_R,pcbddc->vec2_R);CHKERRQ(ierr);
        ierr = VecResetArray(pcbddc->vec1_R);CHKERRQ(ierr);
        ierr = VecResetArray(pcbddc->vec2_R);CHKERRQ(ierr);
      }
      ierr = MatDenseRestoreArray(pcbddc->local_auxmat2,&xarray);CHKERRQ(ierr);
    }

    /* Assemble explicitly S_CC = ( C_{CR} A_{RR}^{-1} C^T_{CR} )^{-1}  */
    ierr = MatConvert(C_CR,impMatType,MAT_REUSE_MATRIX,&C_CR);CHKERRQ(ierr);
    ierr = MatMatMult(C_CR,pcbddc->local_auxmat2,MAT_INITIAL_MATRIX,PETSC_DEFAULT,&M3);CHKERRQ(ierr);
    ierr = MatDuplicate(M3,MAT_DO_NOT_COPY_VALUES,&M1);CHKERRQ(ierr);
    ierr = MatDuplicate(M3,MAT_DO_NOT_COPY_VALUES,&M2);CHKERRQ(ierr);
    ierr = MatLUFactor(M3,NULL,NULL,NULL);CHKERRQ(ierr);
    ierr = VecSet(pcbddc->vec1_C,m_one);CHKERRQ(ierr);
    ierr = MatDiagonalSet(M2,pcbddc->vec1_C,INSERT_VALUES);CHKERRQ(ierr);
    ierr = MatMatSolve(M3,M2,M1);CHKERRQ(ierr);
    ierr = MatDestroy(&M2);CHKERRQ(ierr);
    ierr = MatDestroy(&M3);CHKERRQ(ierr);
    /* Assemble local_auxmat1 = S_CC*C_{CR} needed by BDDC application in KSP and in preproc */
    ierr = MatMatMult(M1,C_CR,MAT_REUSE_MATRIX,PETSC_DEFAULT,&pcbddc->local_auxmat1);CHKERRQ(ierr);
    ierr = MatCopy(M1,S_CC,SAME_NONZERO_PATTERN);CHKERRQ(ierr); /* S_CC can have a different LDA, MatMatSolve doesn't support it */
    ierr = MatDestroy(&M1);CHKERRQ(ierr);
  }
  /* Get submatrices from subdomain matrix */
  if (n_vertices) {
    Mat       newmat;
    IS        is_aux;
    PetscInt  ibs,mbs;
    PetscBool issbaij;

    ierr = ISComplement(pcbddc->is_R_local,0,pcis->n,&is_aux);CHKERRQ(ierr);
    ierr = MatGetBlockSize(pcbddc->local_mat,&mbs);CHKERRQ(ierr);
    ierr = ISGetBlockSize(pcbddc->is_R_local,&ibs);CHKERRQ(ierr);
    if (ibs != mbs) { /* need to convert to SEQAIJ */
      ierr = MatConvert(pcbddc->local_mat,MATSEQAIJ,MAT_INITIAL_MATRIX,&newmat);CHKERRQ(ierr);
      ierr = MatGetSubMatrix(newmat,pcbddc->is_R_local,is_aux,MAT_INITIAL_MATRIX,&A_RV);CHKERRQ(ierr);
      ierr = MatGetSubMatrix(newmat,is_aux,pcbddc->is_R_local,MAT_INITIAL_MATRIX,&A_VR);CHKERRQ(ierr);
      ierr = MatGetSubMatrix(newmat,is_aux,is_aux,MAT_INITIAL_MATRIX,&A_VV);CHKERRQ(ierr);
      ierr = MatDestroy(&newmat);CHKERRQ(ierr);
    } else {
      /* this is safe */
      ierr = MatGetSubMatrix(pcbddc->local_mat,is_aux,is_aux,MAT_INITIAL_MATRIX,&A_VV);CHKERRQ(ierr);
      ierr = PetscObjectTypeCompare((PetscObject)pcbddc->local_mat,MATSEQSBAIJ,&issbaij);CHKERRQ(ierr);
      if (issbaij) { /* need to convert to BAIJ to get offdiagonal blocks */
        ierr = MatConvert(pcbddc->local_mat,MATSEQBAIJ,MAT_INITIAL_MATRIX,&newmat);CHKERRQ(ierr);
        ierr = MatGetSubMatrix(newmat,is_aux,pcbddc->is_R_local,MAT_INITIAL_MATRIX,&A_VR);CHKERRQ(ierr);
        ierr = MatTranspose(A_VR,MAT_INITIAL_MATRIX,&A_RV);CHKERRQ(ierr);
        ierr = MatDestroy(&newmat);CHKERRQ(ierr);
        ierr = MatConvert(A_VV,MATSEQBAIJ,MAT_REUSE_MATRIX,&A_VV);CHKERRQ(ierr);
      } else {
        ierr = MatGetSubMatrix(pcbddc->local_mat,pcbddc->is_R_local,is_aux,MAT_INITIAL_MATRIX,&A_RV);CHKERRQ(ierr);
        ierr = MatGetSubMatrix(pcbddc->local_mat,is_aux,pcbddc->is_R_local,MAT_INITIAL_MATRIX,&A_VR);CHKERRQ(ierr);
      }
    }
    ierr = ISDestroy(&is_aux);CHKERRQ(ierr);
  }

  /* Matrix of coarse basis functions (local) */
  if (pcbddc->coarse_phi_B) {
    PetscInt on_B,on_primal,on_D=n_D;
    if (pcbddc->coarse_phi_D) {
      ierr = MatGetSize(pcbddc->coarse_phi_D,&on_D,NULL);CHKERRQ(ierr);
    }
    ierr = MatGetSize(pcbddc->coarse_phi_B,&on_B,&on_primal);CHKERRQ(ierr);
    if (on_B != n_B || on_primal != pcbddc->local_primal_size || on_D != n_D) {
      PetscScalar *marray;

      ierr = MatDenseGetArray(pcbddc->coarse_phi_B,&marray);CHKERRQ(ierr);
      ierr = PetscFree(marray);CHKERRQ(ierr);
      ierr = MatDestroy(&pcbddc->coarse_phi_B);CHKERRQ(ierr);
      ierr = MatDestroy(&pcbddc->coarse_psi_B);CHKERRQ(ierr);
      ierr = MatDestroy(&pcbddc->coarse_phi_D);CHKERRQ(ierr);
      ierr = MatDestroy(&pcbddc->coarse_psi_D);CHKERRQ(ierr);
    }
  }

  if (!pcbddc->coarse_phi_B) {
    PetscScalar *marray;

    n = n_B*pcbddc->local_primal_size;
    if (pcbddc->switch_static || pcbddc->dbg_flag) {
      n += n_D*pcbddc->local_primal_size;
    }
    if (!pcbddc->issym) {
      n *= 2;
    }
    ierr = PetscCalloc1(n,&marray);CHKERRQ(ierr);
    ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_B,pcbddc->local_primal_size,marray,&pcbddc->coarse_phi_B);CHKERRQ(ierr);
    n = n_B*pcbddc->local_primal_size;
    if (pcbddc->switch_static || pcbddc->dbg_flag) {
      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_D,pcbddc->local_primal_size,marray+n,&pcbddc->coarse_phi_D);CHKERRQ(ierr);
      n += n_D*pcbddc->local_primal_size;
    }
    if (!pcbddc->issym) {
      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_B,pcbddc->local_primal_size,marray+n,&pcbddc->coarse_psi_B);CHKERRQ(ierr);
      if (pcbddc->switch_static || pcbddc->dbg_flag) {
        n = n_B*pcbddc->local_primal_size;
        ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_D,pcbddc->local_primal_size,marray+n,&pcbddc->coarse_psi_D);CHKERRQ(ierr);
      }
    } else {
      ierr = PetscObjectReference((PetscObject)pcbddc->coarse_phi_B);CHKERRQ(ierr);
      pcbddc->coarse_psi_B = pcbddc->coarse_phi_B;
      if (pcbddc->switch_static || pcbddc->dbg_flag) {
        ierr = PetscObjectReference((PetscObject)pcbddc->coarse_phi_D);CHKERRQ(ierr);
        pcbddc->coarse_psi_D = pcbddc->coarse_phi_D;
      }
    }
  }
  /* We are now ready to evaluate coarse basis functions and subdomain contribution to coarse problem */
  /* vertices */
  if (n_vertices) {

    if (n_R) {
      Mat          A_RRmA_RV,S_VVt; /* S_VVt with LDA=N */
      PetscBLASInt B_N,B_one = 1;
      PetscScalar  *x,*y;

      ierr = PetscMemzero(work,2*n_R*n_vertices*sizeof(PetscScalar));CHKERRQ(ierr);
      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_vertices,work,&A_RRmA_RV);CHKERRQ(ierr);
      ierr = MatConvert(A_RV,impMatType,MAT_REUSE_MATRIX,&A_RV);CHKERRQ(ierr);
      if (F) {
        ierr = MatMatSolve(F,A_RV,A_RRmA_RV);CHKERRQ(ierr);
      } else {
        ierr = MatDenseGetArray(A_RV,&y);CHKERRQ(ierr);
        for (i=0;i<n_vertices;i++) {
          ierr = VecPlaceArray(pcbddc->vec1_R,y+i*n_R);CHKERRQ(ierr);
          ierr = VecPlaceArray(pcbddc->vec2_R,work+i*n_R);CHKERRQ(ierr);
          ierr = KSPSolve(pcbddc->ksp_R,pcbddc->vec1_R,pcbddc->vec2_R);CHKERRQ(ierr);
          ierr = VecResetArray(pcbddc->vec1_R);CHKERRQ(ierr);
          ierr = VecResetArray(pcbddc->vec2_R);CHKERRQ(ierr);
        }
        ierr = MatDenseRestoreArray(A_RV,&y);CHKERRQ(ierr);
      }
      ierr = MatScale(A_RRmA_RV,m_one);CHKERRQ(ierr);
      /* S_VV and S_CV are the subdomain contribution to coarse matrix. WARNING -> column major ordering */
      if (n_constraints) {
        Mat B;
        ierr = MatMatMult(pcbddc->local_auxmat1,A_RRmA_RV,MAT_REUSE_MATRIX,PETSC_DEFAULT,&S_CV);CHKERRQ(ierr);
        ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_vertices,work+n_R*n_vertices,&B);CHKERRQ(ierr);
        ierr = MatMatMult(pcbddc->local_auxmat2,S_CV,MAT_REUSE_MATRIX,PETSC_DEFAULT,&B);CHKERRQ(ierr);
        ierr = MatScale(S_CV,m_one);CHKERRQ(ierr);
        ierr = PetscBLASIntCast(n_R*n_vertices,&B_N);CHKERRQ(ierr);
        PetscStackCallBLAS("BLASaxpy",BLASaxpy_(&B_N,&one,work+n_R*n_vertices,&B_one,work,&B_one));
        ierr = MatDestroy(&B);CHKERRQ(ierr);
      }
      ierr = MatConvert(A_VR,impMatType,MAT_REUSE_MATRIX,&A_VR);CHKERRQ(ierr);
      ierr = MatMatMult(A_VR,A_RRmA_RV,MAT_INITIAL_MATRIX,PETSC_DEFAULT,&S_VVt);CHKERRQ(ierr);
      ierr = MatConvert(A_VV,impMatType,MAT_REUSE_MATRIX,&A_VV);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(n_vertices*n_vertices,&B_N);CHKERRQ(ierr);
      ierr = MatDenseGetArray(A_VV,&x);CHKERRQ(ierr);
      ierr = MatDenseGetArray(S_VVt,&y);CHKERRQ(ierr);
      PetscStackCallBLAS("BLASaxpy",BLASaxpy_(&B_N,&one,x,&B_one,y,&B_one));
      ierr = MatDenseRestoreArray(A_VV,&x);CHKERRQ(ierr);
      ierr = MatDenseRestoreArray(S_VVt,&y);CHKERRQ(ierr);
      ierr = MatCopy(S_VVt,S_VV,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
      ierr = MatDestroy(&S_VVt);CHKERRQ(ierr);
      ierr = MatDestroy(&A_RRmA_RV);CHKERRQ(ierr);
    } else {
      ierr = MatConvert(A_VV,impMatType,MAT_REUSE_MATRIX,&A_VV);CHKERRQ(ierr);
      ierr = MatCopy(A_VV,S_VV,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
    }
    /* coarse basis functions */
    for (i=0;i<n_vertices;i++) {
      PetscScalar *y;

      ierr = VecPlaceArray(pcbddc->vec1_R,work+n_R*i);CHKERRQ(ierr);
      ierr = MatDenseGetArray(pcbddc->coarse_phi_B,&y);CHKERRQ(ierr);
      ierr = VecPlaceArray(pcis->vec1_B,y+n_B*i);CHKERRQ(ierr);
      ierr = VecScatterBegin(pcbddc->R_to_B,pcbddc->vec1_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterEnd(pcbddc->R_to_B,pcbddc->vec1_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      y[n_B*i+idx_V_B[i]] = 1.0;
      ierr = MatDenseRestoreArray(pcbddc->coarse_phi_B,&y);CHKERRQ(ierr);
      ierr = VecResetArray(pcis->vec1_B);CHKERRQ(ierr);

      if (pcbddc->switch_static || pcbddc->dbg_flag) {
        ierr = MatDenseGetArray(pcbddc->coarse_phi_D,&y);CHKERRQ(ierr);
        ierr = VecPlaceArray(pcis->vec1_D,y+n_D*i);CHKERRQ(ierr);
        ierr = VecScatterBegin(pcbddc->R_to_D,pcbddc->vec1_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
        ierr = VecScatterEnd(pcbddc->R_to_D,pcbddc->vec1_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
        ierr = VecResetArray(pcis->vec1_D);CHKERRQ(ierr);
        ierr = MatDenseRestoreArray(pcbddc->coarse_phi_D,&y);CHKERRQ(ierr);
      }
      ierr = VecResetArray(pcbddc->vec1_R);CHKERRQ(ierr);
    }
    ierr = MatDestroy(&A_VV);CHKERRQ(ierr);
    ierr = MatDestroy(&A_RV);CHKERRQ(ierr);
  }

  if (n_constraints) {
    Mat B;

    ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_constraints,work,&B);CHKERRQ(ierr);
    ierr = MatScale(S_CC,m_one);CHKERRQ(ierr);
    ierr = MatMatMult(pcbddc->local_auxmat2,S_CC,MAT_REUSE_MATRIX,PETSC_DEFAULT,&B);CHKERRQ(ierr);
    ierr = MatScale(S_CC,m_one);CHKERRQ(ierr);
    if (n_vertices) {
      ierr = MatMatMult(A_VR,B,MAT_REUSE_MATRIX,PETSC_DEFAULT,&S_VC);CHKERRQ(ierr);
    }
    ierr = MatDestroy(&B);CHKERRQ(ierr);
    /* coarse basis functions */
    for (i=0;i<n_constraints;i++) {
      PetscScalar *y;

      ierr = VecPlaceArray(pcbddc->vec1_R,work+n_R*i);CHKERRQ(ierr);
      ierr = MatDenseGetArray(pcbddc->coarse_phi_B,&y);CHKERRQ(ierr);
      ierr = VecPlaceArray(pcis->vec1_B,y+n_B*(i+n_vertices));CHKERRQ(ierr);
      ierr = VecScatterBegin(pcbddc->R_to_B,pcbddc->vec1_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterEnd(pcbddc->R_to_B,pcbddc->vec1_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = MatDenseRestoreArray(pcbddc->coarse_phi_B,&y);CHKERRQ(ierr);
      ierr = VecResetArray(pcis->vec1_B);CHKERRQ(ierr);

      if (pcbddc->switch_static || pcbddc->dbg_flag) {
        ierr = MatDenseGetArray(pcbddc->coarse_phi_D,&y);CHKERRQ(ierr);
        ierr = VecPlaceArray(pcis->vec1_D,y+n_D*(i+n_vertices));CHKERRQ(ierr);
        ierr = VecScatterBegin(pcbddc->R_to_D,pcbddc->vec1_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
        ierr = VecScatterEnd(pcbddc->R_to_D,pcbddc->vec1_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
        ierr = VecResetArray(pcis->vec1_D);CHKERRQ(ierr);
        ierr = MatDenseRestoreArray(pcbddc->coarse_phi_D,&y);CHKERRQ(ierr);
      }
      ierr = VecResetArray(pcbddc->vec1_R);CHKERRQ(ierr);
    }
  }

  /* compute other basis functions for non-symmetric problems */
  if (!pcbddc->issym) {
    Mat B,X;

    ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,pcbddc->local_primal_size,work,&B);CHKERRQ(ierr);

    if (n_constraints) {
      Mat S_CCT,B_C;

      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_constraints,work+n_vertices*n_R,&B_C);CHKERRQ(ierr);
      ierr = MatTranspose(S_CC,MAT_INITIAL_MATRIX,&S_CCT);CHKERRQ(ierr);
      ierr = MatTransposeMatMult(C_CR,S_CCT,MAT_REUSE_MATRIX,PETSC_DEFAULT,&B_C);CHKERRQ(ierr);
      ierr = MatDestroy(&S_CCT);CHKERRQ(ierr);
      if (n_vertices) {
        Mat B_V,S_VCT;

        ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_vertices,work,&B_V);CHKERRQ(ierr);
        ierr = MatTranspose(S_VC,MAT_INITIAL_MATRIX,&S_VCT);CHKERRQ(ierr);
        ierr = MatTransposeMatMult(C_CR,S_VCT,MAT_REUSE_MATRIX,PETSC_DEFAULT,&B_V);CHKERRQ(ierr);
        ierr = MatDestroy(&B_V);CHKERRQ(ierr);
        ierr = MatDestroy(&S_VCT);CHKERRQ(ierr);
      }
      ierr = MatDestroy(&B_C);CHKERRQ(ierr);
    }
    if (n_vertices && n_R) {
      Mat          A_VRT;
      PetscBLASInt B_N,B_one = 1;

      if (!n_constraints) { /* if there are no constraints, reset work */
        ierr = PetscMemzero(work,n_R*pcbddc->local_primal_size*sizeof(PetscScalar));CHKERRQ(ierr);
      }
      ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,n_vertices,work+pcbddc->local_primal_size*n_R,&A_VRT);CHKERRQ(ierr);
      ierr = MatTranspose(A_VR,MAT_REUSE_MATRIX,&A_VRT);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(n_vertices*n_R,&B_N);CHKERRQ(ierr);
      PetscStackCallBLAS("BLASaxpy",BLASaxpy_(&B_N,&m_one,work+pcbddc->local_primal_size*n_R,&B_one,work,&B_one));
      ierr = MatDestroy(&A_VRT);CHKERRQ(ierr);
    }

    ierr = MatCreateSeqDense(PETSC_COMM_SELF,n_R,pcbddc->local_primal_size,work+pcbddc->local_primal_size*n_R,&X);CHKERRQ(ierr);
    if (F) { /* currently there's no support for MatTransposeMatSolve(F,B,X) */
      for (i=0;i<pcbddc->local_primal_size;i++) {
        ierr = VecPlaceArray(pcbddc->vec1_R,work+i*n_R);CHKERRQ(ierr);
        ierr = VecPlaceArray(pcbddc->vec2_R,work+(i+pcbddc->local_primal_size)*n_R);CHKERRQ(ierr);
        ierr = MatSolveTranspose(F,pcbddc->vec1_R,pcbddc->vec2_R);CHKERRQ(ierr);
        ierr = VecResetArray(pcbddc->vec1_R);CHKERRQ(ierr);
        ierr = VecResetArray(pcbddc->vec2_R);CHKERRQ(ierr);
      }
    } else {
      for (i=0;i<pcbddc->local_primal_size;i++) {
        ierr = VecPlaceArray(pcbddc->vec1_R,work+i*n_R);CHKERRQ(ierr);
        ierr = VecPlaceArray(pcbddc->vec2_R,work+(i+pcbddc->local_primal_size)*n_R);CHKERRQ(ierr);
        ierr = KSPSolveTranspose(pcbddc->ksp_R,pcbddc->vec1_R,pcbddc->vec2_R);CHKERRQ(ierr);
        ierr = VecResetArray(pcbddc->vec1_R);CHKERRQ(ierr);
        ierr = VecResetArray(pcbddc->vec2_R);CHKERRQ(ierr);
      }
    }
    ierr = MatDestroy(&B);CHKERRQ(ierr);
    /* coarse basis functions */
    for (i=0;i<pcbddc->local_primal_size;i++) {
      PetscScalar *y;

      ierr = VecPlaceArray(pcbddc->vec1_R,work+n_R*(i+pcbddc->local_primal_size));CHKERRQ(ierr);
      ierr = MatDenseGetArray(pcbddc->coarse_psi_B,&y);CHKERRQ(ierr);
      ierr = VecPlaceArray(pcis->vec1_B,y+n_B*i);CHKERRQ(ierr);
      ierr = VecScatterBegin(pcbddc->R_to_B,pcbddc->vec1_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterEnd(pcbddc->R_to_B,pcbddc->vec1_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      if (i<n_vertices) {
        y[n_B*i+idx_V_B[i]] = 1.0;
      }
      ierr = MatDenseRestoreArray(pcbddc->coarse_psi_B,&y);CHKERRQ(ierr);
      ierr = VecResetArray(pcis->vec1_B);CHKERRQ(ierr);

      if (pcbddc->switch_static || pcbddc->dbg_flag) {
        ierr = MatDenseGetArray(pcbddc->coarse_psi_D,&y);CHKERRQ(ierr);
        ierr = VecPlaceArray(pcis->vec1_D,y+n_D*i);CHKERRQ(ierr);
        ierr = VecScatterBegin(pcbddc->R_to_D,pcbddc->vec1_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
        ierr = VecScatterEnd(pcbddc->R_to_D,pcbddc->vec1_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
        ierr = VecResetArray(pcis->vec1_D);CHKERRQ(ierr);
        ierr = MatDenseRestoreArray(pcbddc->coarse_psi_D,&y);CHKERRQ(ierr);
      }
      ierr = VecResetArray(pcbddc->vec1_R);CHKERRQ(ierr);
    }
    ierr = MatDestroy(&X);CHKERRQ(ierr);
  }

  ierr = PetscFree(idx_V_B);CHKERRQ(ierr);
  ierr = MatDestroy(&S_VV);CHKERRQ(ierr);
  ierr = MatDestroy(&S_CV);CHKERRQ(ierr);
  ierr = MatDestroy(&S_VC);CHKERRQ(ierr);
  ierr = MatDestroy(&S_CC);CHKERRQ(ierr);
  /* Checking coarse_sub_mat and coarse basis functios */
  /* Symmetric case     : It should be \Phi^{(j)^T} A^{(j)} \Phi^{(j)}=coarse_sub_mat */
  /* Non-symmetric case : It should be \Psi^{(j)^T} A^{(j)} \Phi^{(j)}=coarse_sub_mat */
  if (pcbddc->dbg_flag) {
    Mat         coarse_sub_mat;
    Mat         AUXMAT,TM1,TM2,TM3,TM4;
    Mat         coarse_phi_D,coarse_phi_B;
    Mat         coarse_psi_D,coarse_psi_B;
    Mat         A_II,A_BB,A_IB,A_BI;
    Mat         C_B,CPHI;
    IS          is_dummy;
    Vec         mones;
    MatType     checkmattype=MATSEQAIJ;
    PetscReal   real_value;

    ierr = MatConvert(pcis->A_II,checkmattype,MAT_INITIAL_MATRIX,&A_II);CHKERRQ(ierr);
    ierr = MatConvert(pcis->A_IB,checkmattype,MAT_INITIAL_MATRIX,&A_IB);CHKERRQ(ierr);
    ierr = MatConvert(pcis->A_BI,checkmattype,MAT_INITIAL_MATRIX,&A_BI);CHKERRQ(ierr);
    ierr = MatConvert(pcis->A_BB,checkmattype,MAT_INITIAL_MATRIX,&A_BB);CHKERRQ(ierr);
    ierr = MatConvert(pcbddc->coarse_phi_D,checkmattype,MAT_INITIAL_MATRIX,&coarse_phi_D);CHKERRQ(ierr);
    ierr = MatConvert(pcbddc->coarse_phi_B,checkmattype,MAT_INITIAL_MATRIX,&coarse_phi_B);CHKERRQ(ierr);
    if (unsymmetric_check) {
      ierr = MatConvert(pcbddc->coarse_psi_D,checkmattype,MAT_INITIAL_MATRIX,&coarse_psi_D);CHKERRQ(ierr);
      ierr = MatConvert(pcbddc->coarse_psi_B,checkmattype,MAT_INITIAL_MATRIX,&coarse_psi_B);CHKERRQ(ierr);
    }
    ierr = MatCreateSeqDense(PETSC_COMM_SELF,pcbddc->local_primal_size,pcbddc->local_primal_size,coarse_submat_vals,&coarse_sub_mat);CHKERRQ(ierr);

    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Check coarse sub mat computation\n");CHKERRQ(ierr);
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    if (unsymmetric_check) {
      ierr = MatMatMult(A_II,coarse_phi_D,MAT_INITIAL_MATRIX,1.0,&AUXMAT);CHKERRQ(ierr);
      ierr = MatTransposeMatMult(coarse_psi_D,AUXMAT,MAT_INITIAL_MATRIX,1.0,&TM1);CHKERRQ(ierr);
      ierr = MatDestroy(&AUXMAT);CHKERRQ(ierr);
      ierr = MatMatMult(A_BB,coarse_phi_B,MAT_INITIAL_MATRIX,1.0,&AUXMAT);CHKERRQ(ierr);
      ierr = MatTransposeMatMult(coarse_psi_B,AUXMAT,MAT_INITIAL_MATRIX,1.0,&TM2);CHKERRQ(ierr);
      ierr = MatDestroy(&AUXMAT);CHKERRQ(ierr);
      ierr = MatMatMult(A_IB,coarse_phi_B,MAT_INITIAL_MATRIX,1.0,&AUXMAT);CHKERRQ(ierr);
      ierr = MatTransposeMatMult(coarse_psi_D,AUXMAT,MAT_INITIAL_MATRIX,1.0,&TM3);CHKERRQ(ierr);
      ierr = MatDestroy(&AUXMAT);CHKERRQ(ierr);
      ierr = MatMatMult(A_BI,coarse_phi_D,MAT_INITIAL_MATRIX,1.0,&AUXMAT);CHKERRQ(ierr);
      ierr = MatTransposeMatMult(coarse_psi_B,AUXMAT,MAT_INITIAL_MATRIX,1.0,&TM4);CHKERRQ(ierr);
      ierr = MatDestroy(&AUXMAT);CHKERRQ(ierr);
    } else {
      ierr = MatPtAP(A_II,coarse_phi_D,MAT_INITIAL_MATRIX,1.0,&TM1);CHKERRQ(ierr);
      ierr = MatPtAP(A_BB,coarse_phi_B,MAT_INITIAL_MATRIX,1.0,&TM2);CHKERRQ(ierr);
      ierr = MatMatMult(A_IB,coarse_phi_B,MAT_INITIAL_MATRIX,1.0,&AUXMAT);CHKERRQ(ierr);
      ierr = MatTransposeMatMult(coarse_phi_D,AUXMAT,MAT_INITIAL_MATRIX,1.0,&TM3);CHKERRQ(ierr);
      ierr = MatDestroy(&AUXMAT);CHKERRQ(ierr);
      ierr = MatMatMult(A_BI,coarse_phi_D,MAT_INITIAL_MATRIX,1.0,&AUXMAT);CHKERRQ(ierr);
      ierr = MatTransposeMatMult(coarse_phi_B,AUXMAT,MAT_INITIAL_MATRIX,1.0,&TM4);CHKERRQ(ierr);
      ierr = MatDestroy(&AUXMAT);CHKERRQ(ierr);
    }
    ierr = MatAXPY(TM1,one,TM2,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
    ierr = MatAXPY(TM1,one,TM3,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
    ierr = MatAXPY(TM1,one,TM4,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
    ierr = MatConvert(TM1,MATSEQDENSE,MAT_REUSE_MATRIX,&TM1);CHKERRQ(ierr);
    ierr = MatAXPY(TM1,m_one,coarse_sub_mat,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
    ierr = MatNorm(TM1,NORM_FROBENIUS,&real_value);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedAllow(pcbddc->dbg_viewer,PETSC_TRUE);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d      matrix error % 1.14e\n",PetscGlobalRank,real_value);CHKERRQ(ierr);

    /* check constraints */
    ierr = ISCreateStride(PETSC_COMM_SELF,pcbddc->local_primal_size,0,1,&is_dummy);CHKERRQ(ierr);
    ierr = MatGetSubMatrix(pcbddc->ConstraintMatrix,is_dummy,pcis->is_B_local,MAT_INITIAL_MATRIX,&C_B);
    ierr = MatMatMult(C_B,coarse_phi_B,MAT_INITIAL_MATRIX,1.0,&CPHI);CHKERRQ(ierr);
    ierr = MatCreateVecs(CPHI,&mones,NULL);CHKERRQ(ierr);
    ierr = VecSet(mones,-1.0);CHKERRQ(ierr);
    ierr = MatDiagonalSet(CPHI,mones,ADD_VALUES);CHKERRQ(ierr);
    ierr = MatNorm(CPHI,NORM_FROBENIUS,&real_value);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d constraints error % 1.14e\n",PetscGlobalRank,real_value);CHKERRQ(ierr);
    ierr = MatDestroy(&C_B);CHKERRQ(ierr);
    ierr = MatDestroy(&CPHI);CHKERRQ(ierr);
    ierr = ISDestroy(&is_dummy);CHKERRQ(ierr);
    ierr = VecDestroy(&mones);CHKERRQ(ierr);

    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    ierr = MatDestroy(&A_II);CHKERRQ(ierr);
    ierr = MatDestroy(&A_BB);CHKERRQ(ierr);
    ierr = MatDestroy(&A_IB);CHKERRQ(ierr);
    ierr = MatDestroy(&A_BI);CHKERRQ(ierr);
    ierr = MatDestroy(&TM1);CHKERRQ(ierr);
    ierr = MatDestroy(&TM2);CHKERRQ(ierr);
    ierr = MatDestroy(&TM3);CHKERRQ(ierr);
    ierr = MatDestroy(&TM4);CHKERRQ(ierr);
    ierr = MatDestroy(&coarse_phi_D);CHKERRQ(ierr);
    ierr = MatDestroy(&coarse_phi_B);CHKERRQ(ierr);
    if (unsymmetric_check) {
      ierr = MatDestroy(&coarse_psi_D);CHKERRQ(ierr);
      ierr = MatDestroy(&coarse_psi_B);CHKERRQ(ierr);
    }
    ierr = MatDestroy(&coarse_sub_mat);CHKERRQ(ierr);
  }

  /* free memory */
  ierr = PetscFree(work);CHKERRQ(ierr);
  if (n_vertices) {
    ierr = MatDestroy(&A_VR);CHKERRQ(ierr);
  }
  if (n_constraints) {
    ierr = MatDestroy(&C_CR);CHKERRQ(ierr);
  }
  /* get back data */
  *coarse_submat_vals_n = coarse_submat_vals;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatGetSubMatrixUnsorted"
PetscErrorCode MatGetSubMatrixUnsorted(Mat A, IS isrow, IS iscol, Mat* B)
{
  Mat            *work_mat;
  IS             isrow_s,iscol_s;
  PetscBool      rsorted,csorted;
  PetscInt       rsize,*idxs_perm_r,csize,*idxs_perm_c;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = ISSorted(isrow,&rsorted);CHKERRQ(ierr);
  ierr = ISSorted(iscol,&csorted);CHKERRQ(ierr);
  ierr = ISGetLocalSize(isrow,&rsize);CHKERRQ(ierr);
  ierr = ISGetLocalSize(iscol,&csize);CHKERRQ(ierr);

  if (!rsorted) {
    const PetscInt *idxs;
    PetscInt *idxs_sorted,i;

    ierr = PetscMalloc1(rsize,&idxs_perm_r);CHKERRQ(ierr);
    ierr = PetscMalloc1(rsize,&idxs_sorted);CHKERRQ(ierr);
    for (i=0;i<rsize;i++) {
      idxs_perm_r[i] = i;
    }
    ierr = ISGetIndices(isrow,&idxs);CHKERRQ(ierr);
    ierr = PetscSortIntWithPermutation(rsize,idxs,idxs_perm_r);CHKERRQ(ierr);
    for (i=0;i<rsize;i++) {
      idxs_sorted[i] = idxs[idxs_perm_r[i]];
    }
    ierr = ISRestoreIndices(isrow,&idxs);CHKERRQ(ierr);
    ierr = ISCreateGeneral(PETSC_COMM_SELF,rsize,idxs_sorted,PETSC_OWN_POINTER,&isrow_s);CHKERRQ(ierr);
  } else {
    ierr = PetscObjectReference((PetscObject)isrow);CHKERRQ(ierr);
    isrow_s = isrow;
  }

  if (!csorted) {
    if (isrow == iscol) {
      ierr = PetscObjectReference((PetscObject)isrow_s);CHKERRQ(ierr);
      iscol_s = isrow_s;
    } else {
      const PetscInt *idxs;
      PetscInt *idxs_sorted,i;

      ierr = PetscMalloc1(csize,&idxs_perm_c);CHKERRQ(ierr);
      ierr = PetscMalloc1(csize,&idxs_sorted);CHKERRQ(ierr);
      for (i=0;i<csize;i++) {
        idxs_perm_c[i] = i;
      }
      ierr = ISGetIndices(iscol,&idxs);CHKERRQ(ierr);
      ierr = PetscSortIntWithPermutation(csize,idxs,idxs_perm_c);CHKERRQ(ierr);
      for (i=0;i<csize;i++) {
        idxs_sorted[i] = idxs[idxs_perm_c[i]];
      }
      ierr = ISRestoreIndices(iscol,&idxs);CHKERRQ(ierr);
      ierr = ISCreateGeneral(PETSC_COMM_SELF,csize,idxs_sorted,PETSC_OWN_POINTER,&iscol_s);CHKERRQ(ierr);
    }
  } else {
    ierr = PetscObjectReference((PetscObject)iscol);CHKERRQ(ierr);
    iscol_s = iscol;
  }

  ierr = MatGetSubMatrices(A,1,&isrow_s,&iscol_s,MAT_INITIAL_MATRIX,&work_mat);CHKERRQ(ierr);

  if (!rsorted || !csorted) {
    Mat      new_mat;
    IS       is_perm_r,is_perm_c;

    if (!rsorted) {
      PetscInt *idxs_r,i;
      ierr = PetscMalloc1(rsize,&idxs_r);CHKERRQ(ierr);
      for (i=0;i<rsize;i++) {
        idxs_r[idxs_perm_r[i]] = i;
      }
      ierr = PetscFree(idxs_perm_r);CHKERRQ(ierr);
      ierr = ISCreateGeneral(PETSC_COMM_SELF,rsize,idxs_r,PETSC_OWN_POINTER,&is_perm_r);CHKERRQ(ierr);
    } else {
      ierr = ISCreateStride(PETSC_COMM_SELF,rsize,0,1,&is_perm_r);CHKERRQ(ierr);
    }
    ierr = ISSetPermutation(is_perm_r);CHKERRQ(ierr);

    if (!csorted) {
      if (isrow_s == iscol_s) {
        ierr = PetscObjectReference((PetscObject)is_perm_r);CHKERRQ(ierr);
        is_perm_c = is_perm_r;
      } else {
        PetscInt *idxs_c,i;
        ierr = PetscMalloc1(csize,&idxs_c);CHKERRQ(ierr);
        for (i=0;i<csize;i++) {
          idxs_c[idxs_perm_c[i]] = i;
        }
        ierr = PetscFree(idxs_perm_c);CHKERRQ(ierr);
        ierr = ISCreateGeneral(PETSC_COMM_SELF,csize,idxs_c,PETSC_OWN_POINTER,&is_perm_c);CHKERRQ(ierr);
      }
    } else {
      ierr = ISCreateStride(PETSC_COMM_SELF,csize,0,1,&is_perm_c);CHKERRQ(ierr);
    }
    ierr = ISSetPermutation(is_perm_c);CHKERRQ(ierr);

    ierr = MatPermute(work_mat[0],is_perm_r,is_perm_c,&new_mat);CHKERRQ(ierr);
    ierr = MatDestroy(&work_mat[0]);CHKERRQ(ierr);
    work_mat[0] = new_mat;
    ierr = ISDestroy(&is_perm_r);CHKERRQ(ierr);
    ierr = ISDestroy(&is_perm_c);CHKERRQ(ierr);
  }

  ierr = PetscObjectReference((PetscObject)work_mat[0]);CHKERRQ(ierr);
  *B = work_mat[0];
  ierr = MatDestroyMatrices(1,&work_mat);CHKERRQ(ierr);
  ierr = ISDestroy(&isrow_s);CHKERRQ(ierr);
  ierr = ISDestroy(&iscol_s);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCComputeLocalMatrix"
PetscErrorCode PCBDDCComputeLocalMatrix(PC pc, Mat ChangeOfBasisMatrix)
{
  Mat_IS*        matis = (Mat_IS*)pc->pmat->data;
  PC_BDDC*       pcbddc = (PC_BDDC*)pc->data;
  Mat            new_mat;
  IS             is_local,is_global;
  PetscInt       local_size;
  PetscBool      isseqaij;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatDestroy(&pcbddc->local_mat);CHKERRQ(ierr);
  ierr = MatGetSize(matis->A,&local_size,NULL);CHKERRQ(ierr);
  ierr = ISCreateStride(PetscObjectComm((PetscObject)matis->A),local_size,0,1,&is_local);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingApplyIS(matis->mapping,is_local,&is_global);CHKERRQ(ierr);
  ierr = ISDestroy(&is_local);CHKERRQ(ierr);
  ierr = MatGetSubMatrixUnsorted(ChangeOfBasisMatrix,is_global,is_global,&new_mat);CHKERRQ(ierr);
  ierr = ISDestroy(&is_global);CHKERRQ(ierr);

  /* check */
  if (pcbddc->dbg_flag) {
    Vec       x,x_change;
    PetscReal error;

    ierr = MatCreateVecs(ChangeOfBasisMatrix,&x,&x_change);CHKERRQ(ierr);
    ierr = VecSetRandom(x,NULL);CHKERRQ(ierr);
    ierr = MatMult(ChangeOfBasisMatrix,x,x_change);CHKERRQ(ierr);
    ierr = VecScatterBegin(matis->ctx,x,matis->x,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecScatterEnd(matis->ctx,x,matis->x,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = MatMult(new_mat,matis->x,matis->y);CHKERRQ(ierr);
    ierr = VecScatterBegin(matis->ctx,matis->y,x,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecScatterEnd(matis->ctx,matis->y,x,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecAXPY(x,-1.0,x_change);CHKERRQ(ierr);
    ierr = VecNorm(x,NORM_INFINITY,&error);CHKERRQ(ierr);
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Error global vs local change on N: %1.6e\n",error);CHKERRQ(ierr);
    ierr = VecDestroy(&x);CHKERRQ(ierr);
    ierr = VecDestroy(&x_change);CHKERRQ(ierr);
  }

  /* TODO: HOW TO WORK WITH BAIJ and SBAIJ and SEQDENSE? */
  ierr = PetscObjectTypeCompare((PetscObject)matis->A,MATSEQAIJ,&isseqaij);CHKERRQ(ierr);
  if (isseqaij) {
    ierr = MatPtAP(matis->A,new_mat,MAT_INITIAL_MATRIX,2.0,&pcbddc->local_mat);CHKERRQ(ierr);
  } else {
    Mat work_mat;
    ierr = MatConvert(matis->A,MATSEQAIJ,MAT_INITIAL_MATRIX,&work_mat);CHKERRQ(ierr);
    ierr = MatPtAP(work_mat,new_mat,MAT_INITIAL_MATRIX,2.0,&pcbddc->local_mat);CHKERRQ(ierr);
    ierr = MatDestroy(&work_mat);CHKERRQ(ierr);
  }
  ierr = MatSetOption(pcbddc->local_mat,MAT_SYMMETRIC,pcbddc->issym);CHKERRQ(ierr);
#if !defined(PETSC_USE_COMPLEX)
  ierr = MatSetOption(pcbddc->local_mat,MAT_HERMITIAN,pcbddc->issym);CHKERRQ(ierr);
#endif
  /*
  ierr = PetscViewerSetFormat(PETSC_VIEWER_STDOUT_SELF,PETSC_VIEWER_ASCII_MATLAB);CHKERRQ(ierr);
  ierr = MatView(new_mat,(PetscViewer)0);CHKERRQ(ierr);
  */
  ierr = MatDestroy(&new_mat);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSetUpLocalScatters"
PetscErrorCode PCBDDCSetUpLocalScatters(PC pc)
{
  PC_IS*         pcis = (PC_IS*)(pc->data);
  PC_BDDC*       pcbddc = (PC_BDDC*)pc->data;
  IS             is_aux1,is_aux2;
  PetscInt       *aux_array1,*aux_array2,*is_indices,*idx_R_local;
  PetscInt       n_vertices,i,j,n_R,n_D,n_B;
  PetscInt       vbs,bs;
  PetscBT        bitmask;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /*
    No need to setup local scatters if
      - primal space is unchanged
        AND
      - we actually have locally some primal dofs (could not be true in multilevel or for isolated subdomains)
        AND
      - we are not in debugging mode (this is needed since there are Synchronized prints at the end of the subroutine
  */
  if (!pcbddc->new_primal_space_local && pcbddc->local_primal_size && !pcbddc->dbg_flag) {
    PetscFunctionReturn(0);
  }
  /* destroy old objects */
  ierr = ISDestroy(&pcbddc->is_R_local);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&pcbddc->R_to_B);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&pcbddc->R_to_D);CHKERRQ(ierr);
  /* Set Non-overlapping dimensions */
  n_B = pcis->n_B; n_D = pcis->n - n_B;
  n_vertices = pcbddc->n_actual_vertices;
  /* create auxiliary bitmask */
  ierr = PetscBTCreate(pcis->n,&bitmask);CHKERRQ(ierr);
  for (i=0;i<n_vertices;i++) {
    ierr = PetscBTSet(bitmask,pcbddc->primal_indices_local_idxs[i]);CHKERRQ(ierr);
  }

  /* Dohrmann's notation: dofs splitted in R (Remaining: all dofs but the vertices) and V (Vertices) */
  ierr = PetscMalloc1(pcis->n-n_vertices,&idx_R_local);CHKERRQ(ierr);
  for (i=0, n_R=0; i<pcis->n; i++) {
    if (!PetscBTLookup(bitmask,i)) {
      idx_R_local[n_R] = i;
      n_R++;
    }
  }

  /* Block code */
  vbs = 1;
  ierr = MatGetBlockSize(pcbddc->local_mat,&bs);CHKERRQ(ierr);
  if (bs>1 && !(n_vertices%bs)) {
    PetscBool is_blocked = PETSC_TRUE;
    PetscInt  *vary;
    /* Verify if the vertex indices correspond to each element in a block (code taken from sbaij2.c) */
    ierr = PetscMalloc1(pcis->n/bs,&vary);CHKERRQ(ierr);
    ierr = PetscMemzero(vary,pcis->n/bs*sizeof(PetscInt));CHKERRQ(ierr);
    for (i=0; i<n_vertices; i++) vary[pcbddc->primal_indices_local_idxs[i]/bs]++;
    for (i=0; i<n_vertices/bs; i++) {
      if (vary[i]!=0 && vary[i]!=bs) {
        is_blocked = PETSC_FALSE;
        break;
      }
    }
    if (is_blocked) { /* build compressed IS for R nodes (complement of vertices) */
      vbs = bs;
      for (i=0;i<n_R/vbs;i++) {
        idx_R_local[i] = idx_R_local[vbs*i]/vbs;
      }
    }
    ierr = PetscFree(vary);CHKERRQ(ierr);
  }
  ierr = ISCreateBlock(PETSC_COMM_SELF,vbs,n_R/vbs,idx_R_local,PETSC_COPY_VALUES,&pcbddc->is_R_local);CHKERRQ(ierr);
  ierr = PetscFree(idx_R_local);CHKERRQ(ierr);

  /* print some info if requested */
  if (pcbddc->dbg_flag) {
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedAllow(pcbddc->dbg_viewer,PETSC_TRUE);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d local dimensions\n",PetscGlobalRank);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"local_size = %d, dirichlet_size = %d, boundary_size = %d\n",pcis->n,n_D,n_B);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"r_size = %d, v_size = %d, constraints = %d, local_primal_size = %d\n",n_R,n_vertices,pcbddc->local_primal_size-n_vertices,pcbddc->local_primal_size);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"pcbddc->n_vertices = %d, pcbddc->n_constraints = %d\n",pcbddc->n_vertices,pcbddc->n_constraints);CHKERRQ(ierr);
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
  }

  /* VecScatters pcbddc->R_to_B and (optionally) pcbddc->R_to_D */
  ierr = ISGetIndices(pcbddc->is_R_local,(const PetscInt**)&idx_R_local);CHKERRQ(ierr);
  ierr = PetscMalloc1(pcis->n_B-n_vertices,&aux_array1);CHKERRQ(ierr);
  ierr = PetscMalloc1(pcis->n_B-n_vertices,&aux_array2);CHKERRQ(ierr);
  ierr = ISGetIndices(pcis->is_I_local,(const PetscInt**)&is_indices);CHKERRQ(ierr);
  for (i=0; i<n_D; i++) {
    ierr = PetscBTSet(bitmask,is_indices[i]);CHKERRQ(ierr);
  }
  ierr = ISRestoreIndices(pcis->is_I_local,(const PetscInt**)&is_indices);CHKERRQ(ierr);
  for (i=0, j=0; i<n_R; i++) {
    if (!PetscBTLookup(bitmask,idx_R_local[i])) {
      aux_array1[j++] = i;
    }
  }
  ierr = ISCreateGeneral(PETSC_COMM_SELF,j,aux_array1,PETSC_OWN_POINTER,&is_aux1);CHKERRQ(ierr);
  ierr = ISGetIndices(pcis->is_B_local,(const PetscInt**)&is_indices);CHKERRQ(ierr);
  for (i=0, j=0; i<n_B; i++) {
    if (!PetscBTLookup(bitmask,is_indices[i])) {
      aux_array2[j++] = i;
    }
  }
  ierr = ISRestoreIndices(pcis->is_B_local,(const PetscInt**)&is_indices);CHKERRQ(ierr);
  ierr = ISCreateGeneral(PETSC_COMM_SELF,j,aux_array2,PETSC_OWN_POINTER,&is_aux2);CHKERRQ(ierr);
  ierr = VecScatterCreate(pcbddc->vec1_R,is_aux1,pcis->vec1_B,is_aux2,&pcbddc->R_to_B);CHKERRQ(ierr);
  ierr = ISDestroy(&is_aux1);CHKERRQ(ierr);
  ierr = ISDestroy(&is_aux2);CHKERRQ(ierr);

  if (pcbddc->switch_static || pcbddc->dbg_flag) {
    ierr = PetscMalloc1(n_D,&aux_array1);CHKERRQ(ierr);
    for (i=0, j=0; i<n_R; i++) {
      if (PetscBTLookup(bitmask,idx_R_local[i])) {
        aux_array1[j++] = i;
      }
    }
    ierr = ISCreateGeneral(PETSC_COMM_SELF,j,aux_array1,PETSC_OWN_POINTER,&is_aux1);CHKERRQ(ierr);
    ierr = VecScatterCreate(pcbddc->vec1_R,is_aux1,pcis->vec1_D,(IS)0,&pcbddc->R_to_D);CHKERRQ(ierr);
    ierr = ISDestroy(&is_aux1);CHKERRQ(ierr);
  }
  ierr = PetscBTDestroy(&bitmask);CHKERRQ(ierr);
  ierr = ISRestoreIndices(pcbddc->is_R_local,(const PetscInt**)&idx_R_local);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "PCBDDCSetUpLocalSolvers"
PetscErrorCode PCBDDCSetUpLocalSolvers(PC pc, PetscBool dirichlet, PetscBool neumann)
{
  PC_BDDC        *pcbddc = (PC_BDDC*)pc->data;
  PC_IS          *pcis = (PC_IS*)pc->data;
  PC             pc_temp;
  Mat            A_RR;
  MatReuse       reuse;
  PetscScalar    m_one = -1.0;
  PetscReal      value;
  PetscInt       n_D,n_R,ibs,mbs;
  PetscBool      use_exact,use_exact_reduced,issbaij;
  PetscErrorCode ierr;
  /* prefixes stuff */
  char           dir_prefix[256],neu_prefix[256],str_level[16];
  size_t         len;

  PetscFunctionBegin;

  /* compute prefixes */
  ierr = PetscStrcpy(dir_prefix,"");CHKERRQ(ierr);
  ierr = PetscStrcpy(neu_prefix,"");CHKERRQ(ierr);
  if (!pcbddc->current_level) {
    ierr = PetscStrcpy(dir_prefix,((PetscObject)pc)->prefix);CHKERRQ(ierr);
    ierr = PetscStrcpy(neu_prefix,((PetscObject)pc)->prefix);CHKERRQ(ierr);
    ierr = PetscStrcat(dir_prefix,"pc_bddc_dirichlet_");CHKERRQ(ierr);
    ierr = PetscStrcat(neu_prefix,"pc_bddc_neumann_");CHKERRQ(ierr);
  } else {
    ierr = PetscStrcpy(str_level,"");CHKERRQ(ierr);
    sprintf(str_level,"l%d_",(int)(pcbddc->current_level));
    ierr = PetscStrlen(((PetscObject)pc)->prefix,&len);CHKERRQ(ierr);
    len -= 15; /* remove "pc_bddc_coarse_" */
    if (pcbddc->current_level>1) len -= 3; /* remove "lX_" with X level number */
    if (pcbddc->current_level>10) len -= 1; /* remove another char from level number */
    ierr = PetscStrncpy(dir_prefix,((PetscObject)pc)->prefix,len+1);CHKERRQ(ierr);
    ierr = PetscStrncpy(neu_prefix,((PetscObject)pc)->prefix,len+1);CHKERRQ(ierr);
    ierr = PetscStrcat(dir_prefix,"pc_bddc_dirichlet_");CHKERRQ(ierr);
    ierr = PetscStrcat(neu_prefix,"pc_bddc_neumann_");CHKERRQ(ierr);
    ierr = PetscStrcat(dir_prefix,str_level);CHKERRQ(ierr);
    ierr = PetscStrcat(neu_prefix,str_level);CHKERRQ(ierr);
  }

  /* DIRICHLET PROBLEM */
  if (dirichlet) {
    PCBDDCSubSchurs sub_schurs = pcbddc->sub_schurs;
    if (pcbddc->issym) {
      ierr = MatSetOption(pcis->A_II,MAT_SYMMETRIC,PETSC_TRUE);CHKERRQ(ierr);
    }
    /* Matrix for Dirichlet problem is pcis->A_II */
    n_D = pcis->n - pcis->n_B;
    if (!pcbddc->ksp_D) { /* create object if not yet build */
      ierr = KSPCreate(PETSC_COMM_SELF,&pcbddc->ksp_D);CHKERRQ(ierr);
      ierr = PetscObjectIncrementTabLevel((PetscObject)pcbddc->ksp_D,(PetscObject)pc,1);CHKERRQ(ierr);
      /* default */
      ierr = KSPSetType(pcbddc->ksp_D,KSPPREONLY);CHKERRQ(ierr);
      ierr = KSPSetOptionsPrefix(pcbddc->ksp_D,dir_prefix);CHKERRQ(ierr);
      ierr = PetscObjectTypeCompare((PetscObject)pcis->A_II,MATSEQSBAIJ,&issbaij);CHKERRQ(ierr);
      ierr = KSPGetPC(pcbddc->ksp_D,&pc_temp);CHKERRQ(ierr);
      if (issbaij) {
        ierr = PCSetType(pc_temp,PCCHOLESKY);CHKERRQ(ierr);
      } else {
        ierr = PCSetType(pc_temp,PCLU);CHKERRQ(ierr);
      }
      /* Allow user's customization */
      ierr = KSPSetFromOptions(pcbddc->ksp_D);CHKERRQ(ierr);
      ierr = PCFactorSetReuseFill(pc_temp,PETSC_TRUE);CHKERRQ(ierr);
    }
    ierr = KSPSetOperators(pcbddc->ksp_D,pcis->A_II,pcis->A_II);CHKERRQ(ierr);
    if (sub_schurs->interior_solver) {
      ierr = KSPSetPC(pcbddc->ksp_D,sub_schurs->interior_solver);CHKERRQ(ierr);
    }
    /* umfpack interface has a bug when matrix dimension is zero. TODO solve from umfpack interface */
    if (!n_D) {
      ierr = KSPGetPC(pcbddc->ksp_D,&pc_temp);CHKERRQ(ierr);
      ierr = PCSetType(pc_temp,PCNONE);CHKERRQ(ierr);
    }
    /* Set Up KSP for Dirichlet problem of BDDC */
    ierr = KSPSetUp(pcbddc->ksp_D);CHKERRQ(ierr);
    /* set ksp_D into pcis data */
    ierr = KSPDestroy(&pcis->ksp_D);CHKERRQ(ierr);
    ierr = PetscObjectReference((PetscObject)pcbddc->ksp_D);CHKERRQ(ierr);
    pcis->ksp_D = pcbddc->ksp_D;
  }

  /* NEUMANN PROBLEM */
  A_RR = 0;
  if (neumann) {
    /* Matrix for Neumann problem is A_RR -> we need to create/reuse it at this point */
    ierr = ISGetSize(pcbddc->is_R_local,&n_R);CHKERRQ(ierr);
    if (pcbddc->ksp_R) { /* already created ksp */
      PetscInt nn_R;
      ierr = KSPGetOperators(pcbddc->ksp_R,NULL,&A_RR);CHKERRQ(ierr);
      ierr = PetscObjectReference((PetscObject)A_RR);CHKERRQ(ierr);
      ierr = MatGetSize(A_RR,&nn_R,NULL);CHKERRQ(ierr);
      if (nn_R != n_R) { /* old ksp is not reusable, so reset it */
        ierr = KSPReset(pcbddc->ksp_R);CHKERRQ(ierr);
        ierr = MatDestroy(&A_RR);CHKERRQ(ierr);
        reuse = MAT_INITIAL_MATRIX;
      } else { /* same sizes, but nonzero pattern depend on primal vertices so it can be changed */
        if (pcbddc->new_primal_space_local) { /* we are not sure the matrix will have the same nonzero pattern */
          ierr = MatDestroy(&A_RR);CHKERRQ(ierr);
          reuse = MAT_INITIAL_MATRIX;
        } else { /* safe to reuse the matrix */
          reuse = MAT_REUSE_MATRIX;
        }
      }
      /* last check */
      if (pc->flag == DIFFERENT_NONZERO_PATTERN) {
        ierr = MatDestroy(&A_RR);CHKERRQ(ierr);
        reuse = MAT_INITIAL_MATRIX;
      }
    } else { /* first time, so we need to create the matrix */
      reuse = MAT_INITIAL_MATRIX;
    }
    /* extract A_RR */
    ierr = MatGetBlockSize(pcbddc->local_mat,&mbs);CHKERRQ(ierr);
    ierr = ISGetBlockSize(pcbddc->is_R_local,&ibs);CHKERRQ(ierr);
    if (ibs != mbs) {
      Mat newmat;
      ierr = MatConvert(pcbddc->local_mat,MATSEQAIJ,MAT_INITIAL_MATRIX,&newmat);CHKERRQ(ierr);
      ierr = MatGetSubMatrix(newmat,pcbddc->is_R_local,pcbddc->is_R_local,reuse,&A_RR);CHKERRQ(ierr);
      ierr = MatDestroy(&newmat);CHKERRQ(ierr);
    } else {
      ierr = MatGetSubMatrix(pcbddc->local_mat,pcbddc->is_R_local,pcbddc->is_R_local,reuse,&A_RR);CHKERRQ(ierr);
    }
    if (pcbddc->issym) {
      ierr = MatSetOption(A_RR,MAT_SYMMETRIC,PETSC_TRUE);CHKERRQ(ierr);
    }
    if (!pcbddc->ksp_R) { /* create object if not present */
      ierr = KSPCreate(PETSC_COMM_SELF,&pcbddc->ksp_R);CHKERRQ(ierr);
      ierr = PetscObjectIncrementTabLevel((PetscObject)pcbddc->ksp_R,(PetscObject)pc,1);CHKERRQ(ierr);
      /* default */
      ierr = KSPSetType(pcbddc->ksp_R,KSPPREONLY);CHKERRQ(ierr);
      ierr = KSPSetOptionsPrefix(pcbddc->ksp_R,neu_prefix);CHKERRQ(ierr);
      ierr = KSPGetPC(pcbddc->ksp_R,&pc_temp);CHKERRQ(ierr);
      ierr = PetscObjectTypeCompare((PetscObject)A_RR,MATSEQSBAIJ,&issbaij);CHKERRQ(ierr);
      if (issbaij) {
        ierr = PCSetType(pc_temp,PCCHOLESKY);CHKERRQ(ierr);
      } else {
        ierr = PCSetType(pc_temp,PCLU);CHKERRQ(ierr);
      }
      /* Allow user's customization */
      ierr = KSPSetFromOptions(pcbddc->ksp_R);CHKERRQ(ierr);
      ierr = PCFactorSetReuseFill(pc_temp,PETSC_TRUE);CHKERRQ(ierr);
    }
    ierr = KSPSetOperators(pcbddc->ksp_R,A_RR,A_RR);CHKERRQ(ierr);
    /* umfpack interface has a bug when matrix dimension is zero. TODO solve from umfpack interface */
    if (!n_R) {
      ierr = KSPGetPC(pcbddc->ksp_R,&pc_temp);CHKERRQ(ierr);
      ierr = PCSetType(pc_temp,PCNONE);CHKERRQ(ierr);
    }
    /* Set Up KSP for Neumann problem of BDDC */
    ierr = KSPSetUp(pcbddc->ksp_R);CHKERRQ(ierr);
  }

  /* check Dirichlet and Neumann solvers and adapt them if a nullspace correction is needed */
  if (pcbddc->NullSpace || pcbddc->dbg_flag) {
    if (pcbddc->dbg_flag) {
      ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      ierr = PetscViewerASCIISynchronizedAllow(pcbddc->dbg_viewer,PETSC_TRUE);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
    }
    if (dirichlet) { /* Dirichlet */
      ierr = VecSetRandom(pcis->vec1_D,NULL);CHKERRQ(ierr);
      ierr = MatMult(pcis->A_II,pcis->vec1_D,pcis->vec2_D);CHKERRQ(ierr);
      ierr = KSPSolve(pcbddc->ksp_D,pcis->vec2_D,pcis->vec2_D);CHKERRQ(ierr);
      ierr = VecAXPY(pcis->vec1_D,m_one,pcis->vec2_D);CHKERRQ(ierr);
      ierr = VecNorm(pcis->vec1_D,NORM_INFINITY,&value);CHKERRQ(ierr);
      /* need to be adapted? */
      use_exact = (PetscAbsReal(value) > 1.e-4 ? PETSC_FALSE : PETSC_TRUE);
      ierr = MPI_Allreduce(&use_exact,&use_exact_reduced,1,MPIU_BOOL,MPI_LAND,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);
      ierr = PCBDDCSetUseExactDirichlet(pc,use_exact_reduced);CHKERRQ(ierr);
      /* print info */
      if (pcbddc->dbg_flag) {
        ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d infinity error for Dirichlet solve (%s) = % 1.14e \n",PetscGlobalRank,((PetscObject)(pcbddc->ksp_D))->prefix,value);CHKERRQ(ierr);
        ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      }
      if (pcbddc->NullSpace && !use_exact_reduced && !pcbddc->switch_static) {
        ierr = PCBDDCNullSpaceAssembleCorrection(pc,pcis->is_I_local);CHKERRQ(ierr);
      }
    }
    if (neumann) { /* Neumann */
      ierr = VecSetRandom(pcbddc->vec1_R,NULL);CHKERRQ(ierr);
      ierr = MatMult(A_RR,pcbddc->vec1_R,pcbddc->vec2_R);CHKERRQ(ierr);
      ierr = KSPSolve(pcbddc->ksp_R,pcbddc->vec2_R,pcbddc->vec2_R);CHKERRQ(ierr);
      ierr = VecAXPY(pcbddc->vec1_R,m_one,pcbddc->vec2_R);CHKERRQ(ierr);
      ierr = VecNorm(pcbddc->vec1_R,NORM_INFINITY,&value);CHKERRQ(ierr);
      /* need to be adapted? */
      use_exact = (PetscAbsReal(value) > 1.e-4 ? PETSC_FALSE : PETSC_TRUE);
      ierr = MPI_Allreduce(&use_exact,&use_exact_reduced,1,MPIU_BOOL,MPI_LAND,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);
      /* print info */
      if (pcbddc->dbg_flag) {
        ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d infinity error for Neumann solve (%s) = % 1.14e\n",PetscGlobalRank,((PetscObject)(pcbddc->ksp_R))->prefix,value);CHKERRQ(ierr);
        ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      }
      if (pcbddc->NullSpace && !use_exact_reduced) { /* is it the right logic? */
        ierr = PCBDDCNullSpaceAssembleCorrection(pc,pcbddc->is_R_local);CHKERRQ(ierr);
      }
    }
  }
  /* free Neumann problem's matrix */
  ierr = MatDestroy(&A_RR);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSolveSubstructureCorrection"
static PetscErrorCode  PCBDDCSolveSubstructureCorrection(PC pc, Vec rhs, Vec sol, Vec work, PetscBool applytranspose)
{
  PetscErrorCode ierr;
  PC_BDDC*       pcbddc = (PC_BDDC*)(pc->data);

  PetscFunctionBegin;
  if (applytranspose) {
    if (pcbddc->local_auxmat1) {
      ierr = MatMultTranspose(pcbddc->local_auxmat2,rhs,work);CHKERRQ(ierr);
      ierr = MatMultTransposeAdd(pcbddc->local_auxmat1,work,rhs,rhs);CHKERRQ(ierr);
    }
    ierr = KSPSolveTranspose(pcbddc->ksp_R,rhs,sol);CHKERRQ(ierr);
  } else {
    ierr = KSPSolve(pcbddc->ksp_R,rhs,sol);CHKERRQ(ierr);
    if (pcbddc->local_auxmat1) {
      ierr = MatMult(pcbddc->local_auxmat1,sol,work);CHKERRQ(ierr);
      ierr = MatMultAdd(pcbddc->local_auxmat2,work,sol,sol);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

/* parameter apply transpose determines if the interface preconditioner should be applied transposed or not */
#undef __FUNCT__
#define __FUNCT__ "PCBDDCApplyInterfacePreconditioner"
PetscErrorCode  PCBDDCApplyInterfacePreconditioner(PC pc, PetscBool applytranspose)
{
  PetscErrorCode ierr;
  PC_BDDC*        pcbddc = (PC_BDDC*)(pc->data);
  PC_IS*            pcis = (PC_IS*)  (pc->data);
  const PetscScalar zero = 0.0;

  PetscFunctionBegin;
  /* Application of PSI^T or PHI^T (depending on applytranspose, see comment above) */
  if (applytranspose) {
    ierr = MatMultTranspose(pcbddc->coarse_phi_B,pcis->vec1_B,pcbddc->vec1_P);CHKERRQ(ierr);
    if (pcbddc->switch_static) { ierr = MatMultTransposeAdd(pcbddc->coarse_phi_D,pcis->vec1_D,pcbddc->vec1_P,pcbddc->vec1_P);CHKERRQ(ierr); }
  } else {
    ierr = MatMultTranspose(pcbddc->coarse_psi_B,pcis->vec1_B,pcbddc->vec1_P);CHKERRQ(ierr);
    if (pcbddc->switch_static) { ierr = MatMultTransposeAdd(pcbddc->coarse_psi_D,pcis->vec1_D,pcbddc->vec1_P,pcbddc->vec1_P);CHKERRQ(ierr); }
  }
  /* start communications from local primal nodes to rhs of coarse solver */
  ierr = VecSet(pcbddc->coarse_vec,zero);CHKERRQ(ierr);
  ierr = PCBDDCScatterCoarseDataBegin(pc,ADD_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = PCBDDCScatterCoarseDataEnd(pc,ADD_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  /* Coarse solution -> rhs and sol updated inside PCBDDCScattarCoarseDataBegin/End */
  /* TODO remove null space when doing multilevel */
  if (pcbddc->coarse_ksp) {
    Vec rhs,sol;

    ierr = KSPGetRhs(pcbddc->coarse_ksp,&rhs);CHKERRQ(ierr);
    ierr = KSPGetSolution(pcbddc->coarse_ksp,&sol);CHKERRQ(ierr);
    if (applytranspose) {
      ierr = KSPSolveTranspose(pcbddc->coarse_ksp,rhs,sol);CHKERRQ(ierr);
    } else {
      ierr = KSPSolve(pcbddc->coarse_ksp,rhs,sol);CHKERRQ(ierr);
    }
  }

  /* Local solution on R nodes */
  if (pcis->n) {
    ierr = VecSet(pcbddc->vec1_R,zero);CHKERRQ(ierr);
    ierr = VecScatterBegin(pcbddc->R_to_B,pcis->vec1_B,pcbddc->vec1_R,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecScatterEnd(pcbddc->R_to_B,pcis->vec1_B,pcbddc->vec1_R,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    if (pcbddc->switch_static) {
      ierr = VecScatterBegin(pcbddc->R_to_D,pcis->vec1_D,pcbddc->vec1_R,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
      ierr = VecScatterEnd(pcbddc->R_to_D,pcis->vec1_D,pcbddc->vec1_R,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    }
    ierr = PCBDDCSolveSubstructureCorrection(pc,pcbddc->vec1_R,pcbddc->vec2_R,pcbddc->vec1_C,applytranspose);CHKERRQ(ierr);
    ierr = VecSet(pcis->vec1_B,zero);CHKERRQ(ierr);
    ierr = VecScatterBegin(pcbddc->R_to_B,pcbddc->vec2_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecScatterEnd(pcbddc->R_to_B,pcbddc->vec2_R,pcis->vec1_B,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    if (pcbddc->switch_static) {
      ierr = VecScatterBegin(pcbddc->R_to_D,pcbddc->vec2_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterEnd(pcbddc->R_to_D,pcbddc->vec2_R,pcis->vec1_D,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    }
  }

  /* communications from coarse sol to local primal nodes */
  ierr = PCBDDCScatterCoarseDataBegin(pc,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  ierr = PCBDDCScatterCoarseDataEnd(pc,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);

  /* Sum contributions from two levels */
  if (applytranspose) {
    ierr = MatMultAdd(pcbddc->coarse_psi_B,pcbddc->vec1_P,pcis->vec1_B,pcis->vec1_B);CHKERRQ(ierr);
    if (pcbddc->switch_static) { ierr = MatMultAdd(pcbddc->coarse_psi_D,pcbddc->vec1_P,pcis->vec1_D,pcis->vec1_D);CHKERRQ(ierr); }
  } else {
    ierr = MatMultAdd(pcbddc->coarse_phi_B,pcbddc->vec1_P,pcis->vec1_B,pcis->vec1_B);CHKERRQ(ierr);
    if (pcbddc->switch_static) { ierr = MatMultAdd(pcbddc->coarse_phi_D,pcbddc->vec1_P,pcis->vec1_D,pcis->vec1_D);CHKERRQ(ierr); }
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCScatterCoarseDataBegin"
PetscErrorCode PCBDDCScatterCoarseDataBegin(PC pc,InsertMode imode, ScatterMode smode)
{
  PetscErrorCode ierr;
  PC_BDDC*       pcbddc = (PC_BDDC*)(pc->data);
  PetscScalar    *array;
  Vec            from,to;

  PetscFunctionBegin;
  if (smode == SCATTER_REVERSE) { /* from global to local -> get data from coarse solution */
    from = pcbddc->coarse_vec;
    to = pcbddc->vec1_P;
    if (pcbddc->coarse_ksp) { /* get array from coarse processes */
      Vec tvec;

      ierr = KSPGetRhs(pcbddc->coarse_ksp,&tvec);CHKERRQ(ierr);
      ierr = VecResetArray(tvec);CHKERRQ(ierr);
      ierr = KSPGetSolution(pcbddc->coarse_ksp,&tvec);CHKERRQ(ierr);
      ierr = VecGetArray(tvec,&array);CHKERRQ(ierr);
      ierr = VecPlaceArray(from,array);CHKERRQ(ierr);
      ierr = VecRestoreArray(tvec,&array);CHKERRQ(ierr);
    }
  } else { /* from local to global -> put data in coarse right hand side */
    from = pcbddc->vec1_P;
    to = pcbddc->coarse_vec;
  }
  ierr = VecScatterBegin(pcbddc->coarse_loc_to_glob,from,to,imode,smode);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCScatterCoarseDataEnd"
PetscErrorCode PCBDDCScatterCoarseDataEnd(PC pc, InsertMode imode, ScatterMode smode)
{
  PetscErrorCode ierr;
  PC_BDDC*       pcbddc = (PC_BDDC*)(pc->data);
  PetscScalar    *array;
  Vec            from,to;

  PetscFunctionBegin;
  if (smode == SCATTER_REVERSE) { /* from global to local -> get data from coarse solution */
    from = pcbddc->coarse_vec;
    to = pcbddc->vec1_P;
  } else { /* from local to global -> put data in coarse right hand side */
    from = pcbddc->vec1_P;
    to = pcbddc->coarse_vec;
  }
  ierr = VecScatterEnd(pcbddc->coarse_loc_to_glob,from,to,imode,smode);CHKERRQ(ierr);
  if (smode == SCATTER_FORWARD) {
    if (pcbddc->coarse_ksp) { /* get array from coarse processes */
      Vec tvec;

      ierr = KSPGetRhs(pcbddc->coarse_ksp,&tvec);CHKERRQ(ierr);
      ierr = VecGetArray(to,&array);CHKERRQ(ierr);
      ierr = VecPlaceArray(tvec,array);CHKERRQ(ierr);
      ierr = VecRestoreArray(to,&array);CHKERRQ(ierr);
    }
  } else {
    if (pcbddc->coarse_ksp) { /* restore array of pcbddc->coarse_vec */
     ierr = VecResetArray(from);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

/* uncomment for testing purposes */
/* #define PETSC_MISSING_LAPACK_GESVD 1 */
#undef __FUNCT__
#define __FUNCT__ "PCBDDCConstraintsSetUp"
PetscErrorCode PCBDDCConstraintsSetUp(PC pc)
{
  PetscErrorCode    ierr;
  PC_IS*            pcis = (PC_IS*)(pc->data);
  PC_BDDC*          pcbddc = (PC_BDDC*)pc->data;
  Mat_IS*           matis = (Mat_IS*)pc->pmat->data;
  /* one and zero */
  PetscScalar       one=1.0,zero=0.0;
  /* space to store constraints and their local indices */
  PetscScalar       *temp_quadrature_constraint;
  PetscInt          *temp_indices,*temp_indices_to_constraint,*temp_indices_to_constraint_B;
  /* iterators */
  PetscInt          i,j,k,total_counts,temp_start_ptr;
  /* BLAS integers */
  PetscBLASInt      lwork,lierr;
  PetscBLASInt      Blas_N,Blas_M,Blas_K,Blas_one=1;
  PetscBLASInt      Blas_LDA,Blas_LDB,Blas_LDC;
  /* reuse */
  PetscInt          olocal_primal_size;
  PetscInt          *oprimal_indices_local_idxs;
  /* change of basis */
  PetscInt          *aux_primal_numbering,*aux_primal_minloc,*global_indices;
  PetscBool         boolforchange,qr_needed;
  PetscBT           touched,change_basis,qr_needed_idx;
  /* auxiliary stuff */
  PetscInt          *nnz,*is_indices,*aux_primal_numbering_B;
  PetscInt          ncc,*gidxs=NULL,*permutation=NULL,*temp_indices_to_constraint_work=NULL;
  PetscScalar       *temp_quadrature_constraint_work=NULL;
  /* some quantities */
  PetscInt          n_vertices,total_primal_vertices,valid_constraints;
  PetscInt          size_of_constraint,max_size_of_constraint=0,max_constraints,temp_constraints;

  PetscFunctionBegin;
  /* Destroy Mat objects computed previously */
  ierr = MatDestroy(&pcbddc->ChangeOfBasisMatrix);CHKERRQ(ierr);
  ierr = MatDestroy(&pcbddc->ConstraintMatrix);CHKERRQ(ierr);

  /* print some info */
  if (pcbddc->dbg_flag) {
    IS       vertices;
    PetscInt nv,nedges,nfaces;
    ierr = PCBDDCGraphGetCandidatesIS(pcbddc->mat_graph,&nfaces,NULL,&nedges,NULL,&vertices);CHKERRQ(ierr);
    ierr = ISGetSize(vertices,&nv);CHKERRQ(ierr);
    ierr = ISDestroy(&vertices);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedAllow(pcbddc->dbg_viewer,PETSC_TRUE);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"--------------------------------------------------------------\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d got %02d local candidate vertices (%d)\n",PetscGlobalRank,nv,pcbddc->use_vertices);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d got %02d local candidate edges    (%d)\n",PetscGlobalRank,nedges,pcbddc->use_edges);CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d got %02d local candidate faces    (%d)\n",PetscGlobalRank,nfaces,pcbddc->use_faces);CHKERRQ(ierr);
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
  }

  if (!pcbddc->adaptive_selection) {
    IS           ISForVertices,*ISForFaces,*ISForEdges,*used_IS;
    MatNullSpace nearnullsp;
    const Vec    *nearnullvecs;
    Vec          *localnearnullsp;
    PetscScalar  *array;
    PetscInt     n_ISForFaces,n_ISForEdges,nnsp_size;
    PetscBool    nnsp_has_cnst;
    /* LAPACK working arrays for SVD or POD */
    PetscBool    skip_lapack;
    PetscScalar  *work;
    PetscReal    *singular_vals;
#if defined(PETSC_USE_COMPLEX)
    PetscReal    *rwork;
#endif
#if defined(PETSC_MISSING_LAPACK_GESVD)
    PetscScalar  *temp_basis,*correlation_mat;
#else
    PetscBLASInt dummy_int=1;
    PetscScalar  dummy_scalar=1.;
#endif

    /* Get index sets for faces, edges and vertices from graph */
    ierr = PCBDDCGraphGetCandidatesIS(pcbddc->mat_graph,&n_ISForFaces,&ISForFaces,&n_ISForEdges,&ISForEdges,&ISForVertices);CHKERRQ(ierr);
    /* free unneeded index sets */
    if (!pcbddc->use_vertices) {
      ierr = ISDestroy(&ISForVertices);CHKERRQ(ierr);
    }
    if (!pcbddc->use_edges) {
      for (i=0;i<n_ISForEdges;i++) {
        ierr = ISDestroy(&ISForEdges[i]);CHKERRQ(ierr);
      }
      ierr = PetscFree(ISForEdges);CHKERRQ(ierr);
      n_ISForEdges = 0;
    }
    if (!pcbddc->use_faces) {
      for (i=0;i<n_ISForFaces;i++) {
        ierr = ISDestroy(&ISForFaces[i]);CHKERRQ(ierr);
      }
      ierr = PetscFree(ISForFaces);CHKERRQ(ierr);
      n_ISForFaces = 0;
    }
    /* HACKS (the following two blocks of code) */
    if (!ISForVertices && pcbddc->NullSpace && !pcbddc->user_ChangeOfBasisMatrix) {
      pcbddc->use_change_of_basis = PETSC_TRUE;
      if (!ISForEdges) {
        pcbddc->use_change_on_faces = PETSC_TRUE;
      }
    }
    if (pcbddc->NullSpace) {
      /* use_change_of_basis should be consistent among processors */
      PetscBool tbool[2],gbool[2];
      tbool[0] = pcbddc->use_change_of_basis;
      tbool[1] = pcbddc->use_change_on_faces;
      ierr = MPI_Allreduce(tbool,gbool,2,MPIU_BOOL,MPI_LOR,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);
      pcbddc->use_change_of_basis = gbool[0];
      pcbddc->use_change_on_faces = gbool[1];
    }

    /* check if near null space is attached to global mat */
    ierr = MatGetNearNullSpace(pc->pmat,&nearnullsp);CHKERRQ(ierr);
    if (nearnullsp) {
      ierr = MatNullSpaceGetVecs(nearnullsp,&nnsp_has_cnst,&nnsp_size,&nearnullvecs);CHKERRQ(ierr);
      /* remove any stored info */
      ierr = MatNullSpaceDestroy(&pcbddc->onearnullspace);CHKERRQ(ierr);
      ierr = PetscFree(pcbddc->onearnullvecs_state);CHKERRQ(ierr);
      /* store information for BDDC solver reuse */
      ierr = PetscObjectReference((PetscObject)nearnullsp);CHKERRQ(ierr);
      pcbddc->onearnullspace = nearnullsp;
      ierr = PetscMalloc1(nnsp_size,&pcbddc->onearnullvecs_state);CHKERRQ(ierr);
      for (i=0;i<nnsp_size;i++) {
        ierr = PetscObjectStateGet((PetscObject)nearnullvecs[i],&pcbddc->onearnullvecs_state[i]);CHKERRQ(ierr);
      }
    } else { /* if near null space is not provided BDDC uses constants by default */
      nnsp_size = 0;
      nnsp_has_cnst = PETSC_TRUE;
    }
    /* get max number of constraints on a single cc */
    max_constraints = nnsp_size;
    if (nnsp_has_cnst) max_constraints++;

    /*
         Evaluate maximum storage size needed by the procedure
         - temp_indices will contain start index of each constraint stored as follows
         - temp_indices_to_constraint  [temp_indices[i],...,temp_indices[i+1]-1] will contain the indices (in local numbering) on which the constraint acts
         - temp_indices_to_constraint_B[temp_indices[i],...,temp_indices[i+1]-1] will contain the indices (in boundary numbering) on which the constraint acts
         - temp_quadrature_constraint  [temp_indices[i],...,temp_indices[i+1]-1] will contain the scalars representing the constraint itself
                                                                                                                                                           */
    total_counts = n_ISForFaces+n_ISForEdges;
    total_counts *= max_constraints;
    n_vertices = 0;
    if (ISForVertices) {
      ierr = ISGetSize(ISForVertices,&n_vertices);CHKERRQ(ierr);
    }
    total_counts += n_vertices;
    ierr = PetscMalloc1(total_counts+1,&temp_indices);CHKERRQ(ierr);
    ierr = PetscBTCreate(total_counts,&change_basis);CHKERRQ(ierr);
    total_counts = 0;
    max_size_of_constraint = 0;
    for (i=0;i<n_ISForEdges+n_ISForFaces;i++) {
      if (i<n_ISForEdges) {
        used_IS = &ISForEdges[i];
      } else {
        used_IS = &ISForFaces[i-n_ISForEdges];
      }
      ierr = ISGetSize(*used_IS,&j);CHKERRQ(ierr);
      total_counts += j;
      max_size_of_constraint = PetscMax(j,max_size_of_constraint);
    }
    total_counts *= max_constraints;
    total_counts += n_vertices;
    ierr = PetscMalloc3(total_counts,&temp_quadrature_constraint,total_counts,&temp_indices_to_constraint,total_counts,&temp_indices_to_constraint_B);CHKERRQ(ierr);
    /* get local part of global near null space vectors */
    ierr = PetscMalloc1(nnsp_size,&localnearnullsp);CHKERRQ(ierr);
    for (k=0;k<nnsp_size;k++) {
      ierr = VecDuplicate(pcis->vec1_N,&localnearnullsp[k]);CHKERRQ(ierr);
      ierr = VecScatterBegin(matis->ctx,nearnullvecs[k],localnearnullsp[k],INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterEnd(matis->ctx,nearnullvecs[k],localnearnullsp[k],INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    }

    /* whether or not to skip lapack calls */
    skip_lapack = PETSC_TRUE;
    if (n_ISForFaces+n_ISForEdges && max_constraints > 1 && !pcbddc->use_nnsp_true) skip_lapack = PETSC_FALSE;

    /* allocate some auxiliary stuff */
    if (!skip_lapack || pcbddc->use_qr_single) {
      ierr = PetscMalloc4(max_size_of_constraint,&gidxs,max_size_of_constraint,&permutation,max_size_of_constraint,&temp_indices_to_constraint_work,max_size_of_constraint,&temp_quadrature_constraint_work);CHKERRQ(ierr);
    } else {
      gidxs = NULL;
      permutation = NULL;
      temp_indices_to_constraint_work = NULL;
      temp_quadrature_constraint_work = NULL;
    }

    /* First we issue queries to allocate optimal workspace for LAPACKgesvd (or LAPACKsyev if SVD is missing) */
    if (!skip_lapack) {
      PetscScalar temp_work;

#if defined(PETSC_MISSING_LAPACK_GESVD)
      /* Proper Orthogonal Decomposition (POD) using the snapshot method */
      ierr = PetscMalloc1(max_constraints*max_constraints,&correlation_mat);CHKERRQ(ierr);
      ierr = PetscMalloc1(max_constraints,&singular_vals);CHKERRQ(ierr);
      ierr = PetscMalloc1(max_size_of_constraint*max_constraints,&temp_basis);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
      ierr = PetscMalloc1(3*max_constraints,&rwork);CHKERRQ(ierr);
#endif
      /* now we evaluate the optimal workspace using query with lwork=-1 */
      ierr = PetscBLASIntCast(max_constraints,&Blas_N);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(max_constraints,&Blas_LDA);CHKERRQ(ierr);
      lwork = -1;
      ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
#if !defined(PETSC_USE_COMPLEX)
      PetscStackCallBLAS("LAPACKsyev",LAPACKsyev_("V","U",&Blas_N,correlation_mat,&Blas_LDA,singular_vals,&temp_work,&lwork,&lierr));
#else
      PetscStackCallBLAS("LAPACKsyev",LAPACKsyev_("V","U",&Blas_N,correlation_mat,&Blas_LDA,singular_vals,&temp_work,&lwork,rwork,&lierr));
#endif
      ierr = PetscFPTrapPop();CHKERRQ(ierr);
      if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in query to SYEV Lapack routine %d",(int)lierr);
#else /* on missing GESVD */
      /* SVD */
      PetscInt max_n,min_n;
      max_n = max_size_of_constraint;
      min_n = max_constraints;
      if (max_size_of_constraint < max_constraints) {
        min_n = max_size_of_constraint;
        max_n = max_constraints;
      }
      ierr = PetscMalloc1(min_n,&singular_vals);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
      ierr = PetscMalloc1(5*min_n,&rwork);CHKERRQ(ierr);
#endif
      /* now we evaluate the optimal workspace using query with lwork=-1 */
      lwork = -1;
      ierr = PetscBLASIntCast(max_n,&Blas_M);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(min_n,&Blas_N);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(max_n,&Blas_LDA);CHKERRQ(ierr);
      ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
#if !defined(PETSC_USE_COMPLEX)
      PetscStackCallBLAS("LAPACKgesvd",LAPACKgesvd_("O","N",&Blas_M,&Blas_N,&temp_quadrature_constraint[0],&Blas_LDA,singular_vals,&dummy_scalar,&dummy_int,&dummy_scalar,&dummy_int,&temp_work,&lwork,&lierr));
#else
      PetscStackCallBLAS("LAPACKgesvd",LAPACKgesvd_("O","N",&Blas_M,&Blas_N,&temp_quadrature_constraint[0],&Blas_LDA,singular_vals,&dummy_scalar,&dummy_int,&dummy_scalar,&dummy_int,&temp_work,&lwork,rwork,&lierr));
#endif
      ierr = PetscFPTrapPop();CHKERRQ(ierr);
      if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in query to GESVD Lapack routine %d",(int)lierr);
#endif /* on missing GESVD */
      /* Allocate optimal workspace */
      ierr = PetscBLASIntCast((PetscInt)PetscRealPart(temp_work),&lwork);CHKERRQ(ierr);
      ierr = PetscMalloc1(lwork,&work);CHKERRQ(ierr);
    }
    /* Now we can loop on constraining sets */
    total_counts = 0;
    temp_indices[0] = 0;
    /* vertices */
    if (ISForVertices) {
      ierr = ISGetIndices(ISForVertices,(const PetscInt**)&is_indices);CHKERRQ(ierr);
      if (nnsp_has_cnst) { /* consider all vertices */
        ierr = PetscMemcpy(&temp_indices_to_constraint[temp_indices[total_counts]],is_indices,n_vertices*sizeof(PetscInt));CHKERRQ(ierr);
        for (i=0;i<n_vertices;i++) {
          temp_quadrature_constraint[temp_indices[total_counts]]=1.0;
          temp_indices[total_counts+1]=temp_indices[total_counts]+1;
          total_counts++;
        }
      } else { /* consider vertices for which exist at least a localnearnullsp which is not null there */
        PetscBool used_vertex;
        for (i=0;i<n_vertices;i++) {
          used_vertex = PETSC_FALSE;
          k = 0;
          while (!used_vertex && k<nnsp_size) {
            ierr = VecGetArrayRead(localnearnullsp[k],(const PetscScalar**)&array);CHKERRQ(ierr);
            if (PetscAbsScalar(array[is_indices[i]])>0.0) {
              temp_indices_to_constraint[temp_indices[total_counts]]=is_indices[i];
              temp_quadrature_constraint[temp_indices[total_counts]]=1.0;
              temp_indices[total_counts+1]=temp_indices[total_counts]+1;
              total_counts++;
              used_vertex = PETSC_TRUE;
            }
            ierr = VecRestoreArrayRead(localnearnullsp[k],(const PetscScalar**)&array);CHKERRQ(ierr);
            k++;
          }
        }
      }
      ierr = ISRestoreIndices(ISForVertices,(const PetscInt**)&is_indices);CHKERRQ(ierr);
      n_vertices = total_counts;
    }

    /* edges and faces */
    for (ncc=0;ncc<n_ISForEdges+n_ISForFaces;ncc++) {
      if (ncc<n_ISForEdges) {
        used_IS = &ISForEdges[ncc];
        boolforchange = pcbddc->use_change_of_basis; /* change or not the basis on the edge */
      } else {
        used_IS = &ISForFaces[ncc-n_ISForEdges];
        boolforchange = (PetscBool)(pcbddc->use_change_of_basis && pcbddc->use_change_on_faces); /* change or not the basis on the face */
      }
      temp_constraints = 0;          /* zero the number of constraints I have on this conn comp */
      temp_start_ptr = total_counts; /* need to know the starting index of constraints stored */
      ierr = ISGetSize(*used_IS,&size_of_constraint);CHKERRQ(ierr);
      ierr = ISGetIndices(*used_IS,(const PetscInt**)&is_indices);CHKERRQ(ierr);
      /* change of basis should not be performed on local periodic nodes */
      if (pcbddc->mat_graph->mirrors && pcbddc->mat_graph->mirrors[is_indices[0]]) boolforchange = PETSC_FALSE;
      if (nnsp_has_cnst) {
        PetscScalar quad_value;
        temp_constraints++;
        if (!pcbddc->use_nnsp_true) {
          quad_value = (PetscScalar)(1.0/PetscSqrtReal((PetscReal)size_of_constraint));
        } else {
          quad_value = 1.0;
        }
        ierr = PetscMemcpy(&temp_indices_to_constraint[temp_indices[total_counts]],is_indices,size_of_constraint*sizeof(PetscInt));CHKERRQ(ierr);
        for (j=0;j<size_of_constraint;j++) {
          temp_quadrature_constraint[temp_indices[total_counts]+j]=quad_value;
        }
        /* sort by global ordering if using lapack subroutines (not needed!) */
        if (!skip_lapack || pcbddc->use_qr_single) {
          ierr = ISLocalToGlobalMappingApply(matis->mapping,size_of_constraint,temp_indices_to_constraint+temp_indices[total_counts],gidxs);CHKERRQ(ierr);
          for (j=0;j<size_of_constraint;j++) {
            permutation[j]=j;
          }
          ierr = PetscSortIntWithPermutation(size_of_constraint,gidxs,permutation);CHKERRQ(ierr);
          for (j=0;j<size_of_constraint;j++) {
            if (permutation[j]!=j) SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_SUP,"This should not happen");
          }
          for (j=0;j<size_of_constraint;j++) {
            temp_indices_to_constraint_work[j] = temp_indices_to_constraint[temp_indices[total_counts]+permutation[j]];
            temp_quadrature_constraint_work[j] = temp_quadrature_constraint[temp_indices[total_counts]+permutation[j]];
          }
          ierr = PetscMemcpy(temp_indices_to_constraint+temp_indices[total_counts],temp_indices_to_constraint_work,size_of_constraint*sizeof(PetscInt));CHKERRQ(ierr);
          ierr = PetscMemcpy(temp_quadrature_constraint+temp_indices[total_counts],temp_quadrature_constraint_work,size_of_constraint*sizeof(PetscScalar));CHKERRQ(ierr);
        }
        temp_indices[total_counts+1]=temp_indices[total_counts]+size_of_constraint;  /* store new starting point */
        total_counts++;
      }
      for (k=0;k<nnsp_size;k++) {
        PetscReal real_value;
        ierr = VecGetArrayRead(localnearnullsp[k],(const PetscScalar**)&array);CHKERRQ(ierr);
        ierr = PetscMemcpy(&temp_indices_to_constraint[temp_indices[total_counts]],is_indices,size_of_constraint*sizeof(PetscInt));CHKERRQ(ierr);
        for (j=0;j<size_of_constraint;j++) {
          temp_quadrature_constraint[temp_indices[total_counts]+j]=array[is_indices[j]];
        }
        ierr = VecRestoreArrayRead(localnearnullsp[k],(const PetscScalar**)&array);CHKERRQ(ierr);
        /* check if array is null on the connected component */
        ierr = PetscBLASIntCast(size_of_constraint,&Blas_N);CHKERRQ(ierr);
        PetscStackCallBLAS("BLASasum",real_value = BLASasum_(&Blas_N,&temp_quadrature_constraint[temp_indices[total_counts]],&Blas_one));
        if (real_value > 0.0) { /* keep indices and values */
          /* sort by global ordering if using lapack subroutines */
          if (!skip_lapack || pcbddc->use_qr_single) {
            ierr = ISLocalToGlobalMappingApply(matis->mapping,size_of_constraint,temp_indices_to_constraint+temp_indices[total_counts],gidxs);CHKERRQ(ierr);
            for (j=0;j<size_of_constraint;j++) {
              permutation[j]=j;
            }
            ierr = PetscSortIntWithPermutation(size_of_constraint,gidxs,permutation);CHKERRQ(ierr);
            for (j=0;j<size_of_constraint;j++) {
              temp_indices_to_constraint_work[j] = temp_indices_to_constraint[temp_indices[total_counts]+permutation[j]];
              temp_quadrature_constraint_work[j] = temp_quadrature_constraint[temp_indices[total_counts]+permutation[j]];
            }
            ierr = PetscMemcpy(temp_indices_to_constraint+temp_indices[total_counts],temp_indices_to_constraint_work,size_of_constraint*sizeof(PetscInt));CHKERRQ(ierr);
            ierr = PetscMemcpy(temp_quadrature_constraint+temp_indices[total_counts],temp_quadrature_constraint_work,size_of_constraint*sizeof(PetscScalar));CHKERRQ(ierr);
          }
          temp_constraints++;
          temp_indices[total_counts+1]=temp_indices[total_counts]+size_of_constraint;  /* store new starting point */
          total_counts++;
        }
      }
      ierr = ISRestoreIndices(*used_IS,(const PetscInt**)&is_indices);CHKERRQ(ierr);
      valid_constraints = temp_constraints;
      if (!pcbddc->use_nnsp_true && temp_constraints) {
        if (temp_constraints == 1) { /* just normalize the constraint */
          PetscScalar norm;
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_N);CHKERRQ(ierr);
          PetscStackCallBLAS("BLASdot",norm = BLASdot_(&Blas_N,temp_quadrature_constraint+temp_indices[temp_start_ptr],&Blas_one,temp_quadrature_constraint+temp_indices[temp_start_ptr],&Blas_one));
          norm = 1.0/PetscSqrtReal(PetscRealPart(norm));
          PetscStackCallBLAS("BLASscal",BLASscal_(&Blas_N,&norm,temp_quadrature_constraint+temp_indices[temp_start_ptr],&Blas_one));
        } else { /* perform SVD */
          PetscReal tol = 1.0e-8; /* tolerance for retaining eigenmodes */

#if defined(PETSC_MISSING_LAPACK_GESVD)
          /* SVD: Y = U*S*V^H                -> U (eigenvectors of Y*Y^H) = Y*V*(S)^\dag
             POD: Y^H*Y = V*D*V^H, D = S^H*S -> U = Y*V*D^(-1/2)
             -> When PETSC_USE_COMPLEX and PETSC_MISSING_LAPACK_GESVD are defined
                the constraints basis will differ (by a complex factor with absolute value equal to 1)
                from that computed using LAPACKgesvd
             -> This is due to a different computation of eigenvectors in LAPACKheev
             -> The quality of the POD-computed basis will be the same */
          ierr = PetscMemzero(correlation_mat,temp_constraints*temp_constraints*sizeof(PetscScalar));CHKERRQ(ierr);
          /* Store upper triangular part of correlation matrix */
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_N);CHKERRQ(ierr);
          ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
          for (j=0;j<temp_constraints;j++) {
            for (k=0;k<j+1;k++) {
              PetscStackCallBLAS("BLASdot",correlation_mat[j*temp_constraints+k]=BLASdot_(&Blas_N,&temp_quadrature_constraint[temp_indices[temp_start_ptr+k]],&Blas_one,&temp_quadrature_constraint[temp_indices[temp_start_ptr+j]],&Blas_one));
            }
          }
          /* compute eigenvalues and eigenvectors of correlation matrix */
          ierr = PetscBLASIntCast(temp_constraints,&Blas_N);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(temp_constraints,&Blas_LDA);CHKERRQ(ierr);
#if !defined(PETSC_USE_COMPLEX)
          PetscStackCallBLAS("LAPACKsyev",LAPACKsyev_("V","U",&Blas_N,correlation_mat,&Blas_LDA,singular_vals,work,&lwork,&lierr));
#else
          PetscStackCallBLAS("LAPACKsyev",LAPACKsyev_("V","U",&Blas_N,correlation_mat,&Blas_LDA,singular_vals,work,&lwork,rwork,&lierr));
#endif
          ierr = PetscFPTrapPop();CHKERRQ(ierr);
          if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in SYEV Lapack routine %d",(int)lierr);
          /* retain eigenvalues greater than tol: note that LAPACKsyev gives eigs in ascending order */
          j = 0;
          while (j < temp_constraints && singular_vals[j] < tol) j++;
          total_counts = total_counts-j;
          valid_constraints = temp_constraints-j;
          /* scale and copy POD basis into used quadrature memory */
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_M);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(temp_constraints,&Blas_N);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(temp_constraints,&Blas_K);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(temp_constraints,&Blas_LDB);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDC);CHKERRQ(ierr);
          if (j<temp_constraints) {
            PetscInt ii;
            for (k=j;k<temp_constraints;k++) singular_vals[k]=1.0/PetscSqrtReal(singular_vals[k]);
            ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
            PetscStackCallBLAS("BLASgemm",BLASgemm_("N","N",&Blas_M,&Blas_N,&Blas_K,&one,&temp_quadrature_constraint[temp_indices[temp_start_ptr]],&Blas_LDA,correlation_mat,&Blas_LDB,&zero,temp_basis,&Blas_LDC));
            ierr = PetscFPTrapPop();CHKERRQ(ierr);
            for (k=0;k<temp_constraints-j;k++) {
              for (ii=0;ii<size_of_constraint;ii++) {
                temp_quadrature_constraint[temp_indices[temp_start_ptr+k]+ii]=singular_vals[temp_constraints-1-k]*temp_basis[(temp_constraints-1-k)*size_of_constraint+ii];
              }
            }
          }
#else  /* on missing GESVD */
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_M);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(temp_constraints,&Blas_N);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
          ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
#if !defined(PETSC_USE_COMPLEX)
          PetscStackCallBLAS("LAPACKgesvd",LAPACKgesvd_("O","N",&Blas_M,&Blas_N,&temp_quadrature_constraint[temp_indices[temp_start_ptr]],&Blas_LDA,singular_vals,&dummy_scalar,&dummy_int,&dummy_scalar,&dummy_int,work,&lwork,&lierr));
#else
          PetscStackCallBLAS("LAPACKgesvd",LAPACKgesvd_("O","N",&Blas_M,&Blas_N,&temp_quadrature_constraint[temp_indices[temp_start_ptr]],&Blas_LDA,singular_vals,&dummy_scalar,&dummy_int,&dummy_scalar,&dummy_int,work,&lwork,rwork,&lierr));
#endif
          if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in GESVD Lapack routine %d",(int)lierr);
          ierr = PetscFPTrapPop();CHKERRQ(ierr);
          /* retain eigenvalues greater than tol: note that LAPACKgesvd gives eigs in descending order */
          k = temp_constraints;
          if (k > size_of_constraint) k = size_of_constraint;
          j = 0;
          while (j < k && singular_vals[k-j-1] < tol) j++;
          valid_constraints = k-j;
          total_counts = total_counts-temp_constraints+valid_constraints;
#endif /* on missing GESVD */
        }
      }
      /* setting change_of_basis flag is safe now */
      if (boolforchange) {
        for (j=0;j<valid_constraints;j++) {
          PetscBTSet(change_basis,total_counts-j-1);
        }
      }
    }
    /* free workspace */
    if (!skip_lapack || pcbddc->use_qr_single) {
      ierr = PetscFree4(gidxs,permutation,temp_indices_to_constraint_work,temp_quadrature_constraint_work);CHKERRQ(ierr);
    }
    if (!skip_lapack) {
      ierr = PetscFree(work);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
      ierr = PetscFree(rwork);CHKERRQ(ierr);
#endif
      ierr = PetscFree(singular_vals);CHKERRQ(ierr);
#if defined(PETSC_MISSING_LAPACK_GESVD)
      ierr = PetscFree(correlation_mat);CHKERRQ(ierr);
      ierr = PetscFree(temp_basis);CHKERRQ(ierr);
#endif
    }
    for (k=0;k<nnsp_size;k++) {
      ierr = VecDestroy(&localnearnullsp[k]);CHKERRQ(ierr);
    }
    ierr = PetscFree(localnearnullsp);CHKERRQ(ierr);
    /* free index sets of faces, edges and vertices */
    for (i=0;i<n_ISForFaces;i++) {
      ierr = ISDestroy(&ISForFaces[i]);CHKERRQ(ierr);
    }
    if (n_ISForFaces) {
      ierr = PetscFree(ISForFaces);CHKERRQ(ierr);
    }
    for (i=0;i<n_ISForEdges;i++) {
      ierr = ISDestroy(&ISForEdges[i]);CHKERRQ(ierr);
    }
    if (n_ISForEdges) {
      ierr = PetscFree(ISForEdges);CHKERRQ(ierr);
    }
    ierr = ISDestroy(&ISForVertices);CHKERRQ(ierr);
  } else {
    PCBDDCSubSchurs sub_schurs = pcbddc->sub_schurs;
    PetscInt        cum = 0;

    total_counts = 0;
    n_vertices = 0;
    if (sub_schurs->is_Ej_com && pcbddc->use_vertices) {
      ierr = ISGetLocalSize(sub_schurs->is_Ej_com,&n_vertices);CHKERRQ(ierr);
    }
    max_constraints = 0;
    for (i=0;i<sub_schurs->n_subs+n_vertices;i++) {
      total_counts += pcbddc->adaptive_constraints_n[i];
      max_constraints = PetscMax(max_constraints,pcbddc->adaptive_constraints_n[i]);
    }
    temp_indices = pcbddc->adaptive_constraints_ptrs;
    temp_indices_to_constraint = pcbddc->adaptive_constraints_idxs;
    temp_quadrature_constraint = pcbddc->adaptive_constraints_data;

#if 0
    printf("Found %d totals\n",total_counts);
    for (i=0;i<total_counts;i++) {
      printf("const %d, start %d",i,temp_indices[i]);
      printf(" end %d:\n",temp_indices[i+1]);
      for (j=temp_indices[i];j<temp_indices[i+1];j++) {
        printf("  idxs %d",temp_indices_to_constraint[j]);
        printf("  data %1.2e\n",temp_quadrature_constraint[j]);
      }
    }
    for (i=0;i<n_vertices;i++) {
      PetscPrintf(PETSC_COMM_SELF,"[%d] vertex %d, n %d\n",PetscGlobalRank,i,pcbddc->adaptive_constraints_n[i]);
    }
    for (i=0;i<sub_schurs->n_subs;i++) {
      PetscPrintf(PETSC_COMM_SELF,"[%d] sub %d, edge %d, n %d\n",PetscGlobalRank,i,(PetscBool)PetscBTLookup(sub_schurs->is_edge,i),pcbddc->adaptive_constraints_n[i+n_vertices]);
    }
#endif

    max_size_of_constraint = 0;
    for (i=0;i<total_counts;i++) max_size_of_constraint = PetscMax(max_size_of_constraint,temp_indices[i+1]-temp_indices[i]);
    ierr = PetscMalloc1(temp_indices[total_counts],&temp_indices_to_constraint_B);CHKERRQ(ierr);
    /* Change of basis */
    ierr = PetscBTCreate(total_counts,&change_basis);CHKERRQ(ierr);
    if (pcbddc->use_change_of_basis) {
      cum = n_vertices;
      for (i=0;i<sub_schurs->n_subs;i++) {
        if (PetscBTLookup(sub_schurs->is_edge,i) || pcbddc->use_change_on_faces) {
          for (j=0;j<pcbddc->adaptive_constraints_n[i+n_vertices];j++) {
            ierr = PetscBTSet(change_basis,cum+j);CHKERRQ(ierr);
          }
        }
        cum += pcbddc->adaptive_constraints_n[i+n_vertices];
      }
    }
  }

  /* map temp_indices_to_constraint in boundary numbering */
  ierr = ISGlobalToLocalMappingApply(pcis->BtoNmap,IS_GTOLM_DROP,temp_indices[total_counts],temp_indices_to_constraint,&i,temp_indices_to_constraint_B);CHKERRQ(ierr);
  if (i != temp_indices[total_counts]) {
    SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"Error in boundary numbering for constraints indices %d != %d\n",temp_indices[total_counts],i);
  }

  /* set quantities in pcbddc data structure and store previous primal size */
  /* n_vertices defines the number of subdomain corners in the primal space */
  /* n_constraints defines the number of averages (they can be point primal dofs if change of basis is requested) */
  olocal_primal_size = pcbddc->local_primal_size;
  pcbddc->local_primal_size = total_counts;
  pcbddc->n_vertices = n_vertices;
  pcbddc->n_constraints = pcbddc->local_primal_size-pcbddc->n_vertices;

  /* Create constraint matrix */
  /* The constraint matrix is used to compute the l2g map of primal dofs */
  /* so we need to set it up properly either with or without change of basis */
  ierr = MatCreate(PETSC_COMM_SELF,&pcbddc->ConstraintMatrix);CHKERRQ(ierr);
  ierr = MatSetType(pcbddc->ConstraintMatrix,MATAIJ);CHKERRQ(ierr);
  ierr = MatSetSizes(pcbddc->ConstraintMatrix,pcbddc->local_primal_size,pcis->n,pcbddc->local_primal_size,pcis->n);CHKERRQ(ierr);
  /* array to compute a local numbering of constraints : vertices first then constraints */
  ierr = PetscMalloc1(pcbddc->local_primal_size,&aux_primal_numbering);CHKERRQ(ierr);
  /* array to select the proper local node (of minimum index with respect to global ordering) when changing the basis */
  /* note: it should not be needed since IS for faces and edges are already sorted by global ordering when analyzing the graph but... just in case */
  ierr = PetscMalloc1(pcbddc->local_primal_size,&aux_primal_minloc);CHKERRQ(ierr);
  /* auxiliary stuff for basis change */
  ierr = PetscMalloc1(max_size_of_constraint,&global_indices);CHKERRQ(ierr);
  ierr = PetscBTCreate(pcis->n_B,&touched);CHKERRQ(ierr);

  /* find primal_dofs: subdomain corners plus dofs selected as primal after change of basis */
  total_primal_vertices=0;
  for (i=0;i<pcbddc->local_primal_size;i++) {
    size_of_constraint=temp_indices[i+1]-temp_indices[i];
    if (size_of_constraint == 1) {
      ierr = PetscBTSet(touched,temp_indices_to_constraint_B[temp_indices[i]]);CHKERRQ(ierr);
      aux_primal_numbering[total_primal_vertices]=temp_indices_to_constraint[temp_indices[i]];
      aux_primal_minloc[total_primal_vertices]=0;
      total_primal_vertices++;
    } else if (PetscBTLookup(change_basis,i)) { /* Same procedure used in PCBDDCGetPrimalConstraintsLocalIdx */
      PetscInt min_loc,min_index;
      ierr = ISLocalToGlobalMappingApply(pcbddc->mat_graph->l2gmap,size_of_constraint,&temp_indices_to_constraint[temp_indices[i]],global_indices);CHKERRQ(ierr);
      /* find first untouched local node */
      k = 0;
      while (PetscBTLookup(touched,temp_indices_to_constraint_B[temp_indices[i]+k])) k++;
      min_index = global_indices[k];
      min_loc = k;
      /* search the minimum among global nodes already untouched on the cc */
      for (k=1;k<size_of_constraint;k++) {
        /* there can be more than one constraint on a single connected component */
        if (!PetscBTLookup(touched,temp_indices_to_constraint_B[temp_indices[i]+k]) && min_index > global_indices[k]) {
          min_index = global_indices[k];
          min_loc = k;
        }
      }
      ierr = PetscBTSet(touched,temp_indices_to_constraint_B[temp_indices[i]+min_loc]);CHKERRQ(ierr);
      aux_primal_numbering[total_primal_vertices]=temp_indices_to_constraint[temp_indices[i]+min_loc];
      aux_primal_minloc[total_primal_vertices]=min_loc;
      total_primal_vertices++;
    }
  }
  /* determine if a QR strategy is needed for change of basis */
  qr_needed = PETSC_FALSE;
  ierr = PetscBTCreate(pcbddc->local_primal_size,&qr_needed_idx);CHKERRQ(ierr);
  for (i=pcbddc->n_vertices;i<pcbddc->local_primal_size;i++) {
    if (PetscBTLookup(change_basis,i)) {
      if (!pcbddc->use_qr_single && !pcbddc->faster_deluxe) {
        size_of_constraint = temp_indices[i+1]-temp_indices[i];
        j = 0;
        for (k=0;k<size_of_constraint;k++) {
          if (PetscBTLookup(touched,temp_indices_to_constraint_B[temp_indices[i]+k])) {
            j++;
          }
        }
        /* found more than one primal dof on the cc */
        if (j > 1) {
          PetscBTSet(qr_needed_idx,i);
          qr_needed = PETSC_TRUE;
        }
      } else {
        PetscBTSet(qr_needed_idx,i);
        qr_needed = PETSC_TRUE;
      }
    }
  }
  /* free workspace */
  ierr = PetscFree(global_indices);CHKERRQ(ierr);

  /* permute indices in order to have a sorted set of vertices */
  ierr = PetscSortInt(total_primal_vertices,aux_primal_numbering);CHKERRQ(ierr);

  /* nonzero structure of constraint matrix */
  ierr = PetscMalloc1(pcbddc->local_primal_size,&nnz);CHKERRQ(ierr);
  for (i=0;i<total_primal_vertices;i++) nnz[i]=1;
  j=total_primal_vertices;
  for (i=pcbddc->n_vertices;i<pcbddc->local_primal_size;i++) {
    if (!PetscBTLookup(change_basis,i)) {
      nnz[j]=temp_indices[i+1]-temp_indices[i];
      j++;
    }
  }
  ierr = MatSeqAIJSetPreallocation(pcbddc->ConstraintMatrix,0,nnz);CHKERRQ(ierr);
  ierr = PetscFree(nnz);CHKERRQ(ierr);
  /* set values in constraint matrix */
  for (i=0;i<total_primal_vertices;i++) {
    ierr = MatSetValue(pcbddc->ConstraintMatrix,i,aux_primal_numbering[i],1.0,INSERT_VALUES);CHKERRQ(ierr);
  }
  total_counts = total_primal_vertices;
  for (i=pcbddc->n_vertices;i<pcbddc->local_primal_size;i++) {
    if (!PetscBTLookup(change_basis,i)) {
      size_of_constraint=temp_indices[i+1]-temp_indices[i];
      ierr = MatSetValues(pcbddc->ConstraintMatrix,1,&total_counts,size_of_constraint,&temp_indices_to_constraint[temp_indices[i]],&temp_quadrature_constraint[temp_indices[i]],INSERT_VALUES);CHKERRQ(ierr);
      total_counts++;
    }
  }
  /* assembling */
  ierr = MatAssemblyBegin(pcbddc->ConstraintMatrix,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(pcbddc->ConstraintMatrix,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  /*
  ierr = PetscViewerSetFormat(PETSC_VIEWER_STDOUT_SELF,PETSC_VIEWER_ASCII_MATLAB);CHKERRQ(ierr);
  ierr = MatView(pcbddc->ConstraintMatrix,(PetscViewer)0);CHKERRQ(ierr);
  */
  /* Create matrix for change of basis. We don't need it in case pcbddc->use_change_of_basis is FALSE */
  if (pcbddc->use_change_of_basis) {
    /* dual and primal dofs on a single cc */
    PetscInt     dual_dofs,primal_dofs;
    /* iterator on aux_primal_minloc (ordered as read from nearnullspace: vertices, edges and then constraints) */
    PetscInt     primal_counter;
    /* working stuff for GEQRF */
    PetscScalar  *qr_basis,*qr_tau = NULL,*qr_work,lqr_work_t;
    PetscBLASInt lqr_work;
    /* working stuff for UNGQR */
    PetscScalar  *gqr_work,lgqr_work_t;
    PetscBLASInt lgqr_work;
    /* working stuff for TRTRS */
    PetscScalar  *trs_rhs;
    PetscBLASInt Blas_NRHS;
    /* pointers for values insertion into change of basis matrix */
    PetscInt     *start_rows,*start_cols;
    PetscScalar  *start_vals;
    /* working stuff for values insertion */
    PetscBT      is_primal;
    /* matrix sizes */
    PetscInt     global_size,local_size;
    /* temporary change of basis */
    Mat          localChangeOfBasisMatrix;
    /* extra space for debugging */
    PetscScalar  *dbg_work;

    /* local temporary change of basis acts on local interfaces -> dimension is n_B x n_B */
    ierr = MatCreate(PETSC_COMM_SELF,&localChangeOfBasisMatrix);CHKERRQ(ierr);
    ierr = MatSetType(localChangeOfBasisMatrix,MATAIJ);CHKERRQ(ierr);
    ierr = MatSetSizes(localChangeOfBasisMatrix,pcis->n,pcis->n,pcis->n,pcis->n);CHKERRQ(ierr);
    /* nonzeros for local mat */
    ierr = PetscMalloc1(pcis->n,&nnz);CHKERRQ(ierr);
    for (i=0;i<pcis->n;i++) nnz[i]=1;
    for (i=pcbddc->n_vertices;i<pcbddc->local_primal_size;i++) {
      if (PetscBTLookup(change_basis,i)) {
        size_of_constraint = temp_indices[i+1]-temp_indices[i];
        if (PetscBTLookup(qr_needed_idx,i)) {
          for (j=0;j<size_of_constraint;j++) nnz[temp_indices_to_constraint[temp_indices[i]+j]] = size_of_constraint;
        } else {
          for (j=0;j<size_of_constraint;j++) nnz[temp_indices_to_constraint[temp_indices[i]+j]] = 2;
          /* get local primal index on the cc */
          j = 0;
          while (!PetscBTLookup(touched,temp_indices_to_constraint_B[temp_indices[i]+j])) j++;
          nnz[temp_indices_to_constraint[temp_indices[i]+j]] = size_of_constraint;
        }
      }
    }
    ierr = MatSeqAIJSetPreallocation(localChangeOfBasisMatrix,0,nnz);CHKERRQ(ierr);
    ierr = PetscFree(nnz);CHKERRQ(ierr);
    /* Set initial identity in the matrix */
    for (i=0;i<pcis->n;i++) {
      ierr = MatSetValue(localChangeOfBasisMatrix,i,i,1.0,INSERT_VALUES);CHKERRQ(ierr);
    }

    if (pcbddc->dbg_flag) {
      ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"--------------------------------------------------------------\n");CHKERRQ(ierr);
      ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Checking change of basis computation for subdomain %04d\n",PetscGlobalRank);CHKERRQ(ierr);
    }


    /* Now we loop on the constraints which need a change of basis */
    /*
       Change of basis matrix is evaluated similarly to the FIRST APPROACH in
       Klawonn and Widlund, Dual-primal FETI-DP methods for linear elasticity, (see Sect 6.2.1)

       Basic blocks of change of basis matrix T computed by

          - Using the following block transformation if there is only a primal dof on the cc (and -pc_bddc_use_qr_single is not specified)

            | 1        0   ...        0         s_1/S |
            | 0        1   ...        0         s_2/S |
            |              ...                        |
            | 0        ...            1     s_{n-1}/S |
            | -s_1/s_n ...    -s_{n-1}/s_n      s_n/S |

            with S = \sum_{i=1}^n s_i^2
            NOTE: in the above example, the primal dof is the last one of the edge in LOCAL ordering
                  in the current implementation, the primal dof is the first one of the edge in GLOBAL ordering

          - QR decomposition of constraints otherwise
    */
    if (qr_needed) {
      /* space to store Q */
      ierr = PetscMalloc1(max_size_of_constraint*max_size_of_constraint,&qr_basis);CHKERRQ(ierr);
      /* first we issue queries for optimal work */
      ierr = PetscBLASIntCast(max_size_of_constraint,&Blas_M);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(max_constraints,&Blas_N);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(max_size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
      lqr_work = -1;
      PetscStackCallBLAS("LAPACKgeqrf",LAPACKgeqrf_(&Blas_M,&Blas_N,qr_basis,&Blas_LDA,qr_tau,&lqr_work_t,&lqr_work,&lierr));
      if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in query to GEQRF Lapack routine %d",(int)lierr);
      ierr = PetscBLASIntCast((PetscInt)PetscRealPart(lqr_work_t),&lqr_work);CHKERRQ(ierr);
      ierr = PetscMalloc1((PetscInt)PetscRealPart(lqr_work_t),&qr_work);CHKERRQ(ierr);
      lgqr_work = -1;
      ierr = PetscBLASIntCast(max_size_of_constraint,&Blas_M);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(max_size_of_constraint,&Blas_N);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(max_constraints,&Blas_K);CHKERRQ(ierr);
      ierr = PetscBLASIntCast(max_size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
      if (Blas_K>Blas_M) Blas_K=Blas_M; /* adjust just for computing optimal work */
      PetscStackCallBLAS("LAPACKungqr",LAPACKungqr_(&Blas_M,&Blas_N,&Blas_K,qr_basis,&Blas_LDA,qr_tau,&lgqr_work_t,&lgqr_work,&lierr));
      if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in query to UNGQR Lapack routine %d",(int)lierr);
      ierr = PetscBLASIntCast((PetscInt)PetscRealPart(lgqr_work_t),&lgqr_work);CHKERRQ(ierr);
      ierr = PetscMalloc1((PetscInt)PetscRealPart(lgqr_work_t),&gqr_work);CHKERRQ(ierr);
      /* array to store scaling factors for reflectors */
      ierr = PetscMalloc1(max_constraints,&qr_tau);CHKERRQ(ierr);
      /* array to store rhs and solution of triangular solver */
      ierr = PetscMalloc1(max_constraints*max_constraints,&trs_rhs);CHKERRQ(ierr);
      /* allocating workspace for check */
      if (pcbddc->dbg_flag) {
        ierr = PetscMalloc1(max_size_of_constraint*(max_constraints+max_size_of_constraint),&dbg_work);CHKERRQ(ierr);
      }
    }
    /* array to store whether a node is primal or not */
    ierr = PetscBTCreate(pcis->n_B,&is_primal);CHKERRQ(ierr);
    ierr = PetscMalloc1(total_primal_vertices,&aux_primal_numbering_B);CHKERRQ(ierr);
    ierr = ISGlobalToLocalMappingApply(pcis->BtoNmap,IS_GTOLM_DROP,total_primal_vertices,aux_primal_numbering,&i,aux_primal_numbering_B);CHKERRQ(ierr);
    if (i != total_primal_vertices) {
      SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"Error in boundary numbering for BDDC vertices! %d != %d\n",total_primal_vertices,i);
    }
    for (i=0;i<total_primal_vertices;i++) {
      ierr = PetscBTSet(is_primal,aux_primal_numbering_B[i]);CHKERRQ(ierr);
    }
    ierr = PetscFree(aux_primal_numbering_B);CHKERRQ(ierr);

    /* loop on constraints and see whether or not they need a change of basis and compute it */
    /* -> using implicit ordering contained in temp_indices data */
    total_counts = pcbddc->n_vertices;
    primal_counter = total_counts;
    while (total_counts<pcbddc->local_primal_size) {
      primal_dofs = 1;
      if (PetscBTLookup(change_basis,total_counts)) {
        /* get all constraints with same support: if more then one constraint is present on the cc then surely indices are stored contiguosly */
        while (total_counts+primal_dofs < pcbddc->local_primal_size && temp_indices_to_constraint[temp_indices[total_counts]] == temp_indices_to_constraint[temp_indices[total_counts+primal_dofs]]) {
          primal_dofs++;
        }
        /* get constraint info */
        size_of_constraint = temp_indices[total_counts+1]-temp_indices[total_counts];
        dual_dofs = size_of_constraint-primal_dofs;

        if (pcbddc->dbg_flag) {
          ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Constraints %d to %d (incl) need a change of basis (size %d)\n",total_counts,total_counts+primal_dofs-1,size_of_constraint);CHKERRQ(ierr);
        }

        if (PetscBTLookup(qr_needed_idx,total_counts)) { /* QR */

          /* copy quadrature constraints for change of basis check */
          if (pcbddc->dbg_flag) {
            ierr = PetscMemcpy(dbg_work,&temp_quadrature_constraint[temp_indices[total_counts]],size_of_constraint*primal_dofs*sizeof(PetscScalar));CHKERRQ(ierr);
          }
          /* copy temporary constraints into larger work vector (in order to store all columns of Q) */
          ierr = PetscMemcpy(qr_basis,&temp_quadrature_constraint[temp_indices[total_counts]],size_of_constraint*primal_dofs*sizeof(PetscScalar));CHKERRQ(ierr);

          /* compute QR decomposition of constraints */
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_M);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(primal_dofs,&Blas_N);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
          ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
          PetscStackCallBLAS("LAPACKgeqrf",LAPACKgeqrf_(&Blas_M,&Blas_N,qr_basis,&Blas_LDA,qr_tau,qr_work,&lqr_work,&lierr));
          if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in GEQRF Lapack routine %d",(int)lierr);
          ierr = PetscFPTrapPop();CHKERRQ(ierr);

          /* explictly compute R^-T */
          ierr = PetscMemzero(trs_rhs,primal_dofs*primal_dofs*sizeof(*trs_rhs));CHKERRQ(ierr);
          for (j=0;j<primal_dofs;j++) trs_rhs[j*(primal_dofs+1)] = 1.0;
          ierr = PetscBLASIntCast(primal_dofs,&Blas_N);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(primal_dofs,&Blas_NRHS);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(primal_dofs,&Blas_LDB);CHKERRQ(ierr);
          ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
          PetscStackCallBLAS("LAPACKtrtrs",LAPACKtrtrs_("U","T","N",&Blas_N,&Blas_NRHS,qr_basis,&Blas_LDA,trs_rhs,&Blas_LDB,&lierr));
          if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in TRTRS Lapack routine %d",(int)lierr);
          ierr = PetscFPTrapPop();CHKERRQ(ierr);

          /* explicitly compute all columns of Q (Q = [Q1 | Q2] ) overwriting QR factorization in qr_basis */
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_M);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_N);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(primal_dofs,&Blas_K);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
          ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
          PetscStackCallBLAS("LAPACKungqr",LAPACKungqr_(&Blas_M,&Blas_N,&Blas_K,qr_basis,&Blas_LDA,qr_tau,gqr_work,&lgqr_work,&lierr));
          if (lierr) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Error in UNGQR Lapack routine %d",(int)lierr);
          ierr = PetscFPTrapPop();CHKERRQ(ierr);

          /* first primal_dofs columns of Q need to be re-scaled in order to be unitary w.r.t constraints
             i.e. C_{pxn}*Q_{nxn} should be equal to [I_pxp | 0_pxd] (see check below)
             where n=size_of_constraint, p=primal_dofs, d=dual_dofs (n=p+d), I and 0 identity and null matrix resp. */
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_M);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(primal_dofs,&Blas_N);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(primal_dofs,&Blas_K);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(primal_dofs,&Blas_LDB);CHKERRQ(ierr);
          ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDC);CHKERRQ(ierr);
          ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
          PetscStackCallBLAS("BLASgemm",BLASgemm_("N","N",&Blas_M,&Blas_N,&Blas_K,&one,qr_basis,&Blas_LDA,trs_rhs,&Blas_LDB,&zero,&temp_quadrature_constraint[temp_indices[total_counts]],&Blas_LDC));
          ierr = PetscFPTrapPop();CHKERRQ(ierr);
          ierr = PetscMemcpy(qr_basis,&temp_quadrature_constraint[temp_indices[total_counts]],size_of_constraint*primal_dofs*sizeof(PetscScalar));CHKERRQ(ierr);

          /* insert values in change of basis matrix respecting global ordering of new primal dofs */
          start_rows = &temp_indices_to_constraint[temp_indices[total_counts]];
          /* insert cols for primal dofs */
          for (j=0;j<primal_dofs;j++) {
            start_vals = &qr_basis[j*size_of_constraint];
            start_cols = &temp_indices_to_constraint[temp_indices[total_counts]+aux_primal_minloc[primal_counter+j]];
            ierr = MatSetValues(localChangeOfBasisMatrix,size_of_constraint,start_rows,1,start_cols,start_vals,INSERT_VALUES);CHKERRQ(ierr);
          }
          /* insert cols for dual dofs */
          for (j=0,k=0;j<dual_dofs;k++) {
            if (!PetscBTLookup(is_primal,temp_indices_to_constraint_B[temp_indices[total_counts]+k])) {
              start_vals = &qr_basis[(primal_dofs+j)*size_of_constraint];
              start_cols = &temp_indices_to_constraint[temp_indices[total_counts]+k];
              ierr = MatSetValues(localChangeOfBasisMatrix,size_of_constraint,start_rows,1,start_cols,start_vals,INSERT_VALUES);CHKERRQ(ierr);
              j++;
            }
          }

          /* check change of basis */
          if (pcbddc->dbg_flag) {
            PetscInt   ii,jj;
            PetscBool valid_qr=PETSC_TRUE;
            ierr = PetscBLASIntCast(primal_dofs,&Blas_M);CHKERRQ(ierr);
            ierr = PetscBLASIntCast(size_of_constraint,&Blas_N);CHKERRQ(ierr);
            ierr = PetscBLASIntCast(size_of_constraint,&Blas_K);CHKERRQ(ierr);
            ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDA);CHKERRQ(ierr);
            ierr = PetscBLASIntCast(size_of_constraint,&Blas_LDB);CHKERRQ(ierr);
            ierr = PetscBLASIntCast(primal_dofs,&Blas_LDC);CHKERRQ(ierr);
            ierr = PetscFPTrapPush(PETSC_FP_TRAP_OFF);CHKERRQ(ierr);
            PetscStackCallBLAS("BLASgemm",BLASgemm_("T","N",&Blas_M,&Blas_N,&Blas_K,&one,dbg_work,&Blas_LDA,qr_basis,&Blas_LDB,&zero,&dbg_work[size_of_constraint*primal_dofs],&Blas_LDC));
            ierr = PetscFPTrapPop();CHKERRQ(ierr);
            for (jj=0;jj<size_of_constraint;jj++) {
              for (ii=0;ii<primal_dofs;ii++) {
                if (ii != jj && PetscAbsScalar(dbg_work[size_of_constraint*primal_dofs+jj*primal_dofs+ii]) > 1.e-12) valid_qr = PETSC_FALSE;
                if (ii == jj && PetscAbsScalar(dbg_work[size_of_constraint*primal_dofs+jj*primal_dofs+ii]-1.0) > 1.e-12) valid_qr = PETSC_FALSE;
              }
            }
            if (!valid_qr) {
              ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"\t-> wrong change of basis!\n");CHKERRQ(ierr);
              for (jj=0;jj<size_of_constraint;jj++) {
                for (ii=0;ii<primal_dofs;ii++) {
                  if (ii != jj && PetscAbsScalar(dbg_work[size_of_constraint*primal_dofs+jj*primal_dofs+ii]) > 1.e-12) {
                    PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"\tQr basis function %d is not orthogonal to constraint %d (%1.14e)!\n",jj,ii,PetscAbsScalar(dbg_work[size_of_constraint*primal_dofs+jj*primal_dofs+ii]));
                  }
                  if (ii == jj && PetscAbsScalar(dbg_work[size_of_constraint*primal_dofs+jj*primal_dofs+ii]-1.0) > 1.e-12) {
                    PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"\tQr basis function %d is not unitary w.r.t constraint %d (%1.14e)!\n",jj,ii,PetscAbsScalar(dbg_work[size_of_constraint*primal_dofs+jj*primal_dofs+ii]));
                  }
                }
              }
            } else {
              ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"\t-> right change of basis!\n");CHKERRQ(ierr);
            }
          }
        } else { /* simple transformation block */
          PetscInt    row,col;
          PetscScalar val,norm;

          ierr = PetscBLASIntCast(size_of_constraint,&Blas_N);CHKERRQ(ierr);
          PetscStackCallBLAS("BLASdot",norm = BLASdot_(&Blas_N,temp_quadrature_constraint+temp_indices[total_counts],&Blas_one,temp_quadrature_constraint+temp_indices[total_counts],&Blas_one));
          for (j=0;j<size_of_constraint;j++) {
            PetscInt row_B = temp_indices_to_constraint_B[temp_indices[total_counts]+j];
            row = temp_indices_to_constraint[temp_indices[total_counts]+j];
            if (!PetscBTLookup(is_primal,row_B)) {
              col = temp_indices_to_constraint[temp_indices[total_counts]+aux_primal_minloc[primal_counter]];
              ierr = MatSetValue(localChangeOfBasisMatrix,row,row,1.0,INSERT_VALUES);CHKERRQ(ierr);
              ierr = MatSetValue(localChangeOfBasisMatrix,row,col,temp_quadrature_constraint[temp_indices[total_counts]+j]/norm,INSERT_VALUES);CHKERRQ(ierr);
            } else {
              for (k=0;k<size_of_constraint;k++) {
                col = temp_indices_to_constraint[temp_indices[total_counts]+k];
                if (row != col) {
                  val = -temp_quadrature_constraint[temp_indices[total_counts]+k]/temp_quadrature_constraint[temp_indices[total_counts]+aux_primal_minloc[primal_counter]];
                } else {
                  val = temp_quadrature_constraint[temp_indices[total_counts]+aux_primal_minloc[primal_counter]]/norm;
                }
                ierr = MatSetValue(localChangeOfBasisMatrix,row,col,val,INSERT_VALUES);CHKERRQ(ierr);
              }
            }
          }
          if (pcbddc->dbg_flag) {
            ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"\t-> using standard change of basis\n");CHKERRQ(ierr);
          }
        }
        /* increment primal counter */
        primal_counter += primal_dofs;
      } else {
        if (pcbddc->dbg_flag) {
          ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Constraint %d does not need a change of basis (size %d)\n",total_counts,temp_indices[total_counts+1]-temp_indices[total_counts]);CHKERRQ(ierr);
        }
      }
      /* increment constraint counter total_counts */
      total_counts += primal_dofs;
    }

    /* free workspace */
    if (qr_needed) {
      if (pcbddc->dbg_flag) {
        ierr = PetscFree(dbg_work);CHKERRQ(ierr);
      }
      ierr = PetscFree(trs_rhs);CHKERRQ(ierr);
      ierr = PetscFree(qr_tau);CHKERRQ(ierr);
      ierr = PetscFree(qr_work);CHKERRQ(ierr);
      ierr = PetscFree(gqr_work);CHKERRQ(ierr);
      ierr = PetscFree(qr_basis);CHKERRQ(ierr);
    }
    ierr = PetscBTDestroy(&is_primal);CHKERRQ(ierr);
    ierr = MatAssemblyBegin(localChangeOfBasisMatrix,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(localChangeOfBasisMatrix,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

    /* assembling of global change of variable */
    {
      Mat      tmat;
      PetscInt bs;

      ierr = VecGetSize(pcis->vec1_global,&global_size);CHKERRQ(ierr);
      ierr = VecGetLocalSize(pcis->vec1_global,&local_size);CHKERRQ(ierr);
      ierr = MatDuplicate(pc->pmat,MAT_DO_NOT_COPY_VALUES,&tmat);CHKERRQ(ierr);
      ierr = MatISSetLocalMat(tmat,localChangeOfBasisMatrix);CHKERRQ(ierr);
      ierr = MatCreate(PetscObjectComm((PetscObject)pc),&pcbddc->ChangeOfBasisMatrix);CHKERRQ(ierr);
      ierr = MatSetType(pcbddc->ChangeOfBasisMatrix,MATAIJ);CHKERRQ(ierr);
      ierr = MatGetBlockSize(pc->pmat,&bs);CHKERRQ(ierr);
      ierr = MatSetBlockSize(pcbddc->ChangeOfBasisMatrix,bs);CHKERRQ(ierr);
      ierr = MatSetSizes(pcbddc->ChangeOfBasisMatrix,local_size,local_size,global_size,global_size);CHKERRQ(ierr);
      ierr = MatISSetMPIXAIJPreallocation_Private(tmat,pcbddc->ChangeOfBasisMatrix,PETSC_TRUE);CHKERRQ(ierr);
      ierr = MatISGetMPIXAIJ(tmat,MAT_REUSE_MATRIX,&pcbddc->ChangeOfBasisMatrix);CHKERRQ(ierr);
      ierr = MatDestroy(&tmat);CHKERRQ(ierr);
      ierr = VecSet(pcis->vec1_global,0.0);CHKERRQ(ierr);
      ierr = VecSet(pcis->vec1_N,1.0);CHKERRQ(ierr);
      ierr = VecScatterBegin(matis->ctx,pcis->vec1_N,pcis->vec1_global,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
      ierr = VecScatterEnd(matis->ctx,pcis->vec1_N,pcis->vec1_global,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
      ierr = VecReciprocal(pcis->vec1_global);CHKERRQ(ierr);
      ierr = MatDiagonalScale(pcbddc->ChangeOfBasisMatrix,pcis->vec1_global,NULL);CHKERRQ(ierr);
    }
    /* check */
    if (pcbddc->dbg_flag) {
      PetscReal error;
      Vec       x,x_change;

      ierr = VecDuplicate(pcis->vec1_global,&x);CHKERRQ(ierr);
      ierr = VecDuplicate(pcis->vec1_global,&x_change);CHKERRQ(ierr);
      ierr = VecSetRandom(x,NULL);CHKERRQ(ierr);
      ierr = VecCopy(x,pcis->vec1_global);CHKERRQ(ierr);
      ierr = VecScatterBegin(matis->ctx,x,pcis->vec1_N,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterEnd(matis->ctx,x,pcis->vec1_N,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = MatMult(localChangeOfBasisMatrix,pcis->vec1_N,pcis->vec2_N);CHKERRQ(ierr);
      ierr = VecScatterBegin(matis->ctx,pcis->vec2_N,x,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
      ierr = VecScatterEnd(matis->ctx,pcis->vec2_N,x,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
      ierr = MatMult(pcbddc->ChangeOfBasisMatrix,pcis->vec1_global,x_change);CHKERRQ(ierr);
      ierr = VecAXPY(x,-1.0,x_change);CHKERRQ(ierr);
      ierr = VecNorm(x,NORM_INFINITY,&error);CHKERRQ(ierr);
      ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Error global vs local change: %1.6e\n",error);CHKERRQ(ierr);
      ierr = VecDestroy(&x);CHKERRQ(ierr);
      ierr = VecDestroy(&x_change);CHKERRQ(ierr);
    }

    /* adapt sub_schurs computed (if any) */
    if (pcbddc->use_deluxe_scaling) {
      PCBDDCSubSchurs sub_schurs=pcbddc->sub_schurs;
      if (sub_schurs->S_Ej_all) {
        Mat S_new,tmat;
        IS is_all_N;

        ierr = ISLocalToGlobalMappingApplyIS(pcis->BtoNmap,sub_schurs->is_Ej_all,&is_all_N);CHKERRQ(ierr);
        ierr = MatGetSubMatrixUnsorted(localChangeOfBasisMatrix,is_all_N,is_all_N,&tmat);CHKERRQ(ierr);
        ierr = ISDestroy(&is_all_N);CHKERRQ(ierr);
        ierr = MatPtAP(sub_schurs->S_Ej_all,tmat,MAT_INITIAL_MATRIX,1.0,&S_new);CHKERRQ(ierr);
        ierr = MatDestroy(&sub_schurs->S_Ej_all);CHKERRQ(ierr);
        ierr = PetscObjectReference((PetscObject)S_new);CHKERRQ(ierr);
        sub_schurs->S_Ej_all = S_new;
        ierr = MatDestroy(&S_new);CHKERRQ(ierr);
        if (sub_schurs->sum_S_Ej_all) {
          ierr = MatPtAP(sub_schurs->sum_S_Ej_all,tmat,MAT_INITIAL_MATRIX,1.0,&S_new);CHKERRQ(ierr);
          ierr = MatDestroy(&sub_schurs->sum_S_Ej_all);CHKERRQ(ierr);
          ierr = PetscObjectReference((PetscObject)S_new);CHKERRQ(ierr);
          sub_schurs->sum_S_Ej_all = S_new;
          ierr = MatDestroy(&S_new);CHKERRQ(ierr);
        }
        ierr = MatDestroy(&tmat);CHKERRQ(ierr);
      }
    }
    ierr = MatDestroy(&localChangeOfBasisMatrix);CHKERRQ(ierr);
  } else if (pcbddc->user_ChangeOfBasisMatrix) {
    ierr = PetscObjectReference((PetscObject)pcbddc->user_ChangeOfBasisMatrix);CHKERRQ(ierr);
    pcbddc->ChangeOfBasisMatrix = pcbddc->user_ChangeOfBasisMatrix;
  }

  /* set up change of basis context */
  if (pcbddc->ChangeOfBasisMatrix) {
    PCBDDCChange_ctx change_ctx;

    if (!pcbddc->new_global_mat) {
      PetscInt global_size,local_size;

      ierr = VecGetSize(pcis->vec1_global,&global_size);CHKERRQ(ierr);
      ierr = VecGetLocalSize(pcis->vec1_global,&local_size);CHKERRQ(ierr);
      ierr = MatCreate(PetscObjectComm((PetscObject)pc),&pcbddc->new_global_mat);CHKERRQ(ierr);
      ierr = MatSetSizes(pcbddc->new_global_mat,local_size,local_size,global_size,global_size);CHKERRQ(ierr);
      ierr = MatSetType(pcbddc->new_global_mat,MATSHELL);CHKERRQ(ierr);
      ierr = MatShellSetOperation(pcbddc->new_global_mat,MATOP_MULT,(void (*)(void))PCBDDCMatMult_Private);CHKERRQ(ierr);
      ierr = MatShellSetOperation(pcbddc->new_global_mat,MATOP_MULT_TRANSPOSE,(void (*)(void))PCBDDCMatMultTranspose_Private);CHKERRQ(ierr);
      ierr = PetscNew(&change_ctx);CHKERRQ(ierr);
      ierr = MatShellSetContext(pcbddc->new_global_mat,change_ctx);CHKERRQ(ierr);
    } else {
      ierr = MatShellGetContext(pcbddc->new_global_mat,&change_ctx);CHKERRQ(ierr);
      ierr = MatDestroy(&change_ctx->global_change);CHKERRQ(ierr);
      ierr = VecDestroyVecs(2,&change_ctx->work);CHKERRQ(ierr);
    }
    if (!pcbddc->user_ChangeOfBasisMatrix) {
      ierr = PetscObjectReference((PetscObject)pcbddc->ChangeOfBasisMatrix);CHKERRQ(ierr);
      change_ctx->global_change = pcbddc->ChangeOfBasisMatrix;
    } else {
      ierr = PetscObjectReference((PetscObject)pcbddc->user_ChangeOfBasisMatrix);CHKERRQ(ierr);
      change_ctx->global_change = pcbddc->user_ChangeOfBasisMatrix;
    }
    ierr = VecDuplicateVecs(pcis->vec1_global,2,&change_ctx->work);CHKERRQ(ierr);
    ierr = MatSetUp(pcbddc->new_global_mat);CHKERRQ(ierr);
    ierr = MatAssemblyBegin(pcbddc->new_global_mat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(pcbddc->new_global_mat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  }

  /* get indices in local ordering for vertices and constraints */
  if (olocal_primal_size == pcbddc->local_primal_size) { /* if this is true, I need to check if a new primal space has been introduced */
    ierr = PetscMalloc1(olocal_primal_size,&oprimal_indices_local_idxs);CHKERRQ(ierr);
    ierr = PetscMemcpy(oprimal_indices_local_idxs,pcbddc->primal_indices_local_idxs,olocal_primal_size*sizeof(PetscInt));CHKERRQ(ierr);
  }
  ierr = PetscFree(aux_primal_numbering);CHKERRQ(ierr);
  ierr = PetscFree(pcbddc->primal_indices_local_idxs);CHKERRQ(ierr);
  ierr = PetscMalloc1(pcbddc->local_primal_size,&pcbddc->primal_indices_local_idxs);CHKERRQ(ierr);
  ierr = PCBDDCGetPrimalVerticesLocalIdx(pc,&i,&aux_primal_numbering);CHKERRQ(ierr);
  ierr = PetscMemcpy(pcbddc->primal_indices_local_idxs,aux_primal_numbering,i*sizeof(PetscInt));CHKERRQ(ierr);
  ierr = PetscFree(aux_primal_numbering);CHKERRQ(ierr);
  ierr = PCBDDCGetPrimalConstraintsLocalIdx(pc,&j,&aux_primal_numbering);CHKERRQ(ierr);
  ierr = PetscMemcpy(&pcbddc->primal_indices_local_idxs[i],aux_primal_numbering,j*sizeof(PetscInt));CHKERRQ(ierr);
  ierr = PetscFree(aux_primal_numbering);CHKERRQ(ierr);
  /* set quantities in PCBDDC data struct */
  pcbddc->n_actual_vertices = i;
  /* check if a new primal space has been introduced */
  pcbddc->new_primal_space_local = PETSC_TRUE;
  if (olocal_primal_size == pcbddc->local_primal_size) {
    ierr = PetscMemcmp(pcbddc->primal_indices_local_idxs,oprimal_indices_local_idxs,olocal_primal_size,&pcbddc->new_primal_space_local);CHKERRQ(ierr);
    pcbddc->new_primal_space_local = (PetscBool)(!pcbddc->new_primal_space_local);
    ierr = PetscFree(oprimal_indices_local_idxs);CHKERRQ(ierr);
  }
  /* new_primal_space will be used for numbering of coarse dofs, so it should be the same across all subdomains */
  ierr = MPI_Allreduce(&pcbddc->new_primal_space_local,&pcbddc->new_primal_space,1,MPIU_BOOL,MPI_LOR,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);

  /* flush dbg viewer */
  if (pcbddc->dbg_flag) {
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
  }

  /* free workspace */
  ierr = PetscBTDestroy(&touched);CHKERRQ(ierr);
  ierr = PetscBTDestroy(&qr_needed_idx);CHKERRQ(ierr);
  ierr = PetscFree(aux_primal_minloc);CHKERRQ(ierr);
  ierr = PetscBTDestroy(&change_basis);CHKERRQ(ierr);
  if (!pcbddc->adaptive_selection) {
    ierr = PetscFree(temp_indices);CHKERRQ(ierr);
    ierr = PetscFree3(temp_quadrature_constraint,temp_indices_to_constraint,temp_indices_to_constraint_B);CHKERRQ(ierr);
  } else {
    ierr = PetscFree4(pcbddc->adaptive_constraints_n,
                      pcbddc->adaptive_constraints_ptrs,
                      pcbddc->adaptive_constraints_idxs,
                      pcbddc->adaptive_constraints_data);CHKERRQ(ierr);
    ierr = PetscFree(temp_indices_to_constraint_B);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCAnalyzeInterface"
PetscErrorCode PCBDDCAnalyzeInterface(PC pc)
{
  PC_BDDC     *pcbddc = (PC_BDDC*)pc->data;
  PC_IS       *pcis = (PC_IS*)pc->data;
  Mat_IS      *matis  = (Mat_IS*)pc->pmat->data;
  PetscInt    ierr,i,vertex_size;
  PetscViewer viewer=pcbddc->dbg_viewer;

  PetscFunctionBegin;
  /* Reset previously computed graph */
  ierr = PCBDDCGraphReset(pcbddc->mat_graph);CHKERRQ(ierr);
  /* Init local Graph struct */
  ierr = PCBDDCGraphInit(pcbddc->mat_graph,matis->mapping);CHKERRQ(ierr);

  /* Check validity of the csr graph passed in by the user */
  if (pcbddc->mat_graph->nvtxs_csr != pcbddc->mat_graph->nvtxs) {
    ierr = PCBDDCGraphResetCSR(pcbddc->mat_graph);CHKERRQ(ierr);
  }

  /* Set default CSR adjacency of local dofs if not provided by the user with PCBDDCSetLocalAdjacencyGraph */
  if (!pcbddc->mat_graph->xadj || !pcbddc->mat_graph->adjncy) {
    PetscInt  *xadj,*adjncy;
    PetscInt  nvtxs;
    PetscBool flg_row=PETSC_FALSE;

    if (pcbddc->use_local_adj) {

      ierr = MatGetRowIJ(matis->A,0,PETSC_TRUE,PETSC_FALSE,&nvtxs,(const PetscInt**)&xadj,(const PetscInt**)&adjncy,&flg_row);CHKERRQ(ierr);
      if (flg_row) {
        ierr = PCBDDCSetLocalAdjacencyGraph(pc,nvtxs,xadj,adjncy,PETSC_COPY_VALUES);CHKERRQ(ierr);
        pcbddc->computed_rowadj = PETSC_TRUE;
      }
      ierr = MatRestoreRowIJ(matis->A,0,PETSC_TRUE,PETSC_FALSE,&nvtxs,(const PetscInt**)&xadj,(const PetscInt**)&adjncy,&flg_row);CHKERRQ(ierr);
    } else if (pcbddc->current_level) { /* just compute subdomain's connected components for coarser levels */
      IS                     is_dummy;
      ISLocalToGlobalMapping l2gmap_dummy;
      PetscInt               j,sum;
      PetscInt               *cxadj,*cadjncy;
      const PetscInt         *idxs;
      PCBDDCGraph            graph;
      PetscBT                is_on_boundary;

      ierr = ISCreateStride(PETSC_COMM_SELF,pcis->n,0,1,&is_dummy);CHKERRQ(ierr);
      ierr = ISLocalToGlobalMappingCreateIS(is_dummy,&l2gmap_dummy);CHKERRQ(ierr);
      ierr = ISDestroy(&is_dummy);CHKERRQ(ierr);
      ierr = PCBDDCGraphCreate(&graph);CHKERRQ(ierr);
      ierr = PCBDDCGraphInit(graph,l2gmap_dummy);CHKERRQ(ierr);
      ierr = ISLocalToGlobalMappingDestroy(&l2gmap_dummy);CHKERRQ(ierr);
      ierr = MatGetRowIJ(matis->A,0,PETSC_TRUE,PETSC_FALSE,&nvtxs,(const PetscInt**)&xadj,(const PetscInt**)&adjncy,&flg_row);CHKERRQ(ierr);
      if (flg_row) {
        graph->xadj = xadj;
        graph->adjncy = adjncy;
      }
      ierr = PCBDDCGraphSetUp(graph,1,NULL,NULL,0,NULL,NULL);CHKERRQ(ierr);
      ierr = PCBDDCGraphComputeConnectedComponents(graph);CHKERRQ(ierr);
      ierr = MatRestoreRowIJ(matis->A,0,PETSC_TRUE,PETSC_FALSE,&nvtxs,(const PetscInt**)&xadj,(const PetscInt**)&adjncy,&flg_row);CHKERRQ(ierr);

      if (pcbddc->dbg_flag) {
        ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"[%d] Found %d subdomains\n",PetscGlobalRank,graph->ncc);CHKERRQ(ierr);
        for (i=0;i<graph->ncc;i++) {
          ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"[%d] %d cc size %d\n",PetscGlobalRank,i,graph->cptr[i+1]-graph->cptr[i]);CHKERRQ(ierr);
        }
        ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      }

      ierr = PetscBTCreate(pcis->n,&is_on_boundary);CHKERRQ(ierr);
      ierr = ISGetIndices(pcis->is_B_local,&idxs);CHKERRQ(ierr);
      for (i=0;i<pcis->n_B;i++) {
        ierr = PetscBTSet(is_on_boundary,idxs[i]);CHKERRQ(ierr);
      }
      ierr = ISRestoreIndices(pcis->is_B_local,&idxs);CHKERRQ(ierr);

      ierr = PetscCalloc1(pcis->n+1,&cxadj);CHKERRQ(ierr);
      sum = 0;
      for (i=0;i<graph->ncc;i++) {
        PetscInt sizecc = 0;
        for (j=graph->cptr[i];j<graph->cptr[i+1];j++) {
          if (PetscBTLookup(is_on_boundary,graph->queue[j])) {
            sizecc++;
          }
        }
        for (j=graph->cptr[i];j<graph->cptr[i+1];j++) {
          if (PetscBTLookup(is_on_boundary,graph->queue[j])) {
            cxadj[graph->queue[j]] = sizecc;
          }
        }
        sum += sizecc*sizecc;
      }
      ierr = PetscMalloc1(sum,&cadjncy);CHKERRQ(ierr);
      sum = 0;
      for (i=0;i<pcis->n;i++) {
        PetscInt temp = cxadj[i];
        cxadj[i] = sum;
        sum += temp;
      }
      cxadj[pcis->n] = sum;
      for (i=0;i<graph->ncc;i++) {
        for (j=graph->cptr[i];j<graph->cptr[i+1];j++) {
          if (PetscBTLookup(is_on_boundary,graph->queue[j])) {
            PetscInt k,sizecc = 0;
            for (k=graph->cptr[i];k<graph->cptr[i+1];k++) {
              if (PetscBTLookup(is_on_boundary,graph->queue[k])) {
                cadjncy[cxadj[graph->queue[j]]+sizecc]=graph->queue[k];
                sizecc++;
              }
            }
          }
        }
      }
      if (pcis->n) {
        ierr = PCBDDCSetLocalAdjacencyGraph(pc,pcis->n,cxadj,cadjncy,PETSC_OWN_POINTER);CHKERRQ(ierr);
      } else {
        ierr = PetscFree(cxadj);CHKERRQ(ierr);
        ierr = PetscFree(cadjncy);CHKERRQ(ierr);
      }
      graph->xadj = 0;
      graph->adjncy = 0;
      ierr = PCBDDCGraphDestroy(&graph);CHKERRQ(ierr);
      ierr = PetscBTDestroy(&is_on_boundary);CHKERRQ(ierr);
    }
  }

  /* Set default dofs' splitting if no information has been provided by the user with PCBDDCSetDofsSplitting or PCBDDCSetDofsSplittingLocal */
  vertex_size = 1;
  if (pcbddc->user_provided_isfordofs) {
    if (pcbddc->n_ISForDofs) { /* need to convert from global to local and remove references to global dofs splitting */
      ierr = PetscMalloc1(pcbddc->n_ISForDofs,&pcbddc->ISForDofsLocal);CHKERRQ(ierr);
      for (i=0;i<pcbddc->n_ISForDofs;i++) {
        ierr = PCBDDCGlobalToLocal(matis->ctx,pcis->vec1_global,pcis->vec1_N,pcbddc->ISForDofs[i],&pcbddc->ISForDofsLocal[i]);CHKERRQ(ierr);
        ierr = ISDestroy(&pcbddc->ISForDofs[i]);CHKERRQ(ierr);
      }
      pcbddc->n_ISForDofsLocal = pcbddc->n_ISForDofs;
      pcbddc->n_ISForDofs = 0;
      ierr = PetscFree(pcbddc->ISForDofs);CHKERRQ(ierr);
    }
    /* mat block size as vertex size (used for elasticity with rigid body modes as nearnullspace) */
    ierr = MatGetBlockSize(matis->A,&vertex_size);CHKERRQ(ierr);
  } else {
    if (!pcbddc->n_ISForDofsLocal) { /* field split not present, create it in local ordering */
      ierr = MatGetBlockSize(pc->pmat,&pcbddc->n_ISForDofsLocal);CHKERRQ(ierr);
      ierr = PetscMalloc1(pcbddc->n_ISForDofsLocal,&pcbddc->ISForDofsLocal);CHKERRQ(ierr);
      for (i=0;i<pcbddc->n_ISForDofsLocal;i++) {
        ierr = ISCreateStride(PetscObjectComm((PetscObject)pc),pcis->n/pcbddc->n_ISForDofsLocal,i,pcbddc->n_ISForDofsLocal,&pcbddc->ISForDofsLocal[i]);CHKERRQ(ierr);
      }
    }
  }

  /* Setup of Graph */
  if (!pcbddc->DirichletBoundariesLocal && pcbddc->DirichletBoundaries) { /* need to convert from global to local */
    ierr = PCBDDCGlobalToLocal(matis->ctx,pcis->vec1_global,pcis->vec1_N,pcbddc->DirichletBoundaries,&pcbddc->DirichletBoundariesLocal);CHKERRQ(ierr);
  }
  if (!pcbddc->NeumannBoundariesLocal && pcbddc->NeumannBoundaries) { /* need to convert from global to local */
    ierr = PCBDDCGlobalToLocal(matis->ctx,pcis->vec1_global,pcis->vec1_N,pcbddc->NeumannBoundaries,&pcbddc->NeumannBoundariesLocal);CHKERRQ(ierr);
  }
  ierr = PCBDDCGraphSetUp(pcbddc->mat_graph,vertex_size,pcbddc->NeumannBoundariesLocal,pcbddc->DirichletBoundariesLocal,pcbddc->n_ISForDofsLocal,pcbddc->ISForDofsLocal,pcbddc->user_primal_vertices);CHKERRQ(ierr);

  /* Graph's connected components analysis */
  ierr = PCBDDCGraphComputeConnectedComponents(pcbddc->mat_graph);CHKERRQ(ierr);

  /* print some info to stdout */
  if (pcbddc->dbg_flag) {
    ierr = PCBDDCGraphASCIIView(pcbddc->mat_graph,pcbddc->dbg_flag,viewer);CHKERRQ(ierr);
  }

  /* mark topography has done */
  pcbddc->recompute_topography = PETSC_FALSE;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCGetPrimalVerticesLocalIdx"
PetscErrorCode  PCBDDCGetPrimalVerticesLocalIdx(PC pc, PetscInt *n_vertices, PetscInt **vertices_idx)
{
  PC_BDDC        *pcbddc = (PC_BDDC*)(pc->data);
  PetscInt       *vertices,*row_cmat_indices,n,i,size_of_constraint,local_primal_size;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  n = 0;
  vertices = 0;
  if (pcbddc->ConstraintMatrix) {
    ierr = MatGetSize(pcbddc->ConstraintMatrix,&local_primal_size,&i);CHKERRQ(ierr);
    for (i=0;i<local_primal_size;i++) {
      ierr = MatGetRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,NULL,NULL);CHKERRQ(ierr);
      if (size_of_constraint == 1) n++;
      ierr = MatRestoreRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,NULL,NULL);CHKERRQ(ierr);
    }
    if (vertices_idx) {
      ierr = PetscMalloc1(n,&vertices);CHKERRQ(ierr);
      n = 0;
      for (i=0;i<local_primal_size;i++) {
        ierr = MatGetRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,(const PetscInt**)&row_cmat_indices,NULL);CHKERRQ(ierr);
        if (size_of_constraint == 1) {
          vertices[n++]=row_cmat_indices[0];
        }
        ierr = MatRestoreRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,(const PetscInt**)&row_cmat_indices,NULL);CHKERRQ(ierr);
      }
    }
  }
  *n_vertices = n;
  if (vertices_idx) *vertices_idx = vertices;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCGetPrimalConstraintsLocalIdx"
PetscErrorCode  PCBDDCGetPrimalConstraintsLocalIdx(PC pc, PetscInt *n_constraints, PetscInt **constraints_idx)
{
  PC_BDDC        *pcbddc = (PC_BDDC*)(pc->data);
  PetscInt       *constraints_index,*row_cmat_indices,*row_cmat_global_indices;
  PetscInt       n,i,j,size_of_constraint,local_primal_size,local_size,max_size_of_constraint,min_index,min_loc;
  PetscBT        touched;
  PetscErrorCode ierr;

    /* This function assumes that the number of local constraints per connected component
       is not greater than the number of nodes defined for the connected component
       (otherwise we will surely have linear dependence between constraints and thus a singular coarse problem) */
  PetscFunctionBegin;
  n = 0;
  constraints_index = 0;
  if (pcbddc->ConstraintMatrix) {
    ierr = MatGetSize(pcbddc->ConstraintMatrix,&local_primal_size,&local_size);CHKERRQ(ierr);
    max_size_of_constraint = 0;
    for (i=0;i<local_primal_size;i++) {
      ierr = MatGetRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,NULL,NULL);CHKERRQ(ierr);
      if (size_of_constraint > 1) {
        n++;
      }
      max_size_of_constraint = PetscMax(size_of_constraint,max_size_of_constraint);
      ierr = MatRestoreRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,NULL,NULL);CHKERRQ(ierr);
    }
    if (constraints_idx) {
      ierr = PetscMalloc1(n,&constraints_index);CHKERRQ(ierr);
      ierr = PetscMalloc1(max_size_of_constraint,&row_cmat_global_indices);CHKERRQ(ierr);
      ierr = PetscBTCreate(local_size,&touched);CHKERRQ(ierr);
      n = 0;
      for (i=0;i<local_primal_size;i++) {
        ierr = MatGetRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,(const PetscInt**)&row_cmat_indices,NULL);CHKERRQ(ierr);
        if (size_of_constraint > 1) {
          ierr = ISLocalToGlobalMappingApply(pcbddc->mat_graph->l2gmap,size_of_constraint,row_cmat_indices,row_cmat_global_indices);CHKERRQ(ierr);
          /* find first untouched local node */
          j = 0;
          while (PetscBTLookup(touched,row_cmat_indices[j])) j++;
          min_index = row_cmat_global_indices[j];
          min_loc = j;
          /* search the minimum among nodes not yet touched on the connected component
             since there can be more than one constraint on a single cc */
          for (j=1;j<size_of_constraint;j++) {
            if (!PetscBTLookup(touched,row_cmat_indices[j]) && min_index > row_cmat_global_indices[j]) {
              min_index = row_cmat_global_indices[j];
              min_loc = j;
            }
          }
          ierr = PetscBTSet(touched,row_cmat_indices[min_loc]);CHKERRQ(ierr);
          constraints_index[n++] = row_cmat_indices[min_loc];
        }
        ierr = MatRestoreRow(pcbddc->ConstraintMatrix,i,&size_of_constraint,(const PetscInt**)&row_cmat_indices,NULL);CHKERRQ(ierr);
      }
      ierr = PetscBTDestroy(&touched);CHKERRQ(ierr);
      ierr = PetscFree(row_cmat_global_indices);CHKERRQ(ierr);
    }
  }
  *n_constraints = n;
  if (constraints_idx) *constraints_idx = constraints_index;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSubsetNumbering"
PetscErrorCode PCBDDCSubsetNumbering(MPI_Comm comm,ISLocalToGlobalMapping l2gmap, PetscInt n_local_dofs, PetscInt local_dofs[], PetscInt local_dofs_mult[], PetscInt* n_global_subset, PetscInt* global_numbering_subset[])
{
  Vec            local_vec,global_vec;
  IS             seqis,paris;
  VecScatter     scatter_ctx;
  PetscScalar    *array;
  PetscInt       *temp_global_dofs;
  PetscScalar    globalsum;
  PetscInt       i,j,s;
  PetscInt       nlocals,first_index,old_index,max_local,max_global;
  PetscMPIInt    rank_prec_comm,size_prec_comm;
  PetscInt       *dof_sizes,*dof_displs;
  PetscBool      first_found;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* mpi buffers */
  ierr = MPI_Comm_size(comm,&size_prec_comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank_prec_comm);CHKERRQ(ierr);
  j = ( !rank_prec_comm ? size_prec_comm : 0);
  ierr = PetscMalloc2(j,&dof_sizes,j,&dof_displs);CHKERRQ(ierr);
  /* get maximum size of subset */
  ierr = PetscMalloc1(n_local_dofs,&temp_global_dofs);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingApply(l2gmap,n_local_dofs,local_dofs,temp_global_dofs);CHKERRQ(ierr);
  max_local = 0;
  for (i=0;i<n_local_dofs;i++) {
    if (max_local < temp_global_dofs[i] ) {
      max_local = temp_global_dofs[i];
    }
  }
  ierr = MPI_Allreduce(&max_local,&max_global,1,MPIU_INT,MPI_MAX,comm);CHKERRQ(ierr);
  max_global++;
  max_local = 0;
  for (i=0;i<n_local_dofs;i++) {
    if (max_local < local_dofs[i] ) {
      max_local = local_dofs[i];
    }
  }
  max_local++;
  /* allocate workspace */
  ierr = VecCreate(PETSC_COMM_SELF,&local_vec);CHKERRQ(ierr);
  ierr = VecSetSizes(local_vec,PETSC_DECIDE,max_local);CHKERRQ(ierr);
  ierr = VecSetType(local_vec,VECSEQ);CHKERRQ(ierr);
  ierr = VecCreate(comm,&global_vec);CHKERRQ(ierr);
  ierr = VecSetSizes(global_vec,PETSC_DECIDE,max_global);CHKERRQ(ierr);
  ierr = VecSetType(global_vec,VECMPI);CHKERRQ(ierr);
  /* create scatter */
  ierr = ISCreateGeneral(PETSC_COMM_SELF,n_local_dofs,local_dofs,PETSC_COPY_VALUES,&seqis);CHKERRQ(ierr);
  ierr = ISCreateGeneral(comm,n_local_dofs,temp_global_dofs,PETSC_COPY_VALUES,&paris);CHKERRQ(ierr);
  ierr = VecScatterCreate(local_vec,seqis,global_vec,paris,&scatter_ctx);CHKERRQ(ierr);
  ierr = ISDestroy(&seqis);CHKERRQ(ierr);
  ierr = ISDestroy(&paris);CHKERRQ(ierr);
  /* init array */
  ierr = VecSet(global_vec,0.0);CHKERRQ(ierr);
  ierr = VecSet(local_vec,0.0);CHKERRQ(ierr);
  ierr = VecGetArray(local_vec,&array);CHKERRQ(ierr);
  if (local_dofs_mult) {
    for (i=0;i<n_local_dofs;i++) {
      array[local_dofs[i]]=(PetscScalar)local_dofs_mult[i];
    }
  } else {
    for (i=0;i<n_local_dofs;i++) {
      array[local_dofs[i]]=1.0;
    }
  }
  ierr = VecRestoreArray(local_vec,&array);CHKERRQ(ierr);
  /* scatter into global vec and get total number of global dofs */
  ierr = VecScatterBegin(scatter_ctx,local_vec,global_vec,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(scatter_ctx,local_vec,global_vec,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecSum(global_vec,&globalsum);CHKERRQ(ierr);
  *n_global_subset = (PetscInt)PetscRealPart(globalsum);
  /* Fill global_vec with cumulative function for global numbering */
  ierr = VecGetArray(global_vec,&array);CHKERRQ(ierr);
  ierr = VecGetLocalSize(global_vec,&s);CHKERRQ(ierr);
  nlocals = 0;
  first_index = -1;
  first_found = PETSC_FALSE;
  for (i=0;i<s;i++) {
    if (!first_found && PetscRealPart(array[i]) > 0.1) {
      first_found = PETSC_TRUE;
      first_index = i;
    }
    nlocals += (PetscInt)PetscRealPart(array[i]);
  }
  ierr = MPI_Gather(&nlocals,1,MPIU_INT,dof_sizes,1,MPIU_INT,0,comm);CHKERRQ(ierr);
  if (!rank_prec_comm) {
    dof_displs[0]=0;
    for (i=1;i<size_prec_comm;i++) {
      dof_displs[i] = dof_displs[i-1]+dof_sizes[i-1];
    }
  }
  ierr = MPI_Scatter(dof_displs,1,MPIU_INT,&nlocals,1,MPIU_INT,0,comm);CHKERRQ(ierr);
  if (first_found) {
    array[first_index] += (PetscScalar)nlocals;
    old_index = first_index;
    for (i=first_index+1;i<s;i++) {
      if (PetscRealPart(array[i]) > 0.1) {
        array[i] += array[old_index];
        old_index = i;
      }
    }
  }
  ierr = VecRestoreArray(global_vec,&array);CHKERRQ(ierr);
  ierr = VecSet(local_vec,0.0);CHKERRQ(ierr);
  ierr = VecScatterBegin(scatter_ctx,global_vec,local_vec,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  ierr = VecScatterEnd(scatter_ctx,global_vec,local_vec,INSERT_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  /* get global ordering of local dofs */
  ierr = VecGetArrayRead(local_vec,(const PetscScalar**)&array);CHKERRQ(ierr);
  if (local_dofs_mult) {
    for (i=0;i<n_local_dofs;i++) {
      temp_global_dofs[i] = (PetscInt)PetscRealPart(array[local_dofs[i]])-local_dofs_mult[i];
    }
  } else {
    for (i=0;i<n_local_dofs;i++) {
      temp_global_dofs[i] = (PetscInt)PetscRealPart(array[local_dofs[i]])-1;
    }
  }
  ierr = VecRestoreArrayRead(local_vec,(const PetscScalar**)&array);CHKERRQ(ierr);
  /* free workspace */
  ierr = VecScatterDestroy(&scatter_ctx);CHKERRQ(ierr);
  ierr = VecDestroy(&local_vec);CHKERRQ(ierr);
  ierr = VecDestroy(&global_vec);CHKERRQ(ierr);
  ierr = PetscFree2(dof_sizes,dof_displs);CHKERRQ(ierr);
  /* return pointer to global ordering of local dofs */
  *global_numbering_subset = temp_global_dofs;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCOrthonormalizeVecs"
PetscErrorCode PCBDDCOrthonormalizeVecs(PetscInt n, Vec vecs[])
{
  PetscInt       i,j;
  PetscScalar    *alphas;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* this implements stabilized Gram-Schmidt */
  ierr = PetscMalloc1(n,&alphas);CHKERRQ(ierr);
  for (i=0;i<n;i++) {
    ierr = VecNormalize(vecs[i],NULL);CHKERRQ(ierr);
    if (i<n) { ierr = VecMDot(vecs[i],n-i-1,&vecs[i+1],&alphas[i+1]);CHKERRQ(ierr); }
    for (j=i+1;j<n;j++) { ierr = VecAXPY(vecs[j],PetscConj(-alphas[j]),vecs[i]);CHKERRQ(ierr); }
  }
  ierr = PetscFree(alphas);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISGetSubassemblingPattern"
PetscErrorCode MatISGetSubassemblingPattern(Mat mat, PetscInt n_subdomains, PetscBool contiguous, IS* is_sends)
{
  Mat             subdomain_adj;
  IS              new_ranks,ranks_send_to;
  MatPartitioning partitioner;
  Mat_IS          *matis;
  PetscInt        n_neighs,*neighs,*n_shared,**shared;
  PetscInt        prank;
  PetscMPIInt     size,rank,color;
  PetscInt        *xadj,*adjncy,*oldranks;
  PetscInt        *adjncy_wgt,*v_wgt,*is_indices,*ranks_send_to_idx;
  PetscInt        i,local_size,threshold=0;
  PetscErrorCode  ierr;
  PetscBool       use_vwgt=PETSC_FALSE,use_square=PETSC_FALSE;
  PetscSubcomm    subcomm;

  PetscFunctionBegin;
  ierr = PetscOptionsGetBool(NULL,"-matis_partitioning_use_square",&use_square,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,"-matis_partitioning_use_vwgt",&use_vwgt,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(NULL,"-matis_partitioning_threshold",&threshold,NULL);CHKERRQ(ierr);

  /* Get info on mapping */
  matis = (Mat_IS*)(mat->data);
  ierr = ISLocalToGlobalMappingGetSize(matis->mapping,&local_size);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingGetInfo(matis->mapping,&n_neighs,&neighs,&n_shared,&shared);CHKERRQ(ierr);

  /* build local CSR graph of subdomains' connectivity */
  ierr = PetscMalloc1(2,&xadj);CHKERRQ(ierr);
  xadj[0] = 0;
  xadj[1] = PetscMax(n_neighs-1,0);
  ierr = PetscMalloc1(xadj[1],&adjncy);CHKERRQ(ierr);
  ierr = PetscMalloc1(xadj[1],&adjncy_wgt);CHKERRQ(ierr);

  if (threshold) {
    PetscInt xadj_count = 0;
    for (i=1;i<n_neighs;i++) {
      if (n_shared[i] > threshold) {
        adjncy[xadj_count] = neighs[i];
        adjncy_wgt[xadj_count] = n_shared[i];
        xadj_count++;
      }
    }
    xadj[1] = xadj_count;
  } else {
    if (xadj[1]) {
      ierr = PetscMemcpy(adjncy,&neighs[1],xadj[1]*sizeof(*adjncy));CHKERRQ(ierr);
      ierr = PetscMemcpy(adjncy_wgt,&n_shared[1],xadj[1]*sizeof(*adjncy_wgt));CHKERRQ(ierr);
    }
  }
  ierr = ISLocalToGlobalMappingRestoreInfo(matis->mapping,&n_neighs,&neighs,&n_shared,&shared);CHKERRQ(ierr);
  if (use_square) {
    for (i=0;i<xadj[1];i++) {
      adjncy_wgt[i] = adjncy_wgt[i]*adjncy_wgt[i];
    }
  }
  ierr = PetscSortIntWithArray(xadj[1],adjncy,adjncy_wgt);CHKERRQ(ierr);

  ierr = PetscMalloc1(1,&ranks_send_to_idx);CHKERRQ(ierr);

  /*
    Restrict work on active processes only.
  */
  ierr = PetscSubcommCreate(PetscObjectComm((PetscObject)mat),&subcomm);CHKERRQ(ierr);
  ierr = PetscSubcommSetNumber(subcomm,2);CHKERRQ(ierr); /* 2 groups, active process and not active processes */
  ierr = MPI_Comm_rank(PetscObjectComm((PetscObject)mat),&rank);CHKERRQ(ierr);
  ierr = PetscMPIIntCast(!local_size,&color);CHKERRQ(ierr);
  ierr = PetscSubcommSetTypeGeneral(subcomm,color,rank);CHKERRQ(ierr);
  if (color) {
    ierr = PetscFree(xadj);CHKERRQ(ierr);
    ierr = PetscFree(adjncy);CHKERRQ(ierr);
    ierr = PetscFree(adjncy_wgt);CHKERRQ(ierr);
  } else {
    PetscInt coarsening_ratio;
    ierr = MPI_Comm_size(PetscSubcommChild(subcomm),&size);CHKERRQ(ierr);
    ierr = PetscMalloc1(size,&oldranks);CHKERRQ(ierr);
    prank = rank;
    ierr = MPI_Allgather(&prank,1,MPIU_INT,oldranks,1,MPIU_INT,PetscSubcommChild(subcomm));CHKERRQ(ierr);
    /*
    for (i=0;i<size;i++) {
      PetscPrintf(subcomm->comm,"oldranks[%d] = %d\n",i,oldranks[i]);
    }
    */
    for (i=0;i<xadj[1];i++) {
      ierr = PetscFindInt(adjncy[i],size,oldranks,&adjncy[i]);CHKERRQ(ierr);
    }
    ierr = PetscSortIntWithArray(xadj[1],adjncy,adjncy_wgt);CHKERRQ(ierr);
    ierr = MatCreateMPIAdj(PetscSubcommChild(subcomm),1,(PetscInt)size,xadj,adjncy,adjncy_wgt,&subdomain_adj);CHKERRQ(ierr);
    /* ierr = MatView(subdomain_adj,0);CHKERRQ(ierr); */

    /* Partition */
    ierr = MatPartitioningCreate(PetscSubcommChild(subcomm),&partitioner);CHKERRQ(ierr);
    ierr = MatPartitioningSetAdjacency(partitioner,subdomain_adj);CHKERRQ(ierr);
    if (use_vwgt) {
      ierr = PetscMalloc1(1,&v_wgt);CHKERRQ(ierr);
      v_wgt[0] = local_size;
      ierr = MatPartitioningSetVertexWeights(partitioner,v_wgt);CHKERRQ(ierr);
    }
    n_subdomains = PetscMin((PetscInt)size,n_subdomains);
    coarsening_ratio = size/n_subdomains;
    ierr = MatPartitioningSetNParts(partitioner,n_subdomains);CHKERRQ(ierr);
    ierr = MatPartitioningSetFromOptions(partitioner);CHKERRQ(ierr);
    ierr = MatPartitioningApply(partitioner,&new_ranks);CHKERRQ(ierr);
    /* ierr = MatPartitioningView(partitioner,0);CHKERRQ(ierr); */

    ierr = ISGetIndices(new_ranks,(const PetscInt**)&is_indices);CHKERRQ(ierr);
    if (contiguous) {
      ranks_send_to_idx[0] = oldranks[is_indices[0]]; /* contiguos set of processes */
    } else {
      ranks_send_to_idx[0] = coarsening_ratio*oldranks[is_indices[0]]; /* scattered set of processes */
    }
    ierr = ISRestoreIndices(new_ranks,(const PetscInt**)&is_indices);CHKERRQ(ierr);
    /* clean up */
    ierr = PetscFree(oldranks);CHKERRQ(ierr);
    ierr = ISDestroy(&new_ranks);CHKERRQ(ierr);
    ierr = MatDestroy(&subdomain_adj);CHKERRQ(ierr);
    ierr = MatPartitioningDestroy(&partitioner);CHKERRQ(ierr);
  }
  ierr = PetscSubcommDestroy(&subcomm);CHKERRQ(ierr);

  /* assemble parallel IS for sends */
  i = 1;
  if (color) i=0;
  ierr = ISCreateGeneral(PetscObjectComm((PetscObject)mat),i,ranks_send_to_idx,PETSC_OWN_POINTER,&ranks_send_to);CHKERRQ(ierr);

  /* get back IS */
  *is_sends = ranks_send_to;
  PetscFunctionReturn(0);
}

typedef enum {MATDENSE_PRIVATE=0,MATAIJ_PRIVATE,MATBAIJ_PRIVATE,MATSBAIJ_PRIVATE}MatTypePrivate;

#undef __FUNCT__
#define __FUNCT__ "MatISSubassemble"
PetscErrorCode MatISSubassemble(Mat mat, IS is_sends, PetscInt n_subdomains, PetscBool restrict_comm, MatReuse reuse, Mat *mat_n, PetscInt nis, IS isarray[])
{
  Mat                    local_mat;
  Mat_IS                 *matis;
  IS                     is_sends_internal;
  PetscInt               rows,cols,new_local_rows;
  PetscInt               i,bs,buf_size_idxs,buf_size_idxs_is,buf_size_vals;
  PetscBool              ismatis,isdense,newisdense,destroy_mat;
  ISLocalToGlobalMapping l2gmap;
  PetscInt*              l2gmap_indices;
  const PetscInt*        is_indices;
  MatType                new_local_type;
  /* buffers */
  PetscInt               *ptr_idxs,*send_buffer_idxs,*recv_buffer_idxs;
  PetscInt               *ptr_idxs_is,*send_buffer_idxs_is,*recv_buffer_idxs_is;
  PetscInt               *recv_buffer_idxs_local;
  PetscScalar            *ptr_vals,*send_buffer_vals,*recv_buffer_vals;
  /* MPI */
  MPI_Comm               comm,comm_n;
  PetscSubcomm           subcomm;
  PetscMPIInt            n_sends,n_recvs,commsize;
  PetscMPIInt            *iflags,*ilengths_idxs,*ilengths_vals,*ilengths_idxs_is;
  PetscMPIInt            *onodes,*onodes_is,*olengths_idxs,*olengths_idxs_is,*olengths_vals;
  PetscMPIInt            len,tag_idxs,tag_idxs_is,tag_vals,source_dest;
  MPI_Request            *send_req_idxs,*send_req_idxs_is,*send_req_vals;
  MPI_Request            *recv_req_idxs,*recv_req_idxs_is,*recv_req_vals;
  PetscErrorCode         ierr;

  PetscFunctionBegin;
  /* TODO: add missing checks */
  PetscValidLogicalCollectiveInt(mat,n_subdomains,3);
  PetscValidLogicalCollectiveBool(mat,restrict_comm,4);
  PetscValidLogicalCollectiveEnum(mat,reuse,5);
  PetscValidLogicalCollectiveInt(mat,nis,7);
  ierr = PetscObjectTypeCompare((PetscObject)mat,MATIS,&ismatis);CHKERRQ(ierr);
  if (!ismatis) SETERRQ1(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot use %s on a matrix object which is not of type MATIS",__FUNCT__);
  ierr = MatISGetLocalMat(mat,&local_mat);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)local_mat,MATSEQDENSE,&isdense);CHKERRQ(ierr);
  if (!isdense) SETERRQ(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Currently cannot subassemble MATIS when local matrix type is not of type SEQDENSE");
  ierr = MatGetSize(local_mat,&rows,&cols);CHKERRQ(ierr);
  if (rows != cols) SETERRQ(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Local MATIS matrices should be square");
  if (reuse == MAT_REUSE_MATRIX && *mat_n) {
    PetscInt mrows,mcols,mnrows,mncols;
    ierr = PetscObjectTypeCompare((PetscObject)*mat_n,MATIS,&ismatis);CHKERRQ(ierr);
    if (!ismatis) SETERRQ(PetscObjectComm((PetscObject)*mat_n),PETSC_ERR_SUP,"Cannot reuse a matrix which is not of type MATIS");
    ierr = MatGetSize(mat,&mrows,&mcols);CHKERRQ(ierr);
    ierr = MatGetSize(*mat_n,&mnrows,&mncols);CHKERRQ(ierr);
    if (mrows != mnrows) SETERRQ2(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse matrix! Wrong number of rows %D != %D",mrows,mnrows);
    if (mcols != mncols) SETERRQ2(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse matrix! Wrong number of cols %D != %D",mcols,mncols);
  }
  ierr = MatGetBlockSize(local_mat,&bs);CHKERRQ(ierr);
  PetscValidLogicalCollectiveInt(mat,bs,0);
  /* prepare IS for sending if not provided */
  if (!is_sends) {
    PetscBool pcontig = PETSC_TRUE;
    if (!n_subdomains) SETERRQ(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"You should specify either an IS or a target number of subdomains");
    ierr = MatISGetSubassemblingPattern(mat,n_subdomains,pcontig,&is_sends_internal);CHKERRQ(ierr);
  } else {
    ierr = PetscObjectReference((PetscObject)is_sends);CHKERRQ(ierr);
    is_sends_internal = is_sends;
  }

  /* get pointer of MATIS data */
  matis = (Mat_IS*)mat->data;

  /* get comm */
  ierr = PetscObjectGetComm((PetscObject)mat,&comm);CHKERRQ(ierr);

  /* compute number of sends */
  ierr = ISGetLocalSize(is_sends_internal,&i);CHKERRQ(ierr);
  ierr = PetscMPIIntCast(i,&n_sends);CHKERRQ(ierr);

  /* compute number of receives */
  ierr = MPI_Comm_size(comm,&commsize);CHKERRQ(ierr);
  ierr = PetscMalloc1(commsize,&iflags);CHKERRQ(ierr);
  ierr = PetscMemzero(iflags,commsize*sizeof(*iflags));CHKERRQ(ierr);
  ierr = ISGetIndices(is_sends_internal,&is_indices);CHKERRQ(ierr);
  for (i=0;i<n_sends;i++) iflags[is_indices[i]] = 1;
  ierr = PetscGatherNumberOfMessages(comm,iflags,NULL,&n_recvs);CHKERRQ(ierr);
  ierr = PetscFree(iflags);CHKERRQ(ierr);

  /* restrict comm if requested */
  subcomm = 0;
  destroy_mat = PETSC_FALSE;
  if (restrict_comm) {
    PetscMPIInt color,subcommsize;

    color = 0;
    if (!n_recvs) color = 1; /* processes not receiving anything will not partecipate in new comm */
    ierr = MPI_Allreduce(&color,&subcommsize,1,MPI_INT,MPI_SUM,comm);CHKERRQ(ierr);
    subcommsize = commsize - subcommsize;
    /* check if reuse has been requested */
    if (reuse == MAT_REUSE_MATRIX) {
      if (*mat_n) {
        PetscMPIInt subcommsize2;
        ierr = MPI_Comm_size(PetscObjectComm((PetscObject)*mat_n),&subcommsize2);CHKERRQ(ierr);
        if (subcommsize != subcommsize2) SETERRQ2(PetscObjectComm((PetscObject)*mat_n),PETSC_ERR_PLIB,"Cannot reuse matrix! wrong subcomm size %d != %d",subcommsize,subcommsize2);
        comm_n = PetscObjectComm((PetscObject)*mat_n);
      } else {
        comm_n = PETSC_COMM_SELF;
      }
    } else { /* MAT_INITIAL_MATRIX */
      PetscMPIInt rank;

      ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
      ierr = PetscSubcommCreate(comm,&subcomm);CHKERRQ(ierr);
      ierr = PetscSubcommSetNumber(subcomm,2);CHKERRQ(ierr);
      ierr = PetscSubcommSetTypeGeneral(subcomm,color,rank);CHKERRQ(ierr);
      comm_n = PetscSubcommChild(subcomm);
    }
    /* flag to destroy *mat_n if not significative */
    if (color) destroy_mat = PETSC_TRUE;
  } else {
    comm_n = comm;
  }

  /* prepare send/receive buffers */
  ierr = PetscMalloc1(commsize,&ilengths_idxs);CHKERRQ(ierr);
  ierr = PetscMemzero(ilengths_idxs,commsize*sizeof(*ilengths_idxs));CHKERRQ(ierr);
  ierr = PetscMalloc1(commsize,&ilengths_vals);CHKERRQ(ierr);
  ierr = PetscMemzero(ilengths_vals,commsize*sizeof(*ilengths_vals));CHKERRQ(ierr);
  if (nis) {
    ierr = PetscCalloc1(commsize,&ilengths_idxs_is);CHKERRQ(ierr);
  }

  /* Get data from local matrices */
  if (!isdense) {
    SETERRQ(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Subassembling of AIJ local matrices not yet implemented");
    /* TODO: See below some guidelines on how to prepare the local buffers */
    /*
       send_buffer_vals should contain the raw values of the local matrix
       send_buffer_idxs should contain:
       - MatType_PRIVATE type
       - PetscInt        size_of_l2gmap
       - PetscInt        global_row_indices[size_of_l2gmap]
       - PetscInt        all_other_info_which_is_needed_to_compute_preallocation_and_set_values
    */
  } else {
    ierr = MatDenseGetArray(local_mat,&send_buffer_vals);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingGetSize(matis->mapping,&i);CHKERRQ(ierr);
    ierr = PetscMalloc1(i+2,&send_buffer_idxs);CHKERRQ(ierr);
    send_buffer_idxs[0] = (PetscInt)MATDENSE_PRIVATE;
    send_buffer_idxs[1] = i;
    ierr = ISLocalToGlobalMappingGetIndices(matis->mapping,(const PetscInt**)&ptr_idxs);CHKERRQ(ierr);
    ierr = PetscMemcpy(&send_buffer_idxs[2],ptr_idxs,i*sizeof(PetscInt));CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingRestoreIndices(matis->mapping,(const PetscInt**)&ptr_idxs);CHKERRQ(ierr);
    ierr = PetscMPIIntCast(i,&len);CHKERRQ(ierr);
    for (i=0;i<n_sends;i++) {
      ilengths_vals[is_indices[i]] = len*len;
      ilengths_idxs[is_indices[i]] = len+2;
    }
  }
  ierr = PetscGatherMessageLengths2(comm,n_sends,n_recvs,ilengths_idxs,ilengths_vals,&onodes,&olengths_idxs,&olengths_vals);CHKERRQ(ierr);
  /* additional is (if any) */
  if (nis) {
    PetscMPIInt psum;
    PetscInt j;
    for (j=0,psum=0;j<nis;j++) {
      PetscInt plen;
      ierr = ISGetLocalSize(isarray[j],&plen);CHKERRQ(ierr);
      ierr = PetscMPIIntCast(plen,&len);CHKERRQ(ierr);
      psum += len+1; /* indices + lenght */
    }
    ierr = PetscMalloc1(psum,&send_buffer_idxs_is);CHKERRQ(ierr);
    for (j=0,psum=0;j<nis;j++) {
      PetscInt plen;
      const PetscInt *is_array_idxs;
      ierr = ISGetLocalSize(isarray[j],&plen);CHKERRQ(ierr);
      send_buffer_idxs_is[psum] = plen;
      ierr = ISGetIndices(isarray[j],&is_array_idxs);CHKERRQ(ierr);
      ierr = PetscMemcpy(&send_buffer_idxs_is[psum+1],is_array_idxs,plen*sizeof(PetscInt));CHKERRQ(ierr);
      ierr = ISRestoreIndices(isarray[j],&is_array_idxs);CHKERRQ(ierr);
      psum += plen+1; /* indices + lenght */
    }
    for (i=0;i<n_sends;i++) {
      ilengths_idxs_is[is_indices[i]] = psum;
    }
    ierr = PetscGatherMessageLengths(comm,n_sends,n_recvs,ilengths_idxs_is,&onodes_is,&olengths_idxs_is);CHKERRQ(ierr);
  }

  buf_size_idxs = 0;
  buf_size_vals = 0;
  buf_size_idxs_is = 0;
  for (i=0;i<n_recvs;i++) {
    buf_size_idxs += (PetscInt)olengths_idxs[i];
    buf_size_vals += (PetscInt)olengths_vals[i];
    if (nis) buf_size_idxs_is += (PetscInt)olengths_idxs_is[i];
  }
  ierr = PetscMalloc1(buf_size_idxs,&recv_buffer_idxs);CHKERRQ(ierr);
  ierr = PetscMalloc1(buf_size_vals,&recv_buffer_vals);CHKERRQ(ierr);
  ierr = PetscMalloc1(buf_size_idxs_is,&recv_buffer_idxs_is);CHKERRQ(ierr);

  /* get new tags for clean communications */
  ierr = PetscObjectGetNewTag((PetscObject)mat,&tag_idxs);CHKERRQ(ierr);
  ierr = PetscObjectGetNewTag((PetscObject)mat,&tag_vals);CHKERRQ(ierr);
  ierr = PetscObjectGetNewTag((PetscObject)mat,&tag_idxs_is);CHKERRQ(ierr);

  /* allocate for requests */
  ierr = PetscMalloc1(n_sends,&send_req_idxs);CHKERRQ(ierr);
  ierr = PetscMalloc1(n_sends,&send_req_vals);CHKERRQ(ierr);
  ierr = PetscMalloc1(n_sends,&send_req_idxs_is);CHKERRQ(ierr);
  ierr = PetscMalloc1(n_recvs,&recv_req_idxs);CHKERRQ(ierr);
  ierr = PetscMalloc1(n_recvs,&recv_req_vals);CHKERRQ(ierr);
  ierr = PetscMalloc1(n_recvs,&recv_req_idxs_is);CHKERRQ(ierr);

  /* communications */
  ptr_idxs = recv_buffer_idxs;
  ptr_vals = recv_buffer_vals;
  ptr_idxs_is = recv_buffer_idxs_is;
  for (i=0;i<n_recvs;i++) {
    source_dest = onodes[i];
    ierr = MPI_Irecv(ptr_idxs,olengths_idxs[i],MPIU_INT,source_dest,tag_idxs,comm,&recv_req_idxs[i]);CHKERRQ(ierr);
    ierr = MPI_Irecv(ptr_vals,olengths_vals[i],MPIU_SCALAR,source_dest,tag_vals,comm,&recv_req_vals[i]);CHKERRQ(ierr);
    ptr_idxs += olengths_idxs[i];
    ptr_vals += olengths_vals[i];
    if (nis) {
      ierr = MPI_Irecv(ptr_idxs_is,olengths_idxs_is[i],MPIU_INT,source_dest,tag_idxs_is,comm,&recv_req_idxs_is[i]);CHKERRQ(ierr);
      ptr_idxs_is += olengths_idxs_is[i];
    }
  }
  for (i=0;i<n_sends;i++) {
    ierr = PetscMPIIntCast(is_indices[i],&source_dest);CHKERRQ(ierr);
    ierr = MPI_Isend(send_buffer_idxs,ilengths_idxs[source_dest],MPIU_INT,source_dest,tag_idxs,comm,&send_req_idxs[i]);CHKERRQ(ierr);
    ierr = MPI_Isend(send_buffer_vals,ilengths_vals[source_dest],MPIU_SCALAR,source_dest,tag_vals,comm,&send_req_vals[i]);CHKERRQ(ierr);
    if (nis) {
      ierr = MPI_Isend(send_buffer_idxs_is,ilengths_idxs_is[source_dest],MPIU_INT,source_dest,tag_idxs_is,comm,&send_req_idxs_is[i]);CHKERRQ(ierr);
    }
  }
  ierr = ISRestoreIndices(is_sends_internal,&is_indices);CHKERRQ(ierr);
  ierr = ISDestroy(&is_sends_internal);CHKERRQ(ierr);

  /* assemble new l2g map */
  ierr = MPI_Waitall(n_recvs,recv_req_idxs,MPI_STATUSES_IGNORE);CHKERRQ(ierr);
  ptr_idxs = recv_buffer_idxs;
  new_local_rows = 0;
  for (i=0;i<n_recvs;i++) {
    new_local_rows += *(ptr_idxs+1); /* second element is the local size of the l2gmap */
    ptr_idxs += olengths_idxs[i];
  }
  ierr = PetscMalloc1(new_local_rows,&l2gmap_indices);CHKERRQ(ierr);
  ptr_idxs = recv_buffer_idxs;
  new_local_rows = 0;
  for (i=0;i<n_recvs;i++) {
    ierr = PetscMemcpy(&l2gmap_indices[new_local_rows],ptr_idxs+2,(*(ptr_idxs+1))*sizeof(PetscInt));CHKERRQ(ierr);
    new_local_rows += *(ptr_idxs+1); /* second element is the local size of the l2gmap */
    ptr_idxs += olengths_idxs[i];
  }
  ierr = PetscSortRemoveDupsInt(&new_local_rows,l2gmap_indices);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingCreate(comm_n,1,new_local_rows,l2gmap_indices,PETSC_COPY_VALUES,&l2gmap);CHKERRQ(ierr);
  ierr = PetscFree(l2gmap_indices);CHKERRQ(ierr);

  /* infer new local matrix type from received local matrices type */
  /* currently if all local matrices are of type X, then the resulting matrix will be of type X, except for the dense case */
  /* it also assumes that if the block size is set, than it is the same among all local matrices (see checks at the beginning of the function) */
  if (n_recvs) {
    MatTypePrivate new_local_type_private = (MatTypePrivate)send_buffer_idxs[0];
    ptr_idxs = recv_buffer_idxs;
    for (i=0;i<n_recvs;i++) {
      if ((PetscInt)new_local_type_private != *ptr_idxs) {
        new_local_type_private = MATAIJ_PRIVATE;
        break;
      }
      ptr_idxs += olengths_idxs[i];
    }
    switch (new_local_type_private) {
      case MATDENSE_PRIVATE:
        if (n_recvs>1) { /* subassembling of dense matrices does not give a dense matrix! */
          new_local_type = MATSEQAIJ;
          bs = 1;
        } else { /* if I receive only 1 dense matrix */
          new_local_type = MATSEQDENSE;
          bs = 1;
        }
        break;
      case MATAIJ_PRIVATE:
        new_local_type = MATSEQAIJ;
        bs = 1;
        break;
      case MATBAIJ_PRIVATE:
        new_local_type = MATSEQBAIJ;
        break;
      case MATSBAIJ_PRIVATE:
        new_local_type = MATSEQSBAIJ;
        break;
      default:
        SETERRQ2(comm,PETSC_ERR_SUP,"Unsupported private type %d in %s",new_local_type_private,__FUNCT__);
        break;
    }
  } else { /* by default, new_local_type is seqdense */
    new_local_type = MATSEQDENSE;
    bs = 1;
  }

  /* create MATIS object if needed */
  if (reuse == MAT_INITIAL_MATRIX) {
    ierr = MatGetSize(mat,&rows,&cols);CHKERRQ(ierr);
    ierr = MatCreateIS(comm_n,bs,PETSC_DECIDE,PETSC_DECIDE,rows,cols,l2gmap,mat_n);CHKERRQ(ierr);
  } else {
    /* it also destroys the local matrices */
    ierr = MatSetLocalToGlobalMapping(*mat_n,l2gmap,l2gmap);CHKERRQ(ierr);
  }
  ierr = MatISGetLocalMat(*mat_n,&local_mat);CHKERRQ(ierr);
  ierr = MatSetType(local_mat,new_local_type);CHKERRQ(ierr);

  ierr = MPI_Waitall(n_recvs,recv_req_vals,MPI_STATUSES_IGNORE);CHKERRQ(ierr);

  /* Global to local map of received indices */
  ierr = PetscMalloc1(buf_size_idxs,&recv_buffer_idxs_local);CHKERRQ(ierr); /* needed for values insertion */
  ierr = ISGlobalToLocalMappingApply(l2gmap,IS_GTOLM_MASK,buf_size_idxs,recv_buffer_idxs,&i,recv_buffer_idxs_local);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingDestroy(&l2gmap);CHKERRQ(ierr);

  /* restore attributes -> type of incoming data and its size */
  buf_size_idxs = 0;
  for (i=0;i<n_recvs;i++) {
    recv_buffer_idxs_local[buf_size_idxs] = recv_buffer_idxs[buf_size_idxs];
    recv_buffer_idxs_local[buf_size_idxs+1] = recv_buffer_idxs[buf_size_idxs+1];
    buf_size_idxs += (PetscInt)olengths_idxs[i];
  }
  ierr = PetscFree(recv_buffer_idxs);CHKERRQ(ierr);

  /* set preallocation */
  ierr = PetscObjectTypeCompare((PetscObject)local_mat,MATSEQDENSE,&newisdense);CHKERRQ(ierr);
  if (!newisdense) {
    PetscInt *new_local_nnz=0;

    ptr_vals = recv_buffer_vals;
    ptr_idxs = recv_buffer_idxs_local;
    if (n_recvs) {
      ierr = PetscCalloc1(new_local_rows,&new_local_nnz);CHKERRQ(ierr);
    }
    for (i=0;i<n_recvs;i++) {
      PetscInt j;
      if (*ptr_idxs == (PetscInt)MATDENSE_PRIVATE) { /* preallocation provided for dense case only */
        for (j=0;j<*(ptr_idxs+1);j++) {
          new_local_nnz[*(ptr_idxs+2+j)] += *(ptr_idxs+1);
        }
      } else {
        /* TODO */
      }
      ptr_idxs += olengths_idxs[i];
    }
    if (new_local_nnz) {
      for (i=0;i<new_local_rows;i++) new_local_nnz[i] = PetscMin(new_local_nnz[i],new_local_rows);
      ierr = MatSeqAIJSetPreallocation(local_mat,0,new_local_nnz);CHKERRQ(ierr);
      for (i=0;i<new_local_rows;i++) new_local_nnz[i] /= bs;
      ierr = MatSeqBAIJSetPreallocation(local_mat,bs,0,new_local_nnz);CHKERRQ(ierr);
      for (i=0;i<new_local_rows;i++) new_local_nnz[i] = PetscMax(new_local_nnz[i]-i,0);
      ierr = MatSeqSBAIJSetPreallocation(local_mat,bs,0,new_local_nnz);CHKERRQ(ierr);
    } else {
      ierr = MatSetUp(local_mat);CHKERRQ(ierr);
    }
    ierr = PetscFree(new_local_nnz);CHKERRQ(ierr);
  } else {
    ierr = MatSetUp(local_mat);CHKERRQ(ierr);
  }

  /* set values */
  ptr_vals = recv_buffer_vals;
  ptr_idxs = recv_buffer_idxs_local;
  for (i=0;i<n_recvs;i++) {
    if (*ptr_idxs == (PetscInt)MATDENSE_PRIVATE) { /* values insertion provided for dense case only */
      ierr = MatSetOption(local_mat,MAT_ROW_ORIENTED,PETSC_FALSE);CHKERRQ(ierr);
      ierr = MatSetValues(local_mat,*(ptr_idxs+1),ptr_idxs+2,*(ptr_idxs+1),ptr_idxs+2,ptr_vals,ADD_VALUES);CHKERRQ(ierr);
      ierr = MatAssemblyBegin(local_mat,MAT_FLUSH_ASSEMBLY);CHKERRQ(ierr);
      ierr = MatAssemblyEnd(local_mat,MAT_FLUSH_ASSEMBLY);CHKERRQ(ierr);
      ierr = MatSetOption(local_mat,MAT_ROW_ORIENTED,PETSC_TRUE);CHKERRQ(ierr);
    } else {
      /* TODO */
    }
    ptr_idxs += olengths_idxs[i];
    ptr_vals += olengths_vals[i];
  }
  ierr = MatAssemblyBegin(local_mat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(local_mat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyBegin(*mat_n,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(*mat_n,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = PetscFree(recv_buffer_vals);CHKERRQ(ierr);
  ierr = PetscFree(recv_buffer_idxs_local);CHKERRQ(ierr);

#if 0
  if (!restrict_comm) { /* check */
    Vec       lvec,rvec;
    PetscReal infty_error;

    ierr = MatCreateVecs(mat,&rvec,&lvec);CHKERRQ(ierr);
    ierr = VecSetRandom(rvec,NULL);CHKERRQ(ierr);
    ierr = MatMult(mat,rvec,lvec);CHKERRQ(ierr);
    ierr = VecScale(lvec,-1.0);CHKERRQ(ierr);
    ierr = MatMultAdd(*mat_n,rvec,lvec,lvec);CHKERRQ(ierr);
    ierr = VecNorm(lvec,NORM_INFINITY,&infty_error);CHKERRQ(ierr);
    ierr = PetscPrintf(PetscObjectComm((PetscObject)mat),"Infinity error subassembling %1.6e\n",infty_error);
    ierr = VecDestroy(&rvec);CHKERRQ(ierr);
    ierr = VecDestroy(&lvec);CHKERRQ(ierr);
  }
#endif

  /* assemble new additional is (if any) */
  if (nis) {
    PetscInt **temp_idxs,*count_is,j,psum;

    ierr = MPI_Waitall(n_recvs,recv_req_idxs_is,MPI_STATUSES_IGNORE);CHKERRQ(ierr);
    ierr = PetscCalloc1(nis,&count_is);CHKERRQ(ierr);
    ptr_idxs = recv_buffer_idxs_is;
    psum = 0;
    for (i=0;i<n_recvs;i++) {
      for (j=0;j<nis;j++) {
        PetscInt plen = *(ptr_idxs); /* first element is the local size of IS's indices */
        count_is[j] += plen; /* increment counting of buffer for j-th IS */
        psum += plen;
        ptr_idxs += plen+1; /* shift pointer to received data */
      }
    }
    ierr = PetscMalloc1(nis,&temp_idxs);CHKERRQ(ierr);
    ierr = PetscMalloc1(psum,&temp_idxs[0]);CHKERRQ(ierr);
    for (i=1;i<nis;i++) {
      temp_idxs[i] = temp_idxs[i-1]+count_is[i-1];
    }
    ierr = PetscMemzero(count_is,nis*sizeof(PetscInt));CHKERRQ(ierr);
    ptr_idxs = recv_buffer_idxs_is;
    for (i=0;i<n_recvs;i++) {
      for (j=0;j<nis;j++) {
        PetscInt plen = *(ptr_idxs); /* first element is the local size of IS's indices */
        ierr = PetscMemcpy(&temp_idxs[j][count_is[j]],ptr_idxs+1,plen*sizeof(PetscInt));CHKERRQ(ierr);
        count_is[j] += plen; /* increment starting point of buffer for j-th IS */
        ptr_idxs += plen+1; /* shift pointer to received data */
      }
    }
    for (i=0;i<nis;i++) {
      ierr = ISDestroy(&isarray[i]);CHKERRQ(ierr);
      ierr = PetscSortRemoveDupsInt(&count_is[i],temp_idxs[i]);CHKERRQ(ierr);CHKERRQ(ierr);
      ierr = ISCreateGeneral(comm_n,count_is[i],temp_idxs[i],PETSC_COPY_VALUES,&isarray[i]);CHKERRQ(ierr);
    }
    ierr = PetscFree(count_is);CHKERRQ(ierr);
    ierr = PetscFree(temp_idxs[0]);CHKERRQ(ierr);
    ierr = PetscFree(temp_idxs);CHKERRQ(ierr);
  }
  /* free workspace */
  ierr = PetscFree(recv_buffer_idxs_is);CHKERRQ(ierr);
  ierr = MPI_Waitall(n_sends,send_req_idxs,MPI_STATUSES_IGNORE);CHKERRQ(ierr);
  ierr = PetscFree(send_buffer_idxs);CHKERRQ(ierr);
  ierr = MPI_Waitall(n_sends,send_req_vals,MPI_STATUSES_IGNORE);CHKERRQ(ierr);
  if (isdense) {
    ierr = MatISGetLocalMat(mat,&local_mat);CHKERRQ(ierr);
    ierr = MatDenseRestoreArray(local_mat,&send_buffer_vals);CHKERRQ(ierr);
  } else {
    /* ierr = PetscFree(send_buffer_vals);CHKERRQ(ierr); */
  }
  if (nis) {
    ierr = MPI_Waitall(n_sends,send_req_idxs_is,MPI_STATUSES_IGNORE);CHKERRQ(ierr);
    ierr = PetscFree(send_buffer_idxs_is);CHKERRQ(ierr);
  }
  ierr = PetscFree(recv_req_idxs);CHKERRQ(ierr);
  ierr = PetscFree(recv_req_vals);CHKERRQ(ierr);
  ierr = PetscFree(recv_req_idxs_is);CHKERRQ(ierr);
  ierr = PetscFree(send_req_idxs);CHKERRQ(ierr);
  ierr = PetscFree(send_req_vals);CHKERRQ(ierr);
  ierr = PetscFree(send_req_idxs_is);CHKERRQ(ierr);
  ierr = PetscFree(ilengths_vals);CHKERRQ(ierr);
  ierr = PetscFree(ilengths_idxs);CHKERRQ(ierr);
  ierr = PetscFree(olengths_vals);CHKERRQ(ierr);
  ierr = PetscFree(olengths_idxs);CHKERRQ(ierr);
  ierr = PetscFree(onodes);CHKERRQ(ierr);
  if (nis) {
    ierr = PetscFree(ilengths_idxs_is);CHKERRQ(ierr);
    ierr = PetscFree(olengths_idxs_is);CHKERRQ(ierr);
    ierr = PetscFree(onodes_is);CHKERRQ(ierr);
  }
  ierr = PetscSubcommDestroy(&subcomm);CHKERRQ(ierr);
  if (destroy_mat) { /* destroy mat is true only if restrict comm is true and process will not partecipate */
    ierr = MatDestroy(mat_n);CHKERRQ(ierr);
    for (i=0;i<nis;i++) {
      ierr = ISDestroy(&isarray[i]);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

/* temporary hack into ksp private data structure */
#include <petsc/private/kspimpl.h>

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSetUpCoarseSolver"
PetscErrorCode PCBDDCSetUpCoarseSolver(PC pc,PetscScalar* coarse_submat_vals)
{
  PC_BDDC                *pcbddc = (PC_BDDC*)pc->data;
  PC_IS                  *pcis = (PC_IS*)pc->data;
  Mat                    coarse_mat,coarse_mat_is,coarse_submat_dense;
  MatNullSpace           CoarseNullSpace=NULL;
  ISLocalToGlobalMapping coarse_islg;
  IS                     coarse_is,*isarray;
  PetscInt               i,im_active=-1,active_procs=-1;
  PetscInt               nis,nisdofs,nisneu;
  PC                     pc_temp;
  PCType                 coarse_pc_type;
  KSPType                coarse_ksp_type;
  PetscBool              multilevel_requested,multilevel_allowed;
  PetscBool              isredundant,isbddc,isnn,coarse_reuse;
  Mat                    t_coarse_mat_is;
  PetscInt               void_procs,ncoarse_ml,ncoarse_ds,ncoarse;
  PetscMPIInt            all_procs;
  PetscBool              csin_ml,csin_ds,csin,csin_type_simple,redist;
  PetscBool              compute_vecs = PETSC_FALSE;
  PetscScalar            *array;
  PetscErrorCode         ierr;

  PetscFunctionBegin;
  /* Assign global numbering to coarse dofs */
  if (pcbddc->new_primal_space || pcbddc->coarse_size == -1) { /* a new primal space is present or it is the first initialization, so recompute global numbering */
    PetscInt ocoarse_size;
    compute_vecs = PETSC_TRUE;
    ocoarse_size = pcbddc->coarse_size;
    ierr = PetscFree(pcbddc->global_primal_indices);CHKERRQ(ierr);
    ierr = PCBDDCComputePrimalNumbering(pc,&pcbddc->coarse_size,&pcbddc->global_primal_indices);CHKERRQ(ierr);
    /* see if we can avoid some work */
    if (pcbddc->coarse_ksp) { /* coarse ksp has already been created */
      if (ocoarse_size != pcbddc->coarse_size) { /* ...but with different size, so reset it and set reuse flag to false */
        ierr = KSPReset(pcbddc->coarse_ksp);CHKERRQ(ierr);
        coarse_reuse = PETSC_FALSE;
      } else { /* we can safely reuse already computed coarse matrix */
        coarse_reuse = PETSC_TRUE;
      }
    } else { /* there's no coarse ksp, so we need to create the coarse matrix too */
      coarse_reuse = PETSC_FALSE;
    }
    /* reset any subassembling information */
    ierr = ISDestroy(&pcbddc->coarse_subassembling);CHKERRQ(ierr);
    ierr = ISDestroy(&pcbddc->coarse_subassembling_init);CHKERRQ(ierr);
  } else { /* primal space is unchanged, so we can reuse coarse matrix */
    coarse_reuse = PETSC_TRUE;
  }

  /* count "active" (i.e. with positive local size) and "void" processes */
  im_active = !!(pcis->n);
  ierr = MPI_Allreduce(&im_active,&active_procs,1,MPIU_INT,MPI_SUM,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);
  ierr = MPI_Comm_size(PetscObjectComm((PetscObject)pc),&all_procs);CHKERRQ(ierr);
  void_procs = all_procs-active_procs;
  csin_type_simple = PETSC_TRUE;
  redist = PETSC_FALSE;
  if (pcbddc->current_level && void_procs) {
    csin_ml = PETSC_TRUE;
    ncoarse_ml = void_procs;
    /* it has no sense to redistribute on a set of processors larger than the number of active processes */
    if (pcbddc->redistribute_coarse > 0 && pcbddc->redistribute_coarse < active_procs) {
      csin_ds = PETSC_TRUE;
      ncoarse_ds = pcbddc->redistribute_coarse;
      redist = PETSC_TRUE;
    } else {
      csin_ds = PETSC_TRUE;
      ncoarse_ds = active_procs;
      redist = PETSC_TRUE;
    }
  } else {
    csin_ml = PETSC_FALSE;
    ncoarse_ml = all_procs;
    if (void_procs) {
      csin_ds = PETSC_TRUE;
      ncoarse_ds = void_procs;
      csin_type_simple = PETSC_FALSE;
    } else {
      if (pcbddc->redistribute_coarse > 0 && pcbddc->redistribute_coarse < all_procs) {
        csin_ds = PETSC_TRUE;
        ncoarse_ds = pcbddc->redistribute_coarse;
        redist = PETSC_TRUE;
      } else {
        csin_ds = PETSC_FALSE;
        ncoarse_ds = all_procs;
      }
    }
  }

  /*
    test if we can go multilevel: three conditions must be satisfied:
    - we have not exceeded the number of levels requested
    - we can actually subassemble the active processes
    - we can find a suitable number of MPI processes where we can place the subassembled problem
  */
  multilevel_allowed = PETSC_FALSE;
  multilevel_requested = PETSC_FALSE;
  if (pcbddc->current_level < pcbddc->max_levels) {
    multilevel_requested = PETSC_TRUE;
    if (active_procs/pcbddc->coarsening_ratio < 2 || ncoarse_ml/pcbddc->coarsening_ratio < 2) {
      multilevel_allowed = PETSC_FALSE;
    } else {
      multilevel_allowed = PETSC_TRUE;
    }
  }
  /* determine number of process partecipating to coarse solver */
  if (multilevel_allowed) {
    ncoarse = ncoarse_ml;
    csin = csin_ml;
    redist = PETSC_FALSE;
  } else {
    ncoarse = ncoarse_ds;
    csin = csin_ds;
  }

  /* creates temporary l2gmap and IS for coarse indexes */
  ierr = ISCreateGeneral(PetscObjectComm((PetscObject)pc),pcbddc->local_primal_size,pcbddc->global_primal_indices,PETSC_COPY_VALUES,&coarse_is);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingCreateIS(coarse_is,&coarse_islg);CHKERRQ(ierr);

  /* creates temporary MATIS object for coarse matrix */
  ierr = MatCreateSeqDense(PETSC_COMM_SELF,pcbddc->local_primal_size,pcbddc->local_primal_size,NULL,&coarse_submat_dense);CHKERRQ(ierr);
  ierr = MatDenseGetArray(coarse_submat_dense,&array);CHKERRQ(ierr);
  ierr = PetscMemcpy(array,coarse_submat_vals,sizeof(*coarse_submat_vals)*pcbddc->local_primal_size*pcbddc->local_primal_size);CHKERRQ(ierr);
  ierr = MatDenseRestoreArray(coarse_submat_dense,&array);CHKERRQ(ierr);
#if 0
  {
    PetscViewer viewer;
    char filename[256];
    sprintf(filename,"local_coarse_mat%d.m",PetscGlobalRank);
    ierr = PetscViewerASCIIOpen(PETSC_COMM_SELF,filename,&viewer);CHKERRQ(ierr);
    ierr = PetscViewerSetFormat(viewer,PETSC_VIEWER_ASCII_MATLAB);CHKERRQ(ierr);
    ierr = MatView(coarse_submat_dense,viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
#endif
  ierr = MatCreateIS(PetscObjectComm((PetscObject)pc),1,PETSC_DECIDE,PETSC_DECIDE,pcbddc->coarse_size,pcbddc->coarse_size,coarse_islg,&t_coarse_mat_is);CHKERRQ(ierr);
  ierr = MatISSetLocalMat(t_coarse_mat_is,coarse_submat_dense);CHKERRQ(ierr);
  ierr = MatAssemblyBegin(t_coarse_mat_is,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(t_coarse_mat_is,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatDestroy(&coarse_submat_dense);CHKERRQ(ierr);

  /* compute dofs splitting and neumann boundaries for coarse dofs */
  if (multilevel_allowed && (pcbddc->n_ISForDofsLocal || pcbddc->NeumannBoundariesLocal) ) { /* protects from unneded computations */
    PetscInt               *tidxs,*tidxs2,nout,tsize,i;
    const PetscInt         *idxs;
    ISLocalToGlobalMapping tmap;

    /* create map between primal indices (in local representative ordering) and local primal numbering */
    ierr = ISLocalToGlobalMappingCreate(PETSC_COMM_SELF,1,pcbddc->local_primal_size,pcbddc->primal_indices_local_idxs,PETSC_COPY_VALUES,&tmap);CHKERRQ(ierr);
    /* allocate space for temporary storage */
    ierr = PetscMalloc1(pcbddc->local_primal_size,&tidxs);CHKERRQ(ierr);
    ierr = PetscMalloc1(pcbddc->local_primal_size,&tidxs2);CHKERRQ(ierr);
    /* allocate for IS array */
    nisdofs = pcbddc->n_ISForDofsLocal;
    nisneu = !!pcbddc->NeumannBoundariesLocal;
    nis = nisdofs + nisneu;
    ierr = PetscMalloc1(nis,&isarray);CHKERRQ(ierr);
    /* dofs splitting */
    for (i=0;i<nisdofs;i++) {
      /* ierr = ISView(pcbddc->ISForDofsLocal[i],0);CHKERRQ(ierr); */
      ierr = ISGetLocalSize(pcbddc->ISForDofsLocal[i],&tsize);CHKERRQ(ierr);
      ierr = ISGetIndices(pcbddc->ISForDofsLocal[i],&idxs);CHKERRQ(ierr);
      ierr = ISGlobalToLocalMappingApply(tmap,IS_GTOLM_DROP,tsize,idxs,&nout,tidxs);CHKERRQ(ierr);
      ierr = ISRestoreIndices(pcbddc->ISForDofsLocal[i],&idxs);CHKERRQ(ierr);
      ierr = ISLocalToGlobalMappingApply(coarse_islg,nout,tidxs,tidxs2);CHKERRQ(ierr);
      ierr = ISCreateGeneral(PetscObjectComm((PetscObject)pcbddc->ISForDofsLocal[i]),nout,tidxs2,PETSC_COPY_VALUES,&isarray[i]);CHKERRQ(ierr);
      /* ierr = ISView(isarray[i],0);CHKERRQ(ierr); */
    }
    /* neumann boundaries */
    if (pcbddc->NeumannBoundariesLocal) {
      /* ierr = ISView(pcbddc->NeumannBoundariesLocal,0);CHKERRQ(ierr); */
      ierr = ISGetLocalSize(pcbddc->NeumannBoundariesLocal,&tsize);CHKERRQ(ierr);
      ierr = ISGetIndices(pcbddc->NeumannBoundariesLocal,&idxs);CHKERRQ(ierr);
      ierr = ISGlobalToLocalMappingApply(tmap,IS_GTOLM_DROP,tsize,idxs,&nout,tidxs);CHKERRQ(ierr);
      ierr = ISRestoreIndices(pcbddc->NeumannBoundariesLocal,&idxs);CHKERRQ(ierr);
      ierr = ISLocalToGlobalMappingApply(coarse_islg,nout,tidxs,tidxs2);CHKERRQ(ierr);
      ierr = ISCreateGeneral(PetscObjectComm((PetscObject)pcbddc->NeumannBoundariesLocal),nout,tidxs2,PETSC_COPY_VALUES,&isarray[nisdofs]);CHKERRQ(ierr);
      /* ierr = ISView(isarray[nisdofs],0);CHKERRQ(ierr); */
    }
    /* free memory */
    ierr = PetscFree(tidxs);CHKERRQ(ierr);
    ierr = PetscFree(tidxs2);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingDestroy(&tmap);CHKERRQ(ierr);
  } else {
    nis = 0;
    nisdofs = 0;
    nisneu = 0;
    isarray = NULL;
  }
  /* destroy no longer needed map */
  ierr = ISLocalToGlobalMappingDestroy(&coarse_islg);CHKERRQ(ierr);

  /* restrict on coarse candidates (if needed) */
  coarse_mat_is = NULL;
  if (csin) {
    if (!pcbddc->coarse_subassembling_init ) { /* creates subassembling init pattern if not present */
      if (redist) {
        PetscMPIInt rank;
        PetscInt    spc,n_spc_p1,dest[1],destsize;

        ierr = MPI_Comm_rank(PetscObjectComm((PetscObject)pc),&rank);CHKERRQ(ierr);
        spc = active_procs/ncoarse;
        n_spc_p1 = active_procs%ncoarse;
        if (im_active) {
          destsize = 1;
          if (rank > n_spc_p1*(spc+1)-1) {
            dest[0] = n_spc_p1+(rank-(n_spc_p1*(spc+1)))/spc;
          } else {
            dest[0] = rank/(spc+1);
          }
        } else {
          destsize = 0;
        }
        ierr = ISCreateGeneral(PetscObjectComm((PetscObject)pc),destsize,dest,PETSC_COPY_VALUES,&pcbddc->coarse_subassembling_init);CHKERRQ(ierr);
      } else if (csin_type_simple) {
        PetscMPIInt rank;
        PetscInt    issize,isidx;

        ierr = MPI_Comm_rank(PetscObjectComm((PetscObject)pc),&rank);CHKERRQ(ierr);
        if (im_active) {
          issize = 1;
          isidx = (PetscInt)rank;
        } else {
          issize = 0;
          isidx = -1;
        }
        ierr = ISCreateGeneral(PetscObjectComm((PetscObject)pc),issize,&isidx,PETSC_COPY_VALUES,&pcbddc->coarse_subassembling_init);CHKERRQ(ierr);
      } else { /* get a suitable subassembling pattern from MATIS code */
        ierr = MatISGetSubassemblingPattern(t_coarse_mat_is,ncoarse,PETSC_TRUE,&pcbddc->coarse_subassembling_init);CHKERRQ(ierr);
      }

      /* we need to shift on coarse candidates either if we are not redistributing or we are redistributing and we have enough void processes */
      if (!redist || ncoarse <= void_procs) {
        PetscInt ncoarse_cand,tissize,*nisindices;
        PetscInt *coarse_candidates;
        const PetscInt* tisindices;

        /* get coarse candidates' ranks in pc communicator */
        ierr = PetscMalloc1(all_procs,&coarse_candidates);CHKERRQ(ierr);
        ierr = MPI_Allgather(&im_active,1,MPIU_INT,coarse_candidates,1,MPIU_INT,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);
        for (i=0,ncoarse_cand=0;i<all_procs;i++) {
          if (!coarse_candidates[i]) {
            coarse_candidates[ncoarse_cand++]=i;
          }
        }
        if (ncoarse_cand < ncoarse) SETERRQ2(PetscObjectComm((PetscObject)pc),PETSC_ERR_PLIB,"This should not happen! %d < %d",ncoarse_cand,ncoarse);


        if (pcbddc->dbg_flag) {
          ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
          ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Subassembling pattern init (before shift)\n");CHKERRQ(ierr);
          ierr = ISView(pcbddc->coarse_subassembling_init,pcbddc->dbg_viewer);CHKERRQ(ierr);
          ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Coarse candidates\n");CHKERRQ(ierr);
          for (i=0;i<ncoarse_cand;i++) {
            ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"%d ",coarse_candidates[i]);CHKERRQ(ierr);
          }
          ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"\n");CHKERRQ(ierr);
          ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
        }
        /* shift the pattern on coarse candidates */
        ierr = ISGetLocalSize(pcbddc->coarse_subassembling_init,&tissize);CHKERRQ(ierr);
        ierr = ISGetIndices(pcbddc->coarse_subassembling_init,&tisindices);CHKERRQ(ierr);
        ierr = PetscMalloc1(tissize,&nisindices);CHKERRQ(ierr);
        for (i=0;i<tissize;i++) nisindices[i] = coarse_candidates[tisindices[i]];
        ierr = ISRestoreIndices(pcbddc->coarse_subassembling_init,&tisindices);CHKERRQ(ierr);
        ierr = ISGeneralSetIndices(pcbddc->coarse_subassembling_init,tissize,nisindices,PETSC_OWN_POINTER);CHKERRQ(ierr);
        ierr = PetscFree(coarse_candidates);CHKERRQ(ierr);
      }
      if (pcbddc->dbg_flag) {
        ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
        ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Subassembling pattern init\n");CHKERRQ(ierr);
        ierr = ISView(pcbddc->coarse_subassembling_init,pcbddc->dbg_viewer);CHKERRQ(ierr);
        ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      }
    }
    /* get temporary coarse mat in IS format restricted on coarse procs (plus additional index sets of isarray) */
    ierr = MatISSubassemble(t_coarse_mat_is,pcbddc->coarse_subassembling_init,0,PETSC_TRUE,MAT_INITIAL_MATRIX,&coarse_mat_is,nis,isarray);CHKERRQ(ierr);
  } else {
    if (pcbddc->dbg_flag) {
      ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Subassembling pattern init not needed\n");CHKERRQ(ierr);
      ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    }
    ierr = PetscObjectReference((PetscObject)t_coarse_mat_is);CHKERRQ(ierr);
    coarse_mat_is = t_coarse_mat_is;
  }

  /* create local to global scatters for coarse problem */
  if (compute_vecs) {
    PetscInt lrows;
    ierr = VecDestroy(&pcbddc->coarse_vec);CHKERRQ(ierr);
    if (coarse_mat_is) {
      ierr = MatGetLocalSize(coarse_mat_is,&lrows,NULL);CHKERRQ(ierr);
    } else {
      lrows = 0;
    }
    ierr = VecCreate(PetscObjectComm((PetscObject)pc),&pcbddc->coarse_vec);CHKERRQ(ierr);
    ierr = VecSetSizes(pcbddc->coarse_vec,lrows,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetType(pcbddc->coarse_vec,VECSTANDARD);CHKERRQ(ierr);
    ierr = VecScatterDestroy(&pcbddc->coarse_loc_to_glob);CHKERRQ(ierr);
    ierr = VecScatterCreate(pcbddc->vec1_P,NULL,pcbddc->coarse_vec,coarse_is,&pcbddc->coarse_loc_to_glob);CHKERRQ(ierr);
  }
  ierr = ISDestroy(&coarse_is);CHKERRQ(ierr);
  ierr = MatDestroy(&t_coarse_mat_is);CHKERRQ(ierr);

  /* set defaults for coarse KSP and PC */
  if (multilevel_allowed) {
    coarse_ksp_type = KSPRICHARDSON;
    coarse_pc_type = PCBDDC;
  } else {
    coarse_ksp_type = KSPPREONLY;
    coarse_pc_type = PCREDUNDANT;
  }

  /* print some info if requested */
  if (pcbddc->dbg_flag) {
    if (!multilevel_allowed) {
      ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
      if (multilevel_requested) {
        ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Not enough active processes on level %d (active processes %d, coarsening ratio %d)\n",pcbddc->current_level,active_procs,pcbddc->coarsening_ratio);CHKERRQ(ierr);
      } else if (pcbddc->max_levels) {
        ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Maximum number of requested levels reached (%d)\n",pcbddc->max_levels);CHKERRQ(ierr);
      }
      ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    }
  }

  /* create the coarse KSP object only once with defaults */
  if (coarse_mat_is) {
    MatReuse coarse_mat_reuse;
    PetscViewer dbg_viewer = NULL;
    if (pcbddc->dbg_flag) {
      dbg_viewer = PETSC_VIEWER_STDOUT_(PetscObjectComm((PetscObject)coarse_mat_is));
      ierr = PetscViewerASCIIAddTab(dbg_viewer,2*pcbddc->current_level);CHKERRQ(ierr);
    }
    if (!pcbddc->coarse_ksp) {
      char prefix[256],str_level[16];
      size_t len;
      ierr = KSPCreate(PetscObjectComm((PetscObject)coarse_mat_is),&pcbddc->coarse_ksp);CHKERRQ(ierr);
      ierr = PetscObjectIncrementTabLevel((PetscObject)pcbddc->coarse_ksp,(PetscObject)pc,1);CHKERRQ(ierr);
      ierr = KSPSetTolerances(pcbddc->coarse_ksp,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT,1);CHKERRQ(ierr);
      ierr = KSPSetOperators(pcbddc->coarse_ksp,coarse_mat_is,coarse_mat_is);CHKERRQ(ierr);
      ierr = KSPSetType(pcbddc->coarse_ksp,coarse_ksp_type);CHKERRQ(ierr);
      ierr = KSPSetNormType(pcbddc->coarse_ksp,KSP_NORM_NONE);CHKERRQ(ierr);
      ierr = KSPGetPC(pcbddc->coarse_ksp,&pc_temp);CHKERRQ(ierr);
      ierr = PCSetType(pc_temp,coarse_pc_type);CHKERRQ(ierr);
      ierr = PCFactorSetReuseFill(pc_temp,PETSC_TRUE);CHKERRQ(ierr);
      /* prefix */
      ierr = PetscStrcpy(prefix,"");CHKERRQ(ierr);
      ierr = PetscStrcpy(str_level,"");CHKERRQ(ierr);
      if (!pcbddc->current_level) {
        ierr = PetscStrcpy(prefix,((PetscObject)pc)->prefix);CHKERRQ(ierr);
        ierr = PetscStrcat(prefix,"pc_bddc_coarse_");CHKERRQ(ierr);
      } else {
        ierr = PetscStrlen(((PetscObject)pc)->prefix,&len);CHKERRQ(ierr);
        if (pcbddc->current_level>1) len -= 3; /* remove "lX_" with X level number */
        if (pcbddc->current_level>10) len -= 1; /* remove another char from level number */
        ierr = PetscStrncpy(prefix,((PetscObject)pc)->prefix,len+1);CHKERRQ(ierr);
        sprintf(str_level,"l%d_",(int)(pcbddc->current_level));
        ierr = PetscStrcat(prefix,str_level);CHKERRQ(ierr);
      }
      ierr = KSPSetOptionsPrefix(pcbddc->coarse_ksp,prefix);CHKERRQ(ierr);
      /* propagate BDDC info to the next level (these are dummy calls if pc_temp is not of type PCBDDC) */
      ierr = PCBDDCSetLevel(pc_temp,pcbddc->current_level+1);CHKERRQ(ierr);
      ierr = PCBDDCSetCoarseningRatio(pc_temp,pcbddc->coarsening_ratio);CHKERRQ(ierr);
      ierr = PCBDDCSetLevels(pc_temp,pcbddc->max_levels);CHKERRQ(ierr);
      /* allow user customization */
      ierr = KSPSetFromOptions(pcbddc->coarse_ksp);CHKERRQ(ierr);
    }
    /* propagate BDDC info to the next level (these are dummy calls if pc_temp is not of type PCBDDC) */
    if (nisdofs) {
      ierr = PCBDDCSetDofsSplitting(pc_temp,nisdofs,isarray);CHKERRQ(ierr);
      for (i=0;i<nisdofs;i++) {
        ierr = ISDestroy(&isarray[i]);CHKERRQ(ierr);
      }
    }
    if (nisneu) {
      ierr = PCBDDCSetNeumannBoundaries(pc_temp,isarray[nisdofs]);CHKERRQ(ierr);
      ierr = ISDestroy(&isarray[nisdofs]);CHKERRQ(ierr);
    }

    /* get some info after set from options */
    ierr = KSPGetPC(pcbddc->coarse_ksp,&pc_temp);CHKERRQ(ierr);
    ierr = PetscObjectTypeCompare((PetscObject)pc_temp,PCNN,&isnn);CHKERRQ(ierr);
    ierr = PetscObjectTypeCompare((PetscObject)pc_temp,PCBDDC,&isbddc);CHKERRQ(ierr);
    ierr = PetscObjectTypeCompare((PetscObject)pc_temp,PCREDUNDANT,&isredundant);CHKERRQ(ierr);
    if (isbddc && !multilevel_allowed) { /* multilevel can only be requested via pc_bddc_set_levels */
      ierr = PCSetType(pc_temp,coarse_pc_type);CHKERRQ(ierr);
      isbddc = PETSC_FALSE;
    }
    if (isredundant) {
      KSP inner_ksp;
      PC inner_pc;
      ierr = PCRedundantGetKSP(pc_temp,&inner_ksp);CHKERRQ(ierr);
      ierr = KSPGetPC(inner_ksp,&inner_pc);CHKERRQ(ierr);
      ierr = PCFactorSetReuseFill(inner_pc,PETSC_TRUE);CHKERRQ(ierr);
    }

    /* assemble coarse matrix */
    if (coarse_reuse) {
      ierr = KSPGetOperators(pcbddc->coarse_ksp,&coarse_mat,NULL);CHKERRQ(ierr);
      ierr = PetscObjectReference((PetscObject)coarse_mat);CHKERRQ(ierr);
      coarse_mat_reuse = MAT_REUSE_MATRIX;
    } else {
      coarse_mat_reuse = MAT_INITIAL_MATRIX;
    }
    if (isbddc || isnn) {
      if (pcbddc->coarsening_ratio > 1) {
        if (!pcbddc->coarse_subassembling) { /* subassembling info is not present */
          ierr = MatISGetSubassemblingPattern(coarse_mat_is,active_procs/pcbddc->coarsening_ratio,PETSC_TRUE,&pcbddc->coarse_subassembling);CHKERRQ(ierr);
          if (pcbddc->dbg_flag) {
            ierr = PetscViewerASCIIPrintf(dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
            ierr = PetscViewerASCIIPrintf(dbg_viewer,"Subassembling pattern\n");CHKERRQ(ierr);
            ierr = ISView(pcbddc->coarse_subassembling,dbg_viewer);CHKERRQ(ierr);
            ierr = PetscViewerFlush(dbg_viewer);CHKERRQ(ierr);
          }
        }
        ierr = MatISSubassemble(coarse_mat_is,pcbddc->coarse_subassembling,0,PETSC_FALSE,coarse_mat_reuse,&coarse_mat,0,NULL);CHKERRQ(ierr);
      } else {
        ierr = PetscObjectReference((PetscObject)coarse_mat_is);CHKERRQ(ierr);
        coarse_mat = coarse_mat_is;
      }
    } else {
      ierr = MatISGetMPIXAIJ(coarse_mat_is,coarse_mat_reuse,&coarse_mat);CHKERRQ(ierr);
    }
    ierr = MatDestroy(&coarse_mat_is);CHKERRQ(ierr);

    /* propagate symmetry info to coarse matrix */
    ierr = MatSetOption(coarse_mat,MAT_SYMMETRIC,pcbddc->issym);CHKERRQ(ierr);
    ierr = MatSetOption(coarse_mat,MAT_STRUCTURALLY_SYMMETRIC,PETSC_TRUE);CHKERRQ(ierr);

    /* set operators */
    ierr = KSPSetOperators(pcbddc->coarse_ksp,coarse_mat,coarse_mat);CHKERRQ(ierr);
    if (pcbddc->dbg_flag) {
      ierr = PetscViewerASCIISubtractTab(dbg_viewer,2*pcbddc->current_level);CHKERRQ(ierr);
    }
  } else { /* processes non partecipating to coarse solver (if any) */
    coarse_mat = 0;
  }
  ierr = PetscFree(isarray);CHKERRQ(ierr);
#if 0
  {
    PetscViewer viewer;
    char filename[256];
    sprintf(filename,"coarse_mat.m");
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,filename,&viewer);CHKERRQ(ierr);
    ierr = PetscViewerSetFormat(viewer,PETSC_VIEWER_ASCII_MATLAB);CHKERRQ(ierr);
    ierr = MatView(coarse_mat,viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
#endif

  /* Compute coarse null space (special handling by BDDC only) */
  if (pcbddc->NullSpace) {
    ierr = PCBDDCNullSpaceAssembleCoarse(pc,coarse_mat,&CoarseNullSpace);CHKERRQ(ierr);
  }

  if (pcbddc->coarse_ksp) {
    Vec crhs,csol;
    PetscBool ispreonly;
    if (CoarseNullSpace) {
      if (isbddc) {
        ierr = PCBDDCSetNullSpace(pc_temp,CoarseNullSpace);CHKERRQ(ierr);
      } else {
        ierr = KSPSetNullSpace(pcbddc->coarse_ksp,CoarseNullSpace);CHKERRQ(ierr);
      }
    }
    /* setup coarse ksp */
    ierr = KSPSetUp(pcbddc->coarse_ksp);CHKERRQ(ierr);
    ierr = KSPGetSolution(pcbddc->coarse_ksp,&csol);CHKERRQ(ierr);
    ierr = KSPGetRhs(pcbddc->coarse_ksp,&crhs);CHKERRQ(ierr);
    /* hack */
    if (!csol) {
      ierr = MatCreateVecs(coarse_mat,&((pcbddc->coarse_ksp)->vec_sol),NULL);CHKERRQ(ierr);
    }
    if (!crhs) {
      ierr = MatCreateVecs(coarse_mat,NULL,&((pcbddc->coarse_ksp)->vec_rhs));CHKERRQ(ierr);
    }
    /* Check coarse problem if in debug mode or if solving with an iterative method */
    ierr = PetscObjectTypeCompare((PetscObject)pcbddc->coarse_ksp,KSPPREONLY,&ispreonly);CHKERRQ(ierr);
    if (pcbddc->dbg_flag || (!ispreonly && pcbddc->use_coarse_estimates) ) {
      KSP       check_ksp;
      KSPType   check_ksp_type;
      PC        check_pc;
      Vec       check_vec,coarse_vec;
      PetscReal abs_infty_error,infty_error,lambda_min=1.0,lambda_max=1.0;
      PetscInt  its;
      PetscBool compute_eigs;
      PetscReal *eigs_r,*eigs_c;
      PetscInt  neigs;
      const char *prefix;

      /* Create ksp object suitable for estimation of extreme eigenvalues */
      ierr = KSPCreate(PetscObjectComm((PetscObject)pcbddc->coarse_ksp),&check_ksp);CHKERRQ(ierr);
      ierr = KSPSetOperators(check_ksp,coarse_mat,coarse_mat);CHKERRQ(ierr);
      ierr = KSPSetTolerances(check_ksp,1.e-12,1.e-12,PETSC_DEFAULT,pcbddc->coarse_size);CHKERRQ(ierr);
      if (ispreonly) {
        check_ksp_type = KSPPREONLY;
        compute_eigs = PETSC_FALSE;
      } else {
        check_ksp_type = KSPGMRES;
        compute_eigs = PETSC_TRUE;
      }
      ierr = KSPSetType(check_ksp,check_ksp_type);CHKERRQ(ierr);
      ierr = KSPSetComputeSingularValues(check_ksp,compute_eigs);CHKERRQ(ierr);
      ierr = KSPSetComputeEigenvalues(check_ksp,compute_eigs);CHKERRQ(ierr);
      ierr = KSPGMRESSetRestart(check_ksp,pcbddc->coarse_size+1);CHKERRQ(ierr);
      ierr = KSPGetOptionsPrefix(pcbddc->coarse_ksp,&prefix);CHKERRQ(ierr);
      ierr = KSPSetOptionsPrefix(check_ksp,prefix);CHKERRQ(ierr);
      ierr = KSPAppendOptionsPrefix(check_ksp,"check_");CHKERRQ(ierr);
      ierr = KSPSetFromOptions(check_ksp);CHKERRQ(ierr);
      ierr = KSPSetUp(check_ksp);CHKERRQ(ierr);
      ierr = KSPGetPC(pcbddc->coarse_ksp,&check_pc);CHKERRQ(ierr);
      ierr = KSPSetPC(check_ksp,check_pc);CHKERRQ(ierr);
      /* create random vec */
      ierr = KSPGetSolution(pcbddc->coarse_ksp,&coarse_vec);CHKERRQ(ierr);
      ierr = VecDuplicate(coarse_vec,&check_vec);CHKERRQ(ierr);
      ierr = VecSetRandom(check_vec,NULL);CHKERRQ(ierr);
      if (CoarseNullSpace) {
        ierr = MatNullSpaceRemove(CoarseNullSpace,check_vec);CHKERRQ(ierr);
      }
      ierr = MatMult(coarse_mat,check_vec,coarse_vec);CHKERRQ(ierr);
      /* solve coarse problem */
      ierr = KSPSolve(check_ksp,coarse_vec,coarse_vec);CHKERRQ(ierr);
      if (CoarseNullSpace) {
        ierr = MatNullSpaceRemove(CoarseNullSpace,coarse_vec);CHKERRQ(ierr);
      }
      /* set eigenvalue estimation if preonly has not been requested */
      if (compute_eigs) {
        ierr = PetscMalloc1(pcbddc->coarse_size+1,&eigs_r);CHKERRQ(ierr);
        ierr = PetscMalloc1(pcbddc->coarse_size+1,&eigs_c);CHKERRQ(ierr);
        ierr = KSPComputeEigenvalues(check_ksp,pcbddc->coarse_size+1,eigs_r,eigs_c,&neigs);CHKERRQ(ierr);
        lambda_max = eigs_r[neigs-1];
        lambda_min = eigs_r[0];
        if (pcbddc->use_coarse_estimates) {
          if (lambda_max>lambda_min) {
            ierr = KSPChebyshevSetEigenvalues(pcbddc->coarse_ksp,lambda_max,lambda_min);CHKERRQ(ierr);
            ierr = KSPRichardsonSetScale(pcbddc->coarse_ksp,2.0/(lambda_max+lambda_min));CHKERRQ(ierr);
          }
        }
      }

      /* check coarse problem residual error */
      if (pcbddc->dbg_flag) {
        PetscViewer dbg_viewer = PETSC_VIEWER_STDOUT_(PetscObjectComm((PetscObject)pcbddc->coarse_ksp));
        ierr = PetscViewerASCIIAddTab(dbg_viewer,2*(pcbddc->current_level+1));CHKERRQ(ierr);
        ierr = VecAXPY(check_vec,-1.0,coarse_vec);CHKERRQ(ierr);
        ierr = VecNorm(check_vec,NORM_INFINITY,&infty_error);CHKERRQ(ierr);
        ierr = MatMult(coarse_mat,check_vec,coarse_vec);CHKERRQ(ierr);
        ierr = VecNorm(coarse_vec,NORM_INFINITY,&abs_infty_error);CHKERRQ(ierr);
        ierr = VecDestroy(&check_vec);CHKERRQ(ierr);
        ierr = PetscViewerASCIIPrintf(dbg_viewer,"Coarse problem details (use estimates %d)\n",pcbddc->use_coarse_estimates);CHKERRQ(ierr);
        ierr = PetscObjectPrintClassNamePrefixType((PetscObject)(pcbddc->coarse_ksp),dbg_viewer);CHKERRQ(ierr);
        ierr = PetscObjectPrintClassNamePrefixType((PetscObject)(check_pc),dbg_viewer);CHKERRQ(ierr);
        ierr = PetscViewerASCIIPrintf(dbg_viewer,"Coarse problem exact infty_error   : %1.6e\n",infty_error);CHKERRQ(ierr);
        ierr = PetscViewerASCIIPrintf(dbg_viewer,"Coarse problem residual infty_error: %1.6e\n",abs_infty_error);CHKERRQ(ierr);
        if (compute_eigs) {
          PetscReal lambda_max_s,lambda_min_s;
          ierr = KSPGetType(check_ksp,&check_ksp_type);CHKERRQ(ierr);
          ierr = KSPGetIterationNumber(check_ksp,&its);CHKERRQ(ierr);
          ierr = KSPComputeExtremeSingularValues(check_ksp,&lambda_max_s,&lambda_min_s);CHKERRQ(ierr);
          ierr = PetscViewerASCIIPrintf(dbg_viewer,"Coarse problem eigenvalues (estimated with %d iterations of %s): %1.6e %1.6e (%1.6e %1.6e)\n",its,check_ksp_type,lambda_min,lambda_max,lambda_min_s,lambda_max_s);CHKERRQ(ierr);
          for (i=0;i<neigs;i++) {
            ierr = PetscViewerASCIIPrintf(dbg_viewer,"%1.6e %1.6ei\n",eigs_r[i],eigs_c[i]);CHKERRQ(ierr);
          }
        }
        ierr = PetscViewerFlush(dbg_viewer);CHKERRQ(ierr);
        ierr = PetscViewerASCIISubtractTab(dbg_viewer,2*(pcbddc->current_level+1));CHKERRQ(ierr);
      }
      ierr = KSPDestroy(&check_ksp);CHKERRQ(ierr);
      if (compute_eigs) {
        ierr = PetscFree(eigs_r);CHKERRQ(ierr);
        ierr = PetscFree(eigs_c);CHKERRQ(ierr);
      }
    }
  }
  /* print additional info */
  if (pcbddc->dbg_flag) {
    /* waits until all processes reaches this point */
    ierr = PetscBarrier((PetscObject)pc);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Coarse solver setup completed at level %d\n",pcbddc->current_level);CHKERRQ(ierr);
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
  }

  /* free memory */
  ierr = MatNullSpaceDestroy(&CoarseNullSpace);CHKERRQ(ierr);
  ierr = MatDestroy(&coarse_mat);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCComputePrimalNumbering"
PetscErrorCode PCBDDCComputePrimalNumbering(PC pc,PetscInt* coarse_size_n,PetscInt** local_primal_indices_n)
{
  PC_BDDC*       pcbddc = (PC_BDDC*)pc->data;
  PC_IS*         pcis = (PC_IS*)pc->data;
  Mat_IS*        matis = (Mat_IS*)pc->pmat->data;
  PetscInt       i,coarse_size=0;
  PetscInt       *local_primal_indices=NULL;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* Compute global number of coarse dofs */
  if (!pcbddc->primal_indices_local_idxs && pcbddc->local_primal_size) {
    SETERRQ(PetscObjectComm((PetscObject)pc),PETSC_ERR_PLIB,"BDDC Local primal indices have not been created");
  }
  ierr = PCBDDCSubsetNumbering(PetscObjectComm((PetscObject)(pc->pmat)),matis->mapping,pcbddc->local_primal_size,pcbddc->primal_indices_local_idxs,NULL,&coarse_size,&local_primal_indices);CHKERRQ(ierr);

  /* check numbering */
  if (pcbddc->dbg_flag) {
    PetscScalar coarsesum,*array;
    PetscBool set_error = PETSC_FALSE,set_error_reduced = PETSC_FALSE;

    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"--------------------------------------------------\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Check coarse indices\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIISynchronizedAllow(pcbddc->dbg_viewer,PETSC_TRUE);CHKERRQ(ierr);
    ierr = VecSet(pcis->vec1_N,0.0);CHKERRQ(ierr);
    for (i=0;i<pcbddc->local_primal_size;i++) {
      ierr = VecSetValue(pcis->vec1_N,pcbddc->primal_indices_local_idxs[i],1.0,INSERT_VALUES);CHKERRQ(ierr);
    }
    ierr = VecAssemblyBegin(pcis->vec1_N);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(pcis->vec1_N);CHKERRQ(ierr);
    ierr = VecSet(pcis->vec1_global,0.0);CHKERRQ(ierr);
    ierr = VecScatterBegin(matis->ctx,pcis->vec1_N,pcis->vec1_global,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecScatterEnd(matis->ctx,pcis->vec1_N,pcis->vec1_global,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecScatterBegin(matis->ctx,pcis->vec1_global,pcis->vec1_N,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecScatterEnd(matis->ctx,pcis->vec1_global,pcis->vec1_N,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecGetArray(pcis->vec1_N,&array);CHKERRQ(ierr);
    for (i=0;i<pcis->n;i++) {
      if (array[i] == 1.0) {
        set_error = PETSC_TRUE;
        ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d: local index %d owned by a single process!\n",PetscGlobalRank,i);CHKERRQ(ierr);
      }
    }
    ierr = MPI_Allreduce(&set_error,&set_error_reduced,1,MPIU_BOOL,MPI_LOR,PetscObjectComm((PetscObject)pc));CHKERRQ(ierr);
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    for (i=0;i<pcis->n;i++) {
      if (PetscRealPart(array[i]) > 0.0) array[i] = 1.0/PetscRealPart(array[i]);
    }
    ierr = VecRestoreArray(pcis->vec1_N,&array);CHKERRQ(ierr);
    ierr = VecSet(pcis->vec1_global,0.0);CHKERRQ(ierr);
    ierr = VecScatterBegin(matis->ctx,pcis->vec1_N,pcis->vec1_global,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecScatterEnd(matis->ctx,pcis->vec1_N,pcis->vec1_global,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecSum(pcis->vec1_global,&coarsesum);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Size of coarse problem is %d (%lf)\n",coarse_size,PetscRealPart(coarsesum));CHKERRQ(ierr);
    if (pcbddc->dbg_flag > 1 || set_error_reduced) {
      PetscInt *gidxs;

      ierr = PetscMalloc1(pcbddc->local_primal_size,&gidxs);CHKERRQ(ierr);
      ierr = ISLocalToGlobalMappingApply(matis->mapping,pcbddc->local_primal_size,pcbddc->primal_indices_local_idxs,gidxs);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(pcbddc->dbg_viewer,"Distribution of local primal indices\n");CHKERRQ(ierr);
      ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"Subdomain %04d\n",PetscGlobalRank);CHKERRQ(ierr);
      for (i=0;i<pcbddc->local_primal_size;i++) {
        ierr = PetscViewerASCIISynchronizedPrintf(pcbddc->dbg_viewer,"local_primal_indices[%d]=%d (%d,%d)\n",i,local_primal_indices[i],pcbddc->primal_indices_local_idxs[i],gidxs[i]);CHKERRQ(ierr);
      }
      ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
      ierr = PetscFree(gidxs);CHKERRQ(ierr);
    }
    ierr = PetscViewerFlush(pcbddc->dbg_viewer);CHKERRQ(ierr);
    if (set_error_reduced) SETERRQ(PetscObjectComm((PetscObject)pc),PETSC_ERR_PLIB,"BDDC Numbering of coarse dofs failed");
  }
  /* ierr = PetscPrintf(PetscObjectComm((PetscObject)pc),"Size of coarse problem is %d\n",coarse_size);CHKERRQ(ierr); */
  /* get back data */
  *coarse_size_n = coarse_size;
  *local_primal_indices_n = local_primal_indices;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCGlobalToLocal"
PetscErrorCode PCBDDCGlobalToLocal(VecScatter g2l_ctx,Vec gwork, Vec lwork, IS globalis, IS* localis)
{
  IS             localis_t;
  PetscInt       i,lsize,*idxs,n;
  PetscScalar    *vals;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* get indices in local ordering exploiting local to global map */
  ierr = ISGetLocalSize(globalis,&lsize);CHKERRQ(ierr);
  ierr = PetscMalloc1(lsize,&vals);CHKERRQ(ierr);
  for (i=0;i<lsize;i++) vals[i] = 1.0;
  ierr = ISGetIndices(globalis,(const PetscInt**)&idxs);CHKERRQ(ierr);
  ierr = VecSet(gwork,0.0);CHKERRQ(ierr);
  ierr = VecSet(lwork,0.0);CHKERRQ(ierr);
  if (idxs) { /* multilevel guard */
    ierr = VecSetValues(gwork,lsize,idxs,vals,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(gwork);CHKERRQ(ierr);
  ierr = ISRestoreIndices(globalis,(const PetscInt**)&idxs);CHKERRQ(ierr);
  ierr = PetscFree(vals);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(gwork);CHKERRQ(ierr);
  /* now compute set in local ordering */
  ierr = VecScatterBegin(g2l_ctx,gwork,lwork,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(g2l_ctx,gwork,lwork,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecGetArrayRead(lwork,(const PetscScalar**)&vals);CHKERRQ(ierr);
  ierr = VecGetSize(lwork,&n);CHKERRQ(ierr);
  for (i=0,lsize=0;i<n;i++) {
    if (PetscRealPart(vals[i]) > 0.5) {
      lsize++;
    }
  }
  ierr = PetscMalloc1(lsize,&idxs);CHKERRQ(ierr);
  for (i=0,lsize=0;i<n;i++) {
    if (PetscRealPart(vals[i]) > 0.5) {
      idxs[lsize++] = i;
    }
  }
  ierr = VecRestoreArrayRead(lwork,(const PetscScalar**)&vals);CHKERRQ(ierr);
  ierr = ISCreateGeneral(PetscObjectComm((PetscObject)gwork),lsize,idxs,PETSC_OWN_POINTER,&localis_t);CHKERRQ(ierr);
  *localis = localis_t;
  PetscFunctionReturn(0);
}

/* the next two functions will be called in KSPMatMult if a change of basis has been requested */
#undef __FUNCT__
#define __FUNCT__ "PCBDDCMatMult_Private"
static PetscErrorCode PCBDDCMatMult_Private(Mat A, Vec x, Vec y)
{
  PCBDDCChange_ctx change_ctx;
  PetscErrorCode   ierr;

  PetscFunctionBegin;
  ierr = MatShellGetContext(A,&change_ctx);CHKERRQ(ierr);
  ierr = MatMult(change_ctx->global_change,x,change_ctx->work[0]);CHKERRQ(ierr);
  ierr = MatMult(change_ctx->original_mat,change_ctx->work[0],change_ctx->work[1]);CHKERRQ(ierr);
  ierr = MatMultTranspose(change_ctx->global_change,change_ctx->work[1],y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCMatMultTranspose_Private"
static PetscErrorCode PCBDDCMatMultTranspose_Private(Mat A, Vec x, Vec y)
{
  PCBDDCChange_ctx change_ctx;
  PetscErrorCode   ierr;

  PetscFunctionBegin;
  ierr = MatShellGetContext(A,&change_ctx);CHKERRQ(ierr);
  ierr = MatMult(change_ctx->global_change,x,change_ctx->work[0]);CHKERRQ(ierr);
  ierr = MatMultTranspose(change_ctx->original_mat,change_ctx->work[0],change_ctx->work[1]);CHKERRQ(ierr);
  ierr = MatMultTranspose(change_ctx->global_change,change_ctx->work[1],y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCSetUpSubSchurs"
PetscErrorCode PCBDDCSetUpSubSchurs(PC pc)
{
  PC_BDDC             *pcbddc=(PC_BDDC*)pc->data;
  PCBDDCSubSchurs     sub_schurs=pcbddc->sub_schurs;
  PetscInt            *used_xadj,*used_adjncy;
  PetscBool           free_used_adj;
  PetscErrorCode      ierr;

  PetscFunctionBegin;
  /* decide the adjacency to be used for determining internal problems for local schur on subsets */
  free_used_adj = PETSC_FALSE;
  if (pcbddc->sub_schurs_layers == -1) {
    used_xadj = NULL;
    used_adjncy = NULL;
  } else {
    if (pcbddc->sub_schurs_use_useradj && pcbddc->mat_graph->xadj) {
      used_xadj = pcbddc->mat_graph->xadj;
      used_adjncy = pcbddc->mat_graph->adjncy;
    } else if (pcbddc->computed_rowadj) {
      used_xadj = pcbddc->mat_graph->xadj;
      used_adjncy = pcbddc->mat_graph->adjncy;
    } else {
      PetscBool      flg_row=PETSC_FALSE;
      const PetscInt *xadj,*adjncy;
      PetscInt       nvtxs;

      ierr = MatGetRowIJ(pcbddc->local_mat,0,PETSC_TRUE,PETSC_FALSE,&nvtxs,&xadj,&adjncy,&flg_row);CHKERRQ(ierr);
      if (flg_row) {
        ierr = PetscMalloc2(nvtxs+1,&used_xadj,xadj[nvtxs],&used_adjncy);CHKERRQ(ierr);
        ierr = PetscMemcpy(used_xadj,xadj,(nvtxs+1)*sizeof(*xadj));CHKERRQ(ierr);
        ierr = PetscMemcpy(used_adjncy,adjncy,(xadj[nvtxs])*sizeof(*adjncy));CHKERRQ(ierr);
        free_used_adj = PETSC_TRUE;
      } else {
        pcbddc->sub_schurs_layers = -1;
        used_xadj = NULL;
        used_adjncy = NULL;
      }
      ierr = MatRestoreRowIJ(pcbddc->local_mat,0,PETSC_TRUE,PETSC_FALSE,&nvtxs,&xadj,&adjncy,&flg_row);CHKERRQ(ierr);
    }
  }

  /* set Schur complement solver for interior variables (used only when MUMPS is not present) */
  if (!sub_schurs->use_mumps) {
    ierr = MatSchurComplementSetKSP(sub_schurs->S,pcbddc->ksp_D);CHKERRQ(ierr);
  }

  /* setup sub_schurs data */
  ierr = PCBDDCSubSchursSetUp(sub_schurs,used_xadj,used_adjncy,pcbddc->sub_schurs_layers,pcbddc->faster_deluxe,pcbddc->adaptive_selection,pcbddc->use_edges,pcbddc->use_faces);CHKERRQ(ierr);

  /* free adjacency */
  if (free_used_adj) {
    ierr = PetscFree2(used_xadj,used_adjncy);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCBDDCInitSubSchurs"
PetscErrorCode PCBDDCInitSubSchurs(PC pc)
{
  PC_IS               *pcis=(PC_IS*)pc->data;
  PC_BDDC             *pcbddc=(PC_BDDC*)pc->data;
  PCBDDCSubSchurs     sub_schurs=pcbddc->sub_schurs;
  PCBDDCGraph         graph;
  Mat                 S_j;
  PetscErrorCode      ierr;

  PetscFunctionBegin;
  /* attach interface graph for determining subsets */
  if (pcbddc->sub_schurs_rebuild) { /* in case rebuild has been requested, it uses a graph generated only by the neighbouring information */
    IS verticesIS;

    ierr = PCBDDCGraphGetCandidatesIS(pcbddc->mat_graph,NULL,NULL,NULL,NULL,&verticesIS);CHKERRQ(ierr);
    ierr = PCBDDCGraphCreate(&graph);CHKERRQ(ierr);
    ierr = PCBDDCGraphInit(graph,pcbddc->mat_graph->l2gmap);CHKERRQ(ierr);
    ierr = PCBDDCGraphSetUp(graph,0,NULL,pcbddc->DirichletBoundariesLocal,0,NULL,verticesIS);CHKERRQ(ierr);
    ierr = PCBDDCGraphComputeConnectedComponents(graph);CHKERRQ(ierr);
    ierr = ISDestroy(&verticesIS);CHKERRQ(ierr);
/*
    if (pcbddc->dbg_flag) {
      ierr = PCBDDCGraphASCIIView(graph,pcbddc->dbg_flag,pcbddc->dbg_viewer);CHKERRQ(ierr);
    }
*/
  } else {
    graph = pcbddc->mat_graph;
  }

  /* Create Schur complement matrix */
  ierr = MatCreateSchurComplement(pcis->A_II,pcis->A_II,pcis->A_IB,pcis->A_BI,pcis->A_BB,&S_j);CHKERRQ(ierr);

  /* sub_schurs init */
  ierr = PCBDDCSubSchursInit(sub_schurs,pcbddc->local_mat,S_j,pcis->is_I_local,pcis->is_B_local,graph,pcis->BtoNmap);CHKERRQ(ierr);
  ierr = MatDestroy(&S_j);CHKERRQ(ierr);
  /* free graph struct */
  if (pcbddc->sub_schurs_rebuild) {
    ierr = PCBDDCGraphDestroy(&graph);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}
