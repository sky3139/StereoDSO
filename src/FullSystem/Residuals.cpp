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

#include "FullSystem/FullSystem.h"

#include "stdio.h"
#include "util/globalFuncs.h"
#include <Eigen/LU>
#include <algorithm>
#include "IOWrapper/ImageDisplay.h"
#include "util/globalCalib.h"
#include <Eigen/SVD>
#include <Eigen/Eigenvalues>

#include "FullSystem/ResidualProjections.h"
#include "OptimizationBackend/EnergyFunctional.h"
#include "OptimizationBackend/EnergyFunctionalStructs.h"

#include "FullSystem/HessianBlocks.h"

#include "okvis_kinematics/include/okvis/kinematics/operators.hpp"
#include "okvis_kinematics/include/okvis/kinematics/Transformation.hpp"

namespace dso {
  int PointFrameResidual::instanceCounter = 0;


  long runningResID = 0;


  PointFrameResidual::PointFrameResidual() {
    assert(false);
    instanceCounter++;
  }

  PointFrameResidual::~PointFrameResidual() {
    assert(efResidual == 0);
    instanceCounter--;
    delete J;
  }

  PointFrameResidual::PointFrameResidual(PointHessian *point_, FrameHessian *host_, FrameHessian *target_) :
      point(point_),
      host(host_),
      target(target_) {
    efResidual = 0;
    instanceCounter++;
    resetOOB();
    J = new RawResidualJacobian();
    assert(((long) J) % 16 == 0);

    staticStereo = false;

    isNew = true;
  }

#if defined(STEREO_MODE)

  double PointFrameResidual::linearize(CalibHessian *HCalib) {
    state_NewEnergyWithOutlier = -1;

    if (state_state == ResState::OOB) {
      state_NewState = ResState::OOB;
      return state_energy;
    }

    FrameFramePrecalc *precalc = &(host->targetPrecalc[target->idx]);
    float energyLeft = 0;
    const Eigen::Vector3f *dIl = target->dI;
    //const float* const Il = target->I;
    const Mat33f &PRE_KRKiTll = precalc->PRE_KRKiTll;
    const Vec3f &PRE_KtTll = precalc->PRE_KtTll;
    const Mat33f &PRE_RTll_0 = precalc->PRE_RTll_0;
    const Vec3f &PRE_tTll_0 = precalc->PRE_tTll_0;
    const float *const color = point->color;
    const float *const weights = point->weights;

    Vec2f affLL = precalc->PRE_aff_mode;
    float b0 = precalc->PRE_b0_mode;


    Vec6f d_xi_x, d_xi_y;
    Vec4f d_C_x, d_C_y;
    float d_d_x, d_d_y;
    {
      float drescale, u, v, new_idepth; // new_idepth = idepth*drescale;
      float Ku, Kv;
      Vec3f KliP;

      //- For current MACRO SCALE_IDEPTH, point->idepth_zero_scaled equals to idepth
      if (!projectPoint(point->u, point->v, point->idepth_zero_scaled, 0, 0, HCalib,
                        PRE_RTll_0, PRE_tTll_0, drescale, u, v, Ku, Kv, KliP, new_idepth)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      centerProjectedTo = Vec3f(Ku, Kv, new_idepth);

      //- http://www.cnblogs.com/JingeTU/p/8203606.html
      // diff d_idepth
      //- {\partial x_2} \over {\partial \rho_1}
      //- d_d_x, d_d_y: x for u, y for v, they are not part of idepth.
      // #define SCALE_IDEPTH 1.0f
      d_d_x = drescale * (PRE_tTll_0[0] - PRE_tTll_0[2] * u) * SCALE_IDEPTH * HCalib->fxl();
      d_d_y = drescale * (PRE_tTll_0[1] - PRE_tTll_0[2] * v) * SCALE_IDEPTH * HCalib->fyl();




      // diff calib
      //- {\partial x_2} \over {\partial \begin{bmatrix} f_x & f_y & c_x & c_y\end{bmatrix}}
      // drescale = new_idepth / idepth
      // #define SCALE_F 50.0f
      // #define SCALE_C 50.0f
      d_C_x[2] = drescale * (PRE_RTll_0(2, 0) * u - PRE_RTll_0(0, 0));
      d_C_x[3] = HCalib->fxl() * drescale * (PRE_RTll_0(2, 1) * u - PRE_RTll_0(0, 1)) * HCalib->fyli();
      d_C_x[0] = KliP[0] * d_C_x[2];
      d_C_x[1] = KliP[1] * d_C_x[3];

      d_C_y[2] = HCalib->fyl() * drescale * (PRE_RTll_0(2, 0) * v - PRE_RTll_0(1, 0)) * HCalib->fxli();
      d_C_y[3] = drescale * (PRE_RTll_0(2, 1) * v - PRE_RTll_0(1, 1));
      d_C_y[0] = KliP[0] * d_C_y[2];
      d_C_y[1] = KliP[1] * d_C_y[3];

      d_C_x[0] = (d_C_x[0] + u) * SCALE_F;
      d_C_x[1] *= SCALE_F;
      d_C_x[2] = (d_C_x[2] + 1) * SCALE_C;
      d_C_x[3] *= SCALE_C;

      d_C_y[0] *= SCALE_F;
      d_C_y[1] = (d_C_y[1] + v) * SCALE_F;
      d_C_y[2] *= SCALE_C;
      d_C_y[3] = (d_C_y[3] + 1) * SCALE_C;

//      d_C_x[2] = drescale * (-PRE_RTll_0(0, 0));
//      d_C_x[3] = HCalib->fxl() * drescale * (-PRE_RTll_0(0, 1)) * HCalib->fyli();
//      d_C_x[0] = KliP[0] * d_C_x[2];
//      d_C_x[1] = KliP[1] * d_C_x[3];
//
//      d_C_y[2] = HCalib->fyl() * drescale * (-PRE_RTll_0(1, 0)) * HCalib->fxli();
//      d_C_y[3] = drescale * (-PRE_RTll_0(1, 1));
//      d_C_y[0] = KliP[0] * d_C_y[2];
//      d_C_y[1] = KliP[1] * d_C_y[3];
//
//      d_C_x[0] = (d_C_x[0] + u) * SCALE_F;
//      d_C_x[1] *= SCALE_F;
//      d_C_x[2] = (d_C_x[2] + 1) * SCALE_C;
//      d_C_x[3] *= SCALE_C;
//
//      d_C_y[0] *= SCALE_F;
//      d_C_y[1] = (d_C_y[1] + v) * SCALE_F;
//      d_C_y[2] *= SCALE_C;
//      d_C_y[3] = (d_C_y[3] + 1) * SCALE_C;

      //- {\partial x_2} \over {\partial \xi_{21}}
      d_xi_x[0] = new_idepth * HCalib->fxl();
      d_xi_x[1] = 0;
      d_xi_x[2] = -new_idepth * u * HCalib->fxl();
      d_xi_x[3] = -u * v * HCalib->fxl();
      d_xi_x[4] = (1 + u * u) * HCalib->fxl();
      d_xi_x[5] = -v * HCalib->fxl();

      d_xi_y[0] = 0;
      d_xi_y[1] = new_idepth * HCalib->fyl();
      d_xi_y[2] = -new_idepth * v * HCalib->fyl();
      d_xi_y[3] = -(1 + v * v) * HCalib->fyl();
      d_xi_y[4] = u * v * HCalib->fyl();
      d_xi_y[5] = u * HCalib->fyl();
    }


    {
      J->Jpdxi[0] = d_xi_x;
      J->Jpdxi[1] = d_xi_y;

      J->Jpdc[0] = d_C_x;
      J->Jpdc[1] = d_C_y;

      J->Jpdd[0] = d_d_x;
      J->Jpdd[1] = d_d_y;

    }


    float JIdxJIdx_00 = 0, JIdxJIdx_11 = 0, JIdxJIdx_10 = 0;
    float JabJIdx_00 = 0, JabJIdx_01 = 0, JabJIdx_10 = 0, JabJIdx_11 = 0;
    float JabJIdx_20 = 0, JabJIdx_21 = 0, JabJIdx_30 = 0, JabJIdx_31 = 0;
    float JabJab_00 = 0, JabJab_01 = 0, JabJab_11 = 0;
    float JabJab_02 = 0, JabJab_03 = 0, JabJab_12 = 0, JabJab_13 = 0,
        JabJab_22 = 0, JabJab_23 = 0, JabJab_33 = 0;

    float wJI2_sum = 0;

    for (int idx = 0; idx < patternNum; idx++) {
      float Ku, Kv;
      if (!projectPoint(point->u + patternP[idx][0], point->v + patternP[idx][1], point->idepth_scaled, PRE_KRKiTll,
                        PRE_KtTll, Ku, Kv)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      projectedTo[idx][0] = Ku;
      projectedTo[idx][1] = Kv;


      Vec3f hitColor = (getInterpolatedElement33(dIl, Ku, Kv, wG[0]));
      float residual = hitColor[0] - (float) (affLL[0] * color[idx] + affLL[1]);


      float drdA = (color[idx] - b0);
      if (!std::isfinite((float) hitColor[0])) {
        state_NewState = ResState::OOB;
        return state_energy;
      }


      float w = sqrtf(
          setting_outlierTHSumComponent / (setting_outlierTHSumComponent + hitColor.tail<2>().squaredNorm()));
      w = 0.5f * (w + weights[idx]);


      float hw = fabsf(residual) < setting_huberTH ? 1 : setting_huberTH / fabsf(residual);
      energyLeft += w * w * hw * residual * residual * (2 - hw);

      {
        if (hw < 1) hw = sqrtf(hw);
        hw = hw * w;

        hitColor[1] *= hw;
        hitColor[2] *= hw;

        J->resF[idx] = residual * hw;

        J->JIdx[0][idx] = hitColor[1];
        J->JIdx[1][idx] = hitColor[2];
        //- {\partial r_{21}} \over {\partial a_{21}}
        J->JabF[0][idx] = -drdA * hw;
        //- {\partial r_{21}} \over {\partial b_{21}}
        J->JabF[1][idx] = -hw;
        //- Right frame ab.
        J->JabF[2][idx] = 0;
        J->JabF[3][idx] = 0;

        JIdxJIdx_00 += hitColor[1] * hitColor[1];
        JIdxJIdx_11 += hitColor[2] * hitColor[2];
        JIdxJIdx_10 += hitColor[1] * hitColor[2];

        JabJIdx_00 += J->JabF[0][idx] * hitColor[1];
        JabJIdx_01 += J->JabF[0][idx] * hitColor[2];
        JabJIdx_10 += J->JabF[1][idx] * hitColor[1];
        JabJIdx_11 += J->JabF[1][idx] * hitColor[2];

        JabJIdx_20 += J->JabF[2][idx] * hitColor[1];
        JabJIdx_21 += J->JabF[2][idx] * hitColor[2];
        JabJIdx_30 += J->JabF[3][idx] * hitColor[1];
        JabJIdx_31 += J->JabF[3][idx] * hitColor[2];

        JabJab_00 += J->JabF[0][idx] * J->JabF[0][idx];
        JabJab_01 += J->JabF[0][idx] * J->JabF[1][idx];
        JabJab_11 += J->JabF[1][idx] * J->JabF[1][idx];

        JabJab_02 += J->JabF[0][idx] * J->JabF[2][idx];
        JabJab_03 += J->JabF[0][idx] * J->JabF[3][idx];
        JabJab_12 += J->JabF[1][idx] * J->JabF[2][idx];
        JabJab_13 += J->JabF[1][idx] * J->JabF[3][idx];
        JabJab_22 += J->JabF[2][idx] * J->JabF[2][idx];
        JabJab_23 += J->JabF[2][idx] * J->JabF[3][idx];
        JabJab_33 += J->JabF[3][idx] * J->JabF[3][idx];

        wJI2_sum += hw * hw * (hitColor[1] * hitColor[1] + hitColor[2] * hitColor[2]);

        if (setting_affineOptModeA < 0) {
          J->JabF[0][idx] = 0;
          J->JabF[2][idx] = 0;
        }
        if (setting_affineOptModeB < 0) {
          J->JabF[1][idx] = 0;
          J->JabF[3][idx] = 0;
        }

      }
    }

    J->JIdx2(0, 0) = JIdxJIdx_00;
    J->JIdx2(0, 1) = JIdxJIdx_10;
    J->JIdx2(1, 0) = JIdxJIdx_10;
    J->JIdx2(1, 1) = JIdxJIdx_11;
    J->JabJIdx(0, 0) = JabJIdx_00;
    J->JabJIdx(0, 1) = JabJIdx_01;
    J->JabJIdx(1, 0) = JabJIdx_10;
    J->JabJIdx(1, 1) = JabJIdx_11;
    J->JabJIdx(2, 0) = JabJIdx_20;
    J->JabJIdx(2, 1) = JabJIdx_21;
    J->JabJIdx(3, 0) = JabJIdx_30;
    J->JabJIdx(3, 1) = JabJIdx_31;
    J->Jab2(0, 0) = JabJab_00;
    J->Jab2(0, 1) = J->Jab2(1, 0) = JabJab_01;
    J->Jab2(1, 1) = JabJab_11;
    J->Jab2(0, 2) = J->Jab2(2, 0) = JabJab_02;
    J->Jab2(0, 3) = J->Jab2(3, 0) = JabJab_03;
    J->Jab2(1, 2) = J->Jab2(2, 1) = JabJab_12;
    J->Jab2(1, 3) = J->Jab2(3, 1) = JabJab_13;
    J->Jab2(2, 2) = JabJab_22;
    J->Jab2(2, 3) = J->Jab2(3, 2) = JabJab_23;
    J->Jab2(3, 3) = JabJab_33;

    state_NewEnergyWithOutlier = energyLeft;

    if (energyLeft > std::max<float>(host->frameEnergyTH, target->frameEnergyTH) || wJI2_sum < 2) {
      energyLeft = std::max<float>(host->frameEnergyTH, target->frameEnergyTH);
      state_NewState = ResState::OUTLIER;
    }
    else {
      state_NewState = ResState::IN;
    }

    state_NewEnergy = energyLeft;
    return energyLeft;
  }

  double PointFrameResidual::linearizeStatic(CalibHessian *HCalib) {
    assert(host->rightFrame == target);
    state_NewEnergyWithOutlier = -1;

    if (state_state == ResState::OOB) {
      state_NewState = ResState::OOB;
      return state_energy;
    }

    FrameFramePrecalc *precalc = &(host->targetPrecalc.back());
    float energyLeft = 0;
    const Eigen::Vector3f *dIl = target->dI;
    //const float* const Il = target->I;
    const Mat33f &PRE_KRKiTll = precalc->PRE_KRKiTll;
    const Vec3f &PRE_KtTll = precalc->PRE_KtTll;
    const Mat33f &PRE_RTll_0 = precalc->PRE_RTll_0;
    const Vec3f &PRE_tTll_0 = precalc->PRE_tTll_0;
    const float *const color = point->color;
    const float *const weights = point->weights;

    Vec2f affLL = precalc->PRE_aff_mode;
    float b0 = precalc->PRE_b0_mode;

//    std::cout << "PRE_tTll_0: \n";
//    std::cout << PRE_tTll_0 << std::endl;
//    std::cout << "PRE_RTll_0: \n";
//    std::cout << PRE_RTll_0 << std::endl;


//    Vec6f d_xi_x, d_xi_y;
    Vec4f d_C_x, d_C_y;
    float d_d_x, d_d_y;
    {
      float drescale, u, v, new_idepth; // new_idepth = idepth*drescale;
      float Ku, Kv;
      Vec3f KliP;

      //- For current MACRO SCALE_IDEPTH, point->idepth_zero_scaled equals to idepth
      if (!projectPoint(point->u, point->v, point->idepth_zero_scaled, 0, 0, HCalib,
                        PRE_RTll_0, PRE_tTll_0, drescale, u, v, Ku, Kv, KliP, new_idepth)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      centerProjectedTo = Vec3f(Ku, Kv, new_idepth);

      //- http://www.cnblogs.com/JingeTU/p/8203606.html
      // diff d_idepth
      //- {\partial x_2} \over {\partial \rho_1}
      //- d_d_x, d_d_y: x for u, y for v, they are not part of idepth.
      d_d_x = drescale * (PRE_tTll_0[0] - PRE_tTll_0[2] * u) * SCALE_IDEPTH * HCalib->fxl();
      d_d_y = drescale * (PRE_tTll_0[1] - PRE_tTll_0[2] * v) * SCALE_IDEPTH * HCalib->fyl();




      // diff calib
      //- {\partial x_2} \over {\partial \begin{bmatrix} f_x & f_y & c_x & c_y\end{bmatrix}}
      // drescale = new_idepth / idepth
      d_C_x[2] = drescale * (PRE_RTll_0(2, 0) * u - PRE_RTll_0(0, 0));
      d_C_x[3] = HCalib->fxl() * drescale * (PRE_RTll_0(2, 1) * u - PRE_RTll_0(0, 1)) * HCalib->fyli();
      d_C_x[0] = KliP[0] * d_C_x[2];
      d_C_x[1] = KliP[1] * d_C_x[3];

      d_C_y[2] = HCalib->fyl() * drescale * (PRE_RTll_0(2, 0) * v - PRE_RTll_0(1, 0)) * HCalib->fxli();
      d_C_y[3] = drescale * (PRE_RTll_0(2, 1) * v - PRE_RTll_0(1, 1));
      d_C_y[0] = KliP[0] * d_C_y[2];
      d_C_y[1] = KliP[1] * d_C_y[3];

      d_C_x[0] = (d_C_x[0] + u) * SCALE_F;
      d_C_x[1] *= SCALE_F;
      d_C_x[2] = (d_C_x[2] + 1) * SCALE_C;
      d_C_x[3] *= SCALE_C;

      d_C_y[0] *= SCALE_F;
      d_C_y[1] = (d_C_y[1] + v) * SCALE_F;
      d_C_y[2] *= SCALE_C;
      d_C_y[3] = (d_C_y[3] + 1) * SCALE_C;

//      d_C_x[2] = drescale * (-PRE_RTll_0(0, 0));
//      d_C_x[3] = HCalib->fxl() * drescale * (-PRE_RTll_0(0, 1)) * HCalib->fyli();
//      d_C_x[0] = KliP[0] * d_C_x[2];
//      d_C_x[1] = KliP[1] * d_C_x[3];
//
//      d_C_y[2] = HCalib->fyl() * drescale * (-PRE_RTll_0(1, 0)) * HCalib->fxli();
//      d_C_y[3] = drescale * (-PRE_RTll_0(1, 1));
//      d_C_y[0] = KliP[0] * d_C_y[2];
//      d_C_y[1] = KliP[1] * d_C_y[3];
//
//      d_C_x[0] = (d_C_x[0] + u) * SCALE_F;
//      d_C_x[1] *= SCALE_F;
//      d_C_x[2] = (d_C_x[2] + 1) * SCALE_C;
//      d_C_x[3] *= SCALE_C;
//
//      d_C_y[0] *= SCALE_F;
//      d_C_y[1] = (d_C_y[1] + v) * SCALE_F;
//      d_C_y[2] *= SCALE_C;
//      d_C_y[3] = (d_C_y[3] + 1) * SCALE_C;
    }


    {
      J->Jpdxi[0] = Vec6f::Zero();
      J->Jpdxi[1] = Vec6f::Zero();

      J->Jpdc[0] = d_C_x;
      J->Jpdc[1] = d_C_y;

      J->Jpdd[0] = d_d_x;
      J->Jpdd[1] = d_d_y;

    }


    float JIdxJIdx_00 = 0, JIdxJIdx_11 = 0, JIdxJIdx_10 = 0;
    float JabJIdx_00 = 0, JabJIdx_01 = 0, JabJIdx_10 = 0, JabJIdx_11 = 0;
    float JabJIdx_20 = 0, JabJIdx_21 = 0, JabJIdx_30 = 0, JabJIdx_31 = 0;
    float JabJab_00 = 0, JabJab_01 = 0, JabJab_11 = 0;
    float JabJab_02 = 0, JabJab_03 = 0, JabJab_12 = 0, JabJab_13 = 0,
        JabJab_22 = 0, JabJab_23 = 0, JabJab_33 = 0;

    float wJI2_sum = 0;

    for (int idx = 0; idx < patternNum; idx++) {
      float Ku, Kv;
      if (!projectPoint(point->u + patternP[idx][0], point->v + patternP[idx][1], point->idepth_scaled, PRE_KRKiTll,
                        PRE_KtTll, Ku, Kv)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      projectedTo[idx][0] = Ku;
      projectedTo[idx][1] = Kv;


      Vec3f hitColor = (getInterpolatedElement33(dIl, Ku, Kv, wG[0]));
      float residual = hitColor[0] - (float) (affLL[0] * color[idx] + affLL[1]);

      if (!std::isfinite(affLL[0])) {
        LOG(INFO) << "host->aff_g2l() " << host->aff_g2l().a << ", " << host->aff_g2l().b;
        LOG(INFO) << "host->aff_g2l_r() " << host->aff_g2l_r().a << ", " << host->aff_g2l_r().b;
//        assert(std::isfinite(affLL[0]));
      }

      float drdA = (color[idx] - b0);
      if (!std::isfinite((float) hitColor[0])) {
        state_NewState = ResState::OOB;
        return state_energy;
      }


      float w = sqrtf(
          setting_outlierTHSumComponent / (setting_outlierTHSumComponent + hitColor.tail<2>().squaredNorm()));
      w = 0.5f * (w + weights[idx]);


      float hw = fabsf(residual) < setting_huberTH ? 1 : setting_huberTH / fabsf(residual);
      energyLeft += w * w * hw * residual * residual * (2 - hw);

      {
        if (hw < 1) hw = sqrtf(hw);
        hw = hw * w;

        hitColor[1] *= hw;
        hitColor[2] *= hw;

        J->resF[idx] = residual * hw;

        J->JIdx[0][idx] = hitColor[1];
        J->JIdx[1][idx] = hitColor[2];
        J->JabF[0][idx] = 0;
        J->JabF[1][idx] = 0;
        //- Right frame ab.
        //- {\partial r_{21}} \over {\partial a_{21}}
        J->JabF[2][idx] = -drdA * hw;
        //- {\partial r_{21}} \over {\partial b_{21}}
        J->JabF[3][idx] = -hw;

        JIdxJIdx_00 += hitColor[1] * hitColor[1];
        JIdxJIdx_11 += hitColor[2] * hitColor[2];
        JIdxJIdx_10 += hitColor[1] * hitColor[2];

        JabJIdx_00 += J->JabF[0][idx] * hitColor[1];
        JabJIdx_01 += J->JabF[0][idx] * hitColor[2];
        JabJIdx_10 += J->JabF[1][idx] * hitColor[1];
        JabJIdx_11 += J->JabF[1][idx] * hitColor[2];

        JabJIdx_20 += J->JabF[2][idx] * hitColor[1];
        JabJIdx_21 += J->JabF[2][idx] * hitColor[2];
        JabJIdx_30 += J->JabF[3][idx] * hitColor[1];
        JabJIdx_31 += J->JabF[3][idx] * hitColor[2];

        JabJab_00 += J->JabF[0][idx] * J->JabF[0][idx];
        JabJab_01 += J->JabF[0][idx] * J->JabF[1][idx];
        JabJab_11 += J->JabF[1][idx] * J->JabF[1][idx];

        JabJab_02 += J->JabF[0][idx] * J->JabF[2][idx];
        JabJab_03 += J->JabF[0][idx] * J->JabF[3][idx];
        JabJab_12 += J->JabF[1][idx] * J->JabF[2][idx];
        JabJab_13 += J->JabF[1][idx] * J->JabF[3][idx];
        JabJab_22 += J->JabF[2][idx] * J->JabF[2][idx];
        JabJab_23 += J->JabF[2][idx] * J->JabF[3][idx];
        JabJab_33 += J->JabF[3][idx] * J->JabF[3][idx];


        wJI2_sum += hw * hw * (hitColor[1] * hitColor[1] + hitColor[2] * hitColor[2]);

        if (setting_affineOptModeA < 0) {
          J->JabF[0][idx] = 0;
          J->JabF[2][idx] = 0;
        }
        if (setting_affineOptModeB < 0) {
          J->JabF[1][idx] = 0;
          J->JabF[3][idx] = 0;
        }

      }
    }

    J->JIdx2(0, 0) = JIdxJIdx_00;
    J->JIdx2(0, 1) = JIdxJIdx_10;
    J->JIdx2(1, 0) = JIdxJIdx_10;
    J->JIdx2(1, 1) = JIdxJIdx_11;
    J->JabJIdx(0, 0) = JabJIdx_00;
    J->JabJIdx(0, 1) = JabJIdx_01;
    J->JabJIdx(1, 0) = JabJIdx_10;
    J->JabJIdx(1, 1) = JabJIdx_11;
    J->JabJIdx(2, 0) = JabJIdx_20;
    J->JabJIdx(2, 1) = JabJIdx_21;
    J->JabJIdx(3, 0) = JabJIdx_30;
    J->JabJIdx(3, 1) = JabJIdx_31;
    J->Jab2(0, 0) = JabJab_00;
    J->Jab2(0, 1) = J->Jab2(1, 0) = JabJab_01;
    J->Jab2(1, 1) = JabJab_11;
    J->Jab2(0, 2) = J->Jab2(2, 0) = JabJab_02;
    J->Jab2(0, 3) = J->Jab2(3, 0) = JabJab_03;
    J->Jab2(1, 2) = J->Jab2(2, 1) = JabJab_12;
    J->Jab2(1, 3) = J->Jab2(3, 1) = JabJab_13;
    J->Jab2(2, 2) = JabJab_22;
    J->Jab2(2, 3) = J->Jab2(3, 2) = JabJab_23;
    J->Jab2(3, 3) = JabJab_33;

    if (energyLeft > std::max<float>(host->frameEnergyTH, target->frameEnergyTH) || wJI2_sum < 2) {
      energyLeft = std::max<float>(host->frameEnergyTH, target->frameEnergyTH);
      state_NewState = ResState::OUTLIER;
    }
    else {
      state_NewState = ResState::IN;
    }

    state_NewEnergy = energyLeft;
    return energyLeft;
  }

#endif
#if !defined(STEREO_MODE)

  double PointFrameResidual::linearize(CalibHessian *HCalib) {
    state_NewEnergyWithOutlier = -1;

    if (state_state == ResState::OOB) {
      state_NewState = ResState::OOB;
      return state_energy;
    }

    FrameFramePrecalc *precalc = &(host->targetPrecalc[target->idx]);
    float energyLeft = 0;
    const Eigen::Vector3f *dIl = target->dI;
    //const float* const Il = target->I;
    const Mat33f &PRE_KRKiTll = precalc->PRE_KRKiTll;
    const Vec3f &PRE_KtTll = precalc->PRE_KtTll;
    const Mat33f &PRE_RTll_0 = precalc->PRE_RTll_0;
    const Vec3f &PRE_tTll_0 = precalc->PRE_tTll_0;
    const float *const color = point->color;
    const float *const weights = point->weights;

    Vec2f affLL = precalc->PRE_aff_mode;
    float b0 = precalc->PRE_b0_mode;


    Vec6f d_xi_x, d_xi_y;
    Vec4f d_C_x, d_C_y;
    float d_d_x, d_d_y;
    {
      float drescale, u, v, new_idepth; // new_idepth = idepth*drescale;
      float Ku, Kv;
      Vec3f KliP;

      //- For current MACRO SCALE_IDEPTH, point->idepth_zero_scaled equals to idepth
      if (!projectPoint(point->u, point->v, point->idepth_zero_scaled, 0, 0, HCalib,
                        PRE_RTll_0, PRE_tTll_0, drescale, u, v, Ku, Kv, KliP, new_idepth)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      centerProjectedTo = Vec3f(Ku, Kv, new_idepth);

      //- http://www.cnblogs.com/JingeTU/p/8203606.html
      // diff d_idepth
      //- {\partial x_2} \over {\partial \rho_1}
      //- d_d_x, d_d_y: x for u, y for v, they are not part of idepth.
      // #define SCALE_IDEPTH 1.0f
      d_d_x = drescale * (PRE_tTll_0[0] - PRE_tTll_0[2] * u) * SCALE_IDEPTH * HCalib->fxl();
      d_d_y = drescale * (PRE_tTll_0[1] - PRE_tTll_0[2] * v) * SCALE_IDEPTH * HCalib->fyl();




      // diff calib
      //- {\partial x_2} \over {\partial \begin{bmatrix} f_x & f_y & c_x & c_y\end{bmatrix}}
      // drescale = new_idepth / idepth
      // #define SCALE_F 50.0f
      // #define SCALE_C 50.0f
      d_C_x[2] = drescale * (PRE_RTll_0(2, 0) * u - PRE_RTll_0(0, 0));
      d_C_x[3] = HCalib->fxl() * drescale * (PRE_RTll_0(2, 1) * u - PRE_RTll_0(0, 1)) * HCalib->fyli();
      d_C_x[0] = KliP[0] * d_C_x[2];
      d_C_x[1] = KliP[1] * d_C_x[3];

      d_C_y[2] = HCalib->fyl() * drescale * (PRE_RTll_0(2, 0) * v - PRE_RTll_0(1, 0)) * HCalib->fxli();
      d_C_y[3] = drescale * (PRE_RTll_0(2, 1) * v - PRE_RTll_0(1, 1));
      d_C_y[0] = KliP[0] * d_C_y[2];
      d_C_y[1] = KliP[1] * d_C_y[3];

      d_C_x[0] = (d_C_x[0] + u) * SCALE_F;
      d_C_x[1] *= SCALE_F;
      d_C_x[2] = (d_C_x[2] + 1) * SCALE_C;
      d_C_x[3] *= SCALE_C;

      d_C_y[0] *= SCALE_F;
      d_C_y[1] = (d_C_y[1] + v) * SCALE_F;
      d_C_y[2] *= SCALE_C;
      d_C_y[3] = (d_C_y[3] + 1) * SCALE_C;

      //- {\partial x_2} \over {\partial \xi_{21}}
      d_xi_x[0] = new_idepth * HCalib->fxl();
      d_xi_x[1] = 0;
      d_xi_x[2] = -new_idepth * u * HCalib->fxl();
      d_xi_x[3] = -u * v * HCalib->fxl();
      d_xi_x[4] = (1 + u * u) * HCalib->fxl();
      d_xi_x[5] = -v * HCalib->fxl();

      d_xi_y[0] = 0;
      d_xi_y[1] = new_idepth * HCalib->fyl();
      d_xi_y[2] = -new_idepth * v * HCalib->fyl();
      d_xi_y[3] = -(1 + v * v) * HCalib->fyl();
      d_xi_y[4] = u * v * HCalib->fyl();
      d_xi_y[5] = u * HCalib->fyl();
    }


    {
      J->Jpdxi[0] = d_xi_x;
      J->Jpdxi[1] = d_xi_y;

      J->Jpdc[0] = d_C_x;
      J->Jpdc[1] = d_C_y;

      J->Jpdd[0] = d_d_x;
      J->Jpdd[1] = d_d_y;

    }


    float JIdxJIdx_00 = 0, JIdxJIdx_11 = 0, JIdxJIdx_10 = 0;
    float JabJIdx_00 = 0, JabJIdx_01 = 0, JabJIdx_10 = 0, JabJIdx_11 = 0;
    float JabJab_00 = 0, JabJab_01 = 0, JabJab_11 = 0;

    float wJI2_sum = 0;

    for (int idx = 0; idx < patternNum; idx++) {
      float Ku, Kv;
      if (!projectPoint(point->u + patternP[idx][0], point->v + patternP[idx][1], point->idepth_scaled, PRE_KRKiTll,
                        PRE_KtTll, Ku, Kv)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      projectedTo[idx][0] = Ku;
      projectedTo[idx][1] = Kv;


      Vec3f hitColor = (getInterpolatedElement33(dIl, Ku, Kv, wG[0]));
      float residual = hitColor[0] - (float) (affLL[0] * color[idx] + affLL[1]);


      float drdA = (color[idx] - b0);
      if (!std::isfinite((float) hitColor[0])) {
        state_NewState = ResState::OOB;
        return state_energy;
      }


      float w = sqrtf(
          setting_outlierTHSumComponent / (setting_outlierTHSumComponent + hitColor.tail<2>().squaredNorm()));
      w = 0.5f * (w + weights[idx]);


      float hw = fabsf(residual) < setting_huberTH ? 1 : setting_huberTH / fabsf(residual);
      energyLeft += w * w * hw * residual * residual * (2 - hw);

      {
        if (hw < 1) hw = sqrtf(hw);
        hw = hw * w;

        hitColor[1] *= hw;
        hitColor[2] *= hw;

        J->resF[idx] = residual * hw;

        J->JIdx[0][idx] = hitColor[1];
        J->JIdx[1][idx] = hitColor[2];
        //- {\partial r_{21}} \over {\partial a_{21}}
        J->JabF[0][idx] = -drdA * hw;
        //- {\partial r_{21}} \over {\partial b_{21}}
        J->JabF[1][idx] = -hw;

        JIdxJIdx_00 += hitColor[1] * hitColor[1];
        JIdxJIdx_11 += hitColor[2] * hitColor[2];
        JIdxJIdx_10 += hitColor[1] * hitColor[2];

//        JabJIdx_00 += drdA * hw * hitColor[1];
//        JabJIdx_01 += drdA * hw * hitColor[2];
        JabJIdx_00 += J->JabF[0][idx] * hitColor[1];
        JabJIdx_01 += J->JabF[0][idx] * hitColor[2];
        JabJIdx_10 += J->JabF[1][idx] * hitColor[1];
        JabJIdx_11 += J->JabF[1][idx] * hitColor[2];

//        JabJab_00 += drdA * drdA * hw * hw;
//        JabJab_01 += drdA * hw * hw;
//        JabJab_11 += hw * hw;
        JabJab_00 += J->JabF[0][idx] * J->JabF[0][idx];
        JabJab_01 += J->JabF[0][idx] * J->JabF[1][idx];
        JabJab_11 += J->JabF[1][idx] * J->JabF[1][idx];

        wJI2_sum += hw * hw * (hitColor[1] * hitColor[1] + hitColor[2] * hitColor[2]);

        if (setting_affineOptModeA < 0) J->JabF[0][idx] = 0;
        if (setting_affineOptModeB < 0) J->JabF[1][idx] = 0;

      }
    }

    J->JIdx2(0, 0) = JIdxJIdx_00;
    J->JIdx2(0, 1) = JIdxJIdx_10;
    J->JIdx2(1, 0) = JIdxJIdx_10;
    J->JIdx2(1, 1) = JIdxJIdx_11;
    J->JabJIdx(0, 0) = JabJIdx_00;
    J->JabJIdx(0, 1) = JabJIdx_01;
    J->JabJIdx(1, 0) = JabJIdx_10;
    J->JabJIdx(1, 1) = JabJIdx_11;
    J->Jab2(0, 0) = JabJab_00;
    J->Jab2(0, 1) = JabJab_01;
    J->Jab2(1, 0) = JabJab_01;
    J->Jab2(1, 1) = JabJab_11;

    state_NewEnergyWithOutlier = energyLeft;

    if (energyLeft > std::max<float>(host->frameEnergyTH, target->frameEnergyTH) || wJI2_sum < 2) {
      energyLeft = std::max<float>(host->frameEnergyTH, target->frameEnergyTH);
      state_NewState = ResState::OUTLIER;
    }
    else {
      state_NewState = ResState::IN;
    }

    state_NewEnergy = energyLeft;
    return energyLeft;
  }

  double PointFrameResidual::linearizeStatic(CalibHessian *HCalib) {
    assert(false); //-- for MONO, no static residual
    assert(host->rightFrame == target);
    state_NewEnergyWithOutlier = -1;

    if (state_state == ResState::OOB) {
      state_NewState = ResState::OOB;
      return state_energy;
    }

    FrameFramePrecalc *precalc = &(host->targetPrecalc.back());
    float energyLeft = 0;
    const Eigen::Vector3f *dIl = target->dI;
    //const float* const Il = target->I;
    const Mat33f &PRE_KRKiTll = precalc->PRE_KRKiTll;
    const Vec3f &PRE_KtTll = precalc->PRE_KtTll;
    const Mat33f &PRE_RTll_0 = precalc->PRE_RTll_0;
    const Vec3f &PRE_tTll_0 = precalc->PRE_tTll_0;
    const float *const color = point->color;
    const float *const weights = point->weights;

    Vec2f affLL = precalc->PRE_aff_mode;
    float b0 = precalc->PRE_b0_mode;

//    std::cout << "PRE_tTll_0: \n";
//    std::cout << PRE_tTll_0 << std::endl;
//    std::cout << "PRE_RTll_0: \n";
//    std::cout << PRE_RTll_0 << std::endl;


//    Vec6f d_xi_x, d_xi_y;
    Vec4f d_C_x, d_C_y;
    float d_d_x, d_d_y;
    {
      float drescale, u, v, new_idepth; // new_idepth = idepth*drescale;
      float Ku, Kv;
      Vec3f KliP;

      //- For current MACRO SCALE_IDEPTH, point->idepth_zero_scaled equals to idepth
      if (!projectPoint(point->u, point->v, point->idepth_zero_scaled, 0, 0, HCalib,
                        PRE_RTll_0, PRE_tTll_0, drescale, u, v, Ku, Kv, KliP, new_idepth)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      centerProjectedTo = Vec3f(Ku, Kv, new_idepth);

      //- http://www.cnblogs.com/JingeTU/p/8203606.html
      // diff d_idepth
      //- {\partial x_2} \over {\partial \rho_1}
      //- d_d_x, d_d_y: x for u, y for v, they are not part of idepth.
      d_d_x = drescale * (PRE_tTll_0[0] - PRE_tTll_0[2] * u) * SCALE_IDEPTH * HCalib->fxl();
      d_d_y = drescale * (PRE_tTll_0[1] - PRE_tTll_0[2] * v) * SCALE_IDEPTH * HCalib->fyl();




      // diff calib
      //- {\partial x_2} \over {\partial \begin{bmatrix} f_x & f_y & c_x & c_y\end{bmatrix}}
      // drescale = new_idepth / idepth
      d_C_x[2] = drescale * (PRE_RTll_0(2, 0) * u - PRE_RTll_0(0, 0));
      d_C_x[3] = HCalib->fxl() * drescale * (PRE_RTll_0(2, 1) * u - PRE_RTll_0(0, 1)) * HCalib->fyli();
      d_C_x[0] = KliP[0] * d_C_x[2];
      d_C_x[1] = KliP[1] * d_C_x[3];

      d_C_y[2] = HCalib->fyl() * drescale * (PRE_RTll_0(2, 0) * v - PRE_RTll_0(1, 0)) * HCalib->fxli();
      d_C_y[3] = drescale * (PRE_RTll_0(2, 1) * v - PRE_RTll_0(1, 1));
      d_C_y[0] = KliP[0] * d_C_y[2];
      d_C_y[1] = KliP[1] * d_C_y[3];

      d_C_x[0] = (d_C_x[0] + u) * SCALE_F;
      d_C_x[1] *= SCALE_F;
      d_C_x[2] = (d_C_x[2] + 1) * SCALE_C;
      d_C_x[3] *= SCALE_C;

      d_C_y[0] *= SCALE_F;
      d_C_y[1] = (d_C_y[1] + v) * SCALE_F;
      d_C_y[2] *= SCALE_C;
      d_C_y[3] = (d_C_y[3] + 1) * SCALE_C;

    }


    {
      J->Jpdxi[0] = Vec6f::Zero();
      J->Jpdxi[1] = Vec6f::Zero();

      J->Jpdc[0] = d_C_x;
      J->Jpdc[1] = d_C_y;

      J->Jpdd[0] = d_d_x;
      J->Jpdd[1] = d_d_y;

    }


    float JIdxJIdx_00 = 0, JIdxJIdx_11 = 0, JIdxJIdx_10 = 0;
    float JabJIdx_00 = 0, JabJIdx_01 = 0, JabJIdx_10 = 0, JabJIdx_11 = 0;
    float JabJab_00 = 0, JabJab_01 = 0, JabJab_11 = 0;

    float wJI2_sum = 0;

    for (int idx = 0; idx < patternNum; idx++) {
      float Ku, Kv;
      if (!projectPoint(point->u + patternP[idx][0], point->v + patternP[idx][1], point->idepth_scaled, PRE_KRKiTll,
                        PRE_KtTll, Ku, Kv)) {
        state_NewState = ResState::OOB;
        return state_energy;
      }

      projectedTo[idx][0] = Ku;
      projectedTo[idx][1] = Kv;


      Vec3f hitColor = (getInterpolatedElement33(dIl, Ku, Kv, wG[0]));
      float residual = hitColor[0] - (float) (affLL[0] * color[idx] + affLL[1]);


      float drdA = (color[idx] - b0);
      if (!std::isfinite((float) hitColor[0])) {
        state_NewState = ResState::OOB;
        return state_energy;
      }


      float w = sqrtf(
          setting_outlierTHSumComponent / (setting_outlierTHSumComponent + hitColor.tail<2>().squaredNorm()));
      w = 0.5f * (w + weights[idx]);


      float hw = fabsf(residual) < setting_huberTH ? 1 : setting_huberTH / fabsf(residual);
      energyLeft += w * w * hw * residual * residual * (2 - hw);

      {
        if (hw < 1) hw = sqrtf(hw);
        hw = hw * w;

        hitColor[1] *= hw;
        hitColor[2] *= hw;

        J->resF[idx] = residual * hw;

        J->JIdx[0][idx] = hitColor[1];
        J->JIdx[1][idx] = hitColor[2];
        //- {\partial r_{21}} \over {\partial a_{21}}
        J->JabF[0][idx] = -drdA * hw;
        //- {\partial r_{21}} \over {\partial b_{21}}
        J->JabF[1][idx] = -hw;

        JIdxJIdx_00 += hitColor[1] * hitColor[1];
        JIdxJIdx_11 += hitColor[2] * hitColor[2];
        JIdxJIdx_10 += hitColor[1] * hitColor[2];

        JabJIdx_00 += J->JabF[0][idx] * hitColor[1];
        JabJIdx_01 += J->JabF[0][idx] * hitColor[2];
        JabJIdx_10 += J->JabF[1][idx] * hitColor[1];
        JabJIdx_11 += J->JabF[1][idx] * hitColor[2];

        JabJab_00 += J->JabF[0][idx] * J->JabF[0][idx];
        JabJab_01 += J->JabF[0][idx] * J->JabF[1][idx];
        JabJab_11 += J->JabF[1][idx] * J->JabF[1][idx];


        wJI2_sum += hw * hw * (hitColor[1] * hitColor[1] + hitColor[2] * hitColor[2]);

        if (setting_affineOptModeA < 0) J->JabF[0][idx] = 0;
        if (setting_affineOptModeB < 0) J->JabF[1][idx] = 0;

      }
    }

    J->JIdx2(0, 0) = JIdxJIdx_00;
    J->JIdx2(0, 1) = JIdxJIdx_10;
    J->JIdx2(1, 0) = JIdxJIdx_10;
    J->JIdx2(1, 1) = JIdxJIdx_11;
    J->JabJIdx(0, 0) = JabJIdx_00;
    J->JabJIdx(0, 1) = JabJIdx_01;
    J->JabJIdx(1, 0) = JabJIdx_10;
    J->JabJIdx(1, 1) = JabJIdx_11;
    J->Jab2(0, 0) = JabJab_00;
    J->Jab2(0, 1) = JabJab_01;
    J->Jab2(1, 0) = JabJab_01;
    J->Jab2(1, 1) = JabJab_11;

//    std::cout << "J->Jidx: " << J->JIdx2 << std::endl;

    state_NewEnergyWithOutlier = energyLeft;

    if (energyLeft > std::max<float>(host->frameEnergyTH, target->frameEnergyTH) || wJI2_sum < 2) {
      energyLeft = std::max<float>(host->frameEnergyTH, target->frameEnergyTH);
      state_NewState = ResState::OUTLIER;
    }
    else {
      state_NewState = ResState::IN;
    }

    state_NewEnergy = energyLeft;
    return energyLeft;
  }

#endif

  void PointFrameResidual::debugPlot() {
    if (state_state == ResState::OOB) return;
    Vec3b cT = Vec3b(0, 0, 0);

    if (freeDebugParam5 == 0) {
      float rT = 20 * sqrt(state_energy / 9);
      if (rT < 0) rT = 0;
      if (rT > 255)rT = 255;
      cT = Vec3b(0, 255 - rT, rT);
    }
    else {
      if (state_state == ResState::IN) cT = Vec3b(255, 0, 0);
      else if (state_state == ResState::OOB) cT = Vec3b(255, 255, 0);
      else if (state_state == ResState::OUTLIER) cT = Vec3b(0, 0, 255);
      else cT = Vec3b(255, 255, 255);
    }

    for (int i = 0; i < patternNum; i++) {
      if ((projectedTo[i][0] > 2 && projectedTo[i][1] > 2 && projectedTo[i][0] < wG[0] - 3 &&
           projectedTo[i][1] < hG[0] - 3))
        target->debugImage->setPixel1((float) projectedTo[i][0], (float) projectedTo[i][1], cT);
    }
  }


  void PointFrameResidual::applyRes(bool copyJacobians) {
    if (copyJacobians) {
      if (state_state == ResState::OOB) {
        assert(!efResidual->isActiveAndIsGoodNEW);
        return;  // can never go back from OOB
      }
      if (state_NewState == ResState::IN)// && )
      {
        efResidual->isActiveAndIsGoodNEW = true;
        efResidual->takeDataF();
      }
      else {
        efResidual->isActiveAndIsGoodNEW = false;
      }
    }

    setState(state_NewState);
    state_energy = state_NewEnergy;
  }

#if defined(STEREO_MODE) && defined(INERTIAL_MODE)

  IMUResidual::IMUResidual(SpeedAndBiasHessian *from_sb_, SpeedAndBiasHessian *to_sb_,
                           FrameHessian *from_f_, FrameHessian *to_f_,
                           std::vector<IMUMeasurement> &imu_data_) :
      from_sb(from_sb_), to_sb(to_sb_), from_f(from_f_), to_f(to_f_),
      t0_(from_f_->shell->timestamp), t1_(to_f_->shell->timestamp),
      imuData(std::move(imu_data_)) {
    J = new RawIMUResidualJacobian();
    efIMUResidual = 0;
    redo_ = true;
  }

  IMUResidual::~IMUResidual() {
    delete J;
  }

  void IMUResidual::applyRes(bool copyJacobians) {
    if (copyJacobians) {
      efIMUResidual->takeDataF();
    }
  }

  int IMUResidual::redoPreintegration(const SE3 &T_WS, SpeedAndBias &speedAndBias, IMUParameters *imuParameters) const {
    std::lock_guard<std::mutex> lock(preintegrationMutex_);

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
//    Delta_q_ = Eigen::Quaterniond(1, 0, 0, 0);
//    C_integral_ = Eigen::Matrix3d::Zero();
//    C_doubleintegral_ = Eigen::Matrix3d::Zero();
//    acc_integral_ = Eigen::Vector3d::Zero();
//    acc_doubleintegral_ = Eigen::Vector3d::Zero();

    // cross matrix accumulatrion
//    cross_ = Eigen::Matrix3d::Zero();

    // sub-Jacobians
    d_R_d_bg_ = Eigen::Matrix3d::Zero();
    d_p_d_bg_ = Eigen::Matrix3d::Zero();
    d_p_d_ba_ = Eigen::Matrix3d::Zero();
    d_v_d_bg_ = Eigen::Matrix3d::Zero();
    d_v_d_ba_ = Eigen::Matrix3d::Zero();
//    dalpha_db_g_ = Eigen::Matrix3d::Zero();
//    dv_db_g_ = Eigen::Matrix3d::Zero();
//    dp_db_g_ = Eigen::Matrix3d::Zero();
//
    // the Jacobian of the increment (w/o biases)
    Sigma_ij_ = Eigen::Matrix<double, 15, 15>::Zero();
//    P_delta_ = Eigen::Matrix<double, 15, 15>::Zero();

    //Eigen::Matrix<double, 15, 15> F_tot;
    //F_tot.setIdentity();

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
      Eigen::Vector3d Delta_tilde_p_ij =
          Delta_tilde_p_ij_ + Delta_tilde_v_ij_ * dt + 0.5 * Delta_tilde_R_ij_ * acc_S_true * dt * dt;

      // jacobian propagation
      d_R_d_bg_ += -Delta_tilde_R_ij * okvis::kinematics::rightJacobian(omega_S_true * dt) * dt; //- ?
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

      ++i;

      if (nexttime == t1_)
        break;

    }

    // store the reference (linearisation) point
    speedAndBiases_ref_ = speedAndBias;

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

  double IMUResidual::linearize(IMUParameters *imuParameters) {

    SE3 T_WS_0 = from_f->PRE_T_CW.inverse();
    SE3 T_WS_1 = to_f->PRE_T_CW.inverse();
    SpeedAndBias speedAndBiases_0 = from_f->speedAndBiasHessian->get_state();
    SpeedAndBias speedAndBiases_1 = to_f->speedAndBiasHessian->get_state();

//    SE3 T_WS_0 = from_f->get_worldToCam_evalPT().inverse();
//    SE3 T_WS_1 = to_f->get_worldToCam_evalPT().inverse();
//    SpeedAndBias speedAndBiases_0 = from_f->speedAndBiasHessian->speedAndBias_evalPT;
//    SpeedAndBias speedAndBiases_1 = to_f->speedAndBiasHessian->speedAndBias_evalPT;

    // this will NOT be changed:
    Eigen::Vector3d t_S0 = T_WS_0.translation();
    Eigen::Vector3d t_S1 = T_WS_1.translation();
    const Eigen::Matrix3d C_WS_0 = T_WS_0.rotationMatrix();
    const Eigen::Matrix3d C_S0_W = C_WS_0.transpose();
    const Eigen::Matrix3d C_WS_1 = T_WS_1.rotationMatrix();
    const Eigen::Matrix3d C_S1_W = C_WS_1.transpose();

    // call the propagation
    const double Delta_t = (t1_ - t0_);
    Eigen::Matrix<double, 6, 1> Delta_b;

    {
      std::lock_guard<std::mutex> lock(preintegrationMutex_);
      Delta_b = speedAndBiases_0.tail<6>() - speedAndBiases_ref_.tail<6>();
    }

    redo_ = redo_ || (Delta_b.head<3>().norm() * Delta_t > 0.0001);
    if (redo_) {
      redoPreintegration(T_WS_0, speedAndBiases_0, imuParameters);
      redoCounter_++;
      Delta_b.setZero();
      redo_ = false;
    }

    {
      std::lock_guard<std::mutex> lock(preintegrationMutex_);
      const Eigen::Vector3d g_W = imuParameters->g * Eigen::Vector3d(0, 0, 6371009).normalized();

      // the overall error vector
      Eigen::Matrix<double, 15, 1> error;
      error.segment<3>(0) =
          C_S0_W * (t_S1 - t_S0 - speedAndBiases_0.head<3>() * Delta_t - 0.5 * g_W * Delta_t * Delta_t)
          - (Delta_tilde_p_ij_ + d_p_d_bg_ * Delta_b.head<3>() + d_p_d_ba_ * Delta_b.tail<3>()); // p
      error.segment<3>(3) = SO3::log(SO3((Delta_tilde_R_ij_ *
                                          SO3::exp(d_R_d_bg_ * Delta_b.head<3>()).matrix()).transpose() * C_S0_W *
                                         C_WS_1)); // R
      error.segment<3>(6) = C_S0_W * (speedAndBiases_1.head<3>() - speedAndBiases_0.head<3>() - g_W * Delta_t)
                            - (Delta_tilde_v_ij_ + d_v_d_bg_ * Delta_b.head<3>() + d_v_d_ba_ * Delta_b.tail<3>()); // v
      error.tail<6>() = speedAndBiases_1.tail<6>() - speedAndBiases_0.tail<6>();

      // assign Jacobian w.r.t. x0
      Eigen::Matrix<double, 15, 15> F0 =
          Eigen::Matrix<double, 15, 15>::Zero(); // holds for d/db_g, d/db_a
      F0.block<3, 3>(0, 0) = -C_S0_W; // p/p
      F0.block<3, 3>(0, 3) = C_S0_W * okvis::kinematics::crossMx((t_S1 - t_S0 - speedAndBiases_0.head<3>() * Delta_t
                                                                  - 0.5 * g_W * Delta_t * Delta_t)); // p/R
      F0.block<3, 3>(0, 6) = -C_S0_W * Delta_t; // p/v
      F0.block<3, 3>(0, 9) = -d_p_d_bg_; // p/bg
      F0.block<3, 3>(0, 12) = -d_p_d_ba_; // p/ba
      F0.block<3, 3>(3, 3) = -okvis::kinematics::rightJacobian(error.segment<3>(3)).inverse() * C_S1_W; // R/R
      F0.block<3, 3>(3, 9) = -okvis::kinematics::rightJacobian(-error.segment<3>(3)).inverse() // R/v
                             * okvis::kinematics::rightJacobian(d_R_d_bg_ * Delta_b.head<3>()) *
                             d_R_d_bg_; // R/bg J_l(\phi) = J_r(-\phi)
      F0.block<3, 3>(6, 3) = C_S0_W * okvis::kinematics::crossMx(speedAndBiases_1.head<3>() - speedAndBiases_0.head<3>()
                                                                 - g_W * Delta_t); // v/R
      F0.block<3, 3>(6, 6) = -C_S0_W; // v/v
      F0.block<3, 3>(6, 9) = -d_v_d_bg_; // v/bg
      F0.block<3, 3>(6, 12) = -d_v_d_ba_; // v/ba
      F0.block<3, 3>(9, 9) = Eigen::Matrix3d::Identity(); // bg/bg
      F0.block<3, 3>(12, 12) = Eigen::Matrix3d::Identity(); // ba/ba

      // assign Jacobian w.r.t. x1
      Eigen::Matrix<double, 15, 15> F1 =
          Eigen::Matrix<double, 15, 15>::Zero(); // holds for the biases
      F1.block<3, 3>(0, 0) = C_S0_W; // p/p
      F1.block<3, 3>(3, 3) = okvis::kinematics::rightJacobian(error.segment<3>(3)).inverse() * C_S1_W; // R/R
      F1.block<3, 3>(6, 6) = C_S0_W; // v/v
      F1.block<3, 3>(9, 9) = -Eigen::Matrix3d::Identity(); // bg/bg
      F1.block<3, 3>(12, 12) = -Eigen::Matrix3d::Identity(); // ba/ba

      // error weighting
      J->resF = setting_imuResidualWeight * (squareRootInformation_ * error).cast<float>();
      J->Jrdxi[0] = setting_imuResidualWeight * (squareRootInformation_ * F0.block<15, 6>(0, 0)).cast<float>();
      J->Jrdsb[0] = setting_imuResidualWeight * (squareRootInformation_ * F0.block<15, 9>(0, 6)).cast<float>();
      J->Jrdxi[1] = setting_imuResidualWeight * (squareRootInformation_ * F1.block<15, 6>(0, 0)).cast<float>();
      J->Jrdsb[1] = setting_imuResidualWeight * (squareRootInformation_ * F1.block<15, 9>(0, 6)).cast<float>();
    }
    state_NewEnergy = setting_imuResidualWeight * J->resF.norm();

    return state_NewEnergy;
  }

#endif
}
