# Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
# (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
# Government retains certain rights in this software.

.PHONY: clean-host clean-bf

#CFLAGS := -Wall -Werror -g -O0
CFLAGS := -Wall -fPIC -g -O0 -std=c++11 

CC := gcc
CXX := g++
LD := g++


LDFLAGS := ${LDFLAGS} -libverbs

APPNAMES := hod_server_app hod_client_app 
LIBNAMES := libhodserver.so libhodclient.so 

HOSTBUILD := build-host/
HOST_TARGET := host
BFBUILD := build-bf/
BF_TARGET := bf

all:
	@echo "USE with 'make host' or 'make bf'"

$(info BUILD objdir target)
ifeq ("$(MAKECMDGOALS)", "host")
  OBJDIR := $(HOSTBUILD)
else ifeq ("$(MAKECMDGOALS)", "bf")
  OBJDIR := $(BFBUILD)
endif
$(shell if [ ! -z $(OBJDIR) ] && [ ! -e $(OBJDIR) ]; then mkdir $(OBJDIR); fi;)

APPS := $(addprefix $(OBJDIR),$(APPNAMES))
LIBS := $(addprefix $(OBJDIR),$(LIBNAMES))
$(info APPS = $(APPS))

host: ${LIBS} ${APPS}
bf: ${LIBS} ${APPS}

%.o: ../%.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

$(OBJDIR)hod_server_app: $(OBJDIR)HodServerApp.o $(OBJDIR)libhodserver.so
	${LD} -o $@ $< ${LDFLAGS} -L$(OBJDIR) -lhodserver 

$(OBJDIR)libhodserver.so: $(OBJDIR)HodServer.o $(OBJDIR)Ibverbs.o
	${LD} -shared -o $@ $^ ${LDFLAGS}

$(OBJDIR)libhodclient.so: $(OBJDIR)HodClient.o $(OBJDIR)Ibverbs.o
	${LD} -shared -o $@ $^ ${LDFLAGS}

$(OBJDIR)hod_client_app: $(OBJDIR)HodClientApp.o $(OBJDIR)libhodclient.so
	${LD} -o $@ $< ${LDFLAGS} -L$(OBJDIR) -lhodclient 

print-%  : ; @echo $* = $($*)

clean-host:
	rm -rf $(HOSTBUILD)

clean-bf:
	rm -rf $(BFBUILD)
