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

#ifdef __cplusplus
extern "C" {
#endif

   void*
   setup( const char* filename, int* result, int verbosity_level,
          double current_time_stamp, int add_cutoff_constraint );

   void
   delete_problem_instance( void* heuristic_void_ptr );

/***
 * applies the fix and propagate algorithm including 1-opt
 * @param heuristic_void_ptr pointer generated by setup()
 * @param cont_solution non-integer feasible solution
 * @param result array where the result is stored
 * @param n_cols number of variables
 * @param current_obj_value -> gets overwritten in case a better solution is found
 * @param infeasible_copy_strategy -> see InfeasibleCopyStrategy
 * @param apply_conflicts -> should conflicts be applied
 * @param size_of_constraints -> if conflicts are applied collect them and add them when there are more than this parameter
 * @param max_backtracks -> number of backtracks per dive
 * @param perform_one_opt -> 0 = no; 1= only check feasibility; 2 = with Fix&Propagation
 * @param remaining_time_in_sec remaining time in seconds
  @return whether a (better) integer feasible solution was found
 */
   int
   call_algorithm( void* heuristic_void_ptr, double* cont_solution,
                   double* result, int n_cols, double* current_obj_value,
                   int infeasible_copy_strategy, int apply_conflicts,
                   int size_of_constraints, int max_backtracks,
                   int perform_one_opt, double remaining_time_in_sec );

   void
   perform_one_opt( void* heuristic_void_ptr, double* sol, int n_cols,
                    int perform_opt_one, double* current_obj_value,
                    double remaining_time_in_sec );

   int
   call_simple_heuristic( void* heuristic_void_ptr, double* result,
                          double* current_obj_value );

#ifdef __cplusplus
}
#endif
