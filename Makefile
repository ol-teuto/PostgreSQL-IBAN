MODULES = iban

EXTENSION = iban
DATA = iban--1.0.0.sql
PGFILEDESC = "iban - IBAN datatype and functions"

PG_CXXFLAGS = -std=c++14 -fPIC

PG_LDFLAGS = -lstdc++

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)

COMPILE.cxx.bc = $(CLANG) -xc++ -Wno-ignored-attributes $(BITCODE_CXXFLAGS) $(CPPFLAGS) -emit-llvm -c

%.bc : %.cpp
	$(COMPILE.cxx.bc) -o $@ $<
	$(LLVM_BINPATH)/opt -module-summary -f $@ -o $@

include $(PGXS)
