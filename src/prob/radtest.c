#include "copyright.h"

/*==============================================================================
 * FILE: radtest.c
 *
 * PURPOSE:  Problem generator for a non-LTE test of radiative transfer routine
 *           assuming a 1D variation of the optical depth.  Can be run for
 *           any number of dimensions with periodic boundary conditions in
 *           the non-varying directions.  Directions of variation is set
 *           by vert_dir=1,2,3 in the problem block.  eps sets the degree of
 *           deviation from LTE.  See Fabiani Bendicho & Trujillo Bueno ApJ,
 *           455, 646..
 * Initial conditions available:
 *
 *============================================================================*/

#include <math.h>

#include <stdlib.h>
#include <string.h>
#include "defs.h"
#include "athena.h"
#include "globals.h"
#include "prototypes.h"

static Real eps0;
/*==============================================================================
 * PRIVATE FUNCTION PROTOTYPES:
 *============================================================================*/

static Real const_B(const GridS *pG, const int ifr, const int i, const int j, 
		    const int k);
static Real const_eps(const GridS *pG, const int ifr, const int i, const int j, 
		      const int k);
static Real const_opacity(const GridS *pG, const int ifr, const int i, const int j, 
			  const int k);

void problem(DomainS *pDomain)
{
  RadGridS *pRG = (pDomain->RadGrid);
  GridS *pG = (pDomain->Grid);
  int il = pG->is, iu = pG->ie;
  int jl = pG->js, ju = pG->je;
  int kl = pG->ks, ku = pG->ke;
  int nf=pRG->nf, nang=pRG->nang;
  int noct = pRG->noct;
  int i, j, k, ifr, l, m;
  int vdir;
  Real y, ytop, ybtm;  
  Real den = 1.0;
  Real *tau = NULL, taumax, taumin;

/* Read problem parameters. */

  eps0 = par_getd("problem","eps");
  vdir = par_geti("problem","vert_dir");
  taumax = par_getd("problem","taumax");
  taumin = par_getd("problem","taumin");
  R_ideal = 1.0;

/* ---------- Initialize Grid ------------ */

/* Setup density structure */ 
/* tau il used to initialize density grid */

  switch(vdir) {
  
  case 1:
    ytop = pDomain->RootMaxX[0];
    ybtm = pDomain->RootMinX[0];
    if ((tau = (Real *)calloc_1d_array(pG->Nx[0]+2*nghost,sizeof(Real))) == NULL) {
      ath_error("[problem]: Error allocating memory");
    }
    for(i=il; i<=iu+2; i++) {
      y = pG->MinX[0] + (Real)(i-il)*pG->dx1;
      tau[i] = pow(10.0,taumin + (taumax-taumin) * ((y-ybtm)/(ytop-ybtm)));
    }
    il -= 1; iu += 1;
    if (pG->Nx[1] > 1) {
      jl -= 1; ju += 1;
    } else if (pG->Nx[2] > 1) {
      kl -= 1; ku += 1;
    }
    for (k=kl; k<=ku; k++) {
      for (j=jl; j<=ju; j++) {
	for (i=il; i<=iu; i++) {
	  pG->U[k][j][i].d  = (tau[i+1] - tau[i]) / pG->dx1;
	  pG->U[k][j][i].E = 1.0;  /* needed for tgas init */
	}}}

    break;

  case 2:
    ytop = pDomain->RootMaxX[1];
    ybtm = pDomain->RootMinX[1];
    if ((tau = (Real *)calloc_1d_array(pG->Nx[1]+2*nghost,sizeof(Real))) == NULL) {
      ath_error("[problem]: Error allocating memory");
    }
    for(j=jl; j<=ju+2; j++) {
      y = pG->MinX[1] + (Real)(j-jl)*pG->dx2;
      tau[j] = pow(10.0,taumin + (taumax-taumin) * ((y-ybtm)/(ytop-ybtm)));
    }
    il -= 1; iu += 1;
    jl -= 1; ju += 1;
    if (pG->Nx[2] > 1) {
      kl -= 1; ku += 1;
    }
    for (k=kl; k<=ku; k++) {
      for (j=jl; j<=ju; j++) {
	for (i=il; i<=iu; i++) {
	  pG->U[k][j][i].d  = (tau[j+1] - tau[j]) / pG->dx2;
	  pG->U[k][j][i].E = 1.0;  /* needed for tgas init */
	}}}
    
    break;

 case 3:
    ytop = pDomain->RootMaxX[2];
    ybtm = pDomain->RootMinX[2];
    if ((tau = (Real *)calloc_1d_array(pG->Nx[2]+2*nghost,sizeof(Real))) == NULL) {
      ath_error("[problem]: Error allocating memory");
    }
    for(k=kl; k<=ku+2; k++) {
      y = pG->MinX[2] + (Real)(k-kl)*pG->dx3;
      tau[k] = pow(10.0,taumin + (taumax-taumin) * ((y-ybtm)/(ytop-ybtm)));
    }
    il -= 1; iu += 1;
    jl -= 1; ju += 1;
    kl -= 1; ku += 1;
    for (k=kl; k<=ku; k++) {
      for (j=jl; j<=ju; j++) {
	for (i=il; i<=iu; i++) {
	  pG->U[k][j][i].d  = (tau[k+1] - tau[k]) / pG->dx3;
	  pG->U[k][j][i].E = 1.0;  /* needed for tgas init */
	}}}
    break;

  default:
    ath_error("[rad2d]: direction vert_dir must be 1-3\n");
    break;
  }
/* Free up memory */
  free_1d_array(tau);

/* ---------- Initialize RadGrid ------------ */

  il = pRG->is-1, iu = pRG->ie+1;
  jl = pRG->js,   ju = pRG->je;
  kl = pRG->ks,   ku = pRG->ke;
  if (pRG->Nx[1] > 1) { jl -= 1; ju += 1; }
  if (pRG->Nx[2] > 1) { kl -= 1; ku += 1; }

/* Initialize mean intensity */
  for(ifr=0; ifr<nf; ifr++)
    for (k=kl; k<=ku; k++)
      for (j=jl; j<=ju; j++)
	for(i=il; i<=iu; i++) {
	  pRG->R[k][j][i][ifr].J = 1.0;
	}
 
/* ------- Initialize boundary emission ---------------------------------- */

  switch(vdir) {

  case 1:
/* Density gradient aligned with i3 */
    for(ifr=0; ifr<nf; ifr++) {
      /* Initialize boundary intensity in x1 direction */
      for(k=kl; k<=ku; k++) {
	for(j=jl; j<=ju; j++) {
	  for(m=0; m<nang; m++) {
	    /* lower boundary is tau=0, no irradiation */
	    pRG->l1imu[ifr][k][j][0][m] = 0.0;
	    if (noct > 2) {
	      pRG->l1imu[ifr][k][j][2][m] = 0.0;
	      if (noct == 8) {
		pRG->l1imu[ifr][k][j][4][m] = 0.0;
		pRG->l1imu[ifr][k][j][6][m] = 0.0;
	      }
	    }
	    /* upper boundary is large tau, eps=1 */
	    pRG->r1imu[ifr][k][j][1][m] = 1.0;
	    if (noct > 2) {
	      pRG->r1imu[ifr][k][j][3][m] = 1.0;
	      if (noct == 8) {
		pRG->r1imu[ifr][k][j][5][m] = 1.0;
		pRG->r1imu[ifr][k][j][7][m] = 1.0;
	      }
	    }
	  }}

	if (noct > 2) {
/* Initialize boundary intensity in x2 direction */
	  /* lower boundary is tau=0, no irradiation */
	  for(l=0; l<noct; l++) {
	    for(m=0; m<nang; m++) {
	      pRG->r2imu[ifr][k][il][l][m] = 0.0; 
	      pRG->l2imu[ifr][k][il][l][m] = 0.0;
	    }}
	  for(i=il+1; i<=iu-1; i++) {
	    /* periodic radiation at left boundary */
	    for(m=0; m<nang; m++) {
	      pRG->l2imu[ifr][k][i][0][m] = 1.0;
	      pRG->l2imu[ifr][k][i][1][m] = 1.0;
	      if (noct == 8) {
		pRG->l2imu[ifr][k][i][4][m] = 1.0;
		pRG->l2imu[ifr][k][i][5][m] = 1.0;
	      }
	    }
	    /* periodic radiation at right boundary */
	    for(m=0; m<=nang; m++) {
	      pRG->r2imu[ifr][k][i][2][m] = 1.0;
	      pRG->r2imu[ifr][k][i][3][m] = 1.0;
	      if (noct == 8) {
		pRG->r2imu[ifr][k][i][6][m] = 1.0;
		pRG->r2imu[ifr][k][i][7][m] = 1.0;
	      }
	    }
	  }
	  /* upper boundary is large tau, eps=1 */
	  for(l=0; l<noct; l++) {
	    for(m=0; m<nang; m++) {
	      pRG->r2imu[ifr][k][iu][l][m] = 1.0; 
	      pRG->l2imu[ifr][k][iu][l][m] = 1.0;
	    }}
	}
      }
/* Initialize boundary intensity in x3 direction */
      if (noct == 8) {
	for(j=jl; j<=ju; j++) {
	  /* lower boundary is tau=0, no irradiation */
	  for(l=0; l<noct; l++) { 
	    for(m=0; m<nang; m++) {
	      pRG->r3imu[ifr][j][il][l][m] = 0.0; 
	      pRG->l3imu[ifr][j][il][l][m] = 0.0;
	    }}
	  for(i=il+1; i<=iu-1; i++) {
	    for(m=0; m<nang; m++) {
	      /* periodic radiation at left boundary */
	      pRG->l3imu[ifr][j][i][0][m] = 1.0;
	      pRG->l3imu[ifr][j][i][1][m] = 1.0;
	      pRG->l3imu[ifr][j][i][2][m] = 1.0;
	      pRG->l3imu[ifr][j][i][3][m] = 1.0;
	      /* periodic radiation at right boundary */
	      pRG->r3imu[ifr][j][i][4][m] = 1.0;
	      pRG->r3imu[ifr][j][i][5][m] = 1.0;
	      pRG->r3imu[ifr][j][i][6][m] = 1.0;
	      pRG->r3imu[ifr][j][i][7][m] = 1.0;
	    }}
	  /* upper boundary is large tau, eps=1 */
	  for(l=0; l<noct; l++) { 
	    for(m=0; m<nang; m++) {
	      pRG->r3imu[ifr][j][iu][l][m] = 1.0; 
	      pRG->l3imu[ifr][j][iu][l][m] = 1.0;
	    }}
	}
      }
    }
    break;

  case 2:
/* Density gradient aligned with i2 */
    for(ifr=0; ifr<nf; ifr++) {
/* Initialize boundary intensity in x1 direction */
      for(k=kl; k<=ku; k++) {
	/* lower boundary is tau=0, no irradiation */
	for(l=0; l<noct; l++) 
	  for(m=0; m<nang; m++) {
	    pRG->r1imu[ifr][k][jl][l][m] = 0.0;
	    pRG->l1imu[ifr][k][jl][l][m] = 0.0;
	  }
	for(j=jl+1; j<=ju-1; j++) {
	  /* periodic radiation at left boundary */
	  for(m=0; m<nang; m++) {
	    pRG->l1imu[ifr][k][j][0][m] = 1.0;
	    pRG->l1imu[ifr][k][j][2][m] = 1.0;
	    if (noct == 8) {
	      pRG->l1imu[ifr][k][j][4][m] = 1.0;
	      pRG->l1imu[ifr][k][j][6][m] = 1.0;
	    }
	  }
	  /* periodic radiation at right boundary */
	  for(m=0; m<=nang; m++) {
	    pRG->r1imu[ifr][k][j][1][m] = 1.0;
	    pRG->r1imu[ifr][k][j][3][m] = 1.0;
	    if (noct == 8) {
	      pRG->r1imu[ifr][k][j][5][m] = 1.0;
	      pRG->r1imu[ifr][k][j][7][m] = 1.0;
	    }
	  }
	}
	/* upper boundary is large tau, eps=1 */
      	for(l=0; l<noct; l++) 
	  for(m=0; m<nang; m++) {
	    pRG->r1imu[ifr][k][ju][l][m] = 1.0;
	    pRG->l1imu[ifr][k][ju][l][m] = 1.0;
	  }

/* Initialize boundary intensity in x2 direction */
	for(i=il; i<=iu; i++) { 
	  for(m=0; m<nang; m++) {
	    /* lower boundary is tau=0, no irradiation */
	    pRG->l2imu[ifr][k][i][0][m] = 0.0;
	    pRG->l2imu[ifr][k][i][1][m] = 0.0;
	    if (noct == 8) {
	      pRG->l2imu[ifr][k][i][4][m] = 0.0;
	      pRG->l2imu[ifr][k][i][5][m] = 0.0;
	    }
	    /* upper boundary is large tau, eps=1 */
	    pRG->r2imu[ifr][k][i][2][m] = 1.0;
	    pRG->r2imu[ifr][k][i][3][m] = 1.0;
	    if (noct == 8) {
	      pRG->r2imu[ifr][k][i][6][m] = 1.0;
	      pRG->r2imu[ifr][k][i][7][m] = 1.0;
	    }
	  }
	}
      }
/* Initialize boundary intensity in x3 direction */
      if (noct == 8) {
	for(i=il; i<=iu; i++) {
	  /* lower boundary is tau=0, no irradiation */
	  for(l=0; l<noct; l++) { 
	    for(m=0; m<nang; m++) {
	      pRG->r3imu[ifr][jl][i][l][m] = 0.0; 
	      pRG->l3imu[ifr][jl][i][l][m] = 0.0;
	    }}}
	for(j=jl+1; j<=ju-1; j++) {
	  for(i=il; i<=iu; i++) {
	    for(m=0; m<nang; m++) {
	      /* periodic radiation at left boundary */
	      pRG->l3imu[ifr][j][i][0][m] = 1.0;
	      pRG->l3imu[ifr][j][i][1][m] = 1.0;
	      pRG->l3imu[ifr][j][i][2][m] = 1.0;
	      pRG->l3imu[ifr][j][i][3][m] = 1.0;
	      /* periodic radiation at right boundary */
	      pRG->r3imu[ifr][j][i][4][m] = 1.0;
	      pRG->r3imu[ifr][j][i][5][m] = 1.0;
	      pRG->r3imu[ifr][j][i][6][m] = 1.0;
	      pRG->r3imu[ifr][j][i][7][m] = 1.0;
	    }}}
	for(i=il; i<=iu; i++) {
	  /* upper boundary is large tau, eps=1 */
	  for(l=0; l<noct; l++) { 
	    for(m=0; m<nang; m++) {
	      pRG->r3imu[ifr][ju][i][l][m] = 1.0; 
	      pRG->l3imu[ifr][ju][i][l][m] = 1.0;
	    }}}
      }
    }
    break;

 case 3:
/* Density gradient aligned with i3 */
   for(ifr=0; ifr<nf; ifr++) {
     /* Initialize boundary intensity in x1 direction */
     /* lower boundary is tau=0, no irradiation */
     for(j=jl; j<=ju; j++) {
       for(l=0; l<8; l++) { 
	 for(m=0; m<nang; m++) {
	   pRG->r1imu[ifr][kl][j][l][m] = 0.0;
	   pRG->l1imu[ifr][kl][j][l][m] = 0.0;
	 }}}
     for(k=kl+1; k<=ku-1; k++) {
       for(j=jl; j<=ju; j++) {
	 for(m=0; m<nang; m++) {
	   /* periodic radiation at left boundary */
	   pRG->l1imu[ifr][k][j][0][m] = 1.0;
	   pRG->l1imu[ifr][k][j][2][m] = 1.0;
	   pRG->l1imu[ifr][k][j][4][m] = 1.0;
	   pRG->l1imu[ifr][k][j][6][m] = 1.0;
	   /* periodic radiation at right boundary */
	   pRG->r1imu[ifr][k][j][1][m] = 1.0;
	   pRG->r1imu[ifr][k][j][3][m] = 1.0;
	   pRG->r1imu[ifr][k][j][5][m] = 1.0;
	   pRG->r1imu[ifr][k][j][7][m] = 1.0;
	 }}}
     /* upper boundary is large tau, eps=1 */
     for(j=jl; j<=ju; j++) {
       for(l=0; l<8; l++) { 
	 for(m=0; m<nang; m++) {
	   pRG->r1imu[ifr][ku][j][l][m] = 1.0;
	   pRG->l1imu[ifr][ku][j][l][m] = 1.0;
	 }}}

     /* Initialize boundary intensity in x2 direction */
     /* lower boundary is tau=0, no irradiation */
     for(i=il; i<=iu; i++) {
       for(l=0; l<8; l++) { 
	 for(m=0; m<nang; m++) {
	   pRG->r2imu[ifr][kl][i][l][m] = 0.0;
	   pRG->l2imu[ifr][kl][i][l][m] = 0.0;
	 }}}
     for(k=kl+1; k<=ku-1; k++) {
       for(i=il; i<=iu; i++) { 
	 for(m=0; m<nang; m++) {
	   /* periodic radiation at left boundary */     
	   pRG->l2imu[ifr][k][i][0][m] = 1.0;
	   pRG->l2imu[ifr][k][i][1][m] = 1.0;
	   pRG->l2imu[ifr][k][i][4][m] = 1.0;
	   pRG->l2imu[ifr][k][i][5][m] = 1.0;
	   /* periodic radiation at right boundary */
	   pRG->r2imu[ifr][k][i][2][m] = 1.0;
	   pRG->r2imu[ifr][k][i][3][m] = 1.0;
	   pRG->r2imu[ifr][k][i][6][m] = 1.0;
	   pRG->r2imu[ifr][k][i][7][m] = 1.0;
	 }}}
     /* upper boundary is large tau, eps=1 */
     for(i=il; i<=iu; i++) {
       for(l=0; l<8; l++) { 
	 for(m=0; m<nang; m++) {
	   pRG->r2imu[ifr][ku][i][l][m] = 1.0;
	   pRG->l2imu[ifr][ku][i][l][m] = 1.0;
	 }}}

     /* Initialize boundary intensity in x3 direction */
     for(j=jl; j<=ju; j++) {
       for(i=il; i<=iu; i++) {
	  for(m=0; m<nang; m++) {
	    /* lower boundary is tau=0, no irradiation */
	    pRG->l3imu[ifr][j][i][0][m] = 0.0;
	    pRG->l3imu[ifr][j][i][1][m] = 0.0;
	    pRG->l3imu[ifr][j][i][2][m] = 0.0;
	    pRG->l3imu[ifr][j][i][3][m] = 0.0;
	    /* upper boundary is large tau, eps=1 */
	    pRG->r3imu[ifr][j][i][4][m] = 1.0;
	    pRG->r3imu[ifr][j][i][5][m] = 1.0;
	    pRG->r3imu[ifr][j][i][6][m] = 1.0;
	    pRG->r3imu[ifr][j][i][7][m] = 1.0;
	  }
       }}
    }
    break;

  }
/* enroll radiation specification functions */
get_thermal_source = const_B;
get_thermal_fraction = const_eps;
get_total_opacity = const_opacity;

  return;
}

/*==============================================================================
 * PUBLIC PROBLEM USER FUNCTIONS:
 * problem_write_restart() - writes problem-specific user data to restart files
 * problem_read_restart()  - reads problem-specific user data from restart files
 * get_usr_expr()          - sets pointer to expression for special output data
 * get_usr_out_fun()       - returns a user defined output function pointer
 * get_usr_par_prop()      - returns a user defined particle selection function
 * Userwork_in_loop        - problem specific work IN     main loop
 * Userwork_after_loop     - problem specific work AFTER  main loop
 *----------------------------------------------------------------------------*/

void problem_write_restart(MeshS *pM, FILE *fp)
{
  return;
}


void problem_read_restart(MeshS *pM, FILE *fp)
{
  return;
}

ConsFun_t get_usr_expr(const char *expr)
{
  return NULL;
}

VOutFun_t get_usr_out_fun(const char *name){
  return NULL;
}

void Userwork_in_loop(MeshS *pM)
{
  return;
}

void Userwork_after_loop(MeshS *pM)
{
  return;
}

static Real const_B(const GridS *pG, const int ifr, const int i, const int j, 
		    const int k)
{
  return 1.0;
}

static Real const_eps(const GridS *pG, const int ifr, const int i, const int j, 
		      const int k)
{

  return eps0;
  
}

static Real const_opacity(const GridS *pG, const int ifr, const int i, const int j, 
			  const int k)
{

  return pG->U[k][j][i].d;
  
}