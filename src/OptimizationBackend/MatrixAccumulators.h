/**
* This file is part of DSO.
* 
* Copyright 2016 Technical University of Munich and Intel.
* Developed by Jakob Engel <engelj at in dot tum dot de>,
* for more information see <http://vision.in.tum.de/dso>.
* If you use this code, please cite the respective publications as
* listed on the above website.
*
* DSO is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* DSO is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with DSO. If not, see <http://www.gnu.org/licenses/>.
*/


#pragma once

#include "util/NumType.h"

#if !defined(__SSE3__) && !defined(__SSE2__) && !defined(__SSE1__)
#include "SSE2NEON.h"
#endif

namespace dso {


  template<int i, int j>
  class AccumulatorXX {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    Eigen::Matrix<float, i, j> A;
    Eigen::Matrix<float, i, j> A1k;
    Eigen::Matrix<float, i, j> A1m;
    size_t num;

    inline void initialize() {
      A.setZero();
      A1k.setZero();
      A1m.setZero();
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      shiftUp(true);
      num = numIn1 + numIn1k + numIn1m;
    }


    inline void update(const Eigen::Matrix<float, i, 1> &L, const Eigen::Matrix<float, j, 1> &R, float w) {
      A += w * L * R.transpose();
      numIn1++;
      shiftUp(false);
    }

  private:
    float numIn1, numIn1k, numIn1m;

    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        A1k += A;
        A.setZero();
        numIn1k += numIn1;
        numIn1 = 0;
      }
      if (numIn1k > 1000 || force) {
        A1m += A1k;
        A1k.setZero();
        numIn1m += numIn1k;
        numIn1k = 0;
      }
    }
  };

  class Accumulator1 {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    float A;
    size_t num;

    inline void initialize() {
      A = 0;
      memset(SSEData, 0, sizeof(float) * 4 * 1);
      memset(SSEData1k, 0, sizeof(float) * 4 * 1);
      memset(SSEData1m, 0, sizeof(float) * 4 * 1);
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      shiftUp(true);
      A = SSEData1m[0 + 0] + SSEData1m[0 + 1] + SSEData1m[0 + 2] + SSEData1m[0 + 3];
    }


    inline void updateSingle(
        const float val) {
      SSEData[0] += val;
      num++;
      numIn1++;
      shiftUp(false);
    }

    inline void updateSSE(
        const __m128 val) {
      _mm_store_ps(SSEData, _mm_add_ps(_mm_load_ps(SSEData), val));
      num += 4;
      numIn1++;
      shiftUp(false);
    }

    inline void updateSingleNoShift(
        const float val) {
      SSEData[0] += val;
      num++;
      numIn1++;
    }

    inline void updateSSENoShift(
        const __m128 val) {
      _mm_store_ps(SSEData, _mm_add_ps(_mm_load_ps(SSEData), val));
      num += 4;
      numIn1++;
    }


  private:
    EIGEN_ALIGN16 float SSEData[4 * 1];
    EIGEN_ALIGN16 float SSEData1k[4 * 1];
    EIGEN_ALIGN16 float SSEData1m[4 * 1];
    float numIn1, numIn1k, numIn1m;


    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        _mm_store_ps(SSEData1k, _mm_add_ps(_mm_load_ps(SSEData), _mm_load_ps(SSEData1k)));
        numIn1k += numIn1;
        numIn1 = 0;
        memset(SSEData, 0, sizeof(float) * 4 * 1);
      }

      if (numIn1k > 1000 || force) {
        _mm_store_ps(SSEData1m, _mm_add_ps(_mm_load_ps(SSEData1k), _mm_load_ps(SSEData1m)));
        numIn1m += numIn1k;
        numIn1k = 0;
        memset(SSEData1k, 0, sizeof(float) * 4 * 1);
      }
    }
  };


  template<int i>
  class AccumulatorX {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    Eigen::Matrix<float, i, 1> A;
    Eigen::Matrix<float, i, 1> A1k;
    Eigen::Matrix<float, i, 1> A1m;
    size_t num;

    inline void initialize() {
      A.setZero();
      A1k.setZero();
      A1m.setZero();
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      shiftUp(true);
      num = numIn1 + numIn1k + numIn1m;
    }


    inline void update(const Eigen::Matrix<float, i, 1> &L, float w) {
      A += w * L;
      numIn1++;
      shiftUp(false);
    }

    inline void updateNoWeight(const Eigen::Matrix<float, i, 1> &L) {
      A += L;
      numIn1++;
      shiftUp(false);
    }

  private:
    float numIn1, numIn1k, numIn1m;

    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        A1k += A;
        A.setZero();
        numIn1k += numIn1;
        numIn1 = 0;
      }
      if (numIn1k > 1000 || force) {
        A1m += A1k;
        A1k.setZero();
        numIn1m += numIn1k;
        numIn1k = 0;
      }
    }
  };


  class Accumulator14 {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    Mat1414f H;
    Vec14f b;
    size_t num;

    inline void initialize() {
      H.setZero();
      b.setZero();
      memset(SSEData, 0, sizeof(float) * 4 * 105);
      memset(SSEData1k, 0, sizeof(float) * 4 * 105);
      memset(SSEData1m, 0, sizeof(float) * 4 * 105);
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      H.setZero();
      shiftUp(true);
      assert(numIn1 == 0);
      assert(numIn1k == 0);

      int idx = 0;
      for (int r = 0; r < 14; r++)
        for (int c = r; c < 14; c++) {
          float d = SSEData1m[idx + 0] + SSEData1m[idx + 1] + SSEData1m[idx + 2] + SSEData1m[idx + 3];
          H(r, c) = H(c, r) = d;
          idx += 4;
        }
      assert(idx == 4 * 105);
      num = numIn1 + numIn1k + numIn1m;
    }


    inline void updateSSE(
        const __m128 J0, const __m128 J1,
        const __m128 J2, const __m128 J3,
        const __m128 J4, const __m128 J5,
        const __m128 J6, const __m128 J7,
        const __m128 J8, const __m128 J9,
        const __m128 J10, const __m128 J11,
        const __m128 J12, const __m128 J13) {
      float *pt = SSEData;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J0)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J10, J10)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J10, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J10, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J10, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J11, J11)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J11, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J11, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J12, J12)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J12, J13)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J13, J13)));
      pt += 4;

      num += 4;
      numIn1++;
      shiftUp(false);
    }


    inline void updateSingle(
        const float J0, const float J1,
        const float J2, const float J3,
        const float J4, const float J5,
        const float J6, const float J7,
        const float J8, const float J9,
        const float J10, const float J11,
        const float J12, const float J13,
        int off = 0) {
      float *pt = SSEData + off;
      *pt += J0 * J0;
      pt += 4;
      *pt += J1 * J0;
      pt += 4;
      *pt += J2 * J0;
      pt += 4;
      *pt += J3 * J0;
      pt += 4;
      *pt += J4 * J0;
      pt += 4;
      *pt += J5 * J0;
      pt += 4;
      *pt += J6 * J0;
      pt += 4;
      *pt += J7 * J0;
      pt += 4;
      *pt += J8 * J0;
      pt += 4;
      *pt += J9 * J0;
      pt += 4;
      *pt += J10 * J0;
      pt += 4;
      *pt += J11 * J0;
      pt += 4;
      *pt += J12 * J0;
      pt += 4;
      *pt += J13 * J0;
      pt += 4;

      *pt += J1 * J1;
      pt += 4;
      *pt += J2 * J1;
      pt += 4;
      *pt += J3 * J1;
      pt += 4;
      *pt += J4 * J1;
      pt += 4;
      *pt += J5 * J1;
      pt += 4;
      *pt += J6 * J1;
      pt += 4;
      *pt += J7 * J1;
      pt += 4;
      *pt += J8 * J1;
      pt += 4;
      *pt += J9 * J1;
      pt += 4;
      *pt += J10 * J1;
      pt += 4;
      *pt += J11 * J1;
      pt += 4;
      *pt += J12 * J1;
      pt += 4;
      *pt += J13 * J1;
      pt += 4;

      *pt += J2 * J2;
      pt += 4;
      *pt += J3 * J2;
      pt += 4;
      *pt += J4 * J2;
      pt += 4;
      *pt += J5 * J2;
      pt += 4;
      *pt += J6 * J2;
      pt += 4;
      *pt += J7 * J2;
      pt += 4;
      *pt += J8 * J2;
      pt += 4;
      *pt += J9 * J2;
      pt += 4;
      *pt += J10 * J2;
      pt += 4;
      *pt += J11 * J2;
      pt += 4;
      *pt += J12 * J2;
      pt += 4;
      *pt += J13 * J2;
      pt += 4;

      *pt += J3 * J3;
      pt += 4;
      *pt += J4 * J3;
      pt += 4;
      *pt += J5 * J3;
      pt += 4;
      *pt += J6 * J3;
      pt += 4;
      *pt += J7 * J3;
      pt += 4;
      *pt += J8 * J3;
      pt += 4;
      *pt += J9 * J3;
      pt += 4;
      *pt += J10 * J3;
      pt += 4;
      *pt += J11 * J3;
      pt += 4;
      *pt += J12 * J3;
      pt += 4;
      *pt += J13 * J3;
      pt += 4;

      *pt += J4 * J4;
      pt += 4;
      *pt += J5 * J4;
      pt += 4;
      *pt += J6 * J4;
      pt += 4;
      *pt += J7 * J4;
      pt += 4;
      *pt += J8 * J4;
      pt += 4;
      *pt += J9 * J4;
      pt += 4;
      *pt += J10 * J4;
      pt += 4;
      *pt += J11 * J4;
      pt += 4;
      *pt += J12 * J4;
      pt += 4;
      *pt += J13 * J4;
      pt += 4;

      *pt += J5 * J5;
      pt += 4;
      *pt += J6 * J5;
      pt += 4;
      *pt += J7 * J5;
      pt += 4;
      *pt += J8 * J5;
      pt += 4;
      *pt += J9 * J5;
      pt += 4;
      *pt += J10 * J5;
      pt += 4;
      *pt += J11 * J5;
      pt += 4;
      *pt += J12 * J5;
      pt += 4;
      *pt += J13 * J5;
      pt += 4;

      *pt += J6 * J6;
      pt += 4;
      *pt += J7 * J6;
      pt += 4;
      *pt += J8 * J6;
      pt += 4;
      *pt += J9 * J6;
      pt += 4;
      *pt += J10 * J6;
      pt += 4;
      *pt += J11 * J6;
      pt += 4;
      *pt += J12 * J6;
      pt += 4;
      *pt += J13 * J6;
      pt += 4;

      *pt += J7 * J7;
      pt += 4;
      *pt += J8 * J7;
      pt += 4;
      *pt += J9 * J7;
      pt += 4;
      *pt += J10 * J7;
      pt += 4;
      *pt += J11 * J7;
      pt += 4;
      *pt += J12 * J7;
      pt += 4;
      *pt += J13 * J7;
      pt += 4;

      *pt += J8 * J8;
      pt += 4;
      *pt += J9 * J8;
      pt += 4;
      *pt += J10 * J8;
      pt += 4;
      *pt += J11 * J8;
      pt += 4;
      *pt += J12 * J8;
      pt += 4;
      *pt += J13 * J8;
      pt += 4;

      *pt += J9 * J9;
      pt += 4;
      *pt += J10 * J9;
      pt += 4;
      *pt += J11 * J9;
      pt += 4;
      *pt += J12 * J9;
      pt += 4;
      *pt += J13 * J9;
      pt += 4;

      *pt += J10 * J10;
      pt += 4;
      *pt += J11 * J10;
      pt += 4;
      *pt += J12 * J10;
      pt += 4;
      *pt += J13 * J10;
      pt += 4;

      *pt += J11 * J11;
      pt += 4;
      *pt += J12 * J11;
      pt += 4;
      *pt += J13 * J11;
      pt += 4;

      *pt += J12 * J12;
      pt += 4;
      *pt += J13 * J12;
      pt += 4;

      *pt += J13 * J13;
      pt += 4;

      num++;
      numIn1++;
      shiftUp(false);
    }


  private:
    EIGEN_ALIGN16 float SSEData[4 * 105];
    EIGEN_ALIGN16 float SSEData1k[4 * 105];
    EIGEN_ALIGN16 float SSEData1m[4 * 105];
    float numIn1, numIn1k, numIn1m;


    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        for (int i = 0; i < 105; i++)
          _mm_store_ps(SSEData1k + 4 * i, _mm_add_ps(_mm_load_ps(SSEData + 4 * i), _mm_load_ps(SSEData1k + 4 * i)));
        numIn1k += numIn1;
        numIn1 = 0;
        memset(SSEData, 0, sizeof(float) * 4 * 105);
      }

      if (numIn1k > 1000 || force) {
        for (int i = 0; i < 105; i++)
          _mm_store_ps(SSEData1m + 4 * i, _mm_add_ps(_mm_load_ps(SSEData1k + 4 * i), _mm_load_ps(SSEData1m + 4 * i)));
        numIn1m += numIn1k;
        numIn1k = 0;
        memset(SSEData1k, 0, sizeof(float) * 4 * 105);
      }
    }
  };


/*
 * computes the outer sum of 10x2 matrices, weighted with a 2x2 matrix:
 * 			H = [x y] * [a b; b c] * [x y]^T
 * (assuming x,y are column-vectors).
 * numerically robust to large sums.
 */
  class AccumulatorApprox {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    Mat1313f H;
    size_t num;

    inline void initialize() {
      memset(Data, 0, sizeof(float) * 60);
      memset(Data1k, 0, sizeof(float) * 60);
      memset(Data1m, 0, sizeof(float) * 60);

      memset(TopRight_Data, 0, sizeof(float) * 32);
      memset(TopRight_Data1k, 0, sizeof(float) * 32);
      memset(TopRight_Data1m, 0, sizeof(float) * 32);

      memset(BotRight_Data, 0, sizeof(float) * 8);
      memset(BotRight_Data1k, 0, sizeof(float) * 8);
      memset(BotRight_Data1m, 0, sizeof(float) * 8);
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      H.setZero();
      shiftUp(true);
      assert(numIn1 == 0);
      assert(numIn1k == 0);

      int idx = 0;
      for (int r = 0; r < 10; r++)
        for (int c = r; c < 10; c++) {
          H(r, c) = H(c, r) = Data1m[idx];
          idx++;
        }

      idx = 0;
      for (int r = 0; r < 10; r++)
        for (int c = 0; c < 3; c++) {
          H(r, c + 10) = H(c + 10, r) = TopRight_Data1m[idx];
          idx++;
        }

      H(10, 10) = BotRight_Data1m[0];
      H(10, 11) = H(11, 10) = BotRight_Data1m[1];
      H(10, 12) = H(12, 10) = BotRight_Data1m[2];
      H(11, 11) = BotRight_Data1m[3];
      H(11, 12) = H(12, 11) = BotRight_Data1m[4];
      H(12, 12) = BotRight_Data1m[5];


      num = numIn1 + numIn1k + numIn1m;
    }


    inline void updateSSE(
        const float *const x,
        const float *const y,
        const float a,
        const float b,
        const float c) {

      Data[0] += a * x[0] * x[0] + c * y[0] * y[0] + b * (x[0] * y[0] + y[0] * x[0]);
      Data[1] += a * x[1] * x[0] + c * y[1] * y[0] + b * (x[1] * y[0] + y[1] * x[0]);
      Data[2] += a * x[2] * x[0] + c * y[2] * y[0] + b * (x[2] * y[0] + y[2] * x[0]);
      Data[3] += a * x[3] * x[0] + c * y[3] * y[0] + b * (x[3] * y[0] + y[3] * x[0]);
      Data[4] += a * x[4] * x[0] + c * y[4] * y[0] + b * (x[4] * y[0] + y[4] * x[0]);
      Data[5] += a * x[5] * x[0] + c * y[5] * y[0] + b * (x[5] * y[0] + y[5] * x[0]);
      Data[6] += a * x[6] * x[0] + c * y[6] * y[0] + b * (x[6] * y[0] + y[6] * x[0]);
      Data[7] += a * x[7] * x[0] + c * y[7] * y[0] + b * (x[7] * y[0] + y[7] * x[0]);
      Data[8] += a * x[8] * x[0] + c * y[8] * y[0] + b * (x[8] * y[0] + y[8] * x[0]);
      Data[9] += a * x[9] * x[0] + c * y[9] * y[0] + b * (x[9] * y[0] + y[9] * x[0]);


      Data[10] += a * x[1] * x[1] + c * y[1] * y[1] + b * (x[1] * y[1] + y[1] * x[1]);
      Data[11] += a * x[2] * x[1] + c * y[2] * y[1] + b * (x[2] * y[1] + y[2] * x[1]);
      Data[12] += a * x[3] * x[1] + c * y[3] * y[1] + b * (x[3] * y[1] + y[3] * x[1]);
      Data[13] += a * x[4] * x[1] + c * y[4] * y[1] + b * (x[4] * y[1] + y[4] * x[1]);
      Data[14] += a * x[5] * x[1] + c * y[5] * y[1] + b * (x[5] * y[1] + y[5] * x[1]);
      Data[15] += a * x[6] * x[1] + c * y[6] * y[1] + b * (x[6] * y[1] + y[6] * x[1]);
      Data[16] += a * x[7] * x[1] + c * y[7] * y[1] + b * (x[7] * y[1] + y[7] * x[1]);
      Data[17] += a * x[8] * x[1] + c * y[8] * y[1] + b * (x[8] * y[1] + y[8] * x[1]);
      Data[18] += a * x[9] * x[1] + c * y[9] * y[1] + b * (x[9] * y[1] + y[9] * x[1]);


      Data[19] += a * x[2] * x[2] + c * y[2] * y[2] + b * (x[2] * y[2] + y[2] * x[2]);
      Data[20] += a * x[3] * x[2] + c * y[3] * y[2] + b * (x[3] * y[2] + y[3] * x[2]);
      Data[21] += a * x[4] * x[2] + c * y[4] * y[2] + b * (x[4] * y[2] + y[4] * x[2]);
      Data[22] += a * x[5] * x[2] + c * y[5] * y[2] + b * (x[5] * y[2] + y[5] * x[2]);
      Data[23] += a * x[6] * x[2] + c * y[6] * y[2] + b * (x[6] * y[2] + y[6] * x[2]);
      Data[24] += a * x[7] * x[2] + c * y[7] * y[2] + b * (x[7] * y[2] + y[7] * x[2]);
      Data[25] += a * x[8] * x[2] + c * y[8] * y[2] + b * (x[8] * y[2] + y[8] * x[2]);
      Data[26] += a * x[9] * x[2] + c * y[9] * y[2] + b * (x[9] * y[2] + y[9] * x[2]);


      Data[27] += a * x[3] * x[3] + c * y[3] * y[3] + b * (x[3] * y[3] + y[3] * x[3]);
      Data[28] += a * x[4] * x[3] + c * y[4] * y[3] + b * (x[4] * y[3] + y[4] * x[3]);
      Data[29] += a * x[5] * x[3] + c * y[5] * y[3] + b * (x[5] * y[3] + y[5] * x[3]);
      Data[30] += a * x[6] * x[3] + c * y[6] * y[3] + b * (x[6] * y[3] + y[6] * x[3]);
      Data[31] += a * x[7] * x[3] + c * y[7] * y[3] + b * (x[7] * y[3] + y[7] * x[3]);
      Data[32] += a * x[8] * x[3] + c * y[8] * y[3] + b * (x[8] * y[3] + y[8] * x[3]);
      Data[33] += a * x[9] * x[3] + c * y[9] * y[3] + b * (x[9] * y[3] + y[9] * x[3]);


      Data[34] += a * x[4] * x[4] + c * y[4] * y[4] + b * (x[4] * y[4] + y[4] * x[4]);
      Data[35] += a * x[5] * x[4] + c * y[5] * y[4] + b * (x[5] * y[4] + y[5] * x[4]);
      Data[36] += a * x[6] * x[4] + c * y[6] * y[4] + b * (x[6] * y[4] + y[6] * x[4]);
      Data[37] += a * x[7] * x[4] + c * y[7] * y[4] + b * (x[7] * y[4] + y[7] * x[4]);
      Data[38] += a * x[8] * x[4] + c * y[8] * y[4] + b * (x[8] * y[4] + y[8] * x[4]);
      Data[39] += a * x[9] * x[4] + c * y[9] * y[4] + b * (x[9] * y[4] + y[9] * x[4]);


      Data[40] += a * x[5] * x[5] + c * y[5] * y[5] + b * (x[5] * y[5] + y[5] * x[5]);
      Data[41] += a * x[6] * x[5] + c * y[6] * y[5] + b * (x[6] * y[5] + y[6] * x[5]);
      Data[42] += a * x[7] * x[5] + c * y[7] * y[5] + b * (x[7] * y[5] + y[7] * x[5]);
      Data[43] += a * x[8] * x[5] + c * y[8] * y[5] + b * (x[8] * y[5] + y[8] * x[5]);
      Data[44] += a * x[9] * x[5] + c * y[9] * y[5] + b * (x[9] * y[5] + y[9] * x[5]);


      Data[45] += a * x[6] * x[6] + c * y[6] * y[6] + b * (x[6] * y[6] + y[6] * x[6]);
      Data[46] += a * x[7] * x[6] + c * y[7] * y[6] + b * (x[7] * y[6] + y[7] * x[6]);
      Data[47] += a * x[8] * x[6] + c * y[8] * y[6] + b * (x[8] * y[6] + y[8] * x[6]);
      Data[48] += a * x[9] * x[6] + c * y[9] * y[6] + b * (x[9] * y[6] + y[9] * x[6]);


      Data[49] += a * x[7] * x[7] + c * y[7] * y[7] + b * (x[7] * y[7] + y[7] * x[7]);
      Data[50] += a * x[8] * x[7] + c * y[8] * y[7] + b * (x[8] * y[7] + y[8] * x[7]);
      Data[51] += a * x[9] * x[7] + c * y[9] * y[7] + b * (x[9] * y[7] + y[9] * x[7]);


      Data[52] += a * x[8] * x[8] + c * y[8] * y[8] + b * (x[8] * y[8] + y[8] * x[8]);
      Data[53] += a * x[9] * x[8] + c * y[9] * y[8] + b * (x[9] * y[8] + y[9] * x[8]);

      Data[54] += a * x[9] * x[9] + c * y[9] * y[9] + b * (x[9] * y[9] + y[9] * x[9]);


      num++;
      numIn1++;
      shiftUp(false);
    }


/*
 * same as other method, just that x/y are composed of two parts, the first 4 elements are in x4/y4, the last 6 in x6/y6.
 */
    inline void update(
        const float *const x4,
        const float *const x6,
        const float *const y4,
        const float *const y6,
        const float a,
        const float b,
        const float c) {

      Data[0] += a * x4[0] * x4[0] + c * y4[0] * y4[0] + b * (x4[0] * y4[0] + y4[0] * x4[0]);
      Data[1] += a * x4[1] * x4[0] + c * y4[1] * y4[0] + b * (x4[1] * y4[0] + y4[1] * x4[0]);
      Data[2] += a * x4[2] * x4[0] + c * y4[2] * y4[0] + b * (x4[2] * y4[0] + y4[2] * x4[0]);
      Data[3] += a * x4[3] * x4[0] + c * y4[3] * y4[0] + b * (x4[3] * y4[0] + y4[3] * x4[0]);
      Data[4] += a * x6[0] * x4[0] + c * y6[0] * y4[0] + b * (x6[0] * y4[0] + y6[0] * x4[0]);
      Data[5] += a * x6[1] * x4[0] + c * y6[1] * y4[0] + b * (x6[1] * y4[0] + y6[1] * x4[0]);
      Data[6] += a * x6[2] * x4[0] + c * y6[2] * y4[0] + b * (x6[2] * y4[0] + y6[2] * x4[0]);
      Data[7] += a * x6[3] * x4[0] + c * y6[3] * y4[0] + b * (x6[3] * y4[0] + y6[3] * x4[0]);
      Data[8] += a * x6[4] * x4[0] + c * y6[4] * y4[0] + b * (x6[4] * y4[0] + y6[4] * x4[0]);
      Data[9] += a * x6[5] * x4[0] + c * y6[5] * y4[0] + b * (x6[5] * y4[0] + y6[5] * x4[0]);


      Data[10] += a * x4[1] * x4[1] + c * y4[1] * y4[1] + b * (x4[1] * y4[1] + y4[1] * x4[1]);
      Data[11] += a * x4[2] * x4[1] + c * y4[2] * y4[1] + b * (x4[2] * y4[1] + y4[2] * x4[1]);
      Data[12] += a * x4[3] * x4[1] + c * y4[3] * y4[1] + b * (x4[3] * y4[1] + y4[3] * x4[1]);
      Data[13] += a * x6[0] * x4[1] + c * y6[0] * y4[1] + b * (x6[0] * y4[1] + y6[0] * x4[1]);
      Data[14] += a * x6[1] * x4[1] + c * y6[1] * y4[1] + b * (x6[1] * y4[1] + y6[1] * x4[1]);
      Data[15] += a * x6[2] * x4[1] + c * y6[2] * y4[1] + b * (x6[2] * y4[1] + y6[2] * x4[1]);
      Data[16] += a * x6[3] * x4[1] + c * y6[3] * y4[1] + b * (x6[3] * y4[1] + y6[3] * x4[1]);
      Data[17] += a * x6[4] * x4[1] + c * y6[4] * y4[1] + b * (x6[4] * y4[1] + y6[4] * x4[1]);
      Data[18] += a * x6[5] * x4[1] + c * y6[5] * y4[1] + b * (x6[5] * y4[1] + y6[5] * x4[1]);


      Data[19] += a * x4[2] * x4[2] + c * y4[2] * y4[2] + b * (x4[2] * y4[2] + y4[2] * x4[2]);
      Data[20] += a * x4[3] * x4[2] + c * y4[3] * y4[2] + b * (x4[3] * y4[2] + y4[3] * x4[2]);
      Data[21] += a * x6[0] * x4[2] + c * y6[0] * y4[2] + b * (x6[0] * y4[2] + y6[0] * x4[2]);
      Data[22] += a * x6[1] * x4[2] + c * y6[1] * y4[2] + b * (x6[1] * y4[2] + y6[1] * x4[2]);
      Data[23] += a * x6[2] * x4[2] + c * y6[2] * y4[2] + b * (x6[2] * y4[2] + y6[2] * x4[2]);
      Data[24] += a * x6[3] * x4[2] + c * y6[3] * y4[2] + b * (x6[3] * y4[2] + y6[3] * x4[2]);
      Data[25] += a * x6[4] * x4[2] + c * y6[4] * y4[2] + b * (x6[4] * y4[2] + y6[4] * x4[2]);
      Data[26] += a * x6[5] * x4[2] + c * y6[5] * y4[2] + b * (x6[5] * y4[2] + y6[5] * x4[2]);


      Data[27] += a * x4[3] * x4[3] + c * y4[3] * y4[3] + b * (x4[3] * y4[3] + y4[3] * x4[3]);
      Data[28] += a * x6[0] * x4[3] + c * y6[0] * y4[3] + b * (x6[0] * y4[3] + y6[0] * x4[3]);
      Data[29] += a * x6[1] * x4[3] + c * y6[1] * y4[3] + b * (x6[1] * y4[3] + y6[1] * x4[3]);
      Data[30] += a * x6[2] * x4[3] + c * y6[2] * y4[3] + b * (x6[2] * y4[3] + y6[2] * x4[3]);
      Data[31] += a * x6[3] * x4[3] + c * y6[3] * y4[3] + b * (x6[3] * y4[3] + y6[3] * x4[3]);
      Data[32] += a * x6[4] * x4[3] + c * y6[4] * y4[3] + b * (x6[4] * y4[3] + y6[4] * x4[3]);
      Data[33] += a * x6[5] * x4[3] + c * y6[5] * y4[3] + b * (x6[5] * y4[3] + y6[5] * x4[3]);


      Data[34] += a * x6[0] * x6[0] + c * y6[0] * y6[0] + b * (x6[0] * y6[0] + y6[0] * x6[0]);
      Data[35] += a * x6[1] * x6[0] + c * y6[1] * y6[0] + b * (x6[1] * y6[0] + y6[1] * x6[0]);
      Data[36] += a * x6[2] * x6[0] + c * y6[2] * y6[0] + b * (x6[2] * y6[0] + y6[2] * x6[0]);
      Data[37] += a * x6[3] * x6[0] + c * y6[3] * y6[0] + b * (x6[3] * y6[0] + y6[3] * x6[0]);
      Data[38] += a * x6[4] * x6[0] + c * y6[4] * y6[0] + b * (x6[4] * y6[0] + y6[4] * x6[0]);
      Data[39] += a * x6[5] * x6[0] + c * y6[5] * y6[0] + b * (x6[5] * y6[0] + y6[5] * x6[0]);


      Data[40] += a * x6[1] * x6[1] + c * y6[1] * y6[1] + b * (x6[1] * y6[1] + y6[1] * x6[1]);
      Data[41] += a * x6[2] * x6[1] + c * y6[2] * y6[1] + b * (x6[2] * y6[1] + y6[2] * x6[1]);
      Data[42] += a * x6[3] * x6[1] + c * y6[3] * y6[1] + b * (x6[3] * y6[1] + y6[3] * x6[1]);
      Data[43] += a * x6[4] * x6[1] + c * y6[4] * y6[1] + b * (x6[4] * y6[1] + y6[4] * x6[1]);
      Data[44] += a * x6[5] * x6[1] + c * y6[5] * y6[1] + b * (x6[5] * y6[1] + y6[5] * x6[1]);


      Data[45] += a * x6[2] * x6[2] + c * y6[2] * y6[2] + b * (x6[2] * y6[2] + y6[2] * x6[2]);
      Data[46] += a * x6[3] * x6[2] + c * y6[3] * y6[2] + b * (x6[3] * y6[2] + y6[3] * x6[2]);
      Data[47] += a * x6[4] * x6[2] + c * y6[4] * y6[2] + b * (x6[4] * y6[2] + y6[4] * x6[2]);
      Data[48] += a * x6[5] * x6[2] + c * y6[5] * y6[2] + b * (x6[5] * y6[2] + y6[5] * x6[2]);


      Data[49] += a * x6[3] * x6[3] + c * y6[3] * y6[3] + b * (x6[3] * y6[3] + y6[3] * x6[3]);
      Data[50] += a * x6[4] * x6[3] + c * y6[4] * y6[3] + b * (x6[4] * y6[3] + y6[4] * x6[3]);
      Data[51] += a * x6[5] * x6[3] + c * y6[5] * y6[3] + b * (x6[5] * y6[3] + y6[5] * x6[3]);


      Data[52] += a * x6[4] * x6[4] + c * y6[4] * y6[4] + b * (x6[4] * y6[4] + y6[4] * x6[4]);
      Data[53] += a * x6[5] * x6[4] + c * y6[5] * y6[4] + b * (x6[5] * y6[4] + y6[5] * x6[4]);

      Data[54] += a * x6[5] * x6[5] + c * y6[5] * y6[5] + b * (x6[5] * y6[5] + y6[5] * x6[5]);


      num++;
      numIn1++;
      shiftUp(false);
    }


    inline void updateTopRight(
        const float *const x4,
        const float *const x6,
        const float *const y4,
        const float *const y6,
        const float TR00, const float TR10,
        const float TR01, const float TR11,
        const float TR02, const float TR12) {
      // acc[tid][htIDX].updateTopRight(
      //   rJ->Jpdc[0].data(), rJ->Jpdxi[0].data(),
      //   rJ->Jpdc[1].data(), rJ->Jpdxi[1].data(),
      //   rJ->JabJIdx(0,0), rJ->JabJIdx(0,1),
      //   rJ->JabJIdx(1,0), rJ->JabJIdx(1,1),
      //   JI_r[0], JI_r[1]);
      TopRight_Data[0] += x4[0] * TR00 + y4[0] * TR10;
      TopRight_Data[1] += x4[0] * TR01 + y4[0] * TR11;
      TopRight_Data[2] += x4[0] * TR02 + y4[0] * TR12;

      TopRight_Data[3] += x4[1] * TR00 + y4[1] * TR10;
      TopRight_Data[4] += x4[1] * TR01 + y4[1] * TR11;
      TopRight_Data[5] += x4[1] * TR02 + y4[1] * TR12;

      TopRight_Data[6] += x4[2] * TR00 + y4[2] * TR10;
      TopRight_Data[7] += x4[2] * TR01 + y4[2] * TR11;
      TopRight_Data[8] += x4[2] * TR02 + y4[2] * TR12;

      TopRight_Data[9] += x4[3] * TR00 + y4[3] * TR10;
      TopRight_Data[10] += x4[3] * TR01 + y4[3] * TR11;
      TopRight_Data[11] += x4[3] * TR02 + y4[3] * TR12;

      TopRight_Data[12] += x6[0] * TR00 + y6[0] * TR10;
      TopRight_Data[13] += x6[0] * TR01 + y6[0] * TR11;
      TopRight_Data[14] += x6[0] * TR02 + y6[0] * TR12;

      TopRight_Data[15] += x6[1] * TR00 + y6[1] * TR10;
      TopRight_Data[16] += x6[1] * TR01 + y6[1] * TR11;
      TopRight_Data[17] += x6[1] * TR02 + y6[1] * TR12;

      TopRight_Data[18] += x6[2] * TR00 + y6[2] * TR10;
      TopRight_Data[19] += x6[2] * TR01 + y6[2] * TR11;
      TopRight_Data[20] += x6[2] * TR02 + y6[2] * TR12;

      TopRight_Data[21] += x6[3] * TR00 + y6[3] * TR10;
      TopRight_Data[22] += x6[3] * TR01 + y6[3] * TR11;
      TopRight_Data[23] += x6[3] * TR02 + y6[3] * TR12;

      TopRight_Data[24] += x6[4] * TR00 + y6[4] * TR10;
      TopRight_Data[25] += x6[4] * TR01 + y6[4] * TR11;
      TopRight_Data[26] += x6[4] * TR02 + y6[4] * TR12;

      TopRight_Data[27] += x6[5] * TR00 + y6[5] * TR10;
      TopRight_Data[28] += x6[5] * TR01 + y6[5] * TR11;
      TopRight_Data[29] += x6[5] * TR02 + y6[5] * TR12;

    }

    inline void updateBotRight(
        const float a00,
        const float a01,
        const float a02,
        const float a11,
        const float a12,
        const float a22) {
      // acc[tid][htIDX].updateBotRight(rJ->Jab2(0,0), rJ->Jab2(0,1), Jab_r[0],
      // rJ->Jab2(1,1), Jab_r[1],rr);
      // H(10,10) = BotRight_Data1m[0];
      // H(10,11) = H(11,10) = BotRight_Data1m[1];
      // H(10,12) = H(12,10) = BotRight_Data1m[2];
      // H(11,11) = BotRight_Data1m[3];
      // H(11,12) = H(12,11) = BotRight_Data1m[4];
      // H(12,12) = BotRight_Data1m[5];
      BotRight_Data[0] += a00;
      BotRight_Data[1] += a01;
      BotRight_Data[2] += a02;
      BotRight_Data[3] += a11;
      BotRight_Data[4] += a12;
      BotRight_Data[5] += a22;
    }


  private:
    EIGEN_ALIGN16 float Data[60];
    EIGEN_ALIGN16 float Data1k[60];
    EIGEN_ALIGN16 float Data1m[60];

    EIGEN_ALIGN16 float TopRight_Data[32];
    EIGEN_ALIGN16 float TopRight_Data1k[32];
    EIGEN_ALIGN16 float TopRight_Data1m[32];

    EIGEN_ALIGN16 float BotRight_Data[8];
    EIGEN_ALIGN16 float BotRight_Data1k[8];
    EIGEN_ALIGN16 float BotRight_Data1m[8];


    float numIn1, numIn1k, numIn1m;


    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        for (int i = 0; i < 60; i += 4)
          _mm_store_ps(Data1k + i, _mm_add_ps(_mm_load_ps(Data + i), _mm_load_ps(Data1k + i)));
        for (int i = 0; i < 32; i += 4)
          _mm_store_ps(TopRight_Data1k + i,
                       _mm_add_ps(_mm_load_ps(TopRight_Data + i), _mm_load_ps(TopRight_Data1k + i)));
        for (int i = 0; i < 8; i += 4)
          _mm_store_ps(BotRight_Data1k + i,
                       _mm_add_ps(_mm_load_ps(BotRight_Data + i), _mm_load_ps(BotRight_Data1k + i)));


        numIn1k += numIn1;
        numIn1 = 0;
        memset(Data, 0, sizeof(float) * 60);
        memset(TopRight_Data, 0, sizeof(float) * 32);
        memset(BotRight_Data, 0, sizeof(float) * 8);
      }

      if (numIn1k > 1000 || force) {
        for (int i = 0; i < 60; i += 4)
          _mm_store_ps(Data1m + i, _mm_add_ps(_mm_load_ps(Data1k + i), _mm_load_ps(Data1m + i)));
        for (int i = 0; i < 32; i += 4)
          _mm_store_ps(TopRight_Data1m + i,
                       _mm_add_ps(_mm_load_ps(TopRight_Data1k + i), _mm_load_ps(TopRight_Data1m + i)));
        for (int i = 0; i < 8; i += 4)
          _mm_store_ps(BotRight_Data1m + i,
                       _mm_add_ps(_mm_load_ps(BotRight_Data1k + i), _mm_load_ps(BotRight_Data1m + i)));

        numIn1m += numIn1k;
        numIn1k = 0;
        memset(Data1k, 0, sizeof(float) * 60);
        memset(TopRight_Data1k, 0, sizeof(float) * 32);
        memset(BotRight_Data1k, 0, sizeof(float) * 8);
      }
    }
  };


/*
 * computes the outer sum of 10x2 matrices, weighted with a 2x2 matrix:
 * 			H = [x y] * [a b; b c] * [x y]^T
 * (assuming x,y are column-vectors).
 * numerically robust to large sums.
 */
  class AccumulatorApprox1515 {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    Mat1515f H;
    size_t num;

    inline void initialize() {
      memset(Data, 0, sizeof(float) * 60);
      memset(Data1k, 0, sizeof(float) * 60);
      memset(Data1m, 0, sizeof(float) * 60);

      memset(TopRight_Data, 0, sizeof(float) * 52);
      memset(TopRight_Data1k, 0, sizeof(float) * 52);
      memset(TopRight_Data1m, 0, sizeof(float) * 52);

      memset(BotRight_Data, 0, sizeof(float) * 16);
      memset(BotRight_Data1k, 0, sizeof(float) * 16);
      memset(BotRight_Data1m, 0, sizeof(float) * 16);
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      H.setZero();
      shiftUp(true);
      assert(numIn1 == 0);
      assert(numIn1k == 0);

      int idx = 0;
      for (int r = 0; r < 10; r++)
        for (int c = r; c < 10; c++) {
          H(r, c) = H(c, r) = Data1m[idx];
          idx++;
        }

      idx = 0;
      for (int r = 0; r < 10; r++)
        for (int c = 0; c < 5; c++) {
          H(r, c + 10) = H(c + 10, r) = TopRight_Data1m[idx];
          idx++;
        }

      H(10, 10) = BotRight_Data1m[0];
      H(10, 11) = H(11, 10) = BotRight_Data1m[1];
      H(10, 12) = H(12, 10) = BotRight_Data1m[2];
      H(10, 13) = H(13, 10) = BotRight_Data1m[3];
      H(10, 14) = H(14, 10) = BotRight_Data1m[4];
      H(11, 11) = BotRight_Data1m[5];
      H(11, 12) = H(12, 11) = BotRight_Data1m[6];
      H(11, 13) = H(13, 11) = BotRight_Data1m[7];
      H(11, 14) = H(14, 11) = BotRight_Data1m[8];
      H(12, 12) = BotRight_Data1m[9];
      H(12, 13) = H(13, 12) = BotRight_Data1m[10];
      H(12, 14) = H(14, 12) = BotRight_Data1m[11];
      H(13, 13) = BotRight_Data1m[12];
      H(13, 14) = H(14, 13) = BotRight_Data1m[13];
      H(14, 14) = BotRight_Data1m[14];

      num = numIn1 + numIn1k + numIn1m;
    }


    inline void updateSSE(
        const float *const x,
        const float *const y,
        const float a,
        const float b,
        const float c) {

      Data[0] += a * x[0] * x[0] + c * y[0] * y[0] + b * (x[0] * y[0] + y[0] * x[0]);
      Data[1] += a * x[1] * x[0] + c * y[1] * y[0] + b * (x[1] * y[0] + y[1] * x[0]);
      Data[2] += a * x[2] * x[0] + c * y[2] * y[0] + b * (x[2] * y[0] + y[2] * x[0]);
      Data[3] += a * x[3] * x[0] + c * y[3] * y[0] + b * (x[3] * y[0] + y[3] * x[0]);
      Data[4] += a * x[4] * x[0] + c * y[4] * y[0] + b * (x[4] * y[0] + y[4] * x[0]);
      Data[5] += a * x[5] * x[0] + c * y[5] * y[0] + b * (x[5] * y[0] + y[5] * x[0]);
      Data[6] += a * x[6] * x[0] + c * y[6] * y[0] + b * (x[6] * y[0] + y[6] * x[0]);
      Data[7] += a * x[7] * x[0] + c * y[7] * y[0] + b * (x[7] * y[0] + y[7] * x[0]);
      Data[8] += a * x[8] * x[0] + c * y[8] * y[0] + b * (x[8] * y[0] + y[8] * x[0]);
      Data[9] += a * x[9] * x[0] + c * y[9] * y[0] + b * (x[9] * y[0] + y[9] * x[0]);


      Data[10] += a * x[1] * x[1] + c * y[1] * y[1] + b * (x[1] * y[1] + y[1] * x[1]);
      Data[11] += a * x[2] * x[1] + c * y[2] * y[1] + b * (x[2] * y[1] + y[2] * x[1]);
      Data[12] += a * x[3] * x[1] + c * y[3] * y[1] + b * (x[3] * y[1] + y[3] * x[1]);
      Data[13] += a * x[4] * x[1] + c * y[4] * y[1] + b * (x[4] * y[1] + y[4] * x[1]);
      Data[14] += a * x[5] * x[1] + c * y[5] * y[1] + b * (x[5] * y[1] + y[5] * x[1]);
      Data[15] += a * x[6] * x[1] + c * y[6] * y[1] + b * (x[6] * y[1] + y[6] * x[1]);
      Data[16] += a * x[7] * x[1] + c * y[7] * y[1] + b * (x[7] * y[1] + y[7] * x[1]);
      Data[17] += a * x[8] * x[1] + c * y[8] * y[1] + b * (x[8] * y[1] + y[8] * x[1]);
      Data[18] += a * x[9] * x[1] + c * y[9] * y[1] + b * (x[9] * y[1] + y[9] * x[1]);


      Data[19] += a * x[2] * x[2] + c * y[2] * y[2] + b * (x[2] * y[2] + y[2] * x[2]);
      Data[20] += a * x[3] * x[2] + c * y[3] * y[2] + b * (x[3] * y[2] + y[3] * x[2]);
      Data[21] += a * x[4] * x[2] + c * y[4] * y[2] + b * (x[4] * y[2] + y[4] * x[2]);
      Data[22] += a * x[5] * x[2] + c * y[5] * y[2] + b * (x[5] * y[2] + y[5] * x[2]);
      Data[23] += a * x[6] * x[2] + c * y[6] * y[2] + b * (x[6] * y[2] + y[6] * x[2]);
      Data[24] += a * x[7] * x[2] + c * y[7] * y[2] + b * (x[7] * y[2] + y[7] * x[2]);
      Data[25] += a * x[8] * x[2] + c * y[8] * y[2] + b * (x[8] * y[2] + y[8] * x[2]);
      Data[26] += a * x[9] * x[2] + c * y[9] * y[2] + b * (x[9] * y[2] + y[9] * x[2]);


      Data[27] += a * x[3] * x[3] + c * y[3] * y[3] + b * (x[3] * y[3] + y[3] * x[3]);
      Data[28] += a * x[4] * x[3] + c * y[4] * y[3] + b * (x[4] * y[3] + y[4] * x[3]);
      Data[29] += a * x[5] * x[3] + c * y[5] * y[3] + b * (x[5] * y[3] + y[5] * x[3]);
      Data[30] += a * x[6] * x[3] + c * y[6] * y[3] + b * (x[6] * y[3] + y[6] * x[3]);
      Data[31] += a * x[7] * x[3] + c * y[7] * y[3] + b * (x[7] * y[3] + y[7] * x[3]);
      Data[32] += a * x[8] * x[3] + c * y[8] * y[3] + b * (x[8] * y[3] + y[8] * x[3]);
      Data[33] += a * x[9] * x[3] + c * y[9] * y[3] + b * (x[9] * y[3] + y[9] * x[3]);


      Data[34] += a * x[4] * x[4] + c * y[4] * y[4] + b * (x[4] * y[4] + y[4] * x[4]);
      Data[35] += a * x[5] * x[4] + c * y[5] * y[4] + b * (x[5] * y[4] + y[5] * x[4]);
      Data[36] += a * x[6] * x[4] + c * y[6] * y[4] + b * (x[6] * y[4] + y[6] * x[4]);
      Data[37] += a * x[7] * x[4] + c * y[7] * y[4] + b * (x[7] * y[4] + y[7] * x[4]);
      Data[38] += a * x[8] * x[4] + c * y[8] * y[4] + b * (x[8] * y[4] + y[8] * x[4]);
      Data[39] += a * x[9] * x[4] + c * y[9] * y[4] + b * (x[9] * y[4] + y[9] * x[4]);


      Data[40] += a * x[5] * x[5] + c * y[5] * y[5] + b * (x[5] * y[5] + y[5] * x[5]);
      Data[41] += a * x[6] * x[5] + c * y[6] * y[5] + b * (x[6] * y[5] + y[6] * x[5]);
      Data[42] += a * x[7] * x[5] + c * y[7] * y[5] + b * (x[7] * y[5] + y[7] * x[5]);
      Data[43] += a * x[8] * x[5] + c * y[8] * y[5] + b * (x[8] * y[5] + y[8] * x[5]);
      Data[44] += a * x[9] * x[5] + c * y[9] * y[5] + b * (x[9] * y[5] + y[9] * x[5]);


      Data[45] += a * x[6] * x[6] + c * y[6] * y[6] + b * (x[6] * y[6] + y[6] * x[6]);
      Data[46] += a * x[7] * x[6] + c * y[7] * y[6] + b * (x[7] * y[6] + y[7] * x[6]);
      Data[47] += a * x[8] * x[6] + c * y[8] * y[6] + b * (x[8] * y[6] + y[8] * x[6]);
      Data[48] += a * x[9] * x[6] + c * y[9] * y[6] + b * (x[9] * y[6] + y[9] * x[6]);


      Data[49] += a * x[7] * x[7] + c * y[7] * y[7] + b * (x[7] * y[7] + y[7] * x[7]);
      Data[50] += a * x[8] * x[7] + c * y[8] * y[7] + b * (x[8] * y[7] + y[8] * x[7]);
      Data[51] += a * x[9] * x[7] + c * y[9] * y[7] + b * (x[9] * y[7] + y[9] * x[7]);


      Data[52] += a * x[8] * x[8] + c * y[8] * y[8] + b * (x[8] * y[8] + y[8] * x[8]);
      Data[53] += a * x[9] * x[8] + c * y[9] * y[8] + b * (x[9] * y[8] + y[9] * x[8]);

      Data[54] += a * x[9] * x[9] + c * y[9] * y[9] + b * (x[9] * y[9] + y[9] * x[9]);


      num++;
      numIn1++;
      shiftUp(false);
    }


/*
 * same as other method, just that x/y are composed of two parts, the first 4 elements are in x4/y4, the last 6 in x6/y6.
 */
    inline void update(
        const float *const x4,
        const float *const x6,
        const float *const y4,
        const float *const y6,
        const float a,
        const float b,
        const float c) {

      Data[0] += a * x4[0] * x4[0] + c * y4[0] * y4[0] + b * (x4[0] * y4[0] + y4[0] * x4[0]);
      Data[1] += a * x4[1] * x4[0] + c * y4[1] * y4[0] + b * (x4[1] * y4[0] + y4[1] * x4[0]);
      Data[2] += a * x4[2] * x4[0] + c * y4[2] * y4[0] + b * (x4[2] * y4[0] + y4[2] * x4[0]);
      Data[3] += a * x4[3] * x4[0] + c * y4[3] * y4[0] + b * (x4[3] * y4[0] + y4[3] * x4[0]);
      Data[4] += a * x6[0] * x4[0] + c * y6[0] * y4[0] + b * (x6[0] * y4[0] + y6[0] * x4[0]);
      Data[5] += a * x6[1] * x4[0] + c * y6[1] * y4[0] + b * (x6[1] * y4[0] + y6[1] * x4[0]);
      Data[6] += a * x6[2] * x4[0] + c * y6[2] * y4[0] + b * (x6[2] * y4[0] + y6[2] * x4[0]);
      Data[7] += a * x6[3] * x4[0] + c * y6[3] * y4[0] + b * (x6[3] * y4[0] + y6[3] * x4[0]);
      Data[8] += a * x6[4] * x4[0] + c * y6[4] * y4[0] + b * (x6[4] * y4[0] + y6[4] * x4[0]);
      Data[9] += a * x6[5] * x4[0] + c * y6[5] * y4[0] + b * (x6[5] * y4[0] + y6[5] * x4[0]);


      Data[10] += a * x4[1] * x4[1] + c * y4[1] * y4[1] + b * (x4[1] * y4[1] + y4[1] * x4[1]);
      Data[11] += a * x4[2] * x4[1] + c * y4[2] * y4[1] + b * (x4[2] * y4[1] + y4[2] * x4[1]);
      Data[12] += a * x4[3] * x4[1] + c * y4[3] * y4[1] + b * (x4[3] * y4[1] + y4[3] * x4[1]);
      Data[13] += a * x6[0] * x4[1] + c * y6[0] * y4[1] + b * (x6[0] * y4[1] + y6[0] * x4[1]);
      Data[14] += a * x6[1] * x4[1] + c * y6[1] * y4[1] + b * (x6[1] * y4[1] + y6[1] * x4[1]);
      Data[15] += a * x6[2] * x4[1] + c * y6[2] * y4[1] + b * (x6[2] * y4[1] + y6[2] * x4[1]);
      Data[16] += a * x6[3] * x4[1] + c * y6[3] * y4[1] + b * (x6[3] * y4[1] + y6[3] * x4[1]);
      Data[17] += a * x6[4] * x4[1] + c * y6[4] * y4[1] + b * (x6[4] * y4[1] + y6[4] * x4[1]);
      Data[18] += a * x6[5] * x4[1] + c * y6[5] * y4[1] + b * (x6[5] * y4[1] + y6[5] * x4[1]);


      Data[19] += a * x4[2] * x4[2] + c * y4[2] * y4[2] + b * (x4[2] * y4[2] + y4[2] * x4[2]);
      Data[20] += a * x4[3] * x4[2] + c * y4[3] * y4[2] + b * (x4[3] * y4[2] + y4[3] * x4[2]);
      Data[21] += a * x6[0] * x4[2] + c * y6[0] * y4[2] + b * (x6[0] * y4[2] + y6[0] * x4[2]);
      Data[22] += a * x6[1] * x4[2] + c * y6[1] * y4[2] + b * (x6[1] * y4[2] + y6[1] * x4[2]);
      Data[23] += a * x6[2] * x4[2] + c * y6[2] * y4[2] + b * (x6[2] * y4[2] + y6[2] * x4[2]);
      Data[24] += a * x6[3] * x4[2] + c * y6[3] * y4[2] + b * (x6[3] * y4[2] + y6[3] * x4[2]);
      Data[25] += a * x6[4] * x4[2] + c * y6[4] * y4[2] + b * (x6[4] * y4[2] + y6[4] * x4[2]);
      Data[26] += a * x6[5] * x4[2] + c * y6[5] * y4[2] + b * (x6[5] * y4[2] + y6[5] * x4[2]);


      Data[27] += a * x4[3] * x4[3] + c * y4[3] * y4[3] + b * (x4[3] * y4[3] + y4[3] * x4[3]);
      Data[28] += a * x6[0] * x4[3] + c * y6[0] * y4[3] + b * (x6[0] * y4[3] + y6[0] * x4[3]);
      Data[29] += a * x6[1] * x4[3] + c * y6[1] * y4[3] + b * (x6[1] * y4[3] + y6[1] * x4[3]);
      Data[30] += a * x6[2] * x4[3] + c * y6[2] * y4[3] + b * (x6[2] * y4[3] + y6[2] * x4[3]);
      Data[31] += a * x6[3] * x4[3] + c * y6[3] * y4[3] + b * (x6[3] * y4[3] + y6[3] * x4[3]);
      Data[32] += a * x6[4] * x4[3] + c * y6[4] * y4[3] + b * (x6[4] * y4[3] + y6[4] * x4[3]);
      Data[33] += a * x6[5] * x4[3] + c * y6[5] * y4[3] + b * (x6[5] * y4[3] + y6[5] * x4[3]);


      Data[34] += a * x6[0] * x6[0] + c * y6[0] * y6[0] + b * (x6[0] * y6[0] + y6[0] * x6[0]);
      Data[35] += a * x6[1] * x6[0] + c * y6[1] * y6[0] + b * (x6[1] * y6[0] + y6[1] * x6[0]);
      Data[36] += a * x6[2] * x6[0] + c * y6[2] * y6[0] + b * (x6[2] * y6[0] + y6[2] * x6[0]);
      Data[37] += a * x6[3] * x6[0] + c * y6[3] * y6[0] + b * (x6[3] * y6[0] + y6[3] * x6[0]);
      Data[38] += a * x6[4] * x6[0] + c * y6[4] * y6[0] + b * (x6[4] * y6[0] + y6[4] * x6[0]);
      Data[39] += a * x6[5] * x6[0] + c * y6[5] * y6[0] + b * (x6[5] * y6[0] + y6[5] * x6[0]);


      Data[40] += a * x6[1] * x6[1] + c * y6[1] * y6[1] + b * (x6[1] * y6[1] + y6[1] * x6[1]);
      Data[41] += a * x6[2] * x6[1] + c * y6[2] * y6[1] + b * (x6[2] * y6[1] + y6[2] * x6[1]);
      Data[42] += a * x6[3] * x6[1] + c * y6[3] * y6[1] + b * (x6[3] * y6[1] + y6[3] * x6[1]);
      Data[43] += a * x6[4] * x6[1] + c * y6[4] * y6[1] + b * (x6[4] * y6[1] + y6[4] * x6[1]);
      Data[44] += a * x6[5] * x6[1] + c * y6[5] * y6[1] + b * (x6[5] * y6[1] + y6[5] * x6[1]);


      Data[45] += a * x6[2] * x6[2] + c * y6[2] * y6[2] + b * (x6[2] * y6[2] + y6[2] * x6[2]);
      Data[46] += a * x6[3] * x6[2] + c * y6[3] * y6[2] + b * (x6[3] * y6[2] + y6[3] * x6[2]);
      Data[47] += a * x6[4] * x6[2] + c * y6[4] * y6[2] + b * (x6[4] * y6[2] + y6[4] * x6[2]);
      Data[48] += a * x6[5] * x6[2] + c * y6[5] * y6[2] + b * (x6[5] * y6[2] + y6[5] * x6[2]);


      Data[49] += a * x6[3] * x6[3] + c * y6[3] * y6[3] + b * (x6[3] * y6[3] + y6[3] * x6[3]);
      Data[50] += a * x6[4] * x6[3] + c * y6[4] * y6[3] + b * (x6[4] * y6[3] + y6[4] * x6[3]);
      Data[51] += a * x6[5] * x6[3] + c * y6[5] * y6[3] + b * (x6[5] * y6[3] + y6[5] * x6[3]);


      Data[52] += a * x6[4] * x6[4] + c * y6[4] * y6[4] + b * (x6[4] * y6[4] + y6[4] * x6[4]);
      Data[53] += a * x6[5] * x6[4] + c * y6[5] * y6[4] + b * (x6[5] * y6[4] + y6[5] * x6[4]);

      Data[54] += a * x6[5] * x6[5] + c * y6[5] * y6[5] + b * (x6[5] * y6[5] + y6[5] * x6[5]);


      num++;
      numIn1++;
      shiftUp(false);
    }


    inline void updateTopRight(
        const float *const x4,
        const float *const x6,
        const float *const y4,
        const float *const y6,
        const float TR00, const float TR10,
        const float TR01, const float TR11,
        const float TR02, const float TR12,
        const float TR03, const float TR13,
        const float TR04, const float TR14) {
      // acc[tid][htIDX].updateTopRight(
      //   rJ->Jpdc[0].data(), rJ->Jpdxi[0].data(),
      //   rJ->Jpdc[1].data(), rJ->Jpdxi[1].data(),
      //   rJ->JabJIdx(0,0), rJ->JabJIdx(0,1),
      //   rJ->JabJIdx(1,0), rJ->JabJIdx(1,1),
      //   rJ->JabJIdx(2,0), rJ->JabJIdx(2,1),
      //   rJ->JabJIdx(3,0), rJ->JabJIdx(3,1),
      //   JI_r[0], JI_r[1]);
      TopRight_Data[0] += x4[0] * TR00 + y4[0] * TR10;
      TopRight_Data[1] += x4[0] * TR01 + y4[0] * TR11;
      TopRight_Data[2] += x4[0] * TR02 + y4[0] * TR12;
      TopRight_Data[3] += x4[0] * TR03 + y4[0] * TR13;
      TopRight_Data[4] += x4[0] * TR04 + y4[0] * TR14;

      TopRight_Data[5] += x4[1] * TR00 + y4[1] * TR10;
      TopRight_Data[6] += x4[1] * TR01 + y4[1] * TR11;
      TopRight_Data[7] += x4[1] * TR02 + y4[1] * TR12;
      TopRight_Data[8] += x4[1] * TR03 + y4[1] * TR13;
      TopRight_Data[9] += x4[1] * TR04 + y4[1] * TR14;

      TopRight_Data[10] += x4[2] * TR00 + y4[2] * TR10;
      TopRight_Data[11] += x4[2] * TR01 + y4[2] * TR11;
      TopRight_Data[12] += x4[2] * TR02 + y4[2] * TR12;
      TopRight_Data[13] += x4[2] * TR03 + y4[2] * TR13;
      TopRight_Data[14] += x4[2] * TR04 + y4[2] * TR14;

      TopRight_Data[15] += x4[3] * TR00 + y4[3] * TR10;
      TopRight_Data[16] += x4[3] * TR01 + y4[3] * TR11;
      TopRight_Data[17] += x4[3] * TR02 + y4[3] * TR12;
      TopRight_Data[18] += x4[3] * TR03 + y4[3] * TR13;
      TopRight_Data[19] += x4[3] * TR04 + y4[3] * TR14;

      TopRight_Data[20] += x6[0] * TR00 + y6[0] * TR10;
      TopRight_Data[21] += x6[0] * TR01 + y6[0] * TR11;
      TopRight_Data[22] += x6[0] * TR02 + y6[0] * TR12;
      TopRight_Data[23] += x6[0] * TR03 + y6[0] * TR13;
      TopRight_Data[24] += x6[0] * TR04 + y6[0] * TR14;

      TopRight_Data[25] += x6[1] * TR00 + y6[1] * TR10;
      TopRight_Data[26] += x6[1] * TR01 + y6[1] * TR11;
      TopRight_Data[27] += x6[1] * TR02 + y6[1] * TR12;
      TopRight_Data[28] += x6[1] * TR03 + y6[1] * TR13;
      TopRight_Data[29] += x6[1] * TR04 + y6[1] * TR14;

      TopRight_Data[30] += x6[2] * TR00 + y6[2] * TR10;
      TopRight_Data[31] += x6[2] * TR01 + y6[2] * TR11;
      TopRight_Data[32] += x6[2] * TR02 + y6[2] * TR12;
      TopRight_Data[33] += x6[2] * TR03 + y6[2] * TR13;
      TopRight_Data[34] += x6[2] * TR04 + y6[2] * TR14;

      TopRight_Data[35] += x6[3] * TR00 + y6[3] * TR10;
      TopRight_Data[36] += x6[3] * TR01 + y6[3] * TR11;
      TopRight_Data[37] += x6[3] * TR02 + y6[3] * TR12;
      TopRight_Data[38] += x6[3] * TR03 + y6[3] * TR13;
      TopRight_Data[39] += x6[3] * TR04 + y6[3] * TR14;

      TopRight_Data[40] += x6[4] * TR00 + y6[4] * TR10;
      TopRight_Data[41] += x6[4] * TR01 + y6[4] * TR11;
      TopRight_Data[42] += x6[4] * TR02 + y6[4] * TR12;
      TopRight_Data[43] += x6[4] * TR03 + y6[4] * TR13;
      TopRight_Data[44] += x6[4] * TR04 + y6[4] * TR14;

      TopRight_Data[45] += x6[5] * TR00 + y6[5] * TR10;
      TopRight_Data[46] += x6[5] * TR01 + y6[5] * TR11;
      TopRight_Data[47] += x6[5] * TR02 + y6[5] * TR12;
      TopRight_Data[48] += x6[5] * TR03 + y6[5] * TR13;
      TopRight_Data[49] += x6[5] * TR04 + y6[5] * TR14;
    }

    inline void updateBotRight(
        const float a00, const float a01, const float a02, const float a03, const float a04,
        const float a11, const float a12, const float a13, const float a14,
        const float a22, const float a23, const float a24,
        const float a33, const float a34,
        const float a44) {
      // acc[tid][htIDX].updateBotRight(rJ->Jab2(0,0), rJ->Jab2(0,1), rJ->Jab2(0,2), rJ->Jab2(0,3), Jab_r[0],
      // rJ->Jab2(1,1), rJ->Jab2(1,2), rJ->Jab2(1,3), Jab_r[1],
      // rJ->Jab2(2,2), rJ->Jab2(2,3), Jab_r[2],
      // rJ->Jab2(3,3), Jab_r[3],
      // rr);
      BotRight_Data[0] += a00;
      BotRight_Data[1] += a01;
      BotRight_Data[2] += a02;
      BotRight_Data[3] += a03;
      BotRight_Data[4] += a04;
      BotRight_Data[5] += a11;
      BotRight_Data[6] += a12;
      BotRight_Data[7] += a13;
      BotRight_Data[8] += a14;
      BotRight_Data[9] += a22;
      BotRight_Data[10] += a23;
      BotRight_Data[11] += a24;
      BotRight_Data[12] += a33;
      BotRight_Data[13] += a34;
      BotRight_Data[14] += a44;
    }


  private:
    EIGEN_ALIGN16 float Data[60];
    EIGEN_ALIGN16 float Data1k[60];
    EIGEN_ALIGN16 float Data1m[60];

    EIGEN_ALIGN16 float TopRight_Data[52];
    EIGEN_ALIGN16 float TopRight_Data1k[52];
    EIGEN_ALIGN16 float TopRight_Data1m[52];

    EIGEN_ALIGN16 float BotRight_Data[16];
    EIGEN_ALIGN16 float BotRight_Data1k[16];
    EIGEN_ALIGN16 float BotRight_Data1m[16];


    float numIn1, numIn1k, numIn1m;


    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        for (int i = 0; i < 60; i += 4)
          _mm_store_ps(Data1k + i, _mm_add_ps(_mm_load_ps(Data + i), _mm_load_ps(Data1k + i)));
        for (int i = 0; i < 52; i += 4)
          _mm_store_ps(TopRight_Data1k + i,
                       _mm_add_ps(_mm_load_ps(TopRight_Data + i), _mm_load_ps(TopRight_Data1k + i)));
        for (int i = 0; i < 16; i += 4)
          _mm_store_ps(BotRight_Data1k + i,
                       _mm_add_ps(_mm_load_ps(BotRight_Data + i), _mm_load_ps(BotRight_Data1k + i)));


        numIn1k += numIn1;
        numIn1 = 0;
        memset(Data, 0, sizeof(float) * 60);
        memset(TopRight_Data, 0, sizeof(float) * 52);
        memset(BotRight_Data, 0, sizeof(float) * 16);
      }

      if (numIn1k > 1000 || force) {
        for (int i = 0; i < 60; i += 4)
          _mm_store_ps(Data1m + i, _mm_add_ps(_mm_load_ps(Data1k + i), _mm_load_ps(Data1m + i)));
        for (int i = 0; i < 52; i += 4)
          _mm_store_ps(TopRight_Data1m + i,
                       _mm_add_ps(_mm_load_ps(TopRight_Data1k + i), _mm_load_ps(TopRight_Data1m + i)));
        for (int i = 0; i < 16; i += 4)
          _mm_store_ps(BotRight_Data1m + i,
                       _mm_add_ps(_mm_load_ps(BotRight_Data1k + i), _mm_load_ps(BotRight_Data1m + i)));

        numIn1m += numIn1k;
        numIn1k = 0;
        memset(Data1k, 0, sizeof(float) * 60);
        memset(TopRight_Data1k, 0, sizeof(float) * 52);
        memset(BotRight_Data1k, 0, sizeof(float) * 16);
      }
    }
  };

  class Accumulator9 {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    Mat99f H;
    Vec9f b;
    size_t num;

    inline void initialize() {
      H.setZero();
      b.setZero();
      memset(SSEData, 0, sizeof(float) * 4 * 45);
      memset(SSEData1k, 0, sizeof(float) * 4 * 45);
      memset(SSEData1m, 0, sizeof(float) * 4 * 45);
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      H.setZero();
      shiftUp(true);
      assert(numIn1 == 0);
      assert(numIn1k == 0);

      int idx = 0;
      for (int r = 0; r < 9; r++)
        for (int c = r; c < 9; c++) {
          float d = SSEData1m[idx + 0] + SSEData1m[idx + 1] + SSEData1m[idx + 2] + SSEData1m[idx + 3];
          H(r, c) = H(c, r) = d;
          idx += 4;
        }
      assert(idx == 4 * 45);
    }


    inline void updateSSE(
        const __m128 J0, const __m128 J1,
        const __m128 J2, const __m128 J3,
        const __m128 J4, const __m128 J5,
        const __m128 J6, const __m128 J7,
        const __m128 J8) {
      float *pt = SSEData;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J0)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J8)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J8)));
      pt += 4;

      num += 4;
      numIn1++;
      shiftUp(false);
    }


    inline void updateSSE_eighted(
        const __m128 J0, const __m128 J1,
        const __m128 J2, const __m128 J3,
        const __m128 J4, const __m128 J5,
        const __m128 J6, const __m128 J7,
        const __m128 J8, const __m128 w) {
      float *pt = SSEData;

      __m128 J0w = _mm_mul_ps(J0, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J0)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J8)));
      pt += 4;

      __m128 J1w = _mm_mul_ps(J1, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J8)));
      pt += 4;

      __m128 J2w = _mm_mul_ps(J2, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J8)));
      pt += 4;

      __m128 J3w = _mm_mul_ps(J3, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J8)));
      pt += 4;

      __m128 J4w = _mm_mul_ps(J4, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J8)));
      pt += 4;

      __m128 J5w = _mm_mul_ps(J5, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J8)));
      pt += 4;

      __m128 J6w = _mm_mul_ps(J6, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J8)));
      pt += 4;

      __m128 J7w = _mm_mul_ps(J7, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7w, J8)));
      pt += 4;

      __m128 J8w = _mm_mul_ps(J8, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8w, J8)));
      pt += 4;

      num += 4;
      numIn1++;
      shiftUp(false);
    }


    inline void updateSingle(
        const float J0, const float J1,
        const float J2, const float J3,
        const float J4, const float J5,
        const float J6, const float J7,
        const float J8, int off = 0) {
      float *pt = SSEData + off;
      *pt += J0 * J0;
      pt += 4;
      *pt += J1 * J0;
      pt += 4;
      *pt += J2 * J0;
      pt += 4;
      *pt += J3 * J0;
      pt += 4;
      *pt += J4 * J0;
      pt += 4;
      *pt += J5 * J0;
      pt += 4;
      *pt += J6 * J0;
      pt += 4;
      *pt += J7 * J0;
      pt += 4;
      *pt += J8 * J0;
      pt += 4;


      *pt += J1 * J1;
      pt += 4;
      *pt += J2 * J1;
      pt += 4;
      *pt += J3 * J1;
      pt += 4;
      *pt += J4 * J1;
      pt += 4;
      *pt += J5 * J1;
      pt += 4;
      *pt += J6 * J1;
      pt += 4;
      *pt += J7 * J1;
      pt += 4;
      *pt += J8 * J1;
      pt += 4;


      *pt += J2 * J2;
      pt += 4;
      *pt += J3 * J2;
      pt += 4;
      *pt += J4 * J2;
      pt += 4;
      *pt += J5 * J2;
      pt += 4;
      *pt += J6 * J2;
      pt += 4;
      *pt += J7 * J2;
      pt += 4;
      *pt += J8 * J2;
      pt += 4;


      *pt += J3 * J3;
      pt += 4;
      *pt += J4 * J3;
      pt += 4;
      *pt += J5 * J3;
      pt += 4;
      *pt += J6 * J3;
      pt += 4;
      *pt += J7 * J3;
      pt += 4;
      *pt += J8 * J3;
      pt += 4;


      *pt += J4 * J4;
      pt += 4;
      *pt += J5 * J4;
      pt += 4;
      *pt += J6 * J4;
      pt += 4;
      *pt += J7 * J4;
      pt += 4;
      *pt += J8 * J4;
      pt += 4;

      *pt += J5 * J5;
      pt += 4;
      *pt += J6 * J5;
      pt += 4;
      *pt += J7 * J5;
      pt += 4;
      *pt += J8 * J5;
      pt += 4;


      *pt += J6 * J6;
      pt += 4;
      *pt += J7 * J6;
      pt += 4;
      *pt += J8 * J6;
      pt += 4;


      *pt += J7 * J7;
      pt += 4;
      *pt += J8 * J7;
      pt += 4;

      *pt += J8 * J8;
      pt += 4;

      num++;
      numIn1++;
      shiftUp(false);
    }

    inline void updateSingleWeighted(
        float J0, float J1,
        float J2, float J3,
        float J4, float J5,
        float J6, float J7,
        float J8, float w,
        int off = 0) {

      float *pt = SSEData + off;
      *pt += J0 * J0 * w;
      pt += 4;
      J0 *= w;
      *pt += J1 * J0;
      pt += 4;
      *pt += J2 * J0;
      pt += 4;
      *pt += J3 * J0;
      pt += 4;
      *pt += J4 * J0;
      pt += 4;
      *pt += J5 * J0;
      pt += 4;
      *pt += J6 * J0;
      pt += 4;
      *pt += J7 * J0;
      pt += 4;
      *pt += J8 * J0;
      pt += 4;


      *pt += J1 * J1 * w;
      pt += 4;
      J1 *= w;
      *pt += J2 * J1;
      pt += 4;
      *pt += J3 * J1;
      pt += 4;
      *pt += J4 * J1;
      pt += 4;
      *pt += J5 * J1;
      pt += 4;
      *pt += J6 * J1;
      pt += 4;
      *pt += J7 * J1;
      pt += 4;
      *pt += J8 * J1;
      pt += 4;


      *pt += J2 * J2 * w;
      pt += 4;
      J2 *= w;
      *pt += J3 * J2;
      pt += 4;
      *pt += J4 * J2;
      pt += 4;
      *pt += J5 * J2;
      pt += 4;
      *pt += J6 * J2;
      pt += 4;
      *pt += J7 * J2;
      pt += 4;
      *pt += J8 * J2;
      pt += 4;


      *pt += J3 * J3 * w;
      pt += 4;
      J3 *= w;
      *pt += J4 * J3;
      pt += 4;
      *pt += J5 * J3;
      pt += 4;
      *pt += J6 * J3;
      pt += 4;
      *pt += J7 * J3;
      pt += 4;
      *pt += J8 * J3;
      pt += 4;


      *pt += J4 * J4 * w;
      pt += 4;
      J4 *= w;
      *pt += J5 * J4;
      pt += 4;
      *pt += J6 * J4;
      pt += 4;
      *pt += J7 * J4;
      pt += 4;
      *pt += J8 * J4;
      pt += 4;

      *pt += J5 * J5 * w;
      pt += 4;
      J5 *= w;
      *pt += J6 * J5;
      pt += 4;
      *pt += J7 * J5;
      pt += 4;
      *pt += J8 * J5;
      pt += 4;


      *pt += J6 * J6 * w;
      pt += 4;
      J6 *= w;
      *pt += J7 * J6;
      pt += 4;
      *pt += J8 * J6;
      pt += 4;


      *pt += J7 * J7 * w;
      pt += 4;
      J7 *= w;
      *pt += J8 * J7;
      pt += 4;

      *pt += J8 * J8 * w;
      pt += 4;

      num++;
      numIn1++;
      shiftUp(false);
    }


  private:
    EIGEN_ALIGN16 float SSEData[4 * 45];
    EIGEN_ALIGN16 float SSEData1k[4 * 45];
    EIGEN_ALIGN16 float SSEData1m[4 * 45];
    float numIn1, numIn1k, numIn1m;


    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        for (int i = 0; i < 45; i++)
          _mm_store_ps(SSEData1k + 4 * i, _mm_add_ps(_mm_load_ps(SSEData + 4 * i), _mm_load_ps(SSEData1k + 4 * i)));
        numIn1k += numIn1;
        numIn1 = 0;
        memset(SSEData, 0, sizeof(float) * 4 * 45);
      }

      if (numIn1k > 1000 || force) {
        for (int i = 0; i < 45; i++)
          _mm_store_ps(SSEData1m + 4 * i, _mm_add_ps(_mm_load_ps(SSEData1k + 4 * i), _mm_load_ps(SSEData1m + 4 * i)));
        numIn1m += numIn1k;
        numIn1k = 0;
        memset(SSEData1k, 0, sizeof(float) * 4 * 45);
      }
    }
  };


  class Accumulator11 {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    Mat1111f H;
    Vec11f b;
    size_t num;

    inline void initialize() {
      H.setZero();
      b.setZero();
      memset(SSEData, 0, sizeof(float) * 4 * 66);
      memset(SSEData1k, 0, sizeof(float) * 4 * 66);
      memset(SSEData1m, 0, sizeof(float) * 4 * 66);
      num = numIn1 = numIn1k = numIn1m = 0;
    }

    inline void finish() {
      H.setZero();
      shiftUp(true);
      assert(numIn1 == 0);
      assert(numIn1k == 0);

      int idx = 0;
      for (int r = 0; r < 11; r++)
        for (int c = r; c < 11; c++) {
          float d = SSEData1m[idx + 0] + SSEData1m[idx + 1] + SSEData1m[idx + 2] + SSEData1m[idx + 3];
          H(r, c) = H(c, r) = d;
          idx += 4;
        }
      assert(idx == 4 * 66);
    }


    inline void updateSSE(
        const __m128 J0, const __m128 J1,
        const __m128 J2, const __m128 J3,
        const __m128 J4, const __m128 J5,
        const __m128 J6, const __m128 J7,
        const __m128 J8, const __m128 J9,
        const __m128 J10) {
      float *pt = SSEData;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J0)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9, J10)));
      pt += 4;

      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J10, J10)));
      pt += 4;

      num += 4;
      numIn1++;
      shiftUp(false);
    }


    inline void updateSSE_tened(
        const __m128 J0, const __m128 J1,
        const __m128 J2, const __m128 J3,
        const __m128 J4, const __m128 J5,
        const __m128 J6, const __m128 J7,
        const __m128 J8, const __m128 J9,
        const __m128 J10, const __m128 w) {
      float *pt = SSEData;

      __m128 J0w = _mm_mul_ps(J0, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J0)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J0w, J10)));
      pt += 4;

      __m128 J1w = _mm_mul_ps(J1, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J1)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J1w, J10)));
      pt += 4;

      __m128 J2w = _mm_mul_ps(J2, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J2)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J2w, J10)));
      pt += 4;

      __m128 J3w = _mm_mul_ps(J3, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J3)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J3w, J10)));
      pt += 4;

      __m128 J4w = _mm_mul_ps(J4, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J4)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J4w, J10)));
      pt += 4;

      __m128 J5w = _mm_mul_ps(J5, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J5)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J5w, J10)));
      pt += 4;

      __m128 J6w = _mm_mul_ps(J6, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J6)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J6w, J10)));
      pt += 4;

      __m128 J7w = _mm_mul_ps(J7, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7w, J7)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J7w, J10)));
      pt += 4;

      __m128 J8w = _mm_mul_ps(J8, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8w, J8)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J8w, J10)));
      pt += 4;

      __m128 J9w = _mm_mul_ps(J9, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9w, J9)));
      pt += 4;
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J9w, J10)));
      pt += 4;

      __m128 J10w = _mm_mul_ps(J10, w);
      _mm_store_ps(pt, _mm_add_ps(_mm_load_ps(pt), _mm_mul_ps(J10w, J10)));
      pt += 4;

      num += 4;
      numIn1++;
      shiftUp(false);
    }


    inline void updateSingle(
        const float J0, const float J1,
        const float J2, const float J3,
        const float J4, const float J5,
        const float J6, const float J7,
        const float J8, const float J9,
        const float J10, int off = 0) {
      float *pt = SSEData + off;
      *pt += J0 * J0;
      pt += 4;
      *pt += J1 * J0;
      pt += 4;
      *pt += J2 * J0;
      pt += 4;
      *pt += J3 * J0;
      pt += 4;
      *pt += J4 * J0;
      pt += 4;
      *pt += J5 * J0;
      pt += 4;
      *pt += J6 * J0;
      pt += 4;
      *pt += J7 * J0;
      pt += 4;
      *pt += J8 * J0;
      pt += 4;
      *pt += J9 * J0;
      pt += 4;
      *pt += J10 * J0;
      pt += 4;


      *pt += J1 * J1;
      pt += 4;
      *pt += J2 * J1;
      pt += 4;
      *pt += J3 * J1;
      pt += 4;
      *pt += J4 * J1;
      pt += 4;
      *pt += J5 * J1;
      pt += 4;
      *pt += J6 * J1;
      pt += 4;
      *pt += J7 * J1;
      pt += 4;
      *pt += J8 * J1;
      pt += 4;
      *pt += J9 * J1;
      pt += 4;
      *pt += J10 * J1;
      pt += 4;


      *pt += J2 * J2;
      pt += 4;
      *pt += J3 * J2;
      pt += 4;
      *pt += J4 * J2;
      pt += 4;
      *pt += J5 * J2;
      pt += 4;
      *pt += J6 * J2;
      pt += 4;
      *pt += J7 * J2;
      pt += 4;
      *pt += J8 * J2;
      pt += 4;
      *pt += J9 * J2;
      pt += 4;
      *pt += J10 * J2;
      pt += 4;


      *pt += J3 * J3;
      pt += 4;
      *pt += J4 * J3;
      pt += 4;
      *pt += J5 * J3;
      pt += 4;
      *pt += J6 * J3;
      pt += 4;
      *pt += J7 * J3;
      pt += 4;
      *pt += J8 * J3;
      pt += 4;
      *pt += J9 * J3;
      pt += 4;
      *pt += J10 * J3;
      pt += 4;


      *pt += J4 * J4;
      pt += 4;
      *pt += J5 * J4;
      pt += 4;
      *pt += J6 * J4;
      pt += 4;
      *pt += J7 * J4;
      pt += 4;
      *pt += J8 * J4;
      pt += 4;
      *pt += J9 * J4;
      pt += 4;
      *pt += J10 * J4;
      pt += 4;

      *pt += J5 * J5;
      pt += 4;
      *pt += J6 * J5;
      pt += 4;
      *pt += J7 * J5;
      pt += 4;
      *pt += J8 * J5;
      pt += 4;
      *pt += J9 * J5;
      pt += 4;
      *pt += J10 * J5;
      pt += 4;


      *pt += J6 * J6;
      pt += 4;
      *pt += J7 * J6;
      pt += 4;
      *pt += J8 * J6;
      pt += 4;
      *pt += J9 * J6;
      pt += 4;
      *pt += J10 * J6;
      pt += 4;


      *pt += J7 * J7;
      pt += 4;
      *pt += J8 * J7;
      pt += 4;
      *pt += J9 * J7;
      pt += 4;
      *pt += J10 * J7;
      pt += 4;

      *pt += J8 * J8;
      pt += 4;
      *pt += J9 * J8;
      pt += 4;
      *pt += J10 * J8;
      pt += 4;

      *pt += J9 * J9;
      pt += 4;
      *pt += J10 * J9;
      pt += 4;

      *pt += J10 * J10;
      pt += 4;

      num++;
      numIn1++;
      shiftUp(false);
    }

    inline void updateSingleWeighted(
        float J0, float J1,
        float J2, float J3,
        float J4, float J5,
        float J6, float J7,
        float J8, float J9,
        float J10, float w,
        int off = 0) {

      float *pt = SSEData + off;
      *pt += J0 * J0 * w;
      pt += 4;
      J0 *= w;
      *pt += J1 * J0;
      pt += 4;
      *pt += J2 * J0;
      pt += 4;
      *pt += J3 * J0;
      pt += 4;
      *pt += J4 * J0;
      pt += 4;
      *pt += J5 * J0;
      pt += 4;
      *pt += J6 * J0;
      pt += 4;
      *pt += J7 * J0;
      pt += 4;
      *pt += J8 * J0;
      pt += 4;
      *pt += J9 * J0;
      pt += 4;
      *pt += J10 * J0;
      pt += 4;


      *pt += J1 * J1 * w;
      pt += 4;
      J1 *= w;
      *pt += J2 * J1;
      pt += 4;
      *pt += J3 * J1;
      pt += 4;
      *pt += J4 * J1;
      pt += 4;
      *pt += J5 * J1;
      pt += 4;
      *pt += J6 * J1;
      pt += 4;
      *pt += J7 * J1;
      pt += 4;
      *pt += J8 * J1;
      pt += 4;
      *pt += J9 * J1;
      pt += 4;
      *pt += J10 * J1;
      pt += 4;


      *pt += J2 * J2 * w;
      pt += 4;
      J2 *= w;
      *pt += J3 * J2;
      pt += 4;
      *pt += J4 * J2;
      pt += 4;
      *pt += J5 * J2;
      pt += 4;
      *pt += J6 * J2;
      pt += 4;
      *pt += J7 * J2;
      pt += 4;
      *pt += J8 * J2;
      pt += 4;
      *pt += J9 * J2;
      pt += 4;
      *pt += J10 * J2;
      pt += 4;


      *pt += J3 * J3 * w;
      pt += 4;
      J3 *= w;
      *pt += J4 * J3;
      pt += 4;
      *pt += J5 * J3;
      pt += 4;
      *pt += J6 * J3;
      pt += 4;
      *pt += J7 * J3;
      pt += 4;
      *pt += J8 * J3;
      pt += 4;
      *pt += J9 * J3;
      pt += 4;
      *pt += J10 * J3;
      pt += 4;


      *pt += J4 * J4 * w;
      pt += 4;
      J4 *= w;
      *pt += J5 * J4;
      pt += 4;
      *pt += J6 * J4;
      pt += 4;
      *pt += J7 * J4;
      pt += 4;
      *pt += J8 * J4;
      pt += 4;
      *pt += J9 * J4;
      pt += 4;
      *pt += J10 * J4;
      pt += 4;

      *pt += J5 * J5 * w;
      pt += 4;
      J5 *= w;
      *pt += J6 * J5;
      pt += 4;
      *pt += J7 * J5;
      pt += 4;
      *pt += J8 * J5;
      pt += 4;
      *pt += J9 * J5;
      pt += 4;
      *pt += J10 * J5;
      pt += 4;


      *pt += J6 * J6 * w;
      pt += 4;
      J6 *= w;
      *pt += J7 * J6;
      pt += 4;
      *pt += J8 * J6;
      pt += 4;
      *pt += J9 * J6;
      pt += 4;
      *pt += J10 * J6;
      pt += 4;


      *pt += J7 * J7 * w;
      pt += 4;
      J7 *= w;
      *pt += J8 * J7;
      pt += 4;
      *pt += J9 * J7;
      pt += 4;
      *pt += J10 * J7;
      pt += 4;

      *pt += J8 * J8 * w;
      pt += 4;
      J8 *= w;
      *pt += J9 * J8;
      pt += 4;
      *pt += J10 * J8;
      pt += 4;

      *pt += J9 * J9 * w;
      pt += 4;
      J9 *= w;
      *pt += J10 * J9;

      *pt += J10 * J10 * w;
      pt += 4;

      num++;
      numIn1++;
      shiftUp(false);
    }


  private:
    EIGEN_ALIGN16 float SSEData[4 * 66];
    EIGEN_ALIGN16 float SSEData1k[4 * 66];
    EIGEN_ALIGN16 float SSEData1m[4 * 66];
    float numIn1, numIn1k, numIn1m;


    void shiftUp(bool force) {
      if (numIn1 > 1000 || force) {
        for (int i = 0; i < 66; i++)
          _mm_store_ps(SSEData1k + 4 * i, _mm_add_ps(_mm_load_ps(SSEData + 4 * i), _mm_load_ps(SSEData1k + 4 * i)));
        numIn1k += numIn1;
        numIn1 = 0;
        memset(SSEData, 0, sizeof(float) * 4 * 66);
      }

      if (numIn1k > 1000 || force) {
        for (int i = 0; i < 66; i++)
          _mm_store_ps(SSEData1m + 4 * i, _mm_add_ps(_mm_load_ps(SSEData1k + 4 * i), _mm_load_ps(SSEData1m + 4 * i)));
        numIn1m += numIn1k;
        numIn1k = 0;
        memset(SSEData1k, 0, sizeof(float) * 4 * 66);
      }
    }
  };
}
