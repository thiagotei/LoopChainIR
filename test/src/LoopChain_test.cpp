/*******************************************************************************
\file LoopChain_test.cpp
\autors Ian J. Bertolacci

\purpose
To perform unit testsing on the LoopChain data structure.

\copyright
Copyright 2015 Colorado State University
*******************************************************************************/

#include "gtest/gtest.h"
#include "LoopChain.h"
#include <iostream>
#include <utility>

using namespace std;

TEST(LoopChainTest, FourChain) {
  LoopChain chain;

  string lower[1];
  string upper[1];

  lower[0] = "0";
  upper[0] = "N";
  Box box_1( lower, upper, 1 );

  lower[0] = "1";
  upper[0] = "M";
  Box box_2( lower, upper, 1 );

  lower[0] = "2";
  upper[0] = "J";
  Box box_3( lower, upper, 1 );

  lower[0] = "3";
  upper[0] = "K";
  Box box_4( lower, upper, 1 );

  chain.append( LoopNest( box_1 ) );
  chain.append( LoopNest( box_2 ) );
  chain.append( LoopNest( box_3 ) );
  chain.append( LoopNest( box_4 ) );

  EXPECT_EQ( chain.length(), 4 );

  Box got_box = chain.getNest(0).getDomain();

  EXPECT_EQ( got_box.dimensions(), 1 );
  EXPECT_EQ( got_box.getLowerBound(0), string("0"));
  EXPECT_EQ( got_box.getUpperBound(0), string("N"));

  got_box = chain.getNest(1).getDomain();

  EXPECT_EQ( got_box.dimensions(), 1 );
  EXPECT_EQ( got_box.getLowerBound(0), string("1"));
  EXPECT_EQ( got_box.getUpperBound(0), string("M"));

  got_box = chain.getNest(2).getDomain();

  EXPECT_EQ( got_box.dimensions(), 1 );
  EXPECT_EQ( got_box.getLowerBound(0), string("2"));
  EXPECT_EQ( got_box.getUpperBound(0), string("J"));

  got_box = chain.getNest(3).getDomain();

  EXPECT_EQ( got_box.dimensions(), 1 );
  EXPECT_EQ( got_box.getLowerBound(0), string("3"));
  EXPECT_EQ( got_box.getUpperBound(0), string("K"));
}