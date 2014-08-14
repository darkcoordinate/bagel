//
// BAGEL - Parallel electron correlation program.
// Filename: reloverlap_london.h
// Copyright (C) 2014 Toru Shiozaki
//
// Author: Ryan D. Reynolds <RyanDReynolds@u.northwestern.edu>
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


#ifndef __SRC_LONDON_RELOVERLAP_LONDON_H
#define __SRC_LONDON_RELOVERLAP_LONDON_H

#include <src/molecule/zoverlap.h>
#include <src/molecule/zkinetic.h>
#include <src/math/zmatrix.h>

namespace bagel {

class RelOverlap_London : public ZMatrix {
  protected:
    const std::shared_ptr<const Molecule> mol_;
    const std::shared_ptr<const ZMatrix> kinetic_;
    const std::shared_ptr<const ZOverlap> overlap_;

    void compute_();

  public:
    RelOverlap_London(const std::shared_ptr<const Molecule> mol) : ZMatrix(mol->nbasis()*4, mol->nbasis()*4),
                      mol_(mol), kinetic_(std::make_shared<ZKinetic>(mol)), overlap_(std::make_shared<ZOverlap>(mol)) {
      compute_();
    }

    std::shared_ptr<ZMatrix> tildex(const double thresh) const;
    std::shared_ptr<ZMatrix> inverse() const;
};

}

#endif
