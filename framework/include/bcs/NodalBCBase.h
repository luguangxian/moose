//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#ifndef NODALBCBASE_H
#define NODALBCBASE_H

#include "BoundaryCondition.h"
#include "RandomInterface.h"
#include "CoupleableMooseVariableDependencyIntermediateInterface.h"

// Forward declarations
class NodalBCBase;

// libMesh forward declarations
namespace libMesh
{
template <typename T>
class NumericVector;
}

template <>
InputParameters validParams<NodalBCBase>();

/**
 * Base class for deriving any boundary condition that works at nodes
 */
class NodalBCBase : public BoundaryCondition,
                    public RandomInterface,
                    public CoupleableMooseVariableDependencyIntermediateInterface
{
public:
  NodalBCBase(const InputParameters & parameters);

  virtual void computeResidual(NumericVector<Number> & residual) = 0;
  virtual void computeJacobian() = 0;
  virtual void computeOffDiagJacobian(unsigned int jvar) = 0;

  void setBCOnEigen(bool iseigen) { _is_eigen = iseigen; }

protected:
  /// The aux variables to save the residual contributions to
  bool _has_save_in;
  std::vector<MooseVariableFE *> _save_in;
  std::vector<AuxVariableName> _save_in_strings;

  /// The aux variables to save the diagonal Jacobian contributions to
  bool _has_diag_save_in;
  std::vector<MooseVariableFE *> _diag_save_in;
  std::vector<AuxVariableName> _diag_save_in_strings;

  /// Indicate whether or not the boundary condition is applied to the right
  /// hand side of eigenvalue problems
  bool _is_eigen;
};

#endif /* NODALBCBASE_H */
