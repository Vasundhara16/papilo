/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*               This file is part of the program and library                */
/*    PaPILO --- Parallel Presolve for Integer and Linear Optimization       */
/*                                                                           */
/* Copyright (C) 2020-2022 Konrad-Zuse-Zentrum                               */
/*                     fuer Informationstechnik Berlin                       */
/*                                                                           */
/* This program is free software: you can redistribute it and/or modify      */
/* it under the terms of the GNU Lesser General Public License as published  */
/* by the Free Software Foundation, either version 3 of the License, or      */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU Lesser General Public License for more details.                       */
/*                                                                           */
/* You should have received a copy of the GNU Lesser General Public License  */
/* along with this program.  If not, see <https://www.gnu.org/licenses/>.    */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "papilo/core/Objective.hpp"
#include "papilo/core/Presolve.hpp"
#include "papilo/misc/MultiPrecision.hpp"
#include "papilo/misc/OptionsParser.hpp"
#include "papilo/misc/VersionLogger.hpp"

#include <boost/program_options.hpp>
#include <cassert>
#include <fstream>

using namespace papilo;

// TODO: Probing does only work with boolean values

template <typename REAL>
class FixAndPropagate
{
 public:

   void
   fix_and_propagate( const Problem<REAL>& _problem, const Num<REAL>& _num,
                      ProbingView<REAL>& probing_view )
   {
      papilo::Vec<Fixing<REAL>> fixings{};
      while( true )
      {
         probing_view.reset();
         // TODO: this means recalculating all propagations. Makes that sense or is reverting the propagation better?
         if( !fixings.empty() )
         {
            for( auto& fixing : fixings )
               probing_view.setProbingColumn( fixing.get_column_index(),
                                              fixing.get_value() );
         }

         propagate_to_leaf_or_infeasibility( _problem, _num, probing_view );

         if( probing_view.isInfeasible() )
         {
            // TODO: store fixings since they code the conflict
            fixings = probing_view.get_fixings();
            assert( !fixings.empty() );
            Fixing<REAL> infeasible_fixing = fixings[fixings.size() - 1];
            fixings[fixings.size() - 1] =
                ( Fixing<REAL> ){ infeasible_fixing.get_column_index(),
                                  modify_value_due_to_backtrack(
                                      infeasible_fixing.get_value() ) };
         }
         else
         {
            // TODO: store objective value and solution
            Solution<REAL> solution = create_solution( probing_view );
            fmt::print( "found solution {}",
                        _problem.computeSolObjective( solution.primal ) );
            break;
         }
      }
   }

 private:
   double
   modify_value_due_to_backtrack( double value )
   {
      return value == 1 ? 0 : 1;
   }

   Solution<REAL>
   create_solution( const ProbingView<REAL>& _view )
   {
      Vec<REAL> values{};
      for( int i = 0; i < _view.getProbingUpperBounds().size(); i++ )
      {
         assert( _view.getProbingUpperBounds()[i] ==
                 _view.getProbingLowerBounds()[i] );
         values.push_back( _view.getProbingUpperBounds()[i] );
      }
      Solution<REAL> solution{ SolutionType::kPrimal, values };
      return solution;
   }

   void
   propagate_to_leaf_or_infeasibility( const Problem<REAL>& _problem,
                                       const Num<REAL>& _num,
                                       ProbingView<REAL>& probing_view )
   {
      while( true )
      {
         Fixing<REAL> fixing = select_diving_variable( _problem, probing_view );
         // dive until all vars are fixed (and returned fixing is invalid)
         if( fixing.is_invalid() )
            return;

         fmt::print( "{} {}\n", fixing.get_column_index(), fixing.get_value() );

         probing_view.setProbingColumn( fixing.get_column_index(),
                                        fixing.get_value() == 1 );
         probing_view.propagateDomains();
         probing_view.storeImplications();
         if( probing_view.isInfeasible() )
         {
            fmt::print( "infeasible\n" );
            return;
         }
      }
   }

   Fixing<REAL>
   select_diving_variable( const Problem<REAL>& _problem,
                           const ProbingView<REAL>& _probing_view )
   {

      // TODO: currently a draft
      for( int i = 0; i < _problem.getNCols(); i++ )
      {
         _probing_view.getProbingUpperBounds();
         if( _probing_view.getProbingUpperBounds()[i] !=
             _probing_view.getProbingLowerBounds()[i] )
         {
            return { i, 0 };
         }
      }
      return { -1, -1 };
   }
};