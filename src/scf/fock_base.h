//
// BAGEL - Parallel electron correlation program.
// Filename: fock_base.h
// Copyright (C) 2009 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
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


#ifndef __src_scf_fock_base_h
#define __src_scf_fock_base_h

#include <src/wfn/geometry.h>
#include <src/molecule/matrix1e.h>

namespace bagel {

class Fock_base : public Matrix1e {
  protected:
    std::shared_ptr<const Geometry> geom_;
    std::shared_ptr<const Matrix> previous_;
    std::shared_ptr<const Matrix> density_;
    void computebatch(const std::array<std::shared_ptr<const Shell>,2>&, const int, const int, std::shared_ptr<const Molecule>) override;

    // virtual function that is to be defined in the derived class
    virtual void fock_two_electron_part(std::shared_ptr<const Matrix>) = 0;
    void fock_one_electron_part();

    // for non-DF Fock builds
    std::vector<double> schwarz_;
    double schwarz_thresh_;

  private:
    // serialization
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive& ar, const unsigned int) {
      ar & boost::serialization::base_object<Matrix1e>(*this) & geom_ & previous_ & density_ & schwarz_ & schwarz_thresh_;
    }

  public:
    Fock_base() { }
    Fock_base(const std::shared_ptr<const Geometry>, const std::shared_ptr<const Matrix>, const std::shared_ptr<const Matrix>,
              const std::vector<double>& = std::vector<double>());
    virtual ~Fock_base() { }
};

}

#include <src/util/archive.h>
BOOST_CLASS_EXPORT_KEY(bagel::Fock_base)

#endif
