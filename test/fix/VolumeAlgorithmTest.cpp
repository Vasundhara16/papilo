/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*               This file is part of the program and library                */
/*    PaPILO --- Parallel Presolve for Integer and Linear Optimization       */
/*                                                                           */
/* Copyright (C) 2020  Konrad-Zuse-Zentrum                                   */
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

#include "fix/VolumeAlgorithm.hpp"
#include "catch/catch.hpp"
#include "papilo/core/Problem.hpp"
#include "papilo/core/ProblemBuilder.hpp"

using namespace papilo;

Problem<double>
setupProblemForVolumeAlgorithm();

Problem<double>
setupProblemWithoutMatrix();

TEST_CASE( "small-volume-algorithm-test", "[volume]" )
{
   VolumeAlgorithmParameter<double> para{ 0.05, 0.1,  0.2,   0.0005, 2, 2, 1.1,
                                          0.66, 0.01, 0.001, 0.02,   2, 20, 600.0 };
   double d =0;
   Timer timer {d};
   VolumeAlgorithm<double> algorithm{ {}, {}, timer, para};
   Vec<double> c( 2 );
   c = { 1, 2 };
   Vec<double> b( 2 );
   b = { 3, 1 };
   Problem<double> problem = setupProblemWithoutMatrix();
   ConstraintMatrix<double> matrix =
       setupProblemForVolumeAlgorithm().getConstraintMatrix();
   Vec<double> pi( 2 );
   pi = { 0, 0 };
   algorithm.volume_algorithm( c, matrix, b, problem.getVariableDomains(), pi, 3 );
   // TODO: @Suresh add assertions and check input
}

Problem<double>
setupProblemForVolumeAlgorithm()
{
   Vec<double> coefficients{ 1.0, 1.0 };
   Vec<double> upperBounds{ 1.0, 1.0 };
   Vec<double> lowerBounds{ -1.0, 0.0 };
   Vec<uint8_t> isIntegral{ 1, 1 };

   Vec<double> rhs{ 2.0, 3.0 };
   Vec<std::string> rowNames{ "A1", "A2" };
   Vec<std::string> columnNames{ "c1", "c2" };
   Vec<std::tuple<int, int, double>> entries{
       std::tuple<int, int, double>{ 0, 0, 1.0 },
       std::tuple<int, int, double>{ 0, 1, 2.0 },
       std::tuple<int, int, double>{ 1, 1, 1.0 },
   };

   ProblemBuilder<double> pb;
   pb.reserve( (int)entries.size(), (int)rowNames.size(),
               (int)columnNames.size() );
   pb.setNumRows( (int)rowNames.size() );
   pb.setNumCols( (int)columnNames.size() );
   pb.setColUbAll( upperBounds );
   pb.setColLbAll( lowerBounds );
   pb.setObjAll( coefficients );
   pb.setObjOffset( 0.0 );
   pb.setColIntegralAll( isIntegral );
   pb.setRowRhsAll( rhs );
   pb.addEntryAll( entries );
   pb.setColNameAll( columnNames );
   pb.setProblemName( "coefficient strengthening matrix" );
   Problem<double> problem = pb.build();
   return problem;
}

Problem<double>
setupProblemWithoutMatrix()
{
   Vec<double> coefficients{ 1.0, 1.0 };
   Vec<double> upperBounds{ 1.0, 1.0 };
   Vec<double> lowerBounds{ 0.0, 0.0 };
   Vec<uint8_t> isIntegral{ 1, 1 };

   Vec<std::string> rowNames{};
   Vec<std::string> columnNames{ "c1", "c2" };
   Vec<std::tuple<int, int, double>> entries{};

   ProblemBuilder<double> pb;
   pb.reserve( 0, 0, (int)columnNames.size() );
   pb.setNumRows( (int)rowNames.size() );
   pb.setNumCols( (int)columnNames.size() );
   pb.setColUbAll( upperBounds );
   pb.setColLbAll( lowerBounds );
   pb.setObjAll( coefficients );
   pb.setObjOffset( 0.0 );
   pb.setColIntegralAll( isIntegral );
   pb.setColNameAll( columnNames );
   pb.setProblemName( "empty matrix" );
   Problem<double> problem = pb.build();
   return problem;
}