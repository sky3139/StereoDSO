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


/*
 * KFBuffer.cpp
 *
 *  Created on: Jan 7, 2014
 *      Author: engelj
 */

#include "FullSystem/CoarseTracker.h"
#include "FullSystem/FullSystem.h"
#include "FullSystem/HessianBlocks.h"
#include "FullSystem/Residuals.h"
#include "OptimizationBackend/EnergyFunctionalStructs.h"
#include "IOWrapper/ImageRW.h"
#include <algorithm>
#include "okvis_kinematics/include/okvis/kinematics/operators.hpp"
#include "okvis_kinematics/include/okvis/kinematics/Transformation.hpp"

#if !defined(__SSE3__) && !defined(__SSE2__) && !defined(__SSE1__)
#include "SSE2NEON.h"
#endif

namespace dso {


  template<int b, typename T>
  T *allocAligned(int size, std::vector<T *> &rawPtrVec) {
    const int padT = 1 + ((1 << b) / sizeof(T));
    T *ptr = new T[size + padT];
    rawPtrVec.push_back(ptr);
    T *alignedPtr = (T *) ((((uintptr_t) (ptr + padT)) >> b) << b);
    return alignedPtr;
  }


  CoarseTracker::CoarseTracker(int ww, int hh) : lastRef_aff_g2l(0, 0) {
    // make coarse tracking templates.
    for (int lvl = 0; lvl < pyrLevelsUsed; lvl++) {
      int wl = ww >> lvl;
      int hl = hh >> lvl;

      idepth[lvl] = allocAligned<4, float>(wl * hl, ptrToDelete);
      weightSums[lvl] = allocAligned<4, float>(wl * hl, ptrToDelete);
      weightSums_bak[lvl] = allocAligned<4, float>(wl * hl, ptrToDelete);

      pc_u[lvl] = allocAligned<4, float>(wl * hl, ptrToDelete);
      pc_v[lvl] = allocAligned<4, float>(wl * hl, ptrToDelete);
      pc_idepth[lvl] = allocAligned<4, float>(wl * hl, ptrToDelete);
      pc_color[lvl] = allocAligned<4, float>(wl * hl, ptrToDelete);

    }

    // warped buffers
    buf_warped_idepth = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_u = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_v = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_dx = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_dy = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_residual = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_weight = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_refColor = allocAligned<4, float>(ww * hh, ptrToDelete);
#if defined(STEREO_MODE)
    buf_warped_idepth_r = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_dx_r = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_dy_r = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_residual_r = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_weight_r = allocAligned<4, float>(ww * hh, ptrToDelete);
#endif
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)
    buf_warped_dd = allocAligned<4, float>(ww * hh, ptrToDelete);
    buf_warped_dd_r = allocAligned<4, float>(ww * hh, ptrToDelete);
    lastFrameShell = 0;
#endif
    newFrame = 0;
    newFrameRight = 0;
    lastRef = 0;
    debugPlot = debugPrint = true;
    w[0] = h[0] = 0;
    refFrameID = -1;
  }

  CoarseTracker::~CoarseTracker() {
    for (float *ptr : ptrToDelete)
      delete[] ptr;
    ptrToDelete.clear();
  }

  void CoarseTracker::makeK(CalibHessian *HCalib) {
    w[0] = wG[0];
    h[0] = hG[0];

    fx[0] = HCalib->fxl();
    fy[0] = HCalib->fyl();
    cx[0] = HCalib->cxl();
    cy[0] = HCalib->cyl();

    for (int level = 1; level < pyrLevelsUsed; ++level) {
      w[level] = w[0] >> level;
      h[level] = h[0] >> level;
      fx[level] = fx[level - 1] * 0.5;
      fy[level] = fy[level - 1] * 0.5;
      cx[level] = (cx[0] + 0.5) / ((int) 1 << level) - 0.5;
      cy[level] = (cy[0] + 0.5) / ((int) 1 << level) - 0.5;
    }

    for (int level = 0; level < pyrLevelsUsed; ++level) {
      K[level] << fx[level], 0.0, cx[level], 0.0, fy[level], cy[level], 0.0, 0.0, 1.0;
      Ki[level] = K[level].inverse();
      fxi[level] = Ki[level](0, 0);
      fyi[level] = Ki[level](1, 1);
      cxi[level] = Ki[level](0, 2);
      cyi[level] = Ki[level](1, 2);
    }
  }


  void CoarseTracker::makeCoarseDepthL0(std::vector<FrameHessian *> frameHessians) {
    // make coarse tracking templates for latstRef.
    memset(idepth[0], 0, sizeof(float) * w[0] * h[0]);
    memset(weightSums[0], 0, sizeof(float) * w[0] * h[0]);

    for (FrameHessian *fh : frameHessians) {
      for (PointHessian *ph : fh->pointHessians) {
        if (ph->lastResiduals[0].first != 0 && ph->lastResiduals[0].second == ResState::IN) {
          PointFrameResidual *r = ph->lastResiduals[0].first;
          assert(r->efResidual->isActive() && r->target == lastRef);
          int u = r->centerProjectedTo[0] + 0.5f;
          int v = r->centerProjectedTo[1] + 0.5f;
          float new_idepth = r->centerProjectedTo[2];
          float weight = sqrtf(1e-3 / (ph->efPoint->HdiF + 1e-12));
//          float weight = 1.0f;

          idepth[0][u + w[0] * v] += new_idepth * weight;
          weightSums[0][u + w[0] * v] += weight;
        }
      }
    }


    for (int lvl = 1; lvl < pyrLevelsUsed; lvl++) {
      int lvlm1 = lvl - 1;
      int wl = w[lvl], hl = h[lvl], wlm1 = w[lvlm1];

      float *idepth_l = idepth[lvl];
      float *weightSums_l = weightSums[lvl];

      float *idepth_lm = idepth[lvlm1];
      float *weightSums_lm = weightSums[lvlm1];

      for (int y = 0; y < hl; y++)
        for (int x = 0; x < wl; x++) {
          int bidx = 2 * x + 2 * y * wlm1;
          idepth_l[x + y * wl] = idepth_lm[bidx] +
                                 idepth_lm[bidx + 1] +
                                 idepth_lm[bidx + wlm1] +
                                 idepth_lm[bidx + wlm1 + 1];

          weightSums_l[x + y * wl] = weightSums_lm[bidx] +
                                     weightSums_lm[bidx + 1] +
                                     weightSums_lm[bidx + wlm1] +
                                     weightSums_lm[bidx + wlm1 + 1];
        }
    }


    // dilate idepth by 1.
    for (int lvl = 0; lvl < 2; lvl++) {
      int numIts = 1;


      for (int it = 0; it < numIts; it++) {
        int wh = w[lvl] * h[lvl] - w[lvl];
        int wl = w[lvl];
        float *weightSumsl = weightSums[lvl];
        float *weightSumsl_bak = weightSums_bak[lvl];
        memcpy(weightSumsl_bak, weightSumsl, w[lvl] * h[lvl] * sizeof(float));
        float *idepthl = idepth[lvl];  // dotnt need to make a temp copy of depth, since I only
        // read values with weightSumsl>0, and write ones with weightSumsl<=0.
        for (int i = w[lvl]; i < wh; i++) {
          if (weightSumsl_bak[i] <= 0) {
            float sum = 0, num = 0, numn = 0;
            if (weightSumsl_bak[i + 1 + wl] > 0) {
              sum += idepthl[i + 1 + wl];
              num += weightSumsl_bak[i + 1 + wl];
              numn++;
            }
            if (weightSumsl_bak[i - 1 - wl] > 0) {
              sum += idepthl[i - 1 - wl];
              num += weightSumsl_bak[i - 1 - wl];
              numn++;
            }
            if (weightSumsl_bak[i + wl - 1] > 0) {
              sum += idepthl[i + wl - 1];
              num += weightSumsl_bak[i + wl - 1];
              numn++;
            }
            if (weightSumsl_bak[i - wl + 1] > 0) {
              sum += idepthl[i - wl + 1];
              num += weightSumsl_bak[i - wl + 1];
              numn++;
            }
            if (numn > 0) {
              idepthl[i] = sum / numn;
              weightSumsl[i] = num / numn;
            }
          }
        }
      }
    }


    // dilate idepth by 1 (2 on lower levels).
    for (int lvl = 2; lvl < pyrLevelsUsed; lvl++) {
      int wh = w[lvl] * h[lvl] - w[lvl];
      int wl = w[lvl];
      float *weightSumsl = weightSums[lvl];
      float *weightSumsl_bak = weightSums_bak[lvl];
      memcpy(weightSumsl_bak, weightSumsl, w[lvl] * h[lvl] * sizeof(float));
      float *idepthl = idepth[lvl];  // dotnt need to make a temp copy of depth, since I only
      // read values with weightSumsl>0, and write ones with weightSumsl<=0.
      for (int i = w[lvl]; i < wh; i++) {
        if (weightSumsl_bak[i] <= 0) {
          float sum = 0, num = 0, numn = 0;
          if (weightSumsl_bak[i + 1] > 0) {
            sum += idepthl[i + 1];
            num += weightSumsl_bak[i + 1];
            numn++;
          }
          if (weightSumsl_bak[i - 1] > 0) {
            sum += idepthl[i - 1];
            num += weightSumsl_bak[i - 1];
            numn++;
          }
          if (weightSumsl_bak[i + wl] > 0) {
            sum += idepthl[i + wl];
            num += weightSumsl_bak[i + wl];
            numn++;
          }
          if (weightSumsl_bak[i - wl] > 0) {
            sum += idepthl[i - wl];
            num += weightSumsl_bak[i - wl];
            numn++;
          }
          if (numn > 0) {
            idepthl[i] = sum / numn;
            weightSumsl[i] = num / numn;
          }
        }
      }
    }


    // normalize idepths and weights.
    for (int lvl = 0; lvl < pyrLevelsUsed; lvl++) {
      float *weightSumsl = weightSums[lvl];
      float *idepthl = idepth[lvl];
      Eigen::Vector3f *dIRefl = lastRef->dIp[lvl];

      int wl = w[lvl], hl = h[lvl];

      int lpc_n = 0;
      float *lpc_u = pc_u[lvl];
      float *lpc_v = pc_v[lvl];
      float *lpc_idepth = pc_idepth[lvl];
      float *lpc_color = pc_color[lvl];


      for (int y = 2; y < hl - 2; y++)
        for (int x = 2; x < wl - 2; x++) {
          int i = x + y * wl;

          if (weightSumsl[i] > 0) {
            idepthl[i] /= weightSumsl[i];
            lpc_u[lpc_n] = x;
            lpc_v[lpc_n] = y;
            lpc_idepth[lpc_n] = idepthl[i];
            lpc_color[lpc_n] = dIRefl[i][0];


            if (!std::isfinite(lpc_color[lpc_n]) || !(idepthl[i] > 0)) {
              idepthl[i] = -1;
              continue;  // just skip if something is wrong.
            }
            lpc_n++;
          }
          else
            idepthl[i] = -1;

          weightSumsl[i] = 1;
        }

      pc_n[lvl] = lpc_n;
    }

  }

#if defined(STEREO_MODE) && defined(INERTIAL_MODE)

  void CoarseTracker::calcMSCSSEStereo(int lvl, Mat1010 &H_out, Vec10 &b_out, const SE3 &refToNew, AffLight aff_g2l,
                                       AffLight aff_g2l_r) {
    acc.initialize();

    __m128 fxl = _mm_set1_ps(fx[lvl]);
    __m128 fyl = _mm_set1_ps(fy[lvl]);
    __m128 b0 = _mm_set1_ps(lastRef_aff_g2l.b);
    __m128 a = _mm_set1_ps(
        (float) (AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                             aff_g2l)[0]));
    __m128 a_r = _mm_set1_ps(
        (float) (AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                             aff_g2l_r)[0]));

    __m128 one = _mm_set1_ps(1);
    __m128 minusOne = _mm_set1_ps(-1);
    __m128 zero = _mm_set1_ps(0);

    int n = buf_warped_n;
//    assert(n > 0);
//    LOG(INFO) << "buf_warped_n : " << buf_warped_n;
    assert(n % 4 == 0);
    for (int i = 0; i < n; i += 4) {
      __m128 dx = _mm_mul_ps(_mm_load_ps(buf_warped_dx + i), fxl);
      __m128 dy = _mm_mul_ps(_mm_load_ps(buf_warped_dy + i), fyl);
      __m128 u = _mm_load_ps(buf_warped_u + i);
      __m128 v = _mm_load_ps(buf_warped_v + i);
      __m128 id = _mm_load_ps(buf_warped_idepth + i);
      __m128 dd = _mm_load_ps(buf_warped_dd + i);
      __m128 dd_r = _mm_load_ps(buf_warped_dd_r + i);
      __m128 dd2_i = _mm_div_ps(one, _mm_add_ps(_mm_mul_ps(dd, dd), _mm_mul_ps(dd_r, dd_r)));

      acc.updateSSE_tened(
          _mm_mul_ps(dd, _mm_mul_ps(id, dx)),
          _mm_mul_ps(dd, _mm_mul_ps(id, dy)),
          _mm_mul_ps(dd, _mm_sub_ps(zero, _mm_mul_ps(id, _mm_add_ps(_mm_mul_ps(u, dx), _mm_mul_ps(v, dy))))),
          _mm_mul_ps(dd, _mm_sub_ps(zero, _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dx),
              _mm_mul_ps(dy, _mm_add_ps(one, _mm_mul_ps(v, v)))))),
          _mm_mul_ps(dd, _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dy),
              _mm_mul_ps(dx, _mm_add_ps(one, _mm_mul_ps(u, u))))),
          _mm_mul_ps(dd, _mm_sub_ps(_mm_mul_ps(u, dy), _mm_mul_ps(v, dx))),
          _mm_mul_ps(dd, _mm_mul_ps(a, _mm_sub_ps(b0, _mm_load_ps(buf_warped_refColor + i)))), //- a_l
          _mm_mul_ps(dd, minusOne), //- b_l
          _mm_mul_ps(dd, zero), //- a_r
          _mm_mul_ps(dd, zero), //- b_r
          _mm_load_ps(buf_warped_residual + i),
          _mm_mul_ps(_mm_load_ps(buf_warped_weight + i), dd2_i));

      __m128 dx_r = _mm_mul_ps(_mm_mul_ps(_mm_load_ps(buf_warped_idepth_r + i),
                                          _mm_load_ps(buf_warped_dx_r + i)), fxl);
      __m128 dy_r = _mm_mul_ps(_mm_mul_ps(_mm_load_ps(buf_warped_idepth_r + i),
                                          _mm_load_ps(buf_warped_dy_r + i)), fyl);

      acc.updateSSE_tened(
          _mm_mul_ps(dd_r, _mm_mul_ps(id, dx_r)),
          _mm_mul_ps(dd_r, _mm_mul_ps(id, dy_r)),
          _mm_mul_ps(dd_r, _mm_sub_ps(zero, _mm_mul_ps(id, _mm_add_ps(_mm_mul_ps(u, dx_r), _mm_mul_ps(v, dy_r))))),
          _mm_mul_ps(dd_r, _mm_sub_ps(zero, _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dx_r),
              _mm_mul_ps(dy_r, _mm_add_ps(one, _mm_mul_ps(v, v)))))),
          _mm_mul_ps(dd_r, _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dy_r),
              _mm_mul_ps(dx_r, _mm_add_ps(one, _mm_mul_ps(u, u))))),
          _mm_mul_ps(dd_r, _mm_sub_ps(_mm_mul_ps(u, dy_r), _mm_mul_ps(v, dx_r))),
          _mm_mul_ps(dd_r, zero), //- a_l
          _mm_mul_ps(dd_r, zero), //- b_l
          _mm_mul_ps(dd_r, _mm_mul_ps(a_r, _mm_sub_ps(b0, _mm_load_ps(buf_warped_refColor + i)))), //- a_r
          _mm_mul_ps(dd_r, minusOne), //- b_r
          _mm_load_ps(buf_warped_residual_r + i),
          _mm_mul_ps(_mm_load_ps(buf_warped_weight_r + i), dd2_i));
    }

    acc.finish();
    H_out = acc.H.topLeftCorner<10, 10>().cast<double>() * (1.0f / n);
    b_out = acc.H.topRightCorner<10, 1>().cast<double>() * (1.0f / n);

    H_out.block<10, 3>(0, 0) *= SCALE_XI_ROT;
    H_out.block<10, 3>(0, 3) *= SCALE_XI_TRANS;
    H_out.block<10, 1>(0, 6) *= SCALE_A;
    H_out.block<10, 1>(0, 7) *= SCALE_B;
    H_out.block<10, 1>(0, 8) *= SCALE_A;
    H_out.block<10, 1>(0, 9) *= SCALE_B;
    H_out.block<3, 10>(0, 0) *= SCALE_XI_ROT;
    H_out.block<3, 10>(3, 0) *= SCALE_XI_TRANS;
    H_out.block<1, 10>(6, 0) *= SCALE_A;
    H_out.block<1, 10>(7, 0) *= SCALE_B;
    H_out.block<1, 10>(8, 0) *= SCALE_A;
    H_out.block<1, 10>(9, 0) *= SCALE_B;
    b_out.segment<3>(0) *= SCALE_XI_ROT;
    b_out.segment<3>(3) *= SCALE_XI_TRANS;
    b_out.segment<1>(6) *= SCALE_A;
    b_out.segment<1>(7) *= SCALE_B;
    b_out.segment<1>(8) *= SCALE_A;
    b_out.segment<1>(9) *= SCALE_B;
  }

#endif
#if defined(STEREO_MODE)

  void CoarseTracker::calcGSSSEStereo(int lvl, Mat1010 &H_out, Vec10 &b_out, const SE3 &refToNew, AffLight aff_g2l,
                                      AffLight aff_g2l_r) {
    acc.initialize();

    __m128 fxl = _mm_set1_ps(fx[lvl]);
    __m128 fyl = _mm_set1_ps(fy[lvl]);
    __m128 b0 = _mm_set1_ps(lastRef_aff_g2l.b);
    __m128 a = _mm_set1_ps(
        (float) (AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                             aff_g2l)[0]));
    __m128 a_r = _mm_set1_ps(
        (float) (AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                             aff_g2l_r)[0]));

    __m128 one = _mm_set1_ps(1);
    __m128 minusOne = _mm_set1_ps(-1);
    __m128 zero = _mm_set1_ps(0);

    int n = buf_warped_n;
    assert(n % 4 == 0);
    for (int i = 0; i < n; i += 4) {
      __m128 dx = _mm_mul_ps(_mm_load_ps(buf_warped_dx + i), fxl);
      __m128 dy = _mm_mul_ps(_mm_load_ps(buf_warped_dy + i), fyl);
      __m128 u = _mm_load_ps(buf_warped_u + i);
      __m128 v = _mm_load_ps(buf_warped_v + i);
      __m128 id = _mm_load_ps(buf_warped_idepth + i);


      acc.updateSSE_tened(
          _mm_mul_ps(id, dx),
          _mm_mul_ps(id, dy),
          _mm_sub_ps(zero, _mm_mul_ps(id, _mm_add_ps(_mm_mul_ps(u, dx), _mm_mul_ps(v, dy)))),
          _mm_sub_ps(zero, _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dx),
              _mm_mul_ps(dy, _mm_add_ps(one, _mm_mul_ps(v, v))))),
          _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dy),
              _mm_mul_ps(dx, _mm_add_ps(one, _mm_mul_ps(u, u)))),
          _mm_sub_ps(_mm_mul_ps(u, dy), _mm_mul_ps(v, dx)),
          _mm_mul_ps(a, _mm_sub_ps(b0, _mm_load_ps(buf_warped_refColor + i))), //- a_l
          minusOne, //- b_l
          zero, //- a_r
          zero, //- b_r
          _mm_load_ps(buf_warped_residual + i),
          _mm_load_ps(buf_warped_weight + i));

      __m128 dx_r = _mm_mul_ps(_mm_mul_ps(_mm_load_ps(buf_warped_idepth_r + i),
                                          _mm_load_ps(buf_warped_dx_r + i)), fxl);
      __m128 dy_r = _mm_mul_ps(_mm_mul_ps(_mm_load_ps(buf_warped_idepth_r + i),
                                          _mm_load_ps(buf_warped_dy_r + i)), fyl);

      acc.updateSSE_tened(
          _mm_mul_ps(id, dx_r),
          _mm_mul_ps(id, dy_r),
          _mm_sub_ps(zero, _mm_mul_ps(id, _mm_add_ps(_mm_mul_ps(u, dx_r), _mm_mul_ps(v, dy_r)))),
          _mm_sub_ps(zero, _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dx_r),
              _mm_mul_ps(dy_r, _mm_add_ps(one, _mm_mul_ps(v, v))))),
          _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dy_r),
              _mm_mul_ps(dx_r, _mm_add_ps(one, _mm_mul_ps(u, u)))),
          _mm_sub_ps(_mm_mul_ps(u, dy_r), _mm_mul_ps(v, dx_r)),
          zero, //- a_l
          zero, //- b_l
          _mm_mul_ps(a_r, _mm_sub_ps(b0, _mm_load_ps(buf_warped_refColor + i))), //- a_r
          minusOne, //- b_r
          _mm_load_ps(buf_warped_residual_r + i),
          _mm_load_ps(buf_warped_weight_r + i));

    }

    acc.finish();
    H_out = acc.H.topLeftCorner<10, 10>().cast<double>() * (1.0f / n);
    b_out = acc.H.topRightCorner<10, 1>().cast<double>() * (1.0f / n);

    H_out.block<10, 3>(0, 0) *= SCALE_XI_ROT;
    H_out.block<10, 3>(0, 3) *= SCALE_XI_TRANS;
    H_out.block<10, 1>(0, 6) *= SCALE_A;
    H_out.block<10, 1>(0, 7) *= SCALE_B;
    H_out.block<10, 1>(0, 8) *= SCALE_A;
    H_out.block<10, 1>(0, 9) *= SCALE_B;
    H_out.block<3, 10>(0, 0) *= SCALE_XI_ROT;
    H_out.block<3, 10>(3, 0) *= SCALE_XI_TRANS;
    H_out.block<1, 10>(6, 0) *= SCALE_A;
    H_out.block<1, 10>(7, 0) *= SCALE_B;
    H_out.block<1, 10>(8, 0) *= SCALE_A;
    H_out.block<1, 10>(9, 0) *= SCALE_B;
    b_out.segment<3>(0) *= SCALE_XI_ROT;
    b_out.segment<3>(3) *= SCALE_XI_TRANS;
    b_out.segment<1>(6) *= SCALE_A;
    b_out.segment<1>(7) *= SCALE_B;
    b_out.segment<1>(8) *= SCALE_A;
    b_out.segment<1>(9) *= SCALE_B;
  }

  Vec6
  CoarseTracker::calcResStereo(int lvl, const SE3 &refToNew, AffLight aff_g2l, AffLight aff_g2l_r, float cutoffTH) {
    float E = 0;
    int numTermsInE = 0;
    int numTermsInWarped = 0;
    int numSaturated = 0;

    int wl = w[lvl];
    int hl = h[lvl];
    Eigen::Vector3f *dINewl = newFrame->dIp[lvl];
    Eigen::Vector3f *dINewl_r = newFrameRight->dIp[lvl];
    float fxl = fx[lvl];
    float fyl = fy[lvl];
    float cxl = cx[lvl];
    float cyl = cy[lvl];


    Mat33f RKi = (refToNew.rotationMatrix().cast<float>() * Ki[lvl]);
    Vec3f t = (refToNew.translation()).cast<float>();
    Vec2f affLL = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                              aff_g2l).cast<float>();
    Vec2f affLL_r = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrameRight->ab_exposure, lastRef_aff_g2l,
                                                aff_g2l_r).cast<float>();

//    assert(std::isfinite(affLL[0]));

    //- Static stereo reprojection
    Mat33f RKi_s = Mat33f::Identity() * Ki[lvl];
    Vec3f t_s(-baseline, 0, 0);

    float sumSquaredShiftT = 0;
    float sumSquaredShiftRT = 0;
    float sumSquaredShiftNum = 0;

    float maxEnergy =
        2 * setting_huberTH * cutoffTH - setting_huberTH * setting_huberTH;  // energy for r=setting_coarseCutoffTH.


    MinimalImageB3 *resImage = 0;
    if (debugPlot) {
      resImage = new MinimalImageB3(wl, hl);
      resImage->setConst(Vec3b(255, 255, 255));
    }

    int nl = pc_n[lvl];
    float *lpc_u = pc_u[lvl];
    float *lpc_v = pc_v[lvl];
    float *lpc_idepth = pc_idepth[lvl];
    float *lpc_color = pc_color[lvl];


    for (int i = 0; i < nl; i++) {
      bool rightValid = false;
      float id = lpc_idepth[i];
      float x = lpc_u[i];
      float y = lpc_v[i];

      Vec3f pt = RKi * Vec3f(x, y, 1) + t * id;
      float u = pt[0] / pt[2];
      float v = pt[1] / pt[2];
      float Ku = fxl * u + cxl;
      float Kv = fyl * v + cyl;
      float new_idepth = id / pt[2];

      Vec3f pt_r = RKi_s * Vec3f(Ku, Kv, 1) + t_s * new_idepth;
      float u_r = pt_r[0] / pt_r[2];
      float v_r = pt_r[1] / pt_r[2];
      float Ku_r = fxl * u_r + cxl;
      float Kv_r = fyl * v_r + cyl;
      float new_idepth_r = new_idepth / pt_r[2];

      if (lvl == 0 && i % 32 == 0) {
        // translation only (positive)
        Vec3f ptT = Ki[lvl] * Vec3f(x, y, 1) + t * id;
        float uT = ptT[0] / ptT[2];
        float vT = ptT[1] / ptT[2];
        float KuT = fxl * uT + cxl;
        float KvT = fyl * vT + cyl;

        // translation only (negative)
        Vec3f ptT2 = Ki[lvl] * Vec3f(x, y, 1) - t * id;
        float uT2 = ptT2[0] / ptT2[2];
        float vT2 = ptT2[1] / ptT2[2];
        float KuT2 = fxl * uT2 + cxl;
        float KvT2 = fyl * vT2 + cyl;

        //translation and rotation (negative)
        Vec3f pt3 = RKi * Vec3f(x, y, 1) - t * id;
        float u3 = pt3[0] / pt3[2];
        float v3 = pt3[1] / pt3[2];
        float Ku3 = fxl * u3 + cxl;
        float Kv3 = fyl * v3 + cyl;

        //translation and rotation (positive)
        //already have it.

        sumSquaredShiftT += (KuT - x) * (KuT - x) + (KvT - y) * (KvT - y);
        sumSquaredShiftT += (KuT2 - x) * (KuT2 - x) + (KvT2 - y) * (KvT2 - y);
        sumSquaredShiftRT += (Ku - x) * (Ku - x) + (Kv - y) * (Kv - y);
        sumSquaredShiftRT += (Ku3 - x) * (Ku3 - x) + (Kv3 - y) * (Kv3 - y);
        sumSquaredShiftNum += 2;
      }

      if (!(Ku > 2 && Kv > 2 && Ku < wl - 3 && Kv < hl - 3 && new_idepth > 0)) continue;
      if (!(Ku_r > 2 && Kv_r > 2 && Ku_r < wl - 3 && Kv_r < hl - 3 && new_idepth_r > 0)) rightValid = false;
      else rightValid = true;

      float refColor = lpc_color[i];
      Vec3f hitColor = getInterpolatedElement33(dINewl, Ku, Kv, wl);
      if (!std::isfinite((float) hitColor[0]) || hitColor[1] == 0 || hitColor[2] == 0) continue;
      float residual = hitColor[0] - (float) (affLL[0] * refColor + affLL[1]);
      float hw = fabs(residual) < setting_huberTH ? 1 : setting_huberTH / fabs(residual);

      Vec3f hitColor_r = getInterpolatedElement33(dINewl_r, Ku_r, Kv_r, wl);
      if (!std::isfinite((float) hitColor_r[0]) || hitColor_r[1] == 0 || hitColor_r[2] == 0) rightValid = false;
      else rightValid = true;
      float residual_r = hitColor_r[0] - (float) (affLL_r[0] * refColor + affLL_r[1]);
      float hw_r = fabs(residual_r) < setting_huberTH ? 1 : setting_huberTH / fabs(residual_r);

      if (fabs(residual) > cutoffTH) {
        if (debugPlot) resImage->setPixel4(lpc_u[i], lpc_v[i], Vec3b(0, 0, 255));
        E += maxEnergy;
        E += maxEnergy;
        numTermsInE++;
        numSaturated++;
      }
      else {
        if (debugPlot) resImage->setPixel4(lpc_u[i], lpc_v[i], Vec3b(residual + 128, residual + 128, residual + 128));

        E += hw * residual * residual * (2 - hw);
        E += hw_r * residual_r * residual_r * (2 - hw_r);
        numTermsInE++;

        buf_warped_idepth[numTermsInWarped] = new_idepth;
        buf_warped_u[numTermsInWarped] = u;
        buf_warped_v[numTermsInWarped] = v;
        buf_warped_dx[numTermsInWarped] = hitColor[1];
        buf_warped_dy[numTermsInWarped] = hitColor[2];
        buf_warped_residual[numTermsInWarped] = residual;
        buf_warped_weight[numTermsInWarped] = hw;
        buf_warped_refColor[numTermsInWarped] = lpc_color[i];
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)
        float pt2 = new_idepth / id;
        buf_warped_dd[numTermsInWarped] =
            pt2 * (hitColor[1] * fxl * (t[0] - u * t[2]) + hitColor[2] * fyl * (t[1] - v * t[2]));
#endif

        if (rightValid && fabs(residual_r) <= cutoffTH) {
          float pt_r2 = new_idepth_r / new_idepth;
          buf_warped_idepth_r[numTermsInWarped] = pt_r2;
          buf_warped_dx_r[numTermsInWarped] = hitColor_r[1];
          buf_warped_dy_r[numTermsInWarped] = hitColor_r[2];
          buf_warped_residual_r[numTermsInWarped] = residual_r;
          buf_warped_weight_r[numTermsInWarped] = hw_r;
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)
          buf_warped_dd_r[numTermsInWarped] =
              pt_r2 * pt2 * (hitColor_r[1] * fxl * (t[0] - u * t[2]) + hitColor_r[2] * fyl * (t[1] - v * t[2]));
#endif
        }
        else {
          buf_warped_idepth_r[numTermsInWarped] = 0;
          buf_warped_dx_r[numTermsInWarped] = 0;
          buf_warped_dy_r[numTermsInWarped] = 0;
          buf_warped_residual_r[numTermsInWarped] = 0;
          buf_warped_weight_r[numTermsInWarped] = 0;
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)
          buf_warped_dd_r[numTermsInWarped] = 0;
#endif
        }
        numTermsInWarped++;
      }
    }

    while (numTermsInWarped % 4 != 0) {
      buf_warped_idepth[numTermsInWarped] = 0;
      buf_warped_u[numTermsInWarped] = 0;
      buf_warped_v[numTermsInWarped] = 0;
      buf_warped_dx[numTermsInWarped] = 0;
      buf_warped_dy[numTermsInWarped] = 0;
      buf_warped_residual[numTermsInWarped] = 0;
      buf_warped_weight[numTermsInWarped] = 0;
      buf_warped_refColor[numTermsInWarped] = 0;

      buf_warped_idepth_r[numTermsInWarped] = 0;
      buf_warped_dx_r[numTermsInWarped] = 0;
      buf_warped_dy_r[numTermsInWarped] = 0;
      buf_warped_residual_r[numTermsInWarped] = 0;
      buf_warped_weight_r[numTermsInWarped] = 0;
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)
      buf_warped_dd[numTermsInWarped] = 0;
      buf_warped_dd_r[numTermsInWarped] = 0;
#endif
      numTermsInWarped++;
    }
    buf_warped_n = numTermsInWarped;


    if (debugPlot) {
      IOWrap::displayImage("RES", resImage, false);
      IOWrap::waitKey(0);
      delete resImage;
    }

    Vec6 rs;
    rs[0] = E;
    rs[1] = numTermsInE;
    rs[2] = sumSquaredShiftT / (sumSquaredShiftNum + 0.1);
    rs[3] = 0;
    rs[4] = sumSquaredShiftRT / (sumSquaredShiftNum + 0.1);
    rs[5] = numSaturated / (float) numTermsInE;

    return rs;
  }

#endif
#if !defined(STEREO_MODE) && !defined(INERTIAL_MODE)

  void CoarseTracker::calcGSSSE(int lvl, Mat88 &H_out, Vec8 &b_out, const SE3 &refToNew, AffLight aff_g2l) {
    acc.initialize();

    __m128 fxl = _mm_set1_ps(fx[lvl]);
    __m128 fyl = _mm_set1_ps(fy[lvl]);
    __m128 b0 = _mm_set1_ps(lastRef_aff_g2l.b);
    __m128 a = _mm_set1_ps(
        (float) (AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                             aff_g2l)[0]));

    __m128 one = _mm_set1_ps(1);
    __m128 minusOne = _mm_set1_ps(-1);
    __m128 zero = _mm_set1_ps(0);

    int n = buf_warped_n;
    assert(n % 4 == 0);
    for (int i = 0; i < n; i += 4) {
      __m128 dx = _mm_mul_ps(_mm_load_ps(buf_warped_dx + i), fxl);
      __m128 dy = _mm_mul_ps(_mm_load_ps(buf_warped_dy + i), fyl);
      __m128 u = _mm_load_ps(buf_warped_u + i);
      __m128 v = _mm_load_ps(buf_warped_v + i);
      __m128 id = _mm_load_ps(buf_warped_idepth + i);


      acc.updateSSE_eighted(
          _mm_mul_ps(id, dx),
          _mm_mul_ps(id, dy),
          _mm_sub_ps(zero, _mm_mul_ps(id, _mm_add_ps(_mm_mul_ps(u, dx), _mm_mul_ps(v, dy)))),
          _mm_sub_ps(zero, _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dx),
              _mm_mul_ps(dy, _mm_add_ps(one, _mm_mul_ps(v, v))))),
          _mm_add_ps(
              _mm_mul_ps(_mm_mul_ps(u, v), dy),
              _mm_mul_ps(dx, _mm_add_ps(one, _mm_mul_ps(u, u)))),
          _mm_sub_ps(_mm_mul_ps(u, dy), _mm_mul_ps(v, dx)),
          _mm_mul_ps(a, _mm_sub_ps(b0, _mm_load_ps(buf_warped_refColor + i))),
          minusOne,
          _mm_load_ps(buf_warped_residual + i),
          _mm_load_ps(buf_warped_weight + i));
    }

    acc.finish();
    H_out = acc.H.topLeftCorner<8, 8>().cast<double>() * (1.0f / n);
    b_out = acc.H.topRightCorner<8, 1>().cast<double>() * (1.0f / n);

    H_out.block<8, 3>(0, 0) *= SCALE_XI_ROT;
    H_out.block<8, 3>(0, 3) *= SCALE_XI_TRANS;
    H_out.block<8, 1>(0, 6) *= SCALE_A;
    H_out.block<8, 1>(0, 7) *= SCALE_B;
    H_out.block<3, 8>(0, 0) *= SCALE_XI_ROT;
    H_out.block<3, 8>(3, 0) *= SCALE_XI_TRANS;
    H_out.block<1, 8>(6, 0) *= SCALE_A;
    H_out.block<1, 8>(7, 0) *= SCALE_B;
    b_out.segment<3>(0) *= SCALE_XI_ROT;
    b_out.segment<3>(3) *= SCALE_XI_TRANS;
    b_out.segment<1>(6) *= SCALE_A;
    b_out.segment<1>(7) *= SCALE_B;
  }

  Vec6 CoarseTracker::calcRes(int lvl, const SE3 &refToNew, AffLight aff_g2l, float cutoffTH) {
    float E = 0;
    int numTermsInE = 0;
    int numTermsInWarped = 0;
    int numSaturated = 0;

    int wl = w[lvl];
    int hl = h[lvl];
    Eigen::Vector3f *dINewl = newFrame->dIp[lvl];
    float fxl = fx[lvl];
    float fyl = fy[lvl];
    float cxl = cx[lvl];
    float cyl = cy[lvl];


    Mat33f RKi = (refToNew.rotationMatrix().cast<float>() * Ki[lvl]);
    Vec3f t = (refToNew.translation()).cast<float>();
    Vec2f affLL = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                              aff_g2l).cast<float>();


    float sumSquaredShiftT = 0;
    float sumSquaredShiftRT = 0;
    float sumSquaredShiftNum = 0;

    float maxEnergy =
        2 * setting_huberTH * cutoffTH - setting_huberTH * setting_huberTH;  // energy for r=setting_coarseCutoffTH.


    MinimalImageB3 *resImage = 0;
    if (debugPlot) {
      resImage = new MinimalImageB3(wl, hl);
      resImage->setConst(Vec3b(255, 255, 255));
    }

    int nl = pc_n[lvl];
    float *lpc_u = pc_u[lvl];
    float *lpc_v = pc_v[lvl];
    float *lpc_idepth = pc_idepth[lvl];
    float *lpc_color = pc_color[lvl];


    for (int i = 0; i < nl; i++) {
      float id = lpc_idepth[i];
      float x = lpc_u[i];
      float y = lpc_v[i];

      Vec3f pt = RKi * Vec3f(x, y, 1) + t * id;
      float u = pt[0] / pt[2];
      float v = pt[1] / pt[2];
      float Ku = fxl * u + cxl;
      float Kv = fyl * v + cyl;
      float new_idepth = id / pt[2];

      if (lvl == 0 && i % 32 == 0) {
        // translation only (positive)
        Vec3f ptT = Ki[lvl] * Vec3f(x, y, 1) + t * id;
        float uT = ptT[0] / ptT[2];
        float vT = ptT[1] / ptT[2];
        float KuT = fxl * uT + cxl;
        float KvT = fyl * vT + cyl;

        // translation only (negative)
        Vec3f ptT2 = Ki[lvl] * Vec3f(x, y, 1) - t * id;
        float uT2 = ptT2[0] / ptT2[2];
        float vT2 = ptT2[1] / ptT2[2];
        float KuT2 = fxl * uT2 + cxl;
        float KvT2 = fyl * vT2 + cyl;

        //translation and rotation (negative)
        Vec3f pt3 = RKi * Vec3f(x, y, 1) - t * id;
        float u3 = pt3[0] / pt3[2];
        float v3 = pt3[1] / pt3[2];
        float Ku3 = fxl * u3 + cxl;
        float Kv3 = fyl * v3 + cyl;

        //translation and rotation (positive)
        //already have it.

        sumSquaredShiftT += (KuT - x) * (KuT - x) + (KvT - y) * (KvT - y);
        sumSquaredShiftT += (KuT2 - x) * (KuT2 - x) + (KvT2 - y) * (KvT2 - y);
        sumSquaredShiftRT += (Ku - x) * (Ku - x) + (Kv - y) * (Kv - y);
        sumSquaredShiftRT += (Ku3 - x) * (Ku3 - x) + (Kv3 - y) * (Kv3 - y);
        sumSquaredShiftNum += 2;
      }

      if (!(Ku > 2 && Kv > 2 && Ku < wl - 3 && Kv < hl - 3 && new_idepth > 0)) continue;


      float refColor = lpc_color[i];
      Vec3f hitColor = getInterpolatedElement33(dINewl, Ku, Kv, wl);
      if (!std::isfinite((float) hitColor[0])) continue;
      float residual = hitColor[0] - (float) (affLL[0] * refColor + affLL[1]);
      float hw = fabs(residual) < setting_huberTH ? 1 : setting_huberTH / fabs(residual);


      if (fabs(residual) > cutoffTH) {
        if (debugPlot) resImage->setPixel4(lpc_u[i], lpc_v[i], Vec3b(0, 0, 255));
        E += maxEnergy;
        numTermsInE++;
        numSaturated++;
      }
      else {
        if (debugPlot) resImage->setPixel4(lpc_u[i], lpc_v[i], Vec3b(residual + 128, residual + 128, residual + 128));

        E += hw * residual * residual * (2 - hw);
        numTermsInE++;

        buf_warped_idepth[numTermsInWarped] = new_idepth;
        buf_warped_u[numTermsInWarped] = u;
        buf_warped_v[numTermsInWarped] = v;
        buf_warped_dx[numTermsInWarped] = hitColor[1];
        buf_warped_dy[numTermsInWarped] = hitColor[2];
        buf_warped_residual[numTermsInWarped] = residual;
        buf_warped_weight[numTermsInWarped] = hw;
        buf_warped_refColor[numTermsInWarped] = lpc_color[i];
        numTermsInWarped++;
      }
    }

    while (numTermsInWarped % 4 != 0) {
      buf_warped_idepth[numTermsInWarped] = 0;
      buf_warped_u[numTermsInWarped] = 0;
      buf_warped_v[numTermsInWarped] = 0;
      buf_warped_dx[numTermsInWarped] = 0;
      buf_warped_dy[numTermsInWarped] = 0;
      buf_warped_residual[numTermsInWarped] = 0;
      buf_warped_weight[numTermsInWarped] = 0;
      buf_warped_refColor[numTermsInWarped] = 0;
      numTermsInWarped++;
    }
    buf_warped_n = numTermsInWarped;


    if (debugPlot) {
      IOWrap::displayImage("RES", resImage, false);
      IOWrap::waitKey(0);
      delete resImage;
    }

    Vec6 rs;
    rs[0] = E;
    rs[1] = numTermsInE;
    rs[2] = sumSquaredShiftT / (sumSquaredShiftNum + 0.1);
    rs[3] = 0;
    rs[4] = sumSquaredShiftRT / (sumSquaredShiftNum + 0.1);
    rs[5] = numSaturated / (float) numTermsInE;

    return rs;
  }

#endif


  void CoarseTracker::makeCoarseDepthForFirstFrame(FrameHessian *fh) {
    memset(idepth[0], 0, sizeof(float) * w[0] * h[0]);
    memset(weightSums[0], 0, sizeof(float) * w[0] * h[0]);

    for (PointHessian *ph : fh->pointHessians) {
      int u = ph->u + 0.5f;
      int v = ph->v + 0.5f;
      float new_idepth = ph->idepth;
      float weight = sqrtf(1e-3 / (ph->efPoint->HdiF + 1e-12));
//      float weight = 1.0f;

      idepth[0][u + w[0] * v] += new_idepth * weight;
      weightSums[0][u + w[0] * v] += weight;
    }

    for (int lvl = 1; lvl < pyrLevelsUsed; lvl++) {
      int lvlm1 = lvl - 1;
      int wl = w[lvl], hl = h[lvl], wlm = w[lvlm1];

      float *idepth_l = idepth[lvl];
      float *weightSums_l = weightSums[lvl];

      float *idepth_lm = idepth[lvlm1];
      float *weightSums_lm = weightSums[lvlm1];

      for (int y = 0; y < hl; y++)
        for (int x = 0; x < wl; x++) {
          int bidx = 2 * x + 2 * y * wlm;
          idepth_l[x + y * wl] = idepth_lm[bidx] + idepth_lm[bidx + 1]
                                 + idepth_lm[bidx + wlm] + idepth_lm[bidx + wlm + 1];
          weightSums_l[x + y * wl] = weightSums_lm[bidx] + weightSums_lm[bidx + 1]
                                     + weightSums_lm[bidx + wlm] + weightSums_lm[bidx + wlm + 1];
        }
    }

    for (int lvl = 0; lvl < 2; lvl++) {
      int numIts = 1;
      for (int it = 0; it < numIts; it++) {
        int wh = w[lvl] * h[lvl] - w[lvl];
        int wl = w[lvl];
        float *weightSumsl = weightSums[lvl];
        float *weightSumsl_bak = weightSums_bak[lvl];
        memcpy(weightSumsl_bak, weightSumsl, w[lvl] * h[lvl] * sizeof(float));
        float *idepthl = idepth[lvl];
        for (int i = wl; i < wh; i++) {
          if (weightSumsl_bak[i] <= 0) {
            float sum = 0, num = 0, numn = 0;
            if (weightSumsl_bak[i + 1 + wl] > 0) {
              sum += idepthl[i + 1 + wl];
              num += weightSumsl_bak[i + 1 + wl];
              numn++;
            }
            if (weightSumsl_bak[i - 1 - wl] > 0) {
              sum += idepthl[i - 1 - wl];
              num += weightSumsl_bak[i - 1 - wl];
              numn++;
            }
            if (weightSumsl_bak[i - 1 + wl] > 0) {
              sum += idepthl[i - 1 + wl];
              num += weightSumsl_bak[i - 1 + wl];
              numn++;
            }
            if (weightSumsl_bak[i + 1 - wl] > 0) {
              sum += idepthl[i + 1 - wl];
              num += weightSumsl_bak[i + 1 - wl];
              numn++;
            }
            if (numn > 0) {
              idepthl[i] = sum / numn;
              weightSumsl[i] = num / numn;
            }
          }
        }
      }
    }

    for (int lvl = 2; lvl < pyrLevelsUsed; lvl++) {
      int wh = w[lvl] * h[lvl] - w[lvl];
      int wl = w[lvl];
      float *weightSumsl = weightSums[lvl];
      float *weightSumsl_bak = weightSums_bak[lvl];
      memcpy(weightSumsl_bak, weightSumsl, w[lvl] * h[lvl] * sizeof(float));
      float *idepthl = idepth[lvl];
      for (int i = wl; i < wh; i++) {
        if (weightSumsl_bak[i] <= 0) {
          float sum = 0, num = 0, numn = 0;
          if (weightSumsl_bak[i + 1 + wl] > 0) {
            sum += idepthl[i + 1 + wl];
            num += weightSumsl_bak[i + 1 + wl];
            numn++;
          }
          if (weightSumsl_bak[i - 1 - wl] > 0) {
            sum += idepthl[i - 1 - wl];
            num += weightSumsl_bak[i - 1 - wl];
            numn++;
          }
          if (weightSumsl_bak[i - 1 + wl] > 0) {
            sum += idepthl[i - 1 + wl];
            num += weightSumsl_bak[i - 1 + wl];
            numn++;
          }
          if (weightSumsl_bak[i + 1 - wl] > 0) {
            sum += idepthl[i + 1 - wl];
            num += weightSumsl_bak[i + 1 - wl];
            numn++;
          }
          if (numn > 0) {
            idepthl[i] = sum / numn;
            weightSumsl[i] = num / numn;
          }
        }
      }
    }

    // normalize idepths and weights.
    for (int lvl = 0; lvl < pyrLevelsUsed; lvl++) {
      float *weightSumsl = weightSums[lvl];
      float *idepthl = idepth[lvl];
      Eigen::Vector3f *dIRefl = lastRef->dIp[lvl];

      int wl = w[lvl], hl = h[lvl];

      int lpc_n = 0;
      float *lpc_u = pc_u[lvl];
      float *lpc_v = pc_v[lvl];
      float *lpc_idepth = pc_idepth[lvl];
      float *lpc_color = pc_color[lvl];


      for (int y = 2; y < hl - 2; y++)
        for (int x = 2; x < wl - 2; x++) {
          int i = x + y * wl;

          if (weightSumsl[i] > 0) {
            idepthl[i] /= weightSumsl[i];
            lpc_u[lpc_n] = x;
            lpc_v[lpc_n] = y;
            lpc_idepth[lpc_n] = idepthl[i];
            lpc_color[lpc_n] = dIRefl[i][0];


            if (!std::isfinite(lpc_color[lpc_n]) || !(idepthl[i] > 0)) {
              idepthl[i] = -1;
              continue;  // just skip if something is wrong.
            }
            lpc_n++;
          }
          else
            idepthl[i] = -1;

          weightSumsl[i] = 1;
        }

      pc_n[lvl] = lpc_n;
    }
  }

  void CoarseTracker::setCTRefForFirstFrame(std::vector<FrameHessian *> frameHessians) {
    assert(frameHessians.size() > 0);
    lastRef = frameHessians.back();
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)
    optMode = OPT_MODE_2;
    lastFrameShell = NULL;
#endif

    makeCoarseDepthForFirstFrame(lastRef);

    refFrameID = lastRef->shell->id;
    lastRef_aff_g2l = lastRef->aff_g2l();

    firstCoarseRMSE = -1;
  }

  void CoarseTracker::setCoarseTrackingRef(
      std::vector<FrameHessian *> frameHessians) {
    assert(frameHessians.size() > 0);
    lastRef = frameHessians.back();
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)
    optMode = OPT_MODE_2;
    lastFrameShell = NULL;
#endif
    makeCoarseDepthL0(frameHessians);


    refFrameID = lastRef->shell->id;
    lastRef_aff_g2l = lastRef->aff_g2l();

    firstCoarseRMSE = -1;

  }

#if defined(STEREO_MODE)

  bool CoarseTracker::trackNewestCoarseStereo(
      FrameHessian *newFrameHessian,
      FrameHessian *newFrameHessianRight,
      SE3 &lastToNew_out,
      AffLight &aff_g2l_out, AffLight &aff_g2l_r_out,
      int coarsestLvl, Vec5 minResForAbort,
      IOWrap::Output3DWrapper *wrap) {
    debugPlot = setting_render_displayCoarseTrackingFull;
    debugPrint = false;

    assert(coarsestLvl < 5 && coarsestLvl < pyrLevelsUsed);

    lastResiduals.setConstant(NAN);
    lastFlowIndicators.setConstant(1000);


    newFrame = newFrameHessian;
    newFrameRight = newFrameHessianRight;
    int maxIterations[] = {10, 20, 50, 50, 50};
    float lambdaExtrapolationLimit = 0.001;

    SE3 refToNew_current = lastToNew_out;
    AffLight aff_g2l_current = aff_g2l_out;
    AffLight aff_g2l_r_current = aff_g2l_r_out;

    bool haveRepeated = false;


    for (int lvl = coarsestLvl; lvl >= 0; lvl--) {
      Mat1010 H;
      Vec10 b;
      float levelCutoffRepeat = 1;
      Vec6 resOld = calcResStereo(lvl, refToNew_current, aff_g2l_current, aff_g2l_r_current,
                                  setting_coarseCutoffTH * levelCutoffRepeat);
      while (resOld[5] > 0.6 && levelCutoffRepeat < 50) {
        levelCutoffRepeat *= 2;
        resOld = calcResStereo(lvl, refToNew_current, aff_g2l_current, aff_g2l_r_current,
                               setting_coarseCutoffTH * levelCutoffRepeat);

        if (!setting_debugout_runquiet) {
          char buf[256];
          sprintf(buf, "INCREASING cutoff to %f (ratio is %f)!\n", setting_coarseCutoffTH * levelCutoffRepeat,
                  resOld[5]);
          LOG(INFO) << buf;
        }
      }

      calcGSSSEStereo(lvl, H, b, refToNew_current, aff_g2l_current, aff_g2l_r_current);

      float lambda = 0.01;

      if (debugPrint) {
        Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                                   aff_g2l_current).cast<float>();
        printf("lvl%d, it %d (l=%f / %f) %s: %.3f->%.3f (%d -> %d) (|inc| = %f)! \t",
               lvl, -1, lambda, 1.0f,
               "INITIA",
               0.0f,
               resOld[0] / resOld[1],
               0, (int) resOld[1],
               0.0f);
        std::cout << refToNew_current.log().transpose() << " AFF " << aff_g2l_current.vec().transpose() << " (rel "
                  << relAff.transpose() << ")\n";
      }


      for (int iteration = 0; iteration < maxIterations[lvl]; iteration++) {
        Mat1010 Hl = H;
        for (int i = 0; i < 10; i++) Hl(i, i) *= (1 + lambda);
        Vec10 inc = Hl.ldlt().solve(-b);

        if (setting_affineOptModeA < 0 && setting_affineOptModeB < 0)  // fix a, b
        {
          inc.head<6>() = Hl.topLeftCorner<6, 6>().ldlt().solve(-b.head<6>());
          inc.tail<4>().setZero();
        }
        if (!(setting_affineOptModeA < 0) && setting_affineOptModeB < 0)  // fix b //- discard 7, 9
        {
          Mat1010 HlStitch = Hl;
          Vec10 bStitch = b;
          HlStitch.col(7) = HlStitch.col(8);
          HlStitch.row(7) = HlStitch.row(8);
          bStitch[7] = bStitch[8];
          Vec8 incStitch = HlStitch.topLeftCorner<8, 8>().ldlt().solve(-bStitch.head<8>());
          inc.setZero();
          inc.head<6>() = incStitch.head<6>();
          inc[6] = incStitch[6];
          inc[8] = incStitch[7];
        }
        if (setting_affineOptModeA < 0 && !(setting_affineOptModeB < 0))  // fix a //- discard 6, 8
        {
          Mat1010 HlStitch = Hl;
          Vec10 bStitch = b;
          HlStitch.col(6) = HlStitch.col(7);
          HlStitch.row(6) = HlStitch.row(7);
          HlStitch.col(7) = HlStitch.col(9);
          HlStitch.row(7) = HlStitch.row(9);
          bStitch[6] = bStitch[7];
          bStitch[7] = bStitch[9];
          Vec8 incStitch = HlStitch.topLeftCorner<8, 8>().ldlt().solve(-bStitch.head<8>());
          inc.setZero();
          inc.head<6>() = incStitch.head<6>();
          inc[7] = incStitch[6];
          inc[9] = incStitch[7];
        }


        float extrapFac = 1;
        if (lambda < lambdaExtrapolationLimit) extrapFac = sqrt(sqrt(lambdaExtrapolationLimit / lambda));
        inc *= extrapFac;

        Vec10 incScaled = inc;
        incScaled.segment<3>(0) *= SCALE_XI_ROT;
        incScaled.segment<3>(3) *= SCALE_XI_TRANS;
        incScaled[6] *= SCALE_A;
        incScaled[7] *= SCALE_B;
        incScaled[8] *= SCALE_A;
        incScaled[9] *= SCALE_B;

        if (!std::isfinite(incScaled.sum())) incScaled.setZero();

        SE3 refToNew_new = SE3::exp((Vec6) (incScaled.head<6>())) * refToNew_current;
        AffLight aff_g2l_new = aff_g2l_current;
        AffLight aff_g2l_r_new = aff_g2l_r_current;
        aff_g2l_new.a += incScaled[6];
        aff_g2l_new.b += incScaled[7];
        aff_g2l_r_new.a += incScaled[8];
        aff_g2l_r_new.b += incScaled[9];

        Vec6 resNew = calcResStereo(lvl, refToNew_new, aff_g2l_new, aff_g2l_r_new,
                                    setting_coarseCutoffTH * levelCutoffRepeat);

        bool accept = (resNew[0] / resNew[1]) < (resOld[0] / resOld[1]);

        if (debugPrint) {
          Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                                     aff_g2l_new).cast<float>();
          char buf[256];
          sprintf(buf, "lvl %d, it %d (l=%f / %f) %s: %.3f->%.3f (%d -> %d) (|inc| = %f)! \t",
                  lvl, iteration, lambda,
                  extrapFac,
                  (accept ? "ACCEPT" : "REJECT"),
                  resOld[0] / resOld[1],
                  resNew[0] / resNew[1],
                  (int) resOld[1], (int) resNew[1],
                  inc.norm());
          LOG(INFO) << buf;
          LOG(INFO) << refToNew_new.log().transpose() << " AFF " << aff_g2l_new.vec().transpose() << " (rel "
                    << relAff.transpose() << ")\n";
        }
        if (accept) {
          calcGSSSEStereo(lvl, H, b, refToNew_new, aff_g2l_new, aff_g2l_r_new);
          resOld = resNew;
          aff_g2l_current = aff_g2l_new;
          aff_g2l_r_current = aff_g2l_r_new;
          refToNew_current = refToNew_new;
          lambda *= 0.5;
        }
        else {
          lambda *= 4;
          if (lambda < lambdaExtrapolationLimit) lambda = lambdaExtrapolationLimit;
        }

        if (!(inc.norm() > 1e-3)) {
          if (debugPrint)
            printf("inc too small, break!\n");
          if (lvl == coarsestLvl) newFrameHessian->shell->trackIterations = iteration + 1;
          break;
        }
      }

      // set last residual for that level, as well as flow indicators.
      lastResiduals[lvl] = sqrtf((float) (resOld[0] / resOld[1]));
      lastFlowIndicators = resOld.segment<3>(2);
      if (lastResiduals[lvl] > 1.5 * minResForAbort[lvl]) return false;


      if (levelCutoffRepeat > 1 && !haveRepeated) {
        lvl++;
        haveRepeated = true;
        LOG(INFO) << "REPEAT LEVEL!";
      }
    }

    // set!
    lastToNew_out = refToNew_current;
    aff_g2l_out = aff_g2l_current;
    aff_g2l_r_out = aff_g2l_r_current;


    if ((setting_affineOptModeA != 0 && (fabsf(aff_g2l_out.a) > 1.2))
        || (setting_affineOptModeB != 0 && (fabsf(aff_g2l_out.b) > 200)))
      return false;

    Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                               aff_g2l_out).cast<float>();

    if ((setting_affineOptModeA == 0 && (fabsf(logf((float) relAff[0])) > 1.5))
        || (setting_affineOptModeB == 0 && (fabsf((float) relAff[1]) > 200)))
      return false;


    if (setting_affineOptModeA < 0) aff_g2l_out.a = 0;
    if (setting_affineOptModeB < 0) aff_g2l_out.b = 0;

    return true;
  }

#endif
#if defined(STEREO_MODE) && defined(INERTIAL_MODE)

  bool CoarseTracker::trackNewestCoarseStereo(
      FrameHessian *newFrameHessian,
      FrameHessian *newFrameHessianRight,
      SE3 &lastToNew_out,
      const std::vector<IMUMeasurement> &vIMUData,
      AffLight &aff_g2l_out, AffLight &aff_g2l_r_out,
      int coarsestLvl, Vec5 minResForAbort,
      IOWrap::Output3DWrapper *wrap) {
    debugPlot = setting_render_displayCoarseTrackingFull;
    debugPrint = false;

    assert(coarsestLvl < 5 && coarsestLvl < pyrLevelsUsed);

    lastResiduals.setConstant(NAN);
    lastFlowIndicators.setConstant(1000);


    newFrame = newFrameHessian;
    newFrameRight = newFrameHessianRight;
    int maxIterations[] = {10, 20, 50, 50, 50};
    float lambdaExtrapolationLimit = 0.001;

    SE3 refToNew_current = lastToNew_out;
    AffLight aff_g2l_current = aff_g2l_out;
    AffLight aff_g2l_r_current = aff_g2l_r_out;

    bool haveRepeated = false;


    for (int lvl = coarsestLvl; lvl >= 0; lvl--) {
      Mat1010 H;
      Vec10 b;
      float levelCutoffRepeat = 1;
      Vec6 resOld = calcResStereo(lvl, refToNew_current, aff_g2l_current, aff_g2l_r_current,
                                  setting_coarseCutoffTH * levelCutoffRepeat);
      while (resOld[5] > 0.6 && levelCutoffRepeat < 50) {
        levelCutoffRepeat *= 2;
        resOld = calcResStereo(lvl, refToNew_current, aff_g2l_current, aff_g2l_r_current,
                               setting_coarseCutoffTH * levelCutoffRepeat);

        if (!setting_debugout_runquiet) {
          char buf[256];
          sprintf(buf, "INCREASING cutoff to %f (ratio is %f)!\n", setting_coarseCutoffTH * levelCutoffRepeat,
                  resOld[5]);
          LOG(INFO) << buf;
        }
      }

      calcGSSSEStereo(lvl, H, b, refToNew_current, aff_g2l_current, aff_g2l_r_current);

      float lambda = 0.01;

      if (debugPrint) {
        Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                                   aff_g2l_current).cast<float>();
        printf("lvl%d, it %d (l=%f / %f) %s: %.3f->%.3f (%d -> %d) (|inc| = %f)! \t",
               lvl, -1, lambda, 1.0f,
               "INITIA",
               0.0f,
               resOld[0] / resOld[1],
               0, (int) resOld[1],
               0.0f);
        std::cout << refToNew_current.log().transpose() << " AFF " << aff_g2l_current.vec().transpose() << " (rel "
                  << relAff.transpose() << ")\n";
      }


      for (int iteration = 0; iteration < maxIterations[lvl]; iteration++) {
        Mat1010 Hl = H;
        for (int i = 0; i < 10; i++) Hl(i, i) *= (1 + lambda);
        Vec10 inc = Hl.ldlt().solve(-b);

        if (setting_affineOptModeA < 0 && setting_affineOptModeB < 0)  // fix a, b
        {
          inc.head<6>() = Hl.topLeftCorner<6, 6>().ldlt().solve(-b.head<6>());
          inc.tail<4>().setZero();
        }
        if (!(setting_affineOptModeA < 0) && setting_affineOptModeB < 0)  // fix b //- discard 7, 9
        {
          Mat1010 HlStitch = Hl;
          Vec10 bStitch = b;
          HlStitch.col(7) = HlStitch.col(8);
          HlStitch.row(7) = HlStitch.row(8);
          bStitch[7] = bStitch[8];
          Vec8 incStitch = HlStitch.topLeftCorner<8, 8>().ldlt().solve(-bStitch.head<8>());
          inc.setZero();
          inc.head<6>() = incStitch.head<6>();
          inc[6] = incStitch[6];
          inc[8] = incStitch[7];
        }
        if (setting_affineOptModeA < 0 && !(setting_affineOptModeB < 0))  // fix a //- discard 6, 8
        {
          Mat1010 HlStitch = Hl;
          Vec10 bStitch = b;
          HlStitch.col(6) = HlStitch.col(7);
          HlStitch.row(6) = HlStitch.row(7);
          HlStitch.col(7) = HlStitch.col(9);
          HlStitch.row(7) = HlStitch.row(9);
          bStitch[6] = bStitch[7];
          bStitch[7] = bStitch[9];
          Vec8 incStitch = HlStitch.topLeftCorner<8, 8>().ldlt().solve(-bStitch.head<8>());
          inc.setZero();
          inc.head<6>() = incStitch.head<6>();
          inc[7] = incStitch[6];
          inc[9] = incStitch[7];
        }


        float extrapFac = 1;
        if (lambda < lambdaExtrapolationLimit) extrapFac = sqrt(sqrt(lambdaExtrapolationLimit / lambda));
        inc *= extrapFac;

        Vec10 incScaled = inc;
        incScaled.segment<3>(0) *= SCALE_XI_ROT;
        incScaled.segment<3>(3) *= SCALE_XI_TRANS;
        incScaled[6] *= SCALE_A;
        incScaled[7] *= SCALE_B;
        incScaled[8] *= SCALE_A;
        incScaled[9] *= SCALE_B;

        if (!std::isfinite(incScaled.sum())) incScaled.setZero();

        SE3 refToNew_new = SE3::exp((Vec6) (incScaled.head<6>())) * refToNew_current;
        AffLight aff_g2l_new = aff_g2l_current;
        AffLight aff_g2l_r_new = aff_g2l_r_current;
        aff_g2l_new.a += incScaled[6];
        aff_g2l_new.b += incScaled[7];
        aff_g2l_r_new.a += incScaled[8];
        aff_g2l_r_new.b += incScaled[9];

        Vec6 resNew = calcResStereo(lvl, refToNew_new, aff_g2l_new, aff_g2l_r_new,
                                    setting_coarseCutoffTH * levelCutoffRepeat);

        bool accept = (resNew[0] / resNew[1]) < (resOld[0] / resOld[1]);

        if (debugPrint) {
          Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                                     aff_g2l_new).cast<float>();
          char buf[256];
          sprintf(buf, "lvl %d, it %d (l=%f / %f) %s: %.3f->%.3f (%d -> %d) (|inc| = %f)! \t",
                  lvl, iteration, lambda,
                  extrapFac,
                  (accept ? "ACCEPT" : "REJECT"),
                  resOld[0] / resOld[1],
                  resNew[0] / resNew[1],
                  (int) resOld[1], (int) resNew[1],
                  inc.norm());
          LOG(INFO) << buf;
          LOG(INFO) << refToNew_new.log().transpose() << " AFF " << aff_g2l_new.vec().transpose() << " (rel "
                    << relAff.transpose() << ")\n";
        }
        if (accept) {
          calcGSSSEStereo(lvl, H, b, refToNew_new, aff_g2l_new, aff_g2l_r_new);
          resOld = resNew;
          aff_g2l_current = aff_g2l_new;
          aff_g2l_r_current = aff_g2l_r_new;
          refToNew_current = refToNew_new;
          lambda *= 0.5;
        }
        else {
          lambda *= 4;
          if (lambda < lambdaExtrapolationLimit) lambda = lambdaExtrapolationLimit;
        }

        if (!(inc.norm() > 1e-3)) {
          if (debugPrint)
            printf("inc too small, break!\n");
          break;
        }
      }

      // set last residual for that level, as well as flow indicators.
      lastResiduals[lvl] = sqrtf((float) (resOld[0] / resOld[1]));
      lastFlowIndicators = resOld.segment<3>(2);
      if (lastResiduals[lvl] > 1.5 * minResForAbort[lvl]) return false;


      if (levelCutoffRepeat > 1 && !haveRepeated) {
        lvl++;
        haveRepeated = true;
        LOG(INFO) << "REPEAT LEVEL!";
      }
    }
    //- Get a good direct image alignment.
    //- Now IMU & Direct combined optimization.
    //- prepare for imu preintegration
    redoPropagation_ = true;

    if (lastFrameShell != NULL) t0_ = lastFrameShell->timestamp; //- MODE_3
    else t0_ = lastRef->shell->timestamp;
    t1_ = newFrame->shell->timestamp;

    SpeedAndBias speedAndBias_0;
    if (lastFrameShell != NULL) speedAndBias_0 = lastFrameShell->speedAndBias;
    else speedAndBias_0 = lastRef->shell->speedAndBias;
    SpeedAndBias speedAndBias_1 = newFrame->shell->speedAndBias;

    SE3 T_SW_0;
    if (lastFrameShell != NULL) T_SW_0 = lastToNew_out;
    else T_SW_0 = SE3(Sophus::Quaterniond(1, 0, 0, 0), Vec3(0, 0, 0));
    SE3 T_SW_1 = refToNew_current;


    MatXX H;
    VecX b;
    for (int iteration = 0; iteration < 6; iteration++) {
      float lambda = 0.01;
      //- get Direct Hessian
      Mat1010 H_D;
      Vec10 b_D;
      Vec6 resOld = calcResStereo(0, refToNew_current, aff_g2l_current, aff_g2l_r_current,
                                  setting_coarseCutoffTH * 1);
      calcGSSSEStereo(0, H_D, b_D, T_SW_1, aff_g2l_current, aff_g2l_r_current);

      Eigen::Matrix<double, 15, 1> res;
      Eigen::Matrix<double, 15, 6> Jrdxi_0;
      Eigen::Matrix<double, 15, 9> Jrdsb_0;
      Eigen::Matrix<double, 15, 6> Jrdxi_1;
      Eigen::Matrix<double, 15, 9> Jrdsb_1;
      //- get IMU Hessian
      getIMUHessian(vIMUData,
                    T_SW_0, T_SW_1,
                    speedAndBias_0, speedAndBias_1,
                    res,
                    Jrdxi_0, Jrdsb_0,
                    Jrdxi_1, Jrdsb_1);
      if (!lastFrameShell) {
        H = MatXX::Zero(28, 28);
        b = VecX::Zero(28);

        H.block<10, 10>(0, 0) += H_D;
        b.segment<10>(0) += b_D;

        H.block<6, 6>(0, 0) += Jrdxi_1.transpose() * Jrdxi_1; // 0
        H.block<6, 9>(0, 10) += Jrdxi_1.transpose() * Jrdsb_0; // 1
        H.block<6, 9>(0, 19) += Jrdxi_1.transpose() * Jrdsb_1; // 2
        H.block<9, 9>(10, 10) += Jrdsb_0.transpose() * Jrdsb_0; // 4
        H.block<9, 9>(10, 19) += Jrdsb_0.transpose() * Jrdsb_1; // 5
        H.block<9, 9>(19, 19) += Jrdsb_1.transpose() * Jrdsb_1; // 8

        H.block<9, 6>(10, 0) = H.block<6, 9>(0, 10).transpose(); // 3
        H.block<9, 6>(19, 0) = H.block<6, 9>(0, 19).transpose(); // 6
        H.block<9, 9>(19, 10) = H.block<9, 9>(10, 19).transpose(); // 7

        b.segment<6>(0) += Jrdxi_1.transpose() * res;
        b.segment<9>(10) += Jrdsb_0.transpose() * res;
        b.segment<9>(19) += Jrdsb_1.transpose() * res;

        //- no prior

        for (int i = 0; i < 28; i++) H(i, i) *= (1 + lambda);
        Vec28 inc = H.ldlt().solve(-b);

        float extrapFac = 1;
        if (lambda < lambdaExtrapolationLimit) extrapFac = sqrt(sqrt(lambdaExtrapolationLimit / lambda));
        inc *= extrapFac;

        Vec10 incScaled = inc.head<10>();
        incScaled.segment<3>(0) *= SCALE_XI_ROT;
        incScaled.segment<3>(3) *= SCALE_XI_TRANS;
        incScaled[6] *= SCALE_A;
        incScaled[7] *= SCALE_B;
        incScaled[8] *= SCALE_A;
        incScaled[9] *= SCALE_B;
        if (!std::isfinite(incScaled.sum())) incScaled.setZero();
        SE3 T_SW_1_new = SE3::exp((Vec6) (incScaled.head<6>())) * T_SW_1;
        AffLight aff_g2l_new = aff_g2l_current;
        AffLight aff_g2l_r_new = aff_g2l_r_current;
        aff_g2l_new.a += incScaled[6];
        aff_g2l_new.b += incScaled[7];
        aff_g2l_r_new.a += incScaled[8];
        aff_g2l_r_new.b += incScaled[9];

        //- just use direct image alignment residual for simplicity.
        Vec6 resNew = calcResStereo(0, T_SW_1_new, aff_g2l_new, aff_g2l_r_new,
                                    setting_coarseCutoffTH * 1);

        bool accept = (resNew[0] / resNew[1]) < (resOld[0] / resOld[1]);

        LOG(INFO) << "(resNew[0] / resNew[1]): " << (resNew[0] / resNew[1])
                  << "\t(resOld[0] / resOld[1]): " << (resOld[0] / resOld[1]);
        LOG(INFO) << "incScaled.head<6>(): " << incScaled.head<6>().transpose();
        LOG(INFO) << "inc.segment<9>(10): " << inc.segment<9>(10).transpose();
        LOG(INFO) << "inc.segment<9>(19): " << inc.segment<9>(19).transpose();

        if (accept) {
//          calcGSSSEStereo(0, H, b, T_SW_1_new, aff_g2l_new, aff_g2l_r_new);
          resOld = resNew;
          aff_g2l_current = aff_g2l_new;
          aff_g2l_r_current = aff_g2l_r_new;
          T_SW_1 = T_SW_1_new;
          speedAndBias_0 += inc.segment<9>(10);
          speedAndBias_1 += inc.segment<9>(19);
          lambda *= 0.5;
        }
        else {
          lambda *= 4;
          if (lambda < lambdaExtrapolationLimit) lambda = lambdaExtrapolationLimit;
        }
      }
      else {
        H = MatXX::Zero(38, 38);
        b = VecX::Zero(38);

        H.block<10, 10>(19, 19) += H_D;
        b.segment<10>(19) += b_D;

        H.block<6, 6>(0, 0) += Jrdxi_0.transpose() * Jrdxi_0; // 0
        H.block<6, 9>(0, 10) += Jrdxi_0.transpose() * Jrdsb_0; // 1
        H.block<6, 6>(0, 19) += Jrdxi_0.transpose() * Jrdxi_1; // 2
        H.block<6, 9>(0, 28) += Jrdxi_0.transpose() * Jrdsb_1; // 3
        H.block<9, 9>(10, 10) += Jrdsb_0.transpose() * Jrdsb_0; // 5
        H.block<9, 6>(10, 19) += Jrdsb_0.transpose() * Jrdxi_1; // 6
        H.block<9, 9>(10, 28) += Jrdsb_0.transpose() * Jrdsb_1; // 7
        H.block<6, 6>(19, 19) += Jrdxi_1.transpose() * Jrdxi_1; // 10
        H.block<6, 9>(19, 28) += Jrdxi_1.transpose() * Jrdsb_1; // 11
        H.block<9, 9>(29, 29) += Jrdsb_1.transpose() * Jrdsb_1; // 15

        H.block<9, 6>(10, 0) = H.block<6, 9>(0, 10).transpose(); // 4
        H.block<6, 6>(19, 0) = H.block<6, 6>(0, 19).transpose(); // 8
        H.block<6, 9>(19, 10) = H.block<9, 6>(10, 19).transpose(); // 9
        H.block<9, 6>(28, 0) = H.block<6, 9>(0, 28).transpose(); // 12
        H.block<9, 9>(28, 10) = H.block<9, 9>(10, 28).transpose(); // 13
        H.block<9, 6>(28, 19) = H.block<6, 9>(19, 28).transpose(); //14

        b.segment<6>(0) += Jrdxi_0.transpose() * res;
        b.segment<9>(10) += Jrdsb_0.transpose() * res;
        b.segment<6>(19) += Jrdxi_1.transpose() * res;
        b.segment<9>(29) += Jrdsb_1.transpose() * res;

        //- prior
        assert(HM.rows() == 19);
        assert(bM.rows() == 19);
        H.block<10, 10>(0, 0) += HM.block<10, 10>(0, 0);
        H.block<9, 9>(10, 10) += HM.block<9, 9>(10, 10);
        b.segment<10>(0) += bM.segment<10>(0);
        b.segment<9>(10) += bM.segment<9>(10);

        for (int i = 0; i < 38; i++) H(i, i) *= (1 + lambda);
        Vec38 inc = H.ldlt().solve(-b);

        float extrapFac = 1;
        if (lambda < lambdaExtrapolationLimit) extrapFac = sqrt(sqrt(lambdaExtrapolationLimit / lambda));
        inc *= extrapFac;

        Vec10 incScaled = inc.segment<10>(19);
        incScaled.segment<3>(0) *= SCALE_XI_ROT;
        incScaled.segment<3>(3) *= SCALE_XI_TRANS;
        incScaled[6] *= SCALE_A;
        incScaled[7] *= SCALE_B;
        incScaled[8] *= SCALE_A;
        incScaled[9] *= SCALE_B;
        if (!std::isfinite(incScaled.sum())) incScaled.setZero();
        SE3 T_SW_1_new = SE3::exp((Vec6) (incScaled.head<6>())) * T_SW_1;
        AffLight aff_g2l_new = aff_g2l_current;
        AffLight aff_g2l_r_new = aff_g2l_r_current;
        aff_g2l_new.a += incScaled[6];
        aff_g2l_new.b += incScaled[7];
        aff_g2l_r_new.a += incScaled[8];
        aff_g2l_r_new.b += incScaled[9];

        incScaled = inc.segment<10>(0);
        incScaled.segment<3>(0) *= SCALE_XI_ROT;
        incScaled.segment<3>(3) *= SCALE_XI_TRANS;
        incScaled[6] *= SCALE_A;
        incScaled[7] *= SCALE_B;
        incScaled[8] *= SCALE_A;
        incScaled[9] *= SCALE_B;
        if (!std::isfinite(incScaled.sum())) incScaled.setZero();
        SE3 T_SW_0_new = SE3::exp((Vec6) (incScaled.head<6>())) * T_SW_0;


        //- just use direct image alignment residual for simplicity.
        Vec6 resNew = calcResStereo(0, T_SW_1_new, aff_g2l_new, aff_g2l_r_new,
                                    setting_coarseCutoffTH * 1);

        bool accept = (resNew[0] / resNew[1]) < (resOld[0] / resOld[1]);

        if (accept) {
//          calcGSSSEStereo(0, H, b, T_SW_1_new, aff_g2l_new, aff_g2l_r_new);
          resOld = resNew;
          aff_g2l_current = aff_g2l_new;
          aff_g2l_r_current = aff_g2l_r_new;
          T_SW_0 = T_SW_0_new;
          T_SW_1 = T_SW_1_new;
          speedAndBias_0 += inc.segment<9>(10);
          speedAndBias_1 += inc.segment<9>(29);
          lambda *= 0.5;
        }
        else {
          lambda *= 4;
          if (lambda < lambdaExtrapolationLimit) lambda = lambdaExtrapolationLimit;
        }
      }
    }

    //- Marginalize
    if (!lastFrameShell) {
      //- Move H parts
      //- 4 <-> 8
      Mat99 temp4 = H.block<9, 9>(10, 10);
      H.block<9, 9>(10, 10) = H.block<9, 9>(19, 19);
      H.block<9, 9>(19, 19) = temp4;
      //- 5 <-> 7
      H.block<9, 9>(10, 19) = H.block<9, 9>(10, 19).transpose().eval();
      H.block<9, 9>(19, 10) = H.block<9, 9>(19, 10).transpose().eval();
      //- 1 <-> 2
      Eigen::Matrix<double, 10, 9> temp1 = H.block<10, 9>(0, 10);
      H.block<10, 9>(0, 10) = H.block<10, 9>(0, 19);
      H.block<10, 9>(0, 19) = temp1;
      //- 3 <-> 6
      Eigen::Matrix<double, 9, 10> temp3 = H.block<9, 10>(10, 0);
      H.block<9, 10>(10, 0) = H.block<9, 10>(19, 0);
      H.block<9, 10>(19, 0) = temp3;

      //- Move b parts
      Vec9 tempb = b.segment<9>(10);
      b.segment<9>(10) = b.segment<9>(19);
      b.segment<9>(19) = tempb;

      HM = H.block<19, 19>(0, 0) - H.block<19, 9>(0, 19) * H.block<9, 9>(19, 19).inverse() * H.block<9, 19>(19, 0);
      bM = b.segment<19>(0) - H.block<19, 9>(0, 19) * H.block<9, 9>(19, 19).inverse() * b.segment<9>(19);
    }
    else {
      //- No need to move H & b parts
      HM = H.block<19, 19>(19, 19) - H.block<19, 19>(19, 0) * H.block<19, 19>(0, 0).inverse() * H.block<19, 19>(0, 19);
      bM = b.segment<19>(19) - H.block<19, 19>(19, 0) * H.block<19, 19>(0, 0).inverse() * b.segment<19>(0);
    }

    //- shift back
    refToNew_current = T_SW_1;
    if (lastFrameShell != NULL) lastFrameShell->speedAndBias = speedAndBias_0;
    else lastRef->shell->speedAndBias = speedAndBias_0;
    newFrame->shell->speedAndBias = speedAndBias_1;

    // set!
    lastToNew_out = refToNew_current;
    aff_g2l_out = aff_g2l_current;
    aff_g2l_r_out = aff_g2l_r_current;


    if ((setting_affineOptModeA != 0 && (fabsf(aff_g2l_out.a) > 1.2))
        || (setting_affineOptModeB != 0 && (fabsf(aff_g2l_out.b) > 200)))
      return false;

    Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                               aff_g2l_out).cast<float>();

    if ((setting_affineOptModeA == 0 && (fabsf(logf((float) relAff[0])) > 1.5))
        || (setting_affineOptModeB == 0 && (fabsf((float) relAff[1]) > 200)))
      return false;


    if (setting_affineOptModeA < 0) aff_g2l_out.a = 0;
    if (setting_affineOptModeB < 0) aff_g2l_out.b = 0;

    optMode = OPT_MODE_3;
    lastFrameShell = newFrame->shell;

    return true;
  }


  void CoarseTracker::getIMUHessian(const std::vector<IMUMeasurement> &vIMUData,
                                    const SE3 &T_SW_0, const SE3 &T_SW_1,
                                    const SpeedAndBias &speedAndBias_0, const SpeedAndBias &speedAndBias_1,
                                    Eigen::Matrix<double, 15, 1> &res,
                                    Eigen::Matrix<double, 15, 6> &Jrdxi_0, Eigen::Matrix<double, 15, 9> &Jrdsb_0,
                                    Eigen::Matrix<double, 15, 6> &Jrdxi_1, Eigen::Matrix<double, 15, 9> &Jrdsb_1) {
    const double Delta_t = (t1_ - t0_);
    Eigen::Matrix<double, 9, 1> Delta_b = speedAndBias_1 - speedAndBias_ref_;

    SE3 T_WS_0 = T_SW_0.inverse();
    SE3 T_WS_1 = T_SW_1.inverse();

    Eigen::Vector3d t_S0 = T_WS_0.translation();
    Eigen::Vector3d t_S1 = T_WS_1.translation();

    const Eigen::Matrix3d C_WS_0 = T_WS_0.rotationMatrix();
    const Eigen::Matrix3d C_S0_W = C_WS_0.transpose();
    const Eigen::Matrix3d C_WS_1 = T_WS_1.rotationMatrix();
    const Eigen::Matrix3d C_S1_W = C_WS_1.transpose();

    redoPropagation_ = redoPropagation_ || (Delta_b.head<3>().norm() * Delta_t > 0.0001);
    if (redoPropagation_) {
      redoPreintegration(vIMUData, T_WS_0, T_WS_1, speedAndBias_0, &imuParameters);
      Delta_b.setZero();
      redoPropagation_ = false;
    }

//    const Eigen::Vector3d g_W = imuParameters.g * Eigen::Vector3d(0, -1, 0).normalized(); //- use lastRef Rotation matrix to convert this.
    const Eigen::Vector3d g_W = lastRef->worldToCam_evalPT.rotationMatrix() * Eigen::Vector3d(0, -imuParameters.g, 0);

    // the overall error vector
    Eigen::Matrix<double, 15, 1> error;
    error.segment<3>(0) = C_S0_W * (t_S1 - t_S0 - speedAndBias_0.head<3>() * Delta_t - 0.5 * g_W * Delta_t * Delta_t)
                          - (Delta_tilde_p_ij_ + d_p_d_bg_ * Delta_b.head<3>() + d_p_d_ba_ * Delta_b.tail<3>()); // p
    error.segment<3>(3) = SO3::log(SO3((Delta_tilde_R_ij_ *
                                        SO3::exp(d_R_d_bg_ * Delta_b.head<3>()).matrix()).transpose() * C_S0_W *
                                       C_WS_1)); // R
    error.segment<3>(6) = C_S0_W * (speedAndBias_1.head<3>() - speedAndBias_0.head<3>() - g_W * Delta_t)
                          - (Delta_tilde_v_ij_ + d_v_d_bg_ * Delta_b.head<3>() + d_v_d_ba_ * Delta_b.tail<3>()); // v
    error.tail<6>() = speedAndBias_1.tail<6>() - speedAndBias_0.tail<6>();

    // assign Jacobian w.r.t. x0
    Eigen::Matrix<double, 15, 15> F0 =
        Eigen::Matrix<double, 15, 15>::Zero(); // holds for d/db_g, d/db_a
    F0.block<3, 3>(0, 0) = -1 * -C_S0_W; // p/p
    F0.block<3, 3>(0, 3) = -1 * C_S0_W * okvis::kinematics::crossMx((t_S1 - t_S0 - speedAndBias_0.head<3>() * Delta_t
                                                                     - 0.5 * g_W * Delta_t * Delta_t)); // p/R
    F0.block<3, 3>(0, 6) = -C_S0_W * Delta_t; // p/v
    F0.block<3, 3>(0, 9) = -d_p_d_bg_; // p/bg
    F0.block<3, 3>(0, 12) = -d_p_d_ba_; // p/ba
    F0.block<3, 3>(3, 3) = -1 * -okvis::kinematics::rightJacobian(error.segment<3>(3)).inverse() * C_S1_W; // R/R
    F0.block<3, 3>(3, 9) = -okvis::kinematics::rightJacobian(-error.segment<3>(3)).inverse() // R/v
                           * okvis::kinematics::rightJacobian(d_R_d_bg_ * Delta_b.head<3>()) *
                           d_R_d_bg_; // R/bg J_l(\phi) = J_r(-\phi)
    F0.block<3, 3>(6, 3) = -1 * C_S0_W * okvis::kinematics::crossMx(speedAndBias_1.head<3>() - speedAndBias_0.head<3>()
                                                                    - g_W * Delta_t); // v/R
    F0.block<3, 3>(6, 6) = -1 * -C_S0_W; // v/v
    F0.block<3, 3>(6, 9) = -d_v_d_bg_; // v/bg
    F0.block<3, 3>(6, 12) = -d_v_d_ba_; // v/ba
    F0.block<3, 3>(9, 9) = Eigen::Matrix3d::Identity(); // bg/bg
    F0.block<3, 3>(12, 12) = Eigen::Matrix3d::Identity(); // ba/ba

    // assign Jacobian w.r.t. x1
    Eigen::Matrix<double, 15, 15> F1 =
        Eigen::Matrix<double, 15, 15>::Zero(); // holds for the biases
    F1.block<3, 3>(0, 0) = -1 * C_S0_W; // p/p
    F1.block<3, 3>(3, 3) = -1 * okvis::kinematics::rightJacobian(error.segment<3>(3)).inverse() * C_S1_W; // R/R
    F1.block<3, 3>(6, 6) = C_S0_W; // v/v
    F1.block<3, 3>(9, 9) = -Eigen::Matrix3d::Identity(); // bg/bg
    F1.block<3, 3>(12, 12) = -Eigen::Matrix3d::Identity(); // ba/ba

    res = setting_imuResidualWeight * (squareRootInformation_ * error);

    Jrdxi_0 = setting_imuResidualWeight * squareRootInformation_ * F0.block<15, 6>(0, 0);
    Jrdsb_0 = setting_imuResidualWeight * squareRootInformation_ * F0.block<15, 9>(0, 6);
    Jrdxi_1 = setting_imuResidualWeight * squareRootInformation_ * F1.block<15, 6>(0, 0);
    Jrdsb_1 = setting_imuResidualWeight * squareRootInformation_ * F1.block<15, 9>(0, 6);
  }

  int CoarseTracker::redoPreintegration(const std::vector<IMUMeasurement> &imuData,
                                        const SE3 &T_WS_0, const SE3 &T_WS_1,
                                        const SpeedAndBias &speedAndBias, IMUParameters *imuParameters) {

    // now the propagation
    double time = t0_;
    double end = t1_;

    // sanity check:
    assert(imuData.front().timestamp <= time);
    if (!(imuData.back().timestamp >= end))
      return -1;  // nothing to do...

    // increments (initialise with identity)
    Delta_tilde_R_ij_ = Eigen::Matrix3d::Identity();
    Delta_tilde_v_ij_ = Eigen::Vector3d::Zero();
    Delta_tilde_p_ij_ = Eigen::Vector3d::Zero();
    Sigma_eta_(0, 0) = imuParameters->sigma_gw_c * imuParameters->sigma_gw_c;
    Sigma_eta_(1, 1) = imuParameters->sigma_gw_c * imuParameters->sigma_gw_c;
    Sigma_eta_(2, 2) = imuParameters->sigma_gw_c * imuParameters->sigma_gw_c;
    Sigma_eta_(3, 3) = imuParameters->sigma_aw_c * imuParameters->sigma_aw_c;
    Sigma_eta_(4, 4) = imuParameters->sigma_aw_c * imuParameters->sigma_aw_c;
    Sigma_eta_(5, 5) = imuParameters->sigma_aw_c * imuParameters->sigma_aw_c;

    // sub-Jacobians
    d_R_d_bg_ = Eigen::Matrix3d::Zero();
    d_p_d_bg_ = Eigen::Matrix3d::Zero();
    d_p_d_ba_ = Eigen::Matrix3d::Zero();
    d_v_d_bg_ = Eigen::Matrix3d::Zero();
    d_v_d_ba_ = Eigen::Matrix3d::Zero();
//
    // the Jacobian of the increment (w/o biases)
    Sigma_ij_ = Eigen::Matrix<double, 15, 15>::Zero();

    double Delta_t = 0;
    bool hasStarted = false;
    int i = 0;
    for (std::vector<IMUMeasurement>::const_iterator it = imuData.begin();
         it != imuData.end(); ++it) {

      Eigen::Vector3d omega_S_0 = it->gyr;
      Eigen::Vector3d acc_S_0 = it->acc;
      Eigen::Vector3d omega_S_1 = (it + 1)->gyr;
      Eigen::Vector3d acc_S_1 = (it + 1)->acc;

      // time delta
      double nexttime;
      if ((it + 1) == imuData.end()) {
        nexttime = t1_;
      }
      else
        nexttime = (it + 1)->timestamp;
      double dt = (nexttime - time);

      if (end < nexttime) {
        double interval = (nexttime - it->timestamp);
        nexttime = t1_;
        dt = (nexttime - time);
        const double r = dt / interval;
        omega_S_1 = ((1.0 - r) * omega_S_0 + r * omega_S_1).eval();
        acc_S_1 = ((1.0 - r) * acc_S_0 + r * acc_S_1).eval();
      }

      if (dt <= 0.0) {
        continue;
      }
      Delta_t += dt;

      if (!hasStarted) {
        hasStarted = true;
        const double r = dt / (nexttime - it->timestamp);
        omega_S_0 = (r * omega_S_0 + (1.0 - r) * omega_S_1).eval();
        acc_S_0 = (r * acc_S_0 + (1.0 - r) * acc_S_1).eval();
      }

      // ensure integrity
      double sigma_g_c = imuParameters->sigma_g_c;
      double sigma_a_c = imuParameters->sigma_a_c;

      if (fabs(omega_S_0[0]) > imuParameters->g_max
          || fabs(omega_S_0[1]) > imuParameters->g_max
          || fabs(omega_S_0[2]) > imuParameters->g_max
          || fabs(omega_S_1[0]) > imuParameters->g_max
          || fabs(omega_S_1[1]) > imuParameters->g_max
          || fabs(omega_S_1[2]) > imuParameters->g_max) {
        sigma_g_c *= 100;
        LOG(WARNING) << "gyr saturation";
      }

      if (fabs(acc_S_0[0]) > imuParameters->a_max || fabs(acc_S_0[1]) > imuParameters->a_max
          || fabs(acc_S_0[2]) > imuParameters->a_max
          || fabs(acc_S_1[0]) > imuParameters->a_max
          || fabs(acc_S_1[1]) > imuParameters->a_max
          || fabs(acc_S_1[2]) > imuParameters->a_max) {
        sigma_a_c *= 100;
        LOG(WARNING) << "acc saturation";
      }

      // actual propagation (A.10)
      // R:
      const Eigen::Vector3d omega_S_true = (0.5 * (omega_S_0 + omega_S_1) - speedAndBias.segment<3>(3));
      Eigen::Matrix3d Delta_R = SO3::exp(
          omega_S_true * dt).matrix();//SO3::exp(okvis::kinematics::crossMx(omega_S_true * dt));
      Eigen::Matrix3d Delta_tilde_R_ij = Delta_tilde_R_ij_ * Delta_R;
      // v:
      const Eigen::Vector3d acc_S_true = (0.5 * (acc_S_0 + acc_S_1) - speedAndBias.segment<3>(6));
      Eigen::Vector3d Delta_tilde_v_ij = Delta_tilde_v_ij_ + Delta_tilde_R_ij_ * acc_S_true * dt;
      // p:
      Eigen::Vector3d Delta_tilde_p_ij = Delta_tilde_p_ij_ + 1.5 * Delta_tilde_R_ij_ * acc_S_true * dt * dt;

      // jacobian propagation
      d_R_d_bg_ += -T_WS_1.rotationMatrix() * Delta_tilde_R_ij_.transpose() * T_WS_0.rotationMatrix().transpose()
                   * okvis::kinematics::rightJacobian(omega_S_true * dt) * dt; //- ?
      d_v_d_bg_ += -Delta_tilde_R_ij_ * okvis::kinematics::crossMx(omega_S_true) * d_R_d_bg_ * dt;
      d_v_d_ba_ += -Delta_tilde_R_ij_ * dt;
      d_p_d_bg_ += -1.5 * Delta_tilde_R_ij_ * okvis::kinematics::crossMx(omega_S_true) * d_R_d_bg_ * dt;
      d_p_d_ba_ += -1.5 * Delta_tilde_R_ij_ * dt * dt;

      // covariance propagation
      Eigen::Matrix<double, 15, 15> A = Eigen::Matrix<double, 15, 15>::Identity();
      A.block<3, 3>(0, 0) = Delta_R.transpose();
      A.block<3, 3>(3, 0) = -Delta_tilde_R_ij_ * okvis::kinematics::crossMx(acc_S_true) * dt;
      A.block<3, 3>(6, 0) = -1.5 * Delta_tilde_R_ij_ * okvis::kinematics::crossMx(acc_S_true) * dt * dt;

      Eigen::Matrix<double, 15, 6> B = Eigen::Matrix<double, 15, 6>::Zero();
      B.block<3, 3>(0, 0) = okvis::kinematics::rightJacobian(omega_S_true * dt) * dt;
      B.block<3, 3>(3, 3) = Delta_tilde_R_ij_ * dt;
      B.block<3, 3>(6, 3) = 1.5 * Delta_tilde_R_ij_ * dt * dt;
      B.block<3, 3>(9, 0) = Eigen::Matrix3d::Identity() * dt;
      B.block<3, 3>(12, 3) = Eigen::Matrix3d::Identity() * dt;

      Sigma_ij_ = A * Sigma_ij_ * A.transpose() + B * Sigma_eta_ * B.transpose();

      // memory shift
      Delta_tilde_R_ij_ = Delta_tilde_R_ij;
      Delta_tilde_v_ij_ = Delta_tilde_v_ij;
      Delta_tilde_p_ij_ = Delta_tilde_p_ij;
      time = nexttime;

      ++i;

      if (nexttime == t1_)
        break;

    }

    // store the reference (linearisation) point
    speedAndBias_ref_ = speedAndBias;

    // get the weighting:
    // enforce symmetric
    Sigma_ij_ = 0.5 * Sigma_ij_ + 0.5 * Sigma_ij_.transpose().eval();

    // calculate inverse
    information_ = Sigma_ij_.inverse();
    information_ = 0.5 * information_ + 0.5 * information_.transpose().eval();

    // square root
    Eigen::LLT<information_t> lltOfInformation(information_);
    squareRootInformation_ = lltOfInformation.matrixL().transpose();

    return i;
  }

  Vec6 CoarseTracker::calculateRes(FrameHessian *newFrameHessian, FrameHessian *newFrameHessianRight) {

    assert(newFrame == newFrameHessian);
    assert(newFrameRight == newFrameHessianRight);

    SE3 refToNew_current = newFrameHessian->PRE_T_CW * newFrameHessian->shell->trackingRef->T_WC;
    AffLight aff_g2l_current = newFrameHessian->aff_g2l();
    AffLight aff_g2l_r_current = newFrameHessian->aff_g2l_r();

    Vec6 resNew = calcResStereo(0, refToNew_current, aff_g2l_current, aff_g2l_r_current,
                                setting_coarseCutoffTH);
    return resNew;
  }

  void CoarseTracker::calculateHAndb(FrameHessian *newFrameHessian, FrameHessian *newFrameHessianRight,
                                     Mat1010 &H, Vec10 &b) {

    assert(newFrame == newFrameHessian);
    assert(newFrameRight == newFrameHessianRight);

    SE3 refToNew_current = newFrameHessian->PRE_T_CW * this->lastRef->shell->T_WC;
    AffLight aff_g2l_current = newFrameHessian->aff_g2l();
    AffLight aff_g2l_r_current = newFrameHessian->aff_g2l_r();

    calcGSSSEStereo(0, H, b, refToNew_current, aff_g2l_current, aff_g2l_r_current);
  }

  void CoarseTracker::calculateMscAndbsc(FrameHessian *newFrameHessian, FrameHessian *newFrameHessianRight,
                                         Mat1010 &Msc, Vec10 &bsc) {
    assert(newFrame == newFrameHessian);
    assert(newFrameRight == newFrameHessianRight);

    SE3 refToNew_current = newFrameHessian->PRE_T_CW * this->lastRef->shell->T_WC;
    AffLight aff_g2l_current = newFrameHessian->aff_g2l();
    AffLight aff_g2l_r_current = newFrameHessian->aff_g2l_r();

    calcMSCSSEStereo(0, Msc, bsc, refToNew_current, aff_g2l_current, aff_g2l_r_current);
  }

#endif
#if !defined(STEREO_MODE) && !defined(INERTIAL_MODE)

  bool CoarseTracker::trackNewestCoarse(
      FrameHessian *newFrameHessian,
      SE3 &lastToNew_out, AffLight &aff_g2l_out,
      int coarsestLvl,
      Vec5 minResForAbort,
      IOWrap::Output3DWrapper *wrap) {
    debugPlot = setting_render_displayCoarseTrackingFull;
    debugPrint = false;

    assert(coarsestLvl < 5 && coarsestLvl < pyrLevelsUsed);

    lastResiduals.setConstant(NAN);
    lastFlowIndicators.setConstant(1000);


    newFrame = newFrameHessian;
    int maxIterations[] = {10, 20, 50, 50, 50};
    float lambdaExtrapolationLimit = 0.001;

    SE3 refToNew_current = lastToNew_out;
    AffLight aff_g2l_current = aff_g2l_out;

    bool haveRepeated = false;


    for (int lvl = coarsestLvl; lvl >= 0; lvl--) {
      Mat88 H;
      Vec8 b;
      float levelCutoffRepeat = 1;
      Vec6 resOld = calcRes(lvl, refToNew_current, aff_g2l_current, setting_coarseCutoffTH * levelCutoffRepeat);
      while (resOld[5] > 0.6 && levelCutoffRepeat < 50) {
        levelCutoffRepeat *= 2;
        resOld = calcRes(lvl, refToNew_current, aff_g2l_current, setting_coarseCutoffTH * levelCutoffRepeat);

        if (!setting_debugout_runquiet) {
          char buf[256];
          sprintf(buf, "INCREASING cutoff to %f (ratio is %f)!\n", setting_coarseCutoffTH * levelCutoffRepeat,
                  resOld[5]);
          LOG(INFO) << buf;
        }
      }

      calcGSSSE(lvl, H, b, refToNew_current, aff_g2l_current);

      float lambda = 0.01;

      if (debugPrint) {
        Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                                   aff_g2l_current).cast<float>();
        printf("lvl%d, it %d (l=%f / %f) %s: %.3f->%.3f (%d -> %d) (|inc| = %f)! \t",
               lvl, -1, lambda, 1.0f,
               "INITIA",
               0.0f,
               resOld[0] / resOld[1],
               0, (int) resOld[1],
               0.0f);
        std::cout << refToNew_current.log().transpose() << " AFF " << aff_g2l_current.vec().transpose() << " (rel "
                  << relAff.transpose() << ")\n";
      }


      for (int iteration = 0; iteration < maxIterations[lvl]; iteration++) {
        Mat88 Hl = H;
        for (int i = 0; i < 8; i++) Hl(i, i) *= (1 + lambda);
        Vec8 inc = Hl.ldlt().solve(-b);

        if (setting_affineOptModeA < 0 && setting_affineOptModeB < 0)  // fix a, b
        {
          inc.head<6>() = Hl.topLeftCorner<6, 6>().ldlt().solve(-b.head<6>());
          inc.tail<2>().setZero();
        }
        if (!(setting_affineOptModeA < 0) && setting_affineOptModeB < 0)  // fix b
        {
          inc.head<7>() = Hl.topLeftCorner<7, 7>().ldlt().solve(-b.head<7>());
          inc.tail<1>().setZero();
        }
        if (setting_affineOptModeA < 0 && !(setting_affineOptModeB < 0))  // fix a
        {
          Mat88 HlStitch = Hl;
          Vec8 bStitch = b;
          HlStitch.col(6) = HlStitch.col(7);
          HlStitch.row(6) = HlStitch.row(7);
          bStitch[6] = bStitch[7];
          Vec7 incStitch = HlStitch.topLeftCorner<7, 7>().ldlt().solve(-bStitch.head<7>());
          inc.setZero();
          inc.head<6>() = incStitch.head<6>();
          inc[6] = 0;
          inc[7] = incStitch[6];
        }


        float extrapFac = 1;
        if (lambda < lambdaExtrapolationLimit) extrapFac = sqrt(sqrt(lambdaExtrapolationLimit / lambda));
        inc *= extrapFac;

        Vec8 incScaled = inc;
        incScaled.segment<3>(0) *= SCALE_XI_ROT;
        incScaled.segment<3>(3) *= SCALE_XI_TRANS;
        incScaled.segment<1>(6) *= SCALE_A;
        incScaled.segment<1>(7) *= SCALE_B;

        if (!std::isfinite(incScaled.sum())) incScaled.setZero();

        SE3 refToNew_new = SE3::exp((Vec6) (incScaled.head<6>())) * refToNew_current;
        AffLight aff_g2l_new = aff_g2l_current;
        aff_g2l_new.a += incScaled[6];
        aff_g2l_new.b += incScaled[7];

        Vec6 resNew = calcRes(lvl, refToNew_new, aff_g2l_new, setting_coarseCutoffTH * levelCutoffRepeat);

        bool accept = (resNew[0] / resNew[1]) < (resOld[0] / resOld[1]);

        if (debugPrint) {
          Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                                     aff_g2l_new).cast<float>();
          printf("lvl %d, it %d (l=%f / %f) %s: %.3f->%.3f (%d -> %d) (|inc| = %f)! \t",
                 lvl, iteration, lambda,
                 extrapFac,
                 (accept ? "ACCEPT" : "REJECT"),
                 resOld[0] / resOld[1],
                 resNew[0] / resNew[1],
                 (int) resOld[1], (int) resNew[1],
                 inc.norm());
          std::cout << refToNew_new.log().transpose() << " AFF " << aff_g2l_new.vec().transpose() << " (rel "
                    << relAff.transpose() << ")\n";
        }
        if (accept) {
          calcGSSSE(lvl, H, b, refToNew_new, aff_g2l_new);
          resOld = resNew;
          aff_g2l_current = aff_g2l_new;
          refToNew_current = refToNew_new;
          lambda *= 0.5;
        }
        else {
          lambda *= 4;
          if (lambda < lambdaExtrapolationLimit) lambda = lambdaExtrapolationLimit;
        }

        if (!(inc.norm() > 1e-3)) {
          if (debugPrint)
            printf("inc too small, break!\n");
          break;
        }
      }

      // set last residual for that level, as well as flow indicators.
      lastResiduals[lvl] = sqrtf((float) (resOld[0] / resOld[1]));
      lastFlowIndicators = resOld.segment<3>(2);
      if (lastResiduals[lvl] > 1.5 * minResForAbort[lvl]) return false;


      if (levelCutoffRepeat > 1 && !haveRepeated) {
        lvl++;
        haveRepeated = true;
        LOG(INFO) << "REPEAT LEVEL!\n";
      }
    }

    // set!
    lastToNew_out = refToNew_current;
    aff_g2l_out = aff_g2l_current;


    if ((setting_affineOptModeA != 0 && (fabsf(aff_g2l_out.a) > 1.2))
        || (setting_affineOptModeB != 0 && (fabsf(aff_g2l_out.b) > 200)))
      return false;

    Vec2f relAff = AffLight::fromToVecExposure(lastRef->ab_exposure, newFrame->ab_exposure, lastRef_aff_g2l,
                                               aff_g2l_out).cast<float>();

    if ((setting_affineOptModeA == 0 && (fabsf(logf((float) relAff[0])) > 1.5))
        || (setting_affineOptModeB == 0 && (fabsf((float) relAff[1]) > 200)))
      return false;


    if (setting_affineOptModeA < 0) aff_g2l_out.a = 0;
    if (setting_affineOptModeB < 0) aff_g2l_out.b = 0;

    return true;
  }

#endif

  void
  CoarseTracker::debugPlotIDepthMap(float *minID_pt, float *maxID_pt, std::vector<IOWrap::Output3DWrapper *> &wraps) {
    if (w[1] == 0) return;


    int lvl = 0;

    {
      std::vector<float> allID;
      for (int i = 0; i < h[lvl] * w[lvl]; i++) {
        if (idepth[lvl][i] > 0)
          allID.push_back(idepth[lvl][i]);
      }
      std::sort(allID.begin(), allID.end());
      if (allID.size() == 0) return;
      int n = allID.size() - 1;

      float minID_new = allID[(int) (n * 0.05)];
      float maxID_new = allID[(int) (n * 0.95)];

      float minID, maxID;
      minID = minID_new;
      maxID = maxID_new;
      if (minID_pt != 0 && maxID_pt != 0) {
        if (*minID_pt < 0 || *maxID_pt < 0) {
          *maxID_pt = maxID;
          *minID_pt = minID;
        }
        else {

          // slowly adapt: change by maximum 10% of old span.
          float maxChange = 0.3 * (*maxID_pt - *minID_pt);

          if (minID < *minID_pt - maxChange)
            minID = *minID_pt - maxChange;
          if (minID > *minID_pt + maxChange)
            minID = *minID_pt + maxChange;


          if (maxID < *maxID_pt - maxChange)
            maxID = *maxID_pt - maxChange;
          if (maxID > *maxID_pt + maxChange)
            maxID = *maxID_pt + maxChange;

          *maxID_pt = maxID;
          *minID_pt = minID;
        }
      }

      int count = 0;

      MinimalImageB3 mf(w[lvl], h[lvl]);
      mf.setBlack();
      for (int i = 0; i < h[lvl] * w[lvl]; i++) {
        int c = lastRef->dIp[lvl][i][0] * 0.9f;
        if (c > 255) c = 255;
        mf.at(i) = Vec3b(c, c, c);
      }
      int wl = w[lvl];
      for (int y = 3; y < h[lvl] - 3; y++)
        for (int x = 3; x < wl - 3; x++) {
          int idx = x + y * wl;
          float sid = 0, nid = 0;
          float *bp = idepth[lvl] + idx;

          if (bp[0] > 0) {
            sid += bp[0];
            nid++;
          }
          if (bp[1] > 0) {
            sid += bp[1];
            nid++;
          }
          if (bp[-1] > 0) {
            sid += bp[-1];
            nid++;
          }
          if (bp[wl] > 0) {
            sid += bp[wl];
            nid++;
          }
          if (bp[-wl] > 0) {
            sid += bp[-wl];
            nid++;
          }

          if (bp[0] > 0 || nid >= 3) {
            float id = ((sid / nid) - minID) / ((maxID - minID));
            mf.setPixelCirc(x, y, makeJet3B(id));
            count++;
            //mf.at(idx) = makeJet3B(id);
          }
        }
      //IOWrap::displayImage("coarseDepth LVL0", &mf, false);


      for (IOWrap::Output3DWrapper *ow : wraps)
        ow->pushDepthImage(&mf);

      if (debugSaveImages) {
        char buf[1000];
        snprintf(buf, 1000, "images_out/predicted_%05d_%05d.png", lastRef->shell->id, refFrameID);
        IOWrap::writeImage(buf, &mf);
      }

    }
  }


  void CoarseTracker::debugPlotIDepthMapFloat(std::vector<IOWrap::Output3DWrapper *> &wraps) {
    if (w[1] == 0) return;
    int lvl = 0;
    MinimalImageF mim(w[lvl], h[lvl], idepth[lvl]);
    for (IOWrap::Output3DWrapper *ow : wraps)
      ow->pushDepthImageFloat(&mim, lastRef);
  }


  CoarseDistanceMap::CoarseDistanceMap(int ww, int hh) {
    fwdWarpedIDDistFinal = new float[ww * hh / 4];

    bfsList1 = new Eigen::Vector2i[ww * hh / 4];
    bfsList2 = new Eigen::Vector2i[ww * hh / 4];

    int fac = 1 << (pyrLevelsUsed - 1);


    coarseProjectionGrid = new PointFrameResidual *[2048 * (ww * hh / (fac * fac))];
    coarseProjectionGridNum = new int[ww * hh / (fac * fac)];

    w[0] = h[0] = 0;
  }

  CoarseDistanceMap::~CoarseDistanceMap() {
    delete[] fwdWarpedIDDistFinal;
    delete[] bfsList1;
    delete[] bfsList2;
    delete[] coarseProjectionGrid;
    delete[] coarseProjectionGridNum;
  }


  void CoarseDistanceMap::makeDistanceMap(
      std::vector<FrameHessian *> frameHessians,
      FrameHessian *frame) {
    int w1 = w[1];
    int h1 = h[1];
    int wh1 = w1 * h1;
    for (int i = 0; i < wh1; i++)
      fwdWarpedIDDistFinal[i] = 1000;


    // make coarse tracking templates for latstRef.
    int numItems = 0;

    for (FrameHessian *fh : frameHessians) {
      if (frame == fh) continue;

      SE3 fhToNew = frame->PRE_T_CW * fh->PRE_T_WC;
      Mat33f KRKi = (K[1] * fhToNew.rotationMatrix().cast<float>() * Ki[0]);
      Vec3f Kt = (K[1] * fhToNew.translation().cast<float>());

      for (PointHessian *ph : fh->pointHessians) {
        assert(ph->status == PointHessian::ACTIVE);
        Vec3f ptp = KRKi * Vec3f(ph->u, ph->v, 1) + Kt * ph->idepth_scaled;
        int u = ptp[0] / ptp[2] + 0.5f;
        int v = ptp[1] / ptp[2] + 0.5f;
        if (!(u > 0 && v > 0 && u < w[1] && v < h[1])) continue;
        fwdWarpedIDDistFinal[u + w1 * v] = 0;
        bfsList1[numItems] = Eigen::Vector2i(u, v);
        numItems++;
      }
    }

    growDistBFS(numItems);
  }


  void CoarseDistanceMap::makeInlierVotes(std::vector<FrameHessian *> frameHessians) {

  }


  void CoarseDistanceMap::growDistBFS(int bfsNum) {
    assert(w[0] != 0);
    int w1 = w[1], h1 = h[1];
    for (int k = 1; k < 40; k++) {
      int bfsNum2 = bfsNum;
      std::swap<Eigen::Vector2i *>(bfsList1, bfsList2);
      bfsNum = 0;

      if (k % 2 == 0) {
        for (int i = 0; i < bfsNum2; i++) {
          int x = bfsList2[i][0];
          int y = bfsList2[i][1];
          if (x == 0 || y == 0 || x == w1 - 1 || y == h1 - 1) continue;
          int idx = x + y * w1;

          if (fwdWarpedIDDistFinal[idx + 1] > k) {
            fwdWarpedIDDistFinal[idx + 1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x + 1, y);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx - 1] > k) {
            fwdWarpedIDDistFinal[idx - 1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x - 1, y);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx + w1] > k) {
            fwdWarpedIDDistFinal[idx + w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x, y + 1);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx - w1] > k) {
            fwdWarpedIDDistFinal[idx - w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x, y - 1);
            bfsNum++;
          }
        }
      }
      else {
        for (int i = 0; i < bfsNum2; i++) {
          int x = bfsList2[i][0];
          int y = bfsList2[i][1];
          if (x == 0 || y == 0 || x == w1 - 1 || y == h1 - 1) continue;
          int idx = x + y * w1;

          if (fwdWarpedIDDistFinal[idx + 1] > k) {
            fwdWarpedIDDistFinal[idx + 1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x + 1, y);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx - 1] > k) {
            fwdWarpedIDDistFinal[idx - 1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x - 1, y);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx + w1] > k) {
            fwdWarpedIDDistFinal[idx + w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x, y + 1);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx - w1] > k) {
            fwdWarpedIDDistFinal[idx - w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x, y - 1);
            bfsNum++;
          }

          if (fwdWarpedIDDistFinal[idx + 1 + w1] > k) {
            fwdWarpedIDDistFinal[idx + 1 + w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x + 1, y + 1);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx - 1 + w1] > k) {
            fwdWarpedIDDistFinal[idx - 1 + w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x - 1, y + 1);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx - 1 - w1] > k) {
            fwdWarpedIDDistFinal[idx - 1 - w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x - 1, y - 1);
            bfsNum++;
          }
          if (fwdWarpedIDDistFinal[idx + 1 - w1] > k) {
            fwdWarpedIDDistFinal[idx + 1 - w1] = k;
            bfsList1[bfsNum] = Eigen::Vector2i(x + 1, y - 1);
            bfsNum++;
          }
        }
      }
    }
  }


  void CoarseDistanceMap::addIntoDistFinal(int u, int v) {
    if (w[0] == 0) return;
    bfsList1[0] = Eigen::Vector2i(u, v);
    fwdWarpedIDDistFinal[u + w[1] * v] = 0;
    growDistBFS(1);
  }


  void CoarseDistanceMap::makeK(CalibHessian *HCalib) {
    w[0] = wG[0];
    h[0] = hG[0];

    fx[0] = HCalib->fxl();
    fy[0] = HCalib->fyl();
    cx[0] = HCalib->cxl();
    cy[0] = HCalib->cyl();

    for (int level = 1; level < pyrLevelsUsed; ++level) {
      w[level] = w[0] >> level;
      h[level] = h[0] >> level;
      fx[level] = fx[level - 1] * 0.5;
      fy[level] = fy[level - 1] * 0.5;
      cx[level] = (cx[0] + 0.5) / ((int) 1 << level) - 0.5;
      cy[level] = (cy[0] + 0.5) / ((int) 1 << level) - 0.5;
    }

    for (int level = 0; level < pyrLevelsUsed; ++level) {
      K[level] << fx[level], 0.0, cx[level], 0.0, fy[level], cy[level], 0.0, 0.0, 1.0;
      Ki[level] = K[level].inverse();
      fxi[level] = Ki[level](0, 0);
      fyi[level] = Ki[level](1, 1);
      cxi[level] = Ki[level](0, 2);
      cyi[level] = Ki[level](1, 2);
    }
  }

}
