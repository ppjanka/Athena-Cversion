#include "copyright.h"
/*==============================================================================
 * FILE: lr_states_prim1.c
 *
 * PURPOSE: First order (piecewise constant) spatial reconstruction.  The left-
 *   and right-states at the left-interface in each cell are indexed i.
 *   U_{L,i-1/2} is denoted by Ul[i  ];   U_{R,i-1/2} is denoted by Ur[i  ]
 *   U_{L,i+1/2} is denoted by Ul[i+1];   U_{R,i+1/2} is denoted by Ur[i+1]
 *
 * SOURCE TERMS: are added to the primitive variables
 *
 * CONTAINS PUBLIC FUNCTIONS: 
 *   lr_states()          - computes L/R states
 *   lr_states_init()     - initializes memory for static global array W
 *   lr_states_destruct() - frees memory for static global array W
 *============================================================================*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "defs.h"
#include "athena.h"
#include "prototypes.h"

#ifdef FIRST_ORDER

static Prim1D *W=NULL;

/*----------------------------------------------------------------------------*/
/* lr_states:
 * Input Arguments:
 *   U1d = CONSERVED variables at cell centers along 1-D slice
 *   Bxc = B in direction of slice at cell centers
 *   dt = timestep
 *   dtodx = dt/dx
 *   is,ie = starting and ending indices of zone centers in slice
 * U1d and Bxc must be initialized over [is-nghost:ie+nghost]
 *
 * Output Arguments:
 *   Ul,Ur = L/R-states of CONSERVED variables at interfaces over [is:ie+1]
 */

void lr_states(const Cons1D U1d[], const Real Bxc[], const Real Bxi[],
	       const Real dt, const Real dtodx, const int is, const int ie,
	       Cons1D Ul[], Cons1D Ur[])
{
  int i,n;
  Real *pW, pb;

  for (i=is; i<=ie+1; i++) {
    Ul[i] = U1d[i-1];
    Ur[i] = U1d[i  ];
  }

  return;
}

/*----------------------------------------------------------------------------*/
/* lr_states_init:  Allocate enough memory for work array W */

void lr_states_init(int nx1, int nx2, int nx3)
{
  int nmax;
  nmax =  nx1 > nx2  ? nx1 : nx2;
  nmax = (nx3 > nmax ? nx3 : nmax) + 2*nghost;

  if ((W  = (Prim1D*)malloc(nmax*sizeof(Prim1D))) == NULL){
    ath_error("[lr_states_init]: malloc returned a NULL pointer\n");
  }

  return;
}

/*----------------------------------------------------------------------------*/
/* lr_states_destruct:  Free memory used by work array W */

void lr_states_destruct(void)
{
  if (W != NULL) free(W);
  return;
}

#endif /* FIRST_ORDER */
