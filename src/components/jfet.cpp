/*
 * jfet.cpp - jfet class implementation
 *
 * Copyright (C) 2004 Stefan Jahn <stefan@lkcc.org>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 *
 * $Id: jfet.cpp,v 1.3 2004/06/09 23:55:41 ela Exp $
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "complex.h"
#include "matrix.h"
#include "object.h"
#include "node.h"
#include "circuit.h"
#include "net.h"
#include "analysis.h"
#include "dcsolver.h"
#include "component_id.h"
#include "constants.h"
#include "diode.h"
#include "jfet.h"

jfet::jfet () : circuit (3) {
  rs = rd = NULL;
  type = CIR_JFET;
}

void jfet::calcSP (nr_double_t frequency) {
  nr_double_t Cgd = getOperatingPoint ("Cgd");
  nr_double_t Cgs = getOperatingPoint ("Cgs");
  nr_double_t ggs = getOperatingPoint ("ggs");
  nr_double_t ggd = getOperatingPoint ("ggd");
  nr_double_t gds = getOperatingPoint ("gds");
  nr_double_t gm  = getOperatingPoint ("gm");

  complex Ygd = rect (ggd, 2.0 * M_PI * frequency * Cgd);
  complex Ygs = rect (ggs, 2.0 * M_PI * frequency * Cgs);
  complex Yds = gds;

  matrix y = matrix (3); 
  y.set (1, 1, Ygd + Ygs);
  y.set (1, 2, -Ygd);
  y.set (1, 3, -Ygs);
  y.set (2, 1, gm - Ygd);
  y.set (2, 2, Ygd + Yds);
  y.set (2, 3, -Yds - gm);
  y.set (3, 1, -Ygs - gm);
  y.set (3, 2, -Yds);
  y.set (3, 3, Ygs + Yds + gm);
  matrix s = ytos (y);

  setS (1, 1, s.get (1, 1));
  setS (1, 2, s.get (1, 2));
  setS (1, 3, s.get (1, 3));
  setS (2, 1, s.get (2, 1));
  setS (2, 2, s.get (2, 2));
  setS (2, 3, s.get (2, 3));
  setS (3, 1, s.get (3, 1));
  setS (3, 2, s.get (3, 2));
  setS (3, 3, s.get (3, 3));
}

void jfet::initDC (dcsolver * solver) {

  // initialize starting values
  setV (1, 0.8);
  setV (2, 0.0);
  setV (3, 0.0);
  UgdPrev = real (getV (1) - getV (2));
  UgsPrev = real (getV (1) - getV (3));

  // possibly insert series resistance at source
  nr_double_t Rs = getPropertyDouble ("Rs");
  if (Rs != 0) {
    // create additional circuit if necessary and reassign nodes
    rs = diode::splitResistance (this, rs, solver->getNet (),
				 "Rs", "source", 3);
    diode::applyResistance (rs, Rs);
  }
  // no series resistance
  else {
    diode::disableResistance (this, rs, solver->getNet (), 2);
  }

  // possibly insert series resistance at drain
  nr_double_t Rd = getPropertyDouble ("Rd");
  if (Rd != 0) {
    // create additional circuit if necessary and reassign nodes
    rd = diode::splitResistance (this, rd, solver->getNet (),
				 "Rd", "drain", 2);
    diode::applyResistance (rd, Rd);
  }
  // no series resistance
  else {
    diode::disableResistance (this, rd, solver->getNet (), 2);
  }
}

void jfet::calcDC (void) {
  nr_double_t Is   = getPropertyDouble ("Is");
  nr_double_t n    = getPropertyDouble ("N");
  nr_double_t Vt0  = getPropertyDouble ("Vt0");
  nr_double_t l    = getPropertyDouble ("Lambda");
  nr_double_t beta = getPropertyDouble ("Beta");

  nr_double_t Ugs, Ugd, Ut, T, gm, IeqG, IeqD, IeqS, UgsCrit, UgdCrit;
  nr_double_t Uds, ggs, Igs, ggd, Igd, Ids, gds, gtiny;

  T = -K + 26.5;
  Ut = T * kB / Q;
  Ugd = real (getV (1) - getV (2));
  Ugs = real (getV (1) - getV (3));

  // critical voltage necessary for bad start values
  UgsCrit = n * Ut * log (n * Ut / M_SQRT2 / Is);
  UgdCrit = n * Ut * log (n * Ut / M_SQRT2 / Is);
  UgsPrev = Ugs = diode::pnVoltage (Ugs, UgsPrev, Ut * n, UgsCrit);
  UgdPrev = Ugd = diode::pnVoltage (Ugd, UgdPrev, Ut * n, UgdCrit);

  Uds = Ugs - Ugd;

  // gate source diode
  gtiny = Ugs < - 10 * Ut * n ? Is : 0;
  ggs = Is / Ut / n * exp (Ugs / Ut / n) + gtiny;
  Igs = Is * (exp (Ugs / Ut / n) - 1) + gtiny * Ugs;

  // gate drain diode
  gtiny = Ugd < - 10 * Ut * n ? Is : 0;
  ggd = Is / Ut / n * exp (Ugd / Ut / n) + gtiny;
  Igd = Is * (exp (Ugd / Ut / n) - 1) + gtiny * Ugd;

  if (Uds >= 0) {
    nr_double_t Ugst = Ugs - Vt0;
    // normal mode, cutoff region
    if (Ugst < 0) {
      Ids = 0;
      gm = 0;
      gds = 0;
    }
    else {
      nr_double_t b = beta * (1 + l * Uds);
      // normal mode, saturation region
      if (Ugst <= Uds) {
	Ids = b * Ugst * Ugst;
	gm  = b * 2 * Ugst;
	gds = l * b * Ugst * Ugst;
      }
      // normal mode, linear region
      else {
	Ids = b * Uds * (2 * Ugst - Uds);
	gm  = b * 2 * Uds;
	gds = b * 2 * (Ugst - Uds) + l * beta * Uds * (2 * Ugst - Uds);
      }
    }
  }
  else {
    nr_double_t Ugdt = Ugd - Vt0;
    // inverse mode, cutoff region
    if (Ugdt < 0) {
      Ids = 0;
      gm  = 0;
      gds = 0;
    }
    else {
      nr_double_t b = beta * (1 - l * Uds);
      // inverse mode, saturation region
      if (Ugdt <= -Uds) {
	Ids = - b * Ugdt * Ugdt;
	gm  = - b * 2 * Ugdt;
	gds = beta * l * Ugdt * Ugdt + b * 2 * Ugdt;
      }
      // inverse mode, linear region
      else {
	Ids = b * Uds * (2 * Ugdt + Uds);
	gm  = b * 2 * Uds;
	gds = 2 * b * Ugdt - beta * l * Uds * (2 * Ugdt + Uds);
      }
    }
  }

  setOperatingPoint ("gm", gm);
  setOperatingPoint ("gds", gds);
  setOperatingPoint ("Id", Ids);

  IeqG = Igs - ggs * Ugs;
  IeqD = Igd - ggd * Ugd;
  IeqS = Ids - gm * Ugs - gds * Uds;

  setI (1, -IeqG - IeqD);
  setI (2, +IeqD - IeqS);
  setI (3, +IeqG + IeqS);

  setY (1, 1, ggs + ggd); setY (1, 2, -ggd); setY (1, 3, -ggs);
  setY (2, 1, -ggd + gm); setY (2, 2, gds + ggd); setY (2, 3, -gm - gds);
  setY (3, 1, -ggs - gm); setY (3, 2, -gds); setY (3, 3, ggs + gds + gm);
}

void jfet::calcOperatingPoints (void) {
  nr_double_t Is   = getPropertyDouble ("Is");
  nr_double_t n    = getPropertyDouble ("N");
  nr_double_t z    = getPropertyDouble ("M");
  nr_double_t Cgd0 = getPropertyDouble ("Cgd");
  nr_double_t Cgs0 = getPropertyDouble ("Cgs");
  nr_double_t Pb   = getPropertyDouble ("Pb");
  
  nr_double_t Ugs, Ugd, Ut, T, ggd, ggs, Cgs, Cgd, Igs, Igd;

  T = -K + 26.5;
  Ut = kB * T / Q;
  Ugd = real (getV (1) - getV (2));
  Ugs = real (getV (1) - getV (3));

  ggs = Is / Ut / n * exp (Ugs / Ut / n);
  Igs = Is * (exp (Ugs / Ut / n) - 1);
  ggd = Is / Ut / n * exp (Ugd / Ut / n);
  Igd = Is * (exp (Ugd / Ut / n) - 1);

  if (Ugd < 0)
    Cgd = Cgd0 * pow (1 - Ugd / Pb, -z);
  else
    Cgd = Cgd0 * (1 + z * Ugd / Pb);

  if (Ugs < 0)
    Cgs = Cgs0 * pow (1 - Ugs / Pb, -z);
  else
    Cgs = Cgs0 * (1 + z * Ugs / Pb);

  setOperatingPoint ("ggs", ggs);
  setOperatingPoint ("ggd", ggd);
  setOperatingPoint ("Ugs", Ugs);
  setOperatingPoint ("Ugd", Ugd);
  setOperatingPoint ("Cgd", Cgd);
  setOperatingPoint ("Cgs", Cgs);
}