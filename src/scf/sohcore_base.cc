//
// BAGEL - Parallel electron correlation program.
// Filename: sohcore_base.cc
// Copyright (C) 2014 Toru Shiozaki
//
// Author: Hai-Anh Le <anh@u.northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// The BAGEL package is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the BAGEL package; see COPYING.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//


#include <src/scf/sohcore_base.h>
#include <src/integral/os/kineticbatch.h>
#include <src/integral/os/mmbatch.h>
#include <src/integral/rys/naibatch.h>
#include <src/integral/rys/r0batch.h>
#include <src/integral/rys/r1batch.h>
#include <src/integral/rys/r2batch.h>
#include <src/integral/ecp/ecpbatch.h>
#include <src/integral/ecp/soecpbatch.h>

using namespace std;
using namespace bagel;

BOOST_CLASS_EXPORT_IMPLEMENT(SOHcore_base)

SOHcore_base::SOHcore_base(const shared_ptr<const Molecule> mol) : Matrix1e(mol) {

  soiaa_ = make_shared<Matrix>(mol->nbasis(), mol->nbasis());    soiaa_->zero();
  sorab_ = make_shared<Matrix>(mol->nbasis(), mol->nbasis());    sorab_->zero();
  soiab_ = make_shared<Matrix>(mol->nbasis(), mol->nbasis());    soiab_->zero();

  init(mol);
  fill_upper();

  soiaa_->fill_upper_negative();
  sorab_->fill_upper_negative();
  soiab_->fill_upper_negative();

}



void SOHcore_base::computebatch(const array<shared_ptr<const Shell>,2>& input, const int offsetb0, const int offsetb1, std::shared_ptr<const Molecule> mol) {

  // input = [b1, b0]
  assert(input.size() == 2);
  const int dimb1 = input[0]->nbasis();
  const int dimb0 = input[1]->nbasis();

  {
    KineticBatch kinetic(input);
    kinetic.compute();

    copy_block(offsetb1, offsetb0, dimb1, dimb0, kinetic.data());
  }
  {
    NAIBatch nai(input, mol);
    nai.compute();

    add_block(1.0, offsetb1, offsetb0, dimb1, dimb0, nai.data());
  }

  if (mol->atoms(0)->use_ecp_basis()) {
    {
      R0Batch r0(input, mol);
      r0.compute();

      add_block(1.0, offsetb1, offsetb0, dimb1, dimb0, r0.data());
    }
    {
      R1Batch r1(input, mol);
      r1.compute();

      add_block(1.0, offsetb1, offsetb0, dimb1, dimb0, r1.data());
    }
    {
      R2Batch r2(input, mol);
      r2.compute();

      add_block(1.0, offsetb1, offsetb0, dimb1, dimb0, r2.data());
    }
    {
      ECPBatch ecp(input, mol);
      ecp.compute();

      add_block(1.0, offsetb1, offsetb0, dimb1, dimb0, ecp.data());
    }
    {
      if (mol->atoms(0)->so_parameters()) {
        SOECPBatch soecp(input, mol);
        soecp.compute();

        soiaa_->copy_block(offsetb1, offsetb0, dimb1, dimb0, soecp.data());
        sorab_->copy_block(offsetb1, offsetb0, dimb1, dimb0, soecp.data1());
        soiab_->copy_block(offsetb1, offsetb0, dimb1, dimb0, soecp.data2());
      }
    }
  }

  if (mol->external()) {
    DipoleBatch dipole(input, mol);
    dipole.compute();
    const size_t block = dipole.size_block();
    const double* dip = dipole.data();

    int cnt = 0;
    for (int i = offsetb0; i != dimb0 + offsetb0; ++i) {
      for (int j = offsetb1; j != dimb1 + offsetb1; ++j, ++cnt) {
        element(j, i) += dip[cnt        ]*mol->external(0);
        element(j, i) += dip[cnt+block  ]*mol->external(1);
        element(j, i) += dip[cnt+block*2]*mol->external(2);
      }
    }
  }
}


