/*$Id: mprint.h,v 1.1 2000/05/07 00:41:23 bsmith Exp bsmith $*/

#if !defined(__MPRINT_H)
#define __PRINT_H

#include "petscsys.h"             /*I    "petscsys.h"   I*/
#include <stdarg.h>
#if defined(PETSC_HAVE_STDLIB_H)
#include <stdlib.h>
#endif
#include "petscfix.h"


/* ----------------------------------------------------------------------- */
#define QUEUESTRINGSIZE 1024
typedef struct _PrintfQueue *PrintfQueue;
struct _PrintfQueue {
  char        string[QUEUESTRINGSIZE];
  PrintfQueue next;
};
extern PrintfQueue queue,queuebase;
extern int         queuelength;
extern FILE        *queuefile;

#endif
