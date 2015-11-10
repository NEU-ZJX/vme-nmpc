/* This file is part of vme-nmpc.
 *
 * Copyright (C) 2015 Timothy A.V. Teatro - All rights Reserved
 *
 * vme-nmpc is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * vme-nmpc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * vme-nmpc. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VME_NMPC_SRC_NMPCENGINE_HPP_
#define VME_NMPC_SRC_NMPCENGINE_HPP_

#include "linear.hpp"
#include "Subject.hpp"
#include "VMeCommand.hpp"
#include "NmpcMinimizer.hpp"
#include "NmpcModel.hpp"
#include "VMeLogger.hpp"
#include "VMeNmpcInitPkg.hpp"

class NmpcMinimizer;

using vMeModel = NmpcModel<xyth, fp_point2d, up_VMeCommand>;

class VMeNmpcEngine : public Subject {
  NmpcModel<xyth, fp_point2d, up_VMeCommand>* model;
  NmpcMinimizer* minimizer;
  VMeLogger* logger;
  std::unique_ptr<VMeLogger> noOpLogger{nullptr}; // Default log if none given.

  fptype targetDistanceTolerance{0.1};
  unsigned cmdsExecutedFromCurrentHorizon{0};
  bool machineIsHalted{true};

 public:
  fp_point2d currentTarget;

  VMeNmpcEngine(VMeNmpcInitPkg&);
  void setTarget(fp_point2d point);
  up_VMeCommand nextCommand();
  void seed(xyth, fp_point2d);
  void seed(xyth);
  bool isHalted();
  vMeModel* _getModelPointer_();
  NmpcMinimizer* _getMinimizerPointer_();
};

class MinimizerReachedIterationLimit : public std::exception {};

#endif  // VME_NMPC_SRC_NMPCENGINE_HPP_