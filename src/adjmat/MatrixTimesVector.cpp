/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2011-2023 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "ActionWithMatrix.h"
#include "core/ActionRegister.h"

//+PLUMEDOC MCOLVAR MATRIX_VECTOR_PRODUCT
/*
Calculate the product of the matrix and the vector

\par Examples

*/
//+ENDPLUMEDOC

namespace PLMD {
namespace adjmat {

class MatrixTimesVector : public ActionWithMatrix {
private:
  unsigned nderivatives;
  std::vector<bool> stored_arg;
public:
  static void registerKeywords( Keywords& keys );
  explicit MatrixTimesVector(const ActionOptions&);
  unsigned getNumberOfColumns() const override { plumed_error(); }
  unsigned getNumberOfDerivatives();
  void prepare() override ;
  bool isInSubChain( unsigned& nder ) override { nder = arg_deriv_starts[0]; return true; }
  void setupForTask( const unsigned& task_index, std::vector<unsigned>& indices, MultiValue& myvals ) const ;
  void performTask( const std::string& controller, const unsigned& index1, const unsigned& index2, MultiValue& myvals ) const override;
  void runEndOfRowJobs( const unsigned& ind, const std::vector<unsigned> & indices, MultiValue& myvals ) const override ;
  void updateAdditionalIndices( const unsigned& ostrn, MultiValue& myvals ) const override ;
};

PLUMED_REGISTER_ACTION(MatrixTimesVector,"MATRIX_VECTOR_PRODUCT")

void MatrixTimesVector::registerKeywords( Keywords& keys ) {
  ActionWithMatrix::registerKeywords(keys); keys.use("ARG");
  ActionWithValue::useCustomisableComponents(keys);
}

MatrixTimesVector::MatrixTimesVector(const ActionOptions&ao):
  Action(ao),
  ActionWithMatrix(ao)
{
  if( getNumberOfArguments()<2 ) error("Not enough arguments specified");
  unsigned nvectors=0, nmatrices=0;
  for(unsigned i=0; i<getNumberOfArguments(); ++i) {
    if( getPntrToArgument(i)->hasDerivatives() ) error("arguments should be vectors or matrices");
    if( getPntrToArgument(i)->getRank()<=1 ) nvectors++;
    if( getPntrToArgument(i)->getRank()==2 ) nmatrices++;
  }

  std::vector<unsigned> shape(1); shape[0]=getPntrToArgument(0)->getShape()[0];
  if( nvectors==1 ) {
    unsigned n = getNumberOfArguments()-1;
    for(unsigned i=0; i<n; ++i) {
      if( getPntrToArgument(i)->getRank()!=2 || getPntrToArgument(i)->hasDerivatives() ) error("all arguments other than last argument should be matrices");
      if( getPntrToArgument(n)->getRank()==0 ) {
        if( getPntrToArgument(i)->getShape()[1]!=1 ) error("number of columns in input matrix does not equal number of elements in vector");
      } else if( getPntrToArgument(i)->getShape()[1]!=getPntrToArgument(n)->getShape()[0] ) error("number of columns in input matrix does not equal number of elements in vector");
    }
    if( getPntrToArgument(n)->getRank()>0 ) {
      if( getPntrToArgument(n)->getRank()!=1 || getPntrToArgument(n)->hasDerivatives() ) error("last argument to this action should be a vector");
    }
    getPntrToArgument(n)->buildDataStore();

    ActionWithVector* av=dynamic_cast<ActionWithVector*>( getPntrToArgument(0)->getPntrToAction() );
    if( av ) done_in_chain=canBeAfterInChain( av );

    if( getNumberOfArguments()==2 ) {
      addValue( shape ); setNotPeriodic();
    } else {
      for(unsigned i=0; i<getNumberOfArguments()-1; ++i) {
        std::string name = getPntrToArgument(i)->getName();
        if( name.find_first_of(".")!=std::string::npos ) { std::size_t dot=name.find_first_of("."); name = name.substr(dot+1); }
        addComponent( name, shape ); componentIsNotPeriodic( name );
      }
    }
  } else if( nmatrices==1 ) {
    if( getPntrToArgument(0)->getRank()!=2 || getPntrToArgument(0)->hasDerivatives() ) error("first argument to this action should be a matrix");
    for(unsigned i=1; i<getNumberOfArguments(); ++i) {
      if( getPntrToArgument(i)->getRank()>1 || getPntrToArgument(i)->hasDerivatives() ) error("all arguments other than first argument should be vectors");
      if( getPntrToArgument(i)->getRank()==0 ) {
        if( getPntrToArgument(0)->getShape()[1]!=1 ) error("number of columns in input matrix does not equal number of elements in vector");
      } else if( getPntrToArgument(0)->getShape()[1]!=getPntrToArgument(i)->getShape()[0] ) error("number of columns in input matrix does not equal number of elements in vector");
      getPntrToArgument(i)->buildDataStore();
    }

    ActionWithVector* av=dynamic_cast<ActionWithVector*>( getPntrToArgument(0)->getPntrToAction() );
    if( av ) done_in_chain=canBeAfterInChain( av );

    for(unsigned i=1; i<getNumberOfArguments(); ++i) {
      std::string name = getPntrToArgument(i)->getName();
      if( name.find_first_of(".")!=std::string::npos ) { std::size_t dot=name.find_first_of("."); name = name.substr(dot+1); }
      addComponent( name, shape ); componentIsNotPeriodic( name );
    }
  } else error("You should either have one vector or one matrix in input");

  nderivatives = buildArgumentStore(0);
  std::string headstr=getFirstActionInChain()->getLabel(); stored_arg.resize( getNumberOfArguments() );
  for(unsigned i=0; i<getNumberOfArguments(); ++i) stored_arg[i] = getPntrToArgument(i)->ignoreStoredValue( headstr );
}

unsigned MatrixTimesVector::getNumberOfDerivatives() {
  return nderivatives;
}

void MatrixTimesVector::prepare() {
  ActionWithVector::prepare(); Value* myval = getPntrToComponent(0);
  if( myval->getShape()[0]==getPntrToArgument(0)->getShape()[0] ) return;
  std::vector<unsigned> shape(1); shape[0] = getPntrToArgument(0)->getShape()[0]; myval->setShape(shape);
}

void MatrixTimesVector::setupForTask( const unsigned& task_index, std::vector<unsigned>& indices, MultiValue& myvals ) const {
  unsigned start_n = getPntrToArgument(0)->getShape()[0], size_v = getPntrToArgument(0)->getShape()[1];
  if( indices.size()!=size_v+1 ) indices.resize( size_v + 1 );
  for(unsigned i=0; i<size_v; ++i) indices[i+1] = start_n + i;
  myvals.setSplitIndex( size_v + 1 );
}

void MatrixTimesVector::performTask( const std::string& controller, const unsigned& index1, const unsigned& index2, MultiValue& myvals ) const {
  unsigned ind2 = index2; if( index2>=getPntrToArgument(0)->getShape()[0] ) ind2 = index2 - getPntrToArgument(0)->getShape()[0];
  if( getPntrToArgument(1)->getRank()==1 ) {
    for(unsigned i=0; i<getNumberOfArguments()-1; ++i) {
      unsigned ostrn = getConstPntrToComponent(i)->getPositionInStream();
      double matval = getElementOfMatrixArgument( 0, index1, ind2, myvals ), vecval=getArgumentElement( i+1, ind2, myvals );
      // And add this part of the product
      myvals.addValue( ostrn, matval*vecval );
      // Now lets work out the derivatives
      if( doNotCalculateDerivatives() ) continue;
      addDerivativeOnMatrixArgument( stored_arg[0], i, 0, index1, ind2, vecval, myvals ); addDerivativeOnVectorArgument( stored_arg[i+1], i, i+1, ind2, matval, myvals );
    }
  } else {
    unsigned n=getNumberOfArguments()-1;
    for(unsigned i=0; i<getNumberOfArguments()-1; ++i) {
      unsigned ostrn = getConstPntrToComponent(i)->getPositionInStream();
      double matval = getElementOfMatrixArgument( i, index1, ind2, myvals ), vecval=getArgumentElement( n, ind2, myvals );
      // And add this part of the product
      myvals.addValue( ostrn, matval*vecval );
      // Now lets work out the derivatives
      if( doNotCalculateDerivatives() ) continue;
      addDerivativeOnMatrixArgument( stored_arg[i], i, i, index1, ind2, vecval, myvals ); addDerivativeOnVectorArgument( stored_arg[n], i, n, ind2, matval, myvals );
    }
  }
}

void MatrixTimesVector::runEndOfRowJobs( const unsigned& ind, const std::vector<unsigned> & indices, MultiValue& myvals ) const {
  if( doNotCalculateDerivatives() || !actionInChain() ) return ;

  if( getPntrToArgument(1)->getRank()==1 ) {
    unsigned istrn = getPntrToArgument(0)->getPositionInMatrixStash();
    std::vector<unsigned>& mat_indices( myvals.getMatrixRowDerivativeIndices( istrn ) );
    for(unsigned j=0; j<getNumberOfComponents(); ++j) {
      unsigned ostrn = getConstPntrToComponent(j)->getPositionInStream();
      for(unsigned i=0; i<myvals.getNumberOfMatrixRowDerivatives(istrn); ++i) myvals.updateIndex( ostrn, mat_indices[i] );
    }
  } else {
    for(unsigned j=0; j<getNumberOfComponents(); ++j) {
      unsigned istrn = getPntrToArgument(j)->getPositionInMatrixStash();
      unsigned ostrn = getConstPntrToComponent(j)->getPositionInStream();
      std::vector<unsigned>& mat_indices( myvals.getMatrixRowDerivativeIndices( istrn ) );
      for(unsigned i=0; i<myvals.getNumberOfMatrixRowDerivatives(istrn); ++i) myvals.updateIndex( ostrn, mat_indices[i] );
    }
  }
}

void MatrixTimesVector::updateAdditionalIndices( const unsigned& ostrn, MultiValue& myvals ) const {
  unsigned n = getNumberOfArguments()-1; if( getPntrToArgument(1)->getRank()==1 ) n = 1;
  unsigned nvals = getPntrToArgument(n)->getNumberOfValues();
  for(unsigned i=0; i<nvals; ++i) myvals.updateIndex( ostrn, arg_deriv_starts[n] + i );
}

}
}
