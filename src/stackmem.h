//
// Newint - Parallel electron correlation program.
// Filename: stackmem.h
// Copyright (C) 2011 Toru Shiozaki
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


#ifndef __STACKMEM_H
#define __STACKMEM_H

// CAUTION last-in-first-out stack to avoid the overhead of new'ing every time

#include <cassert>
#include <memory>

class StackMem {
  protected:
    std::unique_ptr<double[]> stack_area_;
    size_t pointer_;
    const size_t total_;

  public:
    StackMem() : pointer_(0LU), total_(10000000LU) { // 80MByte
      stack_area_ = std::unique_ptr<double[]>(new double[total_]);
    };

    double* get(const size_t size) {
      assert(pointer_+size < total_);
      double* out = stack_area_.get() + pointer_;
      pointer_ += size;
      return out;
    };

    void release(const size_t size, double* p) {
      pointer_ -= size; 
      assert(p == stack_area_.get()+pointer_);
    };
    
};


#endif
