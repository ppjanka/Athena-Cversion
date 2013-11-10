#ifndef GLOBALS_H
#define GLOBALS_H  
/*============================================================================*/
/*! \file globals.h
 *  \brief Contains global variables.
 *
 * PURPOSE: Contains global variables:
 *   The first occurence in this file is included in main.c and defines the
 *   variables.  The second is included everywhere else.		      */
/*============================================================================*/

#ifdef MAIN_C

Real CourNo;                 /*!< Courant, Friedrichs, & Lewy (CFL) number */
#ifdef ISOTHERMAL
Real Iso_csound;             /*!< isothermal sound speed */
Real Iso_csound2;            /*!< isothermal sound speed squared */
#elif defined ADIABATIC
Real Gamma;                  /*!< adiabatic index (ratio of specific heats) */
Real Gamma_1, Gamma_2;       /*!< (Gamma)-1 and (Gamma)-2 */
#endif
int myID_Comm_world; /*!< Rank (proc ID) in MPI_COMM_WORLD, 0 for single proc */
Real d_MIN = TINY_NUMBER;    /*!< density floor */

GravPotFun_t StaticGravPot = NULL;
CoolingFun_t CoolingFunc = NULL;
#ifdef SELF_GRAVITY
Real four_pi_G, grav_mean_rho;    /*!< 4\pi G and mean density in domain */

#ifdef CONS_GRAVITY
VDFun_t SelfGrav_cons = NULL;
#endif

#endif

#ifdef SHEARING_BOX
GravPotFun_t ShearingBoxPot = NULL;
Real Omega_0, qshear; /*!< orbital freq and shear parameter dln\Omega/dlnr */
enum SS2DCoord ShBoxCoord;
#endif

#ifdef PARTICLES
TSFun_t     get_ts    = NULL;     /*!< get the stopping time */
WeightFun_t getweight = NULL;     /*!< get weight function */
#endif

#ifdef THERMAL_CONDUCTION
Real kappa_iso=0.0, kappa_aniso=0.0;         /*!< coeff of thermal conduction */
#endif
#ifdef RESISTIVITY
Real eta_Ohm=0.0, Q_Hall=0.0, Q_AD=0.0;        /*!< diffusivities */
Real d_ind;                                    /*!< index: n_e ~ d^(d_ind) */
EtaFun_t get_myeta = NULL;       /*!< function to calculate the diffusivities */
#endif
#ifdef VISCOSITY
Real nu_iso=0.0, nu_aniso=0.0;               /*!< coeff of viscosity */
#endif
#ifdef STS
int N_STS;			/*!< number of super timesteps */
Real nu_STS;			/*!< parameter controlling the substeps  */
Real STS_dt;			/*!< STS time step */
#endif

#ifdef CYLINDRICAL
StaticGravAcc_t x1GravAcc = NULL;
Real *r=NULL, *ri=NULL;
#ifdef FARGO
OrbitalFun_t OrbitalProfile = NULL;
ShearFun_t ShearProfile = NULL;
#endif
#endif


#if defined (RADIATION_HYDRO) || defined (RADIATION_MHD) || defined(FULL_RADIATION_TRANSFER)
Real Prat;		/* Pratio=a_rT^4/rho a^2; The ratio between the pressure unit for radiation and gas */ 
Real Crat;		/* Cratio=c/a; The ratio between speed of light and velocity unit */
Real Taufactor = 20.0;
#endif

#if defined (RADIATION_HYDRO) || defined (RADIATION_MHD)
OpacityFun_t Opacity = NULL;
int Ncycle = 5;	/* Numbers of relaxation cycles to do in multigrid-matrix solver */
Real TOL = 1.e-8;  /* relative residual level used in matrix convergent criterian */
Real Eratio = 0.00;
int Erflag = 1; /* Flag to do backward Euler first or later. If 1, do it later. conservative way */
Real T0 = 1.e7; /* the temperature unit used in this simulation */
#endif

#if defined (RADIATION_HYDRO) || defined (RADIATION_MHD) || defined (RADIATION_TRANSFER) || defined (FULL_RADIATION_TRANSFER)
Real R_ideal;		/* Value of ideal gas constant under current unit. Used to calculate the temperature */
#endif

#ifdef RADIATION_TRANSFER
RadInitFun_t get_thermal_source = NULL;
RadInitFun_t get_thermal_fraction = NULL;
RadInitFun_t get_total_opacity = NULL;
int lte;
int radt_mode;
Real CPrat;  /* equiv to Prat * Crat in radhydro module */
#ifdef RAY_TRACING
RadInitFun_t get_raytrace_thermal_fraction = NULL;
RadInitFun_t get_raytrace_opacity = NULL;
RRGIIFun_t raytrace_to_radtrans = NULL;
#endif
#endif

#ifdef FULL_RADIATION_TRANSFER
FullRadOpacity_t get_full_opacity = NULL;
#endif

/*----------------------------------------------------------------------------*/
/* definitions included everywhere except main.c  */

#else /* MAIN_C */

extern Real CourNo;
#ifdef ISOTHERMAL
extern Real Iso_csound, Iso_csound2;
#elif defined ADIABATIC
extern Real Gamma, Gamma_1, Gamma_2;
#endif
extern int myID_Comm_world;
extern Real d_MIN;

extern GravPotFun_t StaticGravPot;
extern CoolingFun_t CoolingFunc;
#ifdef SELF_GRAVITY
extern Real four_pi_G, grav_mean_rho;

#ifdef CONS_GRAVITY
extern VDFun_t SelfGrav_cons;
#endif

#endif

#ifdef SHEARING_BOX
extern GravPotFun_t ShearingBoxPot;
extern Real Omega_0, qshear;
extern enum SS2DCoord ShBoxCoord;
#endif

#ifdef PARTICLES
extern Real alamcoeff, *grrhoa;
extern TSFun_t     get_ts; 
extern WeightFun_t getweight; 
#endif

#ifdef THERMAL_CONDUCTION
extern Real kappa_iso, kappa_aniso;
#endif
#ifdef RESISTIVITY
extern Real eta_Ohm, Q_Hall, Q_AD;
extern Real d_ind;
extern EtaFun_t get_myeta;
#endif
#ifdef VISCOSITY
extern Real nu_iso, nu_aniso;
#endif
#ifdef STS
extern int N_STS;
extern Real nu_STS, STS_dt; 
#endif

#ifdef CYLINDRICAL
extern StaticGravAcc_t x1GravAcc;
extern Real *r, *ri;
#ifdef FARGO
extern OrbitalFun_t OrbitalProfile;
extern ShearFun_t ShearProfile;
#endif
#endif


#if defined (RADIATION_HYDRO) || defined (RADIATION_MHD) || defined(FULL_RADIATION_TRANSFER) 
extern Real Prat;
extern Real Crat;
extern Real Taufactor;
#endif

#if defined (RADIATION_HYDRO) || defined (RADIATION_MHD) 
extern OpacityFun_t Opacity;
extern int Ncycle;	/* Numbers of relaxation cycles to do in multigrid-matrix solver */
extern Real TOL;  /* relative residual level used in matrix convergent criterian */
extern Real Eratio;
extern int Erflag;
extern Real T0;
#endif

#if defined (RADIATION_HYDRO) || defined (RADIATION_MHD) || defined(RADIATION_TRANSFER) || defined(RADIATION_TRANSFER) || defined (FULL_RADIATION_TRANSFER)
extern Real R_ideal;		 	
#endif

#ifdef RADIATION_TRANSFER
extern RadInitFun_t get_thermal_source;
extern RadInitFun_t get_thermal_fraction;
extern RadInitFun_t get_total_opacity;
extern int lte;
extern int radt_mode;
extern Real CPrat; 
#ifdef RAY_TRACING
extern RadInitFun_t get_raytrace_thermal_fraction;
extern RadInitFun_t get_raytrace_opacity;
extern RRGIIFun_t raytrace_to_radtrans;
#endif
#endif


#ifdef FULL_RADIATION_TRANSFER
extern FullRadOpacity_t get_full_opacity;
#endif


#endif /* MAIN_C */
#endif /* GLOBALS_H */
