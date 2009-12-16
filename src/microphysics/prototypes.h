#ifndef MICROPHYS_PROTOTYPES_H
#define MICROPHYS_PROTOTYPES_H 
#include "../copyright.h"
/*==============================================================================
 * FILE: prototypes.h
 *
 * PURPOSE: Prototypes for all public functions from the following files:
 *   anisotropic_conduction.c
 *   braginskii.c
 *   cool.c
 *   integrate_diffusion.c
 *   isotropic_conduction.c
 *   resistivity.c
 *   viscosity.c
 *============================================================================*/

#include <stdio.h>
#include <stdarg.h>
#include "../athena.h"
#include "../defs.h"

#include "../config.h"

/* anisotropic_conduction.c */
#ifdef ANISOTROPIC_CONDUCTION
void anisoconduct_2d(Grid *pG, Domain *pD);
void anisoconduct_3d(Grid *pG, Domain *pD);
void anisoconduct_init(int nx1, int nx2, int nx3);
void anisoconduct_destruct(void);
#endif

/* braginskii.c */
#ifdef BRAGINSKII
void brag_viscosity_2d(Grid *pG, Domain *pD);
void brag_viscosity_3d(Grid *pG, Domain *pD);
void brag_viscosity_init(int nx1, int nx2, int nx3);
void brag_viscosity_destruct(void);
#endif

/* cool.c */
Real KoyInut(const Real dens, const Real Press, const Real dt);

/* hall.c */
#ifdef HALL_MHD
void hall_resistivity_1d(Grid *pG, Domain *pD);
void hall_resistivity_2d(Grid *pG, Domain *pD);
void hall_resistivity_3d(Grid *pG, Domain *pD);
void hall_resistivity_init(int nx1, int nx2, int nx3);
void hall_resistivity_destruct(void);
#endif

/* integrate_diffusion.c */
#ifdef EXPLICIT_DIFFUSION
void integrate_explicit_diff(MeshS *pM);
void integrate_explicit_diff_init(MeshS *pM);
void integrate_explicit_diff_destruct(void);
#endif

/* isotropic_conduction.c */
#ifdef ISOTROPIC_CONDUCTION
void isoconduct(Grid *pG, Domain *pD);
void isoconduct_init(int nx1, int nx2, int nx3);
void isoconduct_destruct(void);
#endif

/* resistivity.c */
#ifdef OHMIC
void ohmic_resistivity_1d(Grid *pG, Domain *pD);
void ohmic_resistivity_2d(Grid *pG, Domain *pD);
void ohmic_resistivity_3d(Grid *pG, Domain *pD);
void ohmic_resistivity_init(int nx1, int nx2, int nx3);
void ohmic_resistivity_destruct(void);
#endif

/* viscosity.c */
#ifdef NAVIER_STOKES
void ns_viscosity_1d(Grid *pG, Domain *pD);
void ns_viscosity_2d(Grid *pG, Domain *pD);
void ns_viscosity_3d(Grid *pG, Domain *pD);
void ns_viscosity_init(int nx1, int nx2, int nx3);
void ns_viscosity_destruct(void);
#endif

#endif /* MICROPHYS_PROTOTYPES_H */
