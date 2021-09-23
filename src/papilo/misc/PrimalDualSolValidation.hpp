/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*               This file is part of the program and library                */
/*    PaPILO --- Parallel Presolve for Integer and Linear Optimization       */
/*                                                                           */
/* Copyright (C) 2020-2021 Konrad-Zuse-Zentrum                               */
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

#ifndef _PAPILO_CORE_PRIMAL_DUAL_SOL_VALIDATION_HPP_
#define _PAPILO_CORE_PRIMAL_DUAL_SOL_VALIDATION_HPP_

#include "papilo/core/Solution.hpp"
#include "papilo/core/postsolve/PostsolveStatus.hpp"

namespace papilo
{
template <typename REAL>
class PrimalDualSolValidation
{

 public:
   PrimalDualSolValidation( const Message msg, const Num<REAL> n )
   {
      message = msg;
      num = n;
   };

 private:
   Num<REAL> num;
   Message message{};

 private:

   bool
   checkLength( const Solution<REAL>& solution, const Problem<REAL>& problem )
   {
      const int nCols = problem.getNCols();

      bool primal_check = solution.primal.size() != nCols;
      if( solution.type == SolutionType::kPrimalDual )
         return primal_check || solution.reducedCosts.size() != nCols ||
                solution.dual.size() != problem.getNRows();
      return primal_check;
   }

   bool
   checkPrimalBounds( const Vec<REAL>& primalSolution,
                      const Problem<REAL>& problem )
   {
      bool failure = false;

      const Vec<REAL> ub = problem.getUpperBounds();
      const Vec<REAL> lb = problem.getLowerBounds();

      for( unsigned int col = 0; col < problem.getNCols(); col++ )
      {
         if( problem.getColFlags()[col].test( ColFlag::kInactive ) )
            continue;

         if( ( not problem.getColFlags()[col].test( ColFlag::kLbInf ) ) &&
             num.isFeasLT( primalSolution[col], lb[col] ) )
         {
            message.info( "Column {:<3} violates lower column bound.\n", col );
            failure = true;
         }

         if( ( not problem.getColFlags()[col].test( ColFlag::kUbInf ) ) &&
             num.isFeasGT( primalSolution[col], ub[col] ) )
         {
            message.info( "Column {:<3} violates upper column bound.\n", col );
            failure = true;
         }
      }
      return failure;
   }

   bool
   checkPrimalConstraintAndUpdateSlack( Solution<REAL>& solution,
                                        const Problem<REAL>& problem ) const
   {
      const Vec<REAL> rhs = problem.getConstraintMatrix().getRightHandSides();
      const Vec<REAL> lhs = problem.getConstraintMatrix().getLeftHandSides();

      if(solution.type == SolutionType::kPrimalDual)
      {
         solution.slack.clear();
         solution.slack.resize(problem.getNRows());
      }

      for( int row = 0; row < problem.getNRows(); row++ )
      {
         if( problem.getRowFlags()[row].test( RowFlag::kRedundant ) )
            continue;

         REAL rowValue = 0;
         auto entries = problem.getConstraintMatrix().getRowCoefficients( row );
         for( int j = 0; j < entries.getLength(); j++ )
         {
            int col = entries.getIndices()[j];
            if( problem.getColFlags()[col].test( ColFlag::kInactive ) )
               continue;
            REAL x = entries.getValues()[j];
            REAL primal = solution.primal[col];
            rowValue += x * primal;
         }

         bool lhs_inf = problem.getRowFlags()[row].test( RowFlag::kLhsInf );
         if( ( not lhs_inf ) && num.isFeasLT( rowValue, lhs[row] ) )
         {
            message.info( "Row {:<3} violates row bounds ({:<3} < {:<3}).\n",
                          row, lhs[row], rowValue );
            return true;
         }
         bool rhs_inf = problem.getRowFlags()[row].test( RowFlag::kRhsInf );
         if( ( not rhs_inf ) && num.isFeasGT( rowValue, rhs[row] ) )
         {
            message.info( "Row {:<3} violates row bounds ({:<3} < {:<3}).\n",
                          row, rowValue, rhs[row] );
            return true;
         }
         if(solution.type == SolutionType::kPrimalDual)
            solution.slack[row] = num.isZero( rowValue ) ? 0 : rowValue;
      }
      return false;
   }

   bool
   checkPrimalFeasibilityAndUpdateSlack( Solution<REAL>& solution,
                                         const Problem<REAL>& problem )
   {
      bool primalBounds = checkPrimalBounds( solution.primal, problem );
      bool primalConstraint =
          checkPrimalConstraintAndUpdateSlack( solution, problem );
      return primalBounds or primalConstraint;
   }

   bool
   checkDualFeasibility( const Vec<REAL>& primalSolution,
                         const Vec<REAL>& dualSolution,
                         const Vec<REAL>& reducedCosts,
                         const Vec<VarBasisStatus>& basis,
                         const Problem<REAL>& problem )
   {
      const papilo::Vec<REAL>& lowerBounds = problem.getLowerBounds();
      const papilo::Vec<REAL>& upperBounds = problem.getUpperBounds();

      const Vec<REAL> rhs = problem.getConstraintMatrix().getRightHandSides();
      const Vec<REAL> lhs = problem.getConstraintMatrix().getLeftHandSides();

      for( int variable = 0; variable < problem.getNCols(); variable++ )
      {
         if( problem.getColFlags()[variable].test( ColFlag::kInactive ) )
            continue;
         REAL colValue = 0;

         auto coeff =
             problem.getConstraintMatrix().getColumnCoefficients( variable );
         for( int counter = 0; counter < coeff.getLength(); counter++ )
         {
            REAL value = coeff.getValues()[counter];
            int rowIndex = coeff.getIndices()[counter];
            colValue += dualSolution[rowIndex] * value;
         }

         if( not num.isFeasEq( colValue + reducedCosts[variable],
                           problem.getObjective().coefficients[variable] ) )
         {
            message.info(
                "Dual row {:<3} violates dual row bounds ({:<3} != {:<3}).\n",
                variable, problem.getObjective().coefficients[variable],
                colValue + reducedCosts[variable],
                problem.getObjective().coefficients[variable] );
            return true;
         }
      }
      return false;
   }

   bool
   checkComplementarySlackness( const Vec<REAL>& primalSolution,
                                const Vec<REAL>& dualSolution,
                                const Vec<REAL>& reducedCosts,
                                const Problem<REAL>& problem )

   {

      const Vec<REAL> lb = problem.getLowerBounds();
      const Vec<REAL> ub = problem.getUpperBounds();

      const Vec<REAL> rhs = problem.getConstraintMatrix().getRightHandSides();
      const Vec<REAL> lhs = problem.getConstraintMatrix().getLeftHandSides();
      for( int row = 0; row < problem.getNRows(); row++ )
      {
         if( problem.getRowFlags()[row].test( RowFlag::kRedundant ) )
            continue;

         REAL rowValue = 0;
         auto entries = problem.getConstraintMatrix().getRowCoefficients( row );
         for( int j = 0; j < entries.getLength(); j++ )
         {
            int col = entries.getIndices()[j];
            if( problem.getColFlags()[col].test( ColFlag::kFixed ) )
               continue;
            rowValue += entries.getValues()[j] * primalSolution[col];
         }

         if( not problem.getRowFlags()[row].test( RowFlag::kLhsInf ) and
             not problem.getRowFlags()[row].test( RowFlag::kRhsInf ) )
         {
            if( num.isFeasGT( lhs[row], rowValue ) and
                num.isFeasLT( rhs[row], rowValue ) and
                not num.isFeasZero( dualSolution[row] ) )
               return true;
         }
         else if( not problem.getRowFlags()[row].test( RowFlag::kLhsInf ) )
         {
            assert( problem.getRowFlags()[row].test( RowFlag::kRhsInf ) );
            if( num.isFeasGT( lhs[row], rowValue ) and
                not num.isFeasZero( dualSolution[row] ) )
               return true;
         }
         else if( ( not problem.getRowFlags()[row].test( RowFlag::kLhsInf ) ) &&
                  num.isFeasGT( rowValue, lhs[row] ) )
         {
            assert( problem.getRowFlags()[row].test( RowFlag::kRhsInf ) );
            if( num.isFeasLT( rhs[row], rowValue ) and
                not num.isFeasZero( dualSolution[row] ) )
               return true;
         }
      }

      for( int col = 0; col < problem.getNCols(); col++ )
      {
         if( problem.getColFlags()[col].test( ColFlag::kInactive ) )
            continue;

         bool isLbInf = problem.getColFlags()[col].test( ColFlag::kLbInf );
         bool isUbInf = problem.getColFlags()[col].test( ColFlag::kUbInf );
         REAL upperBound = ub[col];
         REAL lowerBound = lb[col];
         REAL reducedCost = reducedCosts[col];
         REAL sol = primalSolution[col];

         if( num.isFeasEq( upperBound, lowerBound ) and not isLbInf and
             not isUbInf )
            continue;

         if( not isLbInf and not isUbInf )
         {
            if( num.isFeasGT( sol, lowerBound ) and num.isFeasLT( sol, upperBound ) and
                not num.isFeasZero( reducedCost ) )
               return true;
         }
         else if( not isLbInf )
         {
            assert( isUbInf );
            if( num.isFeasGT( sol, lowerBound ) and not num.isFeasZero( reducedCost ) )
               return true;
         }
         else if( not isUbInf )
         {
            assert( isLbInf );
            if( num.isFeasLT( sol, upperBound ) and not num.isFeasZero( reducedCost ) )
               return true;
         }
      }
      return false;
   }

   bool
   checkBasis( const Solution<REAL>& solution, const Problem<REAL>& problem )
   {
      if(not solution.basisAvailabe)
         return false;
      int number_basic_variable = 0;
      int number_rows = 0;
      for( int variable = 0; variable < problem.getNCols(); variable++ )
      {
         if( problem.getColFlags()[variable].test( ColFlag::kInactive ) )
            continue;
         bool ub_infinity =
             problem.getColFlags()[variable].test( ColFlag::kUbInf );
         bool lb_infinity =
             problem.getColFlags()[variable].test( ColFlag::kLbInf );
         REAL lb = problem.getLowerBounds()[variable];
         REAL ub = problem.getUpperBounds()[variable];
         REAL sol = solution.primal[variable];

         assert( ub_infinity or lb_infinity or num.isFeasGE( ub, lb ) );
         switch( solution.varBasisStatus[variable] )
         {
         case VarBasisStatus::BASIC:
            if( not num.isZero( solution.reducedCosts[variable] ) )
               return true;
            number_basic_variable++;
            break;
         case VarBasisStatus::FIXED:
            if( ub_infinity or lb_infinity or not num.isFeasEq( lb, ub ) or
                not num.isFeasEq( sol, ub ) )
               return true;
            break;
         case VarBasisStatus::ON_LOWER:
            if( lb_infinity or not num.isFeasEq( sol, lb ) )
               return true;
            break;
         case VarBasisStatus::ON_UPPER:
            if( ub_infinity or not num.isFeasEq( sol, ub ) )
               return true;
            break;
         case VarBasisStatus::ZERO:
            if( not lb_infinity or not ub_infinity or
                not num.isZero( sol ) )
               return true;
            break;
         case VarBasisStatus::UNDEFINED:
            return true;
         }
      }
      for( int row = 0; row < problem.getNRows(); row++ )
      {
         if( problem.getRowFlags()[row].test( RowFlag::kRedundant ) )
            continue;
         number_rows++;
         bool lhs_infinity =
             problem.getRowFlags()[row].test( RowFlag::kLhsInf );
         bool rhs_infinity =
             problem.getRowFlags()[row].test( RowFlag::kRhsInf );

         REAL lhs = problem.getConstraintMatrix().getLeftHandSides()[row];
         REAL rhs = problem.getConstraintMatrix().getRightHandSides()[row];
         REAL slack = solution.slack[row];

         assert( lhs_infinity or rhs_infinity or num.isFeasGE( rhs, lhs ) );
         switch( solution.rowBasisStatus[row] )
         {
         case VarBasisStatus::BASIC:
            if( not num.isFeasZero( solution.dual[row] ) )
               return true;
            number_basic_variable++;
            break;
         case VarBasisStatus::FIXED:
            if( lhs_infinity or rhs_infinity or not num.isFeasEq( lhs, rhs ) or
                not num.isFeasEq( slack, rhs ) )
               return true;
            assert( problem.getRowFlags()[row].test( RowFlag::kEquation ) );
            break;
         case VarBasisStatus::ON_LOWER:
            if( lhs_infinity or not num.isFeasEq( slack, lhs ) )
               return true;
            break;
         case VarBasisStatus::ON_UPPER:
            if( rhs_infinity or not num.isFeasEq( slack, rhs ) )
               return true;
            break;
         case VarBasisStatus::ZERO:
            if( not rhs_infinity or not lhs_infinity or
                not num.isZero( slack ) )
               return true;
            break;
         case VarBasisStatus::UNDEFINED:
            return true;
         }
      }
      return number_basic_variable != number_rows;
   }

 public:
   bool
   checkObjectiveFunction( const Vec<REAL>& primalSolution,
                           const Vec<REAL>& dualSolution,
                           const Vec<REAL>& reducedCosts,
                           const Problem<REAL>& problem )
   {
      REAL duality_gap =
          getDualityGap( primalSolution, dualSolution, reducedCosts, problem );
      return not num.isFeasZero( duality_gap )  ;
   }

   PostsolveStatus
   verifySolutionAndUpdateSlack( Solution<REAL>& solution,
                                 const Problem<REAL>& problem )
   {

      bool failure = checkLength( solution, problem );
      if( failure )
      {
         message.info( "Solution vector length check FAILED.\n" );
         return PostsolveStatus::kFailed;
      }

      failure = checkPrimalFeasibilityAndUpdateSlack( solution, problem );
      if( failure )
      {
         message.info( "Primal feasibility check FAILED.\n" );
         return PostsolveStatus::kFailed;
      }

      if( solution.type == SolutionType::kPrimalDual )
      {
         if( checkDualFeasibility( solution.primal, solution.dual,
                                   solution.reducedCosts,
                                   solution.varBasisStatus, problem ) )
         {
            message.info( "Dual feasibility check FAILED.\n" );
            failure = true;
         }

         if( checkComplementarySlackness( solution.primal, solution.dual,
                                          solution.reducedCosts, problem ) )
         {
            message.info( "Complementary slack check FAILED.\n" );
            failure = true;
         }

         if( checkBasis( solution, problem ) )
         {
            message.info( "Basis check FAILED.\n" );
            failure = true;
         }

         if( checkObjectiveFunction( solution.primal, solution.dual,
                                     solution.reducedCosts, problem ) )
         {
            message.info( "Objective function failed.\n" );
            //            failure = true;
         }
         if( failure )
            return PostsolveStatus::kFailed;
      }

      message.info( "Solution passed validation\n" );
      return PostsolveStatus::kOk;
   }

   REAL
   getDualityGap( const Vec<REAL>& primalSolution,
                  const Vec<REAL>& dualSolution, const Vec<REAL>& reducedCosts,
                  const Problem<REAL>& problem )
   {
      StableSum<REAL> primal_objective;
      for( int i = 0; i < problem.getNCols(); i++ )
      {
         primal_objective.add( primalSolution[i] *
                               problem.getObjective().coefficients[i] );
      }
      StableSum<REAL> dual_objective;
      for( int i = 0; i < problem.getNRows(); i++ )
      {
         REAL dual = dualSolution[i];
         REAL side;
         if( dual < 0 )
            side = problem.getConstraintMatrix().getRightHandSides()[i];
         else
            side = problem.getConstraintMatrix().getLeftHandSides()[i];
         dual_objective.add( dual * side );
      }
      for( int i = 0; i < problem.getNCols(); i++ )
      {
         REAL reducedCost = reducedCosts[i];
         REAL side;
         if( reducedCost < 0 )
            side = problem.getUpperBounds()[i];
         else
            side = problem.getLowerBounds()[i];
         dual_objective.add( reducedCost * side );
      }
      return primal_objective.get() - dual_objective.get();
   }
};
} // namespace papilo

#endif