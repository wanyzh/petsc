ALL:

CFLAGS		 =
FFLAGS		 =
CPPFLAGS         =
FPPFLAGS         =
EXAMPLESC        = eptorsion1.c eptorsion2.c minsurf2.c rosenbrock1.c
EXAMPLESF        = eptorsion2f.F rosenbrock1f.F
EXAMPLESCH       =
EXAMPLESFH       = eptorsion2f.h rosenbrock1f.h
LOCDIR		 = src/tao/unconstrained/examples/tutorials/
MANSEC		 =
DOCS		 =
DIRS		 =
CLEANFILES       = eptorsion1 eptorsion2 minsurf2 rosenbrock1 eptorsion2f rosenbrock1f

include ${PETSC_DIR}/conf/variables
include ${PETSC_DIR}/conf/rules

rosenbrock1: rosenbrock1.o chkopts
	-${CLINKER} -o rosenbrock1 rosenbrock1.o ${PETSC_TAO_LIB}
	${RM} rosenbrock1.o

eptorsion2: eptorsion2.o chkopts
	-${CLINKER} -o eptorsion2 eptorsion2.o ${PETSC_TAO_LIB}
	${RM} eptorsion2.o

eptorsion1: eptorsion1.o chkopts
	-${CLINKER} -o eptorsion1 eptorsion1.o ${PETSC_TAO_LIB}
	${RM} eptorsion1.o

rosenbrock1f: rosenbrock1f.o chkopts
	-${FLINKER} -o rosenbrock1f rosenbrock1f.o ${PETSC_TAO_LIB}
	${RM} rosenbrock1f.o

eptorsion2f: eptorsion2f.o chkopts
	-${FLINKER} -o eptorsion2f eptorsion2f.o ${PETSC_TAO_LIB}
	${RM} eptorsion2f.o

minsurf1: minsurf1.o chkopts
	-${CLINKER} -o minsurf1 minsurf1.o ${PETSC_TAO_LIB}
	${RM} minsurf1.o

minsurf2: minsurf2.o chkopts
	-${CLINKER} -o minsurf2 minsurf2.o ${PETSC_TAO_LIB}
	${RM} minsurf2.o

runrosenbrock1:
	-@${MPIEXEC} -n 1 ./rosenbrock1 -tao_smonitor -tao_type nls > rosenbrock1_1.tmp 2>&1;\
	${DIFF} output/rosenbrock1_1.out rosenbrock1_1.tmp || printf '${PWD}\nPossible problem with rosenbrock1 stdout, diffs above \n=========================================\n';\
	${RM} -f rosenbrock1_1.tmp

runrosenbrock1_2:
	-@${MPIEXEC} -n 1 ./rosenbrock1 -tao_smonitor -tao_type lmvm > rosenbrock1_2.tmp 2>&1;\
        ${DIFF} output/rosenbrock1_2.out rosenbrock1_2.tmp || printf '${PWD}\nPossible problem with rosenbrock1_2 stdout, diffs above \n=========================================\n';\
        ${RM} -f rosenbrock1_2.tmp

runrosenbrock1_3:
	-@${MPIEXEC} -n 1 ./rosenbrock1 -tao_smonitor -tao_type ntr > rosenbrock1_3.tmp 2>&1;\
        ${DIFF} output/rosenbrock1_3.out rosenbrock1_3.tmp || printf '${PWD}\nPossible problem with rosenbrock1_3 stdout, diffs above \n=========================================\n';\
        ${RM} -f rosenbrock1_3.tmp

runrosenbrock1f:
	-@${MPIEXEC} -n 1 ./rosenbrock1f -tao_smonitor -tao_type ntr  > rosenbrock1f_1.tmp 2>&1; \
        ${DIFF} output/rosenbrock1f_1.out rosenbrock1f_1.tmp || printf '${PWD}\nPossible problem with rosenbrock1f_1 stdout, diffs above \n=========================================\n';\
        ${RM} -f rosenbrock1f_1.tmp


runminsurf1:
	-@${MPIEXEC} -n 1 ./minsurf1 -tao_smonitor -tao_type nls -mx 10 -my 8 > minsurf1_1.tmp 2>&1;\
	${DIFF} output/minsurf1_1.out minsurf1_1.tmp || printf '${PWD}\nPossible problem with minsurf1 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf1_1.tmp

runminsurf1_2:
	-@${MPIEXEC} -n 1 ./minsurf1 -tao_smonitor -tao_type bmrm -mx 10 -my 8 > minsurf1_2.tmp 2>&1;\
	${DIFF} output/minsurf1_2.out minsurf1_2.tmp || printf '${PWD}\nPossible problem with minsurf1_2 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf1_2.tmp

runminsurf1_3:
	-@${MPIEXEC} -n 1 ./minsurf1 -tao_smonitor -tao_type lmvm -mx 10 -my 8 > minsurf1_3.tmp 2>&1;\
	${DIFF} output/minsurf1_3.out minsurf1_3.tmp || printf '${PWD}\nPossible problem with minsurf1_3 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf1_3.tmp

runminsurf2:
	-@${MPIEXEC} -n 1 ./minsurf2 -tao_smonitor -tao_type lmvm -mx 10 -my 8 > minsurf2_1.tmp 2>&1;\
	${DIFF} output/minsurf2_1.out minsurf2_1.tmp || printf '${PWD}\nPossible problem with minsurf2 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf2_1.tmp

runminsurf2_2:
	-@${MPIEXEC} -n 2 ./minsurf2 -tao_smonitor -tao_type nls > minsurf2_2.tmp 2>&1;\
	${DIFF} output/minsurf2_2.out minsurf2_2.tmp || printf '${PWD}\nPossible problem with minsurf2_2 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf2_2.tmp

runminsurf2_3:
	-@${MPIEXEC} -n 3 ./minsurf2 -tao_smonitor -tao_type cg -tao_cg_type fr -mx 10 -my 10 > minsurf2_3.tmp 2>&1;\
	${DIFF} output/minsurf2_3.out minsurf2_3.tmp || printf '${PWD}\nPossible problem with minsurf2_3 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf2_3.tmp

runminsurf2_4:
	-@${MPIEXEC} -n 2 ./minsurf2 -tao_smonitor -tao_type ntr -mx 6 -my 8 -random 2 > minsurf2_4.tmp 2>&1;\
	${DIFF} output/minsurf2_4.out minsurf2_4.tmp || printf '${PWD}\nPossible problem with minsurf2_4 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf2_4.tmp

runminsurf2_5:
	-@${MPIEXEC} -n 2 ./minsurf2 -tao_smonitor -tao_type bmrm -mx 10 -my 8 > minsurf2_5.tmp 2>&1;\
	${DIFF} output/minsurf2_5.out minsurf2_5.tmp || printf '${PWD}\nPossible problem with minsurf2_5 stdout, diffs above \n=========================================\n';\
	${RM} -f minsurf2_5.tmp


runeptorsion1:
	-@${MPIEXEC} -n 1 ./eptorsion1 -tao_smonitor -tao_type lmvm > eptorsion1_1.tmp 2>&1;\
	${DIFF} output/eptorsion1_1.out eptorsion1_1.tmp || printf '${PWD}\nPossible problem with eptorsion1_1 stdout, diffs above \n=========================================\n';\
	${RM} -f eptorsion1_1.tmp

runeptorsion1_2:
	-@${MPIEXEC} -n 1 ./eptorsion1 -tao_smonitor -tao_type ntl > eptorsion1_2.tmp 2>&1;\
	${DIFF} output/eptorsion1_2.out eptorsion1_2.tmp || printf '${PWD}\nPossible problem with eptorsion1_2 stdout, diffs above \n=========================================\n';\
	${RM} -f eptorsion1_2.tmp

runeptorsion1_3:
	-@${MPIEXEC} -n 1 ./eptorsion1 -tao_smonitor -tao_type ntr > eptorsion1_3.tmp 2>&1;\
	${DIFF} output/eptorsion1_3.out eptorsion1_3.tmp || printf '${PWD}\nPossible problem with eptorsion1_3 stdout, diffs above \n=========================================\n';\
	${RM} -f eptorsion1_3.tmp

runeptorsion2:
	-@${MPIEXEC} -n 1 ./eptorsion2 -tao_smonitor -tao_type lmvm > eptorsion2_1.tmp 2>&1;\
	${DIFF} output/eptorsion2_1.out eptorsion2_1.tmp || printf '${PWD}\nPossible problem with eptorsion2_1 stdout, diffs above \n=========================================\n';\
	${RM} -f eptorsion2_1.tmp


runeptorsion2_2:
	-@${MPIEXEC} -n 2 ./eptorsion2 -tao_smonitor -tao_type nls -mx 16 -my 16 > eptorsion2_2.tmp 2>&1;\
	${DIFF} output/eptorsion2_2.out eptorsion2_2.tmp || printf '${PWD}\nPossible problem with eptorsion2_2 stdout, diffs above \n=========================================\n';\
	${RM} -f eptorsion2_2.tmp


runeptorsion2f:
	-@${MPIEXEC} -n 1 ./eptorsion2f -tao_smonitor -tao_type nls > eptorsion2f_1.tmp 2>&1;\
	${DIFF} output/eptorsion2f_1.out eptorsion2f_1.tmp || printf '${PWD}\nPossible problem with eptorsion2f_1 stdout, diffs above \n=========================================\n';\
	${RM} -f eptorsion2f_1.tmp

runeptorsion2f_2:
	-@${MPIEXEC} -n 2 ./eptorsion2f -tao_smonitor -tao_type lmvm > eptorsion2f_2.tmp 2>&1;\
	${DIFF} output/eptorsion2f_2.out eptorsion2f_2.tmp || printf '${PWD}\nPossible problem with eptorsion2f_2 stdout, diffs above \n=========================================\n';\
	${RM} -f eptorsion2f_2.tmp


TESTEXAMPLES_C    = rosenbrock1.PETSc runrosenbrock1 runrosenbrock1_2 runrosenbrock1_3 rosenbrock1.rm \
                    minsurf1.PETSc runminsurf1 runminsurf1_2 runminsurf1_3 minsurf1.rm \
                    minsurf2.PETSc runminsurf2 runminsurf2_2 runminsurf2_3 runminsurf2_4 runminsurf2_5 minsurf2.rm \
                    eptorsion1.PETSc runeptorsion1 runeptorsion1_2 runeptorsion1_3 eptorsion1.rm \
                    eptorsion2.PETSc runeptorsion2 runeptorsion2_2 eptorsion2.rm

TESTEXAMPLES_FORTRAN =  rosenbrock1f.PETSc runrosenbrock1f rosenbrock1f.rm \
                        eptorsion2f.PETSc runeptorsion2f runeptorsion2f_2 eptorsion2f.rm

TESTEXAMPLES_C_X_MPIUNI = rosenbrock1.PETSc runrosenbrock1 runrosenbrock1_2 runrosenbrock1_3 rosenbrock1.rm \
                    minsurf1.PETSc runminsurf1 runminsurf1_2 runminsurf1_3 minsurf1.rm \
                    eptorsion2.PETSc runeptorsion2 eptorsion2.rm

TESTEXAMPLES_FORTRAN_MPIUNI = rosenbrock1f.PETSc runrosenbrock1f rosenbrock1f.rm \
                        eptorsion2f.PETSc runeptorsion2f eptorsion2f.rm


include ${PETSC_DIR}/conf/test






