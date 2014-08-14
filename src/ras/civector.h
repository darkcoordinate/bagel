//
// BAGEL - Parallel electron correlation program.
// Filename: ras/civector.h
// Copyright (C) 2013 Toru Shiozaki
//
// Author: Shane Parker <shane.parker@u.northwestern.edu>
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


#ifndef __BAGEL_RAS_RASCIVECTOR_H
#define __BAGEL_RAS_RASCIVECTOR_H

#include <list>
#include <src/ras/civector_base.h>
#include <src/ras/apply_block.h>
#include <src/parallel/recvrequest.h>
#include <src/ras/dvector_base.h>

namespace bagel {


template <typename DataType> class RASCivector;
template <typename DataType> using DistCIBlock = DistCIBlock_alloc<DataType, RASString>;

template <typename DataType>
class DistRASCivector : public RASCivector_base<DistCIBlock<DataType>> {
  public: using DetType = RASDeterminants;
  public: using RBlock = DistCIBlock<DataType>;
  public: using LocalizedType = std::false_type;

  protected:
    using RASCivector_base<DistCIBlock<DataType>>::blocks_;
    using RASCivector_base<DistCIBlock<DataType>>::det_;

    mutable std::shared_ptr<RecvRequest> recv_;
    mutable std::shared_ptr<BufferPutRequest> put_;

    const size_t global_size_;

    // for transpose, buffer can be appended
    mutable std::shared_ptr<DistRASCivector<DataType>> buf_;
    mutable std::vector<int> transp_;

    mutable std::mutex mutex_;

  public:
    DistRASCivector(std::shared_ptr<const RASDeterminants> det) : RASCivector_base<DistCIBlock<DataType>>(det), global_size_(det->size()) {
      size_t block_offset = 0;
      for (auto& ipair : det->blockinfo()) {
        if (!ipair->empty())
          blocks_.push_back(std::make_shared<RBlock>(ipair->stringsa(), ipair->stringsb(), block_offset));
        else
          blocks_.push_back(nullptr);
        ++block_offset;
      }
    }

    DistRASCivector(const DistRASCivector<DataType>& o) : DistRASCivector(o.det_) {
      auto j = o.blocks_.begin();
      for (auto i = blocks_.begin(); i != blocks_.end(); ++i, ++j) {
        if (*i) std::copy_n((*j)->local(), (*i)->size(), (*i)->local());
      }
    }
    DistRASCivector(std::shared_ptr<const DistRASCivector<DataType>> o) : DistRASCivector(*o) {}

    DistRASCivector(const RASCivector<DataType>& o) : DistRASCivector(o.det()) {
      for (auto& block : o.blocks()) {
        if (block) {
          std::shared_ptr<RBlock> distblock = this->block(block->stringsb(), block->stringsa());
          std::copy_n(block->data() + distblock->astart()*distblock->lenb(), distblock->size(), distblock->local());
        }
      }
    }

    DistRASCivector(std::shared_ptr<const RASCivector<DataType>> o) : DistRASCivector(*o) {}

    DistRASCivector(DistRASCivector<DataType>&& o) : RASCivector_base<DistCIBlock<DataType>>(o.det_), global_size_(det_->size()) {
      for (auto& iblock : o.blocks()) {
        blocks_.push_back(iblock);
      }
    }

    // Copy assignment
    DistRASCivector<DataType>& operator=(const DistRASCivector<DataType>& o) {
      assert(*det_ == *o.det_);
      for (auto i = blocks_.begin(), j = o.blocks_.begin(); i != blocks_.end(); ++i)
        if (*i) std::copy_n((*j)->local(), (*i)->size(), (*i)->local());
      return *this;
    }

    // Move assignment
    DistRASCivector<DataType>& operator=(DistRASCivector<DataType>&& o) {
      assert(*det_ == *o.det_);
      for (auto i = blocks_.begin(), j = o.blocks_.begin(); i != blocks_.end(); ++i) { *i = *j; }
      return *this;
    }

    using RASCivector_base<DistCIBlock<DataType>>::block;

    // MPI routines
    // Never call concurrently
    void init_mpi_recv() const {
      std::lock_guard<std::mutex> lock(mutex_);
      put_ = std::make_shared<BufferPutRequest>();
      recv_ = std::make_shared<RecvRequest>();
    }

    // Never call concurrently
    void terminate_mpi_recv() const {
      std::lock_guard<std::mutex> lock(mutex_);
      assert( put_ && recv_);
      bool done;
      do {
        done = recv_->test();
#ifndef USE_SERVER_THREAD
        // in case no thread is running behind, we need to cycle this to flush
        size_t d = done ? 0 : 1;
        mpi__->soft_allreduce(&d, 1);
        done = d == 0;
#endif
        if (!done) this->flush();
        if (!done) std::this_thread::sleep_for(sleeptime__);
      } while (!done);
      // cancel all MPI calls
      recv_.reset();
      put_.reset();
    }

    void flush() const {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto i : put_->get_calls()) {
        // off is interpreted as lexical number of the alpha string
        const size_t tag = i[1];
        const size_t dest = i[2];
        const size_t astring = i[3];
        std::unique_ptr<double[]> buf(new double[det_->lenb()]);
        std::fill_n(buf.get(), det_->lenb(), 0.0);
        // locate astring
        std::shared_ptr<const RASString> aspace = det_->template space<0>(det_->string_bits_a(astring));
        size_t rank, off;
        std::tie(rank, off) = aspace->dist()->locate(astring - aspace->offset());
        assert(rank == mpi__->rank());
        for (auto b : this->template allowed_blocks<0>(aspace))
          std::copy_n(b->local() + off * b->lenb(), b->lenb(), buf.get() + b->stringsb()->offset());
        put_->request_send(std::move(buf), det_->lenb(), dest, tag);
      }
#ifndef USE_SERVER_THREAD
      put_->flush();
#endif
    }

    int get_bstring_buf(double* buf, const size_t a) const {
      assert(put_ && recv_);
      const size_t mpirank = mpi__->rank();
      std::shared_ptr<const RASString> aspace = det_->template space<0>(det_->string_bits_a(a));
      size_t rank, off;
      std::tie(rank, off) = aspace->dist()->locate(a - aspace->offset());

      int out = -1;
      if (mpirank == rank) {
        std::fill_n(buf, det_->lenb(), 0.0);
        for (auto b : this->template allowed_blocks<0>(aspace))
          std::copy_n(b->local()+off*b->lenb(), b->lenb(), buf + b->stringsb()->offset());
      } else {
        out = recv_->request_recv(buf, det_->lenb(), rank, a);
      }
      return out;
    }

    void zero() { this->for_each_block( [] (std::shared_ptr<RBlock> i) { std::fill_n(i->local(), i->size(), 0.0 ); } ); }

    std::shared_ptr<DistRASCivector<DataType>> clone() const { return std::make_shared<DistRASCivector<DataType>>(det_); }
    std::shared_ptr<DistRASCivector<DataType>> copy() const  { return std::make_shared<DistRASCivector<DataType>>(*this); }
    std::shared_ptr<DistRASCivector<DataType>> transpose(std::shared_ptr<const RASDeterminants> det = nullptr) const {
      if (!det) det = det_->transpose();
      auto out = std::make_shared<DistRASCivector<DataType>>(det);
      const int myrank = mpi__->rank();

      std::shared_ptr<DistRASCivector<DataType>> trans = clone();
      for (auto& sblock : blocks_) {
        if (!sblock) continue;
        std::shared_ptr<RBlock> tblock = out->block(sblock->stringsa(), sblock->stringsb());
        std::shared_ptr<RBlock> bufblock = trans->block(sblock->stringsb(), sblock->stringsa());
        assert(tblock->global_size() == sblock->global_size() && bufblock->global_size() == sblock->global_size());

        for (int i = 0; i < mpi__->size(); ++i) {
          std::tuple<size_t, size_t> outrange = tblock->dist().range(i);
          std::tuple<size_t, size_t> thisrange = sblock->dist().range(i);

          std::unique_ptr<DataType[]> tmp(new DataType[tblock->dist().size(i)*sblock->asize()]);
          for (size_t j = 0; j != sblock->asize(); ++j)
            std::copy_n(sblock->local()+std::get<0>(outrange)+j*sblock->lenb(), tblock->dist().size(i), tmp.get()+j*tblock->dist().size(i));

          const size_t off = std::get<0>(outrange) * sblock->asize();
          std::copy_n(tmp.get(), tblock->dist().size(i)*sblock->asize(), bufblock->local()+off);
          if ( i != myrank ) {
            const int tag_offset = sblock->block_offset() * mpi__->size();
            const size_t sendsize = tblock->dist().size(i) * sblock->asize();
            if (sendsize)
              out->transp_.push_back(mpi__->request_send(bufblock->local()+off, sendsize, i, tag_offset + myrank));
            const size_t recvsize = tblock->asize() * sblock->dist().size(i);
            if (recvsize)
              out->transp_.push_back(mpi__->request_recv(tblock->local()+tblock->asize()*std::get<0>(thisrange), recvsize, i, tag_offset + i));
          }
          else {
            std::copy_n(bufblock->local() + off, tblock->asize() * sblock->asize(), tblock->local() + sblock->astart() * tblock->asize());
          }
        }
      }

      out->buf_ = trans;
      return out;
    }

    void transpose_wait() {
      for (auto& i: transp_)
        mpi__->wait(i);
      buf_ = clone();
      for (auto i = blocks_.begin(), j = buf_->blocks().begin(); i != blocks_.end(); ++i, ++j) {
        if (!(*i)) continue;
        const size_t asize = (*i)->asize();
        const size_t lb = (*i)->lenb();
        if ( asize * lb == 0 ) continue;
        blas::transpose((*i)->local(), asize, lb, (*j)->local());
        std::copy_n((*j)->local(), asize * lb, (*i)->local());
      }
      buf_.reset();
    }

    std::shared_ptr<RASCivector<DataType>> civec() const { return std::make_shared<RASCivector<DataType>>(*this); }

    // Safe for any structure of blocks.
    DataType dot_product(const DistRASCivector<DataType>& o) const {
      assert( det_->nelea() == o.det()->nelea() && det_->neleb() == o.det()->neleb() && det_->norb() == o.det()->norb() );
      DataType out(0.0);
      for (auto& iblock : this->blocks()) {
        if (!iblock) continue;
        std::shared_ptr<const RBlock> jblock = o.block(iblock->stringsb(), iblock->stringsa());

        if (jblock) out += blas::dot_product(iblock->local(), iblock->size(), jblock->local());
      }

      mpi__->allreduce(&out, 1);
      return out;
    }

    double norm() const { return std::sqrt(dot_product(*this)); }
    double variance() const { return dot_product(*this) / global_size_; }
    double rms() const { return std::sqrt(variance()); }

    void scale(const DataType a) {
      this->for_each_block( [&a] (std::shared_ptr<RBlock> b) { std::for_each(b->local(), b->local()+b->size(), [&a] (DataType& p) { p*= a; }); });
    }
    void ax_plus_y(const DataType a, const DistRASCivector<DataType>& o) {
      this->for_each_block( [&a, &o] (std::shared_ptr<RBlock> iblock) {
        std::shared_ptr<const RBlock> jblock = o.block(iblock->stringsb(), iblock->stringsa());
        assert(jblock);
        blas::ax_plus_y_n(a, jblock->local(), iblock->size(), iblock->local());
      } );
    }
    void ax_plus_y(const DataType a, std::shared_ptr<const DistRASCivector<DataType>> o) { ax_plus_y(a, *o); }

    // Spin functions are only implememted as specialized functions for double (see civec.cc)
    // returns < S^2 >
    DataType spin_expectation() const {
      std::shared_ptr<const DistRASCivector<DataType>> S2 = spin();
      return this->dot_product(*S2);
    }
    std::shared_ptr<DistRASCivector<DataType>> spin() const { assert(false); return nullptr;} // returns S^2 | civec >
    std::shared_ptr<DistRASCivector<DataType>> spin_lower(std::shared_ptr<const RASDeterminants> target_det = nullptr) const { assert(false); return nullptr; } // S_-
    std::shared_ptr<DistRASCivector<DataType>> spin_raise(std::shared_ptr<const RASDeterminants> target_det = nullptr) const { assert(false); return nullptr; } // S_+
    void spin_decontaminate(const double thresh = 1.0e-8) { assert(false); }

    std::shared_ptr<DistRASCivector<DataType>> apply(const int orbital, const bool action, const bool spin) const {
      // action: true -> create; false -> annihilate
      // spin: true -> alpha; false -> beta
      auto out = nullptr;
      return out;
    }

    void project_out(std::shared_ptr<const DistRASCivector<DataType>> o) { ax_plus_y(-dot_product(*o), *o); }

    double orthog(std::list<std::shared_ptr<const DistRASCivector<DataType>>> c) {
      for (auto& iter : c)
        project_out(iter);
      return normalize();
    }

    double orthog(std::shared_ptr<const DistRASCivector<DataType>> o) {
      return orthog(std::list<std::shared_ptr<const DistRASCivector<DataType>>>{o});
    }

    double normalize() {
      const double norm = this->norm();
      const double scal = (norm*norm<1.0e-60 ? 0.0 : 1.0/norm);
      scale(DataType(scal));
      return norm;
    }

    void print(const double thr = 0.05) const {
      std::vector<DataType> data;
      std::vector<size_t> abits;
      std::vector<size_t> bbits;
      // multimap sorts elements so that they will be shown in the descending order in magnitude
      std::multimap<double, std::tuple<DataType, std::bitset<nbit__>, std::bitset<nbit__>>> tmp;
      for (auto& iblock : blocks_) {
        if (!iblock) continue;
        double* i = iblock->local();
        for (size_t ia = iblock->astart(); ia < iblock->aend(); ++ia) {
          for (size_t ib = 0; ib < iblock->lenb(); ++ib) {
            if (std::abs(*i) >= thr) {
              data.push_back(*i);
              abits.push_back(ia + iblock->stringsa()->offset());
              bbits.push_back(ib + iblock->stringsb()->offset());
            }
            ++i;
          }
        }
      }
      std::vector<size_t> nelements(mpi__->size(), 0);
      const size_t nn = data.size();
      mpi__->allgather(&nn, 1, nelements.data(), 1);

      const size_t chunk = *std::max_element(nelements.begin(), nelements.end());
      data.resize(chunk, 0);
      abits.resize(chunk, 0);
      bbits.resize(chunk, 0);

      std::vector<double> alldata(chunk * mpi__->size());
      mpi__->allgather(data.data(), chunk, alldata.data(), chunk);
      std::vector<size_t> allabits(chunk * mpi__->size());
      mpi__->allgather(abits.data(), chunk, allabits.data(), chunk);
      std::vector<size_t> allbbits(chunk * mpi__->size());
      mpi__->allgather(bbits.data(), chunk, allbbits.data(), chunk);

      if (mpi__->rank() == 0) {
        std::multimap<double, std::tuple<double, std::bitset<nbit__>, std::bitset<nbit__>>> tmp;
        for (int i = 0; i < chunk * mpi__->size(); ++i) {
          if (alldata[i] != 0.0)
            tmp.emplace(-std::abs(alldata[i]), std::make_tuple(alldata[i], det_->string_bits_a(allabits[i]), det_->string_bits_b(allbbits[i])));
        }

        for (auto& i : tmp) {
          std::cout << "       " << print_bit(std::get<1>(i.second), std::get<2>(i.second), det_->ras(0))
                    << "-" << print_bit(std::get<1>(i.second), std::get<2>(i.second), det_->ras(0), det_->ras(0)+det_->ras(1))
                    << "-" << print_bit(std::get<1>(i.second), std::get<2>(i.second), det_->ras(0)+det_->ras(1), det_->norb())
                    << "  " << std::setprecision(10) << std::setw(15) << std::get<0>(i.second) << std::endl;

        }
      }
    }
};

template<> std::shared_ptr<DistRASCivector<double>> DistRASCivector<double>::spin() const; // returns S^2 | civec >
template<> std::shared_ptr<DistRASCivector<double>> DistRASCivector<double>::spin_lower(std::shared_ptr<const RASDeterminants>) const; // S_-
template<> std::shared_ptr<DistRASCivector<double>> DistRASCivector<double>::spin_raise(std::shared_ptr<const RASDeterminants>) const; // S_+
template<> void DistRASCivector<double>::spin_decontaminate(const double thresh);

using DistRASCivec = DistRASCivector<double>;
using DistRASDvec = Dvector_base<DistRASCivec>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// partial specialization of CIBlock (ciutil/ciblock.h)
template<typename DataType>
using RASBlock = CIBlock<DataType, RASString>;
template<typename DataType>
using RASBlock_alloc = CIBlock_alloc<DataType, RASString>;


template <typename DataType>
class RASCivector : public RASCivector_base<RASBlock<DataType>> {
  public: using DetType = RASDeterminants;
  public: using RBlock = RASBlock<DataType>;
  public: using LocalizedType = std::true_type;

  protected:
    using RASCivector_base<RASBlock<DataType>>::blocks_;
    using RASCivector_base<RASBlock<DataType>>::det_;

    std::unique_ptr<DataType[]> data_;

    const size_t size_;

  public:
    RASCivector(std::shared_ptr<const RASDeterminants> det) : RASCivector_base<RASBlock<DataType>>(det), size_(det->size()) {
      data_ = std::unique_ptr<DataType[]>(new DataType[size_]);
      std::fill_n(data_.get(), size_, 0.0);

      size_t sz = 0;
      for (auto& ipair : det->blockinfo()) {
        if (!ipair->empty()) {
          blocks_.push_back(std::make_shared<RBlock>(ipair->stringsa(), ipair->stringsb(), data_.get()+sz, sz));
          sz += blocks_.back()->size();
        }
        else {
          blocks_.push_back(nullptr);
        }
      }
    }

    RASCivector(const RASCivector<DataType>& o) : RASCivector(o.det_) { std::copy_n(o.data(), size_, data_.get()); }
    RASCivector(std::shared_ptr<const RASCivector<DataType>> o) : RASCivector(*o) {}

    RASCivector(RASCivector<DataType>&& o) : RASCivector_base<RASBlock<DataType>>(o.det_), size_(o.size_)
      { blocks_ = std::move(o.blocks_); }

    RASCivector(const DistRASCivector<DataType>& o) : RASCivector(o.det()) {
      this->for_each_block( [&o] (std::shared_ptr<RBlock> b) {
        std::shared_ptr<const DistCIBlock<DataType>> distblock = o.block(b->stringsb(), b->stringsa());
        std::copy_n(distblock->local(), distblock->size(), b->data() + distblock->astart()*distblock->lenb());
      } );
      mpi__->allreduce(data(), size());
    }
    RASCivector(std::shared_ptr<const DistRASCivector<DataType>> o) : RASCivector(*o) {}

    DataType* data() { return data_.get(); }
    const DataType* data() const { return data_.get(); }

    // Copy assignment
    RASCivector<DataType>& operator=(const RASCivector<DataType>& o) {
      assert(*o.det_ == *det_);
      std::copy_n(o.data(), size_, data_.get());
      return *this;
    }

    // Move assignment
    RASCivector<DataType>& operator=(RASCivector<DataType>&& o) {
      assert(*o.det_ == *det_);
      data_ = std::move(o.data_);
      blocks_ = std::move(o.blocks_);
      return *this;
    }

    // Element-wise access. Beware: very slow!
    DataType& element(const std::bitset<nbit__> bstring, const std::bitset<nbit__> astring) {
      return block(bstring, astring)->element(bstring, astring);
    }
    const DataType& element(const std::bitset<nbit__> bstring, const std::bitset<nbit__> astring) const {
      return block(bstring, astring)->element(bstring, astring);
    }

    using RASCivector_base<RASBlock<DataType>>::block;

    const size_t size() const { return size_; }
    void zero() { std::fill_n(data_.get(), size_, 0.0); }

    std::shared_ptr<RASCivector<DataType>> clone() const { return std::make_shared<RASCivector<DataType>>(det_); }
    std::shared_ptr<RASCivector<DataType>> copy() const  { return std::make_shared<RASCivector<DataType>>(*this); }
    std::shared_ptr<RASCivector<DataType>> transpose(std::shared_ptr<const RASDeterminants> det = nullptr) const {
      if (!det) det = det_->transpose();
      auto out = std::make_shared<RASCivector<DataType>>(det);
      this->for_each_block( [&out]
        (std::shared_ptr<const RBlock> b) { blas::transpose(b->data(), b->lenb(), b->lena(), out->block(b->stringsa(), b->stringsb())->data(), 1.0); }
      );
      return out;
    }

    std::shared_ptr<DistRASCivector<DataType>> distcivec() const { return std::make_shared<DistRASCivector<DataType>>(*this); }

    // Safe for any structure of blocks.
    DataType dot_product(const RASCivector<DataType>& o) const {
      assert( det_->nelea() == o.det()->nelea() && det_->neleb() == o.det()->neleb() && det_->norb() == o.det()->norb() );
      DataType out(0.0);
      this->for_each_block( [&out, &o] (std::shared_ptr<const RBlock> b) {
        std::shared_ptr<const RBlock> j = o.block(b->stringsb(), b->stringsa());
        if (j) out += blas::dot_product(b->data(), b->lena()*b->lenb(), j->data());
      } );
      return out;
    }

    double norm() const { return std::sqrt(dot_product(*this)); }
    double variance() const { return dot_product(*this) / size_; }
    double rms() const { return std::sqrt(variance()); }

    void scale(const DataType a) { std::for_each( data(), data() + size_, [&a] (DataType& p) { p *= a; } ); }
    void ax_plus_y(const DataType a, const RASCivector<DataType>& o) { blas::ax_plus_y_n(a, o.data(), size_, data()); }
    void ax_plus_y(const DataType a, std::shared_ptr<const RASCivector<DataType>> o) { ax_plus_y(a, *o); }

    // Spin functions are only implememted as specialized functions for double (see civec.cc)
    double spin_expectation() const { assert(false); return 0.0; } // returns < S^2 >
    std::shared_ptr<RASCivector<DataType>> spin() const { assert(false); return nullptr;} // returns S^2 | civec >
    std::shared_ptr<RASCivector<DataType>> spin_lower(std::shared_ptr<const RASDeterminants> target_det = nullptr) const { assert(false); return nullptr; } // S_-
    std::shared_ptr<RASCivector<DataType>> spin_raise(std::shared_ptr<const RASDeterminants> target_det = nullptr) const { assert(false); return nullptr; } // S_+
    void spin_decontaminate(const double thresh = 1.0e-8) { assert(false); }

    std::shared_ptr<RASCivector<DataType>> apply(const int orbital, const bool action, const bool spin) const {
      // action: true -> create; false -> annihilate
      // spin: true -> alpha; false -> beta
      std::shared_ptr<const RASDeterminants> sdet = this->det();

      const int ras1 = sdet->ras(0);
      const int ras2 = sdet->ras(1);
      const int ras3 = sdet->ras(2);

      // 0 -> RASI, 1 -> RASII, 2 -> RASIII
      const int ras_space = ( orbital >= ras1 ) + (orbital >= ras1 + ras2);

      auto to_array = [] (std::shared_ptr<const RASBlock<DataType>> block) {
        auto sa = block->stringsa();
        auto sb = block->stringsb();
        return std::array<int, 6>({sa->nholes(), sb->nholes(), sa->nele2(), sb->nele2(), sa->nparticles(), sb->nparticles()});
      };

      auto op_on_array = [&ras_space, &action, &spin] ( std::array<int, 6> in ) {
        const int mod = ( action ? +1 : -1 ) * ( ras_space == 0 ? -1 : 1 );
        std::array<int, 6> out = in;
        out[2*ras_space] += (spin ? mod : 0);
        out[2*ras_space+1] += (spin ? 0 : mod);
        return out;
      };

      RAS::Apply_block apply_block(orbital, action, spin);

      const int mod = action ? +1 : -1;
      const int telea = sdet->nelea() + ( spin ? mod : 0 );
      const int teleb = sdet->neleb() + ( spin ? 0 : mod );
      const int tholes = std::max(sdet->max_holes() - ( (ras_space == 0) ? mod : 0 ), 0);
      const int tparts = std::max(sdet->max_particles() + ( (ras_space == 2) ? mod : 0), 0);

      auto tdet = std::make_shared<const RASDeterminants>(ras1, ras2, ras3, telea, teleb, tholes, tparts, true);
      auto out = std::make_shared<RASCivector<DataType>>(tdet);

      for (auto& soblock : this->blocks()) {
        if (!soblock) continue;
        std::array<int, 6> tar_array = op_on_array(to_array(soblock));
        if ( std::all_of(tar_array.begin(), tar_array.end(), [] (int i) { return i >= 0; }) ) {
          std::shared_ptr<RASBlock<double>> tarblock = out->block(tar_array[0], tar_array[1], tar_array[4], tar_array[5]);
          if (tarblock) apply_block(soblock, tarblock);
        }
      }

      return out;
    }

    void project_out(std::shared_ptr<const RASCivector<DataType>> o) { ax_plus_y(-dot_product(*o), *o); }

    double orthog(std::list<std::shared_ptr<const RASCivector<DataType>>> c) {
      for (auto& iter : c)
        project_out(iter);
      return normalize();
    }

    double orthog(std::shared_ptr<const RASCivector<DataType>> o) {
      return orthog(std::list<std::shared_ptr<const RASCivector<DataType>>>{o});
    }

    double normalize() {
      const double norm = this->norm();
      const double scal = (norm*norm<1.0e-60 ? 0.0 : 1.0/norm);
      scale(DataType(scal));
      return norm;
    }

    void print(const double thr = 0.05) const {
      // multimap sorts elements so that they will be shown in the descending order in magnitude
      std::multimap<double, std::tuple<DataType, std::bitset<nbit__>, std::bitset<nbit__>>> tmp;
      for (auto& iblock : blocks_) {
        if (!iblock) continue;
        double* i = iblock->data();
        for (auto& ia : *iblock->stringsa()) {
          for (auto& ib : *iblock->stringsb()) {
            if (std::abs(*i) > thr)
              tmp.emplace(-std::abs(*i), std::make_tuple(*i, ia, ib));
            ++i;
          }
        }
      }
      for (auto& i : tmp)
        std::cout << "       " << print_bit(std::get<1>(i.second), std::get<2>(i.second), det_->ras(0))
                  << "-" << print_bit(std::get<1>(i.second), std::get<2>(i.second), det_->ras(0), det_->ras(0)+det_->ras(1))
                  << "-" << print_bit(std::get<1>(i.second), std::get<2>(i.second), det_->ras(0)+det_->ras(1), det_->norb())
                  << "  " << std::setprecision(10) << std::setw(15) << std::get<0>(i.second) << std::endl;
    }

    void synchronize(const int root = 0) {
#ifdef HAVE_MPI_H
      mpi__->broadcast(data(), size(), root);
#endif /* HAVE_MPI_H */
    }
};

template<> double RASCivector<double>::spin_expectation() const; // returns < S^2 >
template<> std::shared_ptr<RASCivector<double>> RASCivector<double>::spin() const; // returns S^2 | civec >
template<> std::shared_ptr<RASCivector<double>> RASCivector<double>::spin_lower(std::shared_ptr<const RASDeterminants>) const; // S_-
template<> std::shared_ptr<RASCivector<double>> RASCivector<double>::spin_raise(std::shared_ptr<const RASDeterminants>) const; // S_+
template<> void RASCivector<double>::spin_decontaminate(const double thresh);

using RASCivec = RASCivector<double>;
using RASDvec  = Dvector_base<RASCivec>;

}

#endif
