#include "copyright.h"
/*==============================================================================
 * FILE: dump_binary.c
 *
 * PURPOSE: Function to write an unformatted dump of the field variables that
 *   can be read, e.g., by IDL scripts.
 *
 * CONTAINS PUBLIC FUNCTIONS: 
 *   dump_binary - writes either conserved or primitive variables depending
 *                 on value of pOut->out read from input block.
 *============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defs.h"
#include "athena.h"
#include "globals.h"
#include "prototypes.h"
#ifdef PARTICLES
#include "particles/particle.h"
#endif

/*----------------------------------------------------------------------------*/
/* dump_binary:  */

void dump_binary(MeshS *pM, OutputS *pOut)
{
  GridS *pGrid;
  PrimS Prim;
  int dnum = pOut->num;
  FILE *p_binfile;
  char *fname;
  int n,ndata[7];
/* Upper and Lower bounds on i,j,k for data dump */
  int i,j,k,il,iu,jl,ju,kl,ku;
  float dat[2],*datax,*datay,*dataz;
  Real *pData,x1,x2,x3;
  int coordsys = -1;

/* Return if Grid is not on this processor */

  pGrid = pM->Domain[pOut->nlevel][pOut->ndomain].Grid;
  if (pGrid == NULL) return;

  il = pGrid->is, iu = pGrid->ie;
  jl = pGrid->js, ju = pGrid->je;
  kl = pGrid->ks, ku = pGrid->ke;

#ifdef WRITE_GHOST_CELLS
  il = pGrid->is - nghost;
  iu = pGrid->ie + nghost;

  if(pGrid->Nx[1] > 1){
    jl = pGrid->js - nghost;
    ju = pGrid->je + nghost;
  }

  if(pGrid->Nx[2] > 1){
    kl = pGrid->ks - nghost;
    ku = pGrid->ke + nghost;
  }
#endif /* WRITE_GHOST_CELLS */

  if((fname = ath_fname(NULL,pM->outfilename,num_digit,dnum,NULL,"bin"))
     == NULL){
    ath_error("[dump_binary]: Error constructing filename\n");
    return;
  }

  if((p_binfile = fopen(fname,"wb")) == NULL){
    ath_error("[dump_binary]: Unable to open binary dump file\n");
    return;
  }

/* Write the coordinate system information */
#if defined CARTESIAN
  coordsys = -1;
#elif defined CYLINDRICAL
  coordsys = -2;
#elif defined SPHERICAL
  coordsys = -3;
#endif
  fwrite(&coordsys,sizeof(int),1,p_binfile);

/* Write number of zones and variables */
  ndata[0] = iu-il+1;
  ndata[1] = ju-jl+1;
  ndata[2] = ku-kl+1;
  ndata[3] = NVAR;
  ndata[4] = NSCALARS;
#ifdef SELF_GRAVITY
  ndata[5] = 1;
#else
  ndata[5] = 0;
#endif
#ifdef PARTICLES
  ndata[6] = 1;
#else
  ndata[6] = 0;
#endif
  fwrite(ndata,sizeof(int),7,p_binfile);

/* Write (gamma-1) and isothermal sound speed */

#ifdef ISOTHERMAL
  dat[0] = (float)0.0;
  dat[1] = (float)Iso_csound;
#elif defined ADIABATIC
  dat[0] = (float)Gamma_1 ;
  dat[1] = (float)0.0;
#else
  dat[0] = dat[1] = 0.0; /* Anything better to put here? */
#endif
  fwrite(dat,sizeof(float),2,p_binfile);

/* Write time, dt */

  dat[0] = (float)pGrid->time;
  dat[1] = (float)pGrid->dt;
  fwrite(dat,sizeof(float),2,p_binfile);

/* Allocate Memory */

  if((datax = (float *)malloc(ndata[0]*sizeof(float))) == NULL){
    ath_error("[dump_binary]: malloc failed for temporary array\n");
    return;
  }
  if((datay = (float *)malloc(ndata[1]*sizeof(float))) == NULL){
    ath_error("[dump_binary]: malloc failed for temporary array\n");
    return;
  }
  if((dataz = (float *)malloc(ndata[2]*sizeof(float))) == NULL){
    ath_error("[dump_binary]: malloc failed for temporary array\n");
    return;
  }

/* compute x,y,z coordinates of cell centers, and write out */

  for (i=il; i<=iu; i++) {
    cc_pos(pGrid,i,jl,kl,&x1,&x2,&x3);
    pData = ((Real *) &(x1));
    datax[i-il] = (float)(*pData);
  }
  fwrite(datax,sizeof(float),(size_t)ndata[0],p_binfile);

  for (j=jl; j<=ju; j++) {
    cc_pos(pGrid,il,j,kl,&x1,&x2,&x3);
    pData = ((Real *) &(x2));
    datay[j-jl] = (float)(*pData);
  }
  fwrite(datay,sizeof(float),(size_t)ndata[1],p_binfile);

  for (k=kl; k<=ku; k++) {
    cc_pos(pGrid,il,jl,k,&x1,&x2,&x3);
    pData = ((Real *) &(x3));
    dataz[k-kl] = (float)(*pData);
  }
  fwrite(dataz,sizeof(float),(size_t)ndata[2],p_binfile);

/* Write cell-centered data (either conserved or primitives) */

  for (n=0;n<NVAR; n++) {
    for (k=0; k<ndata[2]; k++) {
    for (j=0; j<ndata[1]; j++) {
      for (i=0; i<ndata[0]; i++) {

        if (strcmp(pOut->out,"cons") == 0){
          pData = ((Real*)&(pGrid->U[k+kl][j+jl][i+il])) + n;
        } else if(strcmp(pOut->out,"prim") == 0) {
          Prim = Cons_to_Prim(&(pGrid->U[k+kl][j+jl][i+il]));
          pData = ((Real*)&(Prim)) + n;
        }
        datax[i] = (float)(*pData);

      }
      fwrite(datax,sizeof(float),(size_t)ndata[0],p_binfile);

    }}
  }

#ifdef SELF_GRAVITY
  for (k=0; k<ndata[2]; k++) {
  for (j=0; j<ndata[1]; j++) {
    for (i=0; i<ndata[0]; i++) {
      pData = &(pGrid->Phi[k+kl][j+jl][i+il]);
      datax[i] = (float)(*pData);
    }
    fwrite(datax,sizeof(float),(size_t)ndata[0],p_binfile);
  }}
#endif

#ifdef PARTICLES
  if (pOut->out_pargrid) {
    for (k=0; k<ndata[2]; k++) {
    for (j=0; j<ndata[1]; j++) {
      for (i=0; i<ndata[0]; i++) {
        datax[i] = pGrid->Coup[k+kl][j+jl][i+il].grid_d;
      }
      fwrite(datax,sizeof(float),(size_t)ndata[0],p_binfile);
    }}
    for (k=0; k<ndata[2]; k++) {
    for (j=0; j<ndata[1]; j++) {
      for (i=0; i<ndata[0]; i++) {
        datax[i] = pGrid->Coup[k+kl][j+jl][i+il].grid_v1;
      }
      fwrite(datax,sizeof(float),(size_t)ndata[0],p_binfile);
    }}
    for (k=0; k<ndata[2]; k++) {
    for (j=0; j<ndata[1]; j++) {
      for (i=0; i<ndata[0]; i++) {
        datax[i] = pGrid->Coup[k+kl][j+jl][i+il].grid_v2;
      }
      fwrite(datax,sizeof(float),(size_t)ndata[0],p_binfile);
    }}
    for (k=0; k<ndata[2]; k++) {
    for (j=0; j<ndata[1]; j++) {
      for (i=0; i<ndata[0]; i++) {
        datax[i] = pGrid->Coup[k+kl][j+jl][i+il].grid_v3;
      }
      fwrite(datax,sizeof(float),(size_t)ndata[0],p_binfile);
    }}
  }
#endif

/* close file and free memory */
  fclose(p_binfile); 
  free(datax); 
  free(datay); 
  free(dataz); 
  free(fname);
}
