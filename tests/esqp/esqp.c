#include "taosolver.h"
#include "petsctime.h"
typedef struct {
  PetscInt n; // Number of variables
  PetscInt m; // Number of constraints
  PetscInt mx; // grid points in each direction
  PetscInt ns; // Number of data samples (1<=ns<=8)
               // Currently only ns=1 is supported
  PetscInt ndata; // Numbers of data points per sample
  IS s_is;
  IS d_is;
  VecScatter state_scatter;
  VecScatter design_scatter;

  Mat Js,Jd,JsPrec;
  PetscBool jformed,dsg_formed;

  PetscReal alpha; // Regularization parameter
  PetscReal beta; // Weight attributed to ||u||^2 in regularization functional
  PetscReal noise; // Amount of noise to add to data
  Mat Q,QT;
  Mat L,LT;
  Mat Div,Divwork;
  Mat Grad;
  Mat Av,Avwork,AvT;
  Mat DSG;
  //Mat Hs,Hd,Hsd;
  Vec q;
  Vec ur; // reference

  Vec d;
  Vec dwork;

  Vec y; // state variables
  Vec ywork;
  Vec ysave;
  Vec ytrue;

  Vec u; // design variables
  Vec uwork;
  Vec usave;
  Vec utrue;
 
  Vec js_diag;
  
  Vec c; // constraint vector
  Vec cwork;
  
  Vec lwork;
  Vec S;
  Vec Swork,Twork;
  Vec Av_u;

} AppCtx;


PetscErrorCode FormFunction(TaoSolver, Vec, PetscReal*, void*);
PetscErrorCode FormGradient(TaoSolver, Vec, Vec, void*);
PetscErrorCode FormFunctionGradient(TaoSolver, Vec, PetscReal*, Vec, void*);
PetscErrorCode FormJacobianState(TaoSolver, Vec, Mat*, Mat*, MatStructure*,void*);
PetscErrorCode FormJacobianDesign(TaoSolver, Vec, Mat*, Mat*, MatStructure*,void*);
PetscErrorCode FormConstraints(TaoSolver, Vec, Vec, void*);
PetscErrorCode FormHessian(TaoSolver, Vec, Mat*, Mat*, MatStructure*, void*);
PetscErrorCode Gather(Vec x, Vec state, VecScatter s_scat, Vec design, VecScatter d_scat);
PetscErrorCode Scatter(Vec x, Vec state, VecScatter s_scat, Vec design, VecScatter d_scat);
PetscErrorCode ESQPInitialize(AppCtx *user);
PetscErrorCode ESQPDestroy(AppCtx *user);
PetscErrorCode ESQPMonitor(TaoSolver, void*);

PetscErrorCode StateMatMult(Mat,Vec,Vec);
PetscErrorCode StateMatGetDiagonal(Mat,Vec); 
PetscErrorCode StateMatDuplicate(Mat,MatDuplicateOption,Mat*);
PetscErrorCode StateMatInvMult(Mat,Vec,Vec,Vec);
PetscErrorCode StateMatPrecMult(Mat,Vec,Vec);
PetscErrorCode StateMatPrecMult2(PC,Vec,Vec);

PetscErrorCode DesignMatMult(Mat,Vec,Vec);
PetscErrorCode DesignMatMultTranspose(Mat,Vec,Vec);

static  char help[]="";

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc, char **argv)
{
  PetscErrorCode ierr;
  Vec x;
  Vec c;
  Vec y;
  
  TaoSolver tao;
  TaoSolverTerminationReason reason;
  AppCtx user;
  IS is_allstate,is_alldesign,is_y;
  PetscInt *idx,lo,hi,i;
  PetscBool flag;
  

  PetscInitialize(&argc, &argv, (char*)0,help);
  TaoInitialize(&argc, &argv, (char*)0,help);

  user.mx = 8;
  ierr = PetscOptionsInt("-mx","Number of grid points in each direction","",user.mx,&user.mx,&flag); CHKERRQ(ierr);
  user.ns = 1;
  ierr = PetscOptionsInt("-ns","Number of data samples (1<=ns<=8)","",user.ns,&user.ns,&flag); CHKERRQ(ierr);
  user.ndata = 64;
  ierr = PetscOptionsInt("-ndata","Numbers of data points per sample","",user.ndata,&user.ndata,&flag); CHKERRQ(ierr);
  user.alpha = 0.1;
  ierr = PetscOptionsReal("-alpha","Regularization parameter","",user.alpha,&user.alpha,&flag); CHKERRQ(ierr);
  user.beta = 0.00001;
  ierr = PetscOptionsReal("-beta","Weight attributed to ||u||^2 in regularization functional","",user.beta,&user.beta,&flag); CHKERRQ(ierr);
  user.noise = 0.01;
  ierr = PetscOptionsReal("-noise","Amount of noise to add to data","",user.noise,&user.noise,&flag); CHKERRQ(ierr);

  user.m = user.mx*user.mx*user.mx; // number of constraints
  user.n = 2*user.m; // number of variables
  /*ierr = PetscMalloc(user.m*sizeof(PetscInt),&idx); CHKERRQ(ierr);
  for (i=0;i<user.m;i++) idx[i]=i;
  ierr = ISCreateGeneral(PETSC_COMM_SELF,user.m,idx,PETSC_COPY_VALUES,&user.s_is); CHKERRQ(ierr);
  ierr = PetscFree(idx); CHKERRQ(ierr);*/
  
  ierr = VecCreate(PETSC_COMM_WORLD,&x); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&c); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&y); CHKERRQ(ierr);
  ierr = VecSetSizes(x,PETSC_DECIDE,user.n); CHKERRQ(ierr);
  ierr = VecSetSizes(c,PETSC_DECIDE,user.m); CHKERRQ(ierr);
  ierr = VecSetSizes(y,PETSC_DECIDE,user.n-user.m); CHKERRQ(ierr);
  ierr = VecSetFromOptions(x); CHKERRQ(ierr);
  ierr = VecSetFromOptions(c); CHKERRQ(ierr);
  ierr = VecSetFromOptions(y); CHKERRQ(ierr);

  //ierr = ISComplement(user.s_is,user.m,user.n,&user.d_is); CHKERRQ(ierr);
  //ISView(user.d_is,PETSC_VIEWER_STDOUT_SELF); 

  ierr = VecCreate(PETSC_COMM_WORLD,&user.u); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user.y); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user.c); CHKERRQ(ierr);
  //ierr = VecCreateSeq(PETSC_COMM_WORLD,user.n-user.m,&user.u); CHKERRQ(ierr);
  //ierr = VecCreateSeq(PETSC_COMM_WORLD,user.m,&user.y); CHKERRQ(ierr);
  //ierr = VecCreateSeq(PETSC_COMM_WORLD,user.m,&user.c); CHKERRQ(ierr);
  //ierr = VecSetType(user.u,VECMPI); CHKERRQ(ierr);
  //ierr = VecSetType(user.y,VECMPI); CHKERRQ(ierr);
  //ierr = VecSetType(user.c,VECMPI); CHKERRQ(ierr);
  ierr = VecSetSizes(user.u,PETSC_DECIDE,user.n-user.m); CHKERRQ(ierr);
  ierr = VecSetSizes(user.y,PETSC_DECIDE,user.m); CHKERRQ(ierr);
  ierr = VecSetSizes(user.c,PETSC_DECIDE,user.m); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user.u); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user.y); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user.c); CHKERRQ(ierr);

  /* Set up initial vectors and matrices */
  ierr = ESQPInitialize(&user); CHKERRQ(ierr);

  /* Create scatters for reduced spaces */
  ierr = VecGetOwnershipRange(user.y,&lo,&hi); CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_SELF,hi-lo,lo,1,&is_allstate); CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_SELF,hi-lo,lo,1,&user.s_is); CHKERRQ(ierr);

  ierr = VecGetOwnershipRange(user.u,&lo,&hi); CHKERRQ(ierr); 
  ierr = ISCreateStride(PETSC_COMM_SELF,hi-lo,lo,1,&is_alldesign); CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_SELF,hi-lo,lo+user.n-user.m,1,&user.d_is); CHKERRQ(ierr);

  ierr = VecScatterCreate(x,user.s_is,user.y,is_allstate,&user.state_scatter); CHKERRQ(ierr);
  ierr = VecScatterCreate(x,user.d_is,user.u,is_alldesign,&user.design_scatter); CHKERRQ(ierr);
  ierr = ISDestroy(&is_alldesign); CHKERRQ(ierr);
  ierr = ISDestroy(&is_allstate); CHKERRQ(ierr);

  ierr = Gather(x,user.y,user.state_scatter,user.u,user.design_scatter); CHKERRQ(ierr);



  /* Create TAO solver and set desired solution method */
  ierr = TaoSolverCreate(PETSC_COMM_WORLD,&tao); CHKERRQ(ierr);
  ierr = TaoSolverSetType(tao,"tao_sqpcon"); CHKERRQ(ierr);
  ierr = TaoSolverSetType(tao,"tao_lcl"); CHKERRQ(ierr);

  //ierr = TaoSolverSetMonitor(tao,ESQPMonitor,&user); CHKERRQ(ierr);

  // Set solution vector with an initial guess 
  /* Set solution vector with an initial guess */
  ierr = TaoSolverSetInitialVector(tao,x); CHKERRQ(ierr);
  ierr = TaoSolverSetObjectiveRoutine(tao, FormFunction, (void *)&user); CHKERRQ(ierr);
  ierr = TaoSolverSetGradientRoutine(tao, FormGradient, (void *)&user); CHKERRQ(ierr);
  ierr = TaoSolverSetConstraintsRoutine(tao, c, FormConstraints, (void *)&user); CHKERRQ(ierr);

  ierr = TaoSolverSetJacobianStateRoutine(tao, user.Js, user.Js, FormJacobianState, (void *)&user); CHKERRQ(ierr);
  ierr = TaoSolverSetJacobianDesignRoutine(tao, user.Jd, user.Jd, FormJacobianDesign, (void *)&user); CHKERRQ(ierr);

  ierr = ISCreateStride(PETSC_COMM_SELF,user.m,0,1,&is_y); CHKERRQ(ierr);

  ierr = TaoSolverSetFromOptions(tao); CHKERRQ(ierr);
  //ierr = TaoSolverLCLSetStateIS(tao,user.s_is); CHKERRQ(ierr);
  //ierr = TaoSolverSQPCONSetStateIS(tao,user.s_is); CHKERRQ(ierr);
  ierr = TaoSolverLCLSetStateIS(tao,is_y); CHKERRQ(ierr);
  ierr = TaoSolverSQPCONSetStateIS(tao,is_y); CHKERRQ(ierr);

  // SOLVE THE APPLICATION 
  /* SOLVE THE APPLICATION */
  ierr = TaoSolverSolve(tao);  CHKERRQ(ierr);

  ierr = TaoSolverGetTerminationReason(tao,&reason); CHKERRQ(ierr);

  if (reason < 0)
  {
    PetscPrintf(MPI_COMM_WORLD, "TAO failed to converge.\n");
  }
  else
  {
    PetscPrintf(MPI_COMM_WORLD, "Optimization terminated with status %2d.\n", reason);
  }


  // Free TAO data structures 
  /* Free TAO data structures */
  ierr = TaoSolverDestroy(&tao); CHKERRQ(ierr);

  /* Free PETSc data structures */
  ierr = VecDestroy(&x); CHKERRQ(ierr);
  ierr = VecDestroy(&c); CHKERRQ(ierr);
  ierr = ESQPDestroy(&user); CHKERRQ(ierr);

  /* Finalize TAO, PETSc */
  TaoFinalize();
  PetscFinalize();

  return 0;
}
/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormFunction"
/* 
   dwork = Qy - d  
   lwork = L*(u-ur)
   f = 1/2 * (dwork.dork + alpha*lwork.lwork)
*/
PetscErrorCode FormFunction(TaoSolver tao,Vec X,PetscReal *f,void *ptr)
{
  PetscErrorCode ierr;
  PetscReal d1=0,d2=0;
  AppCtx *user = (AppCtx*)ptr;
  PetscFunctionBegin;
  ierr = Scatter(X,user->y,user->state_scatter,user->u,user->design_scatter); CHKERRQ(ierr);
  ierr = MatMult(user->Q,user->y,user->dwork); CHKERRQ(ierr);
  ierr = VecAXPY(user->dwork,-1.0,user->d); CHKERRQ(ierr);
  ierr = VecDot(user->dwork,user->dwork,&d1); CHKERRQ(ierr);

  ierr = VecWAXPY(user->uwork,-1.0,user->ur,user->u); CHKERRQ(ierr);
  ierr = MatMult(user->L,user->uwork,user->lwork); CHKERRQ(ierr);
  ierr = VecDot(user->lwork,user->lwork,&d2); CHKERRQ(ierr);
  
  *f = 0.5 * (d1 + user->alpha*d2); 
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormGradient"
/*  
    state: g_s = Q' *(Qy - d)
    design: g_d = alpha*L'*L*(u-ur)
*/
PetscErrorCode FormGradient(TaoSolver tao,Vec X,Vec G,void *ptr)
{
  PetscErrorCode ierr;
  AppCtx *user = (AppCtx*)ptr;
  PetscFunctionBegin;
  CHKMEMQ;
  ierr = Scatter(X,user->y,user->state_scatter,user->u,user->design_scatter); CHKERRQ(ierr);
  ierr = MatMult(user->Q,user->y,user->dwork); CHKERRQ(ierr);
  ierr = VecAXPY(user->dwork,-1.0,user->d); CHKERRQ(ierr);
  //ierr = MatMultTranspose(user->Q,user->dwork,user->ywork); CHKERRQ(ierr);
  ierr = MatMult(user->QT,user->dwork,user->ywork); CHKERRQ(ierr);
  
  ierr = VecWAXPY(user->uwork,-1.0,user->ur,user->u); CHKERRQ(ierr);
  ierr = MatMult(user->L,user->uwork,user->lwork); CHKERRQ(ierr);
  //ierr = MatMultTranspose(user->L,user->lwork,user->uwork); CHKERRQ(ierr);
  ierr = MatMult(user->LT,user->lwork,user->uwork); CHKERRQ(ierr);
  ierr = VecScale(user->uwork, user->alpha); CHKERRQ(ierr);

		      
  ierr = Gather(G,user->ywork,user->state_scatter,user->uwork,user->design_scatter); CHKERRQ(ierr);
  CHKMEMQ;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "FormFunctionGradient"
PetscErrorCode FormFunctionGradient(TaoSolver tao, Vec X, PetscReal *f, Vec G, void *ptr)
{
  PetscErrorCode ierr;
  PetscReal d1,d2;
  AppCtx *user = (AppCtx*)ptr;
  PetscFunctionBegin;
  ierr = Scatter(X,user->y,user->state_scatter,user->u,user->design_scatter); CHKERRQ(ierr);

  ierr = MatMult(user->Q,user->y,user->dwork); CHKERRQ(ierr);
  ierr = VecAXPY(user->dwork,-1.0,user->d); CHKERRQ(ierr);
  ierr = VecDot(user->dwork,user->dwork,&d1); CHKERRQ(ierr);
  //ierr = MatMultTranspose(user->Q,user->dwork,user->ywork); CHKERRQ(ierr);
  ierr = MatMult(user->QT,user->dwork,user->ywork); CHKERRQ(ierr);

  ierr = VecWAXPY(user->uwork,-1.0,user->ur,user->u); CHKERRQ(ierr);
  ierr = MatMult(user->L,user->uwork,user->lwork); CHKERRQ(ierr);
  ierr = VecDot(user->lwork,user->lwork,&d2); CHKERRQ(ierr);
  //ierr = MatMultTranspose(user->L,user->lwork,user->uwork); CHKERRQ(ierr);
  ierr = MatMult(user->LT,user->lwork,user->uwork); CHKERRQ(ierr);
  ierr = VecScale(user->uwork, user->alpha); CHKERRQ(ierr);
  *f = 0.5 * (d1 + user->alpha*d2); 

  
  
  ierr = Gather(G,user->ywork,user->state_scatter,user->uwork,user->design_scatter); CHKERRQ(ierr);
  PetscFunctionReturn(0);

}


/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormJacobianState"
/* A 
MatShell object
*/
PetscErrorCode FormJacobianState(TaoSolver tao, Vec X, Mat *J, Mat* JPre, MatStructure* flag, void *ptr)
{
  PetscErrorCode ierr;
  AppCtx *user = (AppCtx*)ptr;
  PetscFunctionBegin;
  ierr = Scatter(X,user->ysave,user->state_scatter,user->usave,user->design_scatter); CHKERRQ(ierr);
  ierr = VecSet(user->uwork,0); CHKERRQ(ierr);
  ierr = VecAXPY(user->uwork,-1.0,user->usave); CHKERRQ(ierr);
  ierr = VecExp(user->uwork); CHKERRQ(ierr);
  ierr = MatMult(user->Av,user->uwork,user->Av_u); CHKERRQ(ierr);
  ierr = VecCopy(user->Av_u,user->Swork); CHKERRQ(ierr); 
  ierr = VecReciprocal(user->Swork); CHKERRQ(ierr);
  ierr = MatCopy(user->Div,user->Divwork,SAME_NONZERO_PATTERN); CHKERRQ(ierr);
  ierr = MatDiagonalScale(user->Divwork,PETSC_NULL,user->Swork); CHKERRQ(ierr);
  if (user->dsg_formed) {
    ierr = MatMatMult(user->Divwork,user->Grad,MAT_REUSE_MATRIX,1,&user->DSG); CHKERRQ(ierr);
  } else {
    //ierr = MatMatMult(user->Div,user->Grad,MAT_INITIAL_MATRIX,1,&user->DSG); CHKERRQ(ierr);
    ierr = MatMatMult(user->Divwork,user->Grad,MAT_INITIAL_MATRIX,1,&user->DSG); CHKERRQ(ierr);
    user->dsg_formed = PETSC_TRUE;
  }
    
  //*JPre = user->DSG;
  //*flag = SAME_NONZERO_PATTERN;
  
  *JPre = user->JsPrec;
  *flag = DIFFERENT_NONZERO_PATTERN;

  PetscFunctionReturn(0);
}
/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormJacobianDesign"
/* B */
PetscErrorCode FormJacobianDesign(TaoSolver tao, Vec X, Mat *J, Mat* JPre, MatStructure* flag, void *ptr)
{
  PetscErrorCode ierr;
  AppCtx *user = (AppCtx*)ptr;
  PetscFunctionBegin;

  ierr = Scatter(X,user->y,user->state_scatter,user->u,user->design_scatter); CHKERRQ(ierr);
  /*ierr = MatMult(user->Grad,user->y,user->Swork); CHKERRQ(ierr);
  ierr = VecScale(user->u, -1.0); CHKERRQ(ierr);
  ierr = VecExp(user->u); CHKERRQ(ierr);
  ierr = MatMult(user->Av, user->u, user->S); CHKERRQ(ierr);
  ierr = VecPointwiseMult(user->S,user->S,user->S); CHKERRQ(ierr);
  ierr = VecPointwiseDivide(user->Swork,user->Swork,user->S); CHKERRQ(ierr);
  ierr = MatCopy(user->Av,user->Avwork,SAME_NONZERO_PATTERN); CHKERRQ(ierr);
  ierr = MatDiagonalScale(user->Avwork,user->Swork,user->u); CHKERRQ(ierr);
  if (user->jformed) {
    ierr = MatMatMult(user->Div,user->Avwork,MAT_REUSE_MATRIX,1,J); CHKERRQ(ierr);
  }
 
  else {
    user->jformed=PETSC_TRUE;
    ierr = MatMatMult(user->Div,user->Avwork,MAT_INITIAL_MATRIX,1,J); CHKERRQ(ierr);
  }
  *flag = DIFFERENT_NONZERO_PATTERN;*/

  *J = user->Jd;
  *flag = DIFFERENT_NONZERO_PATTERN;

  PetscFunctionReturn(0);
}



#undef __FUNCT__
#define __FUNCT__ "StateMatMult"
PetscErrorCode StateMatMult(Mat J_shell, Vec X, Vec Y) 
{
  PetscErrorCode ierr;
  PetscReal sum;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = MatShellGetContext(J_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;
 
  ierr = MatMult(user->Grad,X,user->Swork); CHKERRQ(ierr);
  ierr = VecPointwiseDivide(user->S,user->Swork,user->Av_u); CHKERRQ(ierr); 
  ierr = MatMult(user->Div,user->S,Y); CHKERRQ(ierr);
  ierr = VecSum(X,&sum); CHKERRQ(ierr);
  sum /= user->m;
  ierr = VecShift(Y,sum); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DesignMatMult"
PetscErrorCode DesignMatMult(Mat J_shell, Vec X, Vec Y) 
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = MatShellGetContext(J_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;
 
  /* sdiag(1./v) */ 
  ierr = VecSet(user->uwork,0); CHKERRQ(ierr);
  ierr = VecAXPY(user->uwork,-1.0,user->u); CHKERRQ(ierr);
  ierr = VecExp(user->uwork); CHKERRQ(ierr);  

  /* sdiag(1./((Av*(1./v)).^2)) */
  ierr = MatMult(user->Av,user->uwork,user->Swork); CHKERRQ(ierr);
  ierr = VecPointwiseMult(user->Swork,user->Swork,user->Swork); CHKERRQ(ierr);
  ierr = VecReciprocal(user->Swork); CHKERRQ(ierr); 

  /* (Av * (sdiag(1./v) * b)) */ 
  ierr = VecPointwiseMult(user->uwork,user->uwork,X); CHKERRQ(ierr);
  ierr = MatMult(user->Av,user->uwork,user->Twork); CHKERRQ(ierr);

  /* (sdiag(1./((Av*(1./v)).^2)) * (Av * (sdiag(1./v) * b))) */
  ierr = VecPointwiseMult(user->Swork,user->Twork,user->Swork); CHKERRQ(ierr); 

  /* (sdiag(Grad*y(:,i)) */
  ierr = MatMult(user->Grad,user->y,user->Twork); CHKERRQ(ierr);
  
  /* Div * (sdiag(Grad*y(:,i)) * (sdiag(1./((Av*(1./v)).^2)) * (Av * (sdiag(1./v) * b)))) */
  ierr = VecPointwiseMult(user->Swork,user->Twork,user->Swork); CHKERRQ(ierr); 
  ierr = MatMult(user->Div,user->Swork,Y); CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DesignMatMultTranspose"
PetscErrorCode DesignMatMultTranspose(Mat J_shell, Vec X, Vec Y) 
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = MatShellGetContext(J_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;

  /* (Div' * b(:,i)) */
  ierr = MatMult(user->Grad,X,user->Swork); CHKERRQ(ierr);

  /* sdiag(Grad*y(:,i)) */
  ierr = MatMult(user->Grad,user->y,user->Twork); CHKERRQ(ierr);

  /* (sdiag(Grad*y(:,i)) * (Div' * b(:,i))) */
  ierr = VecPointwiseMult(user->Twork,user->Swork,user->Twork); CHKERRQ(ierr);
  
  /* sdiag(1./((Av*(1./v)).^2)) */
  ierr = VecSet(user->uwork,0); CHKERRQ(ierr);
  ierr = VecAXPY(user->uwork,-1.0,user->u); CHKERRQ(ierr);
  ierr = VecExp(user->uwork); CHKERRQ(ierr);
  ierr = MatMult(user->Av,user->uwork,user->Swork); CHKERRQ(ierr);
  ierr = VecPointwiseMult(user->Swork,user->Swork,user->Swork); CHKERRQ(ierr);
  ierr = VecReciprocal(user->Swork); CHKERRQ(ierr);

  /* (sdiag(1./((Av*(1./v)).^2)) * (sdiag(Grad*y(:,i)) * (Div' * b(:,i)))) */
  ierr = VecPointwiseMult(user->Swork,user->Twork,user->Swork); CHKERRQ(ierr);

  /* (Av' * (sdiag(1./((Av*(1./v)).^2)) * (sdiag(Grad*y(:,i)) * (Div' * b(:,i))))) */
  ierr = MatMult(user->AvT,user->Swork,Y); CHKERRQ(ierr);
  
  /* sdiag(1./v) * (Av' * (sdiag(1./((Av*(1./v)).^2)) * (sdiag(Grad*y(:,i)) * (Div' * b(:,i))))) */
  ierr = VecPointwiseMult(Y,user->uwork,Y); CHKERRQ(ierr);
  

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "StateMatPrecMult"
PetscErrorCode StateMatPrecMult(Mat PC_shell, Vec X, Vec Y) 
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = MatShellGetContext(PC_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;

  if (user->dsg_formed) {
    //ierr = MatSOR(user->DSG,X,1.0,(SOR_ZERO_INITIAL_GUESS | SOR_SYMMETRIC_SWEEP),0.0,1,1,Y); CHKERRQ(ierr);
    ierr = MatSOR(user->DSG,X,1.0,(SOR_ZERO_INITIAL_GUESS | SOR_LOCAL_SYMMETRIC_SWEEP),0.0,1,1,Y); CHKERRQ(ierr);
    //VecCopy(X,Y);
  }
  else {
    printf("DSG not formed"); abort();
  }

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "StateMatPrecMult2"
/* There has got to be a better way of accomplishing this */
PetscErrorCode StateMatPrecMult2(PC PC_shell, Vec X, Vec Y) 
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = PCShellGetContext(PC_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;

  if (user->dsg_formed) {
    //ierr = MatSOR(user->DSG,X,1.0,(SOR_ZERO_INITIAL_GUESS | SOR_SYMMETRIC_SWEEP),0.0,1,1,Y); CHKERRQ(ierr);
    ierr = MatSOR(user->DSG,X,1.0,(SOR_ZERO_INITIAL_GUESS | SOR_LOCAL_SYMMETRIC_SWEEP),0.0,1,1,Y); CHKERRQ(ierr);
    //VecCopy(X,Y);
  }
  else {
    printf("DSG not formed"); abort();
  }

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "StateMatInvMult"
PetscErrorCode StateMatInvMult(Mat J_shell, Vec X, Vec u, Vec Y)
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  KSP solver;
  PC prec;
  PetscInt n,its;
  KSPConvergedReason reason;
  PetscFunctionBegin;
  ierr = MatShellGetContext(J_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;

  n = user->mx*user->mx*user->mx;

  /* First compute Av_u = Av*exp(-u) */
  ierr = VecSet(user->uwork,0);
  ierr = VecAXPY(user->uwork,-1.0,u);
  ierr = VecExp(user->uwork); CHKERRQ(ierr);
  ierr = MatMult(user->Av,user->uwork,user->Av_u); CHKERRQ(ierr);

  /* Next form DSG = Div*S*Grad */
  ierr = MatCopy(user->Div,user->Divwork,SAME_NONZERO_PATTERN); CHKERRQ(ierr);
  ierr = MatDiagonalScale(user->Divwork,PETSC_NULL,user->Av_u); CHKERRQ(ierr);
  if (user->dsg_formed) {
    ierr = MatMatMult(user->Divwork,user->Grad,MAT_REUSE_MATRIX,1,&user->DSG); CHKERRQ(ierr);
  } else {
    ierr = MatMatMult(user->Div,user->Grad,MAT_INITIAL_MATRIX,1,&user->DSG); CHKERRQ(ierr);
    user->dsg_formed = PETSC_TRUE;
  }

  /* Now StateMatMult can be used.  Solve for ytrue */
  ierr = KSPCreate(PETSC_COMM_WORLD,&solver); CHKERRQ(ierr);
  ierr = KSPSetType(solver,KSPCG); CHKERRQ(ierr);
  ierr = KSPSetOperators(solver,user->Js,user->JsPrec,DIFFERENT_NONZERO_PATTERN); CHKERRQ(ierr);
  ierr = KSPSetInitialGuessNonzero(solver,PETSC_FALSE); CHKERRQ(ierr);
  ierr = KSPSetTolerances(solver,0.0001,0.0001,100000.0,500);
  ierr = KSPGetPC(solver,&prec); CHKERRQ(ierr);
  ierr = PCSetType(prec,PCSHELL); CHKERRQ(ierr);

  ierr = PCShellSetApply(prec,StateMatPrecMult2); CHKERRQ(ierr);
  ierr = PCShellSetApplyTranspose(prec,StateMatPrecMult2); CHKERRQ(ierr);
  ierr = PCShellSetContext(prec,user); CHKERRQ(ierr);

  ierr = KSPSetFromOptions(solver); CHKERRQ(ierr);
  ierr = KSPSetNormType(solver,KSP_NORM_UNPRECONDITIONED); CHKERRQ(ierr);
  ierr = KSPSolve(solver,X,Y); CHKERRQ(ierr);

  KSPGetConvergedReason(solver,&reason);
  if (reason==KSP_DIVERGED_INDEFINITE_PC) {
    PetscPrintf(PETSC_COMM_WORLD,"\nDivergence because of indefinite preconditioner;\n");
    PetscPrintf(PETSC_COMM_WORLD,"Run the executable again but with -pc_factor_shift_positive_definite option.\n");
  } else if (reason<0) {
    PetscPrintf(PETSC_COMM_WORLD,"\nOther kind of divergence: this should not happen.\n");
  } else if (reason==KSP_CONVERGED_RTOL){
    KSPGetIterationNumber(solver,&its);
    PetscPrintf(PETSC_COMM_WORLD,"\nRTOL, Convergence in %d iterations.\n",(int)its);
  } else if (reason==KSP_CONVERGED_ATOL){
    KSPGetIterationNumber(solver,&its);
    PetscPrintf(PETSC_COMM_WORLD,"\nATOL, Convergence in %d iterations.\n",(int)its);
  }

  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "StateMatDuplicate"
PetscErrorCode StateMatDuplicate(Mat J_shell, MatDuplicateOption opt, Mat *new_shell)
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = MatShellGetContext(J_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;

  ierr = MatCreateShell(PETSC_COMM_WORLD,PETSC_DETERMINE,PETSC_DETERMINE,user->m,user->n-user->m,user,new_shell); CHKERRQ(ierr);
  ierr = MatShellSetOperation(*new_shell,MATOP_MULT,(void(*)(void))StateMatMult); CHKERRQ(ierr);
  ierr = MatShellSetOperation(*new_shell,MATOP_DUPLICATE,(void(*)(void))StateMatDuplicate); CHKERRQ(ierr);
  /* Js is symmetric */
  ierr = MatShellSetOperation(*new_shell,MATOP_MULT_TRANSPOSE,(void(*)(void))StateMatMult); CHKERRQ(ierr);
  ierr = MatShellSetOperation(*new_shell,MATOP_GET_DIAGONAL,(void(*)(void))StateMatGetDiagonal); CHKERRQ(ierr);
  ierr = MatSetOption(*new_shell,MAT_SYMMETRY_ETERNAL,PETSC_TRUE); CHKERRQ(ierr);
  ierr = MatSetOption(user->Js,MAT_SYMMETRY_ETERNAL,PETSC_TRUE); CHKERRQ(ierr);
  

  

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "StateMatGetDiagonal"
PetscErrorCode StateMatGetDiagonal(Mat J_shell, Vec X)
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = MatShellGetContext(J_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;
  ierr =  VecCopy(user->js_diag,X); CHKERRQ(ierr);
  PetscFunctionReturn(0);
  
}
/*#undef __FUNCT__
#define __FUNCT__ "DesignMatMult"
PetscErrorCode DesignMatMult(Mat J_shell, Vec X, Vec Y) 
{
  PetscErrorCode ierr;
  void *ptr;
  AppCtx *user;
  PetscFunctionBegin;
  ierr = MatShellGetContext(J_shell,&ptr); CHKERRQ(ierr);
  user = (AppCtx*)ptr;
  ierr = Scatter(X,user->y,user->state_scatter,user->u,user->design_scatter); CHKERRQ(ierr);
  PetscFunctionReturn(0);
  }*/

#undef __FUNCT__
#define __FUNCT__ "FormConstraints"
PetscErrorCode FormConstraints(TaoSolver tao, Vec X, Vec C, void *ptr)
{
  // C=Ay - q      A = Div * Sigma * Av - hx*hx*hx*ones(n,n)
   PetscErrorCode ierr;
   PetscReal sum;
   AppCtx *user = (AppCtx*)ptr;
   PetscFunctionBegin;
   
   ierr = Scatter(X,user->y,user->state_scatter,user->u,user->design_scatter); CHKERRQ(ierr);
   
   ierr = VecScale(user->u,-1.0); CHKERRQ(ierr);
   ierr = VecExp(user->u); CHKERRQ(ierr);
   ierr = MatMult(user->Av,user->u,user->S); CHKERRQ(ierr);

   ierr = MatMult(user->Grad,user->y,user->Swork); CHKERRQ(ierr);
   ierr = VecPointwiseDivide(user->S,user->Swork,user->S); CHKERRQ(ierr);
   ierr = MatMult(user->Div,user->S,C); CHKERRQ(ierr); 
   ierr = VecSum(user->y,&sum); CHKERRQ(ierr);
   sum /= user->m;
   ierr = VecShift(C,sum); CHKERRQ(ierr);

   ierr = VecAXPY(C,-1.0,user->q); CHKERRQ(ierr);

   PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "Scatter"
PetscErrorCode Scatter(Vec x, Vec state, VecScatter s_scat, Vec design, VecScatter d_scat)
{
  PetscErrorCode ierr;
  PetscFunctionBegin;
  ierr = VecScatterBegin(s_scat,x,state,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  ierr = VecScatterEnd(s_scat,x,state,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);

  ierr = VecScatterBegin(d_scat,x,design,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  ierr = VecScatterEnd(d_scat,x,design,INSERT_VALUES,SCATTER_FORWARD); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "Gather"
PetscErrorCode Gather(Vec x, Vec state, VecScatter s_scat, Vec design, VecScatter d_scat)
{
  PetscErrorCode ierr;
  PetscFunctionBegin;
  ierr = VecScatterBegin(s_scat,state,x,INSERT_VALUES,SCATTER_REVERSE); CHKERRQ(ierr);
  ierr = VecScatterEnd(s_scat,state,x,INSERT_VALUES,SCATTER_REVERSE); CHKERRQ(ierr);
  ierr = VecScatterBegin(d_scat,design,x,INSERT_VALUES,SCATTER_REVERSE); CHKERRQ(ierr);
  ierr = VecScatterEnd(d_scat,design,x,INSERT_VALUES,SCATTER_REVERSE); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
  
    
#undef __FUNCT__
#define __FUNCT__ "ESQPInitialize"
PetscErrorCode ESQPInitialize(AppCtx *user)
{
  PetscErrorCode ierr;
  PetscInt m,n,i,j,k,l,linear_index,is,js,ks,ls,istart,iend,iblock;
  PetscInt nnz[user->mx * user->mx * user->mx + 3 * user->mx * user->mx * (user->mx-1)];
  PetscInt data_cols[user->ndata];
  PetscViewer reader;
  Vec XX,YY,ZZ,XXwork,YYwork,ZZwork,UTwork;
  PetscReal x[user->mx],y[user->mx],z[user->mx];
  PetscReal h,meanut;
  PetscScalar PI = 3.141592653589793238,hinv,neg_hinv,half = 0.5,sqrt_beta;
  const PetscScalar *xr,*yr,*zr;
  Mat data_locations;
  PetscInt im,indx1,indx2,indy1,indy2,indz1,indz2,nx,ny,nz;
  PetscReal xri,yri,zri,xim,yim,zim,dx1,dx2,dy1,dy2,dz1,dz2,Dx,Dy,Dz;
  PetscScalar v,vx,vy,vz;

  PetscFunctionBegin;
  user->jformed = PETSC_FALSE;
  user->dsg_formed = PETSC_FALSE;
  //user->alpha = 0.1;
  //user->ns = 1;
  //user->beta = 0.00001;
  //user->noise = 0.01;
  //user->ndata = 64;

  n = user->mx * user->mx * user->mx;
  m = 3 * user->mx * user->mx * (user->mx-1);
  sqrt_beta = sqrt(user->beta);

  //ierr = VecCreateSeq(PETSC_COMM_WORLD,n,&XX); CHKERRQ(ierr);
  //ierr = VecCreateSeq(PETSC_COMM_WORLD,n*user->ns,&user->q); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&XX); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user->q); CHKERRQ(ierr);
  ierr = VecSetSizes(XX,PETSC_DECIDE,n); CHKERRQ(ierr);
  ierr = VecSetSizes(user->q,PETSC_DECIDE,n*user->ns); CHKERRQ(ierr);
  ierr = VecSetFromOptions(XX); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user->q); CHKERRQ(ierr);

  ierr = VecDuplicate(XX,&YY); CHKERRQ(ierr);
  ierr = VecDuplicate(XX,&ZZ); CHKERRQ(ierr);
  ierr = VecDuplicate(XX,&XXwork); CHKERRQ(ierr);
  ierr = VecDuplicate(XX,&YYwork); CHKERRQ(ierr);
  ierr = VecDuplicate(XX,&ZZwork); CHKERRQ(ierr);
  ierr = VecDuplicate(XX,&UTwork); CHKERRQ(ierr);
  ierr = VecDuplicate(XX,&user->utrue); CHKERRQ(ierr);

  /* Generate 3D grid, and collect ns (1<=ns<=8) right-hand-side vectors into user->q */
  h = 1.0/user->mx;
  hinv = user->mx;
  neg_hinv = -hinv;
  for (i=0; i<user->mx; i++){
    for (j=0; j<user->mx; j++){
      for (k=0; k<user->mx; k++){
	linear_index = i + j*user->mx + k*user->mx*user->mx;
	vx = h*(i+0.5); 
	vy = h*(j+0.5);
	vz = h*(k+0.5);	
	ierr = VecSetValues(XX,1,&linear_index,&vx,INSERT_VALUES); CHKERRQ(ierr);
	ierr = VecSetValues(YY,1,&linear_index,&vy,INSERT_VALUES); CHKERRQ(ierr);
	ierr = VecSetValues(ZZ,1,&linear_index,&vz,INSERT_VALUES); CHKERRQ(ierr);
	for (is=0; is<2; is++){
	  for (js=0; js<2; js++){
	    for (ks=0; ks<2; ks++){
	      ls = is*4 + js*2 + ks;
	      if (ls<user->ns){
		l = ls*n + linear_index;
		v = 100*sin(2*PI*(vx+0.25*is))*sin(2*PI*(vy+0.25*js))*sin(2*PI*(vz+0.25*ks));
		ierr = VecSetValues(user->q,1,&l,&v,INSERT_VALUES); CHKERRQ(ierr);
	      }
	    }
	  }
	}
      }
    }
  }

  ierr = VecAssemblyBegin(XX); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(XX); CHKERRQ(ierr);
  ierr = VecAssemblyBegin(YY); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(YY); CHKERRQ(ierr);
  ierr = VecAssemblyBegin(ZZ); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(ZZ); CHKERRQ(ierr);
  ierr = VecAssemblyBegin(user->q); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(user->q); CHKERRQ(ierr);  

  /* Compute true parameter function
     ut = exp(-((x-0.25)^2+(y-0.25)^2+(z-0.25)^2)/0.05) - exp((x-0.75)^2-(y-0.75)^2-(z-0.75))^2/0.05) */
  ierr = VecCopy(XX,XXwork); CHKERRQ(ierr);
  ierr = VecCopy(YY,YYwork); CHKERRQ(ierr);
  ierr = VecCopy(ZZ,ZZwork); CHKERRQ(ierr);

  ierr = VecShift(XXwork,-0.25); CHKERRQ(ierr);
  ierr = VecShift(YYwork,-0.25); CHKERRQ(ierr);
  ierr = VecShift(ZZwork,-0.25); CHKERRQ(ierr);

  ierr = VecPointwiseMult(XXwork,XXwork,XXwork); CHKERRQ(ierr);
  ierr = VecPointwiseMult(YYwork,YYwork,YYwork); CHKERRQ(ierr);
  ierr = VecPointwiseMult(ZZwork,ZZwork,ZZwork); CHKERRQ(ierr);

  ierr = VecCopy(XXwork,UTwork); CHKERRQ(ierr);
  ierr = VecAXPY(UTwork,1.0,YYwork); CHKERRQ(ierr);
  ierr = VecAXPY(UTwork,1.0,ZZwork); CHKERRQ(ierr);
  ierr = VecScale(UTwork,-20.0); CHKERRQ(ierr);
  ierr = VecExp(UTwork); CHKERRQ(ierr);
  ierr = VecCopy(UTwork,user->utrue); CHKERRQ(ierr);

  ierr = VecCopy(XX,XXwork); CHKERRQ(ierr);
  ierr = VecCopy(YY,YYwork); CHKERRQ(ierr);
  ierr = VecCopy(ZZ,ZZwork); CHKERRQ(ierr);

  ierr = VecShift(XXwork,-0.75); CHKERRQ(ierr);
  ierr = VecShift(YYwork,-0.75); CHKERRQ(ierr);
  ierr = VecShift(ZZwork,-0.75); CHKERRQ(ierr);

  ierr = VecPointwiseMult(XXwork,XXwork,XXwork); CHKERRQ(ierr);
  ierr = VecPointwiseMult(YYwork,YYwork,YYwork); CHKERRQ(ierr);
  ierr = VecPointwiseMult(ZZwork,ZZwork,ZZwork); CHKERRQ(ierr);

  ierr = VecCopy(XXwork,UTwork); CHKERRQ(ierr);
  ierr = VecAXPY(UTwork,1.0,YYwork); CHKERRQ(ierr);
  ierr = VecAXPY(UTwork,1.0,ZZwork); CHKERRQ(ierr);
  ierr = VecScale(UTwork,-20.0); CHKERRQ(ierr);
  ierr = VecExp(UTwork); CHKERRQ(ierr);

  ierr = VecAXPY(user->utrue,-1.0,UTwork); CHKERRQ(ierr);
 
  /* Initial guess and reference model */
  ierr = VecDuplicate(user->utrue,&user->ur); CHKERRQ(ierr);
  ierr = VecDuplicate(user->utrue,&user->u); CHKERRQ(ierr);
  ierr = VecSum(user->utrue,&meanut); CHKERRQ(ierr);
  meanut = meanut / n;
  ierr = VecSet(user->ur,meanut); CHKERRQ(ierr);
  ierr = VecCopy(user->ur,user->u); CHKERRQ(ierr);

  /* Generate Grad matrix */
  ierr = MatCreate(PETSC_COMM_WORLD,&user->Grad); CHKERRQ(ierr);
  ierr = MatSetSizes(user->Grad,PETSC_DECIDE,PETSC_DECIDE,m,n); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->Grad); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->Grad,2,PETSC_NULL,2,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->Grad,2,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatGetOwnershipRange(user->Grad,&istart,&iend);

  for (i=istart; i<iend; i++){
    if (i<m/3){
      iblock = i / (user->mx-1);
      j = iblock*user->mx + (i % (user->mx-1));
      ierr = MatSetValues(user->Grad,1,&i,1,&j,&neg_hinv,INSERT_VALUES); CHKERRQ(ierr);
      j = j+1;
      ierr = MatSetValues(user->Grad,1,&i,1,&j,&hinv,INSERT_VALUES); CHKERRQ(ierr);
    }
    if (i>=m/3 && i<2*m/3){
      iblock = (i-m/3) / (user->mx*(user->mx-1));
      j = iblock*user->mx*user->mx + ((i-m/3) % (user->mx*(user->mx-1)));
      ierr = MatSetValues(user->Grad,1,&i,1,&j,&neg_hinv,INSERT_VALUES); CHKERRQ(ierr);
      j = j + user->mx;
      ierr = MatSetValues(user->Grad,1,&i,1,&j,&hinv,INSERT_VALUES); CHKERRQ(ierr);
    }
    if (i>=2*m/3){
      j = i-2*m/3;
      ierr = MatSetValues(user->Grad,1,&i,1,&j,&neg_hinv,INSERT_VALUES); CHKERRQ(ierr);
      j = j + user->mx*user->mx;
      ierr = MatSetValues(user->Grad,1,&i,1,&j,&hinv,INSERT_VALUES); CHKERRQ(ierr);
    }
  }

  ierr = MatAssemblyBegin(user->Grad,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(user->Grad,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);


  /*Vec a,b;
  PetscInt lo,hi;
  PetscLogDouble v1,v2,elapsed_time;
  ierr = VecCreate(PETSC_COMM_WORLD,&a); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&b); CHKERRQ(ierr);
  ierr = VecSetSizes(a,PETSC_DECIDE,n); CHKERRQ(ierr);
  ierr = VecSetSizes(b,PETSC_DECIDE,m); CHKERRQ(ierr);
  ierr = VecSetFromOptions(a); CHKERRQ(ierr);
  ierr = VecSetFromOptions(b); CHKERRQ(ierr);
  ierr = PetscGetTime(&v1); CHKERRQ(ierr);
  for (i=0; i<1; i++){
    ierr = MatMult(user->Grad,a,b); CHKERRQ(ierr);
  }
  ierr = PetscGetTime(&v2); CHKERRQ(ierr);
  PetscPrintf(PETSC_COMM_SELF," %10.8f\n",v2-v1);
  abort();*/


  /* Generate arithmetic averaging matrix Av */
  //ierr = MatDuplicate(user->Grad,MAT_DO_NOT_COPY_VALUES,&user->Av);
  ierr = MatCreate(PETSC_COMM_WORLD,&user->Av); CHKERRQ(ierr);
  ierr = MatSetSizes(user->Av,PETSC_DECIDE,PETSC_DECIDE,m,n); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->Av); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->Av,2,PETSC_NULL,2,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->Av,2,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatGetOwnershipRange(user->Av,&istart,&iend);

  for (i=istart; i<iend; i++){
    if (i<m/3){
      iblock = i / (user->mx-1);
      j = iblock*user->mx + (i % (user->mx-1));
      ierr = MatSetValues(user->Av,1,&i,1,&j,&half,INSERT_VALUES); CHKERRQ(ierr);
      j = j+1;
      ierr = MatSetValues(user->Av,1,&i,1,&j,&half,INSERT_VALUES); CHKERRQ(ierr);
    }
    if (i>=m/3 && i<2*m/3){
      iblock = (i-m/3) / (user->mx*(user->mx-1));
      j = iblock*user->mx*user->mx + ((i-m/3) % (user->mx*(user->mx-1)));
      ierr = MatSetValues(user->Av,1,&i,1,&j,&half,INSERT_VALUES); CHKERRQ(ierr);
      j = j + user->mx;
      ierr = MatSetValues(user->Av,1,&i,1,&j,&half,INSERT_VALUES); CHKERRQ(ierr);
    }
    if (i>=2*m/3){
      j = i-2*m/3;
      ierr = MatSetValues(user->Av,1,&i,1,&j,&half,INSERT_VALUES); CHKERRQ(ierr);
      j = j + user->mx*user->mx;
      ierr = MatSetValues(user->Av,1,&i,1,&j,&half,INSERT_VALUES); CHKERRQ(ierr);
    }
  }

  ierr = MatAssemblyBegin(user->Av,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(user->Av,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);

  /* Generate transpose of averaging matrix Av */
  ierr = MatCreate(PETSC_COMM_WORLD,&user->AvT); CHKERRQ(ierr);
  ierr = MatSetSizes(user->AvT,PETSC_DECIDE,PETSC_DECIDE,n,m); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->AvT); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->AvT,4,PETSC_NULL,4,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->AvT,4,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatTranspose(user->Av,MAT_INITIAL_MATRIX,&user->AvT); CHKERRQ(ierr);


  /* Generate regularization matrix L */
  for (i=0; i<m+n; i++){
    if (i<m){
      nnz[i] = 2; 
    }
    else {
      nnz[i] = 1;
    }
  }

  /*Mat gradeye[2];
  Mat eye;
  ierr = MatCreate(PETSC_COMM_WORLD,&eye); CHKERRQ(ierr);
  ierr = MatSetSizes(eye,PETSC_DECIDE,PETSC_DECIDE,n,n); CHKERRQ(ierr);
  ierr = MatSetFromOptions(eye); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(eye,1,PETSC_NULL,0,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(eye,1,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatZeroEntries(eye); CHKERRQ(ierr);
  ierr = MatAssemblyBegin(eye,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(eye,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatShift(eye,1.0); CHKERRQ(ierr);
  ierr = MatScale(eye,sqrt_beta); CHKERRQ(ierr);
  
  ierr = MatDuplicate(user->Grad,MAT_COPY_VALUES,&gradeye[0]); CHKERRQ(ierr);
  ierr = MatDuplicate(eye,MAT_COPY_VALUES,&gradeye[1]); CHKERRQ(ierr);
  ierr = MatCreateNest(PETSC_COMM_WORLD,2,PETSC_NULL,1,PETSC_NULL,gradeye,&user->L); CHKERRQ(ierr);*/

  ierr = MatCreate(PETSC_COMM_WORLD,&user->L); CHKERRQ(ierr);
  ierr = MatSetSizes(user->L,PETSC_DECIDE,PETSC_DECIDE,m+n,n); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->L); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->L,2,PETSC_NULL,2,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->L,2,nnz); CHKERRQ(ierr);
  ierr = MatGetOwnershipRange(user->L,&istart,&iend);

  for (i=istart; i<iend; i++){
    if (i<m/3){
      iblock = i / (user->mx-1);
      j = iblock*user->mx + (i % (user->mx-1));
      ierr = MatSetValues(user->L,1,&i,1,&j,&neg_hinv,INSERT_VALUES); CHKERRQ(ierr);
      j = j+1;
      ierr = MatSetValues(user->L,1,&i,1,&j,&hinv,INSERT_VALUES); CHKERRQ(ierr);
    }
    if (i>=m/3 && i<2*m/3){
      iblock = (i-m/3) / (user->mx*(user->mx-1));
      j = iblock*user->mx*user->mx + ((i-m/3) % (user->mx*(user->mx-1)));
      ierr = MatSetValues(user->L,1,&i,1,&j,&neg_hinv,INSERT_VALUES); CHKERRQ(ierr);
      j = j + user->mx;
      ierr = MatSetValues(user->L,1,&i,1,&j,&hinv,INSERT_VALUES); CHKERRQ(ierr);
    }
    if (i>=2*m/3 && i<m){
      j = i-2*m/3;
      ierr = MatSetValues(user->L,1,&i,1,&j,&neg_hinv,INSERT_VALUES); CHKERRQ(ierr);
      j = j + user->mx*user->mx;
      ierr = MatSetValues(user->L,1,&i,1,&j,&hinv,INSERT_VALUES); CHKERRQ(ierr);
    }
    if (i>=m){
      j = i - m;
      ierr = MatSetValues(user->L,1,&i,1,&j,&sqrt_beta,INSERT_VALUES); CHKERRQ(ierr);
    }
    }

  ierr = MatAssemblyBegin(user->L,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(user->L,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);

  ierr = MatScale(user->L,pow(h,1.5)); CHKERRQ(ierr);

  /* Generate Div matrix */
  ierr = MatTranspose(user->Grad,MAT_INITIAL_MATRIX,&user->Div);

  /*Mat DivGrad;
  Vec a,b;
  PetscInt lo,hi;
  ierr = MatCreate(PETSC_COMM_WORLD,&DivGrad); CHKERRQ(ierr);
  ierr = MatSetSizes(DivGrad,PETSC_DECIDE,PETSC_DECIDE,n,n); CHKERRQ(ierr);
  ierr = MatSetFromOptions(DivGrad); CHKERRQ(ierr);
  ierr = MatMatMult(user->Div,user->Grad,MAT_INITIAL_MATRIX,1,&DivGrad); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&a); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&b); CHKERRQ(ierr);
  ierr = VecSetSizes(a,PETSC_DECIDE,n); CHKERRQ(ierr);
  ierr = VecSetSizes(b,PETSC_DECIDE,n); CHKERRQ(ierr);
  ierr = VecSetFromOptions(a); CHKERRQ(ierr);
  ierr = VecSetFromOptions(b); CHKERRQ(ierr);
  for (i=0; i<1; i++){
    MatMult(DivGrad,a,b);
  }
  ierr = VecGetOwnershipRange(a,&lo,&hi);
  PetscPrintf(PETSC_COMM_SELF," %i",lo); PetscPrintf(PETSC_COMM_SELF," %i\n",hi);
  ierr = MatGetOwnershipRange(DivGrad,&lo,&hi);
  PetscPrintf(PETSC_COMM_SELF," %i",lo); PetscPrintf(PETSC_COMM_SELF," %i\n",hi);
  PetscFunctionReturn(0);*/


  /* Build work matrices */
  //ierr = VecCreate(PETSC_COMM_WORLD,&user->S); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user->S); CHKERRQ(ierr);
  //ierr = VecSetType(user->S,VECSEQ); CHKERRQ(ierr);
  ierr = VecSetType(user->S,VECMPI); CHKERRQ(ierr);
  ierr = VecSetSizes(user->S, PETSC_DECIDE, user->mx*user->mx*(user->mx-1)*3); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user->S); CHKERRQ(ierr);

  //ierr = VecCreate(PETSC_COMM_WORLD,&user->lwork); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user->lwork); CHKERRQ(ierr);
  //ierr = VecSetType(user->lwork,VECSEQ); CHKERRQ(ierr);
  ierr = VecSetType(user->lwork,VECMPI); CHKERRQ(ierr);
  ierr = VecSetSizes(user->lwork,PETSC_DECIDE,m+user->mx*user->mx*user->mx); CHKERRQ(ierr); 
  ierr = VecSetFromOptions(user->lwork); CHKERRQ(ierr);

  ierr = MatDuplicate(user->Div,MAT_SHARE_NONZERO_PATTERN,&user->Divwork); CHKERRQ(ierr);

  ierr = MatDuplicate(user->Av,MAT_SHARE_NONZERO_PATTERN,&user->Avwork); CHKERRQ(ierr);

  //ierr = VecCreateSeq(PETSC_COMM_WORLD,user->ndata,&user->d); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user->d); CHKERRQ(ierr);
  ierr = VecSetSizes(user->d,PETSC_DECIDE,user->ndata); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user->d); CHKERRQ(ierr);

  ierr = VecDuplicate(user->S,&user->Swork); CHKERRQ(ierr);
  ierr = VecDuplicate(user->S,&user->Av_u); CHKERRQ(ierr);
  ierr = VecDuplicate(user->S,&user->Twork); CHKERRQ(ierr);
  ierr = VecDuplicate(user->y,&user->ywork); CHKERRQ(ierr);
  ierr = VecDuplicate(user->y,&user->ysave); CHKERRQ(ierr);
  ierr = VecDuplicate(user->u,&user->uwork); CHKERRQ(ierr);
  ierr = VecDuplicate(user->u,&user->usave); CHKERRQ(ierr);
  ierr = VecDuplicate(user->u,&user->js_diag); CHKERRQ(ierr);
  ierr = VecDuplicate(user->u,&user->c); CHKERRQ(ierr);
  ierr = VecDuplicate(user->c,&user->cwork); CHKERRQ(ierr);
  ierr = VecDuplicate(user->d,&user->dwork); CHKERRQ(ierr);

  /* Create matrix-free shell user->Js for computing (A + h^3*e*e^T)*x */
  ierr = MatCreateShell(PETSC_COMM_WORLD,PETSC_DETERMINE,PETSC_DETERMINE,user->m,user->n-user->m,user,&user->Js); CHKERRQ(ierr);
  ierr = MatShellSetOperation(user->Js,MATOP_MULT,(void(*)(void))StateMatMult); CHKERRQ(ierr);
  ierr = MatShellSetOperation(user->Js,MATOP_DUPLICATE,(void(*)(void))StateMatDuplicate); CHKERRQ(ierr);
  /* Js is symmetric */
  ierr = MatShellSetOperation(user->Js,MATOP_MULT_TRANSPOSE,(void(*)(void))StateMatMult); CHKERRQ(ierr);
  ierr = MatShellSetOperation(user->Js,MATOP_GET_DIAGONAL,(void(*)(void))StateMatGetDiagonal); CHKERRQ(ierr);
  ierr = MatSetOption(user->Js,MAT_SYMMETRY_ETERNAL,PETSC_TRUE); CHKERRQ(ierr);

  /* Create a matrix-free shell user->JsPrec for computing (U+D)\D*(L+D)\x, where A = L+D+U,
     D is diagonal, L is strictly lower triangular, and U is strictly upper triangular.
     This is an SSOR preconditioner for user->Js. */
  ierr = MatCreateShell(PETSC_COMM_WORLD,PETSC_DETERMINE,PETSC_DETERMINE,user->m,user->n-user->m,user,&user->JsPrec); CHKERRQ(ierr);
  ierr = MatShellSetOperation(user->JsPrec,MATOP_MULT,(void(*)(void))StateMatPrecMult); CHKERRQ(ierr);
  //ierr = MatShellSetOperation(user->JsPrec,MATOP_DUPLICATE,(void(*)(void))StateMatDuplicate); CHKERRQ(ierr);
  /* JsPrec is symmetric */
  ierr = MatShellSetOperation(user->JsPrec,MATOP_MULT_TRANSPOSE,(void(*)(void))StateMatPrecMult); CHKERRQ(ierr);
  //ierr = MatShellSetOperation(user->JsPrec,MATOP_GET_DIAGONAL,(void(*)(void))StateMatGetDiagonal); CHKERRQ(ierr);
  ierr = MatSetOption(user->JsPrec,MAT_SYMMETRY_ETERNAL,PETSC_TRUE); CHKERRQ(ierr);
  
  /* Create a matrix-free shell user->Jd for computing B*x */
  ierr = MatCreateShell(PETSC_COMM_WORLD,PETSC_DETERMINE,PETSC_DETERMINE,user->m,user->m,user,&user->Jd); CHKERRQ(ierr);
  ierr = MatShellSetOperation(user->Jd,MATOP_MULT,(void(*)(void))DesignMatMult); CHKERRQ(ierr);
  //ierr = MatShellSetOperation(user->Jd,MATOP_DUPLICATE,(void(*)(void))StateMatDuplicate); CHKERRQ(ierr);
  ierr = MatShellSetOperation(user->Jd,MATOP_MULT_TRANSPOSE,(void(*)(void))DesignMatMultTranspose); CHKERRQ(ierr);
  //ierr = MatShellSetOperation(user->Js,MATOP_GET_DIAGONAL,(void(*)(void))StateMatGetDiagonal); CHKERRQ(ierr);

  /* Compute true state function yt given ut */
  //ierr = VecCreateSeq(PETSC_COMM_WORLD,n*user->ns,&user->ytrue); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user->ytrue); CHKERRQ(ierr);
  ierr = VecSetSizes(user->ytrue,PETSC_DECIDE,n*user->ns); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user->ytrue); CHKERRQ(ierr);
  ierr = StateMatInvMult(user->Js,user->q,user->utrue,user->ytrue); CHKERRQ(ierr);

  /* Initial guess y0 for state given u0 */
  //ierr = VecCreateSeq(PETSC_COMM_WORLD,n*user->ns,&user->y); CHKERRQ(ierr);
  ierr = VecCreate(PETSC_COMM_WORLD,&user->y); CHKERRQ(ierr);
  ierr = VecSetSizes(user->y,PETSC_DECIDE,n*user->ns); CHKERRQ(ierr);
  ierr = VecSetFromOptions(user->y); CHKERRQ(ierr);
  ierr = StateMatInvMult(user->Js,user->q,user->u,user->y); CHKERRQ(ierr);
  
  /* Construct projection matrix Q */
  ierr = MatCreate(PETSC_COMM_WORLD,&user->Q); CHKERRQ(ierr);
  ierr = MatSetSizes(user->Q,PETSC_DECIDE,PETSC_DECIDE,user->ndata*user->ns,n*user->ns); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->Q); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->Q,8,PETSC_NULL,8,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->Q,8,PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_SELF,"data_locations.dat",FILE_MODE_READ,&reader); CHKERRQ(ierr);
  ierr = MatCreate(PETSC_COMM_SELF,&data_locations); CHKERRQ(ierr);
  ierr = MatLoad(data_locations, reader); CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&reader); CHKERRQ(ierr);
  for (i=0; i<user->ndata; i++){
    data_cols[i] = i;
  }
  ierr = MatGetRow(data_locations,0,&user->ndata,data_cols,&xr); CHKERRQ(ierr);
  ierr = MatRestoreRow(data_locations,0,&user->ndata,data_cols,&xr); CHKERRQ(ierr);
  ierr = MatGetRow(data_locations,1,&user->ndata,data_cols,&yr); CHKERRQ(ierr);
  ierr = MatRestoreRow(data_locations,1,&user->ndata,data_cols,&yr); CHKERRQ(ierr);
  ierr = MatGetRow(data_locations,2,&user->ndata,data_cols,&zr); CHKERRQ(ierr);
  ierr = MatRestoreRow(data_locations,2,&user->ndata,data_cols,&zr); CHKERRQ(ierr);
 
  for (i=0; i<user->mx; i++){
    x[i] = h*(i+0.5);
    y[i] = h*(i+0.5);
    z[i] = h*(i+0.5);
  }
  //ierr = LinInt(x,y,z,xr,yr,zr,user->ndata,user->mx,user->mx,user->mx,&user->Q); CHKERRQ(ierr);
  
  ierr = MatGetOwnershipRange(user->Q,&istart,&iend);

  nx = user->mx; ny = user->mx; nz = user->mx;
  for (i=istart; i<iend; i++){
    
    xri = xr[i];
    im = 0;
    xim = x[im];
    while (xri>xim && im<nx){
      im = im+1;
      xim = x[im];
    }
    indx1 = im-1;
    indx2 = im;
    dx1 = xri - x[indx1];
    dx2 = x[indx2] - xri;

    yri = yr[i];
    im = 0;
    yim = y[im];
    while (yri>yim && im<ny){
      im = im+1;
      yim = y[im];
    }
    indy1 = im-1;
    indy2 = im;
    dy1 = yri - y[indy1];
    dy2 = y[indy2] - yri;
    
    zri = zr[i];
    im = 0;
    zim = z[im];
    while (zri>zim && im<nz){
      im = im+1;
      zim = z[im];
    }
    indz1 = im-1;
    indz2 = im;
    dz1 = zri - z[indz1];
    dz2 = z[indz2] - zri;

    Dx = x[indx2] - x[indx1];
    Dy = y[indy2] - y[indy1];
    Dz = z[indz2] - z[indz1];

    j = indx1 + indy1*nx + indz1*nx*ny;
    v = (1-dx1/Dx)*(1-dy1/Dy)*(1-dz1/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

    j = indx1 + indy1*nx + indz2*nx*ny;
    v = (1-dx1/Dx)*(1-dy1/Dy)*(1-dz2/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

    j = indx1 + indy2*nx + indz1*nx*ny;
    v = (1-dx1/Dx)*(1-dy2/Dy)*(1-dz1/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

    j = indx1 + indy2*nx + indz2*nx*ny;
    v = (1-dx1/Dx)*(1-dy2/Dy)*(1-dz2/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

    j = indx2 + indy1*nx + indz1*nx*ny;
    v = (1-dx2/Dx)*(1-dy1/Dy)*(1-dz1/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

    j = indx2 + indy1*nx + indz2*nx*ny;
    v = (1-dx2/Dx)*(1-dy1/Dy)*(1-dz2/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

    j = indx2 + indy2*nx + indz1*nx*ny;
    v = (1-dx2/Dx)*(1-dy2/Dy)*(1-dz1/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

    j = indx2 + indy2*nx + indz2*nx*ny;
    v = (1-dx2/Dx)*(1-dy2/Dy)*(1-dz2/Dz);
    ierr = MatSetValues(user->Q,1,&i,1,&j,&v,INSERT_VALUES); CHKERRQ(ierr);

  }

  ierr = MatAssemblyBegin(user->Q,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(user->Q,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);  

  /* Generate transpose of Q and L */
  ierr = MatCreate(PETSC_COMM_WORLD,&user->QT); CHKERRQ(ierr);
  ierr = MatSetSizes(user->QT,PETSC_DECIDE,PETSC_DECIDE,n*user->ns,user->ndata*user->ns); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->QT); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->QT,8,PETSC_NULL,8,PETSC_NULL); CHKERRQ(ierr); 
  ierr = MatSeqAIJSetPreallocation(user->QT,8,PETSC_NULL); CHKERRQ(ierr); // 8 is an overestimate
  ierr = MatTranspose(user->Q,MAT_INITIAL_MATRIX,&user->QT); CHKERRQ(ierr);

  ierr = MatCreate(PETSC_COMM_WORLD,&user->LT); CHKERRQ(ierr);
  ierr = MatSetSizes(user->LT,PETSC_DECIDE,PETSC_DECIDE,n,m+n); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->LT); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->LT,7,PETSC_NULL,7,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->LT,7,PETSC_NULL); CHKERRQ(ierr); 
  ierr = MatTranspose(user->L,MAT_INITIAL_MATRIX,&user->LT); CHKERRQ(ierr);

  /* Add noise to the measurement data */
  ierr = VecSet(user->ywork,1.0); CHKERRQ(ierr);
  ierr = VecAYPX(user->ywork,user->noise,user->ytrue); CHKERRQ(ierr);
  ierr = MatMult(user->Q,user->ywork,user->d); CHKERRQ(ierr);


  //ierr = MatCreateSeqAIJ(PETSC_COMM_WORLD,user->m,user->n-user->m,7,PETSC_NULL,&user->Jd); CHKERRQ(ierr);
  //ierr = MatCreateSeqAIJ(PETSC_COMM_WORLD,user->m,user->m,0,PETSC_NULL,&user->Hsd); CHKERRQ(ierr);

  /*ierr = MatCreate(PETSC_COMM_WORLD,&user->Jd); CHKERRQ(ierr);
  ierr = MatSetSizes(user->Jd,PETSC_DECIDE,PETSC_DECIDE,user->m,user->m); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->Jd); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->Jd,7,PETSC_NULL,7,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->Jd,7,PETSC_NULL); CHKERRQ(ierr);*/

  /*ierr = MatCreate(PETSC_COMM_WORLD,&user->Hsd); CHKERRQ(ierr);
  ierr = MatSetSizes(user->Hsd,PETSC_DECIDE,PETSC_DECIDE,user->m,user->m); CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->Hsd); CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(user->Hsd,0,PETSC_NULL,0,PETSC_NULL); CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(user->Hsd,0,PETSC_NULL); CHKERRQ(ierr);

  ierr = MatZeroEntries(user->Hsd); CHKERRQ(ierr);*/

  //ierr = MatMatMultTranspose(user->Q,user->Q,MAT_INITIAL_MATRIX,2.0,&user->Hs); CHKERRQ(ierr);
  //ierr = MatMatMultTranspose(user->L,user->L,MAT_INITIAL_MATRIX,2.0,&user->Hd); CHKERRQ(ierr);
  //ierr = MatScale(user->Hd,user->alpha); CHKERRQ(ierr);

  //ierr = MatMult(user->Jd,user->utrue,user->ur); CHKERRQ(ierr);
  //VecView(user->ur,PETSC_VIEWER_STDOUT_WORLD); abort();


  /* Assemble the matrix */
/*  ierr = MatAssemblyBegin(user->Js,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(user->Js,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyBegin(user->Jd,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(user->Jd,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);*/

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "ESQPDestroy"
PetscErrorCode ESQPDestroy(AppCtx *user)
{
  PetscErrorCode ierr;
  PetscFunctionBegin;
  ierr = MatDestroy(&user->Q); CHKERRQ(ierr);
  ierr = MatDestroy(&user->Div); CHKERRQ(ierr);
  ierr = MatDestroy(&user->Divwork); CHKERRQ(ierr);
  ierr = MatDestroy(&user->Grad); CHKERRQ(ierr);
  ierr = MatDestroy(&user->Av); CHKERRQ(ierr);
  ierr = MatDestroy(&user->Avwork); CHKERRQ(ierr);
  ierr = MatDestroy(&user->L); CHKERRQ(ierr);
  ierr = MatDestroy(&user->Js); CHKERRQ(ierr);
  ierr = MatDestroy(&user->Jd); CHKERRQ(ierr);
  //ierr = MatDestroy(&user->Hs); CHKERRQ(ierr);
  //ierr = MatDestroy(&user->Hd); CHKERRQ(ierr);
  //ierr = MatDestroy(&user->Hsd); CHKERRQ(ierr);
  ierr = VecDestroy(&user->u); CHKERRQ(ierr);
  ierr = VecDestroy(&user->uwork); CHKERRQ(ierr);
  ierr = VecDestroy(&user->usave); CHKERRQ(ierr);
  ierr = VecDestroy(&user->utrue); CHKERRQ(ierr);
  ierr = VecDestroy(&user->y); CHKERRQ(ierr);
  ierr = VecDestroy(&user->ywork); CHKERRQ(ierr);
  ierr = VecDestroy(&user->ysave); CHKERRQ(ierr);
  ierr = VecDestroy(&user->ytrue); CHKERRQ(ierr);
  ierr = VecDestroy(&user->c); CHKERRQ(ierr);
  ierr = VecDestroy(&user->cwork); CHKERRQ(ierr);
  ierr = VecDestroy(&user->ur); CHKERRQ(ierr);
  ierr = VecDestroy(&user->q); CHKERRQ(ierr);
  ierr = VecDestroy(&user->d); CHKERRQ(ierr);
  ierr = VecDestroy(&user->dwork); CHKERRQ(ierr);
  ierr = VecDestroy(&user->lwork); CHKERRQ(ierr);
  ierr = VecDestroy(&user->S); CHKERRQ(ierr);
  ierr = VecDestroy(&user->Swork); CHKERRQ(ierr);
  ierr = VecDestroy(&user->Av_u); CHKERRQ(ierr);
  ierr = VecDestroy(&user->js_diag); CHKERRQ(ierr);
  ierr = ISDestroy(&user->s_is); CHKERRQ(ierr);
  ierr = ISDestroy(&user->d_is); CHKERRQ(ierr);
  ierr = VecScatterDestroy(&user->state_scatter); CHKERRQ(ierr);
  ierr = VecScatterDestroy(&user->design_scatter); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "ESQPMonitor"
PetscErrorCode ESQPMonitor(TaoSolver tao, void *ptr)
{
  PetscErrorCode ierr;
  Vec X;
  PetscReal unorm,ynorm;
  AppCtx *user = (AppCtx*)ptr;
  PetscFunctionBegin;
  ierr = TaoSolverGetSolutionVector(tao,&X); CHKERRQ(ierr);
  ierr = Scatter(X,user->ywork,user->state_scatter,user->uwork,user->design_scatter); CHKERRQ(ierr);
  ierr = VecAXPY(user->ywork,-1.0,user->ytrue); CHKERRQ(ierr);
  ierr = VecAXPY(user->uwork,-1.0,user->utrue); CHKERRQ(ierr);
  ierr = VecNorm(user->uwork,NORM_2,&unorm); CHKERRQ(ierr);
  ierr = VecNorm(user->ywork,NORM_2,&ynorm); CHKERRQ(ierr);
  ierr = PetscPrintf(MPI_COMM_WORLD, "||u-ut||=%7g ||y-yt||=%7g\n",unorm,ynorm); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
