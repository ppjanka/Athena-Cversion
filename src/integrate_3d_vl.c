#include "copyright.h"
/*==============================================================================
 * FILE: integrate_3d_vl.c
 *
 * PURPOSE: Updates the input Grid structure pointed to by *pGrid by one
 *   timestep using directionally unsplit van Leer scheme.  The variables
 *   updated are:
 *      U.[d,M1,M2,M3,E,B1c,B2c,B3c] -- where U is of type Gas
 *      B1i, B2i, B3i  -- interface magnetic field
 *   Also adds gravitational source terms, and H-correction of Sanders et al.
 *   For adb hydro, requires (9*Cons1D + 3*Real + 1*Gas) = 53 3D arrays
 *   For adb mhd, requires   (9*Cons1D + 9*Real + 1*Gas) = 80 3D arrays
 *   The H-correction of Sanders et al. adds another 3 arrays.
 *
 *   If FIRST_ORDER_FLUX_CORRECTION is defined, uses first-order fluxes when
 *   predict state is negative.  Added by Nicole Lemaster to run supersonic
 *   turbulence
 *
 * REFERENCE: J.M Stone & T.A. Gardiner, "A simple, second-order Godunov method
 *   for MHD using constrained transport", ???
 *
 *   R. Sanders, E. Morano, & M.-C. Druguet, "Multidimensinal dissipation for
 *   upwind schemes: stability and applications to gas dynamics", JCP, 145, 511
 *   (1998)
 *
 * CONTAINS PUBLIC FUNCTIONS: 
 *   integrate_3d_vl
 *   integrate_destruct_3d()
 *   integrate_init_3d()
 *============================================================================*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defs.h"
#include "athena.h"
#include "globals.h"
#include "prototypes.h"

/* FIRST_ORDER_FLUX_CORRECTION: Drop to first order for interfaces where
 * higher-order fluxes would cause cell-centered density to go negative.
 * See Step 13d for important details. */
#define FIRST_ORDER_FLUX_CORRECTION

#ifdef H_CORRECTION
#error : Flux correction in the VL integrator does not work with H-corrrection.
#endif /* H_CORRECTION */

static Gas ***Uhalf=NULL;
static Real *Bxc=NULL, *Bxi=NULL;
static Real ***B1_x1Face=NULL, ***B2_x2Face=NULL, ***B3_x3Face=NULL;
static Cons1D ***Ul_x1Face=NULL, ***Ur_x1Face=NULL;
static Cons1D ***Ul_x2Face=NULL, ***Ur_x2Face=NULL;
static Cons1D ***Ul_x3Face=NULL, ***Ur_x3Face=NULL;
static Cons1D *U1d=NULL, *Ul=NULL, *Ur=NULL;
static Prim1D *W=NULL, *Wl=NULL, *Wr=NULL;
static Cons1D ***x1Flux=NULL, ***x2Flux=NULL, ***x3Flux=NULL;
#ifdef MHD
static Real ***emf1=NULL, ***emf2=NULL, ***emf3=NULL;
static Real ***emf1_cc=NULL, ***emf2_cc=NULL, ***emf3_cc=NULL;
#endif /* MHD */

/* variables needed for H-correction of Sanders et al (1998) */
extern Real etah;
#ifdef H_CORRECTION
static Real ***eta1=NULL, ***eta2=NULL, ***eta3=NULL;
#endif

/* variables needed to drop to first-order fluxes */
#ifdef FIRST_ORDER_FLUX_CORRECTION
static char ***Ineg=NULL;
enum {correct_hydro_x1=1, correct_hydro_x2=2, correct_hydro_x3=4,
      correct_mhd_x1=8,   correct_mhd_x2=16,  correct_mhd_x3=32,
      correct_hydro_all=7,correct_mhd_all=56};
#endif /* FIRST_ORDER_FLUX_CORRECTION */

/*==============================================================================
 * PRIVATE FUNCTION PROTOTYPES: 
 *   integrate_emf1_corner() - 
 *   integrate_emf2_corner() - 
 *   integrate_emf3_corner() - 
 *   first_order_correction() -
 *============================================================================*/

static void integrate_emf1_corner(const Grid *pGrid);
static void integrate_emf2_corner(const Grid *pGrid);
static void integrate_emf3_corner(const Grid *pGrid);
#ifdef FIRST_ORDER_FLUX_CORRECTION
static void first_order_correction(const Grid *pGrid);
#endif /* FIRST_ORDER_FLUX_CORRECTION */

/*=========================== PUBLIC FUNCTIONS ===============================*/
/*----------------------------------------------------------------------------*/
/* integrate_3d_vl: 3D van Leer unsplit integrator for MHD. 
 */

void integrate_3d_vl(Grid *pGrid)
{
  Real dtodx1,dtodx2,dtodx3,q1,q2,q3;
  Real dt = pGrid->dt, hdt = 0.5*pGrid->dt;
  int i, is = pGrid->is, ie = pGrid->ie;
  int j, js = pGrid->js, je = pGrid->je;
  int k, ks = pGrid->ks, ke = pGrid->ke;
  int il = is - nghost, iu = ie + nghost;
  int jl = js - nghost, ju = je + nghost;
  int kl = ks - nghost, ku = ke + nghost;
#ifdef MHD
  Real d, M1, M2, M3, B1c, B2c, B3c;
#endif
#ifdef H_CORRECTION
  Real cfr,cfl,ur,ul;
#endif
#if (NSCALARS > 0)
  int n;
#endif
  Real pb,x1,x2,x3,phicl,phicr,phifc,phil,phir,phic;
  dtodx1 = pGrid->dt/pGrid->dx1;
  dtodx2 = pGrid->dt/pGrid->dx2;
  dtodx3 = pGrid->dt/pGrid->dx3;

/* Set widest loop limits possible */
#ifdef FIRST_ORDER
  int ib = il+1, it = iu-1;
  int jb = jl+1, jt = ju-1;
  int kb = kl+1, kt = ku-1;
#endif /* FIRST_ORDER */
#ifdef SECOND_ORDER
  int ib = il+2, it = iu-2;
  int jb = jl+2, jt = ju-2;
  int kb = kl+2, kt = ku-2;
#endif /* SECOND_ORDER */
#ifdef THIRD_ORDER
  int ib = il+3, it = iu-3;
  int jb = jl+3, jt = ju-3;
  int kb = kl+3, kt = ku-3;
#endif /* THIRD_ORDER */

  for (k=kl; k<=ku; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        Uhalf[k][j][i] = pGrid->U[k][j][i];
      }
    }
  }

/*--- Step 1 -------------------------------------------------------------------
 * Compute first-order flux in x1-direction
 * Set L and R states at x1-interfaces to appropriate cell-centered values
 */

  for (k=kl; k<=ku; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il+1; i<=iu; i++) {
	Ul[i].d = pGrid->U[k][j][i-1].d;
	Ul[i].Mx = pGrid->U[k][j][i-1].M1;
	Ul[i].My = pGrid->U[k][j][i-1].M2;
	Ul[i].Mz = pGrid->U[k][j][i-1].M3;
#ifndef ISOTHERMAL
	Ul[i].E = pGrid->U[k][j][i-1].E;
#endif /* ISOTHERMAL */
#ifdef MHD
        B1_x1Face[k][j][i] = pGrid->B1i[k][j][i];
	Ul[i].By = pGrid->U[k][j][i-1].B2c;
	Ul[i].Bz = pGrid->U[k][j][i-1].B3c;
#endif /* MHD */

	Ur[i].d = pGrid->U[k][j][i].d;
	Ur[i].Mx = pGrid->U[k][j][i].M1;
	Ur[i].My = pGrid->U[k][j][i].M2;
	Ur[i].Mz = pGrid->U[k][j][i].M3;
#ifndef ISOTHERMAL
	Ur[i].E = pGrid->U[k][j][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
	Ur[i].By = pGrid->U[k][j][i].B2c;
	Ur[i].Bz = pGrid->U[k][j][i].B3c;
#endif /* MHD */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++) {
          Ul[i].s[n] = pGrid->U[k][j][i-1].s[n];
          Ur[i].s[n] = pGrid->U[k][j][i  ].s[n];
        }
#endif
      }

/* Compute flux in x1-direction  */

      for (i=il+1; i<=iu; i++) {
        GET_FLUXES(B1_x1Face[k][j][i],Ul[i],Ur[i],&x1Flux[k][j][i]);
      }
    }
  }

/*--- Step 2 -------------------------------------------------------------------
 * Compute first-order flux in x2-direction
 * Set L and R states at x2-interfaces to appropriate cell-centered values
 */

  for (k=kl; k<=ku; k++) {
    for (i=il; i<=iu; i++) {
      for (j=jl+1; j<=ju; j++) {
	Ul[j].d = pGrid->U[k][j-1][i].d;
	Ul[j].Mx = pGrid->U[k][j-1][i].M2;
	Ul[j].My = pGrid->U[k][j-1][i].M3;
	Ul[j].Mz = pGrid->U[k][j-1][i].M1;
#ifndef ISOTHERMAL
	Ul[j].E = pGrid->U[k][j-1][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
        B2_x2Face[k][j][i] = pGrid->B2i[k][j][i];
	Ul[j].By = pGrid->U[k][j-1][i].B3c;
	Ul[j].Bz = pGrid->U[k][j-1][i].B1c;
#endif /* MHD */

	Ur[j].d = pGrid->U[k][j][i].d;
	Ur[j].Mx = pGrid->U[k][j][i].M2;
	Ur[j].My = pGrid->U[k][j][i].M3;
	Ur[j].Mz = pGrid->U[k][j][i].M1;
#ifndef ISOTHERMAL
	Ur[j].E = pGrid->U[k][j][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
	Ur[j].By = pGrid->U[k][j][i].B3c;
	Ur[j].Bz = pGrid->U[k][j][i].B1c;
#endif /* MHD */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++) {
          Ul[j].s[n] = pGrid->U[k][j-1][i].s[n];
          Ur[j].s[n] = pGrid->U[k][j  ][i].s[n];
        }
#endif
      }

/* Compute flux in x2-direction */

      for (j=jl+1; j<=ju; j++) {
        GET_FLUXES(B2_x2Face[k][j][i],Ul[j],Ur[j],&x2Flux[k][j][i]);
      }
    }
  }

/*--- Step 3 -------------------------------------------------------------------
 * Compute first-order flux in x3-direction
 * Set L and R states at x3-interfaces to appropriate cell-centered values
 */

  for (j=jl; j<=ju; j++) {
    for (i=il; i<=iu; i++) {
      for (k=kl+1; k<=ku; k++) {
	Ul[k].d = pGrid->U[k-1][j][i].d;
	Ul[k].Mx = pGrid->U[k-1][j][i].M3;
	Ul[k].My = pGrid->U[k-1][j][i].M1;
	Ul[k].Mz = pGrid->U[k-1][j][i].M2;
#ifndef ISOTHERMAL
	Ul[k].E = pGrid->U[k-1][j][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
        B3_x3Face[k][j][i] = pGrid->B3i[k][j][i];
	Ul[k].By = pGrid->U[k-1][j][i].B1c;
	Ul[k].Bz = pGrid->U[k-1][j][i].B2c;
#endif /* MHD */

	Ur[k].d = pGrid->U[k][j][i].d;
	Ur[k].Mx = pGrid->U[k][j][i].M3;
	Ur[k].My = pGrid->U[k][j][i].M1;
	Ur[k].Mz = pGrid->U[k][j][i].M2;
#ifndef ISOTHERMAL
	Ur[k].E = pGrid->U[k][j][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
	Ur[k].By = pGrid->U[k][j][i].B1c;
	Ur[k].Bz = pGrid->U[k][j][i].B2c;
#endif /* MHD */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++) {
          Ul[k].s[n] = pGrid->U[k-1][j][i].s[n];
          Ur[k].s[n] = pGrid->U[k  ][j][i].s[n];
        }
#endif
      }

/* Compute flux in x3-direction */

      for (k=kl+1; k<=ku; k++) {
        GET_FLUXES(B3_x3Face[k][j][i],Ul[k],Ur[k],&x3Flux[k][j][i]);
      }
    }
  }

/*--- Step 4 ------------------------------------------------------------------
 * Calculate the cell centered value of emf1,2,3 at t^{n} and integrate
 * to corner.
 */

#ifdef MHD
  for (k=kl; k<=ku; k++) {
    for (j=jl; j<=ju; j++) {
      for (i=il; i<=iu; i++) {
        emf1_cc[k][j][i] = (pGrid->U[k][j][i].B2c*pGrid->U[k][j][i].M3 -
			    pGrid->U[k][j][i].B3c*pGrid->U[k][j][i].M2)
                              /pGrid->U[k][j][i].d;
        emf2_cc[k][j][i] = (pGrid->U[k][j][i].B3c*pGrid->U[k][j][i].M1 -
			    pGrid->U[k][j][i].B1c*pGrid->U[k][j][i].M3)
                              /pGrid->U[k][j][i].d;
        emf3_cc[k][j][i] = (pGrid->U[k][j][i].B1c*pGrid->U[k][j][i].M2 -
			    pGrid->U[k][j][i].B2c*pGrid->U[k][j][i].M1)
                              /pGrid->U[k][j][i].d;
      }
    }
  }
  integrate_emf1_corner(pGrid);
  integrate_emf2_corner(pGrid);
  integrate_emf3_corner(pGrid);

/*--- Step 5 -------------------------------------------------------------------
 * Update the interface magnetic fields using CT for a half time step.  Compute
 * cell-centered B at half timestep from face-centered fields.
 */

  q1 = 0.5*dtodx1;
  q2 = 0.5*dtodx2;
  q3 = 0.5*dtodx3;
  for (k=kl+1; k<=ku-1; k++) {
    for (j=jl+1; j<=ju-1; j++) {
      for (i=il+1; i<=iu-1; i++) {
        B1_x1Face[k][j][i] += q3*(emf2[k+1][j  ][i  ] - emf2[k][j][i]) -
                              q2*(emf3[k  ][j+1][i  ] - emf3[k][j][i]);
        B2_x2Face[k][j][i] += q1*(emf3[k  ][j  ][i+1] - emf3[k][j][i]) -
                              q3*(emf1[k+1][j  ][i  ] - emf1[k][j][i]);
        B3_x3Face[k][j][i] += q2*(emf1[k  ][j+1][i  ] - emf1[k][j][i]) -
                              q1*(emf2[k  ][j  ][i+1] - emf2[k][j][i]);
      }
      B1_x1Face[k][j][iu] += q3*(emf2[k+1][j  ][iu]-emf2[k][j][iu]) -
                             q2*(emf3[k  ][j+1][iu]-emf3[k][j][iu]);
    }
    for (i=il+1; i<=iu-1; i++) {
      B2_x2Face[k][ju][i] += q1*(emf3[k  ][ju][i+1]-emf3[k][ju][i]) -
                             q3*(emf1[k+1][ju][i  ]-emf1[k][ju][i]);
    }
  }
  for (j=jl+1; j<=ju-1; j++) {
    for (i=il+1; i<=iu-1; i++) {
      B3_x3Face[ku][j][i] += q2*(emf1[ku][j+1][i  ]-emf1[ku][j][i]) -
                             q1*(emf2[ku][j  ][i+1]-emf2[ku][j][i]);
    }
  }

  for (k=kl+1; k<=ku-1; k++) {
    for (j=jl+1; j<=ju-1; j++) {
      for (i=il+1; i<=iu-1; i++) {
        Uhalf[k][j][i].B1c = 0.5*(B1_x1Face[k][j][i] + B1_x1Face[k][j][i+1]);
        Uhalf[k][j][i].B2c = 0.5*(B2_x2Face[k][j][i] + B2_x2Face[k][j+1][i]);
        Uhalf[k][j][i].B3c = 0.5*(B3_x3Face[k][j][i] + B3_x3Face[k+1][j][i]);
      }
    }
  }
#endif /* MHD */

/*--- Step 6a ------------------------------------------------------------------
 * Update HYDRO cell-centered variables to half-timestep using x1-fluxes
 */

  q1 = 0.5*dtodx1;
  for (k=kl+1; k<=ku-1; k++) {
    for (j=jl+1; j<=ju-1; j++) {
      for (i=il+1; i<=iu-1; i++) {
        Uhalf[k][j][i].d   -= q1*(x1Flux[k][j][i+1].d -x1Flux[k][j][i].d );
        Uhalf[k][j][i].M1  -= q1*(x1Flux[k][j][i+1].Mx-x1Flux[k][j][i].Mx);
        Uhalf[k][j][i].M2  -= q1*(x1Flux[k][j][i+1].My-x1Flux[k][j][i].My);
        Uhalf[k][j][i].M3  -= q1*(x1Flux[k][j][i+1].Mz-x1Flux[k][j][i].Mz);
#ifndef ISOTHERMAL
        Uhalf[k][j][i].E   -= q1*(x1Flux[k][j][i+1].E -x1Flux[k][j][i].E );
#endif /* ISOTHERMAL */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++)
          Uhalf[k][j][i].s[n] -= q1*(x1Flux[k][j][i+1].s[n]
                                   - x1Flux[k][j][i  ].s[n]);
#endif
      }
    }
  }

/*--- Step 6b ------------------------------------------------------------------
 * Update HYDRO cell-centered variables to half-timestep using x2-fluxes
 */

  q2 = 0.5*dtodx2;
  for (k=kl+1; k<=ku-1; k++) {
    for (j=jl+1; j<=ju-1; j++) {
      for (i=il+1; i<=iu-1; i++) {
        Uhalf[k][j][i].d   -= q2*(x2Flux[k][j+1][i].d -x2Flux[k][j][i].d );
        Uhalf[k][j][i].M1  -= q2*(x2Flux[k][j+1][i].Mz-x2Flux[k][j][i].Mz);
        Uhalf[k][j][i].M2  -= q2*(x2Flux[k][j+1][i].Mx-x2Flux[k][j][i].Mx);
        Uhalf[k][j][i].M3  -= q2*(x2Flux[k][j+1][i].My-x2Flux[k][j][i].My);
#ifndef ISOTHERMAL
        Uhalf[k][j][i].E   -= q2*(x2Flux[k][j+1][i].E -x2Flux[k][j][i].E );
#endif /* ISOTHERMAL */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++)
          Uhalf[k][j][i].s[n] -= q2*(x2Flux[k][j+1][i].s[n]
                                   - x2Flux[k][j  ][i].s[n]);
#endif
      }
    }
  }

/*--- Step 6c ------------------------------------------------------------------
 * Update HYDRO cell-centered variables to half-timestep using x3-fluxes
 */

  q3 = 0.5*dtodx3;
  for (k=kl+1; k<=ku-1; k++) {
    for (j=jl+1; j<=ju-1; j++) {
      for (i=il+1; i<=iu-1; i++) {
        Uhalf[k][j][i].d   -= q3*(x3Flux[k+1][j][i].d -x3Flux[k][j][i].d );
        Uhalf[k][j][i].M1  -= q3*(x3Flux[k+1][j][i].My-x3Flux[k][j][i].My);
        Uhalf[k][j][i].M2  -= q3*(x3Flux[k+1][j][i].Mz-x3Flux[k][j][i].Mz);
        Uhalf[k][j][i].M3  -= q3*(x3Flux[k+1][j][i].Mx-x3Flux[k][j][i].Mx);
#ifndef ISOTHERMAL
        Uhalf[k][j][i].E   -= q3*(x3Flux[k+1][j][i].E -x3Flux[k][j][i].E );
#endif /* ISOTHERMAL */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++)
          Uhalf[k][j][i].s[n] -= q3*(x3Flux[k+1][j][i].s[n]
                                   - x3Flux[k  ][j][i].s[n]);
#endif
      }
    }
  }

/*--- Step 6d ------------------------------------------------------------------
 * Add gravitational source terms to half-updated cell-centered values
 */

  if (StaticGravPot != NULL){
    for (k=kl+1; k<=ku-1; k++) {
      for (j=jl+1; j<=ju-1; j++) {
        for (i=il+1; i<=iu-1; i++) {
          cc_pos(pGrid,i,j,k,&x1,&x2,&x3);
          phic = (*StaticGravPot)((x1               ),x2,x3);
          phir = (*StaticGravPot)((x1+0.5*pGrid->dx1),x2,x3);
          phil = (*StaticGravPot)((x1-0.5*pGrid->dx1),x2,x3);

          Uhalf[k][j][i].M1 -= 0.5*dtodx1*(phir-phil)*pGrid->U[k][j][i].d;
#ifndef ISOTHERMAL
          Uhalf[k][j][i].E +=0.5*dtodx1*(x1Flux[k][j][i  ].d*(phil - phic)
                                       + x1Flux[k][j][i+1].d*(phic - phir));
#endif

          phir = (*StaticGravPot)(x1,(x2+0.5*pGrid->dx2),x3);
          phil = (*StaticGravPot)(x1,(x2-0.5*pGrid->dx2),x3);
          Uhalf[k][j][i].M2 -= 0.5*dtodx2*(phir-phil)*pGrid->U[k][j][i].d;
#ifndef ISOTHERMAL
          Uhalf[k][j][i].E +=0.5*dtodx2*(x2Flux[k][j  ][i].d*(phil - phic)
                                       + x2Flux[k][j+1][i].d*(phic - phir));
#endif

          phir = (*StaticGravPot)(x1,x2,(x3+0.5*pGrid->dx3));
          phil = (*StaticGravPot)(x1,x2,(x3-0.5*pGrid->dx3));
          Uhalf[k][j][i].M3 -= 0.5*dtodx3*(phir-phil)*pGrid->U[k][j][i].d;
#ifndef ISOTHERMAL
          Uhalf[k][j][i].E +=0.5*dtodx3*(x3Flux[k  ][j][i].d*(phil - phic)
                                       + x3Flux[k+1][j][i].d*(phic - phir));
#endif
        }
      }
    }
  }

/*--- Step 7a ------------------------------------------------------------------
 * Compute L/R states at x1-interfaces using U^{n+1/2}, store into 3D arrays
 * U1d = (d, M1, M2, M3, E, B2c, B3c)
 */

  for (k=kb; k<=kt; k++) {
    for (j=jb; j<=jt; j++) {
      for (i=il; i<=iu; i++) {
        U1d[i].d  = Uhalf[k][j][i].d;
        U1d[i].Mx = Uhalf[k][j][i].M1;
        U1d[i].My = Uhalf[k][j][i].M2;
        U1d[i].Mz = Uhalf[k][j][i].M3;
#ifndef ISOTHERMAL
        U1d[i].E  = Uhalf[k][j][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
        U1d[i].By = Uhalf[k][j][i].B2c;
        U1d[i].Bz = Uhalf[k][j][i].B3c;
        Bxc[i] = Uhalf[k][j][i].B1c;
        Bxi[i] = B1_x1Face[k][j][i];
#endif /* MHD */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++) U1d[i].s[n] = Uhalf[k][j][i].s[n];
#endif
      }

      for (i=il; i<=iu; i++) {
        pb = Cons1D_to_Prim1D(&U1d[i],&W[i],&Bxc[i]);
      }

      lr_states(W,Bxc,0.0,0.0,ib+1,it-1,Wl,Wr);

      for (i=ib+1; i<=it; i++) {
        pb = Prim1D_to_Cons1D(&Ul[i],&Wl[i],&Bxc[i]);
        pb = Prim1D_to_Cons1D(&Ur[i],&Wr[i],&Bxc[i]);
      }
      for (i=ib+1; i<=it; i++) {
        Ul_x1Face[k][j][i] = Ul[i];
        Ur_x1Face[k][j][i] = Ur[i];
      }
    }
  }

/*--- Step 7b ------------------------------------------------------------------
 * Compute L/R states at x2-interfaces using U^{n+1/2}, store into 3D arrays
 * U1d = (d, M2, M3, M1, E, B3c, B1c)
 */

  for (k=kb; k<=kt; k++) {
    for (i=ib; i<=it; i++) {
      for (j=jl; j<=ju; j++) {
        U1d[j].d  = Uhalf[k][j][i].d;
        U1d[j].Mx = Uhalf[k][j][i].M2;
        U1d[j].My = Uhalf[k][j][i].M3;
        U1d[j].Mz = Uhalf[k][j][i].M1;
#ifndef ISOTHERMAL
        U1d[j].E  = Uhalf[k][j][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
        U1d[j].By = Uhalf[k][j][i].B3c;
        U1d[j].Bz = Uhalf[k][j][i].B1c;
        Bxc[j] = Uhalf[k][j][i].B2c;
        Bxi[j] = B2_x2Face[k][j][i];
#endif /* MHD */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++) U1d[j].s[n] = Uhalf[k][j][i].s[n];
#endif
      }

/* Compute L and R states at x2-interfaces.  */

      for (j=jl; j<=ju; j++) {
        pb = Cons1D_to_Prim1D(&U1d[j],&W[j],&Bxc[j]);
      }

      lr_states(W,Bxc,0.0,0.0,jb+1,jt-1,Wl,Wr);

      for (j=jb+1; j<=jt; j++) {
        pb = Prim1D_to_Cons1D(&Ul[j],&Wl[j],&Bxc[j]);
        pb = Prim1D_to_Cons1D(&Ur[j],&Wr[j],&Bxc[j]);
      }
      for (j=jb+1; j<=jt; j++) {
        Ul_x2Face[k][j][i] = Ul[j];
        Ur_x2Face[k][j][i] = Ur[j];
      }
    }
  }

/*--- Step 7c ------------------------------------------------------------------
 * Compute L/R states at x3-interfaces using U^{n+1/2}, store into 3D arrays
 * U1d = (d, M3, M1, M2, E, B1c, B2c)
 */

  for (j=jb; j<=jt; j++) {
    for (i=ib; i<=it; i++) {
      for (k=kl; k<=ku; k++) {
        U1d[k].d  = Uhalf[k][j][i].d;
        U1d[k].Mx = Uhalf[k][j][i].M3;
        U1d[k].My = Uhalf[k][j][i].M1;
        U1d[k].Mz = Uhalf[k][j][i].M2;
#ifndef ISOTHERMAL
        U1d[k].E  = Uhalf[k][j][i].E;
#endif /* ISOTHERMAL */
#ifdef MHD
        U1d[k].By = Uhalf[k][j][i].B1c;
        U1d[k].Bz = Uhalf[k][j][i].B2c;
        Bxc[k] = Uhalf[k][j][i].B3c;
        Bxi[k] = B3_x3Face[k][j][i];
#endif /* MHD */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++) U1d[k].s[n] = Uhalf[k][j][i].s[n];
#endif
      }

/* Compute L and R states at x3-interfaces.  */

      for (k=kl; k<=ku; k++) {
        pb = Cons1D_to_Prim1D(&U1d[k],&W[k],&Bxc[k]);
      }

      lr_states(W,Bxc,0.0,0.0,kb+1,kt-1,Wl,Wr);

      for (k=kb+1; k<=kt; k++) {
        pb = Prim1D_to_Cons1D(&Ul[k],&Wl[k],&Bxc[k]);
        pb = Prim1D_to_Cons1D(&Ur[k],&Wr[k],&Bxc[k]);
      }
      for (k=kb+1; k<=kt; k++) {
        Ul_x3Face[k][j][i] = Ul[k];
        Ur_x3Face[k][j][i] = Ur[k];
      }
    }
  }

/*--- Step 8 ------------------------------------------------------------------
 * Compute maximum wavespeeds in multidimensions (eta in eq. 10 from Sanders et
 *  al. (1998)) for H-correction
 */

#ifdef H_CORRECTION
  for (k=kb; k<=kt; k++) {
    for (j=jb; j<=jt; j++) {
      for (i=ib+1; i<=it; i++) {
        cfr = cfast(&(Ur_x1Face[k][j][i]), &(B1_x1Face[k][j][i]));
        cfl = cfast(&(Ul_x1Face[k][j][i]), &(B1_x1Face[k][j][i]));
        ur = Ur_x1Face[k][j][i].Mx/Ur_x1Face[k][j][i].d;
        ul = Ul_x1Face[k][j][i].Mx/Ul_x1Face[k][j][i].d;
        eta1[k][j][i] = 0.5*(fabs(ur - ul) + fabs(cfr - cfl));
      }
    }
  }

  for (k=kb; k<=kt; k++) {
    for (j=jb+1; j<=jt; j++) {
      for (i=ib; i<=it; i++) {
        cfr = cfast(&(Ur_x2Face[k][j][i]), &(B2_x2Face[k][j][i]));
        cfl = cfast(&(Ul_x2Face[k][j][i]), &(B2_x2Face[k][j][i]));
        ur = Ur_x2Face[k][j][i].Mx/Ur_x2Face[k][j][i].d;
        ul = Ul_x2Face[k][j][i].Mx/Ul_x2Face[k][j][i].d;
        eta2[k][j][i] = 0.5*(fabs(ur - ul) + fabs(cfr - cfl));
      }
    }
  }

  for (k=kb+1; k<=kt; k++) {
    for (j=jb; j<=jt; j++) {
      for (i=ib; i<=it; i++) {
        cfr = cfast(&(Ur_x3Face[k][j][i]), &(B3_x3Face[k][j][i]));
        cfl = cfast(&(Ul_x3Face[k][j][i]), &(B3_x3Face[k][j][i]));
        ur = Ur_x3Face[k][j][i].Mx/Ur_x3Face[k][j][i].d;
        ul = Ul_x3Face[k][j][i].Mx/Ul_x3Face[k][j][i].d;
        eta3[k][j][i] = 0.5*(fabs(ur - ul) + fabs(cfr - cfl));
      }
    }
  }
#endif /* H_CORRECTION */

/*--- Step 9a ------------------------------------------------------------------
 * Compute second-order fluxes in x1-direction, including H-correction
 */

  for (k=kb; k<=kt; k++) {
    for (j=jb; j<=jt; j++) {
      for (i=ib+1; i<=it; i++) {
#ifdef H_CORRECTION
        etah = MAX(eta2[k][j][i-1],eta2[k][j][i]);
        etah = MAX(etah,eta2[k][j+1][i-1]);
        etah = MAX(etah,eta2[k][j+1][i  ]);

        etah = MAX(etah,eta3[k  ][j][i-1]);
        etah = MAX(etah,eta3[k  ][j][i  ]);
        etah = MAX(etah,eta3[k+1][j][i-1]);
        etah = MAX(etah,eta3[k+1][j][i  ]);

        etah = MAX(etah,eta1[k  ][j][i  ]);
#endif /* H_CORRECTION */
        GET_FLUXES(B1_x1Face[k][j][i],Ul_x1Face[k][j][i],Ur_x1Face[k][j][i]
          ,&x1Flux[k][j][i]);
      }
    }
  }

/*--- Step 9b ------------------------------------------------------------------
 * Compute second-order fluxes in x2-direction, including H-correction
 */

  for (k=kb; k<=kt; k++) {
    for (j=jb+1; j<=jt; j++) {
      for (i=ib; i<=it; i++) {
#ifdef H_CORRECTION
        etah = MAX(eta1[k][j-1][i],eta1[k][j][i]);
        etah = MAX(etah,eta1[k][j-1][i+1]);
        etah = MAX(etah,eta1[k][j  ][i+1]);

        etah = MAX(etah,eta3[k  ][j-1][i]);
        etah = MAX(etah,eta3[k  ][j  ][i]);
        etah = MAX(etah,eta3[k+1][j-1][i]);
        etah = MAX(etah,eta3[k+1][j  ][i]);

        etah = MAX(etah,eta2[k  ][j  ][i]);
#endif /* H_CORRECTION */
        GET_FLUXES(B2_x2Face[k][j][i],Ul_x2Face[k][j][i],Ur_x2Face[k][j][i]
          ,&x2Flux[k][j][i]);
      }
    }
  }

/*--- Step 9c ------------------------------------------------------------------
 * Compute second-order fluxes in x3-direction, including H-correction
 */

  for (k=kb+1; k<=kt; k++) {
    for (j=jb; j<=jt; j++) {
      for (i=ib; i<=it; i++) {
#ifdef H_CORRECTION
        etah = MAX(eta1[k-1][j][i],eta1[k][j][i]);
        etah = MAX(etah,eta1[k-1][j][i+1]);
        etah = MAX(etah,eta1[k][j  ][i+1]);

        etah = MAX(etah,eta2[k-1][j  ][i]);
        etah = MAX(etah,eta2[k  ][j  ][i]);
        etah = MAX(etah,eta2[k-1][j+1][i]);
        etah = MAX(etah,eta2[k  ][j+1][i]);

        etah = MAX(etah,eta3[k  ][j  ][i]);
#endif /* H_CORRECTION */
        GET_FLUXES(B3_x3Face[k][j][i],Ul_x3Face[k][j][i],Ur_x3Face[k][j][i]
          ,&x3Flux[k][j][i]);
      }
    }
  }

/*--- Step 10 ------------------------------------------------------------------
 * Calculate the cell centered value of emf1,2,3 at the half-time-step.
 */

#ifdef MHD
  for (k=kb; k<=kt; k++) {
    for (j=jb; j<=jt; j++) {
      for (i=ib; i<=it; i++) {
        d  = Uhalf[k][j][i].d ;
        M1 = Uhalf[k][j][i].M1;
        M2 = Uhalf[k][j][i].M2;
        M3 = Uhalf[k][j][i].M3;
        B1c = Uhalf[k][j][i].B1c;
        B2c = Uhalf[k][j][i].B2c;
        B3c = Uhalf[k][j][i].B3c;

        emf1_cc[k][j][i] = (B2c*M3 - B3c*M2)/d;
        emf2_cc[k][j][i] = (B3c*M1 - B1c*M3)/d;
        emf3_cc[k][j][i] = (B1c*M2 - B2c*M1)/d;
      }
    }
  }
#endif

/*--- Step 11 ------------------------------------------------------------------
 * Integrate emf*^{n+1/2} to the grid cell corners and then update the
 * interface magnetic fields using CT for a full time step.
 */

#ifdef MHD
  integrate_emf1_corner(pGrid);
  integrate_emf2_corner(pGrid);
  integrate_emf3_corner(pGrid);

  for (k=kb+1; k<=kt-1; k++) {
    for (j=jb+1; j<=jt-1; j++) {
      for (i=ib+1; i<=it-1; i++) {
        pGrid->B1i[k][j][i] += dtodx3*(emf2[k+1][j  ][i  ] - emf2[k][j][i]) -
                               dtodx2*(emf3[k  ][j+1][i  ] - emf3[k][j][i]);
        pGrid->B2i[k][j][i] += dtodx1*(emf3[k  ][j  ][i+1] - emf3[k][j][i]) -
                               dtodx3*(emf1[k+1][j  ][i  ] - emf1[k][j][i]);
        pGrid->B3i[k][j][i] += dtodx2*(emf1[k  ][j+1][i  ] - emf1[k][j][i]) -
                               dtodx1*(emf2[k  ][j  ][i+1] - emf2[k][j][i]);
      }
      pGrid->B1i[k][j][it] +=
        dtodx3*(emf2[k+1][j  ][it] - emf2[k][j][it]) -
        dtodx2*(emf3[k  ][j+1][it] - emf3[k][j][it]);
    }
    for (i=ib+1; i<=it-1; i++) {
      pGrid->B2i[k][jt][i] +=
        dtodx1*(emf3[k  ][jt][i+1] - emf3[k][jt][i]) -
        dtodx3*(emf1[k+1][jt][i  ] - emf1[k][jt][i]);
    }
  }
  for (j=jb+1; j<=jt-1; j++) {
    for (i=ib+1; i<=it-1; i++) {
      pGrid->B3i[kt][j][i] += 
        dtodx2*(emf1[kt][j+1][i  ] - emf1[kt][j][i]) -
        dtodx1*(emf2[kt][j  ][i+1] - emf2[kt][j][i]);
    }
  }
#endif

/*--- Step 12 ------------------------------------------------------------------
 * Add the gravitational source terms at 2nd order
 */


  if (StaticGravPot != NULL){
    for (k=kl+1; k<=ku-1; k++) {
      for (j=jl+1; j<=ju-1; j++) {
        for (i=il+1; i<=iu-1; i++) {
          cc_pos(pGrid,i,j,k,&x1,&x2,&x3);
          phic = (*StaticGravPot)((x1               ),x2,x3);
          phir = (*StaticGravPot)((x1+0.5*pGrid->dx1),x2,x3);
          phil = (*StaticGravPot)((x1-0.5*pGrid->dx1),x2,x3);

          pGrid->U[k][j][i].M1 -= dtodx1*(phir-phil)*Uhalf[k][j][i].d;
#ifndef ISOTHERMAL
          pGrid->U[k][j][i].E += dtodx1*(x1Flux[k][j][i  ].d*(phil - phic)
                                       + x1Flux[k][j][i+1].d*(phic - phir));
#endif

          phir = (*StaticGravPot)(x1,(x2+0.5*pGrid->dx2),x3);
          phil = (*StaticGravPot)(x1,(x2-0.5*pGrid->dx2),x3);
          pGrid->U[k][j][i].M2 -= dtodx2*(phir-phil)*Uhalf[k][j][i].d;
#ifndef ISOTHERMAL
          pGrid->U[k][j][i].E += dtodx2*(x2Flux[k][j  ][i].d*(phil - phic)
                                       + x2Flux[k][j+1][i].d*(phic - phir));
#endif

          phir = (*StaticGravPot)(x1,x2,(x3+0.5*pGrid->dx3));
          phil = (*StaticGravPot)(x1,x2,(x3-0.5*pGrid->dx3));
          pGrid->U[k][j][i].M3 -= dtodx3*(phir-phil)*Uhalf[k][j][i].d;
#ifndef ISOTHERMAL
          pGrid->U[k][j][i].E += dtodx3*(x3Flux[k  ][j][i].d*(phil - phic)
                                       + x3Flux[k+1][j][i].d*(phic - phir));
#endif
        }
      }
    }
  }

/*--- Step 13a -----------------------------------------------------------------
 * Update HYDRO cell-centered variables in pGrid using x1-fluxes
 */

  for (k=kb+1; k<=kt-1; k++) {
    for (j=jb+1; j<=jt-1; j++) {
      for (i=ib+1; i<=it-1; i++) {
        pGrid->U[k][j][i].d -=dtodx1*(x1Flux[k][j][i+1].d -x1Flux[k][j][i].d );
        pGrid->U[k][j][i].M1-=dtodx1*(x1Flux[k][j][i+1].Mx-x1Flux[k][j][i].Mx);
        pGrid->U[k][j][i].M2-=dtodx1*(x1Flux[k][j][i+1].My-x1Flux[k][j][i].My);
        pGrid->U[k][j][i].M3-=dtodx1*(x1Flux[k][j][i+1].Mz-x1Flux[k][j][i].Mz);
#ifndef ISOTHERMAL
        pGrid->U[k][j][i].E -=dtodx1*(x1Flux[k][j][i+1].E -x1Flux[k][j][i].E );
#endif /* ISOTHERMAL */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++)
          pGrid->U[k][j][i].s[n] -= dtodx1*(x1Flux[k][j][i+1].s[n]
                                          - x1Flux[k][j][i  ].s[n]);
#endif
      }
    }
  }

/*--- Step 13b -----------------------------------------------------------------
 * Update HYDRO cell-centered variables in pGrid using x2-fluxes
 */

  for (k=kb+1; k<=kt-1; k++) {
    for (j=jb+1; j<=jt-1; j++) {
      for (i=ib+1; i<=it-1; i++) {
        pGrid->U[k][j][i].d -=dtodx2*(x2Flux[k][j+1][i].d -x2Flux[k][j][i].d );
        pGrid->U[k][j][i].M1-=dtodx2*(x2Flux[k][j+1][i].Mz-x2Flux[k][j][i].Mz);
        pGrid->U[k][j][i].M2-=dtodx2*(x2Flux[k][j+1][i].Mx-x2Flux[k][j][i].Mx);
        pGrid->U[k][j][i].M3-=dtodx2*(x2Flux[k][j+1][i].My-x2Flux[k][j][i].My);
#ifndef ISOTHERMAL
        pGrid->U[k][j][i].E -=dtodx2*(x2Flux[k][j+1][i].E -x2Flux[k][j][i].E );
#endif /* ISOTHERMAL */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++)
          pGrid->U[k][j][i].s[n] -= dtodx2*(x2Flux[k][j+1][i].s[n]
                                          - x2Flux[k][j  ][i].s[n]);
#endif
      }
    }
  }


/*--- Step 13c ----------------------------------------------------------------
 * Update HYDRO cell-centered variables in pGrid using x3-fluxes
 */

  for (k=kb+1; k<=kt-1; k++) {
    for (j=jb+1; j<=jt-1; j++) {
      for (i=ib+1; i<=it-1; i++) {
        pGrid->U[k][j][i].d -=dtodx3*(x3Flux[k+1][j][i].d -x3Flux[k][j][i].d );
        pGrid->U[k][j][i].M1-=dtodx3*(x3Flux[k+1][j][i].My-x3Flux[k][j][i].My);
        pGrid->U[k][j][i].M2-=dtodx3*(x3Flux[k+1][j][i].Mz-x3Flux[k][j][i].Mz);
        pGrid->U[k][j][i].M3-=dtodx3*(x3Flux[k+1][j][i].Mx-x3Flux[k][j][i].Mx);
#ifndef ISOTHERMAL
        pGrid->U[k][j][i].E -=dtodx3*(x3Flux[k+1][j][i].E -x3Flux[k][j][i].E );
#endif /* ISOTHERMAL */
#if (NSCALARS > 0)
        for (n=0; n<NSCALARS; n++)
          pGrid->U[k][j][i].s[n] -= dtodx3*(x3Flux[k+1][j][i].s[n]
                                          - x3Flux[k  ][j][i].s[n]);
#endif

#ifndef FIRST_ORDER_FLUX_CORRECTION
        if (!(pGrid->U[k][j][i].d > 0.0))
          ath_error("Step 13c: pGrid->U[%d][%d][%d].d = %3.2e\n",
                        pGrid->kdisp+k, pGrid->jdisp+j, pGrid->idisp+i,
                        pGrid->U[k][j][i].d);
#endif /* FIRST_ORDER_FLUX_CORRECTION */
      }
    }
  }

/*--- Step 13d ----------------------------------------------------------------
 * If cell-centered densities have gone negative, correct cell-centered
 * variables by using 1st order fluxes instead of higher order
 */

#ifdef FIRST_ORDER_FLUX_CORRECTION
  first_order_correction(pGrid);
#endif /* FIRST_ORDER_FLUX_CORRECTION */

/*--- Step 15 -----------------------------------------------------------------
 * LAST STEP!
 * Set cell centered magnetic fields to average of updated face centered fields.
 */

#ifdef MHD
  for (k=kb+1; k<=kt-1; k++) {
    for (j=jb+1; j<=jt-1; j++) {
      for (i=ib+1; i<=it-1; i++) {
        pGrid->U[k][j][i].B1c = 0.5*(pGrid->B1i[k][j][i]+pGrid->B1i[k][j][i+1]);
        pGrid->U[k][j][i].B2c = 0.5*(pGrid->B2i[k][j][i]+pGrid->B2i[k][j+1][i]);
        pGrid->U[k][j][i].B3c = 0.5*(pGrid->B3i[k][j][i]+pGrid->B3i[k+1][j][i]);
      }
    }
  }
#endif /* MHD */

  return;
}


/*----------------------------------------------------------------------------*/
/* integrate_destruct_3d:  Free temporary integration arrays */

void integrate_destruct_3d(void)
{
#ifdef MHD
  if (emf1 != NULL) free_3d_array(emf1);
  if (emf2 != NULL) free_3d_array(emf2);
  if (emf3 != NULL) free_3d_array(emf3);
  if (emf1_cc != NULL) free_3d_array(emf1_cc);
  if (emf2_cc != NULL) free_3d_array(emf2_cc);
  if (emf3_cc != NULL) free_3d_array(emf3_cc);
#endif /* MHD */
#ifdef H_CORRECTION
  if (eta1 != NULL) free_3d_array(eta1);
  if (eta2 != NULL) free_3d_array(eta2);
  if (eta3 != NULL) free_3d_array(eta3);
#endif /* H_CORRECTION */

  if (Bxc != NULL) free(Bxc);
  if (Bxi != NULL) free(Bxi);
  if (B1_x1Face != NULL) free_3d_array(B1_x1Face);
  if (B2_x2Face != NULL) free_3d_array(B2_x2Face);
  if (B3_x3Face != NULL) free_3d_array(B3_x3Face);

  if (U1d      != NULL) free(U1d);
  if (Ul       != NULL) free(Ul);
  if (Ur       != NULL) free(Ur);
  if (W        != NULL) free(W);
  if (Wl       != NULL) free(Wl);
  if (Wr       != NULL) free(Wr);

  if (Ul_x1Face != NULL) free_3d_array(Ul_x1Face);
  if (Ur_x1Face != NULL) free_3d_array(Ur_x1Face);
  if (Ul_x2Face != NULL) free_3d_array(Ul_x2Face);
  if (Ur_x2Face != NULL) free_3d_array(Ur_x2Face);
  if (Ul_x3Face != NULL) free_3d_array(Ul_x3Face);
  if (Ur_x3Face != NULL) free_3d_array(Ur_x3Face);
  if (x1Flux    != NULL) free_3d_array(x1Flux);
  if (x2Flux    != NULL) free_3d_array(x2Flux);
  if (x3Flux    != NULL) free_3d_array(x3Flux);

  if (Uhalf    != NULL) free_3d_array(Uhalf);
#ifdef FIRST_ORDER_FLUX_CORRECTION
  if (Ineg     != NULL) free_3d_array(Ineg);
#endif /* FIRST_ORDER_FLUX_CORRECTION */

  return;
}

/*----------------------------------------------------------------------------*/
/* integrate_init_3d: Allocate temporary integration arrays */

void integrate_init_3d(int nx1, int nx2, int nx3)
{
  int nmax;
  int Nx1 = nx1 + 2*nghost;
  int Nx2 = nx2 + 2*nghost;
  int Nx3 = nx3 + 2*nghost;
  nmax = MAX(MAX(Nx1,Nx2),Nx3);

/* Make sure we have enough ghost cells to proceed.  If we have more
 * ghost cells than necessary, we'll fully time-evolve them instead of
 * ignoring them. */
#ifdef FIRST_ORDER
  int minghost = 2;
#endif /* FIRST_ORDER */
#ifdef SECOND_ORDER
  int minghost = 3;
#endif /* SECOND_ORDER */
#ifdef THIRD_ORDER
  int minghost = 4;
#endif /* THIRD_ORDER */
#if defined(MHD) && defined(H_CORRECTION)
  minghost++;
#endif
#ifdef FIRST_ORDER_FLUX_CORRECTION
  minghost++;
#endif /* FIRST_ORDER_FLUX_CORRECTION */
  if (nghost < minghost)
    ath_error("[integrate_init_3d]: The VL integrator requires at least %d ghost zones with this configuration.\n", minghost);

#ifdef MHD
  if ((emf1 = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
  if ((emf2 = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
  if ((emf3 = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;

  if ((emf1_cc = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
  if ((emf2_cc = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
  if ((emf3_cc = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
#endif /* MHD */
#ifdef H_CORRECTION
  if ((eta1 = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
  if ((eta2 = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
  if ((eta3 = (Real***)calloc_3d_array(Nx3, Nx2, Nx1, sizeof(Real))) == NULL)
    goto on_error;
#endif /* H_CORRECTION */

  if ((Bxc = (Real*)malloc(nmax*sizeof(Real))) == NULL) goto on_error;
  if ((Bxi = (Real*)malloc(nmax*sizeof(Real))) == NULL) goto on_error;

  if ((B1_x1Face = (Real***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Real))) == NULL)    goto on_error;
  if ((B2_x2Face = (Real***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Real))) == NULL)    goto on_error;
  if ((B3_x3Face = (Real***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Real))) == NULL)    goto on_error;

  if ((U1d =      (Cons1D*)malloc(nmax*sizeof(Cons1D))) == NULL) goto on_error;
  if ((Ul  =      (Cons1D*)malloc(nmax*sizeof(Cons1D))) == NULL) goto on_error;
  if ((Ur  =      (Cons1D*)malloc(nmax*sizeof(Cons1D))) == NULL) goto on_error;
  if ((W   =      (Prim1D*)malloc(nmax*sizeof(Prim1D))) == NULL) goto on_error;
  if ((Wl  =      (Prim1D*)malloc(nmax*sizeof(Prim1D))) == NULL) goto on_error;
  if ((Wr  =      (Prim1D*)malloc(nmax*sizeof(Prim1D))) == NULL) goto on_error;

  if ((Ul_x1Face = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D)))
    == NULL) goto on_error;
  if ((Ur_x1Face = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D)))
    == NULL) goto on_error;
  if ((Ul_x2Face = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D)))
    == NULL) goto on_error;
  if ((Ur_x2Face = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D)))
    == NULL) goto on_error;
  if ((Ul_x3Face = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D)))
    == NULL) goto on_error;
  if ((Ur_x3Face = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D)))
    == NULL) goto on_error;
  if ((x1Flux    = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D))) 
    == NULL) goto on_error;
  if ((x2Flux    = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D))) 
    == NULL) goto on_error;
  if ((x3Flux    = (Cons1D***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Cons1D))) 
    == NULL) goto on_error;

  if ((Uhalf = (Gas***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(Gas))) == NULL)
    goto on_error;
#ifdef FIRST_ORDER_FLUX_CORRECTION
  if ((Ineg = (char***)calloc_3d_array(Nx3,Nx2,Nx1, sizeof(char))) == NULL)
    goto on_error;
#endif /* FIRST_ORDER_FLUX_CORRECTION */

  return;

  on_error:
  integrate_destruct();
  ath_error("[integrate_init]: malloc returned a NULL pointer\n");
}


/*=========================== PRIVATE FUNCTIONS ==============================*/

/*----------------------------------------------------------------------------*/
/* integrate_emf1_corner()
 * integrate_emf2_corner()
 * integrate_emf3_corner()
 *   Integrates face centered B-fluxes to compute corner EMFs.  Note:
 *   x1Flux.By = VxBy - BxVy = v1*b2-b1*v2 = -EMFZ
 *   x1Flux.Bz = VxBz - BxVz = v1*b3-b1*v3 = EMFY
 *   x2Flux.By = VxBy - BxVy = v2*b3-b2*v3 = -EMFX
 *   x2Flux.Bz = VxBz - BxVz = v2*b1-b2*v1 = EMFZ
 *   x3Flux.By = VxBy - BxVy = v3*b1-b3*v1 = -EMFY
 *   x3Flux.Bz = VxBz - BxVz = v3*b2-b3*v2 = EMFX
 */ 

#ifdef MHD
static void integrate_emf1_corner(const Grid *pGrid)
{
  int i, is = pGrid->is, ie = pGrid->ie;
  int j, js = pGrid->js, je = pGrid->je;
  int k, ks = pGrid->ks, ke = pGrid->ke;
  int il = is - nghost, iu = ie + nghost;
  int jl = js - nghost, ju = je + nghost;
  int kl = ks - nghost, ku = ke + nghost;
  Real de1_l2, de1_r2, de1_l3, de1_r3;

  for (k=kl+1; k<=ku; k++) {
    for (j=jl+1; j<=ju; j++) {
      for (i=il+1; i<=iu-1; i++) {
/* NOTE: The x2-Flux of By is -E1. */
/*       The x3-Flux of Bz is +E1. */
	if (x2Flux[k-1][j][i].d > 0.0)
	  de1_l3 = x3Flux[k][j-1][i].Bz - emf1_cc[k-1][j-1][i];
	else if (x2Flux[k-1][j][i].d < 0.0)
	  de1_l3 = x3Flux[k][j][i].Bz - emf1_cc[k-1][j][i];
	else {
	  de1_l3 = 0.5*(x3Flux[k][j-1][i].Bz - emf1_cc[k-1][j-1][i] +
			x3Flux[k][j  ][i].Bz - emf1_cc[k-1][j  ][i] );
	}

	if (x2Flux[k][j][i].d > 0.0)
	  de1_r3 = x3Flux[k][j-1][i].Bz - emf1_cc[k][j-1][i];
	else if (x2Flux[k][j][i].d < 0.0)
	  de1_r3 = x3Flux[k][j][i].Bz - emf1_cc[k][j][i];
	else {
	  de1_r3 = 0.5*(x3Flux[k][j-1][i].Bz - emf1_cc[k][j-1][i] +
			x3Flux[k][j  ][i].Bz - emf1_cc[k][j  ][i] );
	}

	if (x3Flux[k][j-1][i].d > 0.0)
	  de1_l2 = -x2Flux[k-1][j][i].By - emf1_cc[k-1][j-1][i];
	else if (x3Flux[k][j-1][i].d < 0.0)
	  de1_l2 = -x2Flux[k][j][i].By - emf1_cc[k][j-1][i];
	else {
	  de1_l2 = 0.5*(-x2Flux[k-1][j][i].By - emf1_cc[k-1][j-1][i]
			-x2Flux[k  ][j][i].By - emf1_cc[k  ][j-1][i] );
	}

	if (x3Flux[k][j][i].d > 0.0)
	  de1_r2 = -x2Flux[k-1][j][i].By - emf1_cc[k-1][j][i];
	else if (x3Flux[k][j][i].d < 0.0)
	  de1_r2 = -x2Flux[k][j][i].By - emf1_cc[k][j][i];
	else {
	  de1_r2 = 0.5*(-x2Flux[k-1][j][i].By - emf1_cc[k-1][j][i]
			-x2Flux[k  ][j][i].By - emf1_cc[k  ][j][i] );
	}

        emf1[k][j][i] = 0.25*(  x3Flux[k][j][i].Bz + x3Flux[k][j-1][i].Bz
                              - x2Flux[k][j][i].By - x2Flux[k-1][j][i].By 
			      + de1_l2 + de1_r2 + de1_l3 + de1_r3);
      }
    }
  }

  return;
}

static void integrate_emf2_corner(const Grid *pGrid)
{
  int i, is = pGrid->is, ie = pGrid->ie;
  int j, js = pGrid->js, je = pGrid->je;
  int k, ks = pGrid->ks, ke = pGrid->ke;
  int il = is - nghost, iu = ie + nghost;
  int jl = js - nghost, ju = je + nghost;
  int kl = ks - nghost, ku = ke + nghost;
  Real de2_l1, de2_r1, de2_l3, de2_r3;

  for (k=kl+1; k<=ku; k++) {
    for (j=jl+1; j<=ju-1; j++) {
      for (i=il+1; i<=iu; i++) {
/* NOTE: The x1-Flux of Bz is +E2. */
/*       The x3-Flux of By is -E2. */
	if (x1Flux[k-1][j][i].d > 0.0)
	  de2_l3 = -x3Flux[k][j][i-1].By - emf2_cc[k-1][j][i-1];
	else if (x1Flux[k-1][j][i].d < 0.0)
	  de2_l3 = -x3Flux[k][j][i].By - emf2_cc[k-1][j][i];
	else {
	  de2_l3 = 0.5*(-x3Flux[k][j][i-1].By - emf2_cc[k-1][j][i-1] 
			-x3Flux[k][j][i  ].By - emf2_cc[k-1][j][i  ] );
	}

	if (x1Flux[k][j][i].d > 0.0)
	  de2_r3 = -x3Flux[k][j][i-1].By - emf2_cc[k][j][i-1];
	else if (x1Flux[k][j][i].d < 0.0)
	  de2_r3 = -x3Flux[k][j][i].By - emf2_cc[k][j][i];
	else {
	  de2_r3 = 0.5*(-x3Flux[k][j][i-1].By - emf2_cc[k][j][i-1] 
			-x3Flux[k][j][i  ].By - emf2_cc[k][j][i  ] );
	}

	if (x3Flux[k][j][i-1].d > 0.0)
	  de2_l1 = x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i-1];
	else if (x3Flux[k][j][i-1].d < 0.0)
	  de2_l1 = x1Flux[k][j][i].Bz - emf2_cc[k][j][i-1];
	else {
	  de2_l1 = 0.5*(x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i-1] +
			x1Flux[k  ][j][i].Bz - emf2_cc[k  ][j][i-1] );
	}

	if (x3Flux[k][j][i].d > 0.0)
	  de2_r1 = x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i];
	else if (x3Flux[k][j][i].d < 0.0)
	  de2_r1 = x1Flux[k][j][i].Bz - emf2_cc[k][j][i];
	else {
	  de2_r1 = 0.5*(x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i] +
			x1Flux[k  ][j][i].Bz - emf2_cc[k-1][j][i] );
	}

	emf2[k][j][i] = 0.25*(  x1Flux[k][j][i].Bz + x1Flux[k-1][j][i  ].Bz
                              - x3Flux[k][j][i].By - x3Flux[k  ][j][i-1].By
			      + de2_l1 + de2_r1 + de2_l3 + de2_r3);
      }
    }
  }

  return;
}

static void integrate_emf3_corner(const Grid *pGrid)
{
  int i, is = pGrid->is, ie = pGrid->ie;
  int j, js = pGrid->js, je = pGrid->je;
  int k, ks = pGrid->ks, ke = pGrid->ke;
  int il = is - nghost, iu = ie + nghost;
  int jl = js - nghost, ju = je + nghost;
  int kl = ks - nghost, ku = ke + nghost;
  Real de3_l1, de3_r1, de3_l2, de3_r2;

  for (k=kl+1; k<=ku-1; k++) {
    for (j=jl+1; j<=ju; j++) {
      for (i=il+1; i<=iu; i++) {
/* NOTE: The x1-Flux of By is -E3. */
/*       The x2-Flux of Bx is +E3. */
	if (x1Flux[k][j-1][i].d > 0.0)
	  de3_l2 = x2Flux[k][j][i-1].Bz - emf3_cc[k][j-1][i-1];
	else if (x1Flux[k][j-1][i].d < 0.0)
	  de3_l2 = x2Flux[k][j][i].Bz - emf3_cc[k][j-1][i];
	else {
	  de3_l2 = 0.5*(x2Flux[k][j][i-1].Bz - emf3_cc[k][j-1][i-1] + 
			x2Flux[k][j][i  ].Bz - emf3_cc[k][j-1][i  ] );
	}

	if (x1Flux[k][j][i].d > 0.0)
	  de3_r2 = x2Flux[k][j][i-1].Bz - emf3_cc[k][j][i-1];
	else if (x1Flux[k][j][i].d < 0.0)
	  de3_r2 = x2Flux[k][j][i].Bz - emf3_cc[k][j][i];
	else {
	  de3_r2 = 0.5*(x2Flux[k][j][i-1].Bz - emf3_cc[k][j][i-1] + 
			x2Flux[k][j][i  ].Bz - emf3_cc[k][j][i  ] );
	}

	if (x2Flux[k][j][i-1].d > 0.0)
	  de3_l1 = -x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i-1];
	else if (x2Flux[k][j][i-1].d < 0.0)
	  de3_l1 = -x1Flux[k][j][i].By - emf3_cc[k][j][i-1];
	else {
	  de3_l1 = 0.5*(-x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i-1]
			-x1Flux[k][j  ][i].By - emf3_cc[k][j  ][i-1] );
	}

	if (x2Flux[k][j][i].d > 0.0)
	  de3_r1 = -x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i];
	else if (x2Flux[k][j][i].d < 0.0)
	  de3_r1 = -x1Flux[k][j][i].By - emf3_cc[k][j][i];
	else {
	  de3_r1 = 0.5*(-x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i]
			-x1Flux[k][j  ][i].By - emf3_cc[k][j  ][i] );
	}

	emf3[k][j][i] = 0.25*(  x2Flux[k][j  ][i-1].Bz + x2Flux[k][j][i].Bz
			      - x1Flux[k][j-1][i  ].By - x1Flux[k][j][i].By
			      + de3_l1 + de3_r1 + de3_l2 + de3_r2);
      }
    }
  }

  return;
}
#endif /* MHD */

/*----------------------------------------------------------------------------*/
/* first_order_correction()
 *   Drop to first order fluxes for interfaces where higher-order fluxes
 *   would cause cell-centered densities to go negative.  There needs to be
 *   at least 5 ghost cells for this to work at third order.  This is not
 *   presently compatible with the H-correction.  If negative densities
 *   persist at the end of this function, call ath_error().
 *
 *   Ineg: Contains flags indicating which interfaces need to be modified
 *         for a given cell.
 */

#ifdef FIRST_ORDER_FLUX_CORRECTION
static void first_order_correction(const Grid *pGrid)
{
  Real dtodx1 = pGrid->dt/pGrid->dx1;
  Real dtodx2 = pGrid->dt/pGrid->dx2;
  Real dtodx3 = pGrid->dt/pGrid->dx3;
  int i, is = pGrid->is, ie = pGrid->ie;
  int j, js = pGrid->js, je = pGrid->je;
  int k, ks = pGrid->ks, ke = pGrid->ke;
  int il = is - nghost, iu = ie + nghost;
  int jl = js - nghost, ju = je + nghost;
  int kl = ks - nghost, ku = ke + nghost;
#ifdef MHD
  Real de1_l2, de1_r2, de1_l3, de1_r3;
  Real de2_l1, de2_r1, de2_l3, de2_r3;
  Real de3_l1, de3_r1, de3_l2, de3_r2;
#endif /* MHD */

/* Set widest loop limits possible */
#ifdef FIRST_ORDER
  int ib = il+1, it = iu-1;
  int jb = jl+1, jt = ju-1;
  int kb = kl+1, kt = ku-1;
#endif /* FIRST_ORDER */
#ifdef SECOND_ORDER
  int ib = il+2, it = iu-2;
  int jb = jl+2, jt = ju-2;
  int kb = kl+2, kt = ku-2;
#endif /* SECOND_ORDER */
#ifdef THIRD_ORDER
  int ib = il+3, it = iu-3;
  int jb = jl+3, jt = ju-3;
  int kb = kl+3, kt = ku-3;
#endif /* THIRD_ORDER */

  int negcount = 0;

  /* Initialize our interface flags */
  int Nx1 = pGrid->Nx1 + 2*nghost;
  int Nx2 = pGrid->Nx2 + 2*nghost;
  int Nx3 = pGrid->Nx3 + 2*nghost;
  memset(Ineg[0][0], 0, Nx1*Nx2*Nx3*sizeof(char));

  /* Find negative cell-centered densities on the grid */
  for (k=kb+1; k<=kt-1; k++) {
    for (j=jb+1; j<=jt-1; j++) {
      for (i=ib+1; i<=it-1; i++) {
        if (pGrid->U[k][j][i].d <= 0.0) {
          /* All interfaces of this cell will be modified */

          /* Left interfaces: */
          Ineg[k][j][i] |= correct_hydro_all | correct_mhd_all;

          /* Right interfaces */
          Ineg[k][j][i+1] |= correct_hydro_x1 | correct_mhd_x1;
          Ineg[k][j+1][i] |= correct_hydro_x2 | correct_mhd_x2;
          Ineg[k+1][j][i] |= correct_hydro_x3 | correct_mhd_x3;

          /* ath_perr(1,"RANK %d Warning: negative density in [%d][%d][%d]\n",
                          pGrid->my_id,pGrid->kdisp+k,pGrid->jdisp+j,
                          pGrid->idisp+i); */
          negcount++;
        }
      }
    }
  }

  if (negcount > 0) {
    ath_perr(-1,"RANK %d Warning: %d negative densities being corrected\n",
        pGrid->my_id, negcount);

    /* Modifying these fluxes will impact the hydro variables only for
     * the cells adjacent to the interfaces.  For the magnetic fields
     * however, the impact of modifying the fluxes has a larger reach. */
#ifdef MHD
    /* Set flags for the additional interface magnetic fields that will
     * require modification. */
    /* Loop over interfaces, not cell centers */
    for (k=kb+1; k<=kt; k++) {
      for (j=jb+1; j<=jt; j++) {
        for (i=ib+1; i<=it; i++) {
          if (Ineg[k][j][i] & correct_hydro_x1) {
            /* via emf2[k][j][i] */
            Ineg[k-1][j][i] |= correct_mhd_x1;
            Ineg[k][j][i-1] |= correct_mhd_x3;

            /* additional via emf2[k+1][j][i] */
            Ineg[k+1][j][i] |= correct_mhd_x1 | correct_mhd_x3;
            Ineg[k+1][j][i-1] |= correct_mhd_x3;

            /* additional via emf3[k][j][i] */
            Ineg[k][j-1][i] |= correct_mhd_x1;
            Ineg[k][j][i-1] |= correct_mhd_x2;

            /* additional via emf3[k][j+1][i] */
            Ineg[k][j+1][i] |= correct_mhd_x1 | correct_mhd_x2;
            Ineg[k][j+1][i-1] |= correct_mhd_x2;
          }

          if (Ineg[k][j][i] & correct_hydro_x2) {
            /* via emf1[k][j][i] */
            Ineg[k-1][j][i] |= correct_mhd_x2;
            Ineg[k][j-1][i] |= correct_mhd_x3;

            /* additional via emf1[k+1][j][i] */
            Ineg[k+1][j][i] |= correct_mhd_x2 | correct_mhd_x3;
            Ineg[k+1][j-1][i] |= correct_mhd_x3;

            /* additional via emf3[k][j][i] */
            Ineg[k][j-1][i] |= correct_mhd_x1;
            Ineg[k][j][i-1] |= correct_mhd_x2;

            /* additional via emf3[k][j][i+1] */
            Ineg[k][j][i+1] |= correct_mhd_x1 | correct_mhd_x2;
            Ineg[k][j-1][i+1] |= correct_mhd_x1;
          }

          if (Ineg[k][j][i] & correct_hydro_x3) {
            /* via emf1[k][j][i] */
            Ineg[k-1][j][i] |= correct_mhd_x2;
            Ineg[k][j-1][i] |= correct_mhd_x3;

            /* additional via emf1[k][j+1][i] */
            Ineg[k][j+1][i] |= correct_mhd_x2 | correct_mhd_x3;
            Ineg[k-1][j+1][i] |= correct_mhd_x2;

            /* additional via emf2[k][j][i] */
            Ineg[k-1][j][i] |= correct_mhd_x1;
            Ineg[k][j][i-1] |= correct_mhd_x3;

            /* additional via emf2[k][j][i+1] */
            Ineg[k][j][i+1] |= correct_mhd_x1 | correct_mhd_x3;
            Ineg[k-1][j][i+1] |= correct_mhd_x1;
          }
        }
      }
    }
#endif /* MHD */

    /* Undo correction of cell-centered values due to higher-order fluxes
     * (steps 13a-c) for hydro interfaces flagged above */
    for (k=kb+2; k<=kt-2; k++) {
      for (j=jb+2; j<=jt-2; j++) {
        for (i=ib+2; i<=it-2; i++) {
          if (Ineg[k][j][i] & correct_hydro_x1) {
            /* Uncorrect using x1 flux through left interface */
            pGrid->U[k][j][i].d -=dtodx1*x1Flux[k][j][i].d ;
            pGrid->U[k][j][i].M1-=dtodx1*x1Flux[k][j][i].Mx;
            pGrid->U[k][j][i].M2-=dtodx1*x1Flux[k][j][i].My;
            pGrid->U[k][j][i].M3-=dtodx1*x1Flux[k][j][i].Mz;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E -=dtodx1*x1Flux[k][j][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j][i+1] & correct_hydro_x1) {
            /* Uncorrect using x1 flux through right interface */
            pGrid->U[k][j][i].d +=dtodx1*x1Flux[k][j][i+1].d;
            pGrid->U[k][j][i].M1+=dtodx1*x1Flux[k][j][i+1].Mx;
            pGrid->U[k][j][i].M2+=dtodx1*x1Flux[k][j][i+1].My;
            pGrid->U[k][j][i].M3+=dtodx1*x1Flux[k][j][i+1].Mz;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E +=dtodx1*x1Flux[k][j][i+1].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j][i] & correct_hydro_x2) {
            /* Uncorrect using x2 flux through left interface */
            pGrid->U[k][j][i].d -=dtodx2*x2Flux[k][j][i].d;
            pGrid->U[k][j][i].M1-=dtodx2*x2Flux[k][j][i].Mz;
            pGrid->U[k][j][i].M2-=dtodx2*x2Flux[k][j][i].Mx;
            pGrid->U[k][j][i].M3-=dtodx2*x2Flux[k][j][i].My;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E -=dtodx2*x2Flux[k][j][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j+1][i] & correct_hydro_x2) {
            /* Uncorrect using x2 flux through right interface */
            pGrid->U[k][j][i].d +=dtodx2*x2Flux[k][j+1][i].d;
            pGrid->U[k][j][i].M1+=dtodx2*x2Flux[k][j+1][i].Mz;
            pGrid->U[k][j][i].M2+=dtodx2*x2Flux[k][j+1][i].Mx;
            pGrid->U[k][j][i].M3+=dtodx2*x2Flux[k][j+1][i].My;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E +=dtodx2*x2Flux[k][j+1][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j][i] & correct_hydro_x3) {
            /* Uncorrect using x3 flux through left interface */
            pGrid->U[k][j][i].d -=dtodx3*x3Flux[k][j][i].d;
            pGrid->U[k][j][i].M1-=dtodx3*x3Flux[k][j][i].My;
            pGrid->U[k][j][i].M2-=dtodx3*x3Flux[k][j][i].Mz;
            pGrid->U[k][j][i].M3-=dtodx3*x3Flux[k][j][i].Mx;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E -=dtodx3*x3Flux[k][j][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k+1][j][i] & correct_hydro_x3) {
            /* Uncorrect using x3 flux through right interface */
            pGrid->U[k][j][i].d +=dtodx3*x3Flux[k+1][j][i].d;
            pGrid->U[k][j][i].M1+=dtodx3*x3Flux[k+1][j][i].My;
            pGrid->U[k][j][i].M2+=dtodx3*x3Flux[k+1][j][i].Mz;
            pGrid->U[k][j][i].M3+=dtodx3*x3Flux[k+1][j][i].Mx;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E +=dtodx3*x3Flux[k+1][j][i].E;
#endif /* ISOTHERMAL */
          }
        }
      }
    }

    /* Undo correction of interface magnetic fields (step 11) for interface
     * fields flagged above */
#ifdef MHD
    /* Loop over interfaces, not cell centers */
    for (k=kb+2; k<=kt-1; k++) {
      for (j=jb+2; j<=jt-1; j++) {
        for (i=ib+2; i<=it-1; i++) {
          if (Ineg[k][j][i] & correct_mhd_x1) {
            pGrid->B1i[k][j][i] -= dtodx3*(emf2[k+1][j  ][i  ] - emf2[k][j][i])-
                                   dtodx2*(emf3[k  ][j+1][i  ] - emf3[k][j][i]);
          }
          if (Ineg[k][j][i] & correct_mhd_x2) {
            pGrid->B2i[k][j][i] -= dtodx1*(emf3[k  ][j  ][i+1] - emf3[k][j][i])-
                                   dtodx3*(emf1[k+1][j  ][i  ] - emf1[k][j][i]);
          }
          if (Ineg[k][j][i] & correct_mhd_x3) {
            pGrid->B3i[k][j][i] -= dtodx2*(emf1[k  ][j+1][i  ] - emf1[k][j][i])-
                                   dtodx1*(emf2[k  ][j  ][i+1] - emf2[k][j][i]);
          }
        }
      }
    }
#endif /* MHD */

    /* Calculate 1st order L/R states from Uhalf and then fluxes from those
     * L/R states (replacement for steps 7a-c and 9a-c) for hydro interfaces
     * flagged above */
    /* Loop over interfaces, not cell centers */
    for (k=kb+2; k<=kt-1; k++) {
      for (j=jb+2; j<=jt-1; j++) {
        for (i=ib+2; i<=it-1; i++) {
          if (Ineg[k][j][i] & correct_hydro_x1) {
            Ul_x1Face[k][j][i].d = Uhalf[k][j][i-1].d;
            Ul_x1Face[k][j][i].Mx = Uhalf[k][j][i-1].M1;
            Ul_x1Face[k][j][i].My = Uhalf[k][j][i-1].M2;
            Ul_x1Face[k][j][i].Mz = Uhalf[k][j][i-1].M3;
#ifndef ISOTHERMAL
            Ul_x1Face[k][j][i].E = Uhalf[k][j][i-1].E;
#endif /* ISOTHERMAL */

            Ur_x1Face[k][j][i].d = Uhalf[k][j][i].d;
            Ur_x1Face[k][j][i].Mx = Uhalf[k][j][i].M1;
            Ur_x1Face[k][j][i].My = Uhalf[k][j][i].M2;
            Ur_x1Face[k][j][i].Mz = Uhalf[k][j][i].M3;
#ifndef ISOTHERMAL
            Ur_x1Face[k][j][i].E = Uhalf[k][j][i].E;
#endif /* ISOTHERMAL */

            GET_FLUXES(B1_x1Face[k][j][i],Ul_x1Face[k][j][i],Ur_x1Face[k][j][i]
              ,&x1Flux[k][j][i]);
          }

          if (Ineg[k][j][i] & correct_hydro_x2) {
            Ul_x2Face[k][j][i].d = Uhalf[k][j-1][i].d;
            Ul_x2Face[k][j][i].Mx = Uhalf[k][j-1][i].M2;
            Ul_x2Face[k][j][i].My = Uhalf[k][j-1][i].M3;
            Ul_x2Face[k][j][i].Mz = Uhalf[k][j-1][i].M1;
#ifndef ISOTHERMAL
            Ul_x2Face[k][j][i].E = Uhalf[k][j-1][i].E;
#endif /* ISOTHERMAL */

            Ur_x2Face[k][j][i].d = Uhalf[k][j][i].d;
            Ur_x2Face[k][j][i].Mx = Uhalf[k][j][i].M2;
            Ur_x2Face[k][j][i].My = Uhalf[k][j][i].M3;
            Ur_x2Face[k][j][i].Mz = Uhalf[k][j][i].M1;
#ifndef ISOTHERMAL
            Ur_x2Face[k][j][i].E = Uhalf[k][j][i].E;
#endif /* ISOTHERMAL */

            GET_FLUXES(B2_x2Face[k][j][i],Ul_x2Face[k][j][i],Ur_x2Face[k][j][i]
              ,&x2Flux[k][j][i]);
        }

          if (Ineg[k][j][i] & correct_hydro_x3) {
            Ul_x3Face[k][j][i].d = Uhalf[k-1][j][i].d;
            Ul_x3Face[k][j][i].Mx = Uhalf[k-1][j][i].M3;
            Ul_x3Face[k][j][i].My = Uhalf[k-1][j][i].M1;
            Ul_x3Face[k][j][i].Mz = Uhalf[k-1][j][i].M2;
#ifndef ISOTHERMAL
            Ul_x3Face[k][j][i].E = Uhalf[k-1][j][i].E;
#endif /* ISOTHERMAL */

            Ur_x3Face[k][j][i].d = Uhalf[k][j][i].d;
            Ur_x3Face[k][j][i].Mx = Uhalf[k][j][i].M3;
            Ur_x3Face[k][j][i].My = Uhalf[k][j][i].M1;
            Ur_x3Face[k][j][i].Mz = Uhalf[k][j][i].M2;
#ifndef ISOTHERMAL
            Ur_x3Face[k][j][i].E = Uhalf[k][j][i].E;
#endif /* ISOTHERMAL */

            GET_FLUXES(B3_x3Face[k][j][i],Ul_x3Face[k][j][i],Ur_x3Face[k][j][i]
              ,&x3Flux[k][j][i]);
          }
        }
      }
    }

   /* Now re-calculate corner emfs and correct interface magnetic fields
    * (replacement for step 11) for interface fields flagged above */
#ifdef MHD
    for (k=kb+2; k<=kt; k++) {
      for (j=jb+2; j<=jt; j++) {
        for (i=ib+2; i<=it-1; i++) {
          if ((Ineg[k][j][i] & correct_mhd_x2) ||
              (Ineg[k-1][j][i] & correct_mhd_x2) ||
              (Ineg[k][j][i] & correct_mhd_x3) ||
              (Ineg[k][j-1][i] & correct_mhd_x3)) {
            /* integrate_emf1_corner: emf1[k][j][i] */

	    if (x2Flux[k-1][j][i].d > 0.0)
	      de1_l3 = x3Flux[k][j-1][i].Bz - emf1_cc[k-1][j-1][i];
	    else if (x2Flux[k-1][j][i].d < 0.0)
	      de1_l3 = x3Flux[k][j][i].Bz - emf1_cc[k-1][j][i];
	    else {
	      de1_l3 = 0.5*(x3Flux[k][j-1][i].Bz - emf1_cc[k-1][j-1][i] +
			    x3Flux[k][j  ][i].Bz - emf1_cc[k-1][j  ][i] );
	    }

	    if (x2Flux[k][j][i].d > 0.0)
	      de1_r3 = x3Flux[k][j-1][i].Bz - emf1_cc[k][j-1][i];
	    else if (x2Flux[k][j][i].d < 0.0)
	      de1_r3 = x3Flux[k][j][i].Bz - emf1_cc[k][j][i];
	    else {
	      de1_r3 = 0.5*(x3Flux[k][j-1][i].Bz - emf1_cc[k][j-1][i] +
			    x3Flux[k][j  ][i].Bz - emf1_cc[k][j  ][i] );
	    }

	    if (x3Flux[k][j-1][i].d > 0.0)
	      de1_l2 = -x2Flux[k-1][j][i].By - emf1_cc[k-1][j-1][i];
	    else if (x3Flux[k][j-1][i].d < 0.0)
	      de1_l2 = -x2Flux[k][j][i].By - emf1_cc[k][j-1][i];
	    else {
	      de1_l2 = 0.5*(-x2Flux[k-1][j][i].By - emf1_cc[k-1][j-1][i]
			    -x2Flux[k  ][j][i].By - emf1_cc[k  ][j-1][i] );
	    }

	    if (x3Flux[k][j][i].d > 0.0)
	      de1_r2 = -x2Flux[k-1][j][i].By - emf1_cc[k-1][j][i];
	    else if (x3Flux[k][j][i].d < 0.0)
	      de1_r2 = -x2Flux[k][j][i].By - emf1_cc[k][j][i];
	    else {
	      de1_r2 = 0.5*(-x2Flux[k-1][j][i].By - emf1_cc[k-1][j][i]
			    -x2Flux[k  ][j][i].By - emf1_cc[k  ][j][i] );
	    }

            emf1[k][j][i] = 0.25*(  x3Flux[k][j][i].Bz + x3Flux[k][j-1][i].Bz
                                  - x2Flux[k][j][i].By - x2Flux[k-1][j][i].By 
			          + de1_l2 + de1_r2 + de1_l3 + de1_r3);
          }
        }
      }
    }

    for (k=kb+2; k<=kt; k++) {
      for (j=jb+2; j<=jt-1; j++) {
        for (i=ib+2; i<=it; i++) {
          if ((Ineg[k][j][i] & correct_mhd_x1) ||
              (Ineg[k-1][j][i] & correct_mhd_x1) ||
              (Ineg[k][j][i] & correct_mhd_x3) ||
              (Ineg[k][j][i-1] & correct_mhd_x3)) {
            /* integrate_emf2_corner: emf2[k][j][i] */

	    if (x1Flux[k-1][j][i].d > 0.0)
	      de2_l3 = -x3Flux[k][j][i-1].By - emf2_cc[k-1][j][i-1];
	    else if (x1Flux[k-1][j][i].d < 0.0)
	      de2_l3 = -x3Flux[k][j][i].By - emf2_cc[k-1][j][i];
	    else {
	      de2_l3 = 0.5*(-x3Flux[k][j][i-1].By - emf2_cc[k-1][j][i-1] 
			    -x3Flux[k][j][i  ].By - emf2_cc[k-1][j][i  ] );
	    }

	    if (x1Flux[k][j][i].d > 0.0)
	      de2_r3 = -x3Flux[k][j][i-1].By - emf2_cc[k][j][i-1];
	    else if (x1Flux[k][j][i].d < 0.0)
	      de2_r3 = -x3Flux[k][j][i].By - emf2_cc[k][j][i];
	    else {
	      de2_r3 = 0.5*(-x3Flux[k][j][i-1].By - emf2_cc[k][j][i-1] 
			    -x3Flux[k][j][i  ].By - emf2_cc[k][j][i  ] );
	    }

	    if (x3Flux[k][j][i-1].d > 0.0)
	      de2_l1 = x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i-1];
	    else if (x3Flux[k][j][i-1].d < 0.0)
	      de2_l1 = x1Flux[k][j][i].Bz - emf2_cc[k][j][i-1];
	    else {
	      de2_l1 = 0.5*(x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i-1] +
			    x1Flux[k  ][j][i].Bz - emf2_cc[k  ][j][i-1] );
	    }

	    if (x3Flux[k][j][i].d > 0.0)
	      de2_r1 = x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i];
	    else if (x3Flux[k][j][i].d < 0.0)
	      de2_r1 = x1Flux[k][j][i].Bz - emf2_cc[k][j][i];
	    else {
	      de2_r1 = 0.5*(x1Flux[k-1][j][i].Bz - emf2_cc[k-1][j][i] +
			    x1Flux[k  ][j][i].Bz - emf2_cc[k-1][j][i] );
	    }

	    emf2[k][j][i] = 0.25*(  x1Flux[k][j][i].Bz + x1Flux[k-1][j][i  ].Bz
                                  - x3Flux[k][j][i].By - x3Flux[k  ][j][i-1].By
			          + de2_l1 + de2_r1 + de2_l3 + de2_r3);
          }
        }
      }
    }

    for (k=kb+2; k<=kt-1; k++) {
      for (j=jb+2; j<=jt; j++) {
        for (i=ib+2; i<=it; i++) {
          if ((Ineg[k][j][i] & correct_mhd_x1) ||
              (Ineg[k][j-1][i] & correct_mhd_x1) ||
              (Ineg[k][j][i] & correct_mhd_x2) ||
              (Ineg[k][j][i-1] & correct_mhd_x2)) {
            /* integrate_emf3_corner: emf3[k][j][i] */

	    if (x1Flux[k][j-1][i].d > 0.0)
	      de3_l2 = x2Flux[k][j][i-1].Bz - emf3_cc[k][j-1][i-1];
	    else if (x1Flux[k][j-1][i].d < 0.0)
	      de3_l2 = x2Flux[k][j][i].Bz - emf3_cc[k][j-1][i];
	    else {
	      de3_l2 = 0.5*(x2Flux[k][j][i-1].Bz - emf3_cc[k][j-1][i-1] + 
			    x2Flux[k][j][i  ].Bz - emf3_cc[k][j-1][i  ] );
	    }

	    if (x1Flux[k][j][i].d > 0.0)
	      de3_r2 = x2Flux[k][j][i-1].Bz - emf3_cc[k][j][i-1];
	    else if (x1Flux[k][j][i].d < 0.0)
	      de3_r2 = x2Flux[k][j][i].Bz - emf3_cc[k][j][i];
	    else {
	      de3_r2 = 0.5*(x2Flux[k][j][i-1].Bz - emf3_cc[k][j][i-1] + 
			    x2Flux[k][j][i  ].Bz - emf3_cc[k][j][i  ] );
	    }

	    if (x2Flux[k][j][i-1].d > 0.0)
	      de3_l1 = -x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i-1];
	    else if (x2Flux[k][j][i-1].d < 0.0)
	      de3_l1 = -x1Flux[k][j][i].By - emf3_cc[k][j][i-1];
	    else {
	      de3_l1 = 0.5*(-x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i-1]
			    -x1Flux[k][j  ][i].By - emf3_cc[k][j  ][i-1] );
	    }

	    if (x2Flux[k][j][i].d > 0.0)
	      de3_r1 = -x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i];
	    else if (x2Flux[k][j][i].d < 0.0)
	      de3_r1 = -x1Flux[k][j][i].By - emf3_cc[k][j][i];
	    else {
	      de3_r1 = 0.5*(-x1Flux[k][j-1][i].By - emf3_cc[k][j-1][i]
			    -x1Flux[k][j  ][i].By - emf3_cc[k][j  ][i] );
	    }

	    emf3[k][j][i] = 0.25*(  x2Flux[k][j  ][i-1].Bz + x2Flux[k][j][i].Bz
			          - x1Flux[k][j-1][i  ].By - x1Flux[k][j][i].By
			          + de3_l1 + de3_r1 + de3_l2 + de3_r2);
          }
        }
      }
    }

    /* Loop over interfaces, not cell centers */
    for (k=kb+2; k<=kt-1; k++) {
      for (j=jb+2; j<=jt-1; j++) {
        for (i=ib+2; i<=it-1; i++) {
          if (Ineg[k][j][i] & correct_mhd_x1) {
            pGrid->B1i[k][j][i] += dtodx3*(emf2[k+1][j  ][i  ] - emf2[k][j][i])-
                                   dtodx2*(emf3[k  ][j+1][i  ] - emf3[k][j][i]);
          }
          if (Ineg[k][j][i] & correct_mhd_x2) {
            pGrid->B2i[k][j][i] += dtodx1*(emf3[k  ][j  ][i+1] - emf3[k][j][i])-
                                   dtodx3*(emf1[k+1][j  ][i  ] - emf1[k][j][i]);
          }
          if (Ineg[k][j][i] & correct_mhd_x3) {
            pGrid->B3i[k][j][i] += dtodx2*(emf1[k  ][j+1][i  ] - emf1[k][j][i])-
                                   dtodx1*(emf2[k  ][j  ][i+1] - emf2[k][j][i]);
          }
        }
      }
    }
#endif /* MHD */

   /* Now correct cell-centered values using 1st order fluxes for hydro
    * interfaces flagged above (replacement for steps 13a-c) */
    for (k=kb+2; k<=kt-2; k++) {
      for (j=jb+2; j<=jt-2; j++) {
        for (i=ib+2; i<=it-2; i++) {
          if (Ineg[k][j][i] & correct_hydro_x1) {
            /* Correct using x1 flux through left interface */
            pGrid->U[k][j][i].d +=dtodx1*x1Flux[k][j][i].d ;
            pGrid->U[k][j][i].M1+=dtodx1*x1Flux[k][j][i].Mx;
            pGrid->U[k][j][i].M2+=dtodx1*x1Flux[k][j][i].My;
            pGrid->U[k][j][i].M3+=dtodx1*x1Flux[k][j][i].Mz;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E +=dtodx1*x1Flux[k][j][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j][i+1] & correct_hydro_x1) {
            /* Correct using x1 flux through right interface */
            pGrid->U[k][j][i].d -=dtodx1*x1Flux[k][j][i+1].d;
            pGrid->U[k][j][i].M1-=dtodx1*x1Flux[k][j][i+1].Mx;
            pGrid->U[k][j][i].M2-=dtodx1*x1Flux[k][j][i+1].My;
            pGrid->U[k][j][i].M3-=dtodx1*x1Flux[k][j][i+1].Mz;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E -=dtodx1*x1Flux[k][j][i+1].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j][i] & correct_hydro_x2) {
            /* Correct using x2 flux through left interface */
            pGrid->U[k][j][i].d +=dtodx2*x2Flux[k][j][i].d;
            pGrid->U[k][j][i].M1+=dtodx2*x2Flux[k][j][i].Mz;
            pGrid->U[k][j][i].M2+=dtodx2*x2Flux[k][j][i].Mx;
            pGrid->U[k][j][i].M3+=dtodx2*x2Flux[k][j][i].My;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E +=dtodx2*x2Flux[k][j][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j+1][i] & correct_hydro_x2) {
            /* Correct using x2 flux through right interface */
            pGrid->U[k][j][i].d -=dtodx2*x2Flux[k][j+1][i].d;
            pGrid->U[k][j][i].M1-=dtodx2*x2Flux[k][j+1][i].Mz;
            pGrid->U[k][j][i].M2-=dtodx2*x2Flux[k][j+1][i].Mx;
            pGrid->U[k][j][i].M3-=dtodx2*x2Flux[k][j+1][i].My;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E -=dtodx2*x2Flux[k][j+1][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k][j][i] & correct_hydro_x3) {
            /* Correct using x3 flux through left interface */
            pGrid->U[k][j][i].d +=dtodx3*x3Flux[k][j][i].d;
            pGrid->U[k][j][i].M1+=dtodx3*x3Flux[k][j][i].My;
            pGrid->U[k][j][i].M2+=dtodx3*x3Flux[k][j][i].Mz;
            pGrid->U[k][j][i].M3+=dtodx3*x3Flux[k][j][i].Mx;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E +=dtodx3*x3Flux[k][j][i].E;
#endif /* ISOTHERMAL */
          }

          if (Ineg[k+1][j][i] & correct_hydro_x3) {
            /* Correct using x3 flux through right interface */
            pGrid->U[k][j][i].d -=dtodx3*x3Flux[k+1][j][i].d;
            pGrid->U[k][j][i].M1-=dtodx3*x3Flux[k+1][j][i].My;
            pGrid->U[k][j][i].M2-=dtodx3*x3Flux[k+1][j][i].Mz;
            pGrid->U[k][j][i].M3-=dtodx3*x3Flux[k+1][j][i].Mx;
#ifndef ISOTHERMAL
            pGrid->U[k][j][i].E -=dtodx3*x3Flux[k+1][j][i].E;
#endif /* ISOTHERMAL */
          }
        }
      }
    }

    /* Now check for negative cell-centered densities again */
    negcount = 0;
    for (k=ks; k<=ke; k++) {
      for (j=js; j<=je; j++) {
        for (i=is; i<=ie; i++) {
          if (pGrid->U[k][j][i].d <= 0.0) {
            ath_perr(-1,"13d: pGrid->U[%d][%d][%d].d = %5.4e\n",
                          pGrid->kdisp+k,pGrid->jdisp+j,pGrid->idisp+i,
                          pGrid->U[k][j][i].d);
            negcount++;
          }
        }
      }
    }

    if (negcount > 0) ath_error("Negative densities persist.\n");
  } /* original negcount > 0 */

  return;
}
#endif /* FIRST_ORDER_FLUX_CORRECTION */
