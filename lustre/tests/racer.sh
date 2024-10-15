#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

#
# This file is part of Lustre, http://www.lustre.org/
#
# lustre/tests/racer.sh
#
# Launcher racer script which runs individual racer tests under
# lustre/tests/racer/
#
set -e

ONLY=${ONLY:-"$*"}

LUSTRE=${LUSTRE:-$(dirname $0)/..}
. $LUSTRE/tests/test-framework.sh
init_test_env "$@"
init_logging

build_test_filter

racer=$LUSTRE/tests/racer/racer.sh
echo racer: $racer with $MDSCOUNT MDTs

if [ "$SLOW" = "no" ]; then
	DURATION=${DURATION:-300}
else
	DURATION=${DURATION:-900}
fi
MOUNT_2=${MOUNT_2:-"yes"}

check_and_setup_lustre

CLIENTS=${CLIENTS:-$HOSTNAME}
RACERDIRS=${RACERDIRS:-"$DIR $DIR2"}
echo RACERDIRS=$RACERDIRS

RACER_FAILOVER=${RACER_FAILOVER:-false}
FAIL_TARGETS=${FAIL_TARGETS:-"MDS OST"}
RACER_FAILOVER_PERIOD=${RACER_FAILOVER_PERIOD:-60}

if $RACER_FAILOVER; then
	declare -a  victims
	for target in $FAIL_TARGETS; do
		victims=(${victims[@]} $(get_facets $target))
	done
	echo Victim facets "${victims[@]}"
fi

if ((MDSCOUNT > 1)); then
	(( $MDS1_VERSION >= $(version_code 2.5.0) )) &&
		RACER_ENABLE_REMOTE_DIRS=${RACER_ENABLE_REMOTE_DIRS:-true}
	(( $MDS1_VERSION >= $(version_code 2.8.0) )) &&
		RACER_ENABLE_STRIPED_DIRS=${RACER_ENABLE_STRIPED_DIRS:-true}
	(( $MDS1_VERSION >= $(version_code 2.13.57) )) &&
		RACER_ENABLE_MIGRATION=${RACER_ENABLE_MIGRATION:-true}
	(( $MDS1_VERSION >= $(version_code 2.15.55.45) )) &&
		RACER_MIGRATE_STRIPE_MAX=$MDSCOUNT
fi

[[ "$MDS1_VERSION" -lt $(version_code 2.9.54) || $mgs_FSTYPE != zfs ]] &&
	RACER_ENABLE_SNAPSHOT=false

(( "$MDS1_VERSION" <= $(version_code 2.9.55) )) &&
	RACER_ENABLE_PFL=false

(( "$MDS1_VERSION" <= $(version_code 2.10.53) )) &&
	RACER_ENABLE_DOM=false

(( "$MDS1_VERSION" < $(version_code 2.10.55) )) &&
	RACER_ENABLE_FLR=false

(( $MDS1_VERSION < $(version_code 2.12.0) )) &&
	RACER_ENABLE_SEL=false

[[ $OST1_VERSION -lt $(version_code 2.15.59) || $ost1_FSTYPE != ldiskfs ]] &&
	RACER_ENABLE_FALLOCATE=false
check_set_fallocate || RACER_ENABLE_FALLOCATE=false

RACER_ENABLE_REMOTE_DIRS=${RACER_ENABLE_REMOTE_DIRS:-false}
RACER_ENABLE_STRIPED_DIRS=${RACER_ENABLE_STRIPED_DIRS:-false}
RACER_ENABLE_MIGRATION=${RACER_ENABLE_MIGRATION:-false}
RACER_ENABLE_SNAPSHOT=${RACER_ENABLE_SNAPSHOT:-true}
RACER_ENABLE_FILE_MIGRATE=${RACER_ENABLE_FILE_MIGRATE:-true}
RACER_ENABLE_PFL=${RACER_ENABLE_PFL:-true}
RACER_ENABLE_DOM=${RACER_ENABLE_DOM:-false}
RACER_ENABLE_FLR=${RACER_ENABLE_FLR:-true}
RACER_ENABLE_SEL=${RACER_ENABLE_SEL:-true}
# set false, LU-14988
RACER_ENABLE_OVERSTRIPE=${RACER_ENABLE_OVERSTRIPE:-false}
RACER_LOV_MAX_STRIPECOUNT=${RACER_LOV_MAX_STRIPECOUNT:-$LOV_MAX_STRIPE_COUNT}
RACER_EXTRA_LAYOUT=${RACER_EXTRA_LAYOUT:-""}
RACER_MIGRATE_STRIPE_MAX=${RACER_MIGRATE_STRIPE_MAX:-1}
RACER_ENABLE_FALLOCATE=${RACER_ENABLE_FALLOCATE:-true}

fail_random_facet () {
	local facets=${victims[@]}
	facets=${facets// /,}

	sleep $RACER_FAILOVER_PERIOD
	while [ ! -f $racer_done ]; do
		local facet=$(get_random_entry $facets)
		facet_failover $facet
		sleep $RACER_FAILOVER_PERIOD
	done
}

# run racer
test_1() {
	local rrc=0
	local rc=0
	local clients=$CLIENTS
	local RDIRS
	local i
	local racer_done=$TMP/racer_done

	rm -f $racer_done

	for d in ${RACERDIRS}; do
		is_mounted $d || continue

		RDIRS="$RDIRS $d/racer"
		mkdir -p $d/racer
		if [[ -n "$RACER_EXTRA_LAYOUT" ]]; then
			$LFS setstripe $d/racer $RACER_EXTRA_LAYOUT ||
			error "setstripe $RACER_EXTRA_LAYOUT failed"
		fi
		if (( MDSCOUNT >= 2 )); then
			for ((i = 0; i < MDSCOUNT; i++)); do
				RDIRS="$RDIRS $d/racer$i"
				if [[ ! -e $d/racer$i ]]; then
					$LFS mkdir -i $i $RACER_MKDIR_OPTS $d/racer$i ||
						error "lfs mkdir $i failed"
				fi
				if [[ -n "$RACER_EXTRA_LAYOUT" ]]; then
					$LFS setstripe $d/racer$i \
						$RACER_EXTRA_LAYOUT ||
					error "setstripe \
						$RACER_EXTRA_LAYOUT failed"
				fi
			done
		fi
	done

	local rpids=""
	for rdir in $RDIRS; do
		do_nodes $clients "DURATION=$DURATION \
			MDSCOUNT=$MDSCOUNT OSTCOUNT=$OSTCOUNT\
			RACER_ENABLE_REMOTE_DIRS=$RACER_ENABLE_REMOTE_DIRS \
			RACER_ENABLE_STRIPED_DIRS=$RACER_ENABLE_STRIPED_DIRS \
			RACER_ENABLE_MIGRATION=$RACER_ENABLE_MIGRATION \
			RACER_ENABLE_FILE_MIGRATE=$RACER_ENABLE_FILE_MIGRATE \
			RACER_ENABLE_PFL=$RACER_ENABLE_PFL \
			RACER_ENABLE_DOM=$RACER_ENABLE_DOM \
			RACER_ENABLE_FLR=$RACER_ENABLE_FLR \
			RACER_MAX_CLEANUP_WAIT=$RACER_MAX_CLEANUP_WAIT \
			RACER_ENABLE_SEL=$RACER_ENABLE_SEL \
			LOV_MAX_STRIPE_COUNT=$LOV_MAX_STRIPE_COUNT \
			RACER_ENABLE_OVERSTRIPE=$RACER_ENABLE_OVERSTRIPE \
			RACER_LOV_MAX_STRIPECOUNT=$RACER_LOV_MAX_STRIPECOUNT \
			RACER_EXTRA=$RACER_EXTRA \
			RACER_EXTRA_LAYOUT=\\\"$RACER_EXTRA_LAYOUT\\\" \
			RACER_MIGRATE_STRIPE_MAX=$RACER_MIGRATE_STRIPE_MAX \
			RACER_PROGS=$RACER_PROGS \
			NUM_THREADS=$NUM_THREADS \
			MAX_FILES=$MAX_FILES \
			LFS=$LFS \
			LCTL=$LCTL \
			$racer $rdir $NUM_RACER_THREADS" &
		pid=$!
		rpids="$rpids $pid"
	done

	local failpid=""
	if $RACER_FAILOVER; then
		fail_random_facet &
		failpid=$!
		echo racers failpid: $failpid
	fi

	local lss_pids=""
	if $RACER_ENABLE_SNAPSHOT; then
		lss_gen_conf

		$LUSTRE/tests/racer/lss_create.sh &
		pid=$!
		lss_pids="$lss_pids $pid"

		$LUSTRE/tests/racer/lss_destroy.sh &
		pid=$!
		lss_pids="$lss_pids $pid"
	fi

	echo racers pids: $rpids
	for pid in $rpids; do
		wait $pid
		rc=$?
		echo "pid=$pid rc=$rc"
		if [ $rc != 0 ]; then
		    rrc=$((rrc + 1))
		fi
	done

	if $RACER_FAILOVER; then
		touch $racer_done
		wait $failpid
		rrc=$((rrc + $?))
	fi

	if $RACER_ENABLE_SNAPSHOT; then
		killall -q lss_create.sh
		killall -q lss_destroy.sh

		for pid in $lss_pids; do
			wait $pid
		done

		lss_cleanup
	fi

	return $rrc
}
run_test 1 "racer on clients: ${CLIENTS:-$(hostname)} DURATION=$DURATION"

# racer rename stress test
test_2() {
	local rrc=0
	local rc=0
	local clients=$CLIENTS
	local RDIRS
	local i
	local racer_done=$TMP/racer_done

	(( MDSCOUNT > 1 )) || skip "need at least 2 MDTs"

	rm -f $racer_done

	for d in ${RACERDIRS}; do
		is_mounted $d || continue
		mkdir -p $d

		for ((i = 0; i < $MDSCOUNT; i++)); do
			RDIRS+=" $d/racer$i"
			[[  -d "$d/racer$i" ]] && continue
			$LFS mkdir $RACER_MKDIR_OPTS $d/racer$i ||
				error "mkdir $d/racer$i failed"
			if [[ -n "$RACER_EXTRA_LAYOUT" ]]; then
				$LFS setstripe $d/racer$i $RACER_EXTRA_LAYOUT ||
					error "extra $RACER_EXTRA_LAYOUT failed"
			fi
		done
	done

	local rpids=""
	local progs="dir_create+dir_remote+file_rename+file_rename+file_create+file_rm"
	for rdir in $RDIRS; do
		echo "starting on $clients:$rdir with: ${progs//+/ }"
		do_nodes $clients "DURATION=$DURATION \
			MDSCOUNT=$MDSCOUNT OSTCOUNT=$OSTCOUNT\
			RACER_MAX_MB=0 \
			RACER_ENABLE_FLR=false \
			RACER_ENABLE_DOM=false \
			RACER_ENABLE_SEL=false \
			RACER_ENABLE_MIGRATION=false \
			RACER_MAX_CLEANUP_WAIT=$RACER_MAX_CLEANUP_WAIT \
			RACER_EXTRA=\\\"$RACER_EXTRA\\\" \
			RACER_EXTRA_LAYOUT=\\\"$RACER_EXTRA_LAYOUT\\\" \
			RACER_PROGS="$progs" \
			NUM_THREADS=$NUM_THREADS \
			MAX_FILES=$MAX_FILES \
			LFS=$LFS \
			LCTL=$LCTL \
			$racer $rdir $NUM_RACER_THREADS" &
		pid=$!
		rpids="$rpids $pid"
	done

	echo racers pids: $rpids
	for pid in $rpids; do
		wait $pid
		rc=$?
		echo "pid=$pid rc=$rc"
		if [ $rc != 0 ]; then
		    rrc=$((rrc + 1))
		fi
	done

	return $rrc
}
run_test 2 "racer rename: ${CLIENTS:-$(hostname)} DURATION=$DURATION"

complete_test $SECONDS
FSCK_ALWAYS=${FSCK_ALWAYS:-"yes"} check_and_cleanup_lustre

exit_status
