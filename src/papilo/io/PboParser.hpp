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

#ifndef _PAPILO_IO_PBO_PARSER_HPP_
#define _PAPILO_IO_PBO_PARSER_HPP_

#include "papilo/Config.hpp"
#include "papilo/core/ConstraintMatrix.hpp"
#include "papilo/core/Objective.hpp"
#include "papilo/core/Problem.hpp"
#include "papilo/core/VariableDomains.hpp"
#include "papilo/misc/Flags.hpp"
#include "papilo/misc/Hash.hpp"
#include "papilo/misc/Num.hpp"
#include "papilo/external/pdqsort/pdqsort.h"
#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/optional.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/utility/string_ref.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>

#ifdef PAPILO_USE_BOOST_IOSTREAMS_WITH_BZIP2
#include <boost/iostreams/filter/bzip2.hpp>
#endif
#ifdef PAPILO_USE_BOOST_IOSTREAMS_WITH_ZLIB
#include <boost/iostreams/filter/gzip.hpp>
#endif

namespace papilo
{

template <typename REAL, bool isfptype = num_traits<REAL>::is_floating_point>
struct RealParseType
{
   using type = double;
};

template <typename REAL>
struct RealParseType<REAL, true>
{
   using type = REAL;
};

/// Parser for pbo files in fixed and free format
template <typename REAL>
class PboParser
{
   static_assert(
       num_traits<typename RealParseType<REAL>::type>::is_floating_point,
       "the parse type must be a floating point type" );

 public:
   static boost::optional<Problem<REAL>>
   loadProblem( const std::string& filename )
   {
      PboParser<REAL> parser;

      Problem<REAL> problem;

      if( !parser.parseFile( filename ) )
         return boost::none;

      assert( parser.nnz >= 0 );

      Vec<REAL> obj_vec( size_t( parser.nCols ), REAL{ 0.0 } );

      for( auto i : parser.coeffobj )
         obj_vec[i.first] = i.second;
// TODO from here
      problem.setObjective( std::move( obj_vec ), parser.objoffset );
      problem.setConstraintMatrix(
          SparseStorage<REAL>{ std::move( parser.entries ), parser.nCols,
                               parser.nRows, true },
          std::move( parser.rowlhs ), std::move( parser.rowrhs ),
          std::move( parser.row_flags ), true );
      problem.setVariableDomains( std::move( Vec<REAL> vect(n, REAL(0)) ),
                                  std::move( Vec<REAL> vect(n, REAL(1)) ),
                                  std::move( Vec<ColFlags> vect(n, kIntegral) ) ); // kIntegral
      problem.setVariableNames( std::move( parser.colnames ) );
      problem.setName( std::move( filename ) );
      problem.setConstraintNames( std::move( parser.rownames ) );

      problem.setInputTolerance(
          REAL{ pow( typename RealParseType<REAL>::type{ 10 },
                     -std::numeric_limits<
                         typename RealParseType<REAL>::type>::digits10 ) } );
      return problem;
   }

 private:
   PboParser() {}

   /// load LP from PBO file as transposed triplet matrix
   bool
   parseFile( const std::string& filename );

   bool
   parse( boost::iostreams::filtering_istream& file );

   /// Try to comply with http://www.cril.univ-artois.fr/PB16/format.pdf 

   enum class boundtype
   {
      kEq,
      kGE
   };

   enum class parsekey
   {
      kObjective,
      kConstraint,
      kFail,
      kComment
   };

   void
   printErrorMessage( parsekey keyword )
   {
      switch( keyword )
      {
      case parsekey::kObjective:
         std::cerr << "error reading objective " << std::endl;
         break;
      case parsekey::kConstraint:
         std::cerr << "error reading objective " << std::endl;
         break;
      default:
         std::cerr << "undefined read error " << std::endl;
         break;
      }
   };

   /*
    * data for pbo problem
    */

   Vec<Triplet<REAL>> entries;
   Vec<std::pair<int, REAL>> coeffobj;
   Vec<REAL> rowlhs;
   Vec<REAL> rowrhs;
   Vec<std::string> colnames;

   HashMap<std::string, int> colname2idx;
   Vec<boundtype> row_type;
   Vec<RowFlags> row_flags;
   REAL objoffset = REAL(0);

   int nCols = 0;
   int nRows = 0;
   int nnz = 0;

   /// checks first word of strline and wraps it by it_begin and it_end
   parsekey
   checkFirstWord( std::string& strline, std::string::iterator& it,
                   boost::string_ref& word_ref ) const;

   parsekey
   parseDefault( boost::iostreams::filtering_istream& file ) const;


   parsekey
   parseRhs( boost::iostreams::filtering_istream& file );

   Vec<std::pair<int, REAL>> parseRow(std::string& trimmedstrline);

};

template <typename REAL>
typename PboParser<REAL>::parsekey
PboParser<REAL>::checkFirstWord( std::string& strline,
                                 std::string::iterator& it,
                                 boost::string_ref& word_ref ) const
{
   using namespace boost::spirit;

   it = strline.begin() + strline.find_first_not_of( " " );
   std::string::iterator it_start = it;

   // TODO: Daniel
   qi::parse( it, strline.end(), qi::lexeme[+qi::graph] );

   const std::size_t length = std::distance( it_start, it );

   boost::string_ref word( &( *it_start ), length );

   word_ref = word;

   if( word.front() == 'R' ) // todo
   {
   }
}

template <typename REAL>
typename PboParser<REAL>::parsekey
PboParser<REAL>::parseDefault( boost::iostreams::filtering_istream& file ) const
{
   std::string strline;
   getline( file, strline );

   std::string::iterator it;
   boost::string_ref word_ref;
   return checkFirstWord( strline, it, word_ref );
}



template <typename REAL>
bool
PboParser<REAL>::parseFile( const std::string& filename )
{
   std::ifstream file( filename, std::ifstream::in );
   boost::iostreams::filtering_istream in;

   if( !file )
      return false;

#ifdef PAPILO_USE_BOOST_IOSTREAMS_WITH_ZLIB
   if( boost::algorithm::ends_with( filename, ".gz" ) )
      in.push( boost::iostreams::gzip_decompressor() );
#endif

#ifdef PAPILO_USE_BOOST_IOSTREAMS_WITH_BZIP2
   if( boost::algorithm::ends_with( filename, ".bz2" ) )
      in.push( boost::iostreams::bzip2_decompressor() );
#endif

   in.push( file );

   return parse( in );
}

template <typename REAL>
std::pair<Vec<std::pair<int, REAL>>,REAL> parseRow(std::string& trimmedstrline)
{
   const std::string whitespace = " "
   auto beginSpace = trimmedstrline.find_first_of(whitespace);
   while (beginSpace != std::string::npos)
   {
        const auto endSpace = trimmedstrline.find_first_not_of(whitespace, beginSpace);
        const auto range = endSpace - beginSpace;

        trimmedstrline.replace(beginSpace, range, fill);

        const auto newStart = beginSpace + fill.length();
        beginSpace = trimmedstrline.find_first_of(whitespace, newStart);
   } // having a nicer string to work with makes me comfortable i get it right loop maybe O(n^2)
   Vec<std::pair<int, REAL>> result;

   REAL rhsoff = REAL(0);

   std::stringstream row(beginSpace);

   int degree = 0;
   int variable_index;
   REAL weight;

   // I am unfamiliar with error handling conventions in this code base.

   while(getline(row, token, ' '))
   // You can use space as line break and the getline the result.
   {
      if (token == "+")
      {
         assert(degree != 2);
         degree = 0;
         result.push_back(std::make_pair(variable_index, weight));
         continue;
      } 
      else if (token == ">=") || (token == "=")
      {
         assert(degree != 2);
         degree = 0;
         result.push_back(std::make_pair(variable_index, weight));
         getline(row, token, ' ');
         std::istringstream(token) >> weight;
         rhsoff += weight;
         assert(std::string::npos != getline(row, token, ' '));

         break;
      }
      if (degree == 0)
      {
         std::istringstream(token) >> weight;
      } 
      else if (degree == 1)
      {
         if(token.starts_with('~'))
         {
            // a*~x = a*(1-x) = a*1 - a*x = rhs <=> -a*x = rhs-a
            weight = -weight;
            rhsoff += weight;
            token.erase(0,1)
         }
         if(colname2idx.count(token) == 0)
         {
            colname2idx.insert(std::pair<std::string,int>(token,nCols++))
         } 
         variable_index = colname2idx[token];
      }

      degree++;
   }
   assert(degree != 2);
   return std::make_pair(result, rhsoff)

}

template <typename REAL>
bool
PboParser<REAL>::parse( boost::iostreams::filtering_istream& file )
{
   nnz = 0;
   bool has_objective = false;

   // parsing loop
   std::string current_line;

   while(std::getline(file, line)){
      if (line[0] == '*' || line.empty()) continue;
      if (line[0] == 'm' && line[1] == 'i' && line[2] == 'n' && line[3] == ':')
      {
         const auto strBegin = line.find_first_not_of(" ", 4); 
         const auto strEnd = line.find_last_not_of(" ;");
         // being a bit liberal in what is accepted
         const auto strRange = strEnd - strBegin + 1;

         line = line.substr(strBegin, strRange);
         [coeffobj, objoffset] = parseRow(line);

         break; // objective may only be first non comment line
      }
      break;
   }
   while(std::getline(file,line))
   {
      if (line[0] == '*' || line.empty()) continue;

      Vec<std::pair<int, REAL> row;
      int rhs;
      const auto strBegin = line.find_first_not_of(" "); 
      const auto strEnd = line.find_last_not_of(" ;");
      // being a bit liberal in what is accepted
      const auto strRange = strEnd - strBegin + 1;
      line = line.substr(strBegin, strRange);     

      auto [row, lhs] = parseRow(line);
      
         for (const auto& pair : row)
         {  
            entries.push_back(
               std::make_tuple( pair.first, nRows, pair.second ) );
            nnz++;
         }


      if (line.find("=") != std::string::npos) 
      {
         rowlhs.push_back( lhs );
         rowrhs.push_back( lhs );
         row_flags.emplace_back( RowFlag::kEquation );

      }
      else if (line.find(">=") != std::string::npos) 
      {
         rowlhs.push_back( lhs ); 
         // Not sure what to put here as Floating point in does not exist 
         // for rational types i think.
         rowrhs.push_back( REAL{ 0.0 } );
         row_flags.emplace_back( RowFlag::kRhsInf );
      }
      else 
      {
         assert(true);
         // I am unfamiliar with error handling conventions in this code base.
      }
      nRows++;
   }

   nCols = colname2idx.size(); //TODO
   nRows = rowname2idx.size() - 1; // subtract obj row

   return true;
}

} // namespace papilo

#endif /* _PARSING_PBO_PARSER_HPP_ */