#!/usr/bin/bash

NFSVERSION=${1:-"3"}

LUSTRE=${LUSTRE:-$(dirname $0)/..}
. $LUSTRE/tests/test-framework.sh
# only call init_test_env if this script is called directly
if [[ -z "$TESTSUITE" || "$TESTSUITE" = "$(basename $0 .sh)" ]]; then
	init_test_env "$@"
else
	. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}

fi

init_logging

racer=$LUSTRE/tests/racer/racer.sh
. $LUSTRE/tests/setup-nfs.sh

# lustre client used as nfs server (default is mds node)
LUSTRE_CLIENT_NFSSRV=${LUSTRE_CLIENT_NFSSRV:-$(facet_active_host $SINGLEMDS)}
NFS_SRVMNTPT=${NFS_SRVMNTPT:-$MOUNT}
NFS_CLIENTS=${NFS_CLIENTS:-$CLIENTS}
NFS_CLIENTS=$(exclude_items_from_list $NFS_CLIENTS $LUSTRE_CLIENT_NFSSRV)
NFS_CLIMNTPT=${NFS_CLIMNTPT:-$MOUNT}

[ -z "$NFS_CLIENTS" ] &&
	skip_env "need at least two nodes: nfs server and nfs client"

[ "$NFSVERSION" = "4" ] && cl_mnt_opt="${MOUNT_OPTS:+$MOUNT_OPTS,}32bitapi" ||
	cl_mnt_opt=""

check_and_setup_lustre
$LFS df
TESTDIR=$NFS_CLIMNTPT/d0.$(basename $0 .sh)
mkdir -p $TESTDIR
$LFS setstripe -c -1 $TESTDIR

# first unmount all the lustre clients
cleanup_mount $MOUNT

cleanup_exit () {
	trap 0
	cleanup
	check_and_cleanup_lustre
	exit
}

cleanup () {
	cleanup_nfs "$LUSTRE_CLIENT_NFSSRV" "$NFS_SRVMNTPT" \
			"$NFS_CLIENTS" "$NFS_CLIMNTPT" || \
		error_noexit false "failed to cleanup nfs"
	zconf_umount $LUSTRE_CLIENT_NFSSRV $NFS_SRVMNTPT force ||
		error_noexit false "failed to umount lustre on"\
			"$LUSTRE_CLIENT_NFSSRV"
	# restore lustre mount
	restore_mount $MOUNT ||
		error_noexit false "failed to mount lustre"
}

trap cleanup_exit EXIT SIGHUP SIGINT

zconf_mount $LUSTRE_CLIENT_NFSSRV $NFS_SRVMNTPT "$cl_mnt_opt" ||
	error "mount lustre on $LUSTRE_CLIENT_NFSSRV failed"

# setup the nfs
setup_nfs "$LUSTRE_CLIENT_NFSSRV" "$NFS_SRVMNTPT" "$NFS_CLIENTS" \
		"$NFS_CLIMNTPT" "$NFSVERSION" || \
	error false "setup nfs failed!"

NFSCLIENT=true
FAIL_ON_ERROR=false

# common setup
clients=${NFS_CLIENTS:-$HOSTNAME}
generate_machine_file $clients $MACHINEFILE ||
	error "Failed to generate machine file"
num_clients=$(get_node_count ${clients//,/ })

# compilbench
# Run short iteration in nfs mode
cbench_IDIRS=${cbench_IDIRS:-2}
cbench_RUNS=${cbench_RUNS:-2}

# metabench
# Run quick in nfs mode
mbench_NFILES=${mbench_NFILES:-10000}

# connectathon
[ "$SLOW" = "no" ] && cnt_NRUN=2

# IOR
ior_DURATION=${ior_DURATION:-30}

# source the common file after all parameters are set to take affect
. $LUSTRE/tests/functions.sh

build_test_filter

get_mpiuser_id $MPI_USER
MPI_RUNAS=${MPI_RUNAS:-"runas -u $MPI_USER_UID -g $MPI_USER_GID"}
$GSS_KRB5 && refresh_krb5_tgt $MPI_USER_UID $MPI_USER_GID $MPI_RUNAS

test_1() {
	local src_file=/tmp/testfile.txt
	local dst_file=$TESTDIR/$tfile
	local native_file=$MOUNT/${dst_file#$NFS_CLIMNTPT}
	local mode=644
	local got
	local ver

	ver=$(version_code $(lustre_build_version_node $LUSTRE_CLIENT_NFSSRV))
	(( $ver >= $(version_code v2_15_90-11-g75f55f99a3) )) ||
		skip "Need lustre client version of nfs server (MDS1 by default) >= 2.15.91 for NFS ACL handling fix"

	touch $src_file
	chmod $mode $src_file

	cp -p $src_file $dst_file || error "copy  $src_file->$dst_file failed"

	stat $dst_file
	got="$(stat -c %a $dst_file)"
	[[ "$got" == "$mode" ]] || error "permissions mismatch on '$dst_file'"

	local xattr=system.posix_acl_access
	local lustre_xattrs=$(do_node $LUSTRE_CLIENT_NFSSRV \
		"getfattr -d -m - -e hex $native_file")

	echo $lustre_xattrs
	# If this fails then the mountpoint is non-Lustre or does
	# not exist because we failed to find a native mountpoint
	[[ "$lustre_xattrs" =~ "trusted.link" ]] ||
		error "no trusted.link xattr in '$native_file'"

	[[ "$lustre_xattrs" =~ "$xattr" ]] &&
		error "found unexpected $xattr in '$native_file'"

	do_node $LUSTRE_CLIENT_NFSSRV "stat $native_file"
	got=$(do_node $LUSTRE_CLIENT_NFSSRV "stat -c %a $native_file")
	[[ "$got" == "$mode" ]] || error "permission mismatch on '$native_file'"

	rm -f $src_file $dst_file
}
run_test 1 "test copy with attributes"

test_2() {
	local mp1file=$TESTDIR/file1
	local tmpdir=$(mktemp -d /tmp/nfs-XXXXXX)
	local mp2file=$tmpdir/${mp1file#$NFS_CLIMNTPT}
	local ver

	ver=$(version_code $(lustre_build_version_node $LUSTRE_CLIENT_NFSSRV))
	(( $ver >= $(version_code v2_16_50-154-gfb3c3d2052) )) ||
		skip "Need lustre client version of nfs server >= 2.16.51"

	mount -v -t nfs -o nfsvers=$NFSVERSION,async \
		$LUSTRE_CLIENT_NFSSRV:$NFS_SRVMNTPT $tmpdir ||\
		error "Nfs 2nd mount($tmpdir) error"

	local owc=$(do_node $LUSTRE_CLIENT_NFSSRV \
		"dmesg | grep -v 'DEBUG MARKER:' | grep -c 'refcount_t: underflow; use-after-free'")
	(( $owc > 0 )) && do_node $LUSTRE_CLIENT_NFSSRV \
		'echo 1 > /sys/kernel/debug/clear_warn_once'

	touch $mp1file
	local i=0
	for ((i=1; i<=10; i++)) do
		[ $i -eq 10 ] && echo "P100"
		echo "R$((i * 2)),10"
		echo "W$((i * 100)),100"
		sleep 1
	done | flocks_test 6 $mp1file &
	local pid=$!
	for ((i = 0; i < 5; )); do
		echo "T0" | flocks_test 6 $mp2file |\
			grep 'R2,26;W100,900.' && i=$((i + 1))
		local nwc=$(do_node $LUSTRE_CLIENT_NFSSRV \
			"dmesg | grep -v 'DEBUG MARKER:' | grep -c 'refcount_t: underflow; use-after-free'")
		(( $owc >= $nwc )) || {
			do_node $LUSTRE_CLIENT_NFSSRV \
				"dmesg | grep -1 'refcount_t: underflow; use-after-free'"
			error "Failed (owc:$owc < nwc:$nwc)"
		}
		sleep 1
	done
	kill -9 $pid
	wait

	umount $tmpdir
	rm -rf $tmpdir || true
}
run_test 2 "fcntl getlk on nfs shouldn't cause refcount underflow"

test_compilebench() {
	if [[ "$TESTSUITE" =~ "parallel-scale-nfs" ]]; then
		skip "LU-12957 and LU-13068: compilebench for $TESTSUITE"
	fi

	run_compilebench $TESTDIR
}
run_test compilebench "compilebench"

test_metabench() {
	run_metabench $TESTDIR $NFS_CLIMNTPT
}
run_test metabench "metabench"

test_connectathon() {
	run_connectathon $TESTDIR
}
run_test connectathon "connectathon"

test_iorssf() {
	run_ior "ssf" $TESTDIR $NFS_SRVMNTPT
}
run_test iorssf "iorssf"

test_iorfpp() {
	run_ior "fpp" $TESTDIR $NFS_SRVMNTPT
}
run_test iorfpp "iorfpp"

test_racer_on_nfs() {
	local racer_params="MDSCOUNT=$MDSCOUNT OSTCOUNT=$OSTCOUNT LFS=$LFS"

	do_nodes $CLIENTS "$racer_params $racer $TESTDIR"
}
run_test racer_on_nfs "racer on NFS client"

test_stale_after_remote_write() {
	# LU-20055: when Lustre is re-exported over NFS, a write done by
	# another Lustre client must become visible on the NFS client. The
	# re-exporting node only learns of the remote write through DLM lock
	# cancellation, which must bump i_version so the NFS client's
	# change_attr revalidation invalidates its cache.
	#
	# This only works over NFSv4, which has a real change attribute. NFSv3
	# has none and revalidates on ctime/mtime, which in Lustre are only
	# second-granular, so a same-second remote overwrite stays invisible.
	[[ "$NFSVERSION" == "4" ]] ||
		skip "needs NFSv4 change attribute (NFSv3 revalidates on ctime)"

	local ver

	ver=$(version_code $(lustre_build_version_node $LUSTRE_CLIENT_NFSSRV))
	(( $ver >= $(version_code 2.17.57) )) ||
		skip "Need nfs server client >= 2.17.57 for i_version bump"

	local nfsfile=$TESTDIR/$tfile
	# fs-relative path of the test file (NFS_CLIMNTPT is the fs root)
	local relpath=${TESTDIR#$NFS_CLIMNTPT}/$tfile
	local srv=$LUSTRE_CLIENT_NFSSRV
	local wfile=$MOUNT2$relpath
	local tries=10
	local skipmsg=""
	local msg=""
	local -a ctime
	local wcmd
	local got
	local i

	# A second, independent Lustre client is needed to generate a
	# conflicting write: writing through the NFS server's own client
	# would not trigger a cross-client lock cancellation.
	zconf_mount $srv $MOUNT2 $MOUNT_OPTS ||
		error "mount 2nd lustre client on $srv failed"

	# NB: everything from here to the umount below records its verdict in
	# $msg instead of calling error(), and no stack_trap is used either.
	# error() exits and run_one runs the subtest in a subshell, so an
	# assertion would skip the umount and leave the writer client mounted
	# on $srv - which is the MDS host by default, where cleanup() knows
	# nothing about it. stack_trap is no good here because this suite
	# installs a raw "trap cleanup_exit EXIT" (full NFS teardown) that
	# stack_trap would re-arm inside the per-iteration subshell, tearing
	# the NFS setup down after the first iteration of a repeated run.
	for ((i = 0; i < tries; i++)); do
		# Populate via NFS, then read it back so the NFS client caches
		# the data and the file's attributes/change_attr.
		echo old >$nfsfile || { msg="write via NFS failed"; break; }
		got=$(cat $nfsfile)
		[[ "$got" == "old" ]] ||
			{ msg="NFS read got '$got', want 'old'"; break; }

		# Overwrite from the independent client. This cancels the NFS
		# server's read lock; without the i_version bump on
		# cancellation the change_attr does not move and the NFS
		# client keeps serving the stale page. The ctime is read on
		# either side of the write in the same do_node so the ssh
		# round trip stays out of the window the two writes share.
		wcmd="stat -c %Z $wfile && echo new >$wfile"
		ctime=($(do_node $srv "$wcmd && stat -c %Z $wfile"))
		(( ${#ctime[@]} == 2 )) ||
			{ msg="write from 2nd lustre client failed"; break; }

		# nfsd adds ctime to the change attribute, so an overwrite in
		# a later second moves change_attr on its own and would pass
		# without the i_version bump. Lustre ctime is second-granular,
		# so retry until both writes land in the same second.
		(( ctime[0] == ctime[1] )) || continue

		# Close-to-open revalidation on reopen must expose the new
		# data.
		got=$(cat $nfsfile)
		[[ "$got" == "new" ]] ||
			msg="NFS served stale data, got '$got'"
		break
	done
	(( i < tries )) ||
		skipmsg="no same-second overwrite in $tries tries"

	rm -f $nfsfile
	zconf_umount $srv $MOUNT2 force ||
		msg=${msg:-"umount 2nd lustre client on $srv failed"}
	do_node $srv "rmdir $MOUNT2"

	[[ -z "$msg" ]] || error "$msg"
	[[ -z "$skipmsg" ]] || skip "$skipmsg"
}
run_test stale_after_remote_write \
	"NFS client sees fresh data after a remote Lustre write"

complete_test $SECONDS
exit_status
