#ifndef TDB_PRIVATE_H
#define TDB_PRIVATE_H
 /* 
   Trivial Database 2: private types and prototypes
   Copyright (C) Rusty Russell 2010

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

#define _XOPEN_SOURCE 500
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <utime.h>
#include <unistd.h>
#include "config.h"
#include <ccan/tdb2/tdb2.h>
#include <ccan/likely/likely.h>
#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#endif

#ifndef TEST_IT
#define TEST_IT(cond)
#endif

/* #define TDB_TRACE 1 */

#ifndef __STRING
#define __STRING(x)    #x
#endif

#ifndef __STRINGSTRING
#define __STRINGSTRING(x) __STRING(x)
#endif

#ifndef __location__
#define __location__ __FILE__ ":" __STRINGSTRING(__LINE__)
#endif

typedef uint64_t tdb_len_t;
typedef uint64_t tdb_off_t;

#define TDB_MAGIC_FOOD "TDB file\n"
#define TDB_VERSION ((uint64_t)(0x26011967 + 7))
#define TDB_MAGIC ((uint64_t)0x1999)
#define TDB_FREE_MAGIC ((~(uint64_t)TDB_MAGIC) << 6)
#define TDB_COALESCING_MAGIC (0xBAD1DEA2FEEDULL << 6)
#define TDB_HASH_MAGIC (0xA1ABE11A01092008ULL)
#define TDB_RECOVERY_MAGIC (0xf53bc0e7U)
#define TDB_RECOVERY_INVALID_MAGIC (0x0)

#define TDB_OFF_ERR ((tdb_off_t)-1)

/* Prevent others from opening the file. */
#define TDB_OPEN_LOCK 0
/* Doing a transaction. */
#define TDB_TRANSACTION_LOCK 1
/* Expanding file. */
#define TDB_EXPANSION_LOCK 2
/* Hash chain locks. */
#define TDB_HASH_LOCK_START 3

/* Range for hash locks. */
#define TDB_HASH_LOCK_RANGE_BITS 30
#define TDB_HASH_LOCK_RANGE (1 << TDB_HASH_LOCK_RANGE_BITS)

/* We have 1024 entries in the top level. */
#define TDB_TOPLEVEL_HASH_BITS 10
/* And 64 entries in each sub-level: thus 64 bits exactly after 9 levels. */
#define TDB_SUBLEVEL_HASH_BITS 6
/* And 8 entries in each group, ie 8 groups per sublevel. */
#define TDB_HASH_GROUP_BITS 3

/* We start with a 64k-sized zone. */
#define INITIAL_ZONE_BITS 16
/* Try to create zones at least 32 times larger than allocations. */
#define TDB_COMFORT_FACTOR_BITS 5

/* We steal bits from the offsets to store hash info. */
#define TDB_OFF_HASH_GROUP_MASK ((1ULL << TDB_HASH_GROUP_BITS) - 1)
/* We steal this many upper bits, giving a maximum offset of 64 exabytes. */
#define TDB_OFF_UPPER_STEAL 8
#define   TDB_OFF_UPPER_STEAL_EXTRA 7
#define   TDB_OFF_UPPER_STEAL_TRUNCBIT 1
/* If this is set, hash is truncated (only 1 bit is valid). */
#define TDB_OFF_HASH_TRUNCATED_BIT 56
/* The bit number where we store next level of hash. */
#define TDB_OFF_HASH_EXTRA_BIT 57
/* Convenience mask to get actual offset. */
#define TDB_OFF_MASK \
	(((1ULL << (64 - TDB_OFF_UPPER_STEAL)) - 1) - TDB_OFF_HASH_GROUP_MASK)

/* We have to be able to fit a free record here. */
#define TDB_MIN_DATA_LEN	\
	(sizeof(struct tdb_free_record) - sizeof(struct tdb_used_record))

/* We ensure buckets up to size 1 << (zone_bits - TDB_COMFORT_FACTOR_BITS). */
/* FIXME: test this matches size_to_bucket! */
#define BUCKETS_FOR_ZONE(zone_bits) ((zone_bits) + 2 - TDB_COMFORT_FACTOR_BITS)

#if !HAVE_BSWAP_64
static inline uint64_t bswap_64(uint64_t x)
{
	return (((x&0x000000FFULL)<<56)
		| ((x&0x0000FF00ULL)<<48)
		| ((x&0x00FF0000ULL)<<40)
		| ((x&0xFF000000ULL)<<32)
		| ((x>>8)&0xFF000000ULL)
		| ((x>>16)&0x00FF0000ULL)
		| ((x>>24)&0x0000FF00ULL)
		| ((x>>32)&0x000000FFULL));
}
#endif

struct tdb_used_record {
	/* For on-disk compatibility, we avoid bitfields:
	   magic: 16,        (highest)
	   key_len_bits: 5,
	   extra_padding: 32
	   hash_bits: 5,
           zone_bits: 6 (lowest)
	*/
        uint64_t magic_and_meta;
	/* The bottom key_len_bits*2 are key length, rest is data length. */
        uint64_t key_and_data_len;
};

static inline unsigned rec_key_bits(const struct tdb_used_record *r)
{
	return ((r->magic_and_meta >> 43) & ((1 << 5)-1)) * 2;
}

static inline uint64_t rec_key_length(const struct tdb_used_record *r)
{
	return r->key_and_data_len & ((1ULL << rec_key_bits(r)) - 1);
}

static inline uint64_t rec_data_length(const struct tdb_used_record *r)
{
	return r->key_and_data_len >> rec_key_bits(r);
}

static inline uint64_t rec_extra_padding(const struct tdb_used_record *r)
{
	return (r->magic_and_meta >> 11) & 0xFFFFFFFF;
}

static inline unsigned int rec_zone_bits(const struct tdb_used_record *r)
{
	return r->magic_and_meta & ((1 << 6) - 1);
}

static inline uint32_t rec_hash(const struct tdb_used_record *r)
{
	return (r->magic_and_meta >> 6) & ((1 << 5) - 1);
}

static inline uint16_t rec_magic(const struct tdb_used_record *r)
{
	return (r->magic_and_meta >> 48);
}

struct tdb_free_record {
        uint64_t magic_and_meta; /* Bottom 6 bits are zone bits. */
        uint64_t data_len; /* Not counting these two fields. */
	/* This is why the minimum record size is 16 bytes.  */
	uint64_t next, prev;
};

static inline unsigned int frec_zone_bits(const struct tdb_free_record *f)
{
	return f->magic_and_meta & ((1 << 6) - 1);
}

static inline uint64_t frec_magic(const struct tdb_free_record *f)
{
	return f->magic_and_meta & ~((1ULL << 6) - 1);
}

/* Each zone has its set of free lists at the head.
 *
 * For each zone we have a series of per-size buckets, and a final bucket for
 * "too big". */
struct free_zone_header {
	/* How much does this zone cover? */
	uint64_t zone_bits;
	/* tdb_off_t buckets[free_buckets + 1] */
};

/* this is stored at the front of every database */
struct tdb_header {
	char magic_food[64]; /* for /etc/magic */
	/* FIXME: Make me 32 bit? */
	uint64_t version; /* version of the code */
	uint64_t hash_test; /* result of hashing HASH_MAGIC. */
	uint64_t hash_seed; /* "random" seed written at creation time. */

	tdb_off_t reserved[28];

	/* Top level hash table. */
	tdb_off_t hashtable[1ULL << TDB_TOPLEVEL_HASH_BITS];
};

/* Information about a particular (locked) hash entry. */
struct hash_info {
	/* Full hash value of entry. */
	uint64_t h;
	/* Start and length of lock acquired. */
	tdb_off_t hlock_start;
	tdb_len_t hlock_range;
	/* Start of hash group. */
	tdb_off_t group_start;
	/* Bucket we belong in. */
	unsigned int home_bucket;
	/* Bucket we (or an empty space) were found in. */
	unsigned int found_bucket;
	/* How many bits of the hash are already used. */
	unsigned int hash_used;
	/* Current working group. */
	tdb_off_t group[1 << TDB_HASH_GROUP_BITS];
};

struct traverse_info {
	struct traverse_level {
		tdb_off_t hashtable;
		/* We ignore groups here, and treat it as a big array. */
		unsigned entry;
		unsigned int total_buckets;
	} levels[64 / TDB_SUBLEVEL_HASH_BITS];
	unsigned int num_levels;
	unsigned int toplevel_group;
	/* This makes delete-everything-inside-traverse work as expected. */
	tdb_off_t prev;
};

enum tdb_lock_flags {
	/* WAIT == F_SETLKW, NOWAIT == F_SETLK */
	TDB_LOCK_NOWAIT = 0,
	TDB_LOCK_WAIT = 1,
	/* If set, don't log an error on failure. */
	TDB_LOCK_PROBE = 2,
};

struct tdb_lock_type {
	uint32_t off;
	uint32_t count;
	uint32_t ltype;
};

struct tdb_context {
	/* Filename of the database. */
	const char *name;

	/* Mmap (if any), or malloc (for TDB_INTERNAL). */
	void *map_ptr;

	/* Are we accessing directly? (debugging check). */
	int direct_access;

	 /* Open file descriptor (undefined for TDB_INTERNAL). */
	int fd;

	/* How much space has been mapped (<= current file size) */
	tdb_len_t map_size;

	/* Operating read-only? (Opened O_RDONLY, or in traverse_read) */
	bool read_only;

	/* mmap read only? */
	int mmap_flags;

	/* Error code for last tdb error. */
	enum TDB_ERROR ecode; 

	/* the flags passed to tdb_open, for tdb_reopen. */
	uint32_t flags;

	/* Logging function */
	tdb_logfn_t log;
	void *log_priv;

	/* Hash function. */
	tdb_hashfn_t khash;
	void *hash_priv;
	uint64_t hash_seed;

	/* Set if we are in a transaction. */
	struct tdb_transaction *transaction;
	
	/* What zone of the tdb to use, for spreading load. */
	uint64_t zone_off;
	/* Cached copy of zone header. */
	struct free_zone_header zhdr;

	/* IO methods: changes for transactions. */
	const struct tdb_methods *methods;

	/* Lock information */
	struct tdb_lock_type allrecord_lock;
	uint64_t num_lockrecs;
	struct tdb_lock_type *lockrecs;

	/* Single list of all TDBs, to avoid multiple opens. */
	struct tdb_context *next;
	dev_t device;	
	ino_t inode;
};

struct tdb_methods {
	int (*read)(struct tdb_context *, tdb_off_t, void *, tdb_len_t);
	int (*write)(struct tdb_context *, tdb_off_t, const void *, tdb_len_t);
	int (*oob)(struct tdb_context *, tdb_off_t, bool);
	int (*expand_file)(struct tdb_context *, tdb_len_t);
};

/*
  internal prototypes
*/
/* hash.c: */
void tdb_hash_init(struct tdb_context *tdb);

/* Hash random memory. */
uint64_t tdb_hash(struct tdb_context *tdb, const void *ptr, size_t len);

/* Hash on disk. */
uint64_t hash_record(struct tdb_context *tdb, tdb_off_t off);

/* Find and lock a hash entry (or where it would be). */
tdb_off_t find_and_lock(struct tdb_context *tdb,
			struct tdb_data key,
			int ltype,
			struct hash_info *h,
			struct tdb_used_record *rec,
			struct traverse_info *tinfo);

int replace_in_hash(struct tdb_context *tdb,
		    struct hash_info *h,
		    tdb_off_t new_off);

int add_to_hash(struct tdb_context *tdb, struct hash_info *h,
		tdb_off_t new_off);

int delete_from_hash(struct tdb_context *tdb, struct hash_info *h);

/* For tdb_check */
bool is_subhash(tdb_off_t val);

/* free.c: */
int tdb_zone_init(struct tdb_context *tdb);

/* If this fails, try tdb_expand. */
tdb_off_t alloc(struct tdb_context *tdb, size_t keylen, size_t datalen,
		uint64_t hash, bool growing);

/* Put this record in a free list. */
int add_free_record(struct tdb_context *tdb, unsigned int zone_bits,
		    tdb_off_t off, tdb_len_t len_with_header);

/* Set up header for a used record. */
int set_header(struct tdb_context *tdb,
	       struct tdb_used_record *rec,
	       uint64_t keylen, uint64_t datalen,
	       uint64_t actuallen, unsigned hashlow,
	       unsigned int zone_bits);

/* Used by tdb_check to verify. */
unsigned int size_to_bucket(unsigned int free_buckets, tdb_len_t data_len);
tdb_off_t bucket_off(tdb_off_t zone_off, tdb_off_t bucket);

/* io.c: */
/* Initialize tdb->methods. */
void tdb_io_init(struct tdb_context *tdb);

/* Convert endian of the buffer if required. */
void *tdb_convert(const struct tdb_context *tdb, void *buf, tdb_len_t size);

/* Unmap and try to map the tdb. */
void tdb_munmap(struct tdb_context *tdb);
void tdb_mmap(struct tdb_context *tdb);

/* Either make a copy into pad and return that, or return ptr into mmap.
 * Converts endian (ie. will use pad in that case). */
void *tdb_get(struct tdb_context *tdb, tdb_off_t off, void *pad, size_t len);

/* Either alloc a copy, or give direct access.  Release frees or noop. */
const void *tdb_access_read(struct tdb_context *tdb,
			    tdb_off_t off, tdb_len_t len, bool convert);
void *tdb_access_write(struct tdb_context *tdb,
		       tdb_off_t off, tdb_len_t len, bool convert);

/* Release result of tdb_access_read/write. */
void tdb_access_release(struct tdb_context *tdb, const void *p);
/* Commit result of tdb_acces_write. */
int tdb_access_commit(struct tdb_context *tdb, void *p);

/* Convenience routine to get an offset. */
tdb_off_t tdb_read_off(struct tdb_context *tdb, tdb_off_t off);

/* Write an offset at an offset. */
int tdb_write_off(struct tdb_context *tdb, tdb_off_t off, tdb_off_t val);

/* Clear an ondisk area. */
int zero_out(struct tdb_context *tdb, tdb_off_t off, tdb_len_t len);

/* Return a non-zero offset between >= start < end in this array (or end). */
tdb_off_t tdb_find_nonzero_off(struct tdb_context *tdb,
			       tdb_off_t base,
			       uint64_t start,
			       uint64_t end);

/* Return a zero offset in this array, or num. */
tdb_off_t tdb_find_zero_off(struct tdb_context *tdb, tdb_off_t off,
			    uint64_t num);

/* Even on files, we can get partial writes due to signals. */
bool tdb_pwrite_all(int fd, const void *buf, size_t len, tdb_off_t off);
bool tdb_pread_all(int fd, void *buf, size_t len, tdb_off_t off);
bool tdb_read_all(int fd, void *buf, size_t len);

/* Allocate and make a copy of some offset. */
void *tdb_alloc_read(struct tdb_context *tdb, tdb_off_t offset, tdb_len_t len);

/* Writes a converted copy of a record. */
int tdb_write_convert(struct tdb_context *tdb, tdb_off_t off,
		      const void *rec, size_t len);

/* Reads record and converts it */
int tdb_read_convert(struct tdb_context *tdb, tdb_off_t off,
		     void *rec, size_t len);


/* lock.c: */
void tdb_lock_init(struct tdb_context *tdb);

/* Lock/unlock a range of hashes. */
int tdb_lock_hashes(struct tdb_context *tdb,
		    tdb_off_t hash_lock, tdb_len_t hash_range,
		    int ltype, enum tdb_lock_flags waitflag);
int tdb_unlock_hashes(struct tdb_context *tdb,
		      tdb_off_t hash_lock,
		      tdb_len_t hash_range, int ltype);

/* Lock/unlock a particular free bucket. */
int tdb_lock_free_bucket(struct tdb_context *tdb, tdb_off_t b_off,
			 enum tdb_lock_flags waitflag);
void tdb_unlock_free_bucket(struct tdb_context *tdb, tdb_off_t b_off);

/* Do we have any locks? */
bool tdb_has_locks(struct tdb_context *tdb);

/* Lock entire database. */
int tdb_allrecord_lock(struct tdb_context *tdb, int ltype,
		       enum tdb_lock_flags flags, bool upgradable);
int tdb_allrecord_unlock(struct tdb_context *tdb, int ltype);

/* Serialize db open. */
int tdb_lock_open(struct tdb_context *tdb);
void tdb_unlock_open(struct tdb_context *tdb);

/* Serialize db expand. */
int tdb_lock_expand(struct tdb_context *tdb, int ltype);
void tdb_unlock_expand(struct tdb_context *tdb, int ltype);
bool tdb_has_expansion_lock(struct tdb_context *tdb);


/* traverse.c: */
int first_in_hash(struct tdb_context *tdb, int ltype,
		  struct traverse_info *tinfo,
		  TDB_DATA *kbuf, size_t *dlen);
int next_in_hash(struct tdb_context *tdb, int ltype,
		 struct traverse_info *tinfo,
		 TDB_DATA *kbuf, size_t *dlen);


#if 0
/* Low-level locking primitives. */
int tdb_nest_lock(struct tdb_context *tdb, tdb_off_t offset, int ltype,
		  enum tdb_lock_flags flags);
int tdb_nest_unlock(struct tdb_context *tdb, tdb_off_t offset, int ltype);

int tdb_munmap(struct tdb_context *tdb);
void tdb_mmap(struct tdb_context *tdb);
int tdb_lock(struct tdb_context *tdb, int list, int ltype);
int tdb_lock_nonblock(struct tdb_context *tdb, int list, int ltype);
bool tdb_have_locks(struct tdb_context *tdb);
int tdb_unlock(struct tdb_context *tdb, int list, int ltype);
int tdb_brlock(struct tdb_context *tdb,
	       int rw_type, tdb_off_t offset, size_t len,
	       enum tdb_lock_flags flags);
int tdb_brunlock(struct tdb_context *tdb,
		 int rw_type, tdb_off_t offset, size_t len);
bool tdb_have_extra_locks(struct tdb_context *tdb);
void tdb_release_extra_locks(struct tdb_context *tdb);
int tdb_transaction_lock(struct tdb_context *tdb, int ltype);
int tdb_transaction_unlock(struct tdb_context *tdb, int ltype);
int tdb_allrecord_lock(struct tdb_context *tdb, int ltype,
		       enum tdb_lock_flags flags, bool upgradable);
int tdb_allrecord_unlock(struct tdb_context *tdb, int ltype);
int tdb_allrecord_upgrade(struct tdb_context *tdb);
int tdb_write_lock_record(struct tdb_context *tdb, tdb_off_t off);
int tdb_write_unlock_record(struct tdb_context *tdb, tdb_off_t off);
int tdb_ofs_read(struct tdb_context *tdb, tdb_off_t offset, tdb_off_t *d);
int tdb_ofs_write(struct tdb_context *tdb, tdb_off_t offset, tdb_off_t *d);
int tdb_free(struct tdb_context *tdb, tdb_off_t offset, struct tdb_record *rec);
tdb_off_t tdb_allocate(struct tdb_context *tdb, tdb_len_t length, struct tdb_record *rec);
int tdb_ofs_read(struct tdb_context *tdb, tdb_off_t offset, tdb_off_t *d);
int tdb_ofs_write(struct tdb_context *tdb, tdb_off_t offset, tdb_off_t *d);
int tdb_lock_record(struct tdb_context *tdb, tdb_off_t off);
int tdb_unlock_record(struct tdb_context *tdb, tdb_off_t off);
bool tdb_needs_recovery(struct tdb_context *tdb);
int tdb_rec_read(struct tdb_context *tdb, tdb_off_t offset, struct tdb_record *rec);
int tdb_rec_write(struct tdb_context *tdb, tdb_off_t offset, struct tdb_record *rec);
int tdb_do_delete(struct tdb_context *tdb, tdb_off_t rec_ptr, struct tdb_record *rec);
unsigned char *tdb_alloc_read(struct tdb_context *tdb, tdb_off_t offset, tdb_len_t len);
int tdb_parse_data(struct tdb_context *tdb, TDB_DATA key,
		   tdb_off_t offset, tdb_len_t len,
		   int (*parser)(TDB_DATA key, TDB_DATA data,
				 void *private_data),
		   void *private_data);
tdb_off_t tdb_find_lock_hash(struct tdb_context *tdb, TDB_DATA key, uint32_t hash, int locktype,
			   struct tdb_record *rec);
void tdb_io_init(struct tdb_context *tdb);
int tdb_expand(struct tdb_context *tdb, tdb_off_t size);
int tdb_rec_free_read(struct tdb_context *tdb, tdb_off_t off,
		      struct tdb_record *rec);
#endif

#ifdef TDB_TRACE
void tdb_trace(struct tdb_context *tdb, const char *op);
void tdb_trace_seqnum(struct tdb_context *tdb, uint32_t seqnum, const char *op);
void tdb_trace_open(struct tdb_context *tdb, const char *op,
		    unsigned hash_size, unsigned tdb_flags, unsigned open_flags);
void tdb_trace_ret(struct tdb_context *tdb, const char *op, int ret);
void tdb_trace_retrec(struct tdb_context *tdb, const char *op, TDB_DATA ret);
void tdb_trace_1rec(struct tdb_context *tdb, const char *op,
		    TDB_DATA rec);
void tdb_trace_1rec_ret(struct tdb_context *tdb, const char *op,
			TDB_DATA rec, int ret);
void tdb_trace_1rec_retrec(struct tdb_context *tdb, const char *op,
			   TDB_DATA rec, TDB_DATA ret);
void tdb_trace_2rec_flag_ret(struct tdb_context *tdb, const char *op,
			     TDB_DATA rec1, TDB_DATA rec2, unsigned flag,
			     int ret);
void tdb_trace_2rec_retrec(struct tdb_context *tdb, const char *op,
			   TDB_DATA rec1, TDB_DATA rec2, TDB_DATA ret);
#else
#define tdb_trace(tdb, op)
#define tdb_trace_seqnum(tdb, seqnum, op)
#define tdb_trace_open(tdb, op, hash_size, tdb_flags, open_flags)
#define tdb_trace_ret(tdb, op, ret)
#define tdb_trace_retrec(tdb, op, ret)
#define tdb_trace_1rec(tdb, op, rec)
#define tdb_trace_1rec_ret(tdb, op, rec, ret)
#define tdb_trace_1rec_retrec(tdb, op, rec, ret)
#define tdb_trace_2rec_flag_ret(tdb, op, rec1, rec2, flag, ret)
#define tdb_trace_2rec_retrec(tdb, op, rec1, rec2, ret)
#endif /* !TDB_TRACE */

#endif
