/*-----------------------------------------------------------------------------------------
Copyright (c) 2013-2016 by Wolfgang Kurtz and Guowei He (Forschungszentrum Juelich GmbH)

This file is part of TerrSysMP-PDAF

TerrSysMP-PDAF is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TerrSysMP-PDAF is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU LesserGeneral Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with TerrSysMP-PDAF.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------------------------
enkf_parflow.c: Wrapper functions for ParFlow
-------------------------------------------------------------------------------------------*/

#include "parflow.h"
#include "solver.h"

#include "enkf_parflow.h"
#include <string.h>

amps_ThreadLocalDcl(PFModule *, Solver_module);
amps_ThreadLocalDcl(PFModule *, solver);
amps_ThreadLocalDcl(Vector *, evap_trans);
amps_ThreadLocalDcl(Vector *, vpress_dummy);
amps_ThreadLocalDcl(PFModule *, problem);

void init_idx_map_subvec2state(Vector *pf_vector) {
	Grid *grid = VectorGrid(pf_vector);
	int sg;

	// allocate x, y z coords
	xcoord = (double *) malloc(enkf_subvecsize * sizeof(double));
	ycoord = (double *) malloc(enkf_subvecsize * sizeof(double));
	zcoord = (double *) malloc(enkf_subvecsize * sizeof(double));

	// copy dz_mult to double
	ProblemData * problem_data = GetProblemDataRichards(solver);
	Vector * dz_mult = ProblemDataZmult(problem_data);
	Problem * problem = GetProblemRichards(solver);
	PFModule * dz_mult_module = ProblemdzScale(problem);
	char * name;
	name = "dzScale.nzListNumber";

	int num_dz = GetDouble(name);
	int ir;
	double values[num_dz];
	char key[IDB_MAX_KEY_LEN];
	for (ir = 0; ir < num_dz; ir++) {
		sprintf(key, "Cell.%d.dzScale.Value", ir);
		values[ir] = GetDouble(key);
	}
	PF2ENKF(dz_mult, zcoord);

	ForSubgridI(sg, GridSubgrids(grid))
	{
		Subgrid *subgrid = GridSubgrid(grid, sg);

		int ix = SubgridIX(subgrid);
		int iy = SubgridIY(subgrid);
		int iz = SubgridIZ(subgrid);

		int nx = SubgridNX(subgrid);
		int ny = SubgridNY(subgrid);
		int nz = SubgridNZ(subgrid);

		int nx_glob = BackgroundNX(GlobalsBackground);
		int ny_glob = BackgroundNY(GlobalsBackground);

		int i, j, k;
		int counter = 0;

		for (k = iz; k < iz + nz; k++) {
			for (j = iy; j < iy + ny; j++) {
				for (i = ix; i < ix + nx; i++) {
					idx_map_subvec2state[counter] = nx_glob * ny_glob * k
							+ nx_glob * j + i;
					idx_map_subvec2state[counter] += 1; // wolfgang's fix for C -> Fortran index
					xcoord[counter] =
					SubgridX(subgrid) + i * SubgridDX(subgrid);
					ycoord[counter] =
					SubgridY(subgrid) + j * SubgridDY(subgrid);
					zcoord[counter] =
					SubgridZ(subgrid) + k * SubgridDZ(subgrid);
					counter++;
				}
			}
		}
    /* store local dimensions for later use */
    nx_local = nx;
    ny_local = ny;
    nz_local = nz;
	}
}

void PseudoAdvanceRichards(PFModule *this_module, double start_time, /* Starting time */
double stop_time, /* Stopping time */
PFModule *time_step_control, /* Use this module to control timestep if supplied */
Vector *evap_trans, /* Flux from land surface model */
Vector **pressure_out, /* Output vars */
Vector **porosity_out, Vector **saturation_out);
// gw end

void enkfparflowinit(int ac, char *av[], char *input_file) {

	Grid *grid;

	char *filename = input_file;
	MPI_Comm pfcomm;

#ifdef PARFLOW_STAND_ALONE        
	pfcomm = MPI_Comm_f2c(comm_model_pdaf);
#endif

	/*-----------------------------------------------------------------------
	 * Initialize AMPS from existing MPI state
	 *-----------------------------------------------------------------------*/
#ifdef COUP_OAS_PFL
	if (amps_Init(&ac, &av))
	{
#else
	// Parflow stand alone. No need to guard becasue CLM stand alone should not compile this file.
	if (amps_EmbeddedInit_tsmp(pfcomm))
	{
#endif
		amps_Printf("Error: amps_EmbeddedInit initalization failed\n");
		exit(1);
	}

	/*-----------------------------------------------------------------------
	 * Set up globals structure
	 *-----------------------------------------------------------------------*/
	NewGlobals(filename);

	/*-----------------------------------------------------------------------
	 * Read the Users Input Deck
	 *-----------------------------------------------------------------------*/
	amps_ThreadLocal(input_database) = IDB_NewDB(GlobalsInFileName);

	/*-----------------------------------------------------------------------
	 * Setup log printing
	 *-----------------------------------------------------------------------*/
	NewLogging();

	/*-----------------------------------------------------------------------
	 * Setup timing table
	 *-----------------------------------------------------------------------*/
	NewTiming();

	/* End of main includes */

	/* Begin of Solver includes */
	GlobalsNumProcsX = GetIntDefault("Process.Topology.P", 1);
	GlobalsNumProcsY = GetIntDefault("Process.Topology.Q", 1);
	GlobalsNumProcsZ = GetIntDefault("Process.Topology.R", 1);

	GlobalsNumProcs = amps_Size(amps_CommWorld);

	GlobalsBackground = ReadBackground();

	GlobalsUserGrid = ReadUserGrid();

	SetBackgroundBounds(GlobalsBackground, GlobalsUserGrid);

	GlobalsMaxRefLevel = 0;

	amps_ThreadLocal(Solver_module) = PFModuleNewModuleType(
			SolverImpesNewPublicXtraInvoke, SolverRichards, ("Solver"));

	amps_ThreadLocal(solver) = PFModuleNewInstance(
			amps_ThreadLocal(Solver_module), ());
	/* End of solver includes */

	SetupRichards(amps_ThreadLocal(solver));

	/* Create the flow grid */
	grid = CreateGrid(GlobalsUserGrid);

	/* Create the PF vector holding flux */
	amps_ThreadLocal(evap_trans) = NewVectorType(grid, 1, 1,
			vector_cell_centered);
	InitVectorAll(amps_ThreadLocal(evap_trans), 0.0);

	/* kuw: create pf vector for printing results to pfb files */
	amps_ThreadLocal(vpress_dummy) = NewVectorType(grid, 1, 1,
			vector_cell_centered);
	InitVectorAll(amps_ThreadLocal(vpress_dummy), 0.0);
	enkf_subvecsize = enkf_getsubvectorsize(grid);

}

void parflow_oasis_init(double current_time, double dt) {
	double stop_time = current_time + dt;

	Vector *pressure_out;
	Vector *porosity_out;
	Vector *saturation_out;

	VectorUpdateCommHandle *handle;

	handle = InitVectorUpdate(evap_trans, VectorUpdateAll);
	FinalizeVectorUpdate(handle);

	PFModule *time_step_control;

	time_step_control = NewPFModule((void *) SelectTimeStep,
			(void *) SelectTimeStepInitInstanceXtra,
			(void *) SelectTimeStepFreeInstanceXtra,
			(void *) SelectTimeStepNewPublicXtra,
			(void *) SelectTimeStepFreePublicXtra,
			(void *) SelectTimeStepSizeOfTempData, NULL, NULL);

	ThisPFModule = time_step_control;
	SelectTimeStepNewPublicXtra();
	ThisPFModule = NULL;

	PFModule *time_step_control_instance = PFModuleNewInstance(
			time_step_control, ());

	// gw init the OAS, but weird implicit declaration..
	PseudoAdvanceRichards(amps_ThreadLocal(solver), current_time, stop_time,
			time_step_control_instance, amps_ThreadLocal(evap_trans),
			&pressure_out, &porosity_out, &saturation_out);

	PFModuleFreeInstance(time_step_control_instance);
	PFModuleFreeModule(time_step_control);

	// gw: init idx as well
        Problem *problem = GetProblemRichards(solver);
        Vector *pressure_in = GetPressureRichards(solver);
	init_idx_map_subvec2state(pressure_in);
}

void enkfparflowadvance(double current_time, double dt)

{
	double stop_time = current_time + dt;
	int i,j;

	Vector *pressure_out;
	Vector *porosity_out;
	Vector *saturation_out;

	VectorUpdateCommHandle *handle;

	handle = InitVectorUpdate(evap_trans, VectorUpdateAll);
	FinalizeVectorUpdate(handle);



	AdvanceRichards(amps_ThreadLocal(solver), current_time, stop_time, NULL, amps_ThreadLocal(evap_trans), &pressure_out, &porosity_out, &saturation_out);

	handle = InitVectorUpdate(pressure_out, VectorUpdateAll);
	FinalizeVectorUpdate(handle);
	handle = InitVectorUpdate(porosity_out, VectorUpdateAll);
	FinalizeVectorUpdate(handle);
	handle = InitVectorUpdate(saturation_out, VectorUpdateAll);
	FinalizeVectorUpdate(handle);

	// to state vector
	if(pf_updateflag == 1) {
  	  PF2ENKF(pressure_out, subvec_p);
  	  //PF2ENKF(saturation_out, subvec_sat);
  	  //PF2ENKF(porosity_out, subvec_porosity);
  	  //PF2ENKF(pressure_out, subvec_pressure_backup);
  	  for(i=0;i<enkf_subvecsize;i++) pf_statevec[i] = subvec_p[i];
        }

	if(pf_updateflag == 2){
	  //printf("PARFLOW: enkf_parflow_c: writing out saturation to state vector in PDAF\n");
	  PF2ENKF(saturation_out, subvec_sat);
	  PF2ENKF(porosity_out, subvec_porosity);
	  for(i=0;i<enkf_subvecsize;i++) pf_statevec[i] = subvec_sat[i] * subvec_porosity[i];
	}

        if(pf_updateflag == 3){
          PF2ENKF(pressure_out, subvec_p);
          PF2ENKF(saturation_out, subvec_sat);
          PF2ENKF(porosity_out, subvec_porosity);
	  for(i=0;i<enkf_subvecsize;i++) pf_statevec[i] = subvec_sat[i] * subvec_porosity[i];
          //for(i=enkf_subvecsize,j=0;i<pf_statevecsize;i++,j++) pf_statevec[i] = subvec_p[j];
          for(i=enkf_subvecsize,j=0;i<(2*enkf_subvecsize);i++,j++) pf_statevec[i] = subvec_p[j];
        }
       
        if(pf_paramupdate == 1){
           ProblemData *problem_data = GetProblemDataRichards(solver);
           Vector      *perm_xx = ProblemDataPermeabilityX(problem_data);
           handle = InitVectorUpdate(perm_xx, VectorUpdateAll);
           FinalizeVectorUpdate(handle);
           PF2ENKF(perm_xx,subvec_param);
           for(i=(pf_statevecsize-enkf_subvecsize),j=0;i<pf_statevecsize;i++,j++) pf_statevec[i] = log10(subvec_param[j]);
        }
        if(pf_paramupdate == 2){
           ProblemData *problem_data = GetProblemDataRichards(solver);
           Vector       *mannings    = ProblemDataMannings(problem_data);
           handle = InitVectorUpdate(mannings, VectorUpdateAll);
           FinalizeVectorUpdate(handle);
           PF2ENKF(mannings,subvec_param);
           for(i=(pf_statevecsize-pf_paramvecsize),j=0;i<pf_statevecsize;i++,j++) pf_statevec[i] = log10(subvec_param[j]);
        }
}

void enkfparflowfinalize() {

	// gw: free assimilation data structures
	free(idx_map_subvec2state);
	free(xcoord);
	free(ycoord);
	free(zcoord);

	fflush(NULL);
	LogGlobals();
	PrintTiming();
	FreeLogging();
	FreeTiming();
	FreeGlobals();
	amps_Finalize();
}

int enkf_getsubvectorsize(Grid *grid) {
	int sg;
	int out = 0;
	ForSubgridI(sg, GridSubgrids(grid))
	{
		Subgrid *subgrid = GridSubgrid(grid, sg);
		int nx = SubgridNX(subgrid);
		int ny = SubgridNY(subgrid);
		int nz = SubgridNZ(subgrid);
		out += nx * ny * nz; // kuw: correct?
	}
	return (out);
}

void PF2ENKF(Vector *pf_vector, double *enkf_subvec) {

	Grid *grid = VectorGrid(pf_vector);
	int sg;

	ForSubgridI(sg, GridSubgrids(grid))
	{
		Subgrid *subgrid = GridSubgrid(grid, sg);

		int ix = SubgridIX(subgrid);
		int iy = SubgridIY(subgrid);
		int iz = SubgridIZ(subgrid);

		int nx = SubgridNX(subgrid);
		int ny = SubgridNY(subgrid);
		int nz = SubgridNZ(subgrid);


		Subvector *subvector = VectorSubvector(pf_vector, sg);
		double *subvector_data = SubvectorData(subvector);

		int i, j, k;
		int counter = 0;

		for (k = iz; k < iz + nz; k++) {
			for (j = iy; j < iy + ny; j++) {
				for (i = ix; i < ix + nx; i++) {
					int pf_index = SubvectorEltIndex(subvector, i, j, k);
					enkf_subvec[counter] = (subvector_data[pf_index]);
					counter++;
				}
			}
		}
	}
}

void ENKF2PF(Vector *pf_vector, double *enkf_subvec) {

	Grid *grid = VectorGrid(pf_vector);
	int sg;

	ForSubgridI(sg, GridSubgrids(grid))
	{
		Subgrid *subgrid = GridSubgrid(grid, sg);

		int ix = SubgridIX(subgrid);
		int iy = SubgridIY(subgrid);
		int iz = SubgridIZ(subgrid);

		int nx = SubgridNX(subgrid);
		int ny = SubgridNY(subgrid);
		int nz = SubgridNZ(subgrid);

		Subvector *subvector = VectorSubvector(pf_vector, sg);
		double *subvector_data = SubvectorData(subvector);

		int i, j, k;
		int counter = 0;

		for (k = iz; k < iz + nz; k++) {
			for (j = iy; j < iy + ny; j++) {
				for (i = ix; i < ix + nx; i++) {
					int pf_index = SubvectorEltIndex(subvector, i, j, k);
					(subvector_data[pf_index]) = enkf_subvec[counter];
					counter++;
				}
			}
		}
	}
}

void enkf_printvec(char *pre, char *suff, double *data) {
	Grid *grid = VectorGrid(vpress_dummy);
	int sg;

	ForSubgridI(sg, GridSubgrids(grid))
	{
		Subgrid *subgrid = GridSubgrid(grid, sg);
		int ix = SubgridIX(subgrid);
		int iy = SubgridIY(subgrid);
		int iz = SubgridIZ(subgrid);

		int nx = SubgridNX(subgrid);
		int ny = SubgridNY(subgrid);
		int nz = SubgridNZ(subgrid);
		Subvector *subvector = VectorSubvector(vpress_dummy, sg);
		double *subvector_data = SubvectorData(subvector);
		int i, j, k;
		int counter = 0;

		for (k = iz; k < iz + nz; k++) {
			for (j = iy; j < iy + ny; j++) {
				for (i = ix; i < ix + nx; i++) {
					int pf_index = SubvectorEltIndex(subvector, i, j, k);
					subvector_data[pf_index] = data[counter];
					counter++;
				}
			}
		}

	}

	WritePFBinary(pre, suff, vpress_dummy);
}

void enkf_printmannings(char *pre, char *suff){
    ProblemData *problem_data = GetProblemDataRichards(solver);
    WritePFBinary(pre,suff, ProblemDataMannings(problem_data));      
}


void update_parflow () {
  int i,j;
  VectorUpdateCommHandle *handle;
  
  if(pf_updateflag == 1) {
    Vector *pressure_in = GetPressureRichards(solver);
    ENKF2PF(pressure_in, pf_statevec);
    handle = InitVectorUpdate(pressure_in, VectorUpdateAll);
    FinalizeVectorUpdate(handle);
    /* update saturation */
    //Problem * problem = GetProblemRichards(solver);
    //ProblemData *problem_data = GetProblemDataRichards(solver);
    //PFModule *problem_saturation = ProblemSaturation(problem);
    //Vector * saturation_in = GetSaturationRichards(solver);
    //Vector * density = GetDensityRichards(solver);
    //double gravity = ProblemGravity(problem);
    ////PFModuleInvokeType(PhaseDensityInvoke,  phase_density, 
    ////  	     (0, pressure_in, density, 
    ////  	      &dtmp, &dtmp, CALCFCN));
    ////handle = InitVectorUpdate(instance_xtra -> density, VectorUpdateAll);
    ////FinalizeVectorUpdate(handle);
    //PFModuleInvokeType(SaturationInvoke, problem_saturation, (saturation_in, pressure_in, density, gravity, problem_data, CALCFCN));
    //handle = InitVectorUpdate(saturation_in, VectorUpdateAll);
    //FinalizeVectorUpdate(handle);

  }
  
  if(pf_updateflag == 2){
    // write state vector to saturation in parflow
    Vector * saturation_in = GetSaturationRichards(solver);
    for(i=0;i<enkf_subvecsize;i++){
      pf_statevec[i] = pf_statevec[i] / subvec_porosity[i];
    }
    int saturation_to_pressure_type = 1;
    ENKF2PF(saturation_in, pf_statevec);
    Problem * problem = GetProblemRichards(solver);
    double gravity = ProblemGravity(problem);
    Vector * pressure_in = GetPressureRichards(solver);
    Vector * density = GetDensityRichards(solver);
    ProblemData * problem_data = GetProblemDataRichards(solver);
    PFModule * problem_saturation = ProblemSaturation(problem);
    // convert saturation to pressure
    global_ptr_this_pf_module = problem_saturation;
    SaturationToPressure(saturation_in,
    			pressure_in, density, gravity,
    			problem_data, CALCFCN,
    			saturation_to_pressure_type);
    global_ptr_this_pf_module = solver;
  
    PF2ENKF(pressure_in,subvec_p);
    handle = InitVectorUpdate(pressure_in, VectorUpdateAll);
    FinalizeVectorUpdate(handle);
  }
 
  if(pf_updateflag == 3){
    Vector *pressure_in = GetPressureRichards(solver);

    //char pre[200];
    //char suf[10];

    //sprintf(pre,"press.in_%d",task_id);
    //sprintf(suf,"");
    //WritePFBinary(pre, suf, pressure_in);

    ENKF2PF(pressure_in,&pf_statevec[enkf_subvecsize]);

    handle = InitVectorUpdate(pressure_in, VectorUpdateAll);
    FinalizeVectorUpdate(handle);

    //sprintf(pre,"press.out_%d",task_id);
    //sprintf(suf,"");
    //WritePFBinary(pre, suf, pressure_in);
  }

  if(pf_paramupdate == 1){
    ProblemData * problem_data = GetProblemDataRichards(solver);
    //VectorUpdateCommHandle *handle;
    Vector            *perm_xx = ProblemDataPermeabilityX(problem_data);
    Vector            *perm_yy = ProblemDataPermeabilityY(problem_data);
    Vector            *perm_zz = ProblemDataPermeabilityZ(problem_data);
    int nshift = 0;
    if(pf_updateflag == 3){
      nshift = 2*enkf_subvecsize;
    }else{
      nshift = enkf_subvecsize;
    }
 
    /* update perm_xx */    
    for(i=nshift,j=0;i<(nshift+enkf_subvecsize);i++,j++) 
      subvec_param[j] = pf_statevec[i];

    ENKF2PF(perm_xx,subvec_param);
    handle = InitVectorUpdate(perm_xx, VectorUpdateAll);
    FinalizeVectorUpdate(handle);
 
    /* update perm_yy */
    for(i=nshift,j=0;i<(nshift+enkf_subvecsize);i++,j++) 
      subvec_param[j] = pf_statevec[i] * pf_aniso_perm_y;

    ENKF2PF(perm_yy,subvec_param);
    handle = InitVectorUpdate(perm_yy, VectorUpdateAll);
    FinalizeVectorUpdate(handle);
 
    /* update perm_zz */
    for(i=nshift,j=0;i<(nshift+enkf_subvecsize);i++,j++) 
      subvec_param[j] = pf_statevec[i] * pf_aniso_perm_z;

    ENKF2PF(perm_zz,subvec_param);
    handle = InitVectorUpdate(perm_zz, VectorUpdateAll);
    FinalizeVectorUpdate(handle);
 
  }

  if(pf_paramupdate == 2){
    ProblemData *problem_data = GetProblemDataRichards(solver);
    Vector       *mannings    = ProblemDataMannings(problem_data);
    int nshift = 0;
    if(pf_updateflag == 3){
      nshift = 2*enkf_subvecsize;
    }else{
      nshift = enkf_subvecsize;
    }
    /* update mannings */    
    for(i=nshift,j=0;i<(nshift+pf_paramvecsize);i++,j++) 
      subvec_param[j] = pf_statevec[i];

    ENKF2PF(mannings,subvec_param);
    handle = InitVectorUpdate(mannings, VectorUpdateAll);
    FinalizeVectorUpdate(handle);
  }
}

