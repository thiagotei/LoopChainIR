/*! ****************************************************************************
\file AutomaticShiftTransformation.hpp
\authors Ian J. Bertolacci

\brief
Shift all loops by an amount determined by the ILP solver.
Used prior to fusion.

\copyright
Copyright 2015-2016 Colorado State University
Copyright 2017 Universiy of Arizona
*******************************************************************************/

#ifndef AUTOMATIC_SHIFT_TRANSFORMATION_HPP
#define AUTOMATIC_SHIFT_TRANSFORMATION_HPP

#include <LoopChainIR/LoopChain.hpp>
#include <LoopChainIR/Transformation.hpp>
#include <LoopChainIR/ShiftTransformation.hpp>
#include <LoopChainIR/Accesses.hpp>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include "stdio.h"

namespace LoopChainIR {


  class AutomaticShiftTransformation : public Transformation {

  public:
    AutomaticShiftTransformation();

    /*!
    \brief
    Generate ISCC code for the shift transformation, and append it to the
    transformation list of schedule (modifies schedule).

    \param[inout] schedule Schedule this transformation is being applied to.

    \returns
    The ISCC code as a string
    */
    std::vector<std::string> apply( Schedule& schedule, Subspace* subspace );

    /*!
    \brief
    Generate ISCC code for the shift transformation, and append it to the
    transformation list of schedule (modifies schedule).

    \param[inout] schedule Schedule this transformation is being applied to.

    \returns
    The ISCC code as a string
    */
    std::vector<std::string> apply( Schedule& schedule );

    public:
      static std::vector<ShiftTransformation*> computeShiftForFusion( Subspace::size_type dimensions, LoopChain chain, bool include_zero_tuple = false );
      static std::map<LoopChain::size_type, Tuple> computeShiftTuplesForFusion( Subspace::size_type dimensions, LoopChain chain, bool include_zero_tuple = false  );

  };

}

#endif
