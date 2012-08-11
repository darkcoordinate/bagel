//
// Newint - Parallel electron correlation program.
// Filename: geometry.h
// Copyright (C) 2009 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the Newint package (to be renamed).
//
// The Newint package is free software; you can redistribute it and\/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// The Newint package is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the Newint package; see COPYING.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//


#ifndef __src_scf_geometry_h
#define __src_scf_geometry_h

#include <string>
#include <vector>
#include <src/scf/atom.h>
#include <src/scf/petite.h>
#include <memory>
#include <src/df/fit.h>
#include <src/util/input.h>

class Geometry {
  protected:
    // Spherical or Cartesian basis set.
    bool spherical_;

    // Input file name.
    std::string input_;

    // Atoms, which contains basis-set info also.
    std::vector<std::shared_ptr<Atom> > atoms_;
    std::vector<std::shared_ptr<Atom> > aux_atoms_;
    bool aux_merged_;

    // Nuclear repulsion energy.
    double nuclear_repulsion_;
    // Computes the nuclear repulsion energy.
    double compute_nuclear_repulsion();

    // Some shared info for basis sets.
    int nbasis_;
    int nele_;
    int nfrc_;
    int naux_;
    int lmax_;
    int aux_lmax_;
    std::vector<std::vector<int> > offsets_;
    std::vector<std::vector<int> > aux_offsets_;

    int level_;
    std::string basisfile_;
    std::string auxfile_;

    // Symmetry can be used for molecular calculation.
    std::string symmetry_;
    std::shared_ptr<Petite> plist_;
    int nirrep_;

    // integral screening
    double schwarz_thresh_;
    double overlap_thresh_;

    // for DF calculations
    std::shared_ptr<DensityFit> df_;

    // external field
    std::vector<double> external_;

    // for R12 calculations
    double gamma_;

    // Constructor helpers
    void construct_from_atoms(const std::vector<std::shared_ptr<Atom> > atoms, const std::multimap<std::string,std::string> o);
    void common_init1();
    void common_init2(const bool print, const double thresh);

  public:
    Geometry(const std::string, const int level);
    Geometry(const std::multimap<std::string, std::string>);
    Geometry(const std::vector<std::shared_ptr<Atom> > atoms, const std::multimap<std::string, std::string> o);
    Geometry(const Geometry& o, const std::vector<double> disp, const std::multimap<std::string, std::string> geominfo);
    Geometry(const Geometry& o, const std::array<double,3> disp);
    Geometry(std::vector<std::shared_ptr<const Geometry> >);
    ~Geometry();

    // Returns shared pointers of Atom objects, which contains basis-set info.
    std::vector<std::shared_ptr<Atom> > atoms() const { return atoms_; };
    std::vector<std::shared_ptr<Atom> > aux_atoms() const { return aux_atoms_; };
    std::shared_ptr<Atom> atoms(const unsigned int i) const { return atoms_[i]; };

    // Returns a constant
    int natom() const { return atoms_.size(); };
    size_t nbasis() const { return nbasis_; };
    size_t nele() const { return nele_; };
    size_t nfrc() const { return nfrc_; };
    size_t naux() const { return naux_; };
    int lmax() const { return lmax_; };
    int aux_lmax() const { return aux_lmax_; };
    bool spherical() const { return spherical_; };
    int nirrep() const { return nirrep_; };
    double gamma() const {return gamma_; };
    const std::string symmetry() const { return symmetry_; };
    virtual double nuclear_repulsion() const { return nuclear_repulsion_; };
    const std::vector<double> compute_grad_vnuc() const;
    int level() const { return level_; };
    const std::string basisfile() const { return basisfile_; };
    const std::string auxfile() const { return auxfile_; };
    double schwarz_thresh() const { return schwarz_thresh_; };
    double overlap_thresh() const { return overlap_thresh_; };

    // TODO for some reasons needed now in CASSCF
    void set_nele(const int i) { nele_ = i; };
    void set_basis(const int i) { nbasis_ = i; };
    void set_ncore(const int i) { nfrc_ = i; };
    int num_count_ncore(); // also set nfrc_
    int num_count_ncore_only() const; // also set nfrc_
    int num_count_full_valence_nocc() const;

    // The position of the specific funciton in the basis set.
    const std::vector<std::vector<int> > offsets() const { return offsets_; };
    const std::vector<std::vector<int> > aux_offsets() const { return aux_offsets_; };
    const std::vector<int> offset(const unsigned int i) const { return offsets_.at(i); };
    const std::vector<int> aux_offset(const unsigned int i) const { return aux_offsets_.at(i); };

    // returns schwarz screening TODO not working for DF yet
    std::vector<double> schwarz() const;

    // Printing out some info
    void print_atoms() const;

    // Returns the Petite list.
    std::shared_ptr<Petite> plist() const { return plist_; }; 

    // Rerurns DF data
    const std::shared_ptr<const DensityFit> df() const { return df_; };

    // In R12 methods, we need to construct a union of OBS and CABS.
    // Currently, this is done by creating another object and merge OBS and CABS into atoms_.
    // After this, compute_nuclear_repulsion() should not be called.
    // Not undo-able.
    void merge_obs_aux();

    // type T should be a derived class of DensityFit
    template<typename T>
    std::shared_ptr<T> form_fit(const double thr, const bool inverse, const double gam = 0.0) const {
      return std::shared_ptr<T>(new T(nbasis(), naux(), atoms(), offsets(), aux_atoms(), aux_offsets(), thr, inverse, gam));
    };

    std::vector<double> xyz() const;

    std::vector<double> charge_center() const;

    // external field
    bool external() const { return external(0) != 0.0 || external(1) != 0.0 || external(2) != 0.0; };
    double external(const int i) const { return external_[i]; };
};

#endif

