/*$Id: color.h,v 1.4 2000/01/11 21:01:31 bsmith Exp bsmith $*/

#if !defined(_MINPACK_COLOR_H)
#define _MINPACK_COLOR_H

/*
     Prototypes for Minpack coloring routines 
*/
EXTERN int MINPACKdegr(int *,int *,int *,int *,int *,int *,int *);
EXTERN int MINPACKdsm(int *,int *,int *,int *,int *,int *,int *,
                      int *,int *,int *,int *,int *,int *);
EXTERN int MINPACKido(int *,int *,int *,int *,int *,int *,int *,
                      int *,int *,int *,int *,int *,int *);
EXTERN int MINPACKnumsrt(int *,int *,int *,int *,int *,int *,int *);
EXTERN int MINPACKseq(int *,int *,int *,int *,int *,int *,int *,
                      int *,int *);
EXTERN int MINPACKsetr(int*,int*,int*,int*,int*,int*,int*);
EXTERN int MINPACKslo(int *,int *,int *,int *,int *,int *,int *,
                      int *,int *,int *,int *,int *);

#endif
