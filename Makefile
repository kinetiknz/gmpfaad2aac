CXXFLAGS=-std=c++11 -Wall -W -g -fpic -Igmp-api

libgmpfaad2aac.so: src/gmpfaad2aac.cc
	$(CXX) -lfaad -shared $(CXXFLAGS) $< -o $@
