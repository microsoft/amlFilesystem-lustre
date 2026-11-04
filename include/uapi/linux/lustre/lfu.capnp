# SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
#
# LFU object stream: control and data wire format.
#
# Definitions:
# ============
#
#   design                  here        does
#   Input Scanner Module    scanner     produces the stream
#   Filter Rule Module      stage       consumes a stream, produces another
#   Output Format Module    consumer    consumes the stream
#   attribute mask          projection  which attributes records carry
#   Raw Write / Raw Read    transcript  the stream saved to a file
#
#   module                    here
#   ldiskfs Device Scanner    LfuSource.device; LfuKind.mdtInodes, ostObjects
#   Lustre Namespace Scanner  LfuSource.namespace; LfuEa.path
#   Changelog Scanner         LfuSource.changelog; LfuKind.changelog;
#                             LfuOpen.changelogUser
#   Raw Read / Raw Write      LfuSource.transcript; "Transcript" below
#   lfs find Filter           LfuOpen.filter; LfuField; LfuOp
#   Merge / Split             "Pipelines" below
#   Changelog Output Filter   LfuOpen.filter on an LfuKind.changelog stream
#   Text: FID + Pathname      LfuEa.path; LfuLookup with LfuProjection.path
#   OSD API Scanner           LfuSource.osd
#   Index Scanner             LfuSource.index; LfuOpen.order; LfuInode.valid
#   POSIX Scanner             LfuSource.posix
#   Advanced Operators        LfuOp.in; a range is two LfuTerms;
#                             LfuOpen.order; LfuOpen.aggregates
#   Server / Client Bulk RPC  LfuOpen.accessOnly; "LNet" under "Transports"
#   Structured Formats,       consumers of the stream; nothing here
#   Consumer Plugins
#   Persistent Aggregates     LfuBucket holds what the design would store;
#                             storing it is not in this file
#
# Pipelines:
# ==========
#
# A stream flows from a scanner, through any number of stages, to a
# consumer.  Each arrow below is a "hop".  Every hop speaks this protocol,
# whether it is a function call, a kernel/user boundary or a network.
#
#   +---------+     +-------+     +-------+     +----------+
#   | scanner |---->| stage |---->| stage |---->| consumer |
#   +---------+     +-------+     +-------+     +----------+
#    MDT, OST,     lfs find      merge,        text, raw
#    changelog,    filter,       split,        write, JSON,
#    index         access        LNet          RobinHood
#
#   each ---->  is one hop: LfuRequest down, LfuReply and bulk up
#
# On a hop the downstream side is the consumer and the upstream side the
# producer.  A stage is both.  Handles and cursors belong to one hop.
#
# Merge and split (Design: Filter Rule Modules, Output Format modules) need
# nothing extra.  A merge stage opens each source upstream and makes
# cursors of its own that name a position in each.  A split stage opens
# upstream once and serves one handle per consumer.
#
#   scanner A --+                                 +--> consumer 1
#               +--> merge --> ...    ... --> split
#   scanner B --+                                 +--> consumer 2
#
# A stage:
#  - rebuilds LfuOpen from the fields it knows before forwarding it, and
#    adds to the projection what its own work needs ("Negotiation");
#  - reports in LfuOpenReply.applied what upstream applied plus what it
#    applies, in .features what upstream confirmed and it supports, and
#    in .projection what it passes on;
#  - forwards LfuLookup upstream, or serves it from its own source;
#  - may drop objects by building a new chunk, or by shortening the list
#    in place ("Notes for implementers");
#  - sums LfuChunk.scanned and .errors over the chunks it consumed, adds
#    its own errors, and unions .skipped;
#  - sends only cursors it can resume from itself.
#
#
# Operation:
# ==========
#
# The consumer drives the stream with four requests.  Each travels in an
# LfuRequest and is answered by an LfuReply.
#
#   consumer                                      producer
#      |  LfuOpen: what to scan, what to send        |
#      |-------------------------------------------->|
#      |  LfuOpenReply: handle, what was granted     |
#      |<--------------------------------------------|
#      |  LfuRead: handle, cursor, maxSlots          |
#      |-------------------------------------------->|
#      |  LfuReadReply + bulk of chunks              |
#      |<--------------------------------------------|
#      |        ... read again until eof ...         |
#      |  LfuLookup: handle, fids        (optional)  |
#      |-------------------------------------------->|
#      |  LfuReadReply + bulk                        |
#      |<--------------------------------------------|
#      |  LfuClose: handle                           |
#      |-------------------------------------------->|
#      |  LfuCloseReply: summary                     |
#      |<--------------------------------------------|
#
# Probe.  LfuOpen.probe gets the reply of an open with no handle and no
# scan.  That is how a consumer learns, before scanning, what the producer
# would do and what it would leave to the consumer (Design: Client and
# Server Interoperation).
#
# Resume.  Every chunk and read reply carries a cursor.  Every read says
# where to start.  A lost reply is repeated.  After a restart, open again
# and read from the last cursor finished.  -ESTALE means start the shard
# over.  What is inside a cursor is the producer's business.
#
# Lookup.  A source that holds only FIDs, such as an index or a changelog,
# sends partial records.  LfuLookup fetches full records for named FIDs,
# in the order asked.  This is the design's "attributes on demand" and its
# bulk fid2path() (Design: Input Scanner Modules, Output Format modules).
#
# Release.  A changelog keeps records until the consumer says it is done
# with them in LfuRead.release, on behalf of LfuOpen.changelogUser.  A
# scanner ignores it.
#
# Follow.  A changelog grows and never reports eof.  A read at its end
# waits up to LfuRead.waitMsec, then answers an empty bulk with the same
# cursor.
#
# Shards.  One handle per shard (LfuOpen.shardIndex, shardCount), read
# concurrently.  One stream per target (LfuOpen.target).
#
#
# Negotiation:
# ============
#
# The design wants feature flags, not version numbers, and wants the
# consumer to learn at open time what runs on the server, what on the
# client and what not at all (Design: Client and Server Interoperation).
# The open asks; the reply says what was granted:
#
#   LfuOpen asks for          LfuOpenReply answers
#   features                  features: what every stage supports of them
#   filter, subtree, stages,  applied: which of these some stage did;
#   order, aggregates,                 the rest is for the consumer
#   accessOnly
#   projection, optional      projection: what the records will carry
#   source, target            source: the scanner used; sources: the
#                             scanners this producer has for this target
#   probe                     all of the above, no handle, no scan
#
# A stage never guesses at a value it does not know:
#
#   unknown                       the stage
#   LfuKind, LfuSource            fails the open
#   LfuField or LfuOp in a term   reports that field as not applied
#   stage name                    leaves that stage's bit clear
#   feature bit                   leaves it clear in the reply
#   bulk encoding                 answers with "slots"
#   request                       answers -EOPNOTSUPP
#
# A stage forwards a request by building a new one from the fields it
# knows, never by passing the bytes on.  Otherwise upstream would confirm
# a field the stage cannot handle.
#
#
# Bulk Format:
# ============
#
# A bulk is an array of slots of chunkSize bytes (LfuOpenReply).  A chunk
# is one standard, unpacked, single-segment Cap'n Proto message with root
# LfuChunk.  It starts on a slot boundary and is padded to a slot boundary,
# so the segment table alone says how many slots it spans.
#
#   slot 0          slot 1          slot 2          slot 3
#   +---------------+---------------+---------------+---------------+
#   |hdr| chunk A  0|hdr| chunk B (jumbo, 2 slots) 0|hdr| chunk C  0|
#   +---------------+---------------+---------------+---------------+
#    S/8-1 words     2*S/8-1 words                   S/8-1 words
#
#   hdr: 8 byte segment table (segment count - 1 = 0, size in words)
#   0:   zero padding to the slot end
#   S:   chunkSize
#
# Rules:
#  - A chunk normally fills one slot.  One too large for a slot spans K
#    slots ("jumbo"), only with LfuFeature.jumboChunks.  Without it the
#    object is skipped.
#  - Unused words are zero.  Chunks never point at each other.
#  - Only the first LfuReadReply.slots slots are valid.  The valid part is
#    a plain Cap'n Proto message stream: "capnp decode lfu.capnp LfuChunk"
#    reads a raw dump of it.
#
# Why: one segment per chunk means no far pointers, and each chunk decodes
# on its own.  A negotiated slot size, not PAGE_SIZE, keeps page size off
# the wire.  Padding to the slot end keeps stock readers happy.  Jumbo
# chunks keep an object whole and the slot size fixed.  Unpacked keeps
# reading in place possible.
#
# Bulk encodings.  More than half the bytes of a slot are zero.  A hop
# that copies the bulk anyway may carry it smaller.  The consumer asks in
# LfuRead.bulkEncoding and LfuReadReply says what it got:
#
#   slots    the slots as they are
#   trimmed  the slots minus the zero tail of the bulk; the consumer puts
#            it back; nothing is transformed, so it can receive straight
#            into its buffer
#   packed   each chunk in standard Cap'n Proto packed encoding, one after
#            the other; "capnp decode --packed" reads it; costs a copy
#
# A producer may always answer "slots".  Handles, cursors, slot counts and
# the chunks are the same in every encoding.
#
#
# Records:
# ========
#
#   LfuChunk: scanned, errors, cursor, skipped[]
#             one of: empty | inodes[] | fids[] | buckets[] | changelogs[]
#                     | end
#
#   LfuInode: 128 bytes, read in place        LfuEa: written only up to
#   +-------------------------------+----+    its last non-null field
#   | fid(16) size blocks atime     |    |    +------+-----+------+-----
#   | mtime ctime btime mode uid    | ea |--->| path | fid | link | lov ..
#   | gid projid nlink flags rdev   |    |    +--+---+-----+------+-----
#   | valid ino atimeNsec mtimeNsec |    |       |
#   | ctimeNsec btimeNsec           |    |       v
#   +-------------------------------+----+
#                                            bytes as stored (Data)
#
#  - The FID is three scalars, not an LfuFid, to avoid a pointer.
#  - LfuKind.mdtInodes and ostObjects both use LfuInode.  An OST object
#    carries its parent in LfuEa.fid.
#  - Partial records.  With LfuFeature.partialRecords, LfuInode.valid says
#    which scalars are filled.  For sources that hold only FIDs (Design:
#    Index Input Scanner Module).  LfuLookup fills them in.
#  - valid is 32 bits.  Fourteen scalars are defined; the nanoseconds go
#    with their times.  Past 32, append a second mask.
#  - FID-only.  LfuProjection.fidsOnly with LfuFeature.fidChunks gives
#    LfuChunk.fids: 16 bytes per object.
#  - Skipped.  Every object a stage could not send is in LfuChunk.skipped
#    with its errno (LfuFeature.skipped) and counted in LfuChunk.errors.
#    A count alone would hide which wide mirrored files resync missed
#    (Design: Testing New Functionality, File Level Redundancy Resync).
#  - EAs sit behind one pointer because every element of a struct list has
#    the same size.  LfuEa is cut after its last non-null field, and its
#    fields are ordered by how often they are sent: a path alone costs 8
#    bytes, an OST parent alone 16.  With c-capnproto, allocate it with
#    capn_new_struct(), not new_LfuEa().
#  - EA values are the bytes the OSD stores.  This file does not decode
#    them.  Filtering on their content uses the derived LfuField values.
#  - LfuEa.path is the one non-EA: where a namespace scanner puts the
#    path.  Ask for it in LfuOpen.optional (Design: Input Scanner Modules).
#  - Objects are identified by FID.  The one exception is LfuSource.posix.
#  - Timestamps are seconds, as "lfs find" compares them, with the
#    nanoseconds beside them in their own fields.  A changelog record
#    keeps its nanoseconds too.
#  - Names and paths are Data, not Text.  They need not be UTF-8.
#  - Every type is prefixed Lfu so generated C names do not collide.
#
#
# Filtering, ordering and aggregation
#
# What an open can ask a pipeline to do (Design: Filter Rule Modules):
#
#   LfuOpen.filter      clauses of terms (LfuField, LfuOp): every "lfs
#                       find" test but -skip; "in list"; a range is two
#                       terms of one clause
#   LfuOpen.subtree     only objects below a directory
#   LfuOpen.stages      pluggable filters by name, with their own args
#   LfuOpen.order       largest / smallest on an indexed field (Design:
#                       Index Input Scanner Module)
#   LfuOpen.aggregates  histograms and per-user, -group, -project and
#                       -directory totals, computed where the objects are
#                       (Design: Filesystem Usage Reports)
#   LfuOpen.accessOnly  only what the caller may see (Design: Server Bulk
#                       RPC Filter Rule Module)
#
# LfuOpenReply.applied says which of these were done.  The consumer does
# the rest itself, except order and access, which it cannot.
#
# LfuField has three groups: the LfuInode scalars; values derived from
# the EAs (stripes, pools, mirrors, HSM state); and the fields of a
# changelog record.  A stage that evaluates a derived field asks upstream
# for the EA but does not pass it on unless the consumer asked.
#
# Partial filters.  A stage that cannot evaluate a term drops it from its
# clause, which only widens the match.  The stream is then a superset and
# LfuApplied.filterFields says which fields the consumer must re-check.
#
#   "-uid 500 -name '*.dat'" served by an MDT scanner:
#     uid   evaluated on the server     bit set
#     name  an MDT scan has no names    bit clear; the client re-checks
#
#
# Transports:
# ===========
#
#   function call   two stages in one process
#   pipe / socket   stream-framed LfuRequest and LfuReply; a bulk follows
#                   its LfuReply directly
#   transcript      the design's Raw Write and Raw Read; below
#   kernel ring     the design's Kernel API for Object Stream; below
#   LNet            the design's Server and Client Bulk RPC: the bulk is
#                   one LNet bulk transfer.  How the control messages ride
#                   in a ptlrpc RPC, and OBD_CONNECT2_FIND_UTILITY, are not
#                   defined yet (Design: Client and Server Interoperation)
#
# Transcript file: the reply side of one hop, which is all a file can hold.
#
#   +----------+-----------+-----------+------+     +-----------+------+
#   | open req | open rep  | read rep  | bulk | ... | read rep  | bulk |
#   |          |           |           |      |     | eof=true  |      |
#   +----------+-----------+-----------+------+     +-----------+------+
#
# Each bulk is LfuReadReply.slots slots in LfuReadReply.bulkEncoding.
# Transcripts may follow one another.  The recorded open and reply let a
# Raw Read scanner answer a new open honestly: applied, target, source
# and projection as recorded.  Lookups come from the recording if it is
# indexed, else -EOPNOTSUPP.
#
# Kernel ring (Design: Kernel API for Object Stream): open, lookup and
# close are ioctls.  The ring is LfuRead.maxSlots slots.  Chunks appear in
# it with no control message per bulk.
#
#   +-------+-------+-------+-------+-------+-------+
#   | chunk | jumbo chunk   | chunk | empty | empty |  pad to the end,
#   +-------+-------+-------+-------+-------+-------+  never wrap
#   | chunk | chunk | chunk | end   |       |       |  LfuChunk.end closes
#   +-------+-------+-------+-------+-------+-------+  the stream
#
# What LfuReadReply would carry is in the chunks instead: the cursor in
# each, and the status and summary in a final LfuChunk.end.  So a ring
# producer requires LfuFeature.endChunk.  A chunk that would not fit an
# empty ring is a skipped object (-EOVERFLOW).
#
#
# Schema rules:
# =============
#
# Kept within what capnpc-c generates and kernel code can use:
#  - integer, Bool, enum, Data, Text, struct and List fields only
#  - no Float, no interfaces, no generics, no default values
#  - fields and enum values are only ever appended.  Old readers ignore
#    new fields; unwritten fields read as zero or null.  Safe for records.
#    Not safe for requests, which is why the reply confirms every optional
#    behaviour and a stage rebuilds requests ("Negotiation")
#  - a new LfuChunk union member is sent only under a new LfuFeature bit
#    or LfuKind, so an old stage never meets it
#  - Data holding a Lustre structure, a name or a path is carried as
#    stored; its layout is not defined here
#
#
# Notes:
# ======
#
# Avoiding copies:
#  - A chunk needs one linear address range.  A slot larger than a page
#    needs a virtually contiguous bulk buffer (vmalloc or vmap) with its
#    pages handed to LNet.
#  - A userspace consumer receives without a copy if the kernel pins its
#    page-aligned buffer as the bulk sink, as direct I/O does.
#  - capnpc-c's read_X/write_X helpers copy through a C struct.  Its
#    per-field accessors need the fieldgetset annotation from c.capnp,
#    which this file does not import so that it builds with stock capnp.
#  - A struct list needs its count when allocated.  To avoid staging a
#    batch, lay the chunk out by hand: records from the front, EA values
#    from the end, list header last.  Valid, but c-capnproto's allocator
#    will not build it.
#  - What still copies: the scanner writing each object, socklnd receive,
#    bulk encryption, any stage that builds new chunks.
#
# c-capnproto:
#  - To grow a segment for a jumbo chunk, raise its capacity by a slot
#    before allocating.  The "create" hook registers a new segment instead.
#  - Constants come out as extern variables; enum values are compile-time
#    constants.  That is why the bit numbers below are enums.
#
# Dropping objects in place: move kept records down over dropped ones,
# add the distance moved (in words) to each moved record's "ea" pointer
# offset, then write the new count into the list tag word and the new
# word count into the list pointer.
#
# Pluggable stages: a stage name fixes its argument format for good.  The
# Cap'n Proto file defining the arguments is identified by its file id and
# only grows by the rules above.  Names beginning "lfu." are reserved.
#
# Trust: nothing crossing a hop is trusted.  Bounds-check every pointer of
# a chunk you did not build.  Validate or authenticate cursors and release
# values; they are your own state handed back by a peer.  Decode only the
# LfuStage.args you know.  Apply LfuOpen.accessOnly whether asked or not
# for an unprivileged caller, with the identity from the transport, never
# from the request.
#

@0xbd31f21b2afd124b;

const lfuChunkSizeMin :UInt32 = 4096;
const lfuChunkSizeDefault :UInt32 = 65536;
const lfuChunkSizeMax :UInt32 = 1048576;

struct LfuFid {
  # struct lu_fid
  seq @0 :UInt64;
  oid @1 :UInt32;
  ver @2 :UInt32;
}

enum LfuKind {
  # What a stream carries.  Selects the LfuChunk union member.
  mdtInodes @0;
  # MDT objects as LfuChunk.inodes.
  changelog @1;
  # Changelog records as LfuChunk.changelogs (Design: Changelog Input
  # Scanner Module).
  ostObjects @2;
  # OST objects as LfuChunk.inodes (Design: OST Object Index).  The parent
  # FID is in LfuEa.fid.
}

enum LfuSource {
  # Which scanner produces the stream (Design: Input Scanner Modules).  A
  # value of LfuOpen.source and LfuOpenReply.source, and a bit number of
  # LfuOpenReply.sources.
  any @0;
  # In LfuOpen: the producer chooses.  In a reply: not reported.
  osd @1;
  # Design: OSD API Input Scanner Module.
  device @2;
  # Design: ldiskfs Device Input Scanner Module.  Works on a server without
  # LFU support (Design: Usage on Old Servers).  May lag the running server
  # by seconds (Design: Inconsistencies in Scanning Results).
  namespace @3;
  # Design: Lustre Namespace Input Scanner Module.  Works on a client
  # without LFU support (Design: Usage on Old Clients).
  posix @4;
  # Design: POSIX Input Scanner Module: a filesystem that is not Lustre
  # (Design: Client-Side File Cache, Trash Can Undelete).  Its objects have
  # no FID, so a record carries st_ino in fidSeq, st_dev in fidOid and 0 in
  # fidVer.  LfuEa.path is always sent.  Lustre EAs are never sent.
  changelog @5;
  # Design: Changelog Input Scanner Module.
  index @6;
  # Design: Index Input Scanner Module.
  transcript @7;
  # The design's Raw Read (Design: Input Scanner Modules).
}

enum LfuFeature {
  # Optional parts of this format.  Bit numbers of LfuOpen.features and
  # LfuOpenReply.features.  See "Negotiation".

  jumboChunks @0;
  # Chunks may span several slots.
  partialRecords @1;
  # LfuInode.valid is meaningful; records may lack scalars.
  fidChunks @2;
  # LfuChunk.fids may be sent for a projection with fidsOnly.
  lookup @3;
  # LfuRequest.lookup is accepted on the handle.
  bucketChunks @4;
  # LfuChunk.buckets may be sent.  LfuOpen.aggregates needs it.
  skipped @5;
  # LfuChunk.skipped is filled.
  endChunk @6;
  # The last chunk of the stream is an LfuChunk.end.
}

enum LfuBulkEncoding {
  # How the bulk of a read is carried on a hop.  See "Bulk encodings".
  slots @0;
  trimmed @1;
  packed @2;
}

# Data

struct LfuXattr {
  name @0 :Data;
  # Full name with namespace prefix, no trailing NUL.
  value @1 :Data;
}

struct LfuEa {
  # EAs of one object.  A field is null if the EA is absent or was not
  # requested.  Written cut after its last non-null field, so the fields
  # most often sent come first.

  path @0 :Data;
  # Not an EA.  The object's path relative to the root of the scan or to
  # LfuOpen.subtree: no leading "/", empty for that directory itself.  Set
  # by a namespace scanner or by a stage that resolves "link".
  fid @1 :Data;
  # "trusted.fid", struct filter_fid: the parent of an OST object.  Only
  # for LfuKind.ostObjects.
  link @2 :Data;
  # "trusted.link"
  lov @3 :Data;
  # "trusted.lov"
  lmv @4 :Data;
  # "trusted.lmv"
  hsm @5 :Data;
  # "trusted.hsm"
  som @6 :Data;
  # "trusted.som"
  lma @7 :Data;
  # "trusted.lma"
  dmv @8 :Data;
  # "trusted.dmv"
  xattrs @9 :List(LfuXattr);
  # Every other selected EA.
  enc @10 :Data;
  # The encryption context, struct fscrypt_context, as ldiskfs keeps it
  # under the name "c" in its encryption index.  Says whether the names
  # in an encrypted directory are encrypted too.
}

enum LfuAttr {
  # Bit numbers of LfuInode.valid: the scalar fields of a record.
  size @0;
  blocks @1;
  atime @2;
  mtime @3;
  ctime @4;
  btime @5;
  mode @6;
  uid @7;
  gid @8;
  projid @9;
  nlink @10;
  flags @11;
  rdev @12;
  ino @13;
}

struct LfuInode {
  # One MDT or OST object.  All scalars are filled unless
  # LfuFeature.partialRecords is in effect, when "valid" says which are.

  fidSeq @0 :UInt64;
  fidOid @1 :UInt32;
  fidVer @2 :UInt32;

  size @3 :UInt64;
  # As stored on the inode.  On an MDT this is the lazy size; the "som" EA
  # says how far to trust it.
  blocks @4 :UInt64;
  # 512 byte units, same caveat as size.

  atime @5 :Int64;
  mtime @6 :Int64;
  ctime @7 :Int64;
  btime @8 :Int64;
  # Seconds since the epoch; the nanoseconds are in atimeNsec and the rest.

  mode @9 :UInt32;
  # S_IFMT | 07777
  uid @10 :UInt32;
  gid @11 :UInt32;
  projid @12 :UInt32;
  nlink @13 :UInt32;
  flags @14 :UInt32;
  # LUSTRE_*_FL inode flags.
  rdev @15 :UInt32;

  ea @16 :LfuEa;
  # Null if no selected EA is present.

  valid @17 :UInt32;
  # Bits numbered by LfuAttr: the scalars that are filled.  0 unless
  # LfuFeature.partialRecords is in effect.

  ino @18 :UInt64;
  # The object's inode number on its target, as the backend numbers it.
  # It is not unique across targets and is not the number a client sees,
  # which is made from the FID.  It names an object that has no FID, and
  # it is what "lfs find --device -printf %i" prints.  0 if none.

  atimeNsec @19 :UInt32;
  mtimeNsec @20 :UInt32;
  ctimeNsec @21 :UInt32;
  btimeNsec @22 :UInt32;
  # Nanoseconds within the second of atime, mtime, ctime and btime, 0
  # where the source keeps whole seconds.  Filled with the time each goes
  # with: the LfuAttr bit of the time covers its nanoseconds too.
}

struct LfuChangelog {
  # One changelog record: struct changelog_rec and its extensions as
  # stored, flattened (Design: Changelog Input Scanner Module).  Absent
  # extensions read as zero or empty; "flags" and "extra" say which are
  # present.

  index @0 :UInt64;
  # cr_index.  A changelog cursor names the next index; LfuRead.release
  # names the last one consumed.
  type @1 :UInt32;
  # cr_type: CL_CREATE, CL_UNLINK, CL_RENAME, CL_LAYOUT, CL_FLRW, ...
  flags @2 :UInt32;
  # cr_flags: CLF_FLAGMASK operation flags and the CLF_RENAME, CLF_JOBID
  # and CLF_EXTRA_FLAGS extension markers.
  time @3 :Int64;
  # cr_time >> 30: seconds since the epoch.
  timeNsec @4 :UInt32;
  # The low 30 bits of cr_time.
  openFlags @5 :UInt32;
  # cr_openflags, with CLFE_OPEN in "extra".
  prev @6 :UInt64;
  # cr_prev: the last index for this target FID.
  extra @7 :UInt64;
  # cr_extra_flags, CLFE_*: which of uid, gid, nid, openFlags and xattr
  # are present.

  tfidSeq @8 :UInt64;
  tfidOid @9 :UInt32;
  tfidVer @10 :UInt32;
  # cr_tfid, the target.  Zero for CL_MARK.
  pfidSeq @11 :UInt64;
  pfidOid @12 :UInt32;
  pfidVer @13 :UInt32;
  # cr_pfid, the parent.
  sfidSeq @14 :UInt64;
  sfidOid @15 :UInt32;
  sfidVer @16 :UInt32;
  spfidSeq @17 :UInt64;
  spfidOid @18 :UInt32;
  spfidVer @19 :UInt32;
  # cr_sfid and cr_spfid of a rename, with CLF_RENAME in "flags".

  uid @20 :UInt32;
  gid @21 :UInt32;
  # The caller, with CLFE_UIDGID in "extra".  The target's owner is in
  # "inode".

  name @22 :Data;
  # The name in the parent, no trailing NUL.
  sname @23 :Data;
  # The source name of a rename.
  jobid @24 :Data;
  # cr_jobid, with CLF_JOBID in "flags".  No trailing NUL.
  nid @25 :Data;
  # cr_nid as stored, with CLFE_NID in "extra": an lnet_nid_t, or a struct
  # lnet_nid when CLFE_NID_BE is also set.
  xattr @26 :Data;
  # cr_xattr, the EA name a CL_SETXATTR changed, with CLFE_XATTR in
  # "extra".  No trailing NUL.

  inode @27 :LfuInode;
  # The target's attributes, when LfuProjection.inode asks for them and
  # the target still exists (Design: Changelog Output Filter Module).  A
  # filter term on an inode field applies to this.  A producer that cannot
  # fetch it reports the field as not applied.
}

struct LfuSkipped {
  # One object a stage could not send (Design: Testing New Functionality).
  fidSeq @0 :UInt64;
  fidOid @1 :UInt32;
  fidVer @2 :UInt32;
  error @3 :Int32;
  # A negative Linux errno: -EOVERFLOW does not fit the slots available,
  # -EIO could not be read, -ENOENT does not exist (lookup), -EOPNOTSUPP a
  # stage could not evaluate it.
}

struct LfuBucket {
  # One bucket of one of LfuOpen.aggregates.  Buckets with the same
  # aggregate, key, directory and index merge across chunks, shards and
  # sources: count and sum add, min and max combine (Design: Filter Rule
  # Modules).

  key @0 :UInt64;
  # The value of LfuAggregate.groupBy, or 0 if not grouped.
  index @1 :UInt32;
  # Index into LfuAggregate.bounds; bounds.length for values at or above
  # the last bound.
  count @2 :UInt64;
  sum @3 :UInt64;
  min @4 :UInt64;
  max @5 :UInt64;
  # Of LfuAggregate.field over the objects of the bucket.  Time fields are
  # Int64 values.
  aggregate @6 :UInt32;
  # Index into LfuOpen.aggregates.

  dirSeq @7 :UInt64;
  dirOid @8 :UInt32;
  dirVer @9 :UInt32;
  # The parent directory, with LfuAggregate.perDirectory.  Zero otherwise.
}

struct LfuSummary {
  # Totals for one handle.
  scanned @0 :UInt64;
  # Sum of LfuChunk.scanned.
  emitted @1 :UInt64;
  # Objects this stage sent.
  errors @2 :UInt64;
  # Sum of LfuChunk.errors.
  slots @3 :UInt64;
  # Slots this stage sent.
  elapsedUsec @4 :UInt64;
}

struct LfuEnd {
  # The end of a stream, as a chunk (LfuFeature.endChunk).
  status @0 :Int32;
  # 0 if the shard is exhausted, or the negative Linux errno that ended
  # the stream early.
  summary @1 :LfuSummary;
}

struct LfuChunk {
  scanned @0 :UInt32;
  # Objects the scanner examined for this chunk, including those dropped
  # by any stage and those counted in "errors".
  errors @1 :UInt32;
  # Objects skipped because a stage could not read or evaluate them, or
  # because they do not fit in LfuOpenReply.maxSlots slots.

  cursor @2 :Data;
  # Position just after the last object this chunk accounts for.  Empty
  # only in the chunks of a lookup.  See LfuRead.cursor.

  skipped @7 :List(LfuSkipped);
  # The objects counted in "errors", with LfuFeature.skipped.  Sent in any
  # kind of chunk.

  union {
    empty @3 :Void;
    # Counters, cursor and skipped only: progress through a range where
    # nothing matched, or padding at the end of a ring.
    inodes @4 :List(LfuInode);
    # LfuKind.mdtInodes and ostObjects, and the answer to any lookup.
    fids @5 :List(LfuFid);
    # Any kind, with LfuProjection.fidsOnly and LfuFeature.fidChunks.
    buckets @6 :List(LfuBucket);
    # With LfuApplied.aggregate and LfuFeature.bucketChunks: buckets
    # instead of objects, for every one of LfuOpen.aggregates.  A producer
    # may send them in any number of chunks; the consumer merges them.
    changelogs @8 :List(LfuChangelog);
    # LfuKind.changelog.
    end @9 :LfuEnd;
    # The last chunk of the stream, with LfuFeature.endChunk.
  }
}

# Control

struct LfuProjection {
  # The attributes the consumer needs (Design: Input Scanner Modules).  A
  # stage may ask its upstream for more, so a consumer may be sent EAs it
  # did not select and ignores them.

  lma @0 :Bool;
  lov @1 :Bool;
  link @2 :Bool;
  hsm @3 :Bool;
  som @4 :Bool;
  lmv @5 :Bool;
  dmv @6 :Bool;

  xattrs @7 :Bool;
  # EAs other than the ones above, into LfuEa.xattrs.
  xattrNames @8 :List(Data);
  # If not empty, limits "xattrs" to these names.

  fidsOnly @9 :Bool;
  # The scalars are not needed either.  A producer may then send
  # LfuChunk.fids (LfuFeature.fidChunks).

  path @10 :Bool;
  # LfuEa.path.  Required here, so a producer that cannot tell the path
  # fails the open.  Ask in LfuOpen.optional to get it only when a scan
  # has it at hand.

  inode @11 :Bool;
  # For LfuKind.changelog: the target's attributes in LfuChangelog.inode
  # (Design: Changelog Output Filter Module).  The EA fields above then say
  # what that inode carries.  Ignored for other kinds.

  fid @12 :Bool;
  # LfuEa.fid, for LfuKind.ostObjects.
}

enum LfuField {
  # A field of a record that an LfuTerm can test, and a bit number of
  # LfuApplied.filterFields.  A term on a field the stream's kind does not
  # have is dropped, like any term a stage cannot evaluate.  (Data) fields
  # are compared with LfuTerm.data, the rest with LfuTerm.value.  (multi)
  # fields have one value per component, stripe or name: a term matches
  # if any value matches, except ne, noneSet, notIn and the negative
  # globs, which match if no value does.

  # LfuInode scalars (Design: Summary module diagram, lfs find Filter).
  type @0;
  # mode & S_IFMT
  perm @1;
  # mode & 07777
  uid @2;
  gid @3;
  projid @4;
  size @5;
  blocks @6;
  atime @7;
  mtime @8;
  ctime @9;
  btime @10;
  nlink @11;
  flags @12;
  fidSeq @13;

  # Names ("lfs find" -name, -path).
  name @14;
  # (Data, multi) An entry name of the object: from the namespace walk, or
  # every name in "link".  For LfuKind.changelog, LfuChangelog.name.
  path @15;
  # (Data) LfuEa.path.

  # Derived from "trusted.lov": struct lov_mds_md and lov_comp_md.
  pool @16;
  # (Data, multi) The pool of a component ("lfs find" -pool).
  stripeCount @17;
  # (multi) lmm_stripe_count of a component (-stripe-count).
  stripeSize @18;
  # (multi) lmm_stripe_size of a component (-stripe-size).
  ostIndex @19;
  # (multi) The OST index of an object of the layout (-stripe-index).  "in"
  # over a list of OSTs is the design's OST evacuation scan (Design: OST
  # Object Index, File Storage Tier Migration).
  layoutPattern @20;
  # (multi) lmm_pattern of a component, LOV_PATTERN_* (-layout).
  compCount @21;
  # lcm_entry_count; 1 for a plain layout, 0 for none (-comp-count).
  compFlags @22;
  # (multi) lcme_flags of a component, LCME_FL_* (-comp-flags).  "anySet
  # LCME_FL_STALE" is a stale mirror (Design: File Level Redundancy
  # Resync).
  mirrorCount @23;
  # lcm_mirror_count, or 0 for a plain layout (-mirror-count).  "gt 1" is
  # a file with built-in redundancy (Design: File Level Redundancy Resync).
  layoutFlags @24;
  # lcm_flags, LCM_FL_*: the mirror state (-mirror-state).
  foreignType @25;
  # lfm_type of a foreign layout, LU_FOREIGN_TYPE_*, or 0 (-foreign).

  # Derived from "trusted.lmv".
  mdtCount @26;
  # lmv_stripe_count of a striped directory, or 0 (-mdt-count).
  mdtHash @27;
  # lmv_hash_type & LMV_HASH_TYPE_MASK (-mdt-hash).
  mdtIndex @28;
  # (multi) The MDT index of a stripe of a directory, or of the object
  # itself for anything else (-mdt-index).

  # Derived from "trusted.hsm", struct hsm_attrs.
  hsmState @29;
  # hsm_flags, HS_*: "anySet HS_RELEASED" is a released file (Design: File
  # Storage Tier Migration).
  hsmArchive @30;
  # hsm_arch_id.

  # LfuChangelog fields (Design: Changelog Output Filter Module).  A term
  # on an LfuInode scalar or a derived field applies to LfuChangelog.inode:
  # "mirrorCount gt 1" on a changelog stream is the recently written
  # redundant files of the resync use case.
  clIndex @31;
  clType @32;
  # "in" over a list of CL_* types picks the operations a consumer wants
  # (Design: Changelog Input Scanner Module).
  clFlags @33;
  clTime @34;
  # LfuChangelog.time, whole seconds.
  clUid @35;
  clGid @36;
  # The caller: "objects created by UID 1000".
  clJobid @37;
  # (Data)

  # The remaining "lfs find" tests.
  depth @38;
  # Components in LfuEa.path; 0 for the root of the scan (-mindepth,
  # -maxdepth).  Only a scan that has the path can evaluate it.
  compStart @39;
  # (multi) lcme_extent.e_start of a component (-comp-start).
  compEnd @40;
  # (multi) lcme_extent.e_end of a component (-comp-end).
  compId @41;
  # (multi) lcme_id of a component (-comp-id).
  extSize @42;
  # (multi) The extension size of a component with LCME_FL_EXTENSION
  # (-ext-size).
  xattr @43;
  # (Data) The value of the EA named by LfuTerm.name, as stored (-xattr
  # name=glob).  Absent EA: no match, except for ne and the negative
  # globs.  A stage that evaluates it adds the name to the
  # LfuProjection.xattrNames it forwards upstream.
}

enum LfuOp {
  eq @0;
  ne @1;
  lt @2;
  le @3;
  gt @4;
  ge @5;
  allSet @6;
  # (field & value) == value
  anySet @7;
  # (field & value) != 0
  noneSet @8;
  # (field & value) == 0
  in @9;
  # field is one of LfuTerm.values or dataValues: the design's "in list"
  notIn @10;
  # field is none of LfuTerm.values or dataValues
  glob @11;
  # (Data) fnmatch(3) of LfuTerm.data against the field, no flags: "lfs
  # find -name" on "name", -path on "path".
  notGlob @12;
  # (Data) the field does not match.
  iglob @13;
  # (Data) glob, ignoring case: -iname.
  notIglob @14;
  # (Data) iglob, negated.
}

struct LfuTerm {
  field @0 :LfuField;
  op @1 :LfuOp;
  value @2 :UInt64;
  # Time fields compare as Int64, everything else as UInt64.
  values @3 :List(UInt64);
  # For "in" and "notIn", which ignore "value".
  data @4 :Data;
  # For a Data field: the bytes compared with eq and ne, or the pattern of
  # a glob.  "value" is ignored.
  dataValues @5 :List(Data);
  # For "in" and "notIn" on a Data field.
  name @6 :Data;
  # For LfuField.xattr: the full EA name, no trailing NUL.  Ignored for
  # other fields.
}

struct LfuClause {
  terms @0 :List(LfuTerm);
  # All terms must match.  LfuOpen.filter is a list of clauses, any of
  # which may match.  "Regular files of uid 500, or anything over 1GiB":
  #   [type eq S_IFREG, uid eq 500]
  #   [size gt 1073741824]
  # A range is two terms of one clause:
  #   [size ge 1048576, size lt 1073741824]
  # Stale mirrors of one user's files (Design: File Level Redundancy
  # Resync):
  #   [uid eq 500, mirrorCount gt 1, compFlags anySet LCME_FL_STALE]
}

struct LfuStage {
  # One pluggable filter (Design: Filter Rule Modules).
  name @0 :Text;
  # Selects the filter module, and with it the format of "args" for good.
  args @1 :Data;
  # A stream-framed Cap'n Proto message meaningful only to that module.
}

struct LfuOrder {
  # Objects in order of a field, cut after a number of them: the design's
  # "largest" and "smallest" (Design: Index Input Scanner Module).  Applied
  # per shard; a consumer reading several shards merges them.
  field @0 :LfuField;
  descending @1 :Bool;
  limit @2 :UInt64;
  # 0: no limit.
}

struct LfuAggregate {
  # Buckets instead of objects: the design's "histogram" and its per-user,
  # per-group, per-project and per-directory totals (Design: Filter Rule
  # Modules, Filesystem Usage Reports, Persistent Aggregates).  Each object
  # that passes the filter and the stages goes into the bucket for its
  # "field" value within its group, and the stream carries
  # LfuChunk.buckets.  A whole-tree total is an aggregate with
  # LfuOpen.subtree set.
  field @0 :LfuField;
  bounds @1 :List(UInt64);
  # Ascending.  Bucket i holds values below bounds[i] and, for i > 0, at
  # or above bounds[i - 1]; bucket bounds.length holds the rest.  Empty:
  # one bucket, which gives min, max, sum and count of the field.
  grouped @2 :Bool;
  groupBy @3 :LfuField;
  # If grouped, one set of buckets per value of this field: uid, gid,
  # projid or type.
  perDirectory @4 :Bool;
  # One set of buckets per parent directory, named in LfuBucket.dir*.
  # Combines with "grouped".  The design's directory usage summary
  # (Design: Persistent Aggregates), without the rollup: a bucket counts
  # the directory's own entries, and the consumer sums subtrees.
}

struct LfuApplied {
  # What the replying stage, or any stage upstream of it, has applied.
  # The consumer applies the rest itself or closes.  Parts that were not
  # requested count as applied.

  filter @0 :Bool;
  # LfuOpen.filter, every term of it.  If false see filterFields.
  subtree @1 :Bool;
  # LfuOpen.subtree.
  stages @2 :UInt64;
  # Bit i: LfuOpen.stages[i].
  order @3 :Bool;
  # LfuOpen.order.  If false the stream is in scan order and not cut.
  aggregate @4 :Bool;
  # Every one of LfuOpen.aggregates.  If false the stream carries objects,
  # not buckets.
  access @5 :Bool;
  # LfuOpen.accessOnly.  If false the stream may hold objects and
  # attributes the caller may not read.
  filterFields @6 :UInt64;
  # Bits numbered by LfuField: the fields whose terms every stage
  # evaluated.  Terms on other fields were dropped, so the consumer
  # re-checks them.  All bits of the fields used are set when "filter" is
  # true.
}

struct LfuOpen {
  features @0 :UInt64;
  # Bits numbered by LfuFeature: what the consumer can use.
  kind @1 :LfuKind;
  chunkSize @2 :UInt32;
  # Requested slot size in bytes: a power of two in [lfuChunkSizeMin,
  # lfuChunkSizeMax], or 0 for the producer's default.

  shardIndex @3 :UInt32;
  shardCount @4 :UInt32;
  # Scan shard shardIndex of shardCount.  The scanner decides the split;
  # shards are disjoint and cover the target.  shardCount 0 or 1 means the
  # whole target.  Only the scanner applies this; an open it cannot shard
  # as asked fails.

  subtree @5 :LfuFid;
  # If set, only objects below this directory.

  projection @6 :LfuProjection;
  # Attributes the consumer requires.  A producer that cannot supply one
  # fails the open, unless access withholds it (LfuOpenReply.projection).

  filter @7 :List(LfuClause);
  # An object is sent if any clause matches.  Empty matches everything.

  stages @8 :List(LfuStage);
  # Pluggable filters, at most 64.  An object is sent only if every one
  # accepts it.  Order is not significant.

  optional @9 :LfuProjection;
  # Attributes the consumer wants if the producer has them at hand
  # (Design: Input Scanner Modules).  LfuOpenReply.projection says which
  # were granted.

  order @10 :LfuOrder;
  # If set, see LfuOrder.

  aggregates @11 :List(LfuAggregate);
  # If not empty, see LfuAggregate.  Several are computed in one scan.

  accessOnly @12 :Bool;
  # Only objects the caller may access, by POSIX permissions and ACLs,
  # filesets and nodemaps, as the transport identifies the caller.  A
  # producer serving an unprivileged caller applies this whether asked or
  # not, and limits the projection to what the caller may read.

  source @13 :LfuSource;
  # Which scanner to use (Design: Client and Server Interoperation).  "any"
  # lets the producer choose.  A source the producer does not have fails
  # with -ENOENT; LfuOpenReply.sources of a probe says which it has.

  probe @14 :Bool;
  # Answer as for an open, but create no handle and scan nothing.

  changelogUser @15 :Text;
  # For LfuSource.changelog: the registered changelog user, such as "cl1",
  # whose records are read and whose records LfuRead.release clears: the
  # design's per-consumer configuration (Design: Changelog Input Scanner
  # Module).  A changelog producer fails an open without one with -EINVAL.

  target @16 :Text;
  # What to scan, when the producer serves more than one thing: a target
  # name such as "lustre-MDT0000"; a block device path for
  # LfuSource.device; a directory for namespace and posix; a filesystem
  # name for a stream the producer merges across targets.  Empty: the
  # producer's only, or default, target.  An unknown target fails with
  # -ENODEV.
}

struct LfuOpenReply {
  handle @0 :UInt64;
  # Names this stream in LfuRead, LfuLookup and LfuClose.  Valid on this
  # hop only.  Never 0 for a stream; 0 for a probe.
  features @1 :UInt64;
  # Bits numbered by LfuFeature: what is in effect, a subset of what was
  # asked.
  chunkSize @2 :UInt32;
  # Slot size in use.  May differ from the request.
  maxSlots @3 :UInt32;
  # Most slots one LfuRead or LfuLookup can return.
  applied @4 :LfuApplied;

  target @5 :Text;
  # Where the objects come from, in the words of LfuOpen.target.  Empty if
  # the producer does not know.

  projection @6 :LfuProjection;
  # What the records carry: the required projection less what access
  # withholds, plus what of the optional projection was granted.  Null
  # means exactly LfuOpen.projection.

  source @7 :LfuSource;
  # The scanner serving the stream, or "any" if not reported.  A merged
  # stream reports its first upstream.
  sources @8 :UInt64;
  # Bits numbered by LfuSource: the scanners this producer could serve
  # this kind from, for this target (Design: Client and Server
  # Interoperation).  0 if it does not know.
}

struct LfuRead {
  handle @0 :UInt64;
  cursor @1 :Data;
  # Where to start.  Empty means the beginning of the shard; otherwise a
  # cursor from an LfuReadReply or LfuChunk.  Opaque to the consumer.
  # Stays valid across handles and restarts for the same target, kind,
  # source and shard; a producer that can no longer resume from it answers
  # -ESTALE.
  maxSlots @2 :UInt32;
  # Size of the consumer's bulk buffer in slots.
  release @3 :Data;
  # If not empty, a cursor the consumer is done with: the producer may
  # discard what precedes it.  For a changelog this is the clear.  A
  # scanner ignores it.
  bulkEncoding @4 :LfuBulkEncoding;
  # The encoding the consumer would like.  The producer uses it or
  # "slots".
  waitMsec @5 :UInt32;
  # For a changelog: how long the producer may wait for new records
  # before answering with an empty bulk.  0 answers at once.  A scanner
  # ignores it.
}

struct LfuLookup {
  # The records of named objects, with the handle's projection, access
  # limits and stages applied, in the order asked.  Needs
  # LfuFeature.lookup.  Answered by an LfuReadReply whose bulk holds
  # LfuChunk.inodes chunks.  An object that does not exist, or that a stage
  # rejects, is in LfuChunk.skipped instead.
  handle @0 :UInt64;
  fids @1 :List(LfuFid);
  maxSlots @2 :UInt32;
  # Size of the consumer's bulk buffer in slots.  A lookup that needs more
  # is answered -EOVERFLOW with LfuReadReply.needSlots.
  bulkEncoding @3 :LfuBulkEncoding;
}

struct LfuReadReply {
  slots @0 :UInt32;
  # Valid slots in the bulk.
  needSlots @1 :UInt32;
  # With status -EOVERFLOW: the next chunk needs this many slots and
  # maxSlots was smaller.  Nothing was sent.
  eof @2 :Bool;
  # The shard is exhausted; this bulk is the last.  Always set for a
  # lookup.  Never set for a changelog: an empty bulk with eof clear means
  # no new records yet.
  cursor @3 :Data;
  # Pass to the next LfuRead.  Equals the cursor of the last chunk sent.
  # Empty for a lookup.
  summary @4 :LfuSummary;
  # Set when eof.  Equals the summary of the LfuChunk.end chunk when one
  # is sent.
  bulkEncoding @5 :LfuBulkEncoding;
  # The encoding of the bulk that goes with this reply.
  bulkBytes @6 :UInt32;
  # Bytes the bulk takes in that encoding.  0 with "slots" means
  # slots * chunkSize.
}

struct LfuClose {
  handle @0 :UInt64;
}

struct LfuCloseReply {
  summary @0 :LfuSummary;
}

struct LfuRequest {
  union {
    open @0 :LfuOpen;
    read @1 :LfuRead;
    close @2 :LfuClose;
    lookup @3 :LfuLookup;
  }
}

struct LfuReply {
  status @0 :Int32;
  # 0 or a negative Linux errno.  On error the union member may be null.

  union {
    open @1 :LfuOpenReply;
    read @2 :LfuReadReply;
    close @3 :LfuCloseReply;
    lookup @4 :LfuReadReply;
  }
}
