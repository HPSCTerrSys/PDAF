######################################################
# Include file with machine-specific definitions     #
# for building PDAF.                                 #
#                                                    #
# Variant for Linux with Intel Fortran Compiler      #
# without MPI at AWI                                 #
#                                                    #
# In the case of compilation without MPI, a dummy    #
# implementation of MPI, like provided in the        #
# directory nullmpi/ has to be linked when building  #
# an executable.                                     #
######################################################
# $Id: linux_ifort.h 1395 2013-05-03 13:44:37Z lnerger $

# Compiler, Linker, and Archiver
FC = ${EBROOTPSMPI}/bin/mpif90 # "${EBROOTPSMPI}/bin/mpif90", "scorep-mpif90"
LD = $(FC)
CC = ${EBROOTPSMPI}/bin/mpicc # "${EBROOTPSMPI}/bin/mpicc", "scorep-mpicc"
AR = ar
RANLIB = ranlib 

# C preprocessor
# (only required, if preprocessing is not performed via the compiler)
CPP = 

# Definitions for CPP
# Define USE_PDAF to include PDAF
# (if the compiler does not support get_command_argument()
# from Fortran 2003 you should define F77 here.)
CPP_DEFS = -DUSE_PDAF
##JUWELS?: CPP_DEFS = -DUSE_PDAF -I${MKLROOT}/include

# Optimization specs for compiler
#   (You should explicitly define double precision for floating point
#   variables in the compilation)  
OPT= -O2 -xHost -r8
##OPT= -O2 -xHost -fbacktrace -fdefault-real-8 -falign-commons -fno-automatic -finit-local-zero -mcmodel=large

# Optimization specifications for Linker
OPT_LNK = $(OPT)

# Linking libraries (BLAS, LAPACK, if required: MPI)

LINK_LIBS = -Wl,--start-group  ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_intel_lp64.a ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_intel_thread.a ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_core.a -L${EBROOTPSMPI}/lib64 -Wl,--end-group -qopenmp -lpthread -lm
##JUWELS?:LINK_LIBS = -Wl,--start-group  ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_intel_lp64.a ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_intel_thread.a ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_core.a -L${EBROOTPSMPI}/lib64 -Wl,--end-group -qopenmp -lpthread -lmkl_intel_lp64 -lmkl_sequential -lmkl_core -lm
##LINK_LIBS = -Wl,--start-group ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_intel_lp64.a ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_intel_thread.a ${EBROOTIMKL}/mkl/latest/lib/intel64/libmkl_core.a -L${EBROOTPSMPI}/lib64 -Wl,--end-group -lm -qopenmp -lpthread


# Specifications for the archiver
AR_SPEC = 

# Specifications for ranlib
RAN_SPEC =

# Include path for MPI header file
MPI_INC = -I${EBROOTPSMPI}/include

# Object for nullMPI - if compiled without MPI library
#OBJ_MPI = nullmpi.o

# NetCDF (only required for Lorenz96)
NC_LIB   = 
NC_INC   = 
