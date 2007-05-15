#include "copyright.h"
/*==============================================================================
 * FILE: rt.c
 *
 * PURPOSE: Problem generator for RT instabilty.  Gravitational pot. is
 *   hardwired to be -0.1z. Density difference is hardwired to be 2.0 in 2D, 3.0
 *   in 3D.  This reproduces 2D results of Liska & Wendroff, 3D results of
 *   Dimonte et al.
 * 
 * FOR 2D HYDRO:
 * Problem domain should be -1/6 < x < 1/6; -0.5 < y < 0.5 with gamma=1.4 to
 * match Liska & Wendroff. Interface is at y=0; perturbation added to Vy
 * Gravity acts in the y-direction.  Special reflecting boundary conditions
 *   added in x2 to improve hydrostatic eqm (prevents launching of weak waves)
 * Atwood number A = (d2-d1)/(d2+d1) = 1/3
 *
 * FOR 3D:
 * Problem domain should be -.05 < x < .05; -.05 < y < .05, -.1 < z < .1
 * Use gamma=5/3 to match Dimonte et al.
 * Interface is at z=0; perturbation added to Vz
 * Gravity acts in the z-direction.  Special reflecting boundary conditions
 *   added in x3 to improve hydrostatic eqm (prevents launching of weak waves)
 * Atwood number A = (d2-d1)/(d2+d1) = 1/2
 *
 * REFERENCE: R. Liska & B. Wendroff, SIAM J. Sci. Comput., 25, 995 (2003)
 *============================================================================*/

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "defs.h"
#include "athena.h"
#include "globals.h"
#include "prototypes.h"

/*==============================================================================
 * PRIVATE FUNCTION PROTOTYPES:
 * reflect_ix2() - sets BCs on L-x2 (left edge) of grid used in 2D
 * reflect_ox2() - sets BCs on R-x2 (right edge) of grid used in 2D
 * reflect_ix3() - sets BCs on L-x3 (left edge) of grid used in 3D
 * reflect_ox3() - sets BCs on R-x3 (right edge) of grid used in 3D
 * grav_pot2() - gravitational potential for 2D problem (accn in Y)
 * grav_pot3() - gravitational potential for 3D problem (accn in Z)
 *============================================================================*/

static double ran2(long int *idum);
static void reflect_ix2(Grid *pGrid);
static void reflect_ox2(Grid *pGrid);
static void reflect_ix3(Grid *pGrid);
static void reflect_ox3(Grid *pGrid);
static Real grav_pot2(const Real x1, const Real x2, const Real x3);
static Real grav_pot3(const Real x1, const Real x2, const Real x3);

/*=========================== PUBLIC FUNCTIONS ===============================*/
/*----------------------------------------------------------------------------*/
/* problem:  */

void problem(Grid *pGrid, Domain *pDomain)
{
  int i=0,j=0,k=0;
  int is,ie,js,je,ks,ke,iprob;
  long int iseed = -1;
  Real amp,x1,x2,x3,lx,ly,lz;
#ifdef MHD
  Real b0,angle;
#endif
  int Nx1, Nx2, Nx3;
  int ixs, jxs, kxs;

  is = pGrid->is;  ie = pGrid->ie;
  js = pGrid->js;  je = pGrid->je;
  ks = pGrid->ks;  ke = pGrid->ke;

  Nx1 = par_geti("grid","Nx1");
  Nx2 = par_geti("grid","Nx2");
  Nx3 = par_geti("grid","Nx3");

  lx = Nx1*pGrid->dx1;
  ly = Nx2*pGrid->dx2;
  lz = Nx3*pGrid->dx3;

/* Ensure a different initial random seed for each process in an MPI calc. */
  ixs = pGrid->is + pGrid->idisp;
  jxs = pGrid->js + pGrid->jdisp;
  kxs = pGrid->ks + pGrid->kdisp;
  iseed = -1 - (ixs + Nx1*(jxs + Nx2*kxs));

/* Read perturbation amplitude, problem switch */
  amp = par_getd("problem","amp");
  iprob = par_geti("problem","iprob");

/* Read magnetic field strength, angle [should be in degrees, 0 is along +ve
 * X-axis (no rotation)] */
#ifdef MHD
  b0 = par_getd("problem","b0");
  angle = par_getd("problem","angle");
  angle = (angle/180.)*PI;
#endif

/* 2D PROBLEM --------------------------------------------------------------- */
/* Initialize two fluids with interface at y=0.0.  Pressure scaled to give a
 * sound speed of 1 at the interface in the light (lower, d=1) fluid 
 * Perturb V2 using single (iprob=1) or multiple (iprob=2) mode 
 */

  if (pGrid->Nx3 == 1) {
  for (k=ks; k<=ke; k++) {
    for (j=js; j<=je; j++) {
      for (i=is; i<=ie; i++) {
        cc_pos(pGrid,i,j,k,&x1,&x2,&x3);
	pGrid->U[k][j][i].d = 1.0;
        pGrid->U[k][j][i].E = (1.0/Gamma - 0.1*x2)/Gamma_1;
	pGrid->U[k][j][i].M1 = 0.0;
        if (iprob == 1) {
          pGrid->U[k][j][i].M2 = amp/4.0*
            (1.0+cos(2.0*PI*x1/lx))*(1.0+cos(2.0*PI*x2/ly));
        }
        else {
          pGrid->U[k][j][i].M2 = amp*(ran2(&iseed) - 0.5)*
            (1.0+cos(2.0*PI*x2/ly));
	}
        pGrid->U[k][j][i].M3 = 0.0;
        if (x2 > 0.0) {
	  pGrid->U[k][j][i].d = 2.0;
          pGrid->U[k][j][i].M2 *= 2.0;
          pGrid->U[k][j][i].E = (1.0/Gamma - 0.2*x2)/Gamma_1;
	}
	pGrid->U[k][j][i].E+=0.5*SQR(pGrid->U[k][j][i].M2)/pGrid->U[k][j][i].d;
#ifdef MHD
	pGrid->B1i[k][j][i] = b0;
	pGrid->U[k][j][i].B1c = b0;
        pGrid->U[k][j][i].E += 0.5*b0*b0;
#endif
      }
#ifdef MHD
    pGrid->B1i[k][j][ie+1] = b0;
#endif
    }
  }

/* Enroll gravitational potential to give acceleration in y-direction for 2D
 * Use special boundary condition routines.  In 2D, gravity is in the
 * y-direction, so special boundary conditions needed for x2
*/

  StaticGravPot = grav_pot2;
  set_bvals_fun(left_x2,  reflect_ix2);
  set_bvals_fun(right_x2, reflect_ox2);

  } /* end of 2D initialization  */

/* 3D PROBLEM ----------------------------------------------------------------*/
/* Initialize two fluids with interface at z=0.0
 * Pressure scaled to give a sound speed of 1 at the interface
 * in the light (lower, d=1) fluid
 * iprob = 1 -- Perturb V3 using single mode
 * iprob = 2 -- Perturb V3 using multiple mode
 * iprob = 3 -- B in light fluid only, with multimode perturbation
 * iprob = 4 -- B rotated by "angle" at interface, multimode perturbation
 */

  if (pGrid->Nx3 > 1) {
  for (k=ks; k<=ke; k++) {
    for (j=js; j<=je; j++) {
      for (i=is; i<=ie; i++) {
        cc_pos(pGrid,i,j,k,&x1,&x2,&x3);
	pGrid->U[k][j][i].d = 1.0;
        pGrid->U[k][j][i].E = (1.0/Gamma - 0.1*x3)/Gamma_1;
	pGrid->U[k][j][i].M1 = 0.0;
	pGrid->U[k][j][i].M2 = 0.0;
        if (iprob == 1) {
          pGrid->U[k][j][i].M3 = amp/8.0*(1.0+cos(2.0*PI*x1/lx))*
            (1.0+cos(2.0*PI*x2/ly))*(1.0+cos(2.0*PI*x3/lz));
        }
        else {
          pGrid->U[k][j][i].M3 = amp*(ran2(&iseed) - 0.5)*
            (1.0+cos(2.0*PI*x3/lz));
	}
        if (x3 > 0.0) {
	  pGrid->U[k][j][i].d = 3.0;
          pGrid->U[k][j][i].M3 *= 3.0;
          pGrid->U[k][j][i].E = (1.0/Gamma - 0.3*x3)/Gamma_1;
	}
	pGrid->U[k][j][i].E+=0.5*SQR(pGrid->U[k][j][i].M3)/pGrid->U[k][j][i].d;
#ifdef MHD
        switch(iprob){
        case 3: /* B only in light fluid, do not add B^2 to E, total P const */
          if (x3 <= 0.0) {
            pGrid->B1i[k][j][i] = b0;
            if (i == ie) pGrid->B1i[k][j][ie+1] = b0;
            pGrid->U[k][j][i].B1c = b0;
          }
          break;
        case 4: /* rotate B by angle at interface */
          if (x3 <= 0.0) {
            pGrid->B1i[k][j][i] = b0;
            if (i == ie) pGrid->B1i[k][j][ie+1] = b0;
            pGrid->U[k][j][i].B1c = b0;
            pGrid->U[k][j][i].E += 0.5*b0*b0;
          }
          else {
            pGrid->B1i[k][j][i] = b0*cos(angle);
            pGrid->B2i[k][j][i] = b0*sin(angle);
            if (i == ie) pGrid->B1i[k][j][ie+1] = b0*cos(angle);
            if (j == je) pGrid->B2i[k][je+1][i] = b0*sin(angle);
            pGrid->U[k][j][i].B1c = b0*cos(angle);
            pGrid->U[k][j][i].B2c = b0*sin(angle);
            pGrid->U[k][j][i].E += 0.5*b0*b0;
          }
          break;
        default:
          pGrid->B1i[k][j][i] = b0;
          if (i == ie) pGrid->B1i[k][j][ie+1] = b0;
          pGrid->U[k][j][i].B1c = b0;
          pGrid->U[k][j][i].E += 0.5*b0*b0;
        }
#endif
      }
    }
  }

/* Enroll gravitational potential to give accn in z-direction for 3D
 * Use special boundary condition routines.  In 3D, gravity is in the
 * z-direction, so special boundary conditions needed for x3
 */

  StaticGravPot = grav_pot3;

  set_bvals_fun(left_x3,  reflect_ix3);
  set_bvals_fun(right_x3, reflect_ox3);

  } /* end of 3D initialization */

  return;
}

/*==============================================================================
 * PROBLEM USER FUNCTIONS:
 * problem_write_restart() - writes problem-specific user data to restart files
 * problem_read_restart()  - reads problem-specific user data from restart files
 * get_usr_expr()          - sets pointer to expression for special output data
 * Userwork_in_loop        - problem specific work IN     main loop
 * Userwork_after_loop     - problem specific work AFTER  main loop
 *----------------------------------------------------------------------------*/

void problem_write_restart(Grid *pG, Domain *pD, FILE *fp)
{
  return;
}

/*
 * 'problem_read_restart' must enroll special boundary value functions,
 *    and initialize gravity on restarts
 */

void problem_read_restart(Grid *pG, Domain *pD, FILE *fp)
{
  if (pG->Nx3 == 1) {
    StaticGravPot = grav_pot2;
    set_bvals_fun(left_x2,  reflect_ix2);
    set_bvals_fun(right_x2, reflect_ox2);
  }
 
  if (pG->Nx3 > 1) {
    StaticGravPot = grav_pot3;
    set_bvals_fun(left_x3,  reflect_ix3);
    set_bvals_fun(right_x3, reflect_ox3);
  }

  return;
}

Gasfun_t get_usr_expr(const char *expr)
{
  return NULL;
}

void Userwork_in_loop(Grid *pGrid, Domain *pDomain)
{
}

void Userwork_after_loop(Grid *pGrid, Domain *pDomain)
{
}

/*=========================== PRIVATE FUNCTIONS ==============================*/

/*------------------------------------------------------------------------------
 * ran2: extracted from the Numerical Recipes in C (version 2) code.  Modified
 *   to use doubles instead of floats. -- T. A. Gardiner -- Aug. 12, 2003
 */

#define IM1 2147483563
#define IM2 2147483399
#define AM (1.0/IM1)
#define IMM1 (IM1-1)
#define IA1 40014
#define IA2 40692
#define IQ1 53668
#define IQ2 52774
#define IR1 12211
#define IR2 3791
#define NTAB 32
#define NDIV (1+IMM1/NTAB)
#define RNMX (1.0-DBL_EPSILON)

/* Long period (> 2 x 10^{18}) random number generator of L'Ecuyer
 * with Bays-Durham shuffle and added safeguards.  Returns a uniform
 * random deviate between 0.0 and 1.0 (exclusive of the endpoint
 * values).  Call with idum = a negative integer to initialize;
 * thereafter, do not alter idum between successive deviates in a
 * sequence.  RNMX should appriximate the largest floating point value
 * that is less than 1. 
 */

double ran2(long int *idum)
{
  int j;
  long int k;
  static long int idum2=123456789;
  static long int iy=0;
  static long int iv[NTAB];
  double temp;

  if (*idum <= 0) { /* Initialize */
    if (-(*idum) < 1) *idum=1; /* Be sure to prevent idum = 0 */
    else *idum = -(*idum);
    idum2=(*idum);
    for (j=NTAB+7;j>=0;j--) { /* Load the shuffle table (after 8 warm-ups) */
      k=(*idum)/IQ1;
      *idum=IA1*(*idum-k*IQ1)-k*IR1;
      if (*idum < 0) *idum += IM1;
      if (j < NTAB) iv[j] = *idum;
    }
    iy=iv[0];
  }
  k=(*idum)/IQ1;                 /* Start here when not initializing */
  *idum=IA1*(*idum-k*IQ1)-k*IR1; /* Compute idum=(IA1*idum) % IM1 without */
  if (*idum < 0) *idum += IM1;   /* overflows by Schrage's method */
  k=idum2/IQ2;
  idum2=IA2*(idum2-k*IQ2)-k*IR2; /* Compute idum2=(IA2*idum) % IM2 likewise */
  if (idum2 < 0) idum2 += IM2;
  j=(int)(iy/NDIV);              /* Will be in the range 0...NTAB-1 */
  iy=iv[j]-idum2;                /* Here idum is shuffled, idum and idum2 */
  iv[j] = *idum;                 /* are combined to generate output */
  if (iy < 1) iy += IMM1;
  if ((temp=AM*iy) > RNMX) return RNMX; /* No endpoint values */
  else return temp;
}

#undef IM1
#undef IM2
#undef AM
#undef IMM1
#undef IA1
#undef IA2
#undef IQ1
#undef IQ2
#undef IR1
#undef IR2
#undef NTAB
#undef NDIV
#undef RNMX

/*------------------------------------------------------------------------------
 * reflect_ix2: special reflecting boundary functions in x2 for 2D sims
 */

static void reflect_ix2(Grid *pGrid)
{
  int js = pGrid->js;
  int ks = pGrid->ks, ke = pGrid->ke;
  int i,j,k,il,iu,ku; /* i-lower/upper;  k-upper */

  if (pGrid->Nx1 > 1){
    iu = pGrid->ie + nghost;
    il = pGrid->is - nghost;
  } else {
    iu = pGrid->ie;
    il = pGrid->is;
  }

  for (k=ks; k<=ke; k++) {
    for (j=1; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->U[k][js-j][i]    =  pGrid->U[k][js+(j-1)][i];
        pGrid->U[k][js-j][i].M2 = -pGrid->U[k][js-j][i].M2; /* reflect 2-mom. */
        pGrid->U[k][js-j][i].E +=  
	  pGrid->U[k][js+(j-1)][i].d*0.1*(2*j-1)*pGrid->dx2/Gamma_1;
      }
    }
  }

#ifdef MHD
  for (k=ks; k<=ke; k++) {
    for (j=1; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B1i[k][js-j][i] = pGrid->B1i[k][js+(j-1)][i];
      }
    }
  }

  for (k=ks; k<=ke; k++) {
    for (j=1; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B2i[k][js-j][i] = pGrid->B2i[k][js+(j-1)][i];
      }
    }
  }

  if (pGrid->Nx3 > 1) ku=ke+1; else ku=ke;
  for (k=ks; k<=ku; k++) {
    for (j=1; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B3i[k][js-j][i] = pGrid->B3i[k][js+(j-1)][i];
      }
    }
  }
#endif

  return;
}

/*------------------------------------------------------------------------------
 * reflect_ox2: special reflecting boundary functions in x2 for 2D sims
 */

static void reflect_ox2(Grid *pGrid)
{
  int je = pGrid->je;
  int ks = pGrid->ks, ke = pGrid->ke;
  int i,j,k,il,iu,ku; /* i-lower/upper;  k-upper */

  if (pGrid->Nx1 > 1){
    iu = pGrid->ie + nghost;
    il = pGrid->is - nghost;
  } else {
    iu = pGrid->ie;
    il = pGrid->is;
  }

  for (k=ks; k<=ke; k++) {
    for (j=1; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->U[k][je+j][i]    =  pGrid->U[k][je-(j-1)][i];
        pGrid->U[k][je+j][i].M2 = -pGrid->U[k][je+j][i].M2; /* reflect 2-mom. */
        pGrid->U[k][je+j][i].E -=  
	  pGrid->U[k][je-(j-1)][i].d*0.1*(2*j-1)*pGrid->dx2/Gamma_1;
      }
    }
  }

#ifdef MHD
  for (k=ks; k<=ke; k++) {
    for (j=1; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B1i[k][je+j][i] = pGrid->B1i[k][je-(j-1)][i];
      }
    }
  }

/* Note that j=je+1 is not a boundary condition for the interface field B2i */
  for (k=ks; k<=ke; k++) {
    for (j=2; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B2i[k][je+j][i] = pGrid->B2i[k][je-(j-1)][i];
      }
    }
  }

  if (pGrid->Nx3 > 1) ku=ke+1; else ku=ke;
  for (k=ks; k<=ku; k++) {
    for (j=1; j<=nghost; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B3i[k][je+j][i] = pGrid->B3i[k][je-(j-1)][i];
      }
    }
  }
#endif

  return;
}

/*------------------------------------------------------------------------------
 * reflect_ix3: special reflecting boundary functions in x3 for 2D sims
 */

static void reflect_ix3(Grid *pGrid)
{
  int ks = pGrid->ks;
  int i,j,k,il,iu,jl,ju; /* i-lower/upper;  j-lower/upper */

  if (pGrid->Nx1 > 1){
    iu = pGrid->ie + nghost;
    il = pGrid->is - nghost;
  } else {
    iu = pGrid->ie;
    il = pGrid->is;
  }
  if (pGrid->Nx2 > 1){
    ju = pGrid->je + nghost;
    jl = pGrid->js - nghost;
  } else {
    ju = pGrid->je;
    jl = pGrid->js;
  }

  for (k=1; k<=nghost; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->U[ks-k][j][i]    =  pGrid->U[ks+(k-1)][j][i];
        pGrid->U[ks-k][j][i].M3 = -pGrid->U[ks-k][j][i].M3; /* reflect 3-mom. */
        pGrid->U[ks-k][j][i].E +=
          pGrid->U[ks+(k-1)][j][i].d*0.1*(2*k-1)*pGrid->dx3/Gamma_1;
      }
    }
  }

#ifdef MHD
  for (k=1; k<=nghost; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B1i[ks-k][j][i] = pGrid->B1i[ks+(k-1)][j][i];
      }
    }
  }

  for (k=1; k<=nghost; k++) {    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B2i[ks-k][j][i] = pGrid->B2i[ks+(k-1)][j][i];
      }
    }
  }

  for (k=1; k<=nghost; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B3i[ks-k][j][i] = pGrid->B3i[ks+(k-1)][j][i];
      }
    }
  }
#endif

  return;
}

/*------------------------------------------------------------------------------
 * reflect_ox3: special reflecting boundary functions in x3 for 3D sims
 */

static void reflect_ox3(Grid *pGrid)
{
  int ke = pGrid->ke;
  int i,j,k ,il,iu,jl,ju; /* i-lower/upper;  j-lower/upper */

  if (pGrid->Nx1 > 1){
    iu = pGrid->ie + nghost;
    il = pGrid->is - nghost;
  } else {
    iu = pGrid->ie;
    il = pGrid->is;
  }
  if (pGrid->Nx2 > 1){
    ju = pGrid->je + nghost;
    jl = pGrid->js - nghost;
  } else {
    ju = pGrid->je;
    jl = pGrid->js;
  }
  for (k=1; k<=nghost; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->U[ke+k][j][i]    =  pGrid->U[ke-(k-1)][j][i];
        pGrid->U[ke+k][j][i].M3 = -pGrid->U[ke+k][j][i].M3; /* reflect 3-mom. */
        pGrid->U[ke+k][j][i].E -=
          pGrid->U[ke-(k-1)][j][i].d*0.1*(2*k-1)*pGrid->dx3/Gamma_1;
      }
    }
  }

#ifdef MHD
  for (k=1; k<=nghost; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B1i[ke+k][j][i] = pGrid->B1i[ke-(k-1)][j][i];
      }
    }
  }

  for (k=1; k<=nghost; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B2i[ke+k][j][i] = pGrid->B2i[ke-(k-1)][j][i];
      }
    }
  }

/* Note that k=ke+1 is not a boundary condition for the interface field B3i */
  for (k=2; k<=nghost; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        pGrid->B3i[ke+k][j][i] = pGrid->B3i[ke-(k-1)][j][i];
      }
    }
  }
#endif

  return;
}

/*------------------------------------------------------------------------------
 * grav_pot: Gravitational potentials  g = 0.1
 */

static Real grav_pot2(const Real x1, const Real x2, const Real x3)
{
  return 0.1*x2;
}
static Real grav_pot3(const Real x1, const Real x2, const Real x3)
{
  return 0.1*x3;
}
