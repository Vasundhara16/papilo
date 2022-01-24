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

#include "fix/FixAndPropagate.hpp"
#include "fix/VolumeAlgorithm.hpp"
#include "papilo/core/Problem.hpp"
#include "papilo/core/ProblemBuilder.hpp"
#include "papilo/io/MpsParser.hpp"
#include "papilo/misc/OptionsParser.hpp"
#include <boost/program_options.hpp>
#include <fstream>

using namespace papilo;

Problem<double>
modify_problem( Problem<double>& problem );

void
invert( const double* pDouble, double* result, int length );

int
main( int argc, char* argv[] )
{

   // get the options passed by the user
   OptionsInfo optionsInfo;
   try
   {
      optionsInfo = parseOptions( argc, argv );
   }
   catch( const boost::program_options::error& ex )
   {
      std::cerr << "Error while parsing the options.\n" << '\n';
      std::cerr << ex.what() << '\n';
      return 1;
   }

   if( !optionsInfo.is_complete )
      return 0;

   double readtime = 0;
   Problem<double> problem;
   Num<double> num{};
   Message msg{};
   boost::optional<Problem<double>> prob;

   {
      Timer t( readtime );
      prob = MpsParser<double>::loadProblem( optionsInfo.instance_file );
   }

   // Check whether reading was successful or not
   if( !prob )
   {
      fmt::print( "error loading problem {}\n", optionsInfo.instance_file );
      return 0;
   }
   problem = *prob;

   fmt::print( "reading took {:.3} seconds\n", readtime );

   // set up ProblemUpdate to trivialPresolve so that activities exist
   Presolve<double> presolve{};
   auto result = presolve.apply( problem, false );

   switch( result.status )
   {
   case papilo::PresolveStatus::kUnbounded:
   case papilo::PresolveStatus::kUnbndOrInfeas:
   case papilo::PresolveStatus::kInfeasible:
      fmt::print( "PaPILO detected infeasibility or unbounded-ness\n" );
      return 0;
   case papilo::PresolveStatus::kUnchanged:
   case papilo::PresolveStatus::kReduced:
      break;
   }

   VolumeAlgorithm<double> algorithm{ {},  {},   0.5,  0.1,  1, 0.0005, 2,
                                      1.1, 0.66, 0.02, 0.01, 2, 20 };

   // TODO: add same small heuristic

   Problem<double> reformulated = modify_problem( problem );

   // generate pi
   Vec<double> pi{};
   for( int i = 0; i < reformulated.getNRows(); i++ )
      pi.push_back( 0 );

   // generate UB
   StableSum<double> min_value{};

   for( int i = 0; i < problem.getNCols(); i++ )
   {
      if( num.isZero( problem.getObjective().coefficients[i] ) )
         continue;
      else if( num.isLT( problem.getObjective().coefficients[i], 0 ) )
      {
         if( problem.getColFlags()[i].test( ColFlag::kLbInf ) )
         {
            fmt::print(
                "Could not calculate objective bound: variable {} is unbounded",
                i );
            return 1;
         }
         min_value.add( problem.getObjective().coefficients[i] +
                        problem.getLowerBounds()[i] );
      }
      else
      {
         if( problem.getColFlags()[i].test( ColFlag::kUbInf ) )
         {
            fmt::print(
                "Could not calculate objective bound: variable {} is unbounded",
                i );
            return 1;
         }
         min_value.add( problem.getObjective().coefficients[i] +
                        problem.getUpperBounds()[i] );
      }
   }

   algorithm.volume_algorithm(
       reformulated.getObjective().coefficients,
       reformulated.getConstraintMatrix(),
       reformulated.getConstraintMatrix().getLeftHandSides(),
       reformulated.getVariableDomains(), pi, min_value.get() );

   Postsolve<double> postsolve{ msg, num };
   // TODO: add postsolving
   //   postsolve.undo(red, orig, result.postsolve);

   return 0;
}

Problem<double>
modify_problem( Problem<double>& problem )
{
   ProblemBuilder<double> builder;

   int nnz = 0;
   int ncols = problem.getNCols();
   int nrows = 0;
   for( int i = 0; i < problem.getNRows(); i++ )
   {
      nrows++;
      int rowsize = problem.getRowSizes()[i];
      nnz = nnz + rowsize;
      auto flags = problem.getConstraintMatrix().getRowFlags()[i];
      if( flags.test( RowFlag::kEquation ) || flags.test( RowFlag::kLhsInf ) ||
          flags.test( RowFlag::kRhsInf ) )
      {
         continue;
      }
      nrows++;
      nnz = nnz + rowsize;
   }

   builder.reserve( nnz, nrows, ncols );

   /* set up columns */
   builder.setNumCols( ncols );
   for( int i = 0; i != ncols; ++i )
   {
      builder.setColLb( i, problem.getLowerBounds()[i] );
      builder.setColUb( i, problem.getUpperBounds()[i] );
      auto flags = problem.getColFlags()[i];
      builder.setColLbInf( i, flags.test( ColFlag::kLbInf ) );
      builder.setColUbInf( i, flags.test( ColFlag::kUbInf ) );

      builder.setColIntegral( i, flags.test( ColFlag::kIntegral ) );
      builder.setObj( i, problem.getObjective().coefficients[i] );
   }

   /* set up rows */
   builder.setNumRows( nrows );
   int counter = 0;
   for( int i = 0; i != problem.getNRows(); ++i )
   {
      const int* rowcols =
          problem.getConstraintMatrix().getRowCoefficients( 0 ).getIndices();
      const double* rowvals =
          problem.getConstraintMatrix().getRowCoefficients( 0 ).getValues();
      int rowlen =
          problem.getConstraintMatrix().getRowCoefficients( 0 ).getLength();
      auto flags = problem.getRowFlags()[i];
      double lhs = problem.getConstraintMatrix().getLeftHandSides()[i];
      double rhs = problem.getConstraintMatrix().getRightHandSides()[i];

      if( flags.test( RowFlag::kEquation ) || flags.test( RowFlag::kLhsInf ) )
      {
         builder.addRowEntries( counter, rowlen, rowcols, rowvals );
         builder.setRowLhs( counter, lhs );
         builder.setRowRhs( counter, rhs );
         builder.setRowLhsInf( counter, false );
         builder.setRowRhsInf( counter, false );
      }
      else if( !flags.test( RowFlag::kRhsInf ) )
      {
         double neg_rowvals[rowlen];
         invert( rowvals, neg_rowvals, rowlen );
         builder.addRowEntries( counter, rowlen, rowcols, neg_rowvals );
         builder.setRowLhs( counter, -rhs );
         builder.setRowRhs( counter, 0 );
         builder.setRowLhsInf( counter, false );
         builder.setRowRhsInf( counter, true );
      }
      else if( !flags.test( RowFlag::kLhsInf ) )
      {
         builder.addRowEntries( counter, rowlen, rowcols, rowvals );
         builder.setRowLhs( counter, lhs );
         builder.setRowRhs( counter, 0 );
         builder.setRowLhsInf( counter, false );
         builder.setRowRhsInf( counter, true );
      }
      else
      {
         assert( !flags.test( RowFlag::kLhsInf ) );
         assert( !flags.test( RowFlag::kRhsInf ) );
         double neg_rowvals[rowlen];
         invert( rowvals, neg_rowvals, rowlen );

         builder.addRowEntries( counter, rowlen, rowcols, neg_rowvals );
         builder.setRowLhs( counter, -rhs );
         builder.setRowRhs( counter, 0 );
         builder.setRowLhsInf( counter, false );
         builder.setRowRhsInf( counter, true );
         counter++;
         builder.addRowEntries( counter, rowlen, rowcols, rowvals );
         builder.setRowLhs( counter, lhs );
         builder.setRowRhs( counter, 0 );
         builder.setRowLhsInf( counter, false );
         builder.setRowRhsInf( counter, true );
      }
      counter++;
   }
   return builder.build();
}

void
invert( const double* pDouble, double* result, int length )
{
   for( int i = 0; i < length; i++ )
      result[i] = pDouble[i] * -1;
}
