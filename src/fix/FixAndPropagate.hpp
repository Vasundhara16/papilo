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

#ifndef FIX_FIX_AND_PROPAGATE_HPP
#define FIX_FIX_AND_PROPAGATE_HPP

#include "papilo/core/Objective.hpp"
#include "papilo/core/Presolve.hpp"
#include "papilo/core/ProbingView.hpp"

#include "fix/strategy/RoundingStrategy.hpp"
#include "papilo/io/MpsParser.hpp"
#include <cassert>
#include <fstream>
#include <string>

using namespace papilo;

/***
 * This class performs a fix-and-propagate algorithm:
 *
 * V = all integer variables with non integer solution and proposed value is
 within bounds
 * propagate domains does not propagate violated rows
 *
 * while V is not empty
 *  max, var_max, val_max = max_{ v in V} score  (defined by strategy)
 *  fix var_max to value val_max
 *  propagate domains
 *  if perform_backtrack:
 *      [if propagation or fixing is infeasible to backtrack by fixing var_max
 to val_max +/-1]
 *      [if this is still infeasible then perform no more backtracks]
 *
 * for all non fixed variables v
 *  if lb_v < sol(v) < ub_v
 *      fix v to sol(v)
 *  else lb_v > sol(v)
 *      fix v to lb_v
 *  else
 *      fix v to ub_v
 *  propagate domains
 *
 * @tparam REAL the arithmetic parameter
 */
template <typename REAL>
class FixAndPropagate
{
   Message msg;
   Num<REAL> num;

 public:
   FixAndPropagate( Message msg_, Num<REAL> num_ ) : msg( msg_ ), num( num_ ) {}

   bool
   fix_and_propagate( const Vec<REAL>& cont_solution, Vec<REAL>& result,
                      RoundingStrategy<REAL>& strategy,
                      ProbingView<REAL>& probing_view,
                      int& successful_backtracks, bool perform_backtracking,
                      bool stop_at_infeasibility )
   {
      probing_view.reset();
      // if no backtrack just "dive" to the node whether it is infeasible or not
      if( !perform_backtracking )
      {
         propagate_to_leaf_or_infeasibility(
             cont_solution, strategy, stop_at_infeasibility, probing_view );
         if( stop_at_infeasibility && probing_view.isInfeasible() )
            return true;
         fix_remaining_integer_solutions( cont_solution, probing_view );
         create_solution( result, probing_view );
         return probing_view.isInfeasible();
      }
      while( true )
      {
         propagate_to_leaf_or_infeasibility( cont_solution, strategy, true,
                                             probing_view );

         if( probing_view.isInfeasible() )
         {
            assert( perform_backtracking );
            msg.detailed( "backtracking\n" );
            Vec<Fixing<REAL>> fixings = probing_view.get_fixings();
            assert( !fixings.empty() );
            Fixing<REAL> last_fix = fixings[fixings.size() - 1];

            probing_view.reset();
            for( int i = 0; i < fixings.size() - 1; i++ )
            {
               probing_view.setProbingColumn( fixings[i].get_column_index(),fixings[i].get_value() );
               perform_probing_step( probing_view );
            }
            // TODO: this is not necessary
            probing_view.setProbingColumn(
                last_fix.get_column_index(),
                modify_value_due_to_backtrack(
                    last_fix.get_value(),
                    cont_solution[last_fix.get_column_index()] ) );
            bool infeasible = perform_probing_step( probing_view );
            if( infeasible )
            {
               if( stop_at_infeasibility )
                  return true;
               propagate_to_leaf_or_infeasibility( cont_solution, strategy,
                                                   false, probing_view );
               fix_remaining_integer_solutions( cont_solution, probing_view );
               create_solution( result, probing_view );
               return probing_view.isInfeasible();
            }
            successful_backtracks++;
         }
         else
         {
            fix_remaining_integer_solutions( cont_solution, probing_view );
            create_solution( result, probing_view );
            return probing_view.isInfeasible();
         }
      }
   }

   /**
    *
    * @param mode 0 -> 0 1-> lowerbound 2 -> upperbound, 3-> random
    * @param probing_view
    * @return
    */
   bool
   find_initial_solution( int mode, ProbingView<REAL>& probing_view,
                          Vec<REAL>& result )
   {
      probing_view.reset();
      for( int i = 0; i < probing_view.getProbingLowerBounds().size(); i++ )
      {
         auto upper_bounds = probing_view.getProbingUpperBounds();
         auto flags = probing_view.getProbingDomainFlags();
         auto lower_bounds = probing_view.getProbingLowerBounds();
         if( num.isEq( upper_bounds[i], lower_bounds[i] ) )
            continue;
         REAL value;
         switch( mode )
         {
         case 0:
            if( !flags[i].test( ColFlag::kUbInf ) &&
                num.isLT( upper_bounds[i], 0 ) )
               value = upper_bounds[i];
            else if( !flags[i].test( ColFlag::kLbInf ) &&
                     num.isGT( lower_bounds[i], 0 ) )
               value = lower_bounds[i];
            else
               value = 0;
         case 1:
            if( !flags[i].test( ColFlag::kLbInf ) )
               value = lower_bounds[i];
            else if( !flags[i].test( ColFlag::kUbInf ) )
               value = upper_bounds[i];
            else
            {
               assert( flags[i].test( ColFlag::kLbInf ) &&
                       flags[i].test( ColFlag::kUbInf ) );
               value = 0;
            }
            break;
         case 2:
            if( !flags[i].test( ColFlag::kUbInf ) )
               value = upper_bounds[i];
            else if( !flags[i].test( ColFlag::kLbInf ) )
               value = lower_bounds[i];
            else
            {
               assert( flags[i].test( ColFlag::kLbInf ) &&
                       flags[i].test( ColFlag::kUbInf ) );
               value = 0;
            }
            break;
         case 3:
            if( !flags[i].test( ColFlag::kUbInf ) and
                !flags[i].test( ColFlag::kLbInf ) )
            {
//               TODO:
            }
            else if( !flags[i].test( ColFlag::kLbInf ) )
               value = lower_bounds[i];
            else if( !flags[i].test( ColFlag::kUbInf ) )
               value = upper_bounds[i];
            if( !flags[i].test( ColFlag::kUbInf ) )
               value = 0;
            return false;
         default:
            assert( false );
         }
         msg.detailed( "Fix var {} to {}\n", i, value );

         probing_view.setProbingColumn( i, value );
         bool infeasibility_detected = perform_probing_step( probing_view );
         if( infeasibility_detected )
            return true;
      }
      create_solution( result, probing_view );
      return false;
   }

   bool
   one_opt( const Vec<REAL>& feasible_solution, int col, REAL new_value,
            ProbingView<REAL>& probing_view, Vec<REAL>& result )
   {
      // TODO assert only integer values for all integer columns
      probing_view.setProbingColumn( col, new_value );
      bool infeasibility_detected = perform_probing_step( probing_view );
      if( infeasibility_detected )
         return true;
      fix_remaining_integer_solutions( feasible_solution, probing_view );
      create_solution( result, probing_view );
      return probing_view.isInfeasible();
   }

 private:
   void
   propagate_to_leaf_or_infeasibility( Vec<REAL> cont_solution,
                                       RoundingStrategy<REAL>& strategy,
                                       bool stop_at_infeasibility,
                                       ProbingView<REAL>& probing_view )
   {
      while( true )
      {
         Fixing<REAL> fixing =
             strategy.select_rounding_variable( cont_solution, probing_view );
         // dive until all vars are fixed (and returned fixing is invalid)
         if( fixing.is_invalid() )
            return;
         assert( probing_view.is_within_bounds( fixing.get_column_index(),
                                                fixing.get_value() ) );
         msg.detailed( "Fix var {} to {}\n", fixing.get_column_index(),
                       fixing.get_value() );

         probing_view.setProbingColumn( fixing.get_column_index(),
                                        fixing.get_value() );
         bool infeasibility_detected = perform_probing_step( probing_view );
         if( stop_at_infeasibility && infeasibility_detected )
            return;
      }
   }

   bool
   perform_probing_step( ProbingView<REAL>& probing_view )
   {
      if( probing_view.isInfeasible() )
         return true;
      probing_view.propagateDomains();
      return probing_view.isInfeasible();
   }

   REAL
   modify_value_due_to_backtrack( REAL value, REAL solution_value )
   {
      if( num.isGE( value, solution_value ) )
      {
         assert( num.isEq(num.feasFloor( solution_value ) , value - 1) );
         return value - 1;
      }
      assert( num.isLE( value, solution_value ) );
      assert( num.isEq(num.feasCeil( solution_value ), value + 1) );
      return value + 1;
   }

   bool
   fix_remaining_integer_solutions( const Vec<REAL>& cont_solution,
                                    ProbingView<REAL>& probing_view )
   {
      for( int i = 0; i < cont_solution.size(); i++ )
      {

         auto lowerBounds = probing_view.getProbingLowerBounds();
         auto upperBounds = probing_view.getProbingUpperBounds();
         bool ge_lb = num.isGE( cont_solution[i], lowerBounds[i] );
         bool le_ub = num.isLE( cont_solution[i], upperBounds[i] );
         if( !num.isEq( upperBounds[i], lowerBounds[i] ) )
         {
            REAL value;
            if( !probing_view.is_integer_variable( i ) )
            {
               if( ge_lb && le_ub )
                  value = cont_solution[i];
               else if( ge_lb )
                  value = upperBounds[i];
               else
               {
                  assert( le_ub );
                  value = lowerBounds[i];
               }
            }
            else
            {
               if( ge_lb && le_ub )
               {
                  assert( num.isEq( cont_solution[i],
                                    num.round( cont_solution[i] ) ) );
                  value = cont_solution[i];
               }
               else if( ge_lb )
                  value = upperBounds[i];
               else
               {
                  assert( le_ub );
                  value = lowerBounds[i];
               }
            }
            probing_view.setProbingColumn( i, value );
            msg.detailed( "Fix integer var {} to {}\n", i, value );

            perform_probing_step( probing_view );
         }
      }
      return probing_view.isInfeasible();
   }

   void
   create_solution( Vec<REAL>& result, ProbingView<REAL>& probing_view )
   {
      auto upper_bounds = probing_view.getProbingUpperBounds();
      for( int i = 0; i < upper_bounds.size(); i++ )
      {
         assert( num.isEq( upper_bounds[i],
                           probing_view.getProbingLowerBounds()[i] ) );
         result[i] = upper_bounds[i];
      }
   }
};

#endif
