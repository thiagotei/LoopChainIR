#include <LoopChainIR/AutomaticShiftTransformation.hpp>
#include <ortools/linear_solver/linear_solver.h>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <list>
#include <limits>
#include <algorithm>

using namespace std;
using namespace LoopChainIR;
using namespace operations_research;

AutomaticShiftTransformation::AutomaticShiftTransformation(){ }

vector<string> AutomaticShiftTransformation::apply( Schedule& schedule ){
  return this->apply( schedule, *(next(schedule.getSubspaceManager().get_iterator_to_loops())));
}

vector<string> AutomaticShiftTransformation::apply( Schedule& schedule, Subspace* subspace ){
  vector<string> transformations;

  vector<ShiftTransformation*> shift_transformations = this->computeShiftForFusion( subspace->size() , schedule.getChain() );
  for( ShiftTransformation* shift : shift_transformations ){
    vector<string> shifts = shift->apply( schedule );
    transformations.insert( transformations.end(), shifts.begin(), shifts.end() );
  }

  return transformations;
}


map<LoopChain::size_type, Tuple> AutomaticShiftTransformation::computeShiftTuplesForFusion( Subspace::size_type dimensions, LoopChain chain, bool include_zero_tuple ) {
  #if defined USE_SCIP
    MPSolver::OptimizationProblemType optimizationProblemType = MPSolver::SCIP_MIXED_INTEGER_PROGRAMMING;
  #elif defined USE_GLPK
    MPSolver::OptimizationProblemType optimizationProblemType = MPSolver::GLPK_MIXED_INTEGER_PROGRAMMING;
  #elif defined USE_CBC
    MPSolver::OptimizationProblemType optimizationProblemType = MPSolver::CBC_MIXED_INTEGER_PROGRAMMING;
  #elif defined USE_SLM
    MPSolver::OptimizationProblemType optimizationProblemType = MPSolver::SULUM_MIXED_INTEGER_PROGRAMMING;
  #elif defined USE_GUROBI
    MPSolver::OptimizationProblemType optimizationProblemType = MPSolver::GUROBI_MIXED_INTEGER_PROGRAMMING;
  #elif defined USE_CPLEX
    MPSolver::OptimizationProblemType optimizationProblemType = MPSolver::CPLEX_MIXED_INTEGER_PROGRAMMING;
  #else
    #error "Google Optimization Tools has no proper backend!"
  #endif

  MPSolver solver("ShiftSolver", optimizationProblemType);

  const double infinity = solver.infinity();

  map< LoopChain::size_type, vector<MPVariable*>* > shift_variables;
  map< pair<MPVariable*,MPVariable*>, int> max_difference_map;

  MPObjective* objective = solver.MutableObjective();
  vector< MPConstraint* > constraints;

  map< LoopChain::size_type, map<string, Dataspace> > dataspaces;
  map< LoopChain::size_type, set<string> > dataspace_names;

  for( LoopChain::size_type nest_idx = 0; nest_idx < chain.length(); ++nest_idx ){
    // Make shift tuple for this nest
    vector<MPVariable*>* nest_shifts = new vector<MPVariable*>();
    solver.MakeIntVarArray( dimensions,
                            -infinity, infinity,
                            SSTR("shift_" << nest_idx << "_"),
                            nest_shifts
                          );
    shift_variables[nest_idx] = nest_shifts;

    // Setup objective initial coeffecients
    for( MPVariable* variable : *nest_shifts ){
      objective->SetCoefficient( variable, 1 );
    }

    list<Dataspace> nest_unnamed_dataspaces = chain.getNest(nest_idx).getDataspaces();
    map< string, Dataspace > nest_dataspaces;
    set<string> nest_dataspace_names;

    for( Dataspace dataspace : nest_unnamed_dataspaces ){
      nest_dataspace_names.insert( dataspace.name );
      nest_dataspaces.emplace( dataspace.name, dataspace );
    }

    dataspaces[nest_idx] = nest_dataspaces;
    dataspace_names[nest_idx] = nest_dataspace_names;

    // Doing things with respect to the previous nests
    for( LoopChain::size_type previous_idx = 0; previous_idx < nest_idx; ++previous_idx ){
      vector<MPVariable*>* previous_shifts = shift_variables[previous_idx];

      // Setup constraints: c_ywd - C_xwd <= S_yd - S_xd
      map<string, Dataspace> previous_dataspaces = dataspaces[previous_idx];

      set<string> previous_dataspace_names;
      for( map<string, Dataspace>::iterator iter = previous_dataspaces.begin(); iter != previous_dataspaces.end(); ++iter ){
        previous_dataspace_names.insert( iter->first );
      }

      // Determine what dataspaces the pair of loops share
      vector<string> common( previous_dataspace_names.size() + nest_dataspace_names.size(), "" );

      vector<string>::iterator last_insert = set_intersection(
                            previous_dataspace_names.begin(), previous_dataspace_names.end(),
                            nest_dataspace_names.begin(), nest_dataspace_names.end(),
                            common.begin()
                          );

      common.resize( last_insert - common.begin() );

      // pair of loops do not share any dataspaces
      if( common.size() < 1 ){
        continue;
      }

      auto calc_func = [&](TupleCollection nest_collection, TupleCollection prev_collection) {
        for( Tuple nest_access_tuple : nest_collection ){
          for( Tuple previous_access_tuple : prev_collection ){
            for( Subspace::size_type d = 0; d < dimensions; ++d ){
              // Calculate constant lower bound
              int difference = nest_access_tuple[d] - previous_access_tuple[d];
              // Construct constraint constant <= nest_shift_d - prev_shift

              auto map_index = make_pair( previous_shifts->at(d), nest_shifts->at(d) );
              if( max_difference_map.find(map_index) == max_difference_map.end() ){
                max_difference_map[map_index] = difference;
              } else {
                max_difference_map[map_index] = max( max_difference_map[map_index], difference );
              }

            } // for d
          } // for previous_access_tuple
        } // for nest_access_tuple

      }; // lambda function calc_func

      for( string name : common ){

        Dataspace nest_dataspace = nest_dataspaces.find(name)->second;
        Dataspace previous_dataspace = previous_dataspaces.find(name)->second;

        // Writes - writes
        calc_func( nest_dataspace.writes(), previous_dataspace.writes() );

        // Writes - Reads
        calc_func( nest_dataspace.writes(), previous_dataspace.reads() );

        // Reads - Writes
        calc_func( nest_dataspace.reads(), previous_dataspace.writes() );

      } // for( name : common )
    } // for( previous_idx )
  } // for( nest_idx )

  for( map< pair<MPVariable*,MPVariable*>, int >::value_type key_value : max_difference_map ){
    MPVariable* prev_shift = key_value.first.first;
    MPVariable* next_shift = key_value.first.second;
    int max_difference = key_value.second;
    MPConstraint* constraint = solver.MakeRowConstraint( max_difference, max_difference );
    constraint->SetCoefficient( next_shift,  1 );
    constraint->SetCoefficient( prev_shift, -1 );
    constraints.push_back( constraint );
  }

  for( LoopChain::size_type nest_idx = 0; nest_idx < chain.length(); ++nest_idx ){
    vector<MPVariable*>* nests_shifts = shift_variables[nest_idx];
    for( Subspace::size_type d = 0; d < dimensions; ++d  ){
      MPVariable* shift = nests_shifts->at( d );
      shift->SetLB( 0 );
    }
  }

  map<LoopChain::size_type, Tuple> shift_tuples;

  if( constraints.size() > 0 ){
    objective->SetMinimization();

    MPSolver::ResultStatus result_status = solver.Solve();

    // Check that the problem has an optimal solution.
    if( result_status != MPSolver::OPTIMAL ) {
      ostringstream error_stream;
      error_stream << "The problem does not have an optimal solution!" << endl
                   << "it is ";
      switch( result_status ){
        case MPSolver::FEASIBLE : { error_stream << " FEASIBLE." << endl; break; }
        case MPSolver::INFEASIBLE : { error_stream << " INFEASIBLE." << endl; break; }
        case MPSolver::UNBOUNDED : { error_stream << " UNBOUNDED." << endl; break; }
        case MPSolver::ABNORMAL : { error_stream << " ABNORMAL." << endl; break; }
        case MPSolver::MODEL_INVALID : { error_stream << " MODEL_INVALID." << endl; break; }
        case MPSolver::NOT_SOLVED : { error_stream << " NOT_SOLVED." << endl; break; }
        default:
          error_stream << " some unlisted status: " << result_status << endl;
      }
      assertWithException( result_status == MPSolver::OPTIMAL, error_stream.str() );
    } else {

      for( map< LoopChain::size_type, vector<MPVariable*>* >::iterator it = shift_variables.begin();
            it != shift_variables.end();
            ++it ){
        LoopChain::size_type nest_id = it->first;
        vector<MPVariable*>* nests_shifts = it->second;
        vector<int> extents;

        for( MPVariable* var : *nests_shifts ){
          extents.push_back( (int) var->solution_value() );
        }

        Tuple tuple( extents );

        if( include_zero_tuple || !tuple.isEmptyTuple() ){
          shift_tuples.emplace( nest_id, tuple );
        }
      }
    }
  }

  return shift_tuples;
}

vector<ShiftTransformation*> AutomaticShiftTransformation::computeShiftForFusion( Subspace::size_type dimensions, LoopChain chain, bool include_zero_tuple ){
  map<LoopChain::size_type, Tuple> shift_tuples = computeShiftTuplesForFusion( dimensions, chain, include_zero_tuple );
  vector<ShiftTransformation*> transformations;

  for( map<LoopChain::size_type, Tuple>::value_type key_value : shift_tuples ){
    LoopChain::size_type nest_id = key_value.first;
    Tuple tuple = key_value.second;
    transformations.push_back( new ShiftTransformation( nest_id, tuple ) );
  }

  return transformations;
}
