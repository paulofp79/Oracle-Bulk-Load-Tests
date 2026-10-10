CC ?= cc
OCI_HOME ?= $(ORACLE_HOME)
OCI_INCLUDE ?= $(if $(wildcard $(OCI_HOME)/sdk/include/oci.h),$(OCI_HOME)/sdk/include,$(firstword $(wildcard /usr/include/oracle/*/client64)))
OCI_LIB ?= $(OCI_HOME)/lib
CFLAGS ?= -O2
CPPFLAGS += -I$(OCI_INCLUDE)
LDFLAGS += -L$(OCI_LIB) -Wl,-rpath,$(OCI_LIB)
LDLIBS += -lclntsh

.PHONY: all clean check-oci-sdk

all: oci_direct_path_load bsload_sim

check-oci-sdk:
	@test -n "$(OCI_HOME)" || { echo "Set ORACLE_HOME or OCI_HOME to an Oracle Client home." >&2; exit 2; }
	@test -n "$(OCI_INCLUDE)" -a -f "$(OCI_INCLUDE)/oci.h" || { echo "Missing OCI SDK header. Install oracle-instantclient-devel, or add the matching Instant Client SDK so oci.h is available." >&2; exit 2; }
	@test -f "$(OCI_LIB)/libclntsh.so" || { echo "Missing $(OCI_LIB)/libclntsh.so." >&2; exit 2; }

oci_direct_path_load: oci_direct_path_load.c | check-oci-sdk
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c89 -Wall -Wextra -pedantic $(LDFLAGS) -o $@ $< $(LDLIBS)

bsload_sim: bsload_sim.c | check-oci-sdk
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c89 -Wall -Wextra -pedantic $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f oci_direct_path_load bsload_sim
