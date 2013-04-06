//
// BAGEL - Parallel electron correlation program.
// Filename: rdm.cc
// Copyright (C) 2012 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
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


#include <src/wfn/rdm.h>

using namespace bagel;
using namespace std;

RDM_base::RDM_base(const int n, const int rank) : norb_(n), rank_(rank) {
  assert(rank > 0);
  dim_ = 1lu;
  for (int i = 0; i != rank; ++i) dim_ *= n;
  data_ = unique_ptr<double[]>(new double[dim_*dim_]);
}


RDM_base::RDM_base(const RDM_base& o) : norb_(o.norb_), dim_(o.dim_), rank_(o.rank_) {
  data_ = unique_ptr<double[]>(new double[dim_*dim_]);
  copy_n(o.data(), dim_*dim_, data());
}


template<>
bool RDM<1>::natural_orbitals() const {
  const double a = ddot_(norb_*norb_, data_, 1, data_, 1);
  const double b = ddot_(norb_, data_, norb_+1, data_, norb_+1);
  return fabs(a-b) < 1.0e-12;
}


template<>
pair<shared_ptr<Matrix>, vector<double>> RDM<1>::generate_natural_orbitals() const {
  shared_ptr<Matrix> buf(new Matrix(dim_,dim_,true));
  buf->add_diag(2.0);
  daxpy_(dim_*dim_, -1.0, data(), 1, buf->data(), 1);

  vector<double> vec(dim_);
  buf->diagonalize(&vec[0]);

  for (auto& i : vec) i = 2.0-i;
  return make_pair(buf, vec);
}


template<>
void RDM<1>::transform(const shared_ptr<Matrix>& coeff) {
  const double* start = coeff->data();
  unique_ptr<double[]> buf(new double[dim_*dim_]);
  dgemm_("N", "N", dim_, dim_, dim_, 1.0, data(), dim_, start, dim_, 0.0, buf.get(), dim_);
  dgemm_("T", "N", dim_, dim_, dim_, 1.0, start, dim_, buf.get(), dim_, 0.0, data(), dim_);
}


template<>
void RDM<2>::transform(const shared_ptr<Matrix>& coeff) {
  const double* start = coeff->data();
  unique_ptr<double[]> buf(new double[dim_*dim_]);
  // first half transformation
  dgemm_("N", "N", dim_*norb_, norb_, norb_, 1.0, data(), dim_*norb_, start, norb_, 0.0, buf.get(), dim_*norb_);
  for (int i = 0; i != norb_; ++i)
    dgemm_("N", "N", dim_, norb_, norb_, 1.0, buf.get()+i*dim_*norb_, dim_, start, norb_, 0.0, data()+i*dim_*norb_, dim_);
  // then tranpose
  mytranspose_(data(), dim_, dim_, buf.get());
  // and do it again
  dgemm_("N", "N", dim_*norb_, norb_, norb_, 1.0, buf.get(), dim_*norb_, start, norb_, 0.0, data(), dim_*norb_);
  for (int i = 0; i != norb_; ++i)
    dgemm_("N", "N", dim_, norb_, norb_, 1.0, data()+i*dim_*norb_, dim_, start, norb_, 0.0, buf.get()+i*dim_*norb_, dim_);
  // to make sure for non-symmetric density matrices (and anyway this should be cheap).
  mytranspose_(buf.get(), dim_, dim_, data());
}


template<>
shared_ptr<Matrix> RDM<1>::rdm1_mat(shared_ptr<const Geometry> g, const int nclosed, const bool all) const {
  shared_ptr<Matrix> out(new Matrix(nclosed+norb_, nclosed+norb_));
  if (all)
    for (int i = 0; i != nclosed; ++i) out->element(i,i) = 2.0;
  for (int i = 0; i != norb_; ++i)
    for (int j = 0; j != norb_; ++j)
      out->element(j+nclosed, i+nclosed) = element(j,i);
  return out;
}


template<>
void RDM<1>::print(const double thresh) const {
  const double* ptr = data_.get();
  for (int i = 0; i != norb_; ++i)
    for (int j = 0; j != norb_; ++j)
      cout << setw(12) << setprecision(7) << *ptr++ << endl;
}


template<>
void RDM<2>::print(const double thresh) const {
  const double* ptr = data_.get();
  for (int i = 0; i != norb_; ++i)
    for (int j = 0; j != norb_; ++j)
      for (int k = 0; k != norb_; ++k)
        for (int l = 0; l != norb_; ++l, ++ptr)
          if (fabs(*ptr) > thresh)
            cout << setw(3) << l << setw(3)
                      << k << setw(3) << j << setw(3) << i
                      << setw(12) << setprecision(7) << *ptr << endl;
}


template<>
void RDM<3>::print(const double thresh) const {
  const double* ptr = data_.get();
  for (int i = 0; i != norb_; ++i)
    for (int j = 0; j != norb_; ++j)
      for (int k = 0; k != norb_; ++k)
        for (int l = 0; l != norb_; ++l)
        for (int m = 0; m != norb_; ++m)
        for (int n = 0; n != norb_; ++n, ++ptr)
          if (abs(*ptr) > thresh)
            cout << setw(3) << n << setw(3) << m << setw(3) << l << setw(3)
                 << k << setw(3) << j << setw(3) << i
                 << setw(12) << setprecision(7) << *ptr << endl;
}
