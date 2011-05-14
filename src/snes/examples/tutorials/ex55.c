static char help[] = "Allen-Cahn-2d problem for constant mobility and triangular elements.\n\
Runtime options include:\n\
-xmin <xmin>\n\
-xmax <xmax>\n\
-ymin <ymin>\n\
-T <T>, where <T> is the end time for the time domain simulation\n\
-dt <dt>,where <dt> is the step size for the numerical integration\n\
-gamma <gamma>\n\
-theta_c <theta_c>\n\n";

#include "petscsnes.h"
#include "petscdmda.h"

typedef struct{
	PetscReal   dt,T; /* Time step and end time */
	DM          da;
	Mat         M;    /* Jacobian matrix */
	Mat         M_0;
	Vec         q,u1,u2,u3,work1,work2;
	PetscScalar epsilon; /* physics parameters */
	PetscReal   xmin,xmax,ymin,ymax;
}AppCtx;

PetscErrorCode GetParams(AppCtx*);
PetscErrorCode SetVariableBounds(DM,Vec,Vec);
PetscErrorCode SetUpMatrices(AppCtx*);
PetscErrorCode FormFunction(SNES,Vec,Vec,void*);
PetscErrorCode FormJacobian(SNES,Vec,Mat*,Mat*,MatStructure*,void*);
PetscErrorCode SetInitialGuess(Vec,AppCtx*);
PetscErrorCode Update_q(Vec,Vec,Vec,Vec,Mat,AppCtx*);
PetscErrorCode Update_u(Vec,Vec,Vec,Vec);

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc, char **argv)
{
	PetscErrorCode ierr;
	Vec            x,r;  /* Solution and residual vectors */
	SNES           snes; /* Nonlinear solver context */
	AppCtx         user; /* Application context */
	Vec            xl,xu; /* Upper and lower bounds on variables */
	Mat            J;
	PetscScalar    t=0.0;
	
	PetscInitialize(&argc,&argv, (char*)0, help);
	
	/* Get physics and time parameters */
	ierr = GetParams(&user);CHKERRQ(ierr);
	/* Create a 2D DA with dof = 2 */
	ierr = DMDACreate2d(PETSC_COMM_WORLD,DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,DMDA_STENCIL_STAR,-4,-4,PETSC_DECIDE,PETSC_DECIDE,4,1,PETSC_NULL,PETSC_NULL,&user.da);CHKERRQ(ierr);
	/* Set Element type (triangular) */
	ierr = DMDASetElementType(user.da,DMDA_ELEMENT_P1);CHKERRQ(ierr);
	
	/* Set x and y coordinates */
	ierr = DMDASetUniformCoordinates(user.da,user.xmin,user.xmax,user.ymin,user.ymax,0.0,1.0);CHKERRQ(ierr);
	
	/* Get global vector x from DM and duplicate vectors r,xl,xu */
	ierr = DMCreateGlobalVector(user.da,&x);CHKERRQ(ierr);
	ierr = VecDuplicate(x,&r);CHKERRQ(ierr);
	ierr = VecDuplicate(x,&xl);CHKERRQ(ierr);
	ierr = VecDuplicate(x,&xu);CHKERRQ(ierr);
	ierr = VecDuplicate(x,&user.q);CHKERRQ(ierr);
	
	/* Get Jacobian matrix structure from the da */
	ierr = DMGetMatrix(user.da,MATAIJ,&user.M);CHKERRQ(ierr);
	/* Form the jacobian matrix and M_0 */
	ierr = SetUpMatrices(&user);CHKERRQ(ierr);
	ierr = MatDuplicate(user.M,MAT_DO_NOT_COPY_VALUES,&J);CHKERRQ(ierr);
	
	/* Create nonlinear solver context */
	ierr = SNESCreate(PETSC_COMM_WORLD,&snes);CHKERRQ(ierr);
	
	/* Set Function evaluation and jacobian evaluation routines */
	ierr = SNESSetFunction(snes,r,FormFunction,(void*)&user);CHKERRQ(ierr);
	ierr = SNESSetJacobian(snes,J,J,FormJacobian,(void*)&user);CHKERRQ(ierr);
	
	ierr = SNESSetType(snes,SNESVI);CHKERRQ(ierr);
	ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);
	/* Set the boundary conditions */
	ierr = SetVariableBounds(user.da,xl,xu);CHKERRQ(ierr);
	ierr = SNESVISetVariableBounds(snes,xl,xu);CHKERRQ(ierr);
	
	ierr = SetInitialGuess(x,&user);CHKERRQ(ierr);
	/* Begin time loop */
	while(t < user.T) {
		ierr = Update_q(user.q,user.u1,user.u2,user.u3,user.M_0,&user);
		ierr = SNESSolve(snes,PETSC_NULL,x);CHKERRQ(ierr);
		PetscInt its;
		ierr = SNESGetIterationNumber(snes,&its);CHKERRQ(ierr);
		ierr = PetscPrintf(PETSC_COMM_WORLD,"SNESVI solver converged at t = %5.4f in %d iterations\n",t,its);
		ierr = Update_u(user.u1,user.u2,user.u3,x);CHKERRQ(ierr);
		t = t + user.dt;
	}
	
	ierr = VecDestroy(&x);CHKERRQ(ierr);
	ierr = VecDestroy(&r);CHKERRQ(ierr);
	ierr = VecDestroy(&xl);CHKERRQ(ierr);
	ierr = VecDestroy(&xu);CHKERRQ(ierr);
	ierr = VecDestroy(&user.q);CHKERRQ(ierr);
	ierr = VecDestroy(&user.u1);CHKERRQ(ierr);
	ierr = VecDestroy(&user.u2);CHKERRQ(ierr);
	ierr = VecDestroy(&user.u3);CHKERRQ(ierr);
	ierr = VecDestroy(&user.work1);CHKERRQ(ierr);
	ierr = VecDestroy(&user.work2);CHKERRQ(ierr);
	ierr = MatDestroy(&user.M);CHKERRQ(ierr);
	ierr = MatDestroy(&user.M_0);CHKERRQ(ierr);
	ierr = MatDestroy(&J);CHKERRQ(ierr);
	ierr = DMDestroy(&user.da);CHKERRQ(ierr);
	ierr = SNESDestroy(&snes);CHKERRQ(ierr);
	PetscFinalize();
	return 0;
}

#undef __FUNCT__
#define __FUNCT__ "Update_u"
PetscErrorCode Update_u(Vec u1,Vec u2,Vec u3,Vec X)
{
	PetscErrorCode ierr;
	PetscInt       i,n;
	PetscScalar    *u1_arr,*u2_arr,*u3_arr,*x_arr;
	
	PetscFunctionBegin;
	ierr = VecGetLocalSize(u1,&n);CHKERRQ(ierr);
	ierr = VecGetArray(u1,&u1_arr);CHKERRQ(ierr);
	ierr = VecGetArray(u2,&u2_arr);CHKERRQ(ierr);
	ierr = VecGetArray(u3,&u3_arr);CHKERRQ(ierr);
	ierr = VecGetArray(X,&x_arr);CHKERRQ(ierr);
	for(i=0;i<n;i++) {
		u1_arr[i] = x_arr[4*i];
		u2_arr[i] = x_arr[4*i+1];
		u3_arr[i] = x_arr[4*i+2];
	}
	ierr = VecRestoreArray(u1,&u1_arr);CHKERRQ(ierr);
	ierr = VecRestoreArray(u2,&u2_arr);CHKERRQ(ierr);
	ierr = VecRestoreArray(u3,&u3_arr);CHKERRQ(ierr);
	ierr = VecRestoreArray(X,&x_arr);CHKERRQ(ierr);
	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "Update_q"
PetscErrorCode Update_q(Vec q,Vec u1,Vec u2,Vec u3,Mat M_0,AppCtx *user)
{
	PetscErrorCode ierr;
	PetscScalar    *q_arr,*w_arr;
	PetscInt       i,n;
	
	PetscFunctionBegin;
	ierr = VecSet(user->work2,user->dt/3);CHKERRQ(ierr);
	ierr = MatMult(M_0,user->work2,user->work2);CHKERRQ(ierr);
	
	ierr = MatMult(M_0,u1,user->work1);CHKERRQ(ierr);
	ierr = VecScale(user->work1,-1.0-(user->dt));CHKERRQ(ierr);
	ierr = VecAXPY(user->work1,1.0,user->work2);CHKERRQ(ierr);
	ierr = VecGetLocalSize(u1,&n);CHKERRQ(ierr);
	ierr = VecGetArray(q,&q_arr);CHKERRQ(ierr);
	ierr = VecGetArray(user->work1,&w_arr);CHKERRQ(ierr);
	for(i=0;i<n;i++) {
		q_arr[4*i] = w_arr[i];
	}
	
	ierr = MatMult(M_0,u2,user->work1);CHKERRQ(ierr);
	ierr = VecScale(user->work1,-1.0-(user->dt));CHKERRQ(ierr);
	ierr = VecAXPY(user->work1,1.0,user->work2);CHKERRQ(ierr);
	
	for(i=0;i<n;i++) {
		q_arr[4*i+1] = w_arr[i];
	}
	
	ierr = MatMult(M_0,u3,user->work1);CHKERRQ(ierr);
	ierr = VecScale(user->work1,-1.0-(user->dt));CHKERRQ(ierr);
	ierr = VecAXPY(user->work1,1.0,user->work2);CHKERRQ(ierr);
	for(i=0;i<n;i++) {
		q_arr[4*i+2] = w_arr[i];
		q_arr[4*i+3] = 1.0;
	}
	
	ierr = VecRestoreArray(q,&q_arr);CHKERRQ(ierr);
	ierr = VecRestoreArray(user->work1,&w_arr);CHKERRQ(ierr);
	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SetInitialGuess"
PetscErrorCode SetInitialGuess(Vec X,AppCtx* user)
{
	PetscErrorCode ierr;
	PetscScalar    *x,*u1,*u2,*u3;
	PetscInt        n,i;
	Vec             rand;
	
	PetscFunctionBegin;
	/* u = -0.4 + 0.05*rand(N,1)*(rand(N,1) - 0.5) */
	ierr = VecDuplicate(user->u1,&rand);
	ierr = VecSetRandom(rand,PETSC_NULL);
	ierr = VecCopy(rand,user->u1);
	ierr = VecScale(user->u1,0.1);CHKERRQ(ierr);
	ierr = VecShift(user->u1,0.75);CHKERRQ(ierr);
	
	ierr = VecSetRandom(rand,PETSC_NULL);CHKERRQ(ierr);
	ierr = VecCopy(rand,user->u2);
	ierr = VecScale(user->u1,0.25);CHKERRQ(ierr);
	
	
	ierr = VecGetLocalSize(X,&n);CHKERRQ(ierr);
	ierr = VecGetArray(X,&x);CHKERRQ(ierr);
	ierr = VecGetArray(user->u1,&u1);CHKERRQ(ierr);
	ierr = VecGetArray(user->u2,&u2);CHKERRQ(ierr);
	ierr = VecGetArray(user->u3,&u3);CHKERRQ(ierr);
	
	for(i=0;i<n/4;i++) {
		if (u1[i]+u2[i]>1) {
			u2[i]=1-u1[i];
		}
		u3[i]=1.0-u1[i]-u2[i];
	}
	/* Set initial guess, only set value for 2nd dof */
	for(i=0;i<n/4;i++) {
		x[4*i] = u1[i];
		x[4*i+1] = u2[i];
		x[4*i+2] = u3[i];
		x[4*i+3] = 0.0;
	}
	ierr = VecRestoreArray(X,&x);CHKERRQ(ierr);
	ierr = VecRestoreArray(user->u1,&u1);CHKERRQ(ierr);
	ierr = VecRestoreArray(user->u2,&u2);CHKERRQ(ierr);
	ierr = VecRestoreArray(user->u3,&u3);CHKERRQ(ierr);
	
	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "FormFunction"
PetscErrorCode FormFunction(SNES snes,Vec X,Vec F,void* ctx)
{
	PetscErrorCode ierr;
	AppCtx         *user=(AppCtx*)ctx;
	
	PetscFunctionBegin;
	ierr = MatMultAdd(user->M,X,user->q,F);CHKERRQ(ierr);
	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "FormJacobian"
PetscErrorCode FormJacobian(SNES snes,Vec X,Mat *J,Mat *B,MatStructure *flg,void *ctx)
{
	PetscErrorCode ierr;
	AppCtx         *user=(AppCtx*)ctx;
	
	PetscFunctionBegin;
	*flg = SAME_NONZERO_PATTERN;
	ierr = MatCopy(user->M,*J,*flg);CHKERRQ(ierr);
	ierr = MatAssemblyBegin(*J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
	ierr = MatAssemblyEnd(*J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
	PetscFunctionReturn(0);
}
#undef __FUNCT__
#define __FUNCT__ "SetVariableBounds"
PetscErrorCode SetVariableBounds(DM da,Vec xl,Vec xu)
{
	PetscErrorCode ierr;
	PetscScalar    ***l,***u;
	PetscInt       xs,xm,ys,ym;
	PetscInt       j,i;
	
	PetscFunctionBegin;
	ierr = DMDAVecGetArrayDOF(da,xl,&l);CHKERRQ(ierr);
	ierr = DMDAVecGetArrayDOF(da,xu,&u);CHKERRQ(ierr);
	
	ierr = DMDAGetCorners(da,&xs,&ys,PETSC_NULL,&xm,&ym,PETSC_NULL);CHKERRQ(ierr);
	
	for(j=ys; j < ys+ym; j++) {
		for(i=xs; i < xs+xm;i++) {
			l[j][i][0] = 0.0;
			l[j][i][1] = 0.0;
			l[j][i][2] = 0.0;
			l[j][i][3] = -SNES_VI_INF;
			u[j][i][0] = 1.0;
			u[j][i][1] = 1.0;
			u[j][i][2] = 1.0;
			u[j][i][3] = SNES_VI_INF;
		}
	}
	
	ierr = DMDAVecRestoreArrayDOF(da,xl,&l);CHKERRQ(ierr);
	ierr = DMDAVecRestoreArrayDOF(da,xu,&u);CHKERRQ(ierr);
	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "GetParams"
PetscErrorCode GetParams(AppCtx* user)
{
	PetscErrorCode ierr;
	PetscBool      flg;
	
	PetscFunctionBegin;
	
	/* Set default parameters */
	user->xmin = 0.0; user->xmax = 1.0;
	user->ymin = 0.0; user->ymax = 1.0;
	user->T = 0.2;    user->dt = 0.0001;
	user->epsilon = 0;
	
	ierr = PetscOptionsGetReal(PETSC_NULL,"-xmin",&user->xmin,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetReal(PETSC_NULL,"-xmax",&user->xmax,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetReal(PETSC_NULL,"-ymin",&user->ymin,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetReal(PETSC_NULL,"-ymax",&user->ymax,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetReal(PETSC_NULL,"-T",&user->T,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetReal(PETSC_NULL,"-dt",&user->dt,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetScalar(PETSC_NULL,"-epsilon",&user->epsilon,&flg);CHKERRQ(ierr);
	
	PetscFunctionReturn(0);
}

static void Gausspoints(PetscScalar *xx,PetscScalar *yy,PetscScalar *w,PetscScalar *x,PetscScalar *y)
{
	
	xx[0] = 2.0/3.0*x[0] + 1.0/6.0*x[1] + 1.0/6.0*x[2];
	xx[1] = 1.0/6.0*x[0] + 2.0/3.0*x[1] + 1.0/6.0*x[2];
	xx[2] = 1.0/6.0*x[0] + 1.0/6.0*x[1] + 2.0/3.0*x[2];
	
	yy[0] = 2.0/3.0*y[0] + 1.0/6.0*y[1] + 1.0/6.0*y[2];
	yy[1] = 1.0/6.0*y[0] + 2.0/3.0*y[1] + 1.0/6.0*y[2];
	yy[2] = 1.0/6.0*y[0] + 1.0/6.0*y[1] + 2.0/3.0*y[2];
	
	*w = PetscAbsScalar(x[0]*(y[2]-y[1]) + x[2]*(y[1]-y[0]) + x[1]*(y[0]-y[2]))/6.0;
	
}

static void ShapefunctionsT3(PetscScalar *phi,PetscScalar phider[][2],PetscScalar xx,PetscScalar yy,PetscScalar *x,PetscScalar *y)
{
	PetscScalar area,a1,a2,a3,b1,b2,b3,c1,c2,c3,pp;
	
	/* Area of the triangle */
	area = 1.0/2.0*PetscAbsScalar(x[0]*(y[2]-y[1]) + x[2]*(y[1]-y[0]) + x[1]*(y[0]-y[2]));
	
	a1 = x[1]*y[2]-x[2]*y[1]; a2 = x[2]*y[0]-x[0]*y[2]; a3 = x[0]*y[1]-x[1]*y[0];
	b1 = y[1]-y[2]; b2 = y[2]-y[0]; b3 = y[0]-y[1];
	c1 = x[2]-x[1]; c2 = x[0]-x[2]; c3 = x[1]-x[0];
	pp = 1.0/(2.0*area);
	
	/* shape functions */
	phi[0] = pp*(a1 + b1*xx + c1*yy);
	phi[1] = pp*(a2 + b2*xx + c2*yy);
	phi[2] = pp*(a3 + b3*xx + c3*yy);
	
	/* shape functions derivatives */
	phider[0][0] = pp*b1; phider[0][1] = pp*c1;
	phider[1][0] = pp*b2; phider[1][1] = pp*c2;
	phider[2][0] = pp*b3; phider[2][1] = pp*c3;
	
}

#undef __FUNCT__
#define __FUNCT__ "SetUpMatrices"
PetscErrorCode SetUpMatrices(AppCtx* user)
{
	PetscErrorCode    ierr;
	PetscInt          nele,nen,i;
	const PetscInt    *ele;
	PetscScalar       dt=user->dt;
	Vec               coords;
	const PetscScalar *_coords;
	PetscScalar       x[3],y[3],xx[3],yy[3],w;
	PetscInt          idx[3];
	PetscScalar       phi[3],phider[3][2];
	PetscScalar       eM_0[3][3],eM_2[3][3],eID[3][3];
	Mat               M=user->M;
	Mat				  M_0=user->M_0;
	PetscScalar       epsilon=user->epsilon;
	PetscInt n;
	
	
	PetscFunctionBegin;
	/* Get ghosted coordinates */
	ierr = DMDAGetGhostedCoordinates(user->da,&coords);CHKERRQ(ierr);
	ierr = VecGetArrayRead(coords,&_coords);CHKERRQ(ierr);
	
	/* Create the mass matrix M_0 */
	ierr = MatGetLocalSize(M,&n,PETSC_NULL);CHKERRQ(ierr);
	
	ierr = MatCreate(PETSC_COMM_WORLD,&user->M_0);CHKERRQ(ierr);
	ierr = MatSetSizes(user->M_0,n/2,n/2,PETSC_DECIDE,PETSC_DECIDE);CHKERRQ(ierr);
	
	/* Get local element info */
	ierr = DMDAGetElements(user->da,&nele,&nen,&ele);CHKERRQ(ierr);
	for(i=0;i < nele;i++) {
		idx[0] = ele[3*i]; idx[1] = ele[3*i+1]; idx[2] = ele[3*i+2];
		x[0] = _coords[2*idx[0]]; y[0] = _coords[2*idx[0]+1];
		x[1] = _coords[2*idx[1]]; y[1] = _coords[2*idx[1]+1];
		x[2] = _coords[2*idx[2]]; y[2] = _coords[2*idx[2]+1];
		
		ierr = PetscMemzero(xx,3*sizeof(PetscScalar));CHKERRQ(ierr);
		ierr = PetscMemzero(yy,3*sizeof(PetscScalar));CHKERRQ(ierr);
		Gausspoints(xx,yy,&w,x,y);
		
		eM_0[0][0]=eM_0[0][1]=eM_0[0][2]=0.0;
		eM_0[1][0]=eM_0[1][1]=eM_0[1][2]=0.0;
		eM_0[2][0]=eM_0[2][1]=eM_0[2][2]=0.0;
		eM_2[0][0]=eM_2[0][1]=eM_2[0][2]=0.0;
		eM_2[1][0]=eM_2[1][1]=eM_2[1][2]=0.0;
		eM_2[2][0]=eM_2[2][1]=eM_2[2][2]=0.0;
		
		eID[0][0]=eID[1][1]=eID[2][2]=1.0;
		eID[0][1]=eID[0][2]=eID[1][0]=eID[1][2]=eID[2][0]=eID[2][1]=0.0;
		
		PetscInt m;
		for(m=0;m<3;m++) {
			ierr = PetscMemzero(phi,3*sizeof(PetscScalar));CHKERRQ(ierr);
			phider[0][0]=phider[0][1]=0.0;
			phider[1][0]=phider[1][1]=0.0;
			phider[2][0]=phider[2][1]=0.0;
			
			ShapefunctionsT3(phi,phider,xx[m],yy[m],x,y);
			
			PetscInt j,k;
			for(j=0;j<3;j++) {
				for(k=0;k<3;k++) {
					eM_0[k][j] += phi[j]*phi[k]*w;
					eM_2[k][j] += phider[j][0]*phider[k][0]*w + phider[j][1]*phider[k][1]*w;
				}
			}
		}
		PetscInt    row,cols[12],r,row_M_0;
		PetscScalar vals[12],vals_M_0[3];
		
		for(r=0;r<3;r++) {
			row_M_0 = idx[r];
			
			vals_M_0[0]=eM_0[r][0];
			vals_M_0[1]=eM_0[r][1];
			vals_M_0[2]=eM_0[r][2];
			
			ierr = MatSetValuesLocal(M_0,1,&row_M_0,3,idx,vals_M_0,ADD_VALUES);CHKERRQ(ierr);
			
			row = 4*idx[r];
			cols[0] = 4*idx[0];     vals[0] = eM_0[r][0]+dt*epsilon*epsilon*eM_2[r][0];
			cols[1] = 4*idx[0]+1;   vals[1] = 0;
			cols[2] = 4*idx[0]+2;   vals[2] = 0;
			cols[3] = 4*idx[0]+3;   vals[3] = -eID[r][0];
			cols[4] = 4*idx[1];     vals[4] = eM_0[r][1]+dt*epsilon*epsilon*eM_2[r][1];
			cols[5] = 4*idx[1]+1;   vals[5] = 0;
			cols[6] = 4*idx[1]+2;   vals[6] = 0;
			cols[7] = 4*idx[1]+3;   vals[7] = -eID[r][1];
			cols[8] = 4*idx[2];     vals[8] = eM_0[r][2]+dt*epsilon*epsilon*eM_2[r][2];
			cols[9] = 4*idx[2]+1;   vals[9] = 0;
			cols[10] = 4*idx[2]+2;  vals[10] = 0;
			cols[11] = 4*idx[2]+3;  vals[11] = -eID[r][2];
			
			/* Insert values in matrix M for 1st dof */
			ierr = MatSetValuesLocal(M,1,&row,12,cols,vals,ADD_VALUES);CHKERRQ(ierr);
			row = 4*idx[r]+1;
			cols[0] = 4*idx[0];     vals[0] = 0;
			cols[1] = 4*idx[0]+1;   vals[1] = eM_0[r][0]+dt*epsilon*epsilon*eM_2[r][0];
			cols[2] = 4*idx[0]+2;   vals[2] = 0;
			cols[3] = 4*idx[0]+3;   vals[3] = -eID[r][0];
			cols[4] = 4*idx[1];     vals[4] = 0;
			cols[5] = 4*idx[1]+1;   vals[5] = eM_0[r][1]+dt*epsilon*epsilon*eM_2[r][1];
			cols[6] = 4*idx[1]+2;   vals[6] = 0;
			cols[7] = 4*idx[1]+3;   vals[7] = -eID[r][1];
			cols[8] = 4*idx[2];     vals[8] = 0;
			cols[9] = 4*idx[2]+1;   vals[9] = eM_0[r][2]+dt*epsilon*epsilon*eM_2[r][2];
			cols[10] = 4*idx[2]+2;  vals[10] = 0;
			cols[11] = 4*idx[2]+3;  vals[11] = -eID[r][2];
			
			/* Insert values in matrix M for 2nd dof */
			ierr = MatSetValuesLocal(M,1,&row,12,cols,vals,ADD_VALUES);CHKERRQ(ierr);  
			row = 4*idx[r]+2;
			cols[0] = 4*idx[0];     vals[0] = 0;
			cols[1] = 4*idx[0]+1;   vals[1] = 0;
			cols[2] = 4*idx[0]+2;   vals[2] = eM_0[r][0]+dt*epsilon*epsilon*eM_2[r][0];
			cols[3] = 4*idx[0]+3;   vals[3] = -eID[r][0];
			cols[4] = 4*idx[1];     vals[4] = 0;
			cols[5] = 4*idx[1]+1;   vals[5] = 0;
			cols[6] = 4*idx[1]+2;   vals[6] = eM_0[r][1]+dt*epsilon*epsilon*eM_2[r][1];
			cols[7] = 4*idx[1]+3;   vals[7] = -eID[r][1];
			cols[8] = 4*idx[2];     vals[8] = 0;
			cols[9] = 4*idx[2]+1;   vals[9] = 0;
			cols[10] = 4*idx[2]+2;  vals[10] = eM_0[r][2]+dt*epsilon*epsilon*eM_2[r][2];
			cols[11] = 4*idx[2]+3;  vals[11] = -eID[r][2];
			
			/* Insert values in matrix M for 3nd dof */
			ierr = MatSetValuesLocal(M,1,&row,12,cols,vals,ADD_VALUES);CHKERRQ(ierr);  
			row = 4*idx[r]+3;
			cols[0] = 4*idx[0];     vals[0] = -eID[r][0];
			cols[1] = 4*idx[0]+1;   vals[1] = -eID[r][0];
			cols[2] = 4*idx[0]+2;   vals[2] = -eID[r][0];
			cols[3] = 4*idx[0]+3;   vals[3] = 0;
			cols[4] = 4*idx[1];     vals[4] = -eID[r][1];
			cols[5] = 4*idx[1]+1;   vals[5] = -eID[r][1];
			cols[6] = 4*idx[1]+2;   vals[6] = -eID[r][1];
			cols[7] = 4*idx[1]+3;   vals[7] = 0;
			cols[8] = 4*idx[2];     vals[8] = -eID[r][2];
			cols[9] = 4*idx[2]+1;   vals[9] = -eID[r][2];
			cols[10] = 4*idx[2]+2;  vals[10] = -eID[r][2];
			cols[11] = 4*idx[2]+3;  vals[11] = 0;
			
			/* Insert values in matrix M for 4th dof */
			ierr = MatSetValuesLocal(M,1,&row,12,cols,vals,ADD_VALUES);CHKERRQ(ierr);  
			
		}
	}
	
	ierr = DMDARestoreElements(user->da,&nele,&nen,&ele);CHKERRQ(ierr);
	ierr = VecRestoreArrayRead(coords,&_coords);CHKERRQ(ierr);
	
	ierr = MatAssemblyBegin(M,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
	ierr = MatAssemblyEnd(M,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
	
	
	
	ierr = VecCreate(PETSC_COMM_WORLD,&user->u1);CHKERRQ(ierr);
	ierr = VecSetSizes(user->u1,n/4,PETSC_DECIDE);CHKERRQ(ierr);
	ierr = VecSetFromOptions(user->u1);CHKERRQ(ierr);
	ierr = VecDuplicate(user->u1,&user->u2);CHKERRQ(ierr);
	ierr = VecDuplicate(user->u1,&user->u3);CHKERRQ(ierr);
	ierr = VecDuplicate(user->u1,&user->work1);CHKERRQ(ierr);
	ierr = VecDuplicate(user->u1,&user->work2);CHKERRQ(ierr);
	
	PetscFunctionReturn(0);
}
