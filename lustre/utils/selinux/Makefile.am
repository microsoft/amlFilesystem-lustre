# SELinux policy module for Lustre (LU-20119)
#
# On RHEL/Rocky 10+, Lustre helpers (l_getsepol, lctl, etc.) launched by the
# kernel via call_usermodehelper() are denied under SELinux enforcing mode:
# 10.1 and later removed kernel_generic_helper_t from the base selinux-policy,
# and 10.0 keeps the type but does not grant it what the helpers need.
# This module introduces a dedicated lustre_helper_t domain with a
# kernel_t -> lustre_helper_t type transition on the labeled Lustre helper
# binaries (lustre_helpers.fc), confining permissions to Lustre helpers only.
#
# Build: checkmodule + semodule_package (from checkpolicy + policycoreutils)
# Install: semodule -X 200 -i + restorecon (RPM %post handles this; make install
#          handles it too for a direct source install into the system prefix)

CLEANFILES = lustre_helpers.mod lustre_helpers.pp lustre_helpers.fc

lustre_helpers.mod: lustre_helpers.te
	$(CHECKMODULE) -M -m -o $@ $<

# The file contexts are generated so the labels follow $(sbindir) instead of
# a hard-coded path: with a non-default --prefix or --sbindir the binaries
# would never be labeled and the module would load but do nothing.  The
# template also carries the literal /usr/sbin and /usr/bin paths, which the
# kernel upcalls use regardless of configure options; awk drops the entries
# that collide once %sbindir% has been substituted.  The placeholder is
# %sbindir% rather than the usual @sbindir@ because config.status expands
# @...@ when it generates this Makefile from Makefile.in, which would rewrite
# the sed expression itself instead of leaving it to run at build time.
# The substitution skips comment lines, so that the description of the
# placeholder in lustre_helpers.fc.in is not rewritten along with the rules.
lustre_helpers.fc: lustre_helpers.fc.in Makefile
	$(SED) -e '/^\#/!s|%sbindir%|$(sbindir)|g' $(srcdir)/lustre_helpers.fc.in | \
		$(AWK) '/^#/ || !seen[$$1]++' > $@

lustre_helpers.pp: lustre_helpers.mod lustre_helpers.fc
	$(SEMODULE_PACKAGE) -o $@ -m $< -f lustre_helpers.fc

all-local: lustre_helpers.pp

selinuxdir = $(datadir)/selinux/packages
selinux_DATA = lustre_helpers.pp

# For direct source installs (DESTDIR unset): load the module immediately.
# RPM staged builds set DESTDIR, so semodule is called from %post instead.
#
# DESTDIR alone is not enough to tell a real install from a probe: automake's
# "make distcheck" runs an install/uninstall pair with no DESTDIR at all,
# under --prefix=<distdir>/_inst (only its second pair sets DESTDIR).  Since
# semodule always writes to the system policy store, a root "make distcheck"
# would otherwise replace an installed lustre_helpers with one whose .fc
# points into _inst and then delete it, leaving the machine with no module
# and the RPM still owning the .pp.  Require the system prefix as well.
#
# This must be install-data-hook, not install-data-local: automake makes
# install-data-local just another prerequisite of install-data-am alongside
# install-selinuxDATA, so under "make -j install" the two can run
# concurrently and semodule may read a missing or partial .pp.
# install-data-hook is the construct guaranteed to run after the install
# rules have completed.
install-data-hook:
	@if test -z "$(DESTDIR)" && test "$(prefix)" = "/usr"; then \
		$(SEMODULE) -X 200 -i $(selinuxdir)/lustre_helpers.pp || \
			echo "warning: could not load lustre_helpers into" \
			     "the policy store (needs root)"; \
		if test -n "$(RESTORECON)"; then \
			$(RESTORECON) -F $(sbindir)/l_getsepol $(sbindir)/lctl \
				$(sbindir)/l_getidentity $(sbindir)/l_getauth \
				$(sbindir)/l_foreign_symlink \
				2>/dev/null || :; \
		fi; \
	elif test -z "$(DESTDIR)"; then \
		echo "note: prefix is $(prefix), not /usr: leaving the policy" \
		     "store alone; load with semodule -X 200 -i" \
		     "$(selinuxdir)/lustre_helpers.pp"; \
	fi

uninstall-local:
	@if test -z "$(DESTDIR)" && test "$(prefix)" = "/usr"; then \
		$(SEMODULE) -X 200 -r lustre_helpers 2>/dev/null || :; \
	fi

EXTRA_DIST = lustre_helpers.te lustre_helpers.fc.in
