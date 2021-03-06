#include <../src/ksp/ksp/utils/lmvm/symbrdn/symbrdn.h> /*I "petscksp.h" I*/

/*
  Limited-memory Davidon-Fletcher-Powell method for approximating both 
  the forward product and inverse application of a Jacobian.
 */

/*------------------------------------------------------------*/

/*
  The solution method (approximate inverse Jacobian application) is 
  matrix-vector product version of the recursive formula given in 
  Equation (6.15) of Nocedal and Wright "Numerical Optimization" 2nd 
  edition, pg 139.
  
  Note: Q[i] = (B_i)^{-1}*S[i] terms are computed ahead of time whenever 
  the matrix is updated with a new (S[i], Y[i]) pair. This allows 
  repeated calls of MatSolve without incurring redundant computation.

  dX <- J0^{-1} * F

  for i = 0,1,2,...,k
    # Q[i] = (B_i)^{-1} * Y[i]
    gamma = (S[i]^T F) / (Y[i]^T S[i])
    zeta = (Y[i]^T dX) / (Y[i]^T Q[i])
    dX <- dX + (gamma * S[i]) - (zeta * Q[i])
  end
*/
PetscErrorCode MatSolve_LMVMDFP(Mat B, Vec F, Vec dX)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *ldfp = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;
  PetscInt          i, j;
  PetscScalar       yjtqi, sjtyi, ytx, stf, ytq;
  
  PetscFunctionBegin;
  PetscValidHeaderSpecific(F, VEC_CLASSID, 2);
  PetscValidHeaderSpecific(dX, VEC_CLASSID, 3);
  VecCheckSameSize(F, 2, dX, 3);
  VecCheckMatCompatible(B, dX, 3, F, 2);
  
  if (ldfp->needQ) {
    /* Start the loop for (Q[k] = (B_k)^{-1} * Y[k]) */
    for (i = 0; i <= lmvm->k; ++i) {
      ierr = MatSymBrdnApplyJ0Inv(B, lmvm->Y[i], ldfp->Q[i]);CHKERRQ(ierr);
      for (j = 0; j <= i-1; ++j) {
        /* Compute the necessary dot products */
        ierr = VecDotBegin(lmvm->Y[j], ldfp->Q[i], &yjtqi);CHKERRQ(ierr);
        ierr = VecDotBegin(lmvm->S[j], lmvm->Y[i], &sjtyi);CHKERRQ(ierr);
        ierr = VecDotEnd(lmvm->Y[j], ldfp->Q[i], &yjtqi);CHKERRQ(ierr);
        ierr = VecDotEnd(lmvm->S[j], lmvm->Y[i], &sjtyi);CHKERRQ(ierr);
        /* Compute the pure DFP component of the inverse application*/
        ierr = VecAXPBYPCZ(ldfp->Q[i], -PetscRealPart(yjtqi)/ldfp->ytq[j], PetscRealPart(sjtyi)/ldfp->yts[j], 1.0, ldfp->Q[j], lmvm->S[j]);CHKERRQ(ierr);
      }
      ierr = VecDot(lmvm->Y[i], ldfp->Q[i], &ytq);CHKERRQ(ierr);
      ldfp->ytq[i] = PetscRealPart(ytq);
    }
    ldfp->needQ = PETSC_FALSE;
  }
  
  /* Start the outer loop (i) for the recursive formula */
  ierr = MatSymBrdnApplyJ0Inv(B, F, dX);CHKERRQ(ierr);
  for (i = 0; i <= lmvm->k; ++i) {
    /* Get all the dot products we need */
    ierr = VecDotBegin(lmvm->Y[i], dX, &ytx);CHKERRQ(ierr);
    ierr = VecDotBegin(lmvm->S[i], F, &stf);CHKERRQ(ierr);
    ierr = VecDotEnd(lmvm->Y[i], dX, &ytx);CHKERRQ(ierr);
    ierr = VecDotEnd(lmvm->S[i], F, &stf);CHKERRQ(ierr);
    /* Update dX_{i+1} = (B^{-1})_{i+1} * f */
    ierr = VecAXPBYPCZ(dX, -PetscRealPart(ytx)/ldfp->ytq[i], PetscRealPart(stf)/ldfp->yts[i], 1.0, ldfp->Q[i], lmvm->S[i]);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

/*
  The forward product for the approximate Jacobian is the matrix-free 
  implementation of the recursive formula given in Equation 6.13 of 
  Nocedal and Wright "Numerical Optimization" 2nd edition, pg 139.
  
  This forward product has a two-loop form similar to the BFGS two-loop 
  formulation for the inverse Jacobian application. However, the S and 
  Y vectors have interchanged roles.

  work <- X

  for i = k,k-1,k-2,...,0
    rho[i] = 1 / (Y[i]^T S[i])
    alpha[i] = rho[i] * (Y[i]^T work)
    work <- work - (alpha[i] * S[i])
  end

  Z <- J0 * work

  for i = 0,1,2,...,k
    beta = rho[i] * (S[i]^T Y)
    Z <- Z + ((alpha[i] - beta) * Y[i])
  end
*/
PetscErrorCode MatMult_LMVMDFP(Mat B, Vec X, Vec Z)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *ldfp = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;
  PetscInt          i;
  PetscReal         *alpha, beta;
  PetscScalar       ytx, stz;
  
  PetscFunctionBegin;
  /* Copy the function into the work vector for the first loop */
  ierr = VecCopy(X, ldfp->work);CHKERRQ(ierr);
  
  /* Start the first loop */
  ierr = PetscMalloc1(lmvm->k+1, &alpha);CHKERRQ(ierr);
  for (i = lmvm->k; i >= 0; --i) {
    ierr = VecDot(lmvm->Y[i], ldfp->work, &ytx);CHKERRQ(ierr);
    alpha[i] = PetscRealPart(ytx)/ldfp->yts[i];
    ierr = VecAXPY(ldfp->work, -alpha[i], lmvm->S[i]);CHKERRQ(ierr);
  }
  
  /* Apply the forward product with initial Jacobian */
  ierr = MatSymBrdnApplyJ0Fwd(B, ldfp->work, Z);CHKERRQ(ierr);
  
  /* Start the second loop */
  for (i = 0; i <= lmvm->k; ++i) {
    ierr = VecDot(lmvm->S[i], Z, &stz);CHKERRQ(ierr);
    beta = PetscRealPart(stz)/ldfp->yts[i];
    ierr = VecAXPY(Z, alpha[i]-beta, lmvm->Y[i]);CHKERRQ(ierr);
  }
  ierr = PetscFree(alpha);
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

static PetscErrorCode MatUpdate_LMVMDFP(Mat B, Vec X, Vec F)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *ldfp = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;
  PetscInt          old_k, i;
  PetscReal         curvtol;
  PetscScalar       curvature, ytytmp, ststmp;

  PetscFunctionBegin;
  if (!lmvm->m) PetscFunctionReturn(0);
  if (lmvm->prev_set) {
    /* Compute the new (S = X - Xprev) and (Y = F - Fprev) vectors */
    ierr = VecAYPX(lmvm->Xprev, -1.0, X);CHKERRQ(ierr);
    ierr = VecAYPX(lmvm->Fprev, -1.0, F);CHKERRQ(ierr);
    /* Test if the updates can be accepted */
    ierr = VecDotBegin(lmvm->Xprev, lmvm->Fprev, &curvature);CHKERRQ(ierr);
    ierr = VecDotBegin(lmvm->Xprev, lmvm->Xprev, &ststmp);CHKERRQ(ierr);
    ierr = VecDotEnd(lmvm->Xprev, lmvm->Fprev, &curvature);CHKERRQ(ierr);
    ierr = VecDotEnd(lmvm->Xprev, lmvm->Xprev, &ststmp);CHKERRQ(ierr);
    if (PetscRealPart(ststmp) < lmvm->eps) {
      curvtol = 0.0;
    } else {
      curvtol = lmvm->eps * PetscRealPart(ststmp);
    }
    if (PetscRealPart(curvature) > curvtol) {
      /* Update is good, accept it */
      ldfp->watchdog = 0;
      ldfp->needQ = PETSC_TRUE;
      old_k = lmvm->k;
      ierr = MatUpdateKernel_LMVM(B, lmvm->Xprev, lmvm->Fprev);CHKERRQ(ierr);
      /* If we hit the memory limit, shift the yts, yty and sts arrays */
      if (old_k == lmvm->k) {
        for (i = 0; i <= lmvm->k-1; ++i) {
          ldfp->yts[i] = ldfp->yts[i+1];
          ldfp->yty[i] = ldfp->yty[i+1];
          ldfp->sts[i] = ldfp->sts[i+1];
        }
      }
      /* Update history of useful scalars */
      ierr = VecDot(lmvm->Y[lmvm->k], lmvm->Y[lmvm->k], &ytytmp);
      ldfp->yts[lmvm->k] = PetscRealPart(curvature);
      ldfp->yty[lmvm->k] = PetscRealPart(ytytmp);
      ldfp->sts[lmvm->k] = PetscRealPart(ststmp);
      /* Update the scaling */
      switch (ldfp->scale_type) {
      case SYMBRDN_SCALE_SCALAR:
        ierr = MatSymBrdnComputeJ0Scalar(B);CHKERRQ(ierr);
        break;
      case SYMBRDN_SCALE_DIAG:
        ierr = MatSymBrdnComputeJ0Diag(B);CHKERRQ(ierr);
        break;
      case SYMBRDN_SCALE_NONE:
      default:
        break;
      }
    } else {
      /* Update is bad, skip it */
      ++lmvm->nrejects;
      ++ldfp->watchdog;
    }
  } else {
    switch (ldfp->scale_type) {
    case SYMBRDN_SCALE_DIAG:
      ierr = VecSet(ldfp->invD, ldfp->delta);CHKERRQ(ierr);
      break;
    case SYMBRDN_SCALE_SCALAR:
      ldfp->sigma = ldfp->delta;
      break;
    case SYMBRDN_SCALE_NONE:
      ldfp->sigma = 1.0;
      break;
    default:
      break;
    }
  }
  
  if (ldfp->watchdog > ldfp->max_seq_rejects) {
    ierr = MatLMVMReset(B, PETSC_FALSE);CHKERRQ(ierr);
  }

  /* Save the solution and function to be used in the next update */
  ierr = VecCopy(X, lmvm->Xprev);CHKERRQ(ierr);
  ierr = VecCopy(F, lmvm->Fprev);CHKERRQ(ierr);
  lmvm->prev_set = PETSC_TRUE;
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

static PetscErrorCode MatCopy_LMVMDFP(Mat B, Mat M, MatStructure str)
{
  Mat_LMVM          *bdata = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *bctx = (Mat_SymBrdn*)bdata->ctx;
  Mat_LMVM          *mdata = (Mat_LMVM*)M->data;
  Mat_SymBrdn       *mctx = (Mat_SymBrdn*)mdata->ctx;
  PetscErrorCode    ierr;
  PetscInt          i;

  PetscFunctionBegin;
  mctx->needQ = bctx->needQ;
  for (i=0; i<=bdata->k; ++i) {
    mctx->ytq[i] = bctx->ytq[i];
    mctx->yts[i] = bctx->yts[i];
    ierr = VecCopy(bctx->Q[i], mctx->Q[i]);CHKERRQ(ierr);
  }
  mctx->scale_type = bctx->scale_type;
  mctx->alpha = bctx->alpha;
  mctx->beta = bctx->beta;
  mctx->rho = bctx->rho;
  mctx->sigma_hist = bctx->sigma_hist;
  mctx->watchdog = bctx->watchdog;
  mctx->max_seq_rejects = bctx->max_seq_rejects;
  switch (bctx->scale_type) {
  case SYMBRDN_SCALE_SCALAR:
    mctx->sigma = bctx->sigma;
    break;
  case SYMBRDN_SCALE_DIAG:
    ierr = VecCopy(bctx->invD, mctx->invD);CHKERRQ(ierr);
    break;
  case SYMBRDN_SCALE_NONE:
    mctx->sigma = 1.0;
    break;
  default:
    break;
  }
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

static PetscErrorCode MatReset_LMVMDFP(Mat B, PetscBool destructive)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *ldfp = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;
  
  PetscFunctionBegin;
  ldfp->watchdog = 0;
  ldfp->needQ = PETSC_TRUE;
  if (ldfp->allocated) {
    if (destructive) {
      ierr = VecDestroy(&ldfp->work);CHKERRQ(ierr);
      ierr = PetscFree4(ldfp->ytq, ldfp->yts, ldfp->yty, ldfp->sts);CHKERRQ(ierr);
      ierr = VecDestroyVecs(lmvm->m, &ldfp->Q);CHKERRQ(ierr);
      switch (ldfp->scale_type) {
      case SYMBRDN_SCALE_DIAG:
        ierr = VecDestroy(&ldfp->invDnew);CHKERRQ(ierr);
        ierr = VecDestroy(&ldfp->invD);CHKERRQ(ierr);
        ierr = VecDestroy(&ldfp->BFGS);CHKERRQ(ierr);
        ierr = VecDestroy(&ldfp->DFP);CHKERRQ(ierr);
        ierr = VecDestroy(&ldfp->U);CHKERRQ(ierr);
        ierr = VecDestroy(&ldfp->V);CHKERRQ(ierr);
        ierr = VecDestroy(&ldfp->W);CHKERRQ(ierr);
        break;
      default:
        break;
      }
      ldfp->allocated = PETSC_FALSE;
    } else {
      switch (ldfp->scale_type) {
      case SYMBRDN_SCALE_SCALAR:
        ldfp->sigma = ldfp->delta;
        break;
      case SYMBRDN_SCALE_DIAG:
        ierr = VecSet(ldfp->invD, ldfp->delta);CHKERRQ(ierr);
        break;
      case SYMBRDN_SCALE_NONE:
        ldfp->sigma = 1.0;
        break;
      default:
        break;
      }
    }
  }
  ierr = MatReset_LMVM(B, destructive);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

static PetscErrorCode MatAllocate_LMVMDFP(Mat B, Vec X, Vec F)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *ldfp = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;
  
  PetscFunctionBegin;
  ierr = MatAllocate_LMVM(B, X, F);CHKERRQ(ierr);
  if (!ldfp->allocated) {
    ierr = VecDuplicate(X, &ldfp->work);CHKERRQ(ierr);
    ierr = PetscMalloc4(lmvm->m, &ldfp->ytq, lmvm->m, &ldfp->yts, lmvm->m, &ldfp->yty, lmvm->m, &ldfp->sts);CHKERRQ(ierr);
    if (lmvm->m > 0) {
      ierr = VecDuplicateVecs(X, lmvm->m, &ldfp->Q);CHKERRQ(ierr);
    }
    switch (ldfp->scale_type) {
    case SYMBRDN_SCALE_DIAG:
      ierr = VecDuplicate(X, &ldfp->invDnew);CHKERRQ(ierr);
      ierr = VecDuplicate(X, &ldfp->invD);CHKERRQ(ierr);
      ierr = VecDuplicate(X, &ldfp->BFGS);CHKERRQ(ierr);
      ierr = VecDuplicate(X, &ldfp->DFP);CHKERRQ(ierr);
      ierr = VecDuplicate(X, &ldfp->U);CHKERRQ(ierr);
      ierr = VecDuplicate(X, &ldfp->V);CHKERRQ(ierr);
      ierr = VecDuplicate(X, &ldfp->W);CHKERRQ(ierr);
      break;
    default:
      break;
    }
    ldfp->allocated = PETSC_TRUE;
  }
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

static PetscErrorCode MatDestroy_LMVMDFP(Mat B)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *ldfp = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  if (ldfp->allocated) {
    ierr = VecDestroy(&ldfp->work);CHKERRQ(ierr);
    ierr = PetscFree4(ldfp->ytq, ldfp->yts, ldfp->yty, ldfp->sts);CHKERRQ(ierr);
    ierr = VecDestroyVecs(lmvm->m, &ldfp->Q);CHKERRQ(ierr);
    switch (ldfp->scale_type) {
    case SYMBRDN_SCALE_DIAG:
      ierr = VecDestroy(&ldfp->invDnew);CHKERRQ(ierr);
      ierr = VecDestroy(&ldfp->invD);CHKERRQ(ierr);
      ierr = VecDestroy(&ldfp->BFGS);CHKERRQ(ierr);
      ierr = VecDestroy(&ldfp->DFP);CHKERRQ(ierr);
      ierr = VecDestroy(&ldfp->U);CHKERRQ(ierr);
      ierr = VecDestroy(&ldfp->V);CHKERRQ(ierr);
      ierr = VecDestroy(&ldfp->W);CHKERRQ(ierr);
      break;
    default:
      break;
    }
    ldfp->allocated = PETSC_FALSE;
  }
  ierr = PetscFree(lmvm->ctx);CHKERRQ(ierr);
  ierr = MatDestroy_LMVM(B);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

static PetscErrorCode MatSetUp_LMVMDFP(Mat B)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *ldfp = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;
  
  PetscFunctionBegin;
  ierr = MatSetUp_LMVM(B);CHKERRQ(ierr);
  if (!ldfp->allocated) {
    ierr = VecDuplicate(lmvm->Xprev, &ldfp->work);CHKERRQ(ierr);
    ierr = PetscMalloc4(lmvm->m, &ldfp->ytq, lmvm->m, &ldfp->yts, lmvm->m, &ldfp->yty, lmvm->m, &ldfp->sts);CHKERRQ(ierr);
    if (lmvm->m > 0) {
      ierr = VecDuplicateVecs(lmvm->Xprev, lmvm->m, &ldfp->Q);CHKERRQ(ierr);
    }
    switch (ldfp->scale_type) {
    case SYMBRDN_SCALE_DIAG:
      ierr = VecDuplicate(lmvm->Xprev, &ldfp->invDnew);CHKERRQ(ierr);
      ierr = VecDuplicate(lmvm->Xprev, &ldfp->invD);CHKERRQ(ierr);
      ierr = VecDuplicate(lmvm->Xprev, &ldfp->BFGS);CHKERRQ(ierr);
      ierr = VecDuplicate(lmvm->Xprev, &ldfp->DFP);CHKERRQ(ierr);
      ierr = VecDuplicate(lmvm->Xprev, &ldfp->U);CHKERRQ(ierr);
      ierr = VecDuplicate(lmvm->Xprev, &ldfp->V);CHKERRQ(ierr);
      ierr = VecDuplicate(lmvm->Xprev, &ldfp->W);CHKERRQ(ierr);
      break;
    default:
      break;
    }
    ldfp->allocated = PETSC_TRUE;
  }
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

static PetscErrorCode MatSetFromOptions_LMVMDFP(PetscOptionItems *PetscOptionsObject, Mat B)
{
  Mat_LMVM          *lmvm = (Mat_LMVM*)B->data;
  Mat_SymBrdn       *lsb = (Mat_SymBrdn*)lmvm->ctx;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  ierr = MatSetFromOptions_LMVM(PetscOptionsObject, B);CHKERRQ(ierr);
  ierr = PetscOptionsHead(PetscOptionsObject,"Restricted Broyden method for approximating SPD Jacobian actions (MATLMVMSYMBRDN)");CHKERRQ(ierr);
  ierr = PetscOptionsEList("-mat_lmvm_scale_type", "(developer) scaling type applied to J0", "", Scale_Table, SYMBRDN_SCALE_SIZE, Scale_Table[lsb->scale_type], &lsb->scale_type,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-mat_lmvm_theta","(developer) convex ratio between BFGS and DFP components of the diagonal J0 scaling","",lsb->theta,&lsb->theta,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-mat_lmvm_rho","(developer) update limiter in the J0 scaling","",lsb->rho,&lsb->rho,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-mat_lmvm_alpha","(developer) convex ratio in the J0 scaling","",lsb->alpha,&lsb->alpha,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-mat_lmvm_beta","(developer) exponential factor in the diagonal J0 scaling","",lsb->beta,&lsb->beta,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-mat_lmvm_sigma_hist","(developer) number of past updates to use in the default J0 scalar","",lsb->sigma_hist,&lsb->sigma_hist,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsTail();CHKERRQ(ierr);
  if ((lsb->theta < 0.0) || (lsb->theta > 1.0)) SETERRQ(PetscObjectComm((PetscObject)B), PETSC_ERR_ARG_OUTOFRANGE, "convex ratio for the diagonal J0 scale cannot be outside the range of [0, 1]");
  if ((lsb->alpha < 0.0) || (lsb->alpha > 1.0)) SETERRQ(PetscObjectComm((PetscObject)B), PETSC_ERR_ARG_OUTOFRANGE, "convex ratio in the J0 scaling cannot be outside the range of [0, 1]");
  if ((lsb->rho < 0.0) || (lsb->rho > 1.0)) SETERRQ(PetscObjectComm((PetscObject)B), PETSC_ERR_ARG_OUTOFRANGE, "update limiter in the J0 scaling cannot be outside the range of [0, 1]");
  if (lsb->sigma_hist < 0) SETERRQ(PetscObjectComm((PetscObject)B), PETSC_ERR_ARG_OUTOFRANGE, "J0 scaling history length cannot be negative");
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

PetscErrorCode MatCreate_LMVMDFP(Mat B)
{
  Mat_LMVM          *lmvm;
  Mat_SymBrdn       *ldfp;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  ierr = MatCreate_LMVM(B);CHKERRQ(ierr);
  ierr = PetscObjectChangeTypeName((PetscObject)B, MATLMVMDFP);CHKERRQ(ierr);
  ierr = MatSetOption(B, MAT_SPD, PETSC_TRUE);CHKERRQ(ierr);
  B->ops->view = MatView_LMVMSymBrdn;
  B->ops->setup = MatSetUp_LMVMDFP;
  B->ops->destroy = MatDestroy_LMVMDFP;
  B->ops->setfromoptions = MatSetFromOptions_LMVMDFP;
  B->ops->solve = MatSolve_LMVMDFP;

  lmvm = (Mat_LMVM*)B->data;
  lmvm->square = PETSC_TRUE;
  lmvm->ops->allocate = MatAllocate_LMVMDFP;
  lmvm->ops->reset = MatReset_LMVMDFP;
  lmvm->ops->update = MatUpdate_LMVMDFP;
  lmvm->ops->mult = MatMult_LMVMDFP;
  lmvm->ops->copy = MatCopy_LMVMDFP;

  ierr = PetscNewLog(B, &ldfp);CHKERRQ(ierr);
  lmvm->ctx = (void*)ldfp;
  ldfp->allocated = PETSC_FALSE;
  ldfp->needQ = PETSC_TRUE;
  ldfp->phi = 1.0;
  ldfp->theta = 0.125;
  ldfp->alpha = 1.0;
  ldfp->rho = 1.0;
  ldfp->beta = 0.5;
  ldfp->sigma = 1.0;
  ldfp->delta = 1.0;
  ldfp->delta_min = 1e-7;
  ldfp->delta_max = 100.0;
  ldfp->sigma_hist = 1;
  ldfp->scale_type = SYMBRDN_SCALE_DIAG;
  ldfp->watchdog = 0;
  ldfp->max_seq_rejects = lmvm->m/2;
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

/*@
   MatCreateLMVMDFP - Creates a limited-memory Davidon-Fletcher-Powell (DFP) matrix 
   used for approximating Jacobians. L-DFP is symmetric positive-definite by 
   construction, and is the dual of L-BFGS where Y and S vectors swap roles.
   
   The provided local and global sizes must match the solution and function vectors 
   used with MatLMVMUpdate() and MatSolve(). The resulting L-DFP matrix will have 
   storage vectors allocated with VecCreateSeq() in serial and VecCreateMPI() in 
   parallel. To use the L-DFP matrix with other vector types, the matrix must be 
   created using MatCreate() and MatSetType(), followed by MatLMVMAllocate(). 
   This ensures that the internal storage and work vectors are duplicated from the 
   correct type of vector.

   Collective on MPI_Comm

   Input Parameters:
+  comm - MPI communicator, set to PETSC_COMM_SELF
.  n - number of local rows for storage vectors
-  N - global size of the storage vectors

   Output Parameter:
.  B - the matrix

   It is recommended that one use the MatCreate(), MatSetType() and/or MatSetFromOptions()
   paradigm instead of this routine directly.

   Options Database Keys:
.   -mat_lmvm_num_vecs - maximum number of correction vectors (i.e.: updates) stored
.   -mat_lmvm_scale_type - (developer) type of scaling applied to J0 (none, scalar, diagonal)
.   -mat_lmvm_theta - (developer) convex ratio between BFGS and DFP components of the diagonal J0 scaling
.   -mat_lmvm_rho - (developer) update limiter for the J0 scaling
.   -mat_lmvm_alpha - (developer) coefficient factor for the quadratic subproblem in J0 scaling
.   -mat_lmvm_beta - (developer) exponential factor for the diagonal J0 scaling
.   -mat_lmvm_sigma_hist - (developer) number of past updates to use in J0 scaling

   Level: intermediate

.seealso: MatCreate(), MATLMVM, MATLMVMDFP, MatCreateLMVMBFGS(), MatCreateLMVMSR1(), 
           MatCreateLMVMBrdn(), MatCreateLMVMBadBrdn(), MatCreateLMVMSymBrdn()
@*/
PetscErrorCode MatCreateLMVMDFP(MPI_Comm comm, PetscInt n, PetscInt N, Mat *B)
{
  PetscErrorCode    ierr;
  
  PetscFunctionBegin;
  ierr = MatCreate(comm, B);CHKERRQ(ierr);
  ierr = MatSetSizes(*B, n, n, N, N);CHKERRQ(ierr);
  ierr = MatSetType(*B, MATLMVMDFP);CHKERRQ(ierr);
  ierr = MatSetUp(*B);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
