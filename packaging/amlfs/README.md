# Isolated AMLFS packaging

This explicit path is separate from `debian/rules`, `lustre.spec.in` and
`lustre-dkms.spec.in`. Those legacy entrypoints remain unchanged. It is driven by
the external AMLFS shared release renderer; it does not derive native identity
from `LUSTRE_VERSION`, a branch or a local counter.

The renderer exports a recorded, clean source commit and writes `native.env`,
native control/spec files, scriptlets and provenance into a new build directory.
Only that exported copy is compiled. Run:

```sh
bash <prepared>/source/packaging/amlfs/build-payload.sh <prepared>
# For prebuilt modules: finalize/strip before external signing, then sign the
# staged .ko files using an authorized test signer. Never strip after signing.
# Run the external Python "seal" step to record the finalized payload inventory.
bash <prepared>/source/packaging/amlfs/native-build.sh <prepared>
```

The external producer must inspect final native relationships and compare
extracted payload bytes with the staged input before accepting any output.
Python >=3.10 runs in the external renderer/verifier, not these distro scripts.
`check-platform.sh` requires the exact native target for real compilation and
userspace shared-library dependency discovery; fixture archive tests are distinct.
Build tools and target dependencies must already be present; no package manager,
publisher, signing service or kernel mutation is invoked by these builders.

`build-payload.sh` stages real client utilities or exact-target modules through
Autotools and `make install DESTDIR=...`, or exports complete DKMS sources.
Native packages preserve the source runtime version file and full source commit.
DKMS cache identities include source and packaging revision; new source-owned
packaging commits require new release identities. The initial build recipe is a
client-only, socket-transport recipe, not approval of additional transports.
It neither consumes mutable global DKMS recipe overrides nor uses weak modules.

Package scriptlets update target-specific depmod/initramfs without loading or
unloading modules, restarting LNet or recursively running a package manager.
DKMS failures propagate. Debian removes the exact old registration before a new
postinst; RPM removes it in the new pre script and skips the old preun on upgrade.
The runtime activation/admission and interruption/recovery integration is a
separate required component, not supplied by these scriptlets.

Host guard output is deliberately uninstallable until real host lifecycle
enforcement is supplied. All outputs are isolated tests, not release approval.
Native archive inspection, DKMS compilation, solver installation, module trust,
real boot and I/O qualification are different evidence levels.
