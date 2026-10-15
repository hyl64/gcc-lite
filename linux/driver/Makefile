# bitland-mifs-wmi — out-of-tree build
#
#   make            build against the running kernel
#   make build      same, but honours KERNELDIR= (used by DKMS)
#   make sign       sign the module with the MOK key (see scripts/install.sh)
#   make clean
#
# Requires: linux-headers-$(uname -r), gcc, make.

# KERNELDIR is honoured as an alias of KDIR (DKMS passes kernel_source_dir).
ifdef KERNELDIR
KDIR := $(KERNELDIR)
endif
KDIR ?= /lib/modules/$(shell uname -r)/build

# Where sources are read from and where the .ko is written.
#
#   make            -> $(CURDIR), i.e. this directory (normal use)
#   make build DKMS_BUILD_DIR=/var/lib/dkms/<pkg>/<ver>/build
#                   -> DKMS mode
#
# Why DKMS needs this: DKMS runs "make -C /usr/src/<pkg> build", so $(CURDIR)
# resolves to /usr/src/<pkg>, and Kbuild would drop the .ko there. DKMS
# actually copies the tree to its own build dir and only looks for the
# artifact *there*, so without this the build "succeeds" yet DKMS reports
# "binary package ... not found". dkms.conf passes the directory explicitly.
MODULE_SRC ?= $(CURDIR)

# Default signing key material (Ubuntu convention, also used by DKMS).
MOK_DIR ?= /var/lib/shim-signed/mok
SIGN_KEY ?= $(MOK_DIR)/MOK.priv
SIGN_CERT ?= $(MOK_DIR)/MOK.der
HASHALG ?= sha512

obj-m += bitland-mifs-wmi.o

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(MODULE_SRC) modules

build: modules

sign: all
	@test -x $(KDIR)/scripts/sign-file || { echo "sign-file not found in $(KDIR)" >&2; exit 1; }
	@test -f $(SIGN_KEY) || { echo "signing key $(SIGN_KEY) not found (run scripts/install.sh first)" >&2; exit 1; }
	$(KDIR)/scripts/sign-file $(HASHALG) $(SIGN_KEY) $(SIGN_CERT) $(MODULE_SRC)/bitland-mifs-wmi.ko
	@echo "signed: $(MODULE_SRC)/bitland-mifs-wmi.ko"

clean:
	$(MAKE) -C $(KDIR) M=$(MODULE_SRC) clean
	rm -f $(MODULE_SRC)/*.ko $(MODULE_SRC)/*.mod $(MODULE_SRC)/*.mod.c \
	      $(MODULE_SRC)/.*.cmd $(MODULE_SRC)/Module.markers $(MODULE_SRC)/modules.order

.PHONY: all modules build sign clean
