
static char help[] = "Demonstrates using 3 DA's to manage a slightly non-trivial grid";

#include "petscda.h"
#include "petscsys.h"

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **argv)
{
  PetscMPIInt    rank;
  PetscInt       p1 = 6, p2 = 2, r1 = 3, r2 = 2,r1g,r2g,sw = 1,tonglobal,rstart,x,nx,y,ny,*tonatural,i,j,*to,*from;
  PetscInt       *fromnatural,tomax,fromnglobal,nscat;
  PetscErrorCode ierr;

  /* Each DA manages the local vector for the portion of region 1, 2, and 3 for that processor
     Each DA can belong on any subset (overlapping between DA's or not) of processors
     For processes that a particular DA does not exist on, the corresponding dak should be set to zero */
  DA             da1 = 0,da2 = 0,da3 = 0;
  MPI_Comm       comm1 = 0,comm2 = 0,comm3 = 0;
  Vec            vl1 = 0,vl2 = 0,vl3 = 0, vg1 = 0, vg2 = 0,vg3 = 0;
  PetscScalar    **avl1,**avl2,**avl3;
  AO             toao,fromao;
  IS             tois,fromis;
  Vec            tovec,fromvec;
  VecScatter     vscat;

  ierr = PetscInitialize(&argc,&argv,(char*)0,help);CHKERRQ(ierr); 
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);

  comm1 = PETSC_COMM_WORLD;
  comm2 = PETSC_COMM_WORLD;
  comm3 = PETSC_COMM_WORLD;

  /*
      sw is the stencil width  

      p1 is the width of region 1, p2 the width of region 2
      r1 height of region 1 
      r2 height of region 2
 
      r2 is also the height of region 3-4
      (p1 - p2)/2 is the width of both region 3 and region 4
  */
  r1g = r1 + sw;
  r2g = r2 + sw;
  if (p2 > p1 - 2)   SETERRQ(1,"Width of region p2 must be at least 2 less then width of region 1");
  if ((p2 - p1) % 2) SETERRQ(1,"width of region 3 must be divisible by 2");

  if (comm2) {
    ierr = DACreate2d(comm2,DA_XPERIODIC,DA_STENCIL_BOX,p2,r2g,PETSC_DECIDE,PETSC_DECIDE,1,sw,PETSC_NULL,PETSC_NULL,&da2);CHKERRQ(ierr);
    ierr = DAGetLocalVector(da2,&vl2);CHKERRQ(ierr);
    ierr = DAGetGlobalVector(da2,&vg2);CHKERRQ(ierr);
    ierr = DAVecGetArray(da2,vl2,&avl2);CHKERRQ(ierr);
  }
  if (comm3) {
    ierr = DACreate2d(comm3,DA_NONPERIODIC,DA_STENCIL_BOX,p1-p2,r2g,PETSC_DECIDE,PETSC_DECIDE,1,sw,PETSC_NULL,PETSC_NULL,&da3);CHKERRQ(ierr);
    ierr = DAGetLocalVector(da3,&vl3);CHKERRQ(ierr);
    ierr = DAGetGlobalVector(da3,&vg3);CHKERRQ(ierr);
    ierr = DAVecGetArray(da3,vl3,&avl3);CHKERRQ(ierr);
  }
  if (comm1) {
    ierr = DACreate2d(comm1,DA_NONPERIODIC,DA_STENCIL_BOX,p1,r1g,PETSC_DECIDE,PETSC_DECIDE,1,sw,PETSC_NULL,PETSC_NULL,&da1);CHKERRQ(ierr);
    ierr = DAGetLocalVector(da1,&vl1);CHKERRQ(ierr);
    ierr = DAGetGlobalVector(da1,&vg1);CHKERRQ(ierr);
    ierr = DAVecGetArray(da1,vl1,&avl1);CHKERRQ(ierr);
  }

  /* count the number of unknowns owned on each processor and determine the starting point of each processors ownership 
     for global vector with redundancy */
  tonglobal = 0;
  if (comm2) {
    ierr = DAGetCorners(da2,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    tonglobal += nx*ny;
  }
  if (comm3) {
    ierr = DAGetCorners(da3,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    tonglobal += nx*ny;
  }
  if (comm1) {
    ierr = DAGetCorners(da1,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    tonglobal += nx*ny;
  }
  ierr    = MPI_Scan(&tonglobal,&rstart,1,MPIU_INT,MPI_SUM,PETSC_COMM_WORLD);CHKERRQ(ierr);
  rstart -= tonglobal;
  ierr = PetscSynchronizedPrintf(PETSC_COMM_WORLD,"[%d] Number of unknowns owned %d\n",rank,tonglobal);CHKERRQ(ierr);
  ierr = PetscSynchronizedFlush(PETSC_COMM_WORLD);CHKERRQ(ierr);
  
  /* Get tonatural number for each node */
  ierr = PetscMalloc((tonglobal+1)*sizeof(PetscInt),&tonatural);CHKERRQ(ierr);
  tonglobal = 0;
  tomax   = 0;
  if (comm2) {
    ierr = DAGetCorners(da2,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    tomax += nx;
    for (j=0; j<ny; j++) {
      for (i=0; i<nx; i++) {
        tonatural[tonglobal++] = (p1 - p2)/2 + x + i + p1*(y + j);
      }
    }
  }
  if (comm3) {
    ierr = DAGetCorners(da3,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    tomax += nx;
    for (j=0; j<ny; j++) {
      for (i=0; i<nx; i++) {
        if (x + i < (p1 - p2)/2) tonatural[tonglobal++] = x + i + p1*(y + j);
        else tonatural[tonglobal++] = p2 + x + i + p1*(y + j);
      }
    }
  }
  if (comm1) {
    ierr = DAGetCorners(da1,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    tomax += nx;
    for (j=0; j<ny; j++) {
      for (i=0; i<nx; i++) {
        tonatural[tonglobal++] = p1*r2g + x + i + p1*(y + j);
      }
    }
  }
  /*  ierr = PetscIntView(tonglobal,tonatural,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr); */
  ierr = AOCreateBasic(PETSC_COMM_WORLD,tonglobal,tonatural,0,&toao);CHKERRQ(ierr);
  ierr = PetscFree(tonatural);CHKERRQ(ierr);

  /* count the number of unknowns owned on each processor and determine the starting point of each processors ownership 
     for global vector without redundancy */
  fromnglobal = 0;
  if (comm2) {
    ierr = DAGetCorners(da2,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y+ny == r2g) {ny--;}  /* includes the ghost points on the upper side */
    fromnglobal += nx*ny;
  }
  if (comm3) {
    ierr = DAGetCorners(da3,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y+ny == r2g) {ny--;}  /* includes the ghost points on the upper side */
    fromnglobal += nx*ny;
  }
  if (comm1) {
    ierr = DAGetCorners(da1,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y == 0) {ny--;}  /* includes the ghost points on the lower side */
    fromnglobal += nx*ny;
  }
  ierr    = MPI_Scan(&fromnglobal,&rstart,1,MPIU_INT,MPI_SUM,PETSC_COMM_WORLD);CHKERRQ(ierr);
  rstart -= fromnglobal;
  ierr = PetscSynchronizedPrintf(PETSC_COMM_WORLD,"[%d] Number of unknowns owned %d\n",rank,fromnglobal);CHKERRQ(ierr);
  ierr = PetscSynchronizedFlush(PETSC_COMM_WORLD);CHKERRQ(ierr);

  /* Get fromnatural number for each node */
  ierr = PetscMalloc((fromnglobal+1)*sizeof(PetscInt),&fromnatural);CHKERRQ(ierr);
  fromnglobal = 0;
  if (comm2) {
    ierr = DAGetCorners(da2,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y+ny == r2g) {ny--;}  /* includes the ghost points on the upper side */
    for (j=0; j<ny; j++) {
      for (i=0; i<nx; i++) {
        fromnatural[fromnglobal++] = (p1 - p2)/2 + x + i + p1*(y + j);
      }
    }
  }
  if (comm3) {
    ierr = DAGetCorners(da3,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y+ny == r2g) {ny--;}  /* includes the ghost points on the upper side */
    for (j=0; j<ny; j++) {
      for (i=0; i<nx; i++) {
        if (x + i < (p1 - p2)/2) fromnatural[fromnglobal++] = x + i + p1*(y + j);
        else fromnatural[fromnglobal++] = p2 + x + i + p1*(y + j);
      }
    }
  }
  if (comm1) {
    ierr = DAGetCorners(da1,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y == 0) {ny--;}  /* includes the ghost points on the lower side */
    else y--;
    for (j=0; j<ny; j++) {
      for (i=0; i<nx; i++) {
        fromnatural[fromnglobal++] = p1*r2 + x + i + p1*(y + j);
      }
    }
  }
  /*ierr = PetscIntView(fromnglobal,fromnatural,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);*/
  ierr = AOCreateBasic(PETSC_COMM_WORLD,fromnglobal,fromnatural,0,&fromao);CHKERRQ(ierr);
  ierr = PetscFree(fromnatural);CHKERRQ(ierr);

  /* ---------------------------------------------------*/
  /* Create the scatter that updates 1 from 2 and 3 and 3 and 2 from 1 */
  /* currently handles stencil width of 1 ONLY */
  ierr = PetscMalloc((tomax+1)*sizeof(PetscInt),&to);CHKERRQ(ierr);
  ierr = PetscMalloc((tomax+1)*sizeof(PetscInt),&from);CHKERRQ(ierr);
  nscat = 0;
  if (comm2) {
    ierr = DAGetCorners(da2,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y+ny == r2g) {
      j = ny - 1;
      for (i=0; i<nx; i++) {
        to[nscat] = from[nscat] = (p1 - p2)/2 + x + i + p1*(y + j);nscat++;
      }
    }
  }
  if (comm3) {
    ierr = DAGetCorners(da3,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y+ny == r2g) {
      j = ny - 1;
      for (i=0; i<nx; i++) {
        if (x + i < (p1 - p2)/2) {
          to[nscat]   = from[nscat] = x + i + p1*(y + j);nscat++;
        } else {
          to[nscat]   = from[nscat] = p2 + x + i + p1*(y + j);nscat++;
        }
      }
    }
  }
  if (comm1) {
    ierr = DAGetCorners(da1,&x,&y,0,&nx,&ny,0);CHKERRQ(ierr);
    if (y == 0) {
      j = 0;
      for (i=0; i<nx; i++) {
        to[nscat]     = p1*r2g + x + i + p1*(y + j);
        from[nscat++] = p1*(r2 - 1) + x + i + p1*(y + j);
      }
    }
  }
  ierr = AOPetscToApplication(toao,nscat,to);CHKERRQ(ierr);
  ierr = AOPetscToApplication(fromao,nscat,from);CHKERRQ(ierr);
  ierr = ISCreateGeneral(PETSC_COMM_WORLD,nscat,to,&tois);CHKERRQ(ierr);
  ierr = ISCreateGeneral(PETSC_COMM_WORLD,nscat,from,&fromis);CHKERRQ(ierr);
  ierr = PetscFree(to);CHKERRQ(ierr);
  ierr = PetscFree(from);CHKERRQ(ierr);
  ierr = VecCreateMPI(PETSC_COMM_WORLD,tonglobal,PETSC_DETERMINE,&tovec);CHKERRQ(ierr);
  ierr = VecCreateMPI(PETSC_COMM_WORLD,fromnglobal,PETSC_DETERMINE,&fromvec);CHKERRQ(ierr);
  ierr = VecScatterCreate(fromvec,fromis,tovec,tois,&vscat);CHKERRQ(ierr);
  ierr = ISDestroy(tois);CHKERRQ(ierr);
  ierr = ISDestroy(fromis);CHKERRQ(ierr);
  ierr = AODestroy(fromao);CHKERRQ(ierr);
  ierr = AODestroy(toao);CHKERRQ(ierr);

  ierr = VecScatterDestroy(vscat);CHKERRQ(ierr);
  ierr = VecDestroy(tovec);CHKERRQ(ierr);
  ierr = VecDestroy(fromvec);CHKERRQ(ierr);
  if (comm1) {
    ierr = DAVecRestoreArray(da1,vl1,&avl1);CHKERRQ(ierr);
    ierr = DARestoreLocalVector(da1,&vl1);CHKERRQ(ierr);
    ierr = DARestoreGlobalVector(da1,&vg1);CHKERRQ(ierr);
    ierr = DADestroy(da1);CHKERRQ(ierr);
  }
  if (comm2) {
    ierr = DAVecRestoreArray(da2,vl2,&avl2);CHKERRQ(ierr);
    ierr = DARestoreLocalVector(da2,&vl2);CHKERRQ(ierr);
    ierr = DARestoreGlobalVector(da2,&vg2);CHKERRQ(ierr);
    ierr = DADestroy(da2);CHKERRQ(ierr);
  }
  if (comm3) {
    ierr = DAVecRestoreArray(da3,vl3,&avl3);CHKERRQ(ierr);
    ierr = DARestoreLocalVector(da3,&vl3);CHKERRQ(ierr);
    ierr = DARestoreGlobalVector(da3,&vg3);CHKERRQ(ierr);
    ierr = DADestroy(da3);CHKERRQ(ierr);
  }
  ierr = PetscFinalize();CHKERRQ(ierr);
  return 0;
}
 
