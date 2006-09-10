#include "copyright.h"
/*==============================================================================
 * FILE: carbuncle.c
 *
 * PURPOSE: Problem generator for carbuncle instability.  Sets up a planar shock
 *   propagating in the x1-direction with arbitrary Mach number input from
 *   problem file.  Adds perturbations to the transverse velocity of arbitrary
 *   amplitude A in a strip of one zone ahead of shock. If perturbation 
 *   amplitude is zero code keeps shock exactly planar.  If A/Cs = 10^{-4},
 *   shock completely disintegrates without H-correction.  H-correction
 *   completely fixes problem.  Runs two problems:
 *    shk_flag = 0 - standing shock in middle of grid (obc_x1=2 in input file) 
 *    shk_flag = 1 - flow at Ux=Mach into wall (obc_x1=1 in input file) 
 *        
 * REFERENCE: R. Sanders, E. Morano, & M.-C. Druguet, "Multidimensional 
 *   dissipation for upwind schemes: stability and applications to gas dynamics"
 *   JCP, 145, 511 (1998)
 *
 * PROBLEM USER FUNCTIONS: Must be included in every problem file, even if they
 *   are NoOPs and never used.  They provide user-defined functionality.
 * problem_write_restart() - writes problem-specific user data to restart files
 * problem_read_restart()  - reads problem-specific user data from restart files
 * get_usr_expr()          - sets pointer to expression for special output data
 * Userwork_in_loop        - problem specific work IN     main loop
 * Userwork_after_loop     - problem specific work AFTER  main loop
 *============================================================================*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "defs.h"
#include "athena.h"
#include "globals.h"
#include "prototypes.h"

/* static globals passed between functions in this file */
static Real Mach,dl,pl,ul,dr,pr,ur;

/*==============================================================================
 * PRIVATE FUNCTION PROTOTYPES:
 * initialize_states() - sets shock jumps given Mach number
 *============================================================================*/

static void initialize_states(void);

/*=========================== PUBLIC FUNCTIONS ===============================*/
/*----------------------------------------------------------------------------*/
/* problem:  */

void problem(Grid *pGrid)
{
  int i,j,k,is,ie,js,je,ks,ke,ishock,shk_flag;
  Real amp;
  div_t index;

  is = pGrid->is;
  ie = pGrid->ie;

  js = pGrid->js;
  je = pGrid->je;

  ks = pGrid->ks;
  ke = pGrid->ke;

/* Read Mach number, perturbation amplitude, problem type from athinput */
  Mach = par_getd("problem","Mach");
  amp  = par_getd("problem","amp");
  shk_flag  = par_getd("problem","shk_flag");

/* "Right" state is pre-shock conditions, hardwired here */
  dr = 1.0;
  pr = 1.0/Gamma;
  ur = Mach*sqrt(Gamma*pr/dr);

/* Initialize shock jumps for standing shock */
  if (shk_flag == 0) {
    initialize_states();
    ishock = is + pGrid->Nx1/2; 
  } else {  /* uniform flow across grid (shock generated by obx_x1=1)  */
    dl = dr;
    pl = pr;
    ul = ur;
    ishock = ie - 10;
  }

/* Initialize the grid.  Shock moves in -x1 direction, located at i=ishock
 */

  for (k=ks; k<=ke; k++) {
  for (j=js-nghost; j<=je+nghost; j++) {
/*  Preshock flow */
    for (i=is-nghost; i<ishock; i++) { 
      pGrid->U[k][j][i].d  = dr;
      pGrid->U[k][j][i].M1 = dr*ur;
      pGrid->U[k][j][i].M2 = 0.0;
      pGrid->U[k][j][i].M3 = 0.0;
      pGrid->U[k][j][i].E = pr/Gamma_1 + 0.5*dr*ur*ur;
    }
/*  Postshock flow */
    for (i=ishock; i<=ie+nghost; i++) {
      pGrid->U[k][j][i].d  = dl;
      pGrid->U[k][j][i].M1 = dl*ul;
      pGrid->U[k][j][i].M2 = 0.0;
      pGrid->U[k][j][i].M3 = 0.0;
      pGrid->U[k][j][i].E = pl/Gamma_1 + 0.5*dl*ul*ul;
    }

/* Add zone-to-zone pertubations upstream of shock.  We only add perturbations
 * to M2, so this means P has perturbations as well
 */

    index = div((j+k),2);
    if (index.rem == 0) {
      pGrid->U[k][j][ishock-10].M2 = amp;
    } else {
      pGrid->U[k][j][ishock-10].M2 = -amp;
    }
  }}

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

void problem_write_restart(Grid *pG, FILE *fp){
  return;
}

void problem_read_restart(Grid *pG, FILE *fp){
  return;
}

Gasfun_t get_usr_expr(const char *expr){
  return NULL;
}

void Userwork_in_loop(Grid *pGrid)
{
}

void Userwork_after_loop(Grid *pGrid)
{
}

/*=========================== PRIVATE FUNCTIONS ==============================*/

/*-----------------------------------------------------------------------------
 * initialize_states: Uses Rankine Hugoniot relations for adiabatic gas to
 *   shock jump conditions
 */

static void initialize_states(void)
{
  Real jump1, jump2, jump3;

  jump1 = (Gamma + 1.0)/(Gamma_1 + 2.0/(Mach*Mach));
  jump2 = (2.0*Gamma*Mach*Mach - Gamma_1)/(Gamma + 1.0);
  jump3 = 2.0*(1.0 - 1.0/(Mach*Mach))/(Gamma + 1.0);

  dl = dr*jump1;
  pl = pr*jump2;
  ul = ur - jump3*Mach*sqrt(Gamma*pr/dr);

/* Make the shock stationary */
  ur = Mach*sqrt(Gamma*pr/dr);
  ul = ur/jump1;

  return;
}
