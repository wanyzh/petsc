#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: dadestroy.c,v 1.13 1997/10/19 03:30:13 bsmith Exp bsmith $";
#endif
 
/*
  Code for manipulating distributed regular arrays in parallel.
*/

#include "src/da/daimpl.h"    /*I   "da.h"   I*/

#undef __FUNC__  
#define __FUNC__ "DADestroy"
/*@C
   DADestroy - Destroys a distributed array.

   Input Parameter:
.  da - the distributed array to destroy 

.keywords: distributed array, destroy

.seealso: DACreate1d(), DACreate2d(), DACreate3d()
@*/int DADestroy(DA da)
{
  int ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(da,DA_COOKIE);
  if (--da->refct > 0) PetscFunctionReturn(0);

  PLogObjectDestroy(da);
  PetscFree(da->idx);
  ierr = VecScatterDestroy(da->ltog);CHKERRQ(ierr);
  ierr = VecScatterDestroy(da->gtol);CHKERRQ(ierr);
  ierr = VecScatterDestroy(da->ltol);CHKERRQ(ierr);
  ierr = VecDestroy(da->global);CHKERRQ(ierr);
  ierr = VecDestroy(da->local);CHKERRQ(ierr);
  ierr = AODestroy(da->ao); CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingDestroy(da->ltogmap); CHKERRQ(ierr);
  if (da->gtog1) PetscFree(da->gtog1);
  if (da->dfshell) {ierr = DFShellDestroy(da->dfshell); CHKERRQ(ierr);}
  PetscHeaderDestroy(da);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DAGetISLocalToGlobalMapping"
/*@C
   DAGetISLocalToGlobalMapping - Accesses the local to global mapping in a DA.

   Input Parameter:
.  da - the distributed array to destroy 

   Output Parameter:
.  ltog - the mapping

.keywords: distributed array, destroy

.seealso: DACreate1d(), DACreate2d(), DACreate3d()
@*/int DAGetISLocalToGlobalMapping(DA da,ISLocalToGlobalMapping *map)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(da,DA_COOKIE);
  *map = da->ltogmap;
  PetscFunctionReturn(0);
}
