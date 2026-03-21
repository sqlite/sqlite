/**
  This C file contains both the header and source file for c-pp,
  a.k.a. libcmpp.
*/
#if !defined(NET_WANDERINGHORSE_LIBCMPP_C_INCLUDED)
#define NET_WANDERINGHORSE_LIBCMPP_C_INCLUDED
#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L /* for fdopen() in stdio.h */
#endif
#define CMPP_AMALGAMATION
#if !defined(NET_WANDERINGHORSE_LIBCMPP_H_INCLUDED)
/**
  This is the auto-generated "amalgamation build" of libcmpp. It was amalgamated
  using:

  ./c-pp -I. -I./src -Dsrcdir=./src -Dsed=/usr/bin/sed -o libcmpp.h ./tool/libcmpp.c-pp.h -o libcmpp.c ./tool/libcmpp.c-pp.c

  with libcmpp 2.0.x c02f3e3e2d3f3573a9a33c1474c2e52fc48e52c70730404a90d0ae51517e7d37 @ 2026-03-08 14:50:35.123 UTC
*/
#define CMPP_PACKAGE_NAME "libcmpp"
#define CMPP_LIB_VERSION "2.0.x"
#define CMPP_LIB_VERSION_HASH "c02f3e3e2d3f3573a9a33c1474c2e52fc48e52c70730404a90d0ae51517e7d37"
#define CMPP_LIB_VERSION_TIMESTAMP "2026-03-08 14:50:35.123 UTC"
#define CMPP_LIB_CONFIG_TIMESTAMP "2026-03-08 15:32 GMT"
#define CMPP_VERSION CMPP_LIB_VERSION " " CMPP_LIB_VERSION_HASH " @ " CMPP_LIB_VERSION_TIMESTAMP
#define CMPP_PLATFORM_EXT_DLL ".so"
#define CMPP_MODULE_PATH ".:/usr/local/lib/cmpp"

#if !defined(NET_WANDERINGHORSE_CMPP_H_INCLUDED_)
#define NET_WANDERINGHORSE_CMPP_H_INCLUDED_
/*
** 2022-11-12:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
**
** The C-minus Preprocessor: C-like preprocessor.  Why? Because C
** preprocessors _can_ process non-C code but generally make quite a
** mess of it. The purpose of this library is a customizable
** preprocessor suitable for use with arbitrary UTF-8-encoded text.
**
** The supported preprocessor directives are documented in the
** README.md hosted with this file (or see the link below).
**
** Any mention of "#" in the docs, e.g. "#if", is symbolic. The
** directive delimiter is configurable and defaults to "##". Define
** CMPP_DEFAULT_DELIM to a string when compiling to define the default
** at build-time.
**
** This API is presented as a library but was evolved from a
** monolithic app. Thus is library interface is likely still missing
** some pieces needed to make it more readily usable as a library.
**
** Author(s):
**
** - Stephan Beal <https://wanderinghorse.net/home/stephan/>
**
** Canonical homes:
**
** - https://fossil.wanderinghorse.net/r/c-pp
** - https://sqlite.org/src/file/ext/wasm/c-pp-lite.c
**
** With the former hosting this app's SCM and the latter being the
** original deployment of c-pp.c, from which this library
** evolved. SQLite uses a "lite" version of c-pp, whereas _this_ copy
** is its much-heavier-weight fork.
*/

#if defined(CMPP_HAVE_AUTOCONFIG_H)
#include "libcmpp-autoconfig.h"
#endif
#if defined(HAVE_AUTOCONFIG_H)
#include "autoconfig.h"
#endif
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#ifdef _WIN32
# if defined(BUILD_libcmpp_static) || defined(CMPP_AMALGAMATION_BUILD)
#  define CMPP_EXPORT extern
# elif defined(BUILD_libcmpp)
#  define CMPP_EXPORT extern __declspec(dllexport)
# else
#  define CMPP_EXPORT extern __declspec(dllimport)
# endif
#else
# define CMPP_EXPORT extern
#endif

/**
   cmpp_FILE is a portability hack for WASM builds, where we want to
   elide the (FILE*)-using pieces to avoid having a dependency on
   Emscripten's POSIX I/O proxies. In all non-WASM builds it is
   guaranteed to be an alias for FILE. On WASM builds it is guaranteed
   to be an alias for void and the cmpp APIs which use it become
   inoperative in WASM builds.

   That said: the code does not yet support completely compiling out
   (FILE*) dependencies, and may not be able to because canonical
   sqlite3 (upon which it is based) depends heavily on file
   descriptors and slightly on FILE handles.
*/
#if defined(__EMSCRIPTEN__) || defined(__wasm__) || defined(__wasi__)
  typedef void cmpp_FILE;
#  define CMPP_PLATFORM_IS_WASM 1
#else
  #include <stdio.h>
  typedef FILE cmpp_FILE;
#  define CMPP_PLATFORM_IS_WASM 0
#endif

#include <stdint.h>
#include <inttypes.h> /* PRIu32 and friends */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   32-bit flag bitmask type. This typedef exists primarily to improve
   legibility of function signatures and member structs by conveying
   their intent for use as flags instead of result codes or lengths.
*/
typedef uint32_t cmpp_flag32_t;
//typedef uint16_t cmpp_flag16_t;

/**
   An X-macro which invokes its argument (a macro name) to expand to
   all possible values of cmpp_rc_e entries. The macro name passed to
   it is invoked once for each entry and passed 3 arguments: the enum
   entry's full name (CMPP_RC_...), its integer value, and a help-text
   string.
*/
#define cmpp_rc_e_map(E) \
  E(CMPP_RC_OK, 0,                                                      \
    "The quintessential not-an-error value.")                           \
  E(CMPP_RC_ERROR, 100,                                                 \
    "Generic/unknown error.")                                           \
  E(CMPP_RC_NYI, 101,                                                   \
    "A placeholder return value for not yet implemented functions.")    \
  E(CMPP_RC_OOM, 102,                                                   \
    "Out of memory. Indicates that a resource allocation "              \
    "request failed.")                                                  \
  E(CMPP_RC_MISUSE, 103,                                                \
    "API misuse (invalid args)")                                        \
  E(CMPP_RC_RANGE, 104,                                                 \
    "A range was violated.")                                            \
  E(CMPP_RC_ACCESS, 105,                                                \
    "Access to or locking of a resource was denied "                    \
    "by some security mechanism or other.")                             \
  E(CMPP_RC_IO, 106,                                                    \
    "Indicates an I/O error. Whether it was reading or "                \
    "writing is context-dependent.")                                    \
  E(CMPP_RC_NOT_FOUND, 107,                                             \
    "Requested resource not found.")                                    \
  E(CMPP_RC_ALREADY_EXISTS, 108,                                        \
    "Indicates that a to-be-created resource already exists.")          \
  E(CMPP_RC_CORRUPT, 109,                                               \
    "Data consistency problem.")                                        \
  E(CMPP_RC_SYNTAX, 110,                                                \
    "Some sort of syntax error.")                                       \
  E(CMPP_RC_NOOP, 111,                                                  \
    "Special sentinel value for some APIs.")                            \
  E(CMPP_RC_UNSUPPORTED, 112,                                           \
    "An unsupported operation was request.")                            \
  E(CMPP_RC_DB, 113,                                                    \
    "Indicates db-level error (e.g. statement prep failed). In such "   \
    "cases, the error state of the related db handle (cmpp_db) "        \
    "will be updated to contain more information directly from the "    \
    "db driver.")                                                       \
  E(CMPP_RC_NOT_DEFINED, 114,                                           \
    "Failed to expand an undefined value.")                             \
  E(CMPP_RC_ASSERT, 116, "An #assert failed.")                          \
  E(CMPP_RC_TYPE, 118,                                                  \
    "Indicates that some data type or logical type is incorrect.")      \
  E(CMPP_RC_CANNOT_HAPPEN, 140,                                         \
    "This is intended only for internal use, to "                       \
    "report conditions which \"cannot possibly happen\".")              \
  E(CMPP_RC_HELP, 141,                                                  \
    "--help was used in the arguments to cmpp_process_argv()")          \
  E(CMPP_RC_NO_DIRECTIVE, 142,                                          \
    "A special case of CMPP_RC_NOT_FOUND needed to disambiguate.")      \
  E(CMPP_RC_end,200,                                                    \
    "Must be the final entry in the enum. Used for creating client-side " \
    "result codes which are guaranteed to live outside of this one's "  \
    "range.")

/**
   Most functions in this library which return an int return result
   codes from the cmpp_rc_e enum.  None of these entries are
   guaranteed to have a specific value across library versions except
   for CMPP_RC_OK, which is guaranteed to always be 0 (and the API
   guarantees that no other code shall have a value of zero).

   The only reasons numbers are hard-coded to the values is to
   simplify debugging during development. Clients may use
   cmpp_rc_cstr() to get some human-readable (or programmer-readable)
   form for any given value in this enum.
*/
enum cmpp_rc_e {
#define E(N,V,H) N = V,
  cmpp_rc_e_map(E)
#undef E
};
typedef enum cmpp_rc_e cmpp_rc_e;

/**
   Returns the string form of the given cmpp_rc_e value or NULL if
   it's not a member of that enum.
*/
char const * cmpp_rc_cstr(int rc);

/**
   CMPP_BITNESS specifies whether the library should use 32- or 64-bit
   integer types for its size/length measurements. It's difficult to
   envision use cases for a preprocessor which would require counters
   or rangers larger than 32 bits provide for, so the default is 32
   bits. Builds created with different CMPP_BITNESS values are
   not binary-compatible.
*/
#define CMPP_BITNESS 32
#if 32==CMPP_BITNESS
/**
   Unsigned integer type for string/stream lengths. 32 bits is
   sufficient for all but the weirdest of inputs and outputs.
*/
typedef uint32_t cmpp_size_t;

/**
  A signed integer type indicating the maximum length of strings or
  byte ranges in a stream. It is most frequently used in API
  signatures where a negative value means "if it's negative then use
  strlen() to count it".
*/
typedef int32_t cmpp_ssize_t;

/**
   The printf-format-compatible format letter (or group of letters)
   appropriate for use with cmpp_size_t. Contrary to popular usage,
   size_t cannot be portably used with printf(), without careful
   casting, because it has neither a fixed size nor a standardized
   printf/scanf format specifier (like the stdint.h types do).
*/
#define CMPP_SIZE_T_PFMT PRIu32
#elif 64==CMPP_BITNESS
typedef uint64_t cmpp_size_t;
typedef int64_t cmpp_ssize_t;
#define CMPP_SIZE_T_PFMT PRIu64
#else
#error "Invalid CMPP_BITNESS value. Expecting 32 or 64."
#endif

/**
   Generic interface for streaming in data. Implementations must read
   (at most) *n bytes from their input, copy it to dest, assign *n to
   the number of bytes actually read, return 0 on success, and return
   non-0 cmpp_rc_e value on error (e.g. CMPP_RC_IO).

   When called, *n is the max length to read. On return, *n must be
   set to the amount actually read. Implementations may need to
   internally distinguish a short read due to EOF from a short read
   due to an I/O error, e.g. using feof() and/or ferror(). A short
   read for EOF is not an error but a short read for input failure is.
   This library invariably treats a short read as EOF.

   The state parameter is the implementation-specified input
   file/buffer/whatever channel.
*/
typedef int (*cmpp_input_f)(void * state, void * dest, cmpp_size_t * n);

/**
   Generic interface for streaming out data. Implementations must
   write n bytes from src to their destination channel and return 0 on
   success, or a value from the cmpp_rc_e enum on error
   (e.g. CMPP_RC_IO). The state parameter is the
   implementation-specified output channel.

   It is implementation-defined whether an n of 0 is legal. This
   library refrains from passing 0 to these functions.

   In the context of cmpp, the library makes no guarantees that output
   will always end at a character boundary. It may send any given
   multibyte character as the end resp. start of two calls to this
   function.  If that is of a concern for implementors of these
   functions (e.g. because they're appending the output to a UI
   widget), they may need to buffer all of the output before applying
   it (see cmpp_b), or otherwise account for partial characters.

   That said: the core library, by an accident of design, will always
   emit data at character boundaries, assuming that its input is
   well-formed UTF-8 text (which cmpp does not validate to be the
   case).  Custom cmpp_dx_f() implementations are not strictly
   required to do so but, because of how cmpp is used, almost
   certainly will. But relying on that is ill-advised.
*/
typedef int (*cmpp_output_f)(void * state, void const * src,
                             cmpp_size_t n);

/**
   Generic interface for flushing arbitrary output streams.  Must
   return 0 on success, a non-0 cmpp_rc_e value on error. When in
   doubt, return CMPP_RC_IO on error. The interpretation of the state
   parameter is implementation-specific.
*/
typedef int (*cmpp_flush_f)(void * state);

typedef struct cmpp_pimpl cmpp_pimpl;
typedef struct cmpp_api_thunk cmpp_api_thunk;
typedef struct cmpp_outputer cmpp_outputer;

/**
   The library's primary class. Each one of these represents a
   separate preprocessor instance.

   See also: cmpp_dx (the class which client-side extensions interact
   with the most).
*/
struct cmpp {

  /**
     API thunk object to support use via loadable modules. Client code
     does not normally need to access this member, but it's exposed
     here to give loadable modules more flexibility in how they use
     the thunk.

     This pointer is _always_ the same singleton object. The library
     never exposes a cmpp object with a NULL api member.
  */
  cmpp_api_thunk const * const api;

  /**
     Private internal state.
  */
  cmpp_pimpl * const pimpl;
};
typedef struct cmpp cmpp;

/**
   Flags for use with cmpp_ctor_cfg::flags.
*/
enum cmpp_ctor_e {
  /* Sentinel value. */
  cmpp_ctor_F_none       = 0,
  /* Disables #include. */
  cmpp_ctor_F_NO_INCLUDE = 0x01,
  /* Disables #pipe. */
  cmpp_ctor_F_NO_PIPE    = 0x02,
  /* Disables #attach, #detach, and #query. */
  cmpp_ctor_F_NO_DB      = 0x04,
  /* Disables #module. */
  cmpp_ctor_F_NO_MODULE  = 0x08,
  /**
    Disable all built-in directives which may work with the filesystem
    or invoke external processes. Client-defined directives with the
    cmpp_d_F_NOT_IN_SAFEMODE flag are also disabled. Directives
    disabled via the cmpp_ctor_F_NO_... flags (or equivalent library
    built-time options) do not get registered, so will trigger
    "unknown directive" errors rather than safe-mode violation errors.
  */
  cmpp_ctor_F_SAFEMODE   = 0x10,
};

/**
   A configuration object for cmpp_ctor(). This type may be extended
   as new construction-time customization opportunities are
   discovered.
*/
struct cmpp_ctor_cfg {
  /**
     Bitmask from the cmpp_ctor_e enum.
  */
  cmpp_flag32_t flags;
  /**
     If not NULL then this must name either an existing SQLite3 db
     file or the name of one which can be created on demand. If NULL
     then an in-memory or temporary database is used (which one is
     unspecified). The library copies these bytes, so they need not be
     valid after a call to cmpp_ctor().
  */
  char const * dbFile;
};
typedef struct cmpp_ctor_cfg cmpp_ctor_cfg;

/**
   Assigns *pp to a new cmpp or NULL on OOM. Any non-NULL return
   value must eventually be passed to cmpp_dtor() to free it.

   The cfg argument, if not NULL, holds config info for the new
   instance. If NULL, an instance with unspecified defaults is
   used. These configuration pieces may not be modified after the
   instance is created.

   It returns 0 if *pp is ready to use and non-0 if either allocation
   fails (in which case *pp will be set to 0) or initialization of *pp
   failed (in which case cmpp_err_get() can be used to determine why
   it failed). In either case, the caller must eventually pass *pp to
   cmpp_dtor() to free it.

   If the library is built with the symbol CMPP_CTOR_INSTANCE_INIT
   defined, it must refer to a function with this signature:

   int CMPP_CTOR_INSTANCE_INIT(cmpp *);

   The library calls this before returning and arranges to call it
   lazily if pp gets reset.  The intent is that the init function
   installs custom directives using cmpp_d_register(). That
   initialization, on error, is expected to set its argument's error
   state with cmpp_err_set().
*/
CMPP_EXPORT int cmpp_ctor(cmpp **pp, cmpp_ctor_cfg const * cfg);

/**
   If pp is not NULL, it is passed to cmpp_reset() and then freed.
*/
CMPP_EXPORT void cmpp_dtor(cmpp *pp);


/**
   realloc(3)-compatible allocator used by the library.

   This API very specifically uses sqlite3_realloc() as its basis.
*/
CMPP_EXPORT void * cmpp_mrealloc(void * p, size_t n);

/**
   malloc(3)-compatible allocator used by the library.

   This API very specifically uses sqlite3_malloc() as its basis.
*/
CMPP_EXPORT void * cmpp_malloc(size_t n);

/**
   free(3)-compatible deallocator. It can also be used as a destructor
   for cmpp_d_register() _if_ the memory in question is allocated by
   cmpp_malloc(), cmpp_realloc(), or the sqlite3_malloc() family of
   APIs.

   This is not called cmpp_free() to try to avoid any confusion with
   cmpp_dtor().
*/
CMPP_EXPORT void cmpp_mfree(void *);

/**
   If m is NULL then pp's persistent error code is set to CMPP_RC_OOM,
   else this is a no-op. Returns pp's error code.

   To simplify certain uses, pp may be NULL, in which case this
   function returns CMPP_RC_OOM if m is NULL and 0 if it's not.
*/
CMPP_EXPORT int cmpp_check_oom(cmpp * const pp, void const * const m );

/**
   Re-initializes all state of pp. This saves some memory for reuse
   but resets it all to default states. This closes the database and
   will also reset any autoloader, policies, or delimiter
   configurations to their compile-time defaults. It retains only a
   small amount of state, like any configuration which was passed to
   cmpp_ctor().

   After calling this, pp is in a cleanly-initialized state and may be
   re-used with the cmpp API. Its database will not be initialized
   until an API which needs it is called, so pp can be used with
   functions which may otherwise be prohibited after the db is
   opened. (Do we still have any?)

   As of this writing, this is the only way to reliably recover a cmpp
   instance from any significant errors. Errors may do things like
   leave savepoints out of balance, and this cleanup step resets all
   of that state. However, it also loses state like the autoloader.

   TODO?: we need(?) a partial-clear operation which keeps some of the
   instance's state, most notably custom directives, the db handle,
   and any cached prepared statements. See cmpp_err_set() for the
   distinction between recoverable and non-recoverable errors.
*/
CMPP_EXPORT void cmpp_reset(cmpp *pp);

#if 0
Not yet;
/**
   If called before pp has initialized its database, this sets the
   file name used for that database. If called afterwards, pp's error
   state is updated and CMPP_RC_MISUSE is returned.  If called while
   pp has error state set, that code is returned without side-effects.

   This does not open the database. It is opened on demand when
   processing starts.

   On success it returns 0 and this function makes a copy of zName.

   As a special case, zName may be NULL to use the default name, but
   there is little reason to do so unless one changes their mind after
   setting it to non-NULL.
 */
CMPP_EXPORT int cmpp_db_name_set(cmpp *pp, const char * zName);
#endif

/**
   Returns true if the bytes in the range [zName, zName+n) comprise a
   legal name for a directive or a define.

   It disallows any control characters, spaces, and most punctuation,
   but allows alphanumeric (but must not start with a number) as well
   as any of: -./:_ (but it may not start with '-').  Any characters
   with a high bit set are assumed to be UTF-8 and are permitted as
   well.

   The name's length  is limited, rather arbitrarily, to 64 bytes.

   If the key is not legal then false is returned and if zErrPos is
   not NULL then *zErrPos is set to the position in zName of the first
   offending character. If validation fails because n is too long then
   *zErrPos (if zErrPos is not NULL) will be set to 0.

   Design note: this takes unsigned characters because it most
   commonly takes input from cmpp_args::z strings.
*/
CMPP_EXPORT bool cmpp_is_legal_key(unsigned char const *zName,
                                   cmpp_size_t n,
                                   unsigned char const **zErrPos);

/**
   Adds the given `#define` macro name to the list of macros, overwriting
   any previous value.

   zKey must be NUL-terminated and legal as a key. The rules are the
   same as for cmpp_is_legal_key() except that a '=' is also permitted
   if it's not at the start of the string because...

   If zVal is NULL then zKey may contain an '=', from which the value
   will be extracted. If zVal is not NULL then zKey may _not_ contain
   an '='.

   The ability for zKey to contain a key=val was initially to
   facilitate input from the CLI (e.g. -Dfoo=bar) because cmpp was
   initially a CLI app (as opposed to a library). It's considered a
   "legacy" feature, not recommended for most purposes, but it _is_
   convenient for that particular purpose.

   Returns 0 on success and updates pp's error state on error.

   See: cmpp_define_v2()
   See: cmpp_undef()
*/
CMPP_EXPORT int cmpp_define_legacy(cmpp *pp, const char * zKey,
                                   char const *zVal);

/**
   Works like cmpp_define_legacy() except that it does not examine zKey to
   see if it contains an '='.
*/
CMPP_EXPORT int cmpp_define_v2(cmpp *pp, const char * zKey, char const *zVal);

/**
   Removes the given `#define` macro name from the list of
   macros. zKey is, in this case, treated as a GLOB pattern, and all
   matching defines are deleted.

   If nRemoved is not NULL then, on success, it is set to the number
   of entries removed by this call.

   Returns 0 on success and updates pp's error state on error. It is not
   an error if no value was undefined.

   This does _not_ affect defines made using cmpp_define_shadow().
*/
CMPP_EXPORT int cmpp_undef(cmpp *pp, const char * zKey,
                           unsigned int *nRemoved);

/**
   This works similarly to cmpp_define_v2() except that:

   - It does not permit its zKey argument to contain the value
     part like that function does.

   - The new define "shadows", rather than overwrites, an existing
     define with the same name.

   All APIs which look up define keys will get the value of the shadow
   define.  The shadow can be uninstalled with cmpp_define_unshadow(),
   effectively restoring its previous value (if any).  That function
   should be called one time for each call to this one, passing the
   same key to each call.  A given key may be shadowed any number of
   times by this routine. Each one saves the internal ID of the shadow
   into *pId (and pId must not be NULL). That value must be passed to
   cmpp_define_unshadow() to ensure that the "shadow stack" stays
   balanced in the face of certain error-handling paths.

   cmpp_undef() will _not_ undefine an entry added through this
   interface.

   Returns pp's persistent error code (0 on success).

   Design note: this function was added to support adding a define
   named __FILE__ to input scripts which works like it does in a C
   preprocessor. Alas, supporting __LINE__ would be much more costly,
   as it would have to be updated in the db from several places, so
   its cost would outweigh its meager benefits.
*/
CMPP_EXPORT int cmpp_define_shadow(cmpp *pp, char const *zKey,
                                   char const *zVal,
                                   int64_t * pId);

/**
   Removes the most shadow define matching the zKey and id values
   which where previously passed to cmpp_define_shadow().  It is not
   an error if no match is found, in which case this function has no
   visible side-effects.

   Unlike cmpp_undef(), zKey is matched precisely, not against a glob.

   In order to keep the "shadow stack" properly balanced, this will
   delete any shadow entries for the given key which have the same id
   or a newer one (i.e. they were left over from a missed call to
   cmpp_define_unshadow()).

   Returns pp's persistent error code (0 on success).
*/
CMPP_EXPORT int cmpp_define_unshadow(cmpp *pp, char const *zKey,
                                     int64_t id);

/**
   Adds the given dir to the list of includes. They are checked in the
   order they are added.
*/
CMPP_EXPORT int cmpp_include_dir_add(cmpp *pp, const char * zKey);

/**
   Sets pp's default output channel. If pp already has a channel, it
   is closed[^1].

   The second argument, if not NULL, is _bitwise copied_, which has
   implications for the ownership of out->state (see below). If it is
   is NULL, cmpp_outputer_empty is copied in its place, which makes
   further output a no-op.

   The third argument is a symbolic name for the channel (perhaps its
   file name). It is used in debugging and error messages.  cmpp does
   _not_ copy it, so its bytes must outlive the cmpp instance. (In
   practice, the byte names come from main()'s argv or scope-local
   strings in the same scope as the cmpp instance.) This argument
   should only be NULL if the second argument is.

   cmpp_reset(), or opening another channel, will end up calling
   out->cleanup() (if it's not NULL) and passing it a pointer to a
   _different_ cmpp_outputer object, but with the _same_
   cmpp_outputer::state pointer, which may invalidate out->state.

   To keep cmpp from doing that, make a copy of the output object, set
   the cleanup member of that copy to NULL, then pass that copy to this
   function. It is then up to the client to call out->cleanup(out) when
   the time is right.

   For example:

   ```
   cmpp_outputer my = cmpp_outputer_FILE;
   my.state = cmpp_fopen("/some/file", "wb");
   cmpp_outputer tmp = my;
   tmp.cleanup = NULL;
   cmpp_outputer_set( pp, &tmp, "my file");
   ...
   my.cleanup(&my); // will cmpp_fclose(my.state)
   ```

   Potential TODO: internally store the output channel as a pointer.
   It's not clear whether that would resolve the above grief or
   compound it.

   [^1]: depending on the output channel, it might not _actually_ be
   closed, but pp is disassociated from it, in any case.
*/
CMPP_EXPORT
void cmpp_outputer_set(cmpp *pp, cmpp_outputer const *out, char const *zName);

/**
   Treats the range (zIn,zIn+nIn] as a complete cmpp input and process
   it appropriately. zName is the name of the input for purposes of
   error messages. If nIn is negative, strlen() is used to calculate
   it.

   This is a no-op if pp has any error state set. It returns pp's
   persistent error code.
*/
CMPP_EXPORT int cmpp_process_string(cmpp *pp, const char * zName,
                                    unsigned char const * zIn,
                                    cmpp_ssize_t nIn);

/**
   A thin proxy for cmpp_process_string() which reads its input from
   the given file. Returns 0 on success, else returns pp's persistent
   error code.
*/
CMPP_EXPORT int cmpp_process_file(cmpp *pp, const char * zName);

/**
   A thin proxy for cmpp_process_string() which reads its input from
   the given input source, consuming it all before passing it
   on. Returns 0 on success, else returns pp's persistent error code.
*/
CMPP_EXPORT int cmpp_process_stream(cmpp *pp, const char * zName,
                                    cmpp_input_f src, void * srcState);

/**
   Process the given main()-style arguments list. When calling from
   main(), be sure to pass it main()'s (argc+1, argv+1) to skip argv[0]
   (the binary's name).

   Each argument is expected to be one of the following:

   1) One of --help or -?: causes this function to return CMPP_RC_HELP
   without emitting any output.

   2) -DX or -DX=Y: sets define X to 1 (if no "=" is used and no Y given) or to
   Y.

   3) -UX: unsets all defines matching glob X.

   4) -FX=Y: works like -DX=Y but treats Y as a filename and sets X to
   the contents of that file.

   5) -IX: adds X to the "include path". If _no_ include path is
   provided then cmpp assumes a path of ".", but if _any_ paths are
   provided then it does not assume that "." is in the path.

   6) --chomp-F: specifies whether subsequent -F flags should "chomp"
   one trailing newline from their input files.

   7) --delimiter|-d=X sets the directive delimiter to X. Its default
   is a compile-time constant.

   8) --output|-o=filename: sets the output channel to the given file.
   A value of "-" means stdout. If no output channel is opened when
   this is called, and files are to be processed, stdout is
   assumed. (That's a historical artifact from earlier evolutions.)
   To override that behavior use cmpp_outputer_set().

   9) --file|-f=filename: sets the input channel to the given file.
   A value of "-" means stdin.

   10) -e=SCRIPT Treat SCRIPT as a complete c-pp input and process it.
   Because it's difficult to pack multiple lines of text into this,
   it's really of use for testing #expr and #assert.

   11) --@policy=X sets the @token@ parsing policy. X must be
   one of (retain, elide, error, off) and defaults to off.

   12) -@: shorthand for --@policy=error.

   13) --sql-trace: enables tracing of all SQL statements to stderr.
   This is useful for seeing how a script interacts with the
   database. Use --no-sql-trace to disable it.

   14) --sql-trace-x: like --sql-trace but replaces bound parameter
   placeholders with their SQL values. Use --no-sql-trace to disable
   it.

   15) --dump-defines: emit all defines to stdout. They should
   arguably go to stderr but that interferes with automated testing.

   Any argument which does not match one of the above flags, and does
   not start with a "-", is treated as if it were passed to the --file
   flag.

   Flags may start with either 1 or 2 dashes - they are equivalent.

   Flags which take a value may either be in the form X=Y or X Y, i.e.
   may be a single argv entry or a pair of them.

   It performs two passes on the arguments: the first is for validation
   checking for --help/-?. No processing of the input(s) and output(s)
   happens unless the first pass completes. Similarly, no validation of
   whether any provided filename are actually readable is performed
   until the second pass.

   Arguments are processed in the order they are given. Thus the following
   have completely different meanings:

   1) -f foo.in -Dfoo
   2) -Dfoo -f foo.in

   The former will process foo.in before defining foo.

   This behavior makes it possible to process multiple input files in
   a single go:

   --output foo.out foo.in -Dfoo foo.in -Ufoo --output bar.out -Dbar foo.in

   It returns 0 on success. cmpp_err_get() can be used to fetch any
   error message.
*/
CMPP_EXPORT int cmpp_process_argv(cmpp *pp, int argc, char const * const * argv);

/**
   Intended to be called if cmpp_process_argv() returns CMPP_RC_HELP.
   It emits --help-style text to the given output stream.
   As the first argument pass it either argv[0] or NULL. The second
   should normally be stdout or stderr.

   Reminder to self: this could take a (cmpp_output_f,void*) pair
   instead, and should do so for the sake of WASI builds, but its impl
   currently relies heavily on fprintf() formatting.
*/
CMPP_EXPORT void cmpp_process_argv_usage(char const *zAppName,
                                         cmpp_FILE *os);

/**
   Returns pp's current error number (from the cmpp_rc_e enum) and
   sets *zMsg (if zMsg is not NULL) to the error string. The bytes are
   owned by pp and may be invalidated by any functions which take pp
   as an argument.

   See cmpp_err_get() for more information.

*/
CMPP_EXPORT int cmpp_err_get(cmpp *pp, char const **zMsg);

/**
   Sets or clears (if 0==rc) pp's persistent error state. zFmt may be
   NULL or a format string compatible with sqlite3_mprintf().

   To simplify certain uses, this is a no-op if pp is NULL, returning
   rc without other side effects.

   Returns rc with one exception: if allocation of a copy of the error
   string fails then CMPP_RC_OOM will be returned (and pp will be
   updated appropriately).

   If pp is currently processing a script, the resulting error string
   will be prefixed with the name of the current input script and the
   line number of the directive which triggered the error.

   It is legal for zFmt to be NULL or an empty string, in which case a
   default, vague error message is used (without requiring allocation
   of a new string).

   Recoverable vs. unrecoverable errors:

   Most cmpp APIs become no-ops if their cmpp object has error state
   set, treating any error as unrecoverable. That approach simplifies
   writing code for it by allowing multiple calls to be chained
   without concern for whether the previous one succeeded.

   ACHTUNG: simply clearing the error state by passing 0 as the 2nd
   argument to this function is _not_ enough to recover from certain
   errors. e.g. an error in the middle of a script may leave db
   savepoints imbalanced. The only way to _fully_ recover from any
   significant failures is to use cmpp_reset(), which resets all of
   pp's state.

   APIs which may set the error state but are recoverable by simply
   clearing that state will document that. Errors from APIs which do
   not claim to be recoverable in error cases must be treated as
   unrecoverable.

   See cmpp_err_get() for more information.

   FIXME: we need a different variant for WASM builds, where variadics
   aren't a usable thing.

   Potential TODO: change the error-reporting interface to support
   distinguishing from recoverable and non-recoverable errors.  "The
   problem" is that no current uses need that - they simply quit and
   free up the cmpp instance on error. Maybe that's the way it
   _should_ be.
*/
CMPP_EXPORT int cmpp_err_set(cmpp *pp, int rc, char const *zFmt, ...);

/**
   A variant of cmpp_err_set() which is not variadic, as a consolation
   for WASM builds. zMsg may be NULL. The given string, if not NULL,
   is copied.
*/
CMPP_EXPORT int cmpp_err_set1(cmpp *pp, int rc, char const *zMsg);

#if 0
/**
   Clears any error state in pp. Most cmpp APIs become no-ops if their
   cmpp instance has its error flag set.

   See cmpp_err_get() for important details about doing this.
*/
//CMPP_EXPORT void cmpp_err_clear(cmpp *pp);

/**
   This works like a combination of cmpp_err_get() and
   cmpp_err_clear(), in that it clears pp's error state by transferring
   ownership of it to the caller. If pp has any error state, *zMsg is
   set to the error string and the error code is returned, else 0 is
   returned and *zMsg is set to 0.

   The string returned via *zMsg must eventually be passed to
   cmpp_mfree() to free it.

   This function is provided simply as an optimization to avoid
   having to copy the error string in some cases.

   ACHTUNG: see the ACHTUNG in cmpp_err_clear().
*/
CMPP_EXPORT int cmpp_err_take(cmpp *pp, char **zMsg);
#endif

/**
   Returns pp's current error code, which will be 0 if it currently
   has no error state.

   To simplify certain uses, this is a no-op if pp is NULL, returning
   0.
*/
CMPP_EXPORT int cmpp_err_has(cmpp const * pp);

/**
   Returns true if pp was initialized in "safe mode". That is: if the
   cmpp_ctor_F_SAFEMODE flag was passed to cmpp_ctor().

   To simplify certain uses, this is a no-op if pp is NULL, returning
   false.
*/
CMPP_EXPORT bool cmpp_is_safemode(cmpp const * pp);

/**
   Starts a new SAVEPOINT in the database. Returns non-0, and updates
   pp's persistent error state, on failure.

   If this returns 0, the caller is obligated to later call either
   cmpp_sp_commit() or cmpp_sp_rollback() later.
*/
CMPP_EXPORT int cmpp_sp_begin(cmpp *pp);

/**
   Commits the most recently-opened savepoint UNLESS pp's error state
   is set, in which case this behaves like cmpp_sp_rollback().
   Returns 0 on success.

   A call to cmpp_sp_begin() which returns 0 obligates the caller to
   call either cmpp_sp_rollback() or cmpp_sp_commit(). It is illegal
   for either to be called in any other context.
*/
CMPP_EXPORT int cmpp_sp_commit(cmpp *pp);

/**
   Rolls back the most recently-opened savepoint.  Returns 0 on
   success.

   A call to cmpp_sp_begin() which returns 0 obligates the caller to
   call either cmpp_sp_rollback() or cmpp_sp_commit(). It is illegal
   for either to be called in any other context.
*/
CMPP_EXPORT int cmpp_sp_rollback(cmpp *pp);

/**
   A cmpp_output_f() impl which requires state to be a (FILE*), which
   this function passes the call on to fwrite(). Returns 0 on
   success, CMPP_RC_IO on error.

   If state is NULL then stdout is used.
*/
CMPP_EXPORT int cmpp_output_f_FILE(void * state, void const * src, cmpp_size_t n);

/**
   A cmpp_output_f() impl which requires state to be a ([const] int*)
   referring to a writable file descriptor, which this function
   dereferences and passes to write(2).
*/
CMPP_EXPORT int cmpp_output_f_fd(void * state, void const * src, cmpp_size_t n);

/**
   A cmpp_input_f() implementation which requires that state be
   a readable (FILE*) handle, which it passes to fread(3).
*/
CMPP_EXPORT int cmpp_input_f_FILE(void * state, void * dest, cmpp_size_t * n);

/**
   A cmpp_input_f() implementation which requires that state be a
   readable file descriptor, in the form of an ([const] int*), which
   this function passes to write(2).
*/
CMPP_EXPORT int cmpp_input_f_fd(void * state, void * dest, cmpp_size_t * n);

/**
   A cmpp_flush_f() impl which expects pFile to be-a (FILE*) opened
   for writing, which this function passes the call on to
   fflush(). If fflush() returns 0, so does this function, else it
   returns non-0.
*/
CMPP_EXPORT int cmpp_flush_f_FILE(void * pFile);

/**
   A generic streaming routine which copies data from an
   cmpp_input_f() to an cmpp_outpuf_f().

   Reads all data from inF(inState,...) in chunks of an unspecified
   size and passes them on to outF(outState,...). It reads until inF()
   returns fewer bytes than requested or returns non-0. Returns the
   result of the last call to outF() or (if reading fails) inF().
   Results are undefined if either of inState or outState arguments
   are NULL and their callbacks require non-NULL.  (This function
   cannot know whether a NULL state argument is legal for the given
   callbacks.)

   Here is an example which basically does the same thing as the
   cat(1) command on Unix systems:

   ```
   cmpp_stream(cmpp_input_f_FILE, stdin, cmpp_output_f_FILE, stdout);
   ```

   Or copy a FILE to a string buffer:

   ```
   cmpp_b os = cmpp_b_empty;
   FILE * f = cmpp_fopen(...);
   rc = cmpp_stream(cmpp_input_f_FILE, f, cmpp_output_f_b, &os);
   // On error os might be partially populated.
   // Eventually clean up the buffer:
   cmpp_b_clear(&os);
   ```
*/
CMPP_EXPORT int cmpp_stream(cmpp_input_f inF, void * inState,
                            cmpp_output_f outF, void * outState);

/**
   Reads the entire contents of the given input stream, allocating it
   in a buffer. On success, returns 0, assigns *pOut to the buffer,
   and *nOut to the number of bytes read (which will be fewer than are
   allocated). It guarantees that on success it NUL-terminates the
   buffer at one byte after the returned size, with one exception: if
   the string has no input, both *pOut and *nOut will be set to 0.

   On error it returns whatever code xIn() returns.
*/
CMPP_EXPORT int cmpp_slurp(cmpp_input_f xIn, void *stateIn,
                           unsigned char **pOut, cmpp_size_t * nOut);

/**
   _Almost_ equivalent to fopen(3) but:

   - If name=="-", it returns one of stdin or stdout, depending on the
   mode string: stdout is returned if 'w' or '+' appear, otherwise
   stdin.

   If it returns NULL, the global errno "should" contain a description
   of the problem unless the problem was argument validation.

   If at all possible, use cmpp_fclose() (as opposed to fclose()) to
   close these handles, as it has logic to skip closing the three
   standard streams.
*/
CMPP_EXPORT cmpp_FILE * cmpp_fopen(char const * name, char const *mode);

/**
   Passes f to fclose(3) unless f is NULL or one of the C-standard
   handles (stdin, stdout, stderr), in which cases it does nothing at
   all.
*/
CMPP_EXPORT void cmpp_fclose(cmpp_FILE * f);

/**
   A cleanup callback interface for use with cmpp_outputer::cleanup().
   Implementations must handle self->state appropriately for its type,
   and clear self->state if appropriate, but must not free the self
   object. It is implementation-specified whether self->state and/or
   self->name are set to NULL by this function. Whether they should be
   often depends on how they're used.
*/
typedef void (*cmpp_outputer_cleanup_f)(cmpp_outputer *self);

/**
   An interface which encapsulates data for managing a streaming
   output destination, primarily intended for use with cmpp_stream()
   but also used internally by cmpp for directing output to a buffer.
*/
struct cmpp_outputer {
  /**
     An optional descriptive name for the channel. The bytes
     are owned elsewhere and are typically static or similarly
     long-lived.
  */
  char const * name;

  /**
     Output channel.
  */
  cmpp_output_f out;

  /**
     flush() implementation. This may be NULL for most uses of this
     class. Cases which specifically require it must document that
     requirement so.
  */
  cmpp_flush_f flush;

  /**
     Optional: if not NULL, it must behave appropriately for its state
     type, cleaning up any memory it owns.
  */
  cmpp_outputer_cleanup_f cleanup;

  /**
     State to be used when calling this->out() and this->flush(),
     namely: this->out(this->state, ... ) and
     this->flush(this->state).

     Whether or not any given instance of this class owns the memory
     pointed to by this member must be documented for their cleanup()
     method.

     Because cmpp_outputer instances frequently need to be stashed and
     unstashed via bitwise copying, it is illegal to replace this
     pointer after its initial assignment. The object it points to may
     be mutated freely, but this pointer must stay stable for the life
     of this object.
  */
  void * state;
};

/**
   Empty-initialized cmpp_outputer instance, intended for
   const-copy initialization.
*/
#define cmpp_outputer_empty_m                                 \
  {.name=NULL, .out = NULL,.flush = NULL, .cleanup = NULL, .state =NULL}

/**
   Empty-initialized cmpp_outputer instance, intended for
   non-const-copy initialization. These copies can, for purposes of
   cmpp's output API, be used as-is to have cmpp process its inputs
   but generate no output.
*/
CMPP_EXPORT const cmpp_outputer cmpp_outputer_empty;

/**
   If o->out is not NULL, the result of o->out(o->state,p,n) is
   returned, else 0 is returned.
*/
CMPP_EXPORT int cmpp_outputer_out(cmpp_outputer *o, void const *p, cmpp_size_t n);

/**
   If o->flush is not NULL, the result of o->flush(o->state) is
   returned, else 0 is returned.
*/
CMPP_EXPORT int cmpp_outputer_flush(cmpp_outputer *o);

/**
   If o->cleanup is not NULL, it is called, otherwise this is a no-op.
*/
CMPP_EXPORT void cmpp_outputer_cleanup(cmpp_outputer *o);

/**
   A cmpp_outputer initializer which uses cmpp_flush_f_FILE(),
   cmpp_output_f_FILE(), and cmpp_outputer_cleanup_f_FILE() for its
   implementation. After copying this, the state member must be
   pointed to an opened-for-writing (FILE*).
*/
CMPP_EXPORT const cmpp_outputer cmpp_outputer_FILE;

/**
   The cmpp_outputer_cleanup_f() impl used by cmpp_outputer_FILE.  If
   self->state is not NULL then it is passed to fclose() (_unless_ it
   is stdin, stdout, or stderr) and set to NULL. self->name is
   also set to NULL.
*/
CMPP_EXPORT void cmpp_outputer_cleanup_f_FILE(cmpp_outputer *self);

/**
   Sets pp's current directive delimiter to a copy of the
   NUL-terminated zDelim. The delimiter is the sequence which starts
   line and distinguishes cmpp directives from other input, in the
   same way that C preprocessors use '#' as a delimiter.

   If zDelim is NULL then the default delimiter is used. The default
   delimiter can be set when compiling the library by defining
   CMPP_DEFAULT_DELIM to a quoted string value.

   zDelim is assumed to be in UTF-8 encoding. If any bytes in the
   range (0,32) are found, CMPP_RC_MISUSE is returned and pp's
   persistent error state is set.

   The delimiter must be short and syntactically unambiguous for the
   intended inputs. It has a rather arbitrary maximum length of 12,
   but it's difficult to envision it being remotely human-friendly
   with a delimiter longer than 3 bytes. It's conceivable, but
   seemingly far-fetched, that longer delimiters might be interesting
   in some machine-generated cases, e.g. using a random sequence as
   the delimiter.

   Returns 0 on success. Returns non-0 if called when the delimiter
   stack is empty, if it cannot copy the string or zDelim is deemed
   unsuitable for use as a delimiter. Calling this when the stack is
   empty represents a serious API misuse (indicating that
   cmpp_delimiter_pop() was used out of scope) and will trigger an
   assert() in debug builds.  Except for that last case, errors from
   this function are recoverable (see cmpp_err_set()).
*/
CMPP_EXPORT int cmpp_delimiter_set(cmpp *pp, char const *zDelim);

/**
   Fetches pp's current delimiter string, assigning it to *zDelim.
   The string is owned by pp and will be invalidated by any call to
   cmpp_delimiter_set() or the #delimiter script directive.

   If, by some odd usage constellation, this is called after an
   allocation of the delimiter stack has failed, this will set *zDelim
   to the compile-time-default delimiter. That "cannot happen" in normal use because such a failure
   would have been reacted to and this would not be called.
*/
CMPP_EXPORT void cmpp_delimiter_get(cmpp const *pp, char const **zDelim);

/**
   Pushes zDelim as the current directive delimiter. Returns 0 on
   success and non-zero on error (invalid zDelim value or allocation
   error).  If this returns 0 then the caller is obligated to
   eventually call cmpp_delimiter_pop() one time. If it returns non-0
   then they must _not_ call that function.
 */
CMPP_EXPORT int cmpp_delimiter_push(cmpp *pp, char const *zDelim);

/**
   Must be called one time for each successful call to
   cmpp_delimiter_push(). It restores the directive delimimter to the
   value it has when cmpp_delimiter_push() was last called.

   Returns pp's current error code, and will set it to non-0 if called
   when no cmpp_delimiter_push() is active. Popping an empty stack
   represents a serious API misuse and may fail an assert() in debug
   builds.
*/
CMPP_EXPORT int cmpp_delimiter_pop(cmpp *pp);

/**
   If z[*n] ends on a \n or \r\n pair, it/they are stripped,
   *z is NUL-terminated there, and *n is adjusted downwards
   by 1 or 2. Returns true if it chomped, else false.
*/
CMPP_EXPORT bool cmpp_chomp(unsigned char * z, cmpp_size_t * n);

/**
   A basic memory buffer class. This is primarily used with
   cmpp_outputer_b to capture arbitrary output for later use.
   It's also used for incrementally creating dynamic strings.

   TODO: add the heuristic that an nAlloc of 0 with a non-NULL z
   refers to externally-owned memory. This would change the
   buffer-write APIs to automatically copy it before making any
   changes. We have code for this in the trees this class derives
   from, it just needs to be ported over. It would allow us to avoid
   allocating in some cases where we need a buffer but it will always
   (or commonly) be a copy of a static string, like a single space.
*/
struct cmpp_b {
  /**
     This buffer's memory, owned by this object. This library exclusively
     uses sqlite3_realloc() and friends for memory management.

     If this pointer is taken away from this object then it must
     eventually be passed to cmpp_mfree().
  */
  unsigned char * z;
  /**
     Number of bytes of this->z which are in use, not counting any
     automatic NUL terminator which this class's APIs may add.
  */
  cmpp_size_t n;
  /**
     Number of bytes allocated in this->z.

     Potential TODO: use a value of zero here, with a non-zero
     this->n, to mean that this->z is owned elsewhere. This would
     cause cmpp_b_append() to copy its original source before
     appending. Similarly, cmpp_b_clear() would necessarily _not_
     free this->z. We've used that heuristic in a predecessor of this
     class in another tree to good effect for years, but it's not
     certain that we'd get the same level of utility out of that
     capability as we do in that other project.
  */
  cmpp_size_t nAlloc;

  /**
     cmpp_b APIs which may fail will set this. Similarly, most
     of the cmpp_b APIs become no-ops if this is non-0.
  */
  int errCode;
};

typedef struct cmpp_b cmpp_b;

/**
   An empty-initialized cmpp_b struct for use in const-copy
   initialization.
*/
#define cmpp_b_empty_m {.z=0,.n=0,.nAlloc=0,.errCode=0}

/**
   An empty-initialized cmpp_b struct for use in non-copy copy
   initialization.
*/
extern const cmpp_b cmpp_b_empty;

/**
   Frees s->z and zeroes out s but does not free s.
*/
CMPP_EXPORT void cmpp_b_clear(cmpp_b *s);

/**
   If s has content, s->nUsed is set to 0 and s->z is NUL-terminated
   at its first byte, else this is a no-op. s->errCode is
   set to 0. Returns s.
 */
CMPP_EXPORT cmpp_b * cmpp_b_reuse(cmpp_b *s);

/**
   Swaps all contents of the given buffers, including their persistent
   error code.
*/
CMPP_EXPORT void cmpp_b_swap(cmpp_b * l, cmpp_b * r);

/**
   If s->errCode is 0 and s->nAlloc is less than n, s->z is
   reallocated to have at least n bytes, else this is a no-op. Returns
   0 on success, CMPP_RC_OOM on error.
*/
CMPP_EXPORT int cmpp_b_reserve(cmpp_b *s, cmpp_size_t n);

/**
   Works just like cmpp_b_reserve() but on allocation error it
   updates pp's error state.
*/
CMPP_EXPORT int cmpp_b_reserve3(cmpp * pp, cmpp_b * os, cmpp_size_t n);

/**
   Appends n bytes from src to os, reallocating os as necessary.
   Returns 0 on succes, CMPP_RC_OOM on allocation error.

   Errors from this function, and the other cmpp_b_append...()
   variants, are recoverable (see cmpp_err_set()).
*/
CMPP_EXPORT int cmpp_b_append(cmpp_b * os, void const *src,
                              cmpp_size_t n);

/**
   Works just like cmpp_b_append() but on allocation error it
   updates pp's error state.
*/
CMPP_EXPORT int cmpp_b_append4(cmpp * pp,
                               cmpp_b * os,
                               void const * src,
                               cmpp_size_t n);

/**
   Appends ch to the end of os->z, expanding as necessary, and
   NUL-terminates os. Returns os->errCode and is a no-op if that is
   non-0 when this is called. This is slightly more efficient than
   passing length-1 strings to cmpp_b_append() _if_ os's memory
   is pre-allocated with cmpp_b_reserve(), otherwise it may be
   less efficient because it may need to allocate frequently if used
   repeatedly.
*/
CMPP_EXPORT int cmpp_b_append_ch(cmpp_b * os, char ch);

/**
   Appends a decimal string representation of d to os. Returns
   os->errCode and is a no-op if that is non-0 when this is called.
*/
CMPP_EXPORT int cmpp_b_append_i32(cmpp_b * os, int32_t d);

/** int64_t counterpart of cmpp_b_append_i32(). */
CMPP_EXPORT int cmpp_b_append_i64(cmpp_b * os, int64_t d);

/**
   A thin wrapper around cmpp_chomp() which chomps b->z.
*/
CMPP_EXPORT bool cmpp_b_chomp(cmpp_b * b);

/**
   A cmpp_output_f() impl which requires that its first argument be a
   (cmpp_b*) or be NULL. If buffer is not NULL then it appends n bytes
   of src to buffer, reallocating as needed. Returns CMPP_RC_OOM in
   reallocation error. On success it always NUL-terminates buffer->z.
   A NULL buffer is treated as success but has no side effects.

   Example usage:

   ```
   cmpp_b os = cmpp_b_empty;
   int rc = cmpp_stream(cmpp_input_f_FILE, stdin,
                        cmpp_output_f_b, &os);
   ...
   cmpp_b_clear(&os);
   ```
*/
CMPP_EXPORT int cmpp_output_f_b(void * buffer, void const * src,
                                cmpp_size_t n);

/**
   A cmpp_outputer_cleanup_f() implementation which requires that
   self->state be either NULL or a cmpp_b pointer. This function
   passes it to cmpp_b_clear(). It does _not_ set self->state or
   self->name to NULL.
*/
CMPP_EXPORT void cmpp_outputer_cleanup_f_b(cmpp_outputer *self);

/**
   A cmpp_outputer prototype which can be copied to use a dynamic
   string buffer as an output source. Its state member must be set (by
   the client) to a cmpp_b instance. Its out() method is
   cmpp_output_f_b().  Its cleanup() method is
   cmpp_outputer_cleanup_f_b(). It has no flush() method.
*/
extern const cmpp_outputer cmpp_outputer_b;

/**
   Returns a string containing version information in an unspecified
   format.
*/
CMPP_EXPORT char const * cmpp_version(void);

/**
   Type IDs for directive lines and argument-parsing tokens.

   This is largely a historical artifact and work is underway
   to factor this back out of the public API.
*/
enum cmpp_tt {

/**
   X-macro which defines token types. It invokes E(X,Y) for each
   entry, where X is the base name part of the token type and Y is the
   token name as it appears in input scripts (if any, else it's 0).

   Maintenance reminder: their ordering in this map is insignificant
   except that None must be first and must have the value 0.

   Some of the more significant ones are:

   - Word: an unquoted word-like token.

   - String: a quoted string.

   - StringAt: an @"..." string.

   - GroupParen, GroupBrace, GroupSquiggly: (), [], and {}

   - All which start with D_ are directives. D_Line is a transitional
     state between "unparsed" and another D_... value.
*/
#define cmpp_tt_map(E)    \
  E(None,         0)            \
  E(RawLine,      0)            \
  E(Unknown,      0)            \
  E(Word,         0)            \
  E(Noop,         0)            \
  E(Int,          0)            \
  E(Null,         0)            \
  E(String,       0)            \
  E(StringAt,     0)            \
  E(GroupParen,   0)            \
  E(GroupBrace,   0)            \
  E(GroupSquiggly,0)            \
  E(OpEq,         "=")          \
  E(OpNeq,        "!=")         \
  E(OpLt,         "<")          \
  E(OpLe,         "<=")         \
  E(OpGt,         ">")          \
  E(OpGe,         ">=")         \
  E(ArrowR,       "->")         \
  E(ArrowL,       "<-")         \
  E(Plus,         "+")          \
  E(Minus,        "-")          \
  E(ShiftR,       ">>")         \
  E(ShiftL,       "<<")         \
  E(ShiftL3,      "<<<")        \
  E(OpNot,        "not")        \
  E(OpAnd,        "and")        \
  E(OpOr,         "or")         \
  E(OpDefined,    "defined")    \
  E(OpGlob,       "glob")       \
  E(OpNotGlob,    "not glob")   \
  E(AnyType,      0)            \
  E(Eof,          0)

#define E(N,TOK) cmpp_TT_ ## N,
  cmpp_tt_map(E)
#undef E
  /** Used by cmpp_d_register() to assign new IDs. */
  cmpp_TT__last
};
typedef enum cmpp_tt cmpp_tt;

/**
   For all of the cmpp_tt enum entries, returns a string form of the
   enum entry name, e.g. "cmpp_TT_D_If". Returns NULL for any other
   values
*/
CMPP_EXPORT char const * cmpp_tt_cstr(int tt);

/**
   Policies for how to handle undefined @tokens@ when performing
   content filtering.
*/
enum cmpp_atpol_e {
  /** Sentinel value. */
  cmpp_atpol_invalid = -1,
  /** Turn off @token@ parsing. */
  cmpp_atpol_OFF = 0,
  /** Retain undefined @token@ - emit it as-is. */
  cmpp_atpol_RETAIN,
  /** Elide undefined @token@. */
  cmpp_atpol_ELIDE,
  /** Error for undefined @token@. */
  cmpp_atpol_ERROR,
  /** A sentinel value for use with cmpp_dx_out_expand(). */
  cmpp_atpol_CURRENT,
  /**
    This isn't _really_ the default. It's the default for the
    --@policy CLI flag and #@pragma when it's given no value.
  */
  cmpp_atpol_DEFAULT_FOR_FLAG = cmpp_atpol_ERROR,
  /**
     The compile-time default for all cmpp instances.
  */
  cmpp_atpol_DEFAULT  = cmpp_atpol_OFF

};
typedef enum cmpp_atpol_e cmpp_atpol_e;

/**
   Policies describing how cmpp should react to attempts to use
   undefined keys.
*/
enum cmpp_unpol_e {
  /* Sentinel. */
  cmpp_unpol_invalid,
  /** Treat undefined keys as NULL/falsy. This is the default. */
  cmpp_unpol_NULL,
  /** Trigger an error for undefined keys. This should probably be the
      default. */
  cmpp_unpol_ERROR,
  /**
     The compile-time default for all cmpp instances.
  */
  cmpp_unpol_DEFAULT = cmpp_unpol_NULL
};
typedef enum cmpp_unpol_e cmpp_unpol_e;

typedef struct cmpp_arg cmpp_arg;
/**
   A single argument for a directive. When a cmpp_d::flags have
   cmpp_d_F_ARGS_V2 set then the part of the input immediately
   following the directive (and on the same line) is parsed into a
   cmpp_args, a container for these.
*/
struct cmpp_arg {
  /** Token type. */
  cmpp_tt ttype;
  /**
     The arg's string value, shorn of any opening/closing quotes or ()
     or {} or []. The args-parsing process guarantees to NUL-terminate
     this. The bytes are typically owned by a cmpp_args object, but
     clients may direct them wherever the need to, so long as the
     bytes are valid longer than this object is.
  */
  unsigned char const * z;
  /**
     The arg's effective length, in bytes, after opening/closing chars
     are stripped. That is, its string form is the range [z,z+n).
  */
  unsigned n;
  /**
     The next argument in the list. It is owned by whatever code set
     it up (typically cmpp_args_parse()).
  */
  cmpp_arg const * next;
};

/**
   Empty-initialized cmpp_arg instance, intended for
   const-copy initialization.
*/
#define cmpp_arg_empty_m {cmpp_TT_None,0,0,0}

/**
   Empty-initialized cmpp_outputer instance, intended for
   non-const-copy initialization.
*/
extern const cmpp_arg cmpp_arg_empty;

typedef struct cmpp_dx cmpp_dx;
typedef struct cmpp_dx_pimpl cmpp_dx_pimpl;
typedef struct cmpp_d cmpp_d;

/**
   Flags for use with cmpp_d::flags.
*/
enum cmpp_d_e {
  /** Sentinel value. */
  cmpp_d_F_none        = 0,
  /**
     cmpp_dx_next() will not parse the directive's arguments. Instead,
     it makes cmpp_dx::arg0 encapsulate the whole line of the
     directive (sans the directive's name) as a single argument. The
     only transformation which is performed is the removal of
     backslashes from backslash-escaped newlines. It is up to the
     directive's callback to handle (or not) the arguments.
  */
  cmpp_d_F_ARGS_RAW    = 0x01,

  /**
     cmpp_dx_next() will parse the directive's arguments.
     cmpp_dx::arg0 will point to the first argument in the list, or
     NULL if there are no arguments.

     If both cmpp_d_F_ARGS_LIST and cmpp_d_F_ARGS_RAW are specified,
     cmpp_d_F_ARGS_LIST will win.
  */
  cmpp_d_F_ARGS_LIST   = 0x02,

  /**
     Indicates that the direction should not be available if the cmpp
     instance is configured with any of the cmpp_ctor_F_SAFEMODE flags.
     All directives when do any of the following are obligated to
     set this flag:

     - Filesystem or network access.
     - Invoking external processes.

     Or anything else which might be deamed "security-relevant".

     When registering a directive which has both opener and closer
     implementations, it is sufficient to set this only on the opener.

     The library imposes this flag in the following places:

     - Registration of a directive with this flag will fail if
       cmpp_is_safemode() is true for that cmpp instance.

     - cmpp_dx_process() will refuse to invoke a directive with this
       flag when cmpp_is_safemode() is true.
  */
  cmpp_d_F_NOT_IN_SAFEMODE  = 0x04,

  /**
     Call-only directives are only usable in [directive ...]  "call"
     contexts. They are not permitted to have a closing directive.
  */
  cmpp_d_F_CALL_ONLY        = 0x08,
  /**
     Indicates that the directive is incapable of working in a [call]
     context and an error should be trigger if it is. _Most_
     directives which have a closing directive should have this
     flag. The exceptions are directives which only conditionally use
     a closing directive, like #query.
  */
  cmpp_d_F_NO_CALL          = 0x10,

  /**
     Mask of the client-usable range for this enum. Values outside of
     this mask are reserved for internal use and will be stripped from
     registrations made with cmpp_d_register().
  */
  cmpp_d_F_MASK             = 0x0000ffff

};

/**
   Callback type for cmpp_d::impl::callback(). cmpp directives are all
   implemented as functions with this signature. Implementations are
   called only by cmpp_dx_process() (and only after cmpp_dx_next() has
   found a preprocessor line), passed the current context object.
   These callbacks are only ever passed directives which were
   specifically registered with them (see cmpp_d_register()).

   The first rule of callback is: to report errors (all of which end
   processing of the current input) call cmpp_dx_err_set(), passing it
   the callback's only argument, then clean up any local resources,
   then return. The library will recognize the error and propagate it.

   dx's memory is only valid for the duration of this call. It must
   not be held on to longer than that. dx->args.arg0 has slightly different
   lifetime: if this callback does _not_ call back in to
   cmpp_dx_next() then dx->args.arg0 and its neighbors will survive until
   this call is completed. Calling cmpp_dx_next(), or any API which
   invokes it, invalidates dx->args.arg0's memory. Thus directives which
   call into that must _copy_ any data they need from their own
   arguments before doing so, as their arguments list will be
   invalidated.
*/
typedef void (*cmpp_dx_f)(cmpp_dx * dx);

/**
   A typedef for generic deallocation routines.
*/
typedef void (*cmpp_finalizer_f)(void *);

/**
   State specific to concrete cmpp_d implementations.

   TODO: move this, except for the state pointer, out of cmpp_d
   so that directives cannot invoke these callbacks directly. Getting
   that to work requires moving the builtin directives into the
   dynamic directives list.
*/
struct cmpp_d_impl {
  /**
     Callback func. If any API other othan cmpp_dx_process() invokes
     this, behavior is undefined.
  */
  cmpp_dx_f callback;

  /**
     For custom directives with a non-NULL this->state, this will be
     called, and passed that object, when the directive is cleaned
     up. For directives with both an opening and a closing tag, this
     destructor is only attached to the opening tag.

     If any API other othan cmpp's internal cleanup routines invoke
     this, behavior is undefined.
  */
  cmpp_finalizer_f dtor;

  /**
     State for the directive's callback. It is accessible in
     cmpp_dx_f() impls via theDx->d->impl.state. For custom
     directives with both an opening and closing directive, this
     same state object gets assigned to both.
  */
  void * state;
};
typedef struct cmpp_d_impl cmpp_d_impl;
#define cmpp_d_impl_empty_m {0,0,0}

/**
   Each c-pp "directive" is modeled by one of these.
*/
struct cmpp_d {

  struct {
    /**
       The directive's name, as it must appear after the directive
       delimiter. Its bytes are assumed to be static or otherwise
       outlive this object.
    */
    const char *z;
    /** Byte length of this->z. We record this to speed up searches.  */
    unsigned n;
  } name;

  /**
     Bitmask of flags from cmpp_d_e plus possibly internal flags.
  */
  cmpp_flag32_t flags;

  /**
     The directive which acts as this directive's closing element
     element, or 0 if it has none.
  */
  cmpp_d const * closer;

  /**
     State specific to concrete implementations.
  */
  cmpp_d_impl impl;
};

/**
   Each instance of the cmpp_dx class (a.k.a. "directive context")
   manages a single input source. It's responsible for the
   tokenization of all input, locating directives, and processing
   ("running") directives.  The directive-specific work happens in
   cmpp_dx_f() implementations, and this class internally manages the
   setup, input traversal, and teardown.

   These objects only exist while cmpp is actively processing
   input. Client code interacts with them only through cmpp_dx_f()
   implementations which the library invokes.

   The process of filtering input to look for directives is to call
   cmpp_dx_next() until it indicates either an error or that a
   directive was found. In the latter case, the cmpp_dx object is
   populated with info about the current directive. cmpp_dx_process()
   will run that directive, but cmpp_dx_f() implementations sometimes
   need to make decisions based on the located directive before doing
   so (and sometimes they need to skip running it).

   If cmpp_dx_next() finds no directive, the end of the input has been
   reached and there is no further output to generate.

   Content encountered before a directive is found is passed on to the
   output stream via cmpp_dx_out_raw() or cmpp_dx_out_expand().
*/
struct cmpp_dx {
  /**
     The cmpp object which owns this context.
  */
  cmpp * const pp;

  /**
     The directive on whose behalf this context is active.
  */
  cmpp_d const *d;

  /**
     Name of the input for error reporting. Typically an input script
     file name, but it need not refer to a file.
  */
  unsigned const char * const sourceName;

  /**
     State related to arguments passed to the current directive.

     It is important to keep in mind that the memory for the members
     of this sub-struct may be modified or reallocated
     (i.e. invalidated) by any APIs which call in to cmpp_dx_next().
     cmpp_dx_f() implementations must take care not to use any of this
     memory after calling into that function, cmpp_dx_consume(), or
     similar. If needed, it must be copied (e.g. using
     cmpp_args_clone() to create a local copy of the parsed
     arguments).
  */
  struct {
    /**
       Starting byte of unparsed arguments. This is for cmpp_d_f()
       implementations which need custom argument parsing.
    */
    unsigned const char * z;

    /**
       The byte length of z.
    */
    cmpp_size_t nz;

    /**
       The parsed arg count for the this->arg0 list.
    */
    unsigned argc;

    /**
       The first parsed arg or NULL. How this is set up is affected by
       cmpp_d::flags.

       This is specifically _NOT_ defined as a sequential array and
       using pointer math to traverse it invokes undefined behavior.

       To traverse the list:

       for( cmpp_arg const *a = dx->args.arg0; a; a=a->next ){
         ...
       }
    */
    cmpp_arg const * arg0;
  } args;

  /**
     Private impl details.
  */
  cmpp_dx_pimpl * const pimpl;
};

/**
   Thin proxy for cmpp_err_set(), replacing only the first argument.
*/
CMPP_EXPORT int cmpp_dx_err_set(cmpp_dx *dx, int rc,
                                char const *zFmt, ...);


/**
   Returns true if dx's current call into the API is the result
   of a function call, else false. Any APIs which recurse into
   input processing will reset this to false, so it needs to be
   evaluated before doing any such work.

   Design note: this flag is actually tied to dx's arguments, which
   get reset by APIs which consume from the input stream.
*/
CMPP_EXPORT bool cmpp_dx_is_call(cmpp_dx * const dx);

/**
   Returns true if dx->pp has error state, else false. If this
   function returns true, cmpp_dx_f() implementations are required to
   stop working, clean up any local resources, and return. Continuing
   to use dx when it's in an error state may exacerbate the problem.
*/
#define cmpp_dx_err_check(DX) (DX)->pp->api->err_has((DX)->pp)

/**
   Scans dx to the next directive line, emitting all input before that
   which is _not_ a directive line to dx->pp's output channel unless
   it's elided due to being inside a block which elides its content
   (e.g. #if).

   Returns 0 if no errors were triggered, else a cmpp_rc_e code.  This
   is a no-op if dx->pp has persistent error state set, and that error
   code is returned.

   If it returns 0 then it sets *pGotOne to true if a directive was
   found and false if not (in which case the end of the input has
   been reached and further calls to this function for the same input
   source will be no-ops).  If it sets *pGotOne to true then it also
   sets up dx's state for use with cmpp_dx_process(), which should
   (normally) then be called.

   ACHTUNG: calling this resets any argument-handling-related state of
   dx. That is important for cmpp_dx_f() implemenations, which _must
   not_ hold copies of any pointers from dx->args.arg0 or dx->args.z
   beyond a call to this function. Any state they need must be
   evaluated, potentially copied, before calling this function().
*/
CMPP_EXPORT int cmpp_dx_next(cmpp_dx * dx, bool * pGotOne);

/**
   This is only legal to call immediately after a successful call to
   cmpp_dx_next(). It requires that cmpp_dx_next() has just located
   the next directive. This function runs that directive.  Returns 0
   on success and all that.

   Design note: directive-Search and directive-process are two
   distinctly separate steps because directives which have both
   open/closing tags frequently discard the closing directive without
   running it (it exists to tell the directive how far to read). Those
   closing directives exist independently, though, and will trigger
   errors when encountered outside of the context of their opening
   directive tag (e.g. an "#/if" without an "#if").
*/
CMPP_EXPORT int cmpp_dx_process(cmpp_dx * dx);

/**
   A bitmask of flags for use with cmpp_dx_consume()
*/
enum cmpp_dx_consume_e {
  /**
     Tells cmpp_dx_consume() to process any directives it encounters
     which are not in the specified set of closing directives. Its
     default is to fail if another directive is seen.
  */
  cmpp_dx_consume_F_PROCESS_OTHER_D = 0x01,
  /**
     Tells cmpp_dx_consume() that non-directive content encountered
     before the designated closing directive(s) must use an at-policy
     of cmpp_atpol_OFF. That is: the output target of that function will
     get the raw, unfiltered content. This is for cases where the
     consumer will later re-emit that content, delaying @token@
     parsing until a later step (e.g. #query does this).

     This may misinteract in unpredictable ways when used with
     cmpp_dx_consume_F_PROCESS_OTHER_D. Please report them as bugs.
  */
  cmpp_dx_consume_F_RAW           = 0x02
};

/**
   A helper for cmpp_dx_f() implementations which read in their
   blocked-off content instead of passing it through the normal output
   channel.  e.g. `#define x <<` stores that content in a define named
   "x".

   This function runs a cmpp_dx_next() loop which does the following:

   If the given output channel is not NULL then it first replaces the
   output channel with the given one, such that all output which would
   normally be produced will be sent there until this function
   returns, at which point the output channel is restored. If the
   given channel is NULL then output is not captured - it instead goes
   dx's current output channel.

   dClosers must be a list of legal closing tags nClosers entries
   long. Typically this is the single closing directive/tag of the
   current directive, available to the opening directive's cmpp_dx_f()
   impl via dx->d->closer. Some special cases require multiple
   candidates, however.

   The flags argument may be 0 or a bitmask of values from the
   cmpp_dx_consume_e enum.

   If flags does not have the cmpp_dx_consume_F_PROCESS_OTHER_D bit set
   then this function requires that the next directive in the input be
   one specified by dClosers.  If the next directive is not one of
   those, it will fail with code CMPP_RC_SYNTAX.

   If flags has the cmpp_dx_consume_F_PROCESS_OTHER_D bit set then it
   will continue to search for and process directives until the
   dCloser directive is found. Calling into other directives will
   invalidate certain state that a cmpp_dx_f() has access to - see
   below for details. If dCloser is not found before EOF, a
   CMPP_RC_SYNTAX error is triggered.

   Once one of dCloser is found, this function returns with dx->d
   referring to the that directive.  In practice, the caller should
   _not_ call cmpp_dx_process() at that point - the closing directive
   is typically a no-op placeholder which exists only to mark the end
   of the block.  If the closer has work to do, however, the caller of
   this function should call cmpp_dx_process() at that point.

   On success it returns 0, the input stream will have been consumed
   between the directive dx and its closing tag, and dx->d will point
   to the new directive.  If os is not NULL then os will have been
   sent any content.

   On error, processing of the directive must end immediately,
   returning from the cmpp_dx_f() impl after cleaning up any local
   resources.

   ACHTUNG: since this invokes cmpp_dx_next(), it invalidates
   dx->args.arg0. Its dx->d is also replaced but the previous value
   remains valid until the cmpp instance is cleaned up.

   Example from the context of a cmpp_dx_f() implementation

   ```
   // "dx" is the cmpp_dx arg to this function
   cmpp_outputer oss = cmpp_outputer_b;
   cmpp_b os = cmpp_b_empty;
   oss.state = &os;
   if( 0==cmpp_dx_consume(dx, &oss, dx->d->closer, 0) ){
     cmpp_b_chomp( &os );
     ... maybe modify the buffer or decorate the output in some way...
     cmpp_dx_out_raw(dx, os.z, os.n);
   }
   cmpp_b_clear(&os);
   ```

   Design issue: this API does not currently have a way to handle
   directives which have multiple potential waypoints/endpoints, in
   the way that an #if may optionally have an #elif or #else before
   the #/if. Such processing has to be done in the directive's
   impl.
*/
CMPP_EXPORT int cmpp_dx_consume(cmpp_dx * dx, cmpp_outputer * os,
                                cmpp_d const *const * dClosers,
                                unsigned nClosers,
                                cmpp_flag32_t flags);

/**
   Equivalent to cmpp_dx_consume(), capturing to the given buffer
   instead of a cmpp_outputer object.
*/
CMPP_EXPORT int cmpp_dx_consume_b(cmpp_dx * dx, cmpp_b * b,
                                  cmpp_d const * const * dClosers,
                                  unsigned nClosers,
                                  cmpp_flag32_t flags);

/**
   If arg is not NULL, cleans up any resources owned by
   arg but does not free arg.

   As of this writing, they own none and some code still requires
   that. That is Olde Thynking, though.
*/
CMPP_EXPORT void cmpp_arg_cleanup(cmpp_arg *arg);

/**
   If arg is not NULL resets arg to be re-used. arg must have
   initially been cleanly initialized by copying cmpp_arg_empty (or
   equivalent, i.e. zeroing it out).
*/
CMPP_EXPORT void cmpp_arg_reuse(cmpp_arg *arg);

/**
   This is the core argument-parsing function used by the library's
   provided directives. Its is available in the public API as a
   convenience for custom cmpp_dx_f() implementations, but custom
   implementations are not required to make use of it.

   Populates a cmpp_arg object by parsing the next token from its
   input source.

   Expects *pzIn to point to the start of input for parsing arguments
   and zInEnd to be the logical EOF of that range. This function
   populates pOut with the info of the parse. Returns 0 on success,
   non-0 (and updates pp's error state) on error.

   Output (the parsed token) is written to *pzOut. zOutEnd must be the
   logical EOF of *pzOut. *pzOut needs to be, at most,
   (zInEnd-*pzIn)+1 bytes long. This function range checks the output
   and will not write to or past zOutEnd, but that will trigger a
   CMPP_RC_RANGE error.

   On success, *pzIn will be set to 1 byte after the last one parsed
   for pOut and *pzOut will be set to one byte after the final output
   (NUL-terminated). pOut->z will point to the start of *pzOut and
   pOut->n will be set to the byte-length of pOut->z.

   When the end of the input is reached, this function returns 0
   and sets pOut->ttype to cmpp_TT_EOF.

   General tokenization rules:

   Tokens come in the following flavors:

   - Quoted strings: single- or double-quoted. cmpp_arg::ttype:
     cmpp_TT_String.

   - "At-strings": @"..." and @'...'. cmpp_arg::ttype value:
     cmpp_TT_StringAt.

   - Decimal integers with an optional sign. cmpp_arg::ttype value:
     cmpp_TT_Int.

   - Groups: (...), {...}, and [...]. cmpp_arg::ttype values:
     cmpp_TT_GroupParen, cmpp_TT_GroupSquiggly, and
     cmpp_TT_GroupBrace. These types do not automatically get parsed
     recursively. To recurse into one of these, pass cmpp_arg_parse()
     the grouping argument's bytes as the input range.

   - Word: anything which doesn't look like one of these above.  Token
     type IDs: cmpp_TT_Word. These are most often interpreted as
     #define keys but cmpp_dx_f() implementations sometimes treat
     them as literal values.

   - A small subset of words and operator-like tokens, e.g. '=' and
     '!=', get a very specific ttype, e.g. cmpp_TT_OpNeq, but these
     can generally be treated as strings.

   - Outside of strings and groups, spaces, tabs, carriage-returns,
     and newlines are skipped.

   These are explained in more detail in the user's manual
   (a.k.a. README.md).

   There are many other token types, mostly used internally.

   This function supports _no_ backslash escape sequences in
   tokens. All backslashes, with the obligatory exception of those
   which make up backslash-escaped newlines in the input stream, are
   retained as-is in all token types. That means, for example, that
   strings may not contain their own quote character.

   As an example of where this function is useful: cmpp_dx_f()
   implementations which need to selectively parse a subset of the
   directive's arguments can use this.  As input, their dx argument's
   args.z and args.nz members delineate the current directive line's
   arguments. See c-pp.c:cmpp_dx_f_pipe() for an example.
*/
CMPP_EXPORT int cmpp_arg_parse(cmpp_dx * dx,
                               cmpp_arg *pOut,
                               unsigned char const **pzIn,
                               unsigned char const *zInEnd,
                               unsigned char ** pzOut,
                               unsigned char const * zOutEnd);

/**
   True if (cmpp_arg const *)ARG's contents match the string literal
   STR, else false.
*/
#define cmpp_arg_equals(ARG,STR) \
  (sizeof(STR)-1==(ARG)->n && 0==memcmp(STR,(ARG)->z,sizeof(STR)-1))

/**
   True if (cmpp_arg const *)ARG's contents match the string literal
   STR or ("-" STR), else false. The intent is that "-flag" be passed
   here to tolerantly accept either "-flag" or "--flag".
*/
#define cmpp_arg_isflag(ARG,STR) \
  cmpp_arg_equals(ARG,STR) || cmpp_arg_equals(ARG, "-" STR)

/**
   Creates a copy of arg->z. If allocation fails then pp's persistent
   error code is set to CMPP_RC_OOM. If pp's error code is not 0 when
   this is called then this is a no-op and returns NULL. In other
   words, if this function returns NULL, pp's error state was either
   already set when this was called or it was set because allocation
   failed.

   Ownership of the returned memory is transferred to the caller, who
   must eventually free it using cmpp_mfree().
*/
CMPP_EXPORT char * cmpp_arg_strdup(cmpp *pp, cmpp_arg const *arg);

/**
   Flag bitmasks for use with cmpp_arg_to_b(). With my apologies
   for the long names (but consistency calls for them).
*/
enum cmpp_arg_to_b_e {
  /**
     Specifies that the argument's string value should be used as-is,
     rather than expanding it (if the arg's ttype would normally cause
     it to be expanded).
  */
  cmpp_arg_to_b_F_FORCE_STRING = 0x01,

  /**
     Tells cmpp_arg_to_b() to not expand arguments with type
     cmpp_TT_Word, which it normally treats as define keys. It instead
     treats these as strings.
   */
  cmpp_arg_to_b_F_NO_DEFINES = 0x02,

  /**
     If set, arguments with a ttype of cmpp_TT_GroupBrace will be
     "called" by passing them to cmpp_call_arg(). The space-trimmed
     result of the call becomes the output of the cmpp_arg_to_b()
     call.

     FIXME: make this opt-out instead of opt-in. We end up _almost_
     always wanting this.
  */
  cmpp_arg_to_b_F_BRACE_CALL = 0x04,

  /**
     Explicitly disable [call] expansion even if
     cmpp_arg_to_b_F_BRACE_CALL is set in the flags.
  */
  cmpp_arg_to_b_F_NO_BRACE_CALL = 0x08

  /**
     TODO? cmpp_arg_to_b_F_UNESCAPE
  */
};

/**
   Appends some form of arg to the given buffer.

   arg->ttype values of cmpp_TT_Word (define keys) and
   cmpp_TT_StringAt cause the value to be expanded appropriately (the
   latter according to dx->pp's current at-policy). Others get emitted
   as-is.

   The flags argument influences the expansion decisions, as documented
   in the cmpp_arg_to_b_e enum.

   Returns 0 on success and all that.

   See: cmpp_atpol_get(), cmpp_atpol_set()

   Reminder to self: though this function may, via script-side
   function call resolution, recurse into the library, any such
   recursion gets its own cmpp_dx instance. In this context that's
   significant because it means this call won't invalidate arg's
   memory like cmpp_dx_consume() or cmpp_dx_next() can (depending on
   where args came from - typically it's owned by dx but
   cmpp_args_clone() exists solely to work around such potential
   invalidation).
*/
CMPP_EXPORT int cmpp_arg_to_b(cmpp_dx * dx, cmpp_arg const *arg,
                              cmpp_b * os, cmpp_flag32_t flags);

/**
   Flags for use with cmpp_call_str() and friends.
*/
enum cmpp_call_e {
  /** Do not trim a newline from the result. */
  cmpp_call_F_NO_TRIM    = 0x01,
  /** Trim all leading and trailing space and newlines
      from the result. */
  cmpp_call_F_TRIM_ALL   = 0x02
};

/**
   This assumes that arg->z holds a "callable" directive
   string in the form:

   directiveName ...args

   This function composes a new cmpp input source from that line
   (prefixed with dx's current directive prefix if it's not already
   got one), processes it with cmpp_process_string(), redirecting the
   output to dest (which gets appended to, so be sure to
   cmpp_b_reuse() it if needed before calling this).

   To simplify common expected usage, by default the output is trimmed
   of a single newline. The flags argument, 0 or a bitmask of values
   from the cmpp_call_e enum, can be used to modify that behavior.

   This is the basis of "function calls" in cmpp.

   Returns 0 on success.
*/
int cmpp_call_str(cmpp *dx,
                  unsigned char const * z,
                  cmpp_ssize_t n,
                  cmpp_b * dest,
                  cmpp_flag32_t flags);

/**
   Convert an errno value to a cmpp_rc_e approximation, defaulting to
   dflt if no known match is found. This is intended for use by
   cmpp_dx_f implementations which use errno-using APIs.
*/
CMPP_EXPORT int cmpp_errno_rc(int errNo, int dflt);

/**
   Configuration object for use with cmpp_d_register().
*/
struct cmpp_d_reg {
  /**
     The name of the directive as it will be used in
     input scripts, e.g. "mydirective". It will be copied by
     cmpp_d_register().
  */
  char const *name;
  /**
     A combination of bits from the cmpp_d_e enum.

     These flags are currently applied only to this->opener.
     this->closer, because of how it's typically used, assumes
  */
  struct {
    /**
       Callback for the directive's opening tag.
    */
    cmpp_dx_f f;
    /**
       Flags from cmpp_d_e. Typically one of cmpp_d_F_ARGS_LIST or
       cmpp_d_F_ARGS_RAW.
    */
    cmpp_flag32_t flags;
  } opener;
  struct {
    /**
       Callback for the directive's closing tag, if any.

       This is only relevant for directives which have both an open and
       a closing tag (even if that closing tag is only needed in some
       contexts, e.g. "#define X <<" (with a closer) vs "#define X Y"
       (without)).  See cmpp_dx_f_dangling_closer() for a default
       implementation which triggers an error if it's seen in the input
       and not consumed by its counterpart opening directive. That
       implementation has proven useful for #define, #pipe, and friends.

       Design notes: it's as yet unclear how to model, in the public
       interface, directives which have a whole family of cooperating
       directives, namely #if/#elif/#else.
    */
    cmpp_dx_f f;
    /**
       Flags from cmpp_d_e. For closers this can typically be
       left at 0.
    */
    cmpp_flag32_t flags;
  } closer;
  /**
     If not NULL then it is assigned to the directive's opener part
     and will be called by the library in either of the following
     cases:

     - When the custom directive is cleaned up.

     - If cmpp_d_register() fails (returns non-0), regardless of how
       it fails.

     It is passed this->state.
  */
  cmpp_finalizer_f dtor;
  /**
     Implementation state for the callbacks.
  */
  void * state;
};
typedef struct cmpp_d_reg cmpp_d_reg;
/**
   Empty-initialized cmpp_d_reg instance, intended for const-copy
   initialization.
*/
#define cmpp_d_reg_empty_m {0,{0,0},{0,0},0,0}
/**
   Empty-initialized instance, intended for non-const-copy
   initialization.
*/
//extern const cmpp_d_reg cmpp_d_reg_empty;

/**
   Registers a new directive, or a pair of opening/closing directives,
   with pp.

   The semantics of r's members are documented in the cmpp_d_reg
   class. r->name and r->opener.f are required. The remainder may be
   0/NULL. Its members are copied - r need not live longer than this
   call.

   When the new directive is seen in a script, r->opener.f() will be
   called. If the closing directive (if any) is seen in a script,
   r->closer.f() is called. In both cases, the callback
   implementation can get access to the r->state object via
   cmdd_dx::d::impl::state (a.k.a dx->d->impl.state).

   If r->closer.f is not NULL then the closing directive will be named
   "/${zName}". (Design note: it is thought that forcing a common
   end-directive syntax will lead to fewer issues than allowing
   free-form closing tag names, e.g. fewer chances of a name collision
   or not quite remembering the spelling of a given closing tag
   (#endef vs #enddefine vs #/define).)

   Returns 0 on success and updates pp's error state on error. Similarly,
   this is a no-op if pp has an error code when this is called, in which
   case it returns that result code without other side-effects.

   On success, if pOut is not NULL then it is set to the directive
   pointer, memory owned by pp until it cleans up its directives. This
   is the only place in the API a non-const pointer to a directive can
   be found, and it is provided only for very specific use-cases where
   a directive needs to be manipulated (carefully) after
   registration[^post-reg-manipulation]. If this function also
   registered a closing directive, it is available as (*pOut)->closer.
   pOut should normally be NULL.

   Failure modes include:

   - Returns CMPP_RC_RANGE if zName is not legal for use as a
     directive name. See cmpp_is_legal_key().

   - Returns CMPP_RC_OOM on an allocation error.

   Errors from this function are recoverable (see cmpp_err_set()). A
   failed registration, even one which doesn't fail until the
   registration of the closing element, will leave pp in a
   well-defined state (with neither of r's directives being
   registered).

   [^post-reg-manipulation]: The one known use case if the #if family
   of directives, all of which use the same #/if closing
   directive. The public registration API does not account for sharing
   of closers that way, and whether it _should_ is still TBD. The
   workaround, for this case, is to get the directives as they're
   registered and point the cmpp_d::closer of each of #if, #elif, and
   #else to #/if.
*/
CMPP_EXPORT int cmpp_d_register(cmpp * pp, cmpp_d_reg const * r,
                                cmpp_d **pOut);

/**
   A cmpp_dx_f() impl which is intended to be used as a callback for
   directive closing tags for directives in which the opening tag's
   implementation consumes the input up to the closing tag.  This impl
   triggers an error if called, indicating that the directive closing
   was seen in the input without its accompanying directive opening.
*/
CMPP_EXPORT void cmpp_dx_f_dangling_closer(cmpp_dx *dx);

/**
   Writes the first n bytes of z to dx->pp's current output channel
   without performing any @token@ parsing.

   Returns dx->pp's persistent error code (0 on success) and sets that
   code to non-0 on error. This is a no-op if dx->pp has a non-0 error
   state, returning that code.

   See: cmpp_dx_out_expand()
*/
CMPP_EXPORT int cmpp_dx_out_raw(cmpp_dx * dx, void const *z,
                                cmpp_size_t n);

/**
   Sends [zFrom,zFrom+n) to pOut, performing @token@ expansion if the
   given policy says to (else it passes the content through as-is, as
   per cmpp_dx_out_raw()). A policy of cmpp_atpol_CURRENT uses dx->pp's
   current policy. A policy of cmpp_atpol_OFF behaves exactly like
   cmpp_dx_out_raw().

   Returns dx->pp's persistent error code (0 on success) and sets that
   code to non-0 on error. This is a no-op if dx->pp has a non-0 error
   state, returning that code.

   If pOut is NULL then dx->pp's default channel is used, with the
   caveat that atPolicy's only legal value in that case is
   cmpp_atpol_CURRENT. (The internals do not allow the at-policy to be
   overridden for that particular output channel, to avoid accidental
   filtering when it's not enabled. They do not impose that
   restriction for other output channels, which are frequently used
   for filtering intermediary results.)

   See: cmpp_dx_out_raw()

   Notes regarding how this is used internally:

   - This function currently specifically does nothing when invoked in
   skip-mode[^1]. Hypothetically it cannot ever be called in skip-mode
   except when evaluating #elif expressions (previous #if/#elifs
   having failed and put us in skip-mode), where it's expanding
   expression operands. That part currently (as of 2025-10-21) uses
   dx->pp's current policy, and it's not clear whether that is
   sufficient or whether we need to force it to expand (and which
   policy to use when doing so). We could possibly get away with
   always using cmpp_atpol_ERROR for purposes of evaluating at-string
   expression operands.

   [^1]: Skip-mode is the internal mechanism which keeps directives
   from running, and content from being emitted, within a falsy branch
   of an #if/#elif block.  Only flow-control directives are ever run
   when skip-mode is active, and client-provided directives cannot
   easily provide flow-control support. Ergo, much of this paragraph
   is not relevant for client-level code, but it is for this library's
   own use of this function.
*/
CMPP_EXPORT int cmpp_dx_out_expand(cmpp_dx const * dx,
                                   cmpp_outputer * pOut,
                                   unsigned char const * zFrom,
                                   cmpp_size_t n,
                                   cmpp_atpol_e policy);

/**
   This creates a formatted string using sqlite3_mprintf() and emits it
   using cmpp_dx_out_raw(). Returns CMPP_RC_OOM if allocation of the
   string fails, else it returns whatever cmpp_dx_out_raw() returns.

   This is a no-op if dx->pp is in an error state, returning
   that code.
*/
CMPP_EXPORT int cmpp_dx_outf(cmpp_dx *dx, char const *zFmt, ...);

/**
   Convenience form of cmpp_delimiter_get() which returns the
   delimiter which was active at the time when the currently-running
   cmpp_dx_f() was called. This memory may be invalidated by any calls
   into cmpp_dx_process() or cmpp_delimiter_set(), so a copy of this
   pointer must not be retained past such a point.

   This function is primarily intended for use in generating debug and
   error messages.

   If the delimiter stack is empty, this function returns NULL.
*/
CMPP_EXPORT char const * cmpp_dx_delim(cmpp_dx const *dx);

/**
   Borrows a buffer from pp's buffer recycling pool, allocating one if
   needed. It returns NULL only on allocation error, in which case it
   updates pp's error state.

   This transfers ownership of the buffer to the caller, who is
   obligated to eventually do ONE of the following:

   - Pass it to cmpp_b_return() with the same dx argument.

   - Pass it to cmpp_b_clear() then cmpp_mfree().

   The purpose of this function is a memory reuse optimization.  Most
   directives, and many internals, need to use buffers for something
   or other and this gives them a way to reuse buffers.

   Potential TODO: How this pool optimizes (or not) buffer allotment
   is an internal detail. Maybe add an argument which provides a hint
   about the buffer usage. e.g. argument-conversion buffers are
   normally small but block content buffers can be arbitrarily large.
*/
CMPP_EXPORT cmpp_b * cmpp_b_borrow(cmpp *dx);

/**
   Returns a buffer borrowed from cmpp_b_borrow(), transferring
   ownership back to pp. Passing a non-NULL b which was not returned
   by cmpp_b_borrow() invoked undefined behavior (possibly delayed
   until the list is cleaned up). To simplify usage, b may be NULL.

   After calling this, b must be considered "freed" - it must not be
   used again. This function is free (as it were) to immediately free
   the object's memory instead of recycling it.
*/
CMPP_EXPORT void cmpp_b_return(cmpp *dx, cmpp_b *b);

/**
   If NUL-terminated z matches one of the strings listed below, its
   corresponding cmpp_atpol_e entry is returned, else
   cmpp_atpol_invalid is returned.

   If pp is not NULL then (A) this also sets its current at-policy and
   (B) it recognizes an additional string (see below).  In this case,
   if z is not a valid string then pp's persistent error state is set.

   Its accepted values each correspond to a like-named policy value:

  - "off" (the default): no processing of `@` is performed.

  - "error": fail if an undefined `X` is referenced in @token@
    parsing.

  - "retain": emit any unresolved `@X@` tokens as-is to the output
    stream. i.e. `@X@` renders as `@X@`.

    - "elide": omit unresolved `@X@` from the output, as if their values
    were empty. i.e. `@X@` renders as an empty string, i.e. is not
    emitted at all.

  - "current": if pp!=NULL then it returns the current policy, else
    this string resolves to cmpp_atpol_invalid.
*/
CMPP_EXPORT cmpp_atpol_e cmpp_atpol_from_str(cmpp * pp, char const *z);

/**
   Returns pp's current at-token policy.
*/
CMPP_EXPORT cmpp_atpol_e cmpp_atpol_get(cmpp const * const pp);

/**
   Sets pp's current at-token policy. Returns 0 if pol is valid, else
   it updates pp's error state and returns CMPP_RC_RANGE. This is a
   no-op if pp has error state, returning that code instead.

   The policy cmpp_atpol_CURRENT is a no-op, permitted to simplify
   certain client-side usage.
*/
CMPP_EXPORT int cmpp_atpol_set(cmpp * const pp, cmpp_atpol_e pol);

/**
   Pushes pol as the current at-policy. Returns 0 on success and
   non-zero on error (bad pol value or allocation error).  If this
   returns 0 then the caller is obligated to eventually call
   cmpp_atpol_pop() one time. If it returns non-0 then they _must not_
   call that function.
*/
CMPP_EXPORT int cmpp_atpol_push(cmpp *pp, cmpp_atpol_e pol);

/**
   Must be called one time for each successful call to
   cmpp_atpol_push(). It restores the at-policy to the value it
   has when cmpp_atpol_push() was last called.

   If called when no cmpp_delimiter_push() is active then debug builds
   will fail an assert(), else pp's error state is updated if it has
   none already.
*/
CMPP_EXPORT void cmpp_atpol_pop(cmpp *pp);

/**
   The cmpp_unpol_e counterpart of cmpp_atpol_from_str(). It
   behaves identically, just for a different policy group with
   different names.

   Its accepted values are: "null" and "error". The value "current" is
   only legal if pp!=NULL, else it resolves to cmpp_unpol_invalid.
*/
CMPP_EXPORT cmpp_unpol_e cmpp_unpol_from_str(cmpp * pp, char const *z);

/**
   Returns pp's current policy regarding use of undefined define keys.
*/
CMPP_EXPORT cmpp_unpol_e cmpp_unpol_get(cmpp const * const pp);

/**
   Sets pp's current policy regarding use of undefined define keys.
   Returns 0 if pol is valid, else it updates pp's error state and
   returns CMPP_RC_RANGE.
*/
CMPP_EXPORT int cmpp_unpol_set(cmpp * const pp, cmpp_unpol_e pol);

/**
   The undefined-policy counterpart of cmpp_atpol_push().
*/
CMPP_EXPORT int cmpp_unpol_push(cmpp *pp, cmpp_unpol_e pol);

/**
   The undefined-policy counterpart of cmpp_atpol_pop().
*/
CMPP_EXPORT void cmpp_unpol_pop(cmpp *pp);

/**
   The at-token counterpart of cmpp_delimiter_get(). This sets *zOpen
   (if zOpen is not NULL) to the opening delimiter and *zClose (if
   zClose is not NULL) to the closing delimiter.  The memory is owned
   by pp and may be invalidated by any calls to cmpp_atdelim_set(),
   cmpp_atdelim_push(), or any APIs which consume input. Each string
   is NUL-terminated and must be copied by the caller if they need
   these strings past a point where they might be invalidated.

   If called when the the delimiter stack is empty, debug builds with
   fail an assert() and non-debug builds will behave as if the stack
   contains the compile-time default delimiters.
*/
CMPP_EXPORT void cmpp_atdelim_get(cmpp const * pp,
                                  char const **zOpen,
                                  char const **zClose);
/**
   The `@token@`-delimiter counterpart of cmpp_delimeter_set().

   This sets the delimiter for `@token@` content to the given opening
   and closing strings (which the library makes a copy of).  If zOpen
   is NULL then the compile-time default is assumed. If zClose is NULL
   then zOpen is assumed.

   Returns 0 on success. Returns non-0 if called when the delimiter
   stack is empty, if it cannot copy the string or zDelim is deemed
   unsuitable for use as a delimiter.

   In debug builds this will trigger an assert if no `@token@`
   delimiter has been set, but pp starts with one level in place, so
   it is safe to call without having made an explicit
   cmpp_atdelim_push() unless cmpp_atdelim_pop() has been misused.
*/
CMPP_EXPORT int cmpp_atdelim_set(cmpp * pp,
                                 char const *zOpen,
                                 char const *zClose);

/**
   The `@token@`-delimiter counterpart of cmpp_delimeter_push().

   See cmpp_atdelim_set() for the semantics of the arguments.
*/
CMPP_EXPORT int cmpp_atdelim_push(cmpp *pp,
                                  char const *zOpen,
                                  char const *zClose);

/**
   The @token@-delimiter counterpart of cmpp_delimiter_pop().
*/
CMPP_EXPORT int cmpp_atdelim_pop(cmpp *pp);

/**
   Searches the given path (zPath), split on the given path separator
   (pathSep), for the given file (zBaseName), optionally with the
   given file extension (zExt).

   If zBaseName or zBaseName+zExt are found as-is, without any
   search path prefix, that will be the result, else the result
   is either zBaseName or zBaseName+zExt prefixed by one of the
   search directories.

   On success, returns a new string, transfering ownership to the
   caller (who must eventually pass it to cmpp_mfree() to deallocate).

   If no match is found, or on error, returns NULL. On a genuine
   error, pp's error state is updated and the error is unlikely to be
   recoverable (see cmpp_err_set()).

   This function is a no-op if called when pp's error state is set,
   returning NULL.

   Results are undefined (in the sense of whether it will work or not,
   as opposed to whether it will crash or not) if pathSep is a control
   character.

   Design note: this is implemented as a Common Table Expression
   query.
*/
CMPP_EXPORT char * cmpp_path_search(cmpp *pp,
                                    char const *zPath,
                                    char pathSep,
                                    char const *zBaseName,
                                    char const *zExt);

/**
   Scans [*zPos,zEnd) for the next chSep character. Sets *zPos to one
   after the last consumed byte, so its result includes the separator
   character unless EOF is hit before then. If pCounter is not NULL
   then it does ++*pCounter when finding chSep.

   Returns true if any input is consumed, else false (EOF). When it
   returns false, *zPos will have the same value it had when this was
   called. If it returns true, *zPos will be greater than it was
   before this call and <= zEnd.

   Usage:

   ```
   unsigned char const * zBegin = ...;
   unsigned char const * const zEnd = zBegin + strlen(zBegin);
   unsigned char const * zEol = zBegin;
   cmpp_size_t nLn = 0;
   while( cmpp_next_chunk(&zEol, zEnd, '\n', &nLn) ){
     ...
   }
   ```
*/
CMPP_EXPORT
bool cmpp_next_chunk(unsigned char const **zPos,
                     unsigned char const *zEnd,
                     unsigned char chSep,
                     cmpp_size_t *pCounter);

/**
   Flags and constants related to the cmpp_args type.
*/
enum cmpp_args_e {
  /**
    cmpp_args_parse() flag which tells cmpp_args_parse() not to
    dive into (...) group tokens. It insteads leaves them to be parsed
    (or not) by downstream code. The only reason to parse them in
    advance is to catch syntax errors sooner rather than later.
  */
  cmpp_args_F_NO_PARENS    = 0x01
};

/**
   An internal detail of cmpp_args.
*/
typedef struct cmpp_args_pimpl cmpp_args_pimpl;

/**
   A container for parsing a line's worth of cmpp_arg
   objects.

   Instances MUST be cleanly initialized by bitwise-copying either
   cmpp_args_empty or (depending on the context) cmpp_args_empty_m.

   Instances MUST eventually be passed to cmpp_args_cleanup().

   Design notes: this class is provided to the public API as a
   convenience, not as a core/required component. It offers one of
   many possible solutions for dealing with argument lists and is not
   the End All/Be All of solutions. I didn't _really_ want to expose
   this class in the public API at all but I also want client-side
   directives to have the _option_ to to do some of the things
   currently builtin directives can do which are (as of this writing)
   unavailable in the public API, e.g. evaluate expressions (in that
   limited form which this library supports). A stepping stone to
   doing so is making this class public.
*/
struct cmpp_args {
  /**
     Number of parsed args. In the context of a cmpp_dx_f(), argument
     lists do not include their directive's name as an argument.
  */
  unsigned argc;

  /**
     The list of args. This is very specifically NOT an array (or at
     least not one which client code can rely on to behave
     sensibly). Some internal APIs adjust a cmpp_args's arg list,
     re-linking the entries via cmpp_arg::next and making array-style
     traversal a foot-gun.

     To loop over them:

     for( cmpp_arg const * arg = args->arg0; arg; arg = arg->next ){...}

     This really ought to be const but it currenty cannot be for
     internal reasons. Client code really should not modify these
     objects, though. Doing so invokes undefined behavior.

     For directives with the cmpp_d_F_ARGS_RAW flag, this member will,
     after a successful call to cmpp_dx_next(), point to a single
     argument which holds the directive's entire argument string,
     stripped of leading spaces.
  */
  cmpp_arg * arg0;

  /**
     Internal implementation details. This is initialized via
     cmpp_args_parse() and freed via cmpp_args_cleanup().
  */
  cmpp_args_pimpl * pimpl;
};
typedef struct cmpp_args cmpp_args;

/**
   Empty-initialized cmpp_args instance, intended for const-copy
   initialization.
*/
#define cmpp_args_empty_m { \
  .argc = 0,                \
  .arg0 = 0,                \
  .pimpl = 0                \
}

/**
   Empty-initialized instance, intended for non-const-copy
   initialization.
*/
extern const cmpp_args cmpp_args_empty;

/**
   Parses the range [zInBegin,zInBegin+nIn) into a list of cmpp_arg
   objects by iteratively processing that range with cmpp_arg_parse().
   If nIn is negative, strlen() is used to calculate it.

   Requires that arg be a cleanly-initialized instance (via
   bitwise-copying cmpp_args_empty) or that it have been successfully
   used with this function before. Behavior is undefined if pArgs was
   not properly initialized.

   The 3rd argument is an optional bitmask of flags from the
   cmpp_args_e enum.

   On success it populates arg, noting that an empty list is valid.
   The memory pointed to by the arguments made available via
   arg->arg0 is all owned by arg and will be invalidated by either a
   subsequent call to this function (the memory will be overwritten or
   reallocated) or cmpp_args_cleanup() (the memory will be freed).

   On error, returns non-0 and updates pp's error state with info
   about the problem.
*/
CMPP_EXPORT int cmpp_args_parse(cmpp_dx * dx,
                                cmpp_args * pOut,
                                unsigned char const * zInBegin,
                                cmpp_ssize_t nIn,
                                cmpp_flag32_t flags);

/**
   Frees any resources owned by its argument but does not free the
   argument (which is typically stack-allocated). After calling this,
   the object may again be used with cmpp_args_parse() (in which case
   it eventually needs to be passed to this again).

   This is a harmless no-op if `a` is already cleaned up but `a` must
   not be NULL.
*/
CMPP_EXPORT void cmpp_args_cleanup(cmpp_args *a);

/**
   A wrapper around cmpp_args_parse() which uses dx->args.z as an
   input source. This is sometimes convenient in cmpp_dx_f()
   implementations which use cmpp_dx_next(), or similar, to read and
   process custom directives, as doing so invalidates dx->arg's
   memory.

   On success, returns 0 and populates args. On error, returns non-0
   and sets dx->pp's error state.

   cmpp_dx_args_clone() does essentially the same thing, but is more
   efficient when dx->args.arg0 is is already parsed.
*/
CMPP_EXPORT int cmpp_dx_args_parse(cmpp_dx *dx, cmpp_args *args);

/**
   Populates pOut, replacing any current content, with a copy of each
   arg in dx->args.arg0 (traversing arg0->next).

   *pOut MUST be cleanly initialized via copying cmpp_args_empty or it
   must have previously been used with either cmpp_args_parse() (which
   has the same initialization requirement) or this function has
   undefined results.

   On success, pOut->argc and pOut->arg0 will refer to pOut's copy
   of the arguments.

   Copying of arguments is necessary in cmpp_dx_f() implementations
   which need to hold on to arguments for use _after_ calling
   cmpp_dx_next() or any API which calls that (which most directives
   don't do). See that function for why.
*/
CMPP_EXPORT int cmpp_dx_args_clone(cmpp_dx * dx, cmpp_args *pOut);

/** Flags for cmpp_popen(). */
enum cmpp_popen_e {
  /**
     Use execl[p](CMD, CMD,0) instead of
     execl[p]("/bin/sh","-c",CMD,0).
  */
  cmpp_popen_F_DIRECT = 0x01,
  /** Use execlp() or execvp() instead of execl() or execv(). */
  cmpp_popen_F_PATH   = 0x02
};

/**
   Result state for cmpp_popen() and friends.
*/
struct cmpp_popen_t {
  /**
     The child process ID.
   */
  int childPid;
  /**
     The child process's stdout.
  */
  int fdFromChild;
  /**
     If not NULL, cmpp_popen() will set *fpToChild to a FILE handle
     mapped to the child process's stdin. If it is NULL, the child
     process's stdin will be closed instead.
  */
  cmpp_FILE **fpToChild;
};
typedef struct cmpp_popen_t cmpp_popen_t;
/**
   Empty-initialized cmpp_popen_t instance, intended for const-copy
   initialization.
*/
#define cmpp_popen_t_empty_m {-1,-1,0}
/**
   Empty-initialized instance, intended for non-const-copy
   initialization.
*/
extern const cmpp_popen_t cmpp_popen_t_empty;

/**
   Uses fork()/exec() to run a command in a separate process and open
   a two-way stream to it. It is provided in this API to facilitate
   the creation of custom directives which shell out to external
   processes.

   zCmd must contain the NUL-terminated command to run and any flags
   for that command, e.g. "myapp --flag --other-flag". It is passed as
   the 4th argument to:

     execl("/bin/sh", "/bin/sh", "-c", zCmd, NULL)

   The po object MUST be cleanly initialized before calling this by
   bitwise copying cmpp_popen_t_empty or (depending on the context)
   cmpp_popen_t_empty_m.

   Flags:

   - cmpp_popen_F_DIRECT: zCmd is passed to execl(zCmd, zCmd, NULL).
     instead of exec(). That can only work if zCmd is a single command
     without arguments.

   - cmpp_popen_F_PATH: tells it to use execlp() or execvp(), which
     performs path lookup of its initial argument. Again, that can
     only work if zCmd is a single command without arguments.

   On success:

   - po->childPid will be set to the PID of the child process.

   - po->fdFromChild is set to the child's stdout file
   descriptor. read(2) from it to read from the child.

   - If po->fpToChild is not NULL then *po->fpToChild is set to a
   buffered output handle to the child's stdin.  fwrite(3) to it to
   send the child stuff. Be sure to fflush(3) and/or fclose(3) it to
   keep it from hanging forever. If po->fpToChild is NULL then the
   stdin of the child is closed. (Why buffered instead of unbuffered?
   My attempts at getting unbuffered child stdin to work have all
   failed when write() is called on it.)

   On success, the caller is obligated to pass po to cmpp_pclose().
   The caller may pass pi to cmpp_pclose() on error, if that's easier
   for them, provided that the po argument was cleanly initialized
   before passing it to this function.

   If the caller fclose(3)s *po->fpToChild then they must set it to
   NULL so that passing it to cmpp_pclose() knows not to close it.

   On error: you know the drill. This function is a no-op if pp has
   error state when it's called, and the current error code is
   returned instead.

   This function is only available on non-WASM Unix-like environments.
   On others it will always trigger a CMPP_RC_UNSUPPORTED error.

   Bugs: because the command is run via /bin/sh -c ...  we cannot tell
   if it's actually found. All we can tell is that /bin/sh ran.

   Also: this doesn't capture stderr, so commands should redirect
   stderr to stdout. Adding the child's stderr handle to cmpp_popen_t is
   a potential TODO without a current use case.

   See: cmpp_pclose()
   See: cmpp_popenv()
*/
CMPP_EXPORT int cmpp_popen(cmpp *pp, unsigned char const *zCmd,
                           cmpp_flag32_t flags, cmpp_popen_t *po);

/**
   Works like cmpp_popen() except that:

   - It takes it arguments in the form of a main()-style array of
     strings because it uses execv() instead of exec(). The
     cmpp_popen_F_PATH flag causes it to use execvp().

   - It does not honor the cmpp_popen_F_DIRECT flag because all
     arguments have to be passed in via the arguments array.

   As per execv()'s requirements: azCmd _MUST_ end with a NULL entry.
*/
CMPP_EXPORT int cmpp_popenv(cmpp *pp, char * const * azCmd,
                            cmpp_flag32_t flags, cmpp_popen_t *po);

/**
   Closes handles returned by cmpp_popen() and zeroes out po. If the
   caller fclose()d *po->fpToChild then they need to set it to NULL so
   that this function does not double-close it.

   Returns the result code of the child process.

   After calling this, po may again be used as an argument to
   cmpp_popen().
*/
CMPP_EXPORT int cmpp_pclose(cmpp_popen_t *po);

/**
   A cmpp_popenv() proxy which builds up an execv()-style array of
   arguments from the given args. It has a hard, and mostly arbitrary,
   upper limit on the number of args it can take in order to avoid
   extra allocation.
*/
CMPP_EXPORT int cmpp_popen_args(cmpp_dx *dx, cmpp_args const * args,
                                cmpp_popen_t *p);


/**
   Callback type for use with cmpp_kav_each().

   cmpp_kav_each() calls this one time per key/value in such a list,
   passing it the relevant key/value strings and lengths, plus the
   opaque state pointer which is passed to cmpp_kav_each().

   Must return 0 on success or update (or propagate) dx->pp's error
   state on error.
*/
typedef int cmpp_kav_each_f(
  cmpp_dx *dx,
  unsigned char const *zKey, cmpp_size_t nKey,
  unsigned char const *zVal, cmpp_size_t nVal,
  void* callbackState
);

/**
   Flag bitmask for use with cmpp_kav_each() and cmpp_str_each().
*/
enum cmpp_kav_each_e {
  /**
     The key argument should be expanded using cmpp_arg_to_b()
     with a 0 flags value. This flag should normally not be used.
  */
  cmpp_kav_each_F_EXPAND_KEY = 0x01,
  /**
     The key argument should be expanded using cmpp_arg_to_b()
     with a 0 flags value. This flag should normally be used.
  */
  cmpp_kav_each_F_EXPAND_VAL = 0x02,
  /**
     Treat (...) value tokens (ttype=cmpp_TT_GroupParen) as integer
     expressions. Keys are never treated this way. Without this flag,
     the token expands to the ... part of (...).
  */
  cmpp_kav_each_F_PARENS_EXPR = 0x04,
  /**
     Indicates that an empty input list is an error. If this flag is
     not set and the list is empty, the callback will not be called
     and no error will be triggered.
  */
  cmpp_kav_each_F_NOT_EMPTY   = 0x08,
  /**
     Indicates that the list does not have the '->' part(s).  That is,
     the list needs to be in pairs of KEY VAL rather than triples of
     KEY -> VALUE.
  */
  cmpp_kav_each_F_NO_ARROW    = 0x10,

  /**
     If set, keys get the cmpp_arg_to_b_F_BRACE_CALL flag added
     to them. This implies cmpp_kav_each_F_EXPAND_KEY.
  */
  cmpp_kav_each_F_CALL_KEY    = 0x20,
  /** Value counterpart of cmpp_kav_each_F_CALL_KEY. */
  cmpp_kav_each_F_CALL_VAL    = 0x40,
  /** Both cmpp_kav_each_F_CALL_KEY and cmpp_kav_each_F_CALL_VAL. */
  cmpp_kav_each_F_CALL        = 0x60,

  //TODO: append to defines which already exist
  cmpp_kav_each_F_APPEND = 0,
  cmpp_kav_each_F_APPEND_SPACE = 0,
  cmpp_kav_each_F_APPEND_NL = 0
};

/**
   A helper for cmpp_dx_f() implementations in processing directive
   arguments which are lists in this form:

   { a -> b c -> d ... }

   ("kav" is short for "key arrow value".)

   The range [zBegin,zBegin+nIn) contains the raw list (not including
   any containing braces, parentheses, quotes, or the like). If nIn is
   negative, strlen() is used to calculate it.

   The range is parsed using cmpp_args_parse().

   For each key/arrow/value triplet in that list, callback() is passed
   the stringified form of the key and the value, plus the
   callbackState pointer.

   The flags argument controls whether the keys and values get
   expanded or not. (Typically the keys should not be expanded but the
   values should.)

   Returns 0 on success. If the callback returns non-0, it is expected
   to have updated dx's error state. callback() will never be called
   when dx's error state is non-0.

   Error results include:

   - CMPP_RC_RANGE: the list is empty does not contain the correct
   number of entries (groups of 3, or 2 if flags has
   cmpp_kav_each_F_NO_ARROW).

   - CMPP_RC_OOM: allocation error.

   - Any value returned by cmpp_args_parse().

   - Any number of errors can be triggered during expansion of
     keys and values.
*/
CMPP_EXPORT int cmpp_kav_each(
  cmpp_dx *dx,
  unsigned char const *zBegin,
  cmpp_ssize_t nIn,
  cmpp_kav_each_f callback, void *callbackState,
  cmpp_flag32_t flags
);

/**
   This works like cmpp_kav_each() except that it treats each token in
   the list as a single entry.

   When the callback is triggered, the "key" part will be the raw
   token and the "value" part will be the expanded form of that
   value. Its flags may contain most of the cmpp_kav_each_F_... flags,
   with the exception that cmpp_kav_each_F_EXPAND_KEY has no effect
   here. If cmpp_kav_each_F_EXPAND_VAL is not in the flags then the
   callback receives the same string for both the key and value.
*/
CMPP_EXPORT int cmpp_str_each(
  cmpp_dx *dx,
  unsigned char const *zBegin,
  cmpp_ssize_t nIn,
  cmpp_kav_each_f callback, void *callbackState,
  cmpp_flag32_t flags
);

/**
   An interface for clients to provide directives to the library
   on-demand.

   This is called when pp has encountered a directive name is does not
   know. It is passed the cmpp object, the name of the directive, and
   the opaque state pointer which was passed to cmpp_d_autoloader_et().

   Implementations should compare dname to any directives they know
   about.  If they find no match they must return CMPP_RC_NO_DIRECTIVE
   _without_ using cmpp_err_set() to make the error persistent.

   If they find a match, they must use cmpp_d_register() to register
   it and (on success) return 0. The library will then look again in
   the registered directive list for the directive before giving up.

   If they find a match but registration fails then the result of that
   failure must be returned.

   For implementation-specific errors, e.g. trying to load a directive
   from a DLL but the loading of the DLL fails, implementations are
   expected to use cmpp_err_set() to report the error and to return
   that result code after performing any necessary cleanup.

   It is legal for an implementation to register multiple directives
   in a single invocation (in particular a pair of opening/closing
   directives), as well as to register directives other than the one
   requested (if necessary). Regardless of which one(s) it registers,
   it must return 0 only if it registers one named dname.
*/
typedef int (*cmpp_d_autoloader_f)(cmpp *pp, char const *dname, void *state);

/**
   A c-pp directive "autoloader". See cmpp_d_autoloader_set()
   and cmpp_d_autoloader_take().
*/
struct cmpp_d_autoloader {
  /** The autoloader callback. */
  cmpp_d_autoloader_f f;
  /**
     Finalizer for this->state. After calling this, if there's any
     chance that this object might be later used, then it is important
     that this->state be set to 0 (which this finalizer cannot
     do). "Best practice" is to bitwise copy cmpp_d_autoloader_empty
     over any instances immediately after calling dtor().
  */
  cmpp_finalizer_f dtor;
  /**
     Implementation-specific state, to be passed as the final argument
     to this->f and this->dtor.
  */
  void * state;
};
typedef struct cmpp_d_autoloader cmpp_d_autoloader;
/**
   Empty-initialized cmpp_d_autoloader instance, intended for
   const-copy initialization.
*/
#define cmpp_d_autoloader_empty_m {.f=0,.dtor=0,.state=0}
/**
   Empty-initialized cmpp_d_autoloader instance, intended for
   non-const-copy initialization.
*/
extern const cmpp_d_autoloader cmpp_d_autoloader_empty;

/**
   Sets pp's "directive autoloader". Each cmpp instance has but a
   single autoloader but this API is provided so that several
   instances may be chained from client-side code.

   This function will call the existing autoloader's destructor (if
   any), invalidating any pointers to its state object.

   If pNew is not NULL then pp's autoloader is set to a bitwise copy
   of *pNew, otherwise it is zeroed out. This transfers ownership of
   pNew->state to pp.

   See cmpp_d_autoloader_f()'s docs for how pNew must behave.

   This function has no error conditions but downstream results are
   undefined if if pNew and an existing autoloader refer to the same
   dtor/state values (a gateway to double-frees).
*/
CMPP_EXPORT void cmpp_d_autoloader_set(cmpp *pp, cmpp_d_autoloader const * pNew);

/**
   Moves pp's current autoloader state into pOld, transerring
   ownership of it to the caller.

   This obligates the caller to eventually either pass that same
   pointer to cmpp_d_autoloader_set() (to transfer ownership back to
   pp) or to call pOld->dtor() (if it's not NULL), passing it it
   pOld->state (even if pOld->state is NULL). In either case, all
   contents of pOld are semantically invalidated and perhaps freed.

   This would normally be a prelude to cmpp_d_autoloader_set() to
   install a custom, perhaps chained, autoloader.
*/
CMPP_EXPORT void cmpp_d_autoloader_take(cmpp *pp, cmpp_d_autoloader * pOld);

/**
   True only for ' ' and '\t'.
*/
CMPP_EXPORT bool cmpp_isspace(int ch);

/**
   Reassigns *p to the address of the first non-space character at or
   after the initial *p value. It stops looking if it reaches zEnd.

   If `*p` does not point to memory before zEnd, or is not a part of
   the same logical string, results are undefined.


   Achtung: do not pass this the address of a cmpp_b::z,
   or similar, as that will effectively corrupt the buffer's
   memory. To trim a whole buffer, use something like:

   ```
   cmpp_b ob = cmpp_b_empty;
   ... populate ob...;
   // get the trimmed range:
   unsigned char const *zB = ob.z;
   unsigned char const *zE = zB + n;
   cmpp_skip_snl(&zB, zE);
   assert( zB<=zE );
   cmpp_skip_snl_trailing(zB, &zE);
   assert( zE>=zB );
   printf("trimmed range: [%.*s]\n", (int)(zE-zB), zB);
   ```

   Those assert()s are not error handling - they're demonstrating
   invariants of the calls made before them.
*/
CMPP_EXPORT void cmpp_skip_space( unsigned char const **p,
                                  unsigned char const *zEnd );

/**
   Works just like cmpp_skip_space() but it also
   skips newlines.

   FIXME (2026-02-21): it does not recognize CRNL pairs as
   atomic newlines.
*/
CMPP_EXPORT void cmpp_skip_snl( unsigned char const **p,
                                unsigned char const *zEnd );

/**
   "Trims" trailing cmpp_isspace() characters from the range [zBegin,
   *p). *p must initially point to one byte after the end of zBegin
   (i.e. its NUL byte or virtual EOF). Upon return *p will be modified
   leftwards (if at all) until a non-space is found or *p==zBegin.
 */
CMPP_EXPORT void cmpp_skip_space_trailing( unsigned char const *zBegin,
                                           unsigned char const **p );

/**
   Works just like cmpp_skip_space_trailing() but
   skips cmpp_skip_snl() characters.

   FIXME (2026-02-21): it does not recognize CRNL pairs as
   atomic newlines.
*/
CMPP_EXPORT void cmpp_skip_snl_trailing( unsigned char const *zBegin,
                                         unsigned char const **p );


/**
   Generic array-of-T list memory-reservation routine.

   *list is the input array-of-T. nDesired is the number of entries to
   reserve (list entry count, not byte length). *nAlloc is the number
   of entries allocated in the list. sizeOfEntry is the sizeof(T) for
   each entry in *list. T may be either a value type or a pointer
   type and sizeofEntry must match, i.e. it must be sizeof(T*) for a
   list-of-pointers and sizeof(T) for a list-of-objects.

   If pp is not NULL then this function updates pp's error state on
   error, else it simply returns CMPP_RC_OOM on error. If pp is not
   NULL then this function is a no-op if called when pp's error state
   is set, returning that code without other side-effects.

   If nDesired > *nAlloc then *list is reallocated to contain at least
   nDesired entries, else this function returns without side effects.

   On success *list is re-assigned to the reallocated list memory, all
   newly-(re)allocated memory is zeroed out, and *nAlloc is updated to
   the new allocation size of *list (the number of list entries, not
   the number of bytes).

   On failure neither *list nor *nAlloc are modified.

   Returns 0 on success or CMPP_RC_OOM on error.  Errors generated by
   this routine are, at least in principle, recoverable (see
   cmpp_err_set()), though that simply means that the pp object is
   left in a well-defined state, not that the app can necessarily
   otherwise recover from an OOM.

   This seemingly-out-of-API-scope routine is in the public API as a
   convenience for client-level cmpp_dx_f() implementations[^1]. This API
   internally has an acute need for basic list management and non-core
   extensions inherit that as well.

   [^1]: this project's own directives are written as if they were
   client-side whenever feasible. Some require cmpp-internal state to
   do their jobs, though.
*/
CMPP_EXPORT int cmpp_array_reserve(cmpp *pp, void **list, cmpp_size_t nDesired,
                                   cmpp_size_t * nAlloc, unsigned sizeOfEntry);


/**
   The current cmpp_api_thunk::apiVersion value.
   See cmpp_api_thunk_map.
*/
#define cmpp_api_thunk_version 20260206

/**
   A helper for use with cmpp_api_thunk.

   V() defines the API version number.  It invokes
   V(NAME,TYPE,VERSION) once. NAME is the member name for the
   cmpp_api_thunk struct. TYPE is an integer type.  VERSION is the
   cmpp_api_thunk object version. This is initially 0 and will
   eventually be given a number which increments which new members
   appended. This is to enable DLLs to check whether their
   cmpp_api_thunk object has the methods they're looking for.

   Then it invokes F(NAME,RETTYPE,PARAMS) and O(NAME,TYPE)
   once for each cmpp_api_thunk member in an unspecified order, and
   and A(VERSION) an arbitrary number of times.

   F() is for functions. O() is for objects, which are exposed here as
   pointers to those objects so that we don't copy them. A() is
   injected at each point where a new API version was introduced, and
   that number (an integer) is its only argument.  A()'s definition
   can normally be empty.

   In all cases, NAME is the public API symbol name minus the "cmpp_"
   prefix. RETTYPE is the function return type or object type. PARAMS
   is the function parameters, wrapped in (...). For O(), TYPE is the
   const-qualified type of the object referred to by
   NAME. cmpp_api_thunk necessarily exposes those as pointers, but
   that pointer is not part of the TYPE argument.

   See cmpp_api_thunk for details.

   In order to help DLLs to not inadvertently use invalid areas of the
   API object by referencing members which they loading c-pp version
   does not have, this list must only ever be modified by appending to
   it. That enables DLLs to check their compile-time
   cmpp_api_thunk_version against the dx->pp->api->apiVersion. If
   the runtime version is older (less than) than their compile-time
   version, the DLL must not access any methods added after
   dx->pp->api->apiVersion.
*/
#define cmpp_api_thunk_map(A,V,F,O)                                   \
  A(0)                                                                \
  V(apiVersion,unsigned,cmpp_api_thunk_version)                       \
  F(mrealloc,void *,(void * p, size_t n))                             \
  F(malloc,void *,(size_t n))                                         \
  F(mfree,void,(void *))                                              \
  F(ctor,int,(cmpp **pp, cmpp_ctor_cfg const *))                      \
  F(dtor,void,(cmpp *pp))                                             \
  F(reset,void,(cmpp *pp))                                            \
  F(check_oom,int,(cmpp * const pp, void const * m))                  \
  F(is_legal_key,bool,(unsigned char const *, cmpp_size_t n,          \
                       unsigned char const **))                       \
  F(define_legacy,int,(cmpp *, const char *,char const *))            \
  F(define_v2,int,(cmpp *, const char *, char const *))               \
  F(undef,int,(cmpp *, const char *, unsigned int *))                 \
  F(define_shadow,int,(cmpp *, char const *, char const *,            \
                       int64_t *))                                    \
  F(define_unshadow,int,(cmpp *, char const *, int64_t))              \
  F(process_string,int,(cmpp *, const char *,                         \
                        unsigned char const *, cmpp_ssize_t))         \
  F(process_file,int,(cmpp *, const char *))                          \
  F(process_stream,int,(cmpp *, const char *,                         \
                        cmpp_input_f, void *))                        \
  F(process_argv,int,(cmpp *, int, char const * const *))             \
  F(err_get,int,(cmpp *, char const **))                              \
  F(err_set,int,(cmpp *, int, char const *, ...))                     \
  F(err_set1,int,(cmpp *, int, char const *))                         \
  F(err_has,int,(cmpp const *))                                       \
  F(is_safemode,bool,(cmpp const *))                                  \
  F(sp_begin,int,(cmpp *))                                            \
  F(sp_commit,int,(cmpp *))                                           \
  F(sp_rollback,int,(cmpp *))                                         \
  F(output_f_FILE,int,(void *, void const *, cmpp_size_t))            \
  F(output_f_fd,int,(void *, void const *, cmpp_size_t))              \
  F(input_f_FILE,int,(void *, void *, cmpp_size_t *))                 \
  F(input_f_fd,int,(void *, void *, cmpp_size_t *))                   \
  F(flush_f_FILE,int,(void *))                                        \
  F(stream,int,(cmpp_input_f, void *,                                 \
                cmpp_output_f, void *))                               \
  F(slurp,int,(cmpp_input_f, void *,                                  \
               unsigned char **, cmpp_size_t *))                      \
  F(fopen,cmpp_FILE *,(char const *, char const *))                   \
  F(fclose,void,(cmpp_FILE * ))                                       \
  F(outputer_out,int,(cmpp_outputer *, void const *, cmpp_size_t))    \
  F(outputer_flush,int,(cmpp_outputer *))                             \
  F(outputer_cleanup,void,(cmpp_outputer *))                          \
  F(outputer_cleanup_f_FILE,void,(cmpp_outputer *))                   \
  F(delimiter_set,int,(cmpp *, char const *))                         \
  F(delimiter_get,void,(cmpp const *, char const **))                 \
  F(chomp,bool,(unsigned char *, cmpp_size_t *))                      \
  F(b_clear,void,(cmpp_b *))                                          \
  F(b_reuse,cmpp_b *,(cmpp_b *))                                      \
  F(b_swap,void,(cmpp_b *, cmpp_b *))                                 \
  F(b_reserve,int,(cmpp_b *, cmpp_size_t))                            \
  F(b_reserve3,int,(cmpp *, cmpp_b *,cmpp_size_t))                    \
  F(b_append,int,(cmpp_b *, void const *,cmpp_size_t))                \
  F(b_append4,int,(cmpp *,cmpp_b *,void const *,                      \
                   cmpp_size_t))                                      \
  F(b_append_ch,  int,(cmpp_b *, char))                               \
  F(b_append_i32,int,(cmpp_b *, int32_t))                             \
  F(b_append_i64,int,(cmpp_b *, int64_t))                             \
  F(b_chomp,bool,(cmpp_b *))                                          \
  F(output_f_b,int,(void *, void const *,cmpp_size_t))                \
  F(outputer_cleanup_f_b,void,(cmpp_outputer *self))                  \
  F(version,char const *,(void))                                      \
  F(tt_cstr,char const *,(int tt))                                    \
  F(dx_err_set,int,(cmpp_dx *dx, int rc, char const *zFmt, ...))      \
  F(dx_next,int,(cmpp_dx * dx, bool * pGotOne))                       \
  F(dx_process,int,(cmpp_dx * dx))                                    \
  F(dx_consume,int,(cmpp_dx *, cmpp_outputer *,                       \
                    cmpp_d const *const *, unsigned, cmpp_flag32_t))  \
  F(dx_consume_b,int,(cmpp_dx *, cmpp_b *, cmpp_d const * const *,    \
                      unsigned, cmpp_flag32_t))                       \
  F(arg_parse,int,(cmpp_dx * dx, cmpp_arg *,                          \
                   unsigned char const **, unsigned char const *,     \
                   unsigned char ** , unsigned char const * ))        \
  F(arg_strdup,char *,(cmpp *pp, cmpp_arg const *arg))                \
  F(arg_to_b,int,(cmpp_dx * dx, cmpp_arg const *arg,                  \
                  cmpp_b * os, cmpp_flag32_t flags))                  \
  F(errno_rc,int,(int errNo, int dflt))                               \
  F(d_register,int,(cmpp * pp, cmpp_d_reg const * r, cmpp_d **pOut))  \
  F(dx_f_dangling_closer,void,(cmpp_dx *dx))                          \
  F(dx_out_raw,int,(cmpp_dx * dx, void const *z, cmpp_size_t n))      \
  F(dx_out_expand,int,(cmpp_dx const * dx, cmpp_outputer * pOut,      \
                       unsigned char const * zFrom,  cmpp_size_t n,   \
                       cmpp_atpol_e policy))                          \
  F(dx_outf,int,(cmpp_dx *dx, char const *zFmt, ...))                 \
  F(dx_delim,char const *,(cmpp_dx const *dx))                        \
  F(atpol_from_str,cmpp_atpol_e,(cmpp * pp, char const *z))           \
  F(atpol_get,cmpp_atpol_e,(cmpp const * const pp))                   \
  F(atpol_set,int,(cmpp * const pp, cmpp_atpol_e pol))                \
  F(atpol_push,int,(cmpp * pp, cmpp_atpol_e pol))                     \
  F(atpol_pop,void,(cmpp * pp))                                       \
  F(unpol_from_str,cmpp_unpol_e,(cmpp * pp,char const *z))            \
  F(unpol_get,cmpp_unpol_e,(cmpp const * const pp))                   \
  F(unpol_set,int,(cmpp * const pp, cmpp_unpol_e pol))                \
  F(unpol_push,int,(cmpp * pp, cmpp_unpol_e pol))                     \
  F(unpol_pop,void,(cmpp * pp))                                       \
  F(path_search,char *,(cmpp *pp, char const *zPath, char pathSep,    \
                        char const *zBaseName, char const *zExt))     \
  F(args_parse,int,(cmpp_dx * dx, cmpp_args * pOut,                   \
                    unsigned char const * zInBegin,                   \
                    cmpp_ssize_t nIn, cmpp_flag32_t flags))           \
  F(args_cleanup,void,(cmpp_args *a))                                 \
  F(dx_args_clone,int,(cmpp_dx * dx, cmpp_args *pOut))                \
  F(popen,int,(cmpp *, unsigned char const *, cmpp_flag32_t,          \
               cmpp_popen_t *))                                       \
  F(popenv,int,(cmpp *pp, char * const * azCmd, cmpp_flag32_t flags,  \
                cmpp_popen_t *po))                                    \
  F(pclose,int,(cmpp_popen_t *po))                                    \
  F(popen_args,int,(cmpp_dx *, cmpp_args const *, cmpp_popen_t *))    \
  F(kav_each,int, (cmpp_dx *,unsigned char const *, cmpp_ssize_t,     \
                   cmpp_kav_each_f, void *, cmpp_flag32_t))           \
  F(d_autoloader_set,void,(cmpp *pp, cmpp_d_autoloader const * pNew)) \
  F(d_autoloader_take,void,(cmpp *pp, cmpp_d_autoloader * pOld))      \
  F(isspace,bool,(int ch))                                            \
  F(skip_space,void,(unsigned char const **, unsigned char const *))  \
  F(skip_snl,void,(unsigned char const **, unsigned char const *))    \
  F(skip_space_trailing,void,(unsigned char const *zBegin,            \
                              unsigned char const **p))               \
  F(skip_snl_trailing,void,(unsigned char const *zBegin,              \
                            unsigned char const **p))                 \
  F(array_reserve,int,(cmpp *pp, void **list, cmpp_size_t nDesired,   \
                       cmpp_size_t * nAlloc, unsigned sizeOfEntry))   \
  F(module_load,int,(cmpp *, char const *,char const *))              \
  F(module_dir_add,int,(cmpp *, const char *))                        \
  O(outputer_FILE,cmpp_outputer const)                                \
  O(outputer_b,cmpp_outputer const)                                   \
  O(outputer_empty,cmpp_outputer const)                               \
  O(b_empty,cmpp_b const)                                             \
  A(20251116)                                                         \
  F(next_chunk,bool,(unsigned char const **,unsigned char const *,    \
                     unsigned char,cmpp_size_t*))                     \
  A(20251118)                                                         \
  F(atdelim_get,void,(cmpp const *,char const **,char const **))      \
  F(atdelim_set,int,(cmpp *,char const *,char const *))               \
  F(atdelim_push,int,(cmpp *,char const *,char const *))              \
  F(atdelim_pop,int,(cmpp *))                                         \
  A(20251224)                                                         \
  F(dx_pos_save,void,(cmpp_dx const *, cmpp_dx_pos *))                \
  F(dx_pos_restore,void,(cmpp_dx *, cmpp_dx_pos const *))             \
  A(20260130)                                                         \
  F(dx_is_call,bool,(cmpp_dx * const))                                \
  A(20260206)                                                         \
  F(b_borrow,cmpp_b *,(cmpp *dx))                                     \
  F(b_return,void,(cmpp *dx, cmpp_b*))                                \
  A(1+cmpp_api_thunk_version)


/**
   Callback signature for cmpp module import routines.

   This is called by the library after having first encountering this
   module (typically after looking for it in a DLL, but static
   instances are supported).

   The primary intended purpose of this interface is for
   implementations to call cmpp_d_register() (any number of times). It
   is also legal to use APIs which set or query defines. This
   interface is not intended to interact with pp's I/O in any way
   (that's the job of the directives which these functions
   register). Violating that will invoke undefined results, perhaps
   stepping on the toes of any being-processed directive which
   triggered the dynamic load of this directive.

   Errors in module initialization must be reported via cmpp_err_set()
   and that code must be returned.

   Implementations must typically call cmpp_api_init(pp) as their
   first operation.

   See the files named d-*.c in libcmpp's source tree for examples.
*/
typedef int (*cmpp_module_init_f)(cmpp * pp);

/**
   Holds information for mapping a cmpp_module_init_f to a name.
   Its purpose is to get installed by the CMPP_MODULE_xxx family of
   macros and referenced later via a module-loading mechanism.
*/
struct cmpp_module{
  /**
     Symbolic name of the module.
  */
  char const * name;

  /**
     The initialization routine for the module.
  */
  cmpp_module_init_f init;
};

/** Convenience typedef. */
typedef struct cmpp_module cmpp_module;

/** @def CMPP_MODULE_DECL

   Declares an extern (cmpp_module*) symbol called
   cmpp_module__#\#CNAME.

   Use CMPP_MODULE_IMPL2() or CMPP_MODULE_IMPL3() to create the
   matching implementation code.

   This macro should be used in the C or H file for a loadable module.
   It may be compined in a file with a single CMPP_MODULE_IMPL_SOLO()
   declaration with the same name, such that the module can be loaded
   both with and without the explicit symbol name.
*/
#define CMPP_MODULE_DECL(CNAME)                            \
    extern const cmpp_module * cmpp_module__##CNAME

/** @def CMPP_MODULE_IMPL

   Intended to be used to implement module declarations.  If a module
   has both C and H files, CMPP_MODULE_DECL(CNAME) should be used in the
   H file and CMPP_MODULE_IMPL2() should be used in the C file. If the
   DLL has only a C file (or no public H file), CMPP_MODULE_DECL is
   unnecessary.

   If the module's human-use name is a legal C identifier,
   CMPP_MODULE_IMPL2() is slightly easier to use than this macro.

   Implements a static cmpp_module object named
   cmpp_module__#\#CNAME#\#_impl and a non-static
   (cmpp_module*) named cmpp_module__#\#CNAME which points to
   cmpp_module__#\#CNAME#\#_impl. (The latter symbol may optionally be
   declared in a header file via CMPP_MODULE_DECL.) NAME is used as
   the cmpp_module::name value.

   INIT_F must be a cmpp_module_init_f() function pointer. That function
   is called when cmpp_module_load() loads the module.

   This macro may be combined in a file with a single
   CMPP_MODULE_IMPL_SOLO() declaration using the same CNAME value,
   such that the module can be loaded both with and without the
   explicit symbol name.

   Example usage, in a module's header file, if any:

   ```
   CMPP_MODULE_DECL(mymodule);
   ```

   (The declaration is not strictly necessary - it is more of a matter
   of documentation.)

   And in the C file:

   ```
   CMPP_MODULE_IMPL3(mymodule,"mymodule",mymodule_install);
   // OR:
   CMPP_MODULE_IMPL2(mymodule,mymodule_install);
   ```

   If it will be the only module in the target DLL, one can also add
   this:

   ```
   CMPP_MODULE_IMPL2(mymodule,mymodule_install);
   // _OR_ (every so slightly different):
   CMPP_MODULE_STANDALONE_IMPL2(mymodule,mymodule_install);
   ```

   Which simplifies client-side module loading by allowing them to
   leave out the module name when loading, but that approach only
   works if modules are compiled one per DLL (as opposed to being
   packaged together in one DLL).

   @see CMPP_MODULE_DECL
   @see CMPP_MODULE_IMPL_SOLO
*/
#define CMPP_MODULE_IMPL3(CNAME,NAME,INIT_F)        \
  static const cmpp_module                 \
  cmpp_module__##CNAME##_impl = { NAME, INIT_F };   \
  const cmpp_module *                      \
  cmpp_module__##CNAME = &cmpp_module__##CNAME##_impl

/** @def CMPP_MODULE_IMPL3

    A simplier form of CMPP_MODULE_IMPL3() for cases where a module name
    is a legal C symbol name.
*/
#define CMPP_MODULE_IMPL2(CNAME,INIT_F) \
  CMPP_MODULE_IMPL3(CNAME,#CNAME,INIT_F)

/** @def CMPP_MODULE_IMPL_SOLO

   Implements a static cmpp_module symbol called
   cmpp_module1_impl and a non-static (cmpp_module*) named
   cmpp_module1 which points to cmpp_module1_impl

   INIT_F must be a cmpp_module_init_f.

   This macro must only be used in the C file for a loadable module
   when that module is to be the only one in the resuling DLL. Do not
   use it when packaging multiple modules into one DLL: use
   CMPP_MODULE_IMPL for those cases (CMPP_MODULE_IMPL can also be used
   together with this macro).

   @see CMPP_MODULE_IMPL
   @see CMPP_MODULE_DECL
   @see CMPP_MODULE_STANDALONE_IMPL
*/
#define CMPP_MODULE_IMPL_SOLO(NAME,INIT_F)        \
  static const cmpp_module               \
  cmpp_module1_impl = { NAME, INIT_F };           \
  const cmpp_module * cmpp_module1 = &cmpp_module1_impl
/** @def CMPP_MODULE_STANDALONE_IMPL

    CMPP_MODULE_STANDALONE_IMPL2() works like CMPP_MODULE_IMPL_SOLO()
    but is only fully expanded if the preprocessor variable
    CMPP_MODULE_STANDALONE is defined (to any value).  If
    CMPP_MODULE_STANDALONE is not defined, this macro expands to a
    dummy placeholder which does nothing (but has to expand to
    something to avoid leaving a trailing semicolon in the C code,
    which upsets the compiler (the other alternative would be to not
    require a semicolon after the macro call, but that upsets emacs'
    sense of indentation (and keeping emacs happy is more important
    than keeping compilers happy (all of these parens are _not_ a
    reference to emacs lisp, by the way)))).

    This macro may be used in the same source file as
    CMPP_MODULE_IMPL.

    The intention is that DLLs prefer this option over
    CMPP_MODULE_IMPL_SOLO, to allow that the DLLs can be built as
    standalone DLLs, multi-plugin DLLs, and compiled directly into a
    project (in which case the code linking it in needs to resolve and
    call the cmpp_module entry for each built-in module).

   @see CMPP_MODULE_IMPL_SOLO
   @see CMPP_MODULE_REGISTER
*/
#if defined(CMPP_MODULE_STANDALONE)
#  define CMPP_MODULE_STANDALONE_IMPL2(NAME,INIT_F) \
  CMPP_MODULE_IMPL_SOLO(NAME,INIT_F)
//arguably too much magic in one place:
//#  if !defined(CMPP_API_THUNK)
//#    define CMPP_API_THUNK
//#  endif
#else
#  define CMPP_MODULE_STANDALONE_IMPL2(NAME,INIT_F) \
  extern void cmpp_module__dummy_does_not_exist__(void)
#endif

/** @def CMPP_MODULE_REGISTER3

   Performs all the necessary setup for registering a loadable module,
   including declaration and definition. NAME is the stringified name
   of the module. This is normally called immediately after defining
   the plugin's init func (which is passed as the 3rd argument to this
   macro).

   See CMPP_MODULE_IMPL3() and CMPP_MODULE_STANDALONE_IMPL2() for
   the fine details.
*/
#define CMPP_MODULE_REGISTER3(CNAME,NAME,INIT_F) \
  CMPP_MODULE_IMPL3(CNAME,NAME,INIT_F);          \
  CMPP_MODULE_STANDALONE_IMPL2(NAME,INIT_F)

/**
   Slight convenience form of CMPP_MODULE_REGISTER3() which assumes a
   registration function name of cpp_ext_${CNAME}_register().
*/
#define CMPP_MODULE_REGISTER2(CNAME,NAME) \
  CMPP_MODULE_REGISTER3(CNAME,NAME,cmpp_module__ ## CNAME ## _register)

/**
   Slight convenience form of CMPP_MODULE_REGISTER2() for cases when
   CNAME and NAME are the same.
*/
#define CMPP_MODULE_REGISTER1(CNAME) \
  CMPP_MODULE_REGISTER3(CNAME,#CNAME,cmpp_module__ ## CNAME ## _register)

/**
   This looks for a DLL file named fname.  If found, it is dlopen()ed
   (or equivalent) and searched for a symbol named symName. If found,
   it is assumed to be a cmpp_module instance and its init() method is
   invoked.

   If fname is NULL then the module is looked up in the
   currently-running program.

   If symName is NULL then the name "cmpp_module1" is assumed, which
   is the name used by CMPP_MODULE_IMPL_SOLO() and friends (for use
   when a module is the only one in its DLL).

   If no match is found, or there's a problem loading the DLL or
   resolving the name, non-0 is returned. Similarly, if the init()
   method fails, non-0 is returned.

   The file name is searched using the cmpp_module_dir_add() path, and
   if fname is an exact match, or an exact when the system's
   conventional DLL file extension is appended to it, that is used
   rather than any potential match from the search path.

   On error, pp's error state will contain more information. It's
   indeterminate which errors from this API are recoverable.

   This function is a no-op if called when pp's error state is set,
   returning that code.

   If built without module-loading support then this will always
   fail with CMPP_RC_UNSUPPORTED.
*/
CMPP_EXPORT int cmpp_module_load(cmpp * pp, char const * fname,
                                 char const * symName);

/**
   Adds the directory or directories listed in zDirs to the search
   path used by cmpp_module_load(). The entries are expected to be
   either colon- or semicolon-delimited, depending on the platform the
   library was built for.

   If zDirs is NULL and pp's library path is empty then it looks for
   the environment variable CMPP_MODULE_PATH. If that is set, it is
   used in place of zDirs, otherwise the library's compile-time
   default is used (as set by the CMPP_MODULE_PATH compile-time value,
   which defaults to ".:$prefix/lib/cmpp" in the canonical builds).
   This should only be done once per cmpp instance, as the path will
   otherwise be extended each time. (The current list structure does
   not make it easy to recognize duplicates.)

   Returns 0 on success or if zDirs is empty. Returns CMPP_RC_OOM on
   allocation error (ostensibly recoverable - see cmpp_err_set()).

   This is a no-op if called when pp has error state, returning that
   code without other side-effects.

   If modules are not enabled then this function is a no-op and always
   returns CMPP_RC_UNSUPPORTED _without_ setting pp's error state (as
   it's not an error, per se). That can typically be ignored as a
   non-error.
*/
CMPP_EXPORT int cmpp_module_dir_add(cmpp *pp, const char * zDirs);


/**
   State for a cmpp_dx_pimpl which we need in order to snapshot the
   parse position for purposes of restoring it later. This is
   basically to support that #query can contain other #query
   directives, but this same capability is required by any directives
   which want to both process directives in their content block and
   loop over the content block.
*/
struct cmpp_dx_pos {
  /** Current parse pos. */
  unsigned char const *z;
  /** Current line number. */
  cmpp_size_t lineNo;
};
typedef struct cmpp_dx_pos cmpp_dx_pos;
#define cmpp_dx_pos_empty_m {.z=0,.lineNo=0U}//,.dline=CmppDLine_empty_m}

/**
   Stores dx's current input position into pos. pos gets completely
   initialized by this routine - it need not (in contrast to many
   other functions in this library) be cleanly initialized by the
   caller first.
*/
CMPP_EXPORT void cmpp_dx_pos_save(cmpp_dx const * dx, cmpp_dx_pos *pos);

/**
   Restores dx's input position from pos. Results are undefined if pos
   is not populated with the result of having passed the same dx/pos
   pointer combination to cmpp_dx_pos_save().
*/
CMPP_EXPORT void cmpp_dx_pos_restore(cmpp_dx * dx, cmpp_dx_pos const * pos);

/**
   A "thunk" for use with loadable modules, encapsulating all of the
   functions from the public cmpp API into an object. This allows
   loadable modules to call into the cmpp API if the binary which
   loads them not built in such a way that it exports libcmpp's
   symbols to the DLL. (On Linux systems, that means if it's not
   linked with -rdynamic.)

   For every public cmpp function, this struct has a member with the
   same signature and name, minus the "cmpp_" name prefix. Thus
   cmpp_foo(...) is accessible as api->foo(...).

   Object-type exports, e.g. cmpp_b_empty, are exposed here as
   pointers instead of objects. The CMPP_API_THUNK-installed API
   wrapper macros account for that.

   There is only one instance of this class and it gets passed into
   cmpp_module_init_f() methods. It is also assigned to the
   cmpp_dx::api member of cmpp_dx instances which get passed to
   cmpp_dx_f() implementations.

   Loadable modules "should" use this interface to access the API,
   rather than the global symbols. If they don't then the module may,
   depending on how the loading application was linked, throw
   unresolved symbols errors when loading.
*/
struct cmpp_api_thunk {
#define A(VER)
#define V(N,T,VER) T N;
#define F(N,T,P) T (*N)P;
#define O(N,T) T * const N;
  cmpp_api_thunk_map(A,V,F,O)
#undef F
#undef O
#undef V
#undef A
};

/**
   For loadable modules to be able portably access the cmpp API,
   without requiring that their loading binary be linked with
   -rdynamic, we need a "thunk". The API exposes cmpp_api_thunk
   for that purpose. The following macros set up the thunk for
   a given compilation unit. They are intended to only be used
   by loadable modules, not generic client code.

   Before including this header, define CMPP_API_THUNK with no value
   and/or define CMPP_API_THUNK_NAME to a C symbol name.  The latter
   macro implies the former and defines the name of the static symbol
   to be the local cmpp_api_thunk instance, defaulting to cmppApi.

   The first line of a module's registration function should then be:

   cmpp_api_init(pp);

   where pp is the name of the sole argument to the registration
   callback. After that is done, the cmpp_...() APIs may be used via
   the macros defined below, all of which route through the thunk
   object.
*/
#if defined(CMPP_API_THUNK) || defined(CMPP_API_THUNK_NAME)
#  if !defined(CMPP_API_THUNK)
#    define CMPP_API_THUNK
#  endif
#  if !defined(CMPP_API_THUNK_NAME)
#    define CMPP_API_THUNK_NAME cmppApi
#  endif
#  if !defined(CMPP_API_THUNK__defined)
#  define CMPP_API_THUNK__defined
static cmpp_api_thunk const * CMPP_API_THUNK_NAME = 0;
#  endif
/**
   cmpp_api_init() must be invoked from the module's registration
   function, passed the only argument to that function. It sets the
   global symbol CMPP_API_THUNK_NAME to its argument. From that point
   on, the thunk's API is accessible via cmpp_foo macros which proxy
   theThunk->foo.

   It is safe to call this from, e.g. a cmpp_dx_f() implementation, as
   it will always have the same pointer, so long as it is not passed
   NULL, which would make the next cmpp_...() call segfault.
*/
#  if !defined(CMPP_API_THUNK__assigned)
#  define CMPP_API_THUNK__assigned
#    define cmpp_api_init(PP) CMPP_API_THUNK_NAME = (PP)->api
#  else
#    define cmpp_api_init(PP) (void)(PP)/*CMPP_API_THUNK_NAME*/
#  endif
/* What follows is generated code from c-pp's (#pragma api-thunk). */
/* Thunk APIs which follow are available as of version 0... */
#define cmpp_mrealloc CMPP_API_THUNK_NAME->mrealloc
#define cmpp_malloc CMPP_API_THUNK_NAME->malloc
#define cmpp_mfree CMPP_API_THUNK_NAME->mfree
#define cmpp_ctor CMPP_API_THUNK_NAME->ctor
#define cmpp_dtor CMPP_API_THUNK_NAME->dtor
#define cmpp_reset CMPP_API_THUNK_NAME->reset
#define cmpp_check_oom CMPP_API_THUNK_NAME->check_oom
#define cmpp_is_legal_key CMPP_API_THUNK_NAME->is_legal_key
#define cmpp_define_legacy CMPP_API_THUNK_NAME->define_legacy
#define cmpp_define_v2 CMPP_API_THUNK_NAME->define_v2
#define cmpp_undef CMPP_API_THUNK_NAME->undef
#define cmpp_define_shadow CMPP_API_THUNK_NAME->define_shadow
#define cmpp_define_unshadow CMPP_API_THUNK_NAME->define_unshadow
#define cmpp_process_string CMPP_API_THUNK_NAME->process_string
#define cmpp_process_file CMPP_API_THUNK_NAME->process_file
#define cmpp_process_stream CMPP_API_THUNK_NAME->process_stream
#define cmpp_process_argv CMPP_API_THUNK_NAME->process_argv
#define cmpp_err_get CMPP_API_THUNK_NAME->err_get
#define cmpp_err_set CMPP_API_THUNK_NAME->err_set
#define cmpp_err_set1 CMPP_API_THUNK_NAME->err_set1
#define cmpp_err_has CMPP_API_THUNK_NAME->err_has
#define cmpp_is_safemode CMPP_API_THUNK_NAME->is_safemode
#define cmpp_sp_begin CMPP_API_THUNK_NAME->sp_begin
#define cmpp_sp_commit CMPP_API_THUNK_NAME->sp_commit
#define cmpp_sp_rollback CMPP_API_THUNK_NAME->sp_rollback
#define cmpp_output_f_FILE CMPP_API_THUNK_NAME->output_f_FILE
#define cmpp_output_f_fd CMPP_API_THUNK_NAME->output_f_fd
#define cmpp_input_f_FILE CMPP_API_THUNK_NAME->input_f_FILE
#define cmpp_input_f_fd CMPP_API_THUNK_NAME->input_f_fd
#define cmpp_flush_f_FILE CMPP_API_THUNK_NAME->flush_f_FILE
#define cmpp_stream CMPP_API_THUNK_NAME->stream
#define cmpp_slurp CMPP_API_THUNK_NAME->slurp
#define cmpp_fopen CMPP_API_THUNK_NAME->fopen
#define cmpp_fclose CMPP_API_THUNK_NAME->fclose
#define cmpp_outputer_out CMPP_API_THUNK_NAME->outputer_out
#define cmpp_outputer_flush CMPP_API_THUNK_NAME->outputer_flush
#define cmpp_outputer_cleanup CMPP_API_THUNK_NAME->outputer_cleanup
#define cmpp_outputer_cleanup_f_FILE CMPP_API_THUNK_NAME->outputer_cleanup_f_FILE
#define cmpp_delimiter_set CMPP_API_THUNK_NAME->delimiter_set
#define cmpp_delimiter_get CMPP_API_THUNK_NAME->delimiter_get
#define cmpp_chomp CMPP_API_THUNK_NAME->chomp
#define cmpp_b_clear CMPP_API_THUNK_NAME->b_clear
#define cmpp_b_reuse CMPP_API_THUNK_NAME->b_reuse
#define cmpp_b_swap CMPP_API_THUNK_NAME->b_swap
#define cmpp_b_reserve CMPP_API_THUNK_NAME->b_reserve
#define cmpp_b_reserve3 CMPP_API_THUNK_NAME->b_reserve3
#define cmpp_b_append CMPP_API_THUNK_NAME->b_append
#define cmpp_b_append4 CMPP_API_THUNK_NAME->b_append4
#define cmpp_b_append_ch CMPP_API_THUNK_NAME->b_append_ch
#define cmpp_b_append_i32 CMPP_API_THUNK_NAME->b_append_i32
#define cmpp_b_append_i64 CMPP_API_THUNK_NAME->b_append_i64
#define cmpp_b_chomp CMPP_API_THUNK_NAME->b_chomp
#define cmpp_output_f_b CMPP_API_THUNK_NAME->output_f_b
#define cmpp_outputer_cleanup_f_b CMPP_API_THUNK_NAME->outputer_cleanup_f_b
#define cmpp_version CMPP_API_THUNK_NAME->version
#define cmpp_tt_cstr CMPP_API_THUNK_NAME->tt_cstr
#define cmpp_dx_err_set CMPP_API_THUNK_NAME->dx_err_set
#define cmpp_dx_next CMPP_API_THUNK_NAME->dx_next
#define cmpp_dx_process CMPP_API_THUNK_NAME->dx_process
#define cmpp_dx_consume CMPP_API_THUNK_NAME->dx_consume
#define cmpp_dx_consume_b CMPP_API_THUNK_NAME->dx_consume_b
#define cmpp_arg_parse CMPP_API_THUNK_NAME->arg_parse
#define cmpp_arg_strdup CMPP_API_THUNK_NAME->arg_strdup
#define cmpp_arg_to_b CMPP_API_THUNK_NAME->arg_to_b
#define cmpp_errno_rc CMPP_API_THUNK_NAME->errno_rc
#define cmpp_d_register CMPP_API_THUNK_NAME->d_register
#define cmpp_dx_f_dangling_closer CMPP_API_THUNK_NAME->dx_f_dangling_closer
#define cmpp_dx_out_raw CMPP_API_THUNK_NAME->dx_out_raw
#define cmpp_dx_out_expand CMPP_API_THUNK_NAME->dx_out_expand
#define cmpp_dx_outf CMPP_API_THUNK_NAME->dx_outf
#define cmpp_dx_delim CMPP_API_THUNK_NAME->dx_delim
#define cmpp_atpol_from_str CMPP_API_THUNK_NAME->atpol_from_str
#define cmpp_atpol_get CMPP_API_THUNK_NAME->atpol_get
#define cmpp_atpol_set CMPP_API_THUNK_NAME->atpol_set
#define cmpp_atpol_push CMPP_API_THUNK_NAME->atpol_push
#define cmpp_atpol_pop CMPP_API_THUNK_NAME->atpol_pop
#define cmpp_unpol_from_str CMPP_API_THUNK_NAME->unpol_from_str
#define cmpp_unpol_get CMPP_API_THUNK_NAME->unpol_get
#define cmpp_unpol_set CMPP_API_THUNK_NAME->unpol_set
#define cmpp_unpol_push CMPP_API_THUNK_NAME->unpol_push
#define cmpp_unpol_pop CMPP_API_THUNK_NAME->unpol_pop
#define cmpp_path_search CMPP_API_THUNK_NAME->path_search
#define cmpp_args_parse CMPP_API_THUNK_NAME->args_parse
#define cmpp_args_cleanup CMPP_API_THUNK_NAME->args_cleanup
#define cmpp_dx_args_clone CMPP_API_THUNK_NAME->dx_args_clone
#define cmpp_popen CMPP_API_THUNK_NAME->popen
#define cmpp_popenv CMPP_API_THUNK_NAME->popenv
#define cmpp_pclose CMPP_API_THUNK_NAME->pclose
#define cmpp_popen_args CMPP_API_THUNK_NAME->popen_args
#define cmpp_kav_each CMPP_API_THUNK_NAME->kav_each
#define cmpp_d_autoloader_set CMPP_API_THUNK_NAME->d_autoloader_set
#define cmpp_d_autoloader_take CMPP_API_THUNK_NAME->d_autoloader_take
#define cmpp_isspace CMPP_API_THUNK_NAME->isspace
#define cmpp_isnl CMPP_API_THUNK_NAME->isnl
#define cmpp_issnl CMPP_API_THUNK_NAME->issnl
#define cmpp_skip_space CMPP_API_THUNK_NAME->skip_space
#define cmpp_skip_snl CMPP_API_THUNK_NAME->skip_snl
#define cmpp_skip_space_trailing CMPP_API_THUNK_NAME->skip_space_trailing
#define cmpp_skip_snl_trailing CMPP_API_THUNK_NAME->skip_snl_trailing
#define cmpp_array_reserve CMPP_API_THUNK_NAME->array_reserve
#define cmpp_module_load CMPP_API_THUNK_NAME->module_load
#define cmpp_module_dir_add CMPP_API_THUNK_NAME->module_dir_add
#define cmpp_outputer_FILE (*CMPP_API_THUNK_NAME->outputer_FILE)
#define cmpp_outputer_b (*CMPP_API_THUNK_NAME->outputer_b)
#define cmpp_outputer_empty (*CMPP_API_THUNK_NAME->outputer_empty)
#define cmpp_b_empty (*CMPP_API_THUNK_NAME->b_empty)
/* Thunk APIs which follow are available as of version 20251116... */
#define cmpp_next_chunk CMPP_API_THUNK_NAME->next_chunk
/* Thunk APIs which follow are available as of version 20251118... */
#define cmpp_atdelim_get CMPP_API_THUNK_NAME->atdelim_get
#define cmpp_atdelim_set CMPP_API_THUNK_NAME->atdelim_set
#define cmpp_atdelim_push CMPP_API_THUNK_NAME->atdelim_push
#define cmpp_atdelim_pop CMPP_API_THUNK_NAME->atdelim_pop
/* Thunk APIs which follow are available as of version 20251224... */
#define cmpp_dx_pos_save CMPP_API_THUNK_NAME->dx_pos_save
#define cmpp_dx_pos_restore CMPP_API_THUNK_NAME->dx_pos_restore
/* Thunk APIs which follow are available as of version 20260130... */
#define cmpp_dx_is_call CMPP_API_THUNK_NAME->dx_is_call
/* Thunk APIs which follow are available as of version 20260206... */
#define cmpp_b_borrow CMPP_API_THUNK_NAME->b_borrow
#define cmpp_b_return CMPP_API_THUNK_NAME->b_return


#else /* not CMPP_API_THUNK */
/**
   cmpp_api_init() is a no-op when not including a file-local API
   thunk.
*/
#  define cmpp_api_init(PP) (void)0
#endif /* CMPP_API_THUNK */

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* include guard */
#endif /* NET_WANDERINGHORSE_LIBCMPP_H_INCLUDED */
#if !defined(NET_WANDERINGHORSE_CMPP_INTERNAL_H_INCLUDED)
#define NET_WANDERINGHORSE_CMPP_INTERNAL_H_INCLUDED
/**
   This file houses declarations and macros for the private/internal
   libcmpp APIs.
*/
#include "sqlite3.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

/* write() and friends */
#if defined(_WIN32) || defined(WIN32)
#  include <io.h>
#  include <fcntl.h>
#  ifndef access
#    define access(f,m) _access((f),(m))
#  endif
#else
#  include <unistd.h>
#  include <sys/wait.h>
#endif

#ifndef CMPP_DEFAULT_DELIM
#define CMPP_DEFAULT_DELIM "##"
#endif

#ifndef CMPP_ATSIGN
#define CMPP_ATSIGN (unsigned char)'@'
#endif

#ifndef CMPP_MODULE_PATH
#define CMPP_MODULE_PATH "."
#endif

#if defined(NDEBUG)
#define cmpp__staticAssert(NAME,COND) (void)1
#else
#define cmpp__staticAssert(NAME,COND) \
  static const char staticAssert_ ## NAME[ (COND) ? 1 : -1 ] = {0}; \
  (void)staticAssert_ ## NAME
#endif

#if defined(CMPP_OMIT_ALL_UNSAFE)
#undef  CMPP_OMIT_D_PIPE
#define CMPP_OMIT_D_PIPE
#undef  CMPP_OMIT_D_DB
#define CMPP_OMIT_D_DB
#undef  CMPP_OMIT_D_INCLUDE
#define CMPP_OMIT_D_INCLUDE
#undef  CMPP_OMIT_D_MODULE
#define CMPP_OMIT_D_MODULE
#endif

#if !defined(CMPP_VERSION)
#error "exporting CMPP_VERSION to have been set up"
#endif

#define CMPP__DB_MAIN_NAME "cmpp"

#if defined(CMPP_AMALGAMATION)
#define CMPP_PRIVATE static
#else
#define CMPP_PRIVATE
#endif

#if CMPP_PLATFORM_IS_WASM
#  define CMPP_PLATFORM_IS_WINDOWS 0
#  define CMPP_PLATFORM_IS_UNIX 0
#  define CMPP_PLATFORM_PLATFORM "wasm"
#  define CMPP_PATH_SEPARATOR ':'
#  define CMPP__EXPORT_NAMED(X) __attribute__((export_name(#X),used,visibility("default")))
// See also:
//__attribute__((export_name("theExportedName"), used, visibility("default")))
#  define CMPP_OMIT_FILE_IO /* potential todo but with a large footprint */
#  if !defined(CMPP_PLATFORM_EXT_DLL)
#    define CMPP_PLATFORM_EXT_DLL ""
#  endif
#else
//#  define CMPP_WASM_EXPORT
#  define CMPP__EXPORT_NAMED(X)
#  if defined(_WIN32) || defined(WIN32)
#    define CMPP_PLATFORM_IS_WINDOWS 1
#    define CMPP_PLATFORM_IS_UNIX 0
#    define CMPP_PLATFORM_PLATFORM "windows"
#    define CMPP_PATH_SEPARATOR ';'
//#  include <io.h>
#  elif defined(__MINGW32__) || defined(__MINGW64__)
#    define CMPP_PLATFORM_IS_WINDOWS 1
#    define CMPP_PLATFORM_IS_UNIX 0
#    define CMPP_PLATFORM_PLATFORM "windows"
#    define CMPP_PATH_SEPARATOR ':' /*?*/
#  elif defined(__CYGWIN__)
#    define CMPP_PLATFORM_IS_WINDOWS 0
#    define CMPP_PLATFORM_IS_UNIX 1
#    define CMPP_PLATFORM_PLATFORM "unix"
#    define CMPP_PATH_SEPARATOR ':'
#  else
#    define CMPP_PLATFORM_IS_WINDOWS 0
#    define CMPP_PLATFORM_IS_UNIX 1
#    define CMPP_PLATFORM_PLATFORM "unix"
#    define CMPP_PATH_SEPARATOR ':'
#  endif
#endif

#define CMPP__EXPORT(RETTYPE,NAME) CMPP__EXPORT_NAMED(NAME) RETTYPE NAME

#if !defined(CMPP_PLATFORM_EXT_DLL)
#  error "Expecting CMPP_PLATFORM_EXT_DLL to have been set by the auto-configured bits"
#  define CMPP_PLATFORM_EXT_DLL "???"
#endif

#if 1
#  define CMPP_NORETURN __attribute__((noreturn))
#else
#  define CMPP_NORETURN
#endif

/** @def CMPP_HAVE_DLOPEN

    If set to true, use dlopen() and friends. Requires
    linking to -ldl on some platforms.

    Only one of CMPP_HAVE_DLOPEN and CMPP_HAVE_LTDLOPEN may be
    true.
*/
/** @def CMPP_HAVE_LTDLOPEN

    If set to true, use lt_dlopen() and friends. Requires
    linking to -lltdl on most platforms.

    Only one of CMPP_HAVE_DLOPEN and CMPP_HAVE_LTDLOPEN may be
    true.
*/
#if !defined(CMPP_HAVE_DLOPEN)
#  if defined(HAVE_DLOPEN)
#    define CMPP_HAVE_DLOPEN HAVE_DLOPEN
#  else
#    define CMPP_HAVE_DLOPEN 0
#  endif
#endif

#if !defined(CMPP_HAVE_LTDLOPEN)
#  if defined(HAVE_LTDLOPEN)
#    define CMPP_HAVE_LTDLOPEN HAVE_LTDLOPEN
#  else
#    define CMPP_HAVE_LTDLOPEN 0
#  endif
#endif

#if !defined(CMPP_ENABLE_DLLS)
#  define CMPP_ENABLE_DLLS (CMPP_HAVE_LTDLOPEN || CMPP_HAVE_DLOPEN)
#endif
#if CMPP_ENABLE_DLLS && !defined(CMPP_OMIT_D_MODULE)
#  define CMPP_D_MODULE 1
#else
#  define CMPP_D_MODULE 0
#endif

/**
  Many years of practice have taught that it is literally impossible
  to safely close DLLs because simply opening one may trigger
  arbitrary code (at least for C++ DLLs) which "might" be used by the
  application. e.g. some classloaders use DLL initialization to inject
  new classes into the application without the app having to do
  anything more than open the DLL. (That's precisely what the cmpp
  port of this code is doing except that we don't call it classloading
  here.)

  So cmpp does not close DLLs. Except (...sigh...) to please valgrind.

  When CMPP_CLOSE_DLLS is true then this API will keep track of DLL
  handles so that they can be closed, and offers the ability for
  higher-level clients to close them (all at once, not individually).
*/
#if !defined(CMPP_CLOSE_DLLS)
#define CMPP_CLOSE_DLLS 1
#endif

/** Proxy for cmpp_malloc() which (A) is a no-op if ppCode
    and (B) sets pp->err on OOM.
*/
CMPP_PRIVATE void * cmpp__malloc(cmpp* pp, cmpp_size_t n);

/**
   Internal-use-only flags for use with cmpp_d::flags.

   These supplement the ones from the public API's cmpp_d_e.
*/
enum cmpp_d_ext_e {
  /**
     Mask of flag bits from this enum and cmpp_d_e which are for
     internal use only and are disallowed in client-side directives.
  */
  cmpp_d_F_MASK_INTERNAL = ~cmpp_d_F_MASK,

  /**
     If true, and if cmpp_d_F_ARGS_LIST is set, then cmpp_args_parse()
     will pass its results to cmpp_args__not_simplify(). Only
     directives which eval cmpp_arg expressions need this, and the
     library does not expose the pieces for evaluating such
     expressions.  As such, this flag is for internal use only. This
     only has an effect if cmpp_d_F_ARGS_LIST is also used.
  */
  cmpp_d_F_NOT_SIMPLIFY  = 0x10000,

  /**
     Most directives are inert when they are seen in the "falsy" part
     of an if/else. The callbacks for such directives are skipped, as
     opposed to requiring each directive's callback to check whether
     they should be skipping. This flag indicates that a directive
     must always be run, even when skipping content (e.g. inside of an
     #if 0 block). Only flow-control directives may have the
     FLOW_CONTROL bit set. The library API does not expose enough of
     its internals for client-defined directives to make flow-control
     decisions.

     i really want to get rid of this flag but it seems to be a
     necessary evil.
  */
  cmpp_d_F_FLOW_CONTROL  = 0x20000
};

/**
   A single directive line from an input stream.
*/
struct CmppDLine {
  /** Line number in the source input. */
  cmpp_size_t lineNo;
  /** Start of the line within its source input. */
  unsigned char const * zBegin;
  /** One-past-the-end byte of the line. A virtual EOF. It will only
      actually be NUL-terminated if it is the last line of the input
      and that input has no trailing newline. */
  unsigned char const * zEnd;
};
typedef struct CmppDLine CmppDLine;
#define CmppDLine_empty_m {0U,0,0}

/**
   A snippet from a string.
*/
struct CmppSnippet {
  /* Start of the range. */
  unsigned char const *z;
  /* Number of bytes. */
  unsigned int n;
};
typedef struct CmppSnippet CmppSnippet;
#define CmppSnippet_empty_m {0,0}

/**
   CmppLvl represents one "level" of parsing, pushing one level
   for each of `#if` and popping one for each `#/if`.

   These pieces are ONLY for use with flow-control directives. It's
   not proven that they can be of any use to more than a single
   flow-control directive. e.g. if we had a hypothetical #foreach, we
   may need to extend this.
*/
struct CmppLvl {
#if 0
  /**
     The directive on whose behalf this level was opened.
  */
  cmpp_d const * d;
  /**
     Opaque directive-specific immutable state. It's provided as a way
     for a directive to see whether the top of the stack is correct
     after it processes inner directives.
  */
  void const * state;
#endif
  /**
     Bitmask of CmppLvl_F_...
  */
  cmpp_flag32_t flags;
  /**
     The directive line number which started this level.  This is used for
     reporting the starting lines of erroneously unclosed block
     constructs.
  */
  cmpp_size_t lineNo;
};
typedef struct CmppLvl CmppLvl;
#define CmppLvl_empty_m {/*.d=0, .state=0,*/ .flags=0U, .lineNo=0U}

/**
   Declares struct T as a container for a list-of-MT.  MT may be
   pointer-qualified. cmpp__ListType_impl() with the same arguments
   implements T_reserve() for basic list allocation. Cleanup, alas, is
   MT-dependent.
*/
#define cmpp__ListType_decl(T,MT)                      \
  struct T {                                           \
    MT * list;                                         \
    cmpp_size_t n;                                     \
    cmpp_size_t nAlloc;                                \
  };                                                   \
  typedef struct T T;                                  \
  int T ## _reserve(cmpp *pp, T *li, cmpp_size_t min)
#define CMPP__MAX(X,Y) ((X)<=(Y) ? (X) : (Y))
#define cmpp__ListType_impl(T,MT)                             \
  int T ## _reserve(cmpp *pp,struct T *li, cmpp_size_t min) { \
    return cmpp_array_reserve(pp, (void**)&li->list, min,     \
                              &li->nAlloc, sizeof(MT));       \
  }

#define cmpp__LIST_T_empty_m {.list=0,.n=0,.nAlloc=0}

/**
   A dynamically-allocated list of CmppLvl objects.
*/
cmpp__ListType_decl(CmppLvlList,CmppLvl*);
#define CmppLvlList_empty_m cmpp__LIST_T_empty_m
CMPP_PRIVATE CmppLvl * CmppLvl_push(cmpp_dx *dx);
CMPP_PRIVATE CmppLvl * CmppLvl_get(cmpp_dx const *dx);
CMPP_PRIVATE void CmppLvl_pop(cmpp_dx *dx, CmppLvl *lvl);
CMPP_PRIVATE void CmppLvl_elide(CmppLvl *lvl, bool on);
CMPP_PRIVATE bool CmppLvl_is_eliding(CmppLvl const *lvl);
CMPP_PRIVATE bool cmpp_dx_is_eliding(cmpp_dx const *dx);

/**
   A dynamically-allocated list of cmpp_b objects.
*/
cmpp__ListType_decl(cmpp_b_list,cmpp_b*);
#define cmpp_b_list_empty_m cmpp__LIST_T_empty_m
extern const cmpp_b_list cmpp_b_list_empty;
CMPP_PRIVATE void cmpp_b_list_cleanup(cmpp_b_list *li);
//CMPP_PRIVATE cmpp_b * cmpp_b_list_push(cmpp_b_list *li);
//CMPP_PRIVATE void cmpp_b_list_reuse(cmpp_b_list *li);
/**
   cmpp_b_list sorting policies. NULL entries must
   always sort last.
*/
enum cmpp_b_list_e {
  cmpp_b_list_UNSORTED,
  /* Smallest first. */
  cmpp_b_list_ASC,
  /* Largest first. */
  cmpp_b_list_DESC
};

/**
   A dynamically-allocated list of cmpp_arg objects. Used by
   cmmp_args.
*/
cmpp__ListType_decl(CmppArgList,cmpp_arg);
#define CmppArgList_empty_m cmpp__LIST_T_empty_m

/** Allocate a new arg, owned by li, and return it (cleanly zeroed
    out). Returns NULL and updates pp->err on error. */
CMPP_PRIVATE cmpp_arg * CmppArgList_append(cmpp *pp, CmppArgList *li);

/**
   The internal part of the cmpp_args interface.
*/
struct cmpp_args_pimpl {
  /**
     We need(?) a (cmpp*) here for finalization/recycling purposes.
  */
  cmpp *pp;
  bool isCall;
  /**
     Next entry in the free-list.
  */
  cmpp_args_pimpl * nextFree;
  /** Version 3 of the real args memory.  */
  CmppArgList argli;
  /**
     cmpp_args_parse() copies each argument's bytes into here,
     each one NUL-terminated.
  */
  cmpp_b argOut;
};
#define cmpp_args_pimpl_empty_m { \
  .pp = 0,                        \
  .isCall = false,                \
  .nextFree = 0,                  \
  .argli = CmppArgList_empty_m,   \
  .argOut = cmpp_b_empty_m        \
}
extern const cmpp_args_pimpl cmpp_args_pimpl_empty;
void cmpp_args_pimpl_cleanup(cmpp_args_pimpl *p);

/**
   The internal part of the cmpp_dx interface.
*/
struct cmpp_dx_pimpl {
  /** Start of input. */
  unsigned const char * zBegin;
  /** One-after-the-end of input. */
  unsigned const char * zEnd;
  /**
     Current input position. Generally speaking, only
     cmpp_dx_delim_search() should update this, but it turns out that
     the ability to rewind the input is necessary for looping
     constructs, like #query, when they want to be able to include
     other directives in their bodies.
  */
  cmpp_dx_pos pos;
  /**
     Currently input line.
   */
  CmppDLine dline;
  /** Number of active #savepoints. */
  unsigned nSavepoint;
  /** Current directive's args. */
  cmpp_args args;
  /**
     A stack of state used by #if and friends to inform the innards
     that they must not generate output. This is largely historical
     and could have been done differently had this code started as a
     library instead of a monolithic app.

     TODO is to figure out how best to move this state completely into
     the #if handler, rather than fiddle with this all throughout the
     processing. We could maybe move this stack into CmppIfState?
  */
  CmppLvlList dxLvl;
  struct {
    /**
       A copy of this->d's input line which gets translated
       slightly from its native form for futher processing.
    */
    cmpp_b line;
    /**
       Holds the semi-raw input line, stripped only of backslash-escaped
       newlines and leading spaces. This is primarily for debug output
       but also for custom arg parsing for some directives.
    */
    cmpp_b argsRaw;
  } buf;

  /**
     Record IDs for/from cmpp_[un]define_shadow().
  */
  struct {
    /** ID for __FILE__. */
    int64_t sidFile;
    /** Rowid for #include path entry. */
    int64_t ridInclPath;
  } shadow;

  struct {
    /**
       Set when we're searching for directives so that we know whether
       cmpp_out_expand() should count newlines.
     */
    unsigned short countLines;
    /**
       True if the next directive is the start of a [call].
    */
    bool nextIsCall;
  } flags;
};
/**
   Initializes or resets a. Returns non-0 on OOM.
*/
CMPP_PRIVATE int cmpp_args__init(cmpp *pp, cmpp_args *a);

/**
   If a has state then it's recycled for reuse, else this zeroes out a
   except for a->pimpl, which is retained (but may be NULL).
*/
CMPP_PRIVATE void cmpp_args_reuse(cmpp_args *a);

#define cmpp_dx_pimpl_empty_m {  \
  .zBegin=0, .zEnd=0,            \
  .pos=cmpp_dx_pos_empty_m,     \
  .dline=CmppDLine_empty_m,      \
  .nSavepoint=0,                 \
  .args = cmpp_args_empty_m,     \
  .dxLvl = CmppLvlList_empty_m,  \
  .buf = {                       \
    cmpp_b_empty_m,              \
    cmpp_b_empty_m               \
  },                             \
  .shadow = {                    \
    .sidFile = 0,                \
    .ridInclPath = 0             \
  },                             \
  .flags = {                     \
    .countLines = 0,             \
    .nextIsCall = false          \
  }                              \
}

/**
   A level of indirection for CmppDList in order to be able to
   manage ownership of their name (string) lifetimes.
*/
struct CmppDList_entry {
  /** this->d.name.z points to this, which is owned by the CmppDList
      which manages this object. */
  char * zName;
  /* Potential TODO: move d->id into here. That doesn't eliminate our
     dependency on it, though. */
  cmpp_d d;
  //cmpp_d_reg reg;
};
typedef struct CmppDList_entry CmppDList_entry;
#define CmppDList_entry_empty_m {0,cmpp_d_empty_m/*,cmpp_d_reg_empty_m*/}

/**
   A dynamically-allocated list of cmpp_arg objects. Used by CmmpArgs.
*/
cmpp__ListType_decl(CmppDList,CmppDList_entry*);
#define CmppDList_empty_m cmpp__LIST_T_empty_m

/**
   State for keeping track of DLL handles, a.k.a. shared-object
   handles, a.k.a. "soh".

   Instances of this must be either cleanly initialized by bitwise
   copying CmppSohList_empty, memset() (or equivalent) them to 0, or
   allocating them with CmppSohList_new().
*/
cmpp__ListType_decl(CmppSohList,void*);
#define CmppSohList_empty_m cmpp__LIST_T_empty_m

/**
   Closes all handles which have been CmppSohList_append()ed to soli
   and frees any memory it owns, but does not free soli (which might
   be stack-allocated or part of another struct).

   Special case: if built without DLL-closing support then this
   is no-op.
*/
CMPP_PRIVATE void CmppSohList_close(CmppSohList *soli);

/**
   Operators and operator policies for use with X=Y-format keys.  This
   is legacy stuff, actually, but some of the #define management still
   needs it.
*/
#define CmppKvp_op_map(E)  \
  E(none,"")               \
  E(eq1,"=")

enum CmppKvp_op_e {
#define E(N,S) CmppKvp_op_ ## N,
  CmppKvp_op_map(E)
#undef E
};
typedef enum CmppKvp_op_e CmppKvp_op_e;

/**
   Result type for CmppKvp_parse().
*/
struct CmppKvp {
  /* Key part of the kvp. */
  CmppSnippet k;
  /* Key part of the kvp. Might be empty. */
  CmppSnippet v;
  /* Operator part of kvp, if any. */
  CmppKvp_op_e op;
};
typedef struct CmppKvp CmppKvp;
extern const CmppKvp CmppKvp_empty;

/**
   Parses X or X=Y into p. Sets pp's error state on error.

   If nKey is negative then strlen() is used to calculate it.

   The third argument specifies whether/how to permit/treat the '='
   part of X=Y.
*/
CMPP_PRIVATE int CmppKvp_parse(cmpp *pp, CmppKvp * p,
                               unsigned char const *zKey,
                               cmpp_ssize_t nKey,
                               CmppKvp_op_e opPolicy);


/**
   Stack of POD values. Intended for use with cmpp at-token and
   undefined key policies.
*/
#define cmpp__PodList_decl(ST,ET)         \
  struct ST {                              \
    /* current stack index */              \
    cmpp_size_t n;                         \
    cmpp_size_t na;                        \
    ET * stack;                            \
  }; typedef struct ST ST;                 \
  void ST ## _wipe(ST * s, ET v);          \
  int ST ## _push(cmpp *pp, ST * s, ET v); \
  void ST ## _set(ST * s, ET v);           \
  void ST ## _finalize(ST * s);            \
  void ST ## _pop(ST *s);                  \
  int ST ## _reserve(cmpp *, ST *, cmpp_size_t min)

#define cmpp__PodList_impl(ST,ET)                               \
  void ST ## _wipe(ST * const s, ET v){                          \
    if( s->na ) memset(s->stack, (int)v, sizeof(ET)*s->na);      \
    s->n = 0;                                                    \
  }                                                              \
  int ST ## _reserve(cmpp * const pp, ST * const s,              \
                     cmpp_size_t min){                           \
    return cmpp_array_reserve(pp, (void**)&s->stack, min>0       \
                              ? min : (s->n                      \
                                     ? (s->n==s->na-1            \
                                        ? s->na*2 : s->n+1)      \
                                     : 8),                       \
                              &s->na, sizeof(ET));               \
  }                                                              \
  int ST ## _push(cmpp * const pp, ST * s, ET v){                \
    if( 0== ST ## _reserve(pp, s, 0) ) s->stack[++s->n] = v;     \
    return ppCode;                                               \
  }                                                              \
  void ST ## _set(ST * s, ET v){                                 \
    assert(s->n);                                                \
    if( 0== ST ## _reserve(NULL, s, 0) ){                        \
      s->stack[s->n] = v;                                        \
    }                                                            \
  }                                                              \
  ET ST ## _get(ST const * const s){                             \
    assert(s->na && s->na >=s->n);                               \
    return s->stack[s->n];                                       \
  }                                                              \
  void ST ## _pop(ST *s){                                        \
    assert(s->n);                                                \
    if(s->n) --s->n;                                             \
  }                                                              \
  void ST ## _finalize(ST *s){                                   \
    cmpp_mfree(s->stack);                                        \
    s->stack = NULL;                                             \
    s->n = s->na = 0;                                            \
  }

cmpp__PodList_decl(PodList__atpol,cmpp_atpol_e);
cmpp__PodList_decl(PodList__unpol,cmpp_unpol_e);

#define cmpp__epol(PP,WHICH) (PP)->pimpl->policy.WHICH
#define cmpp__policy(PP,WHICH) \
  cmpp__epol(PP,WHICH).stack[cmpp__epol(PP,WHICH).n]

/**
   A "delimiter" object. That is, the "#" referred to in the libcmpp
   docs. It's also outfitted for a second delimiter so that it can be
   used for the opening/closing delimiters of @tokens@.
*/
struct cmpp__delim {
  /**
     Bytes of the directive delimiter/prefix or the @token@ opening
     delimiter. Owned elsewhere but often points at this->zOwns.
  */
  CmppSnippet open;
  /**
     Closing @token@ delimiter. This has no meaning for the directive
     delimiter.
  */
  CmppSnippet close;
  /**
     Memory, owned by this object, for this->open and this->close.  In
     the latter case, it's one string with both delimiters encoded in
     it.
  */
  unsigned char * zOwns;
};
typedef struct cmpp__delim cmpp__delim;
#define cmpp__delim_empty_m {              \
  .open={                                  \
    .z=(unsigned char*)CMPP_DEFAULT_DELIM, \
    .n=sizeof(CMPP_DEFAULT_DELIM)-1        \
  },                                       \
  .close=CmppSnippet_empty_m,              \
  .zOwns=0                                 \
}

extern const cmpp__delim cmpp__delim_empty;
void cmpp__delim_cleanup(cmpp__delim *d);

/**
   A dynamically-allocated list of cmpp__delim objects.
*/
cmpp__ListType_decl(cmpp__delim_list,cmpp__delim);
#define cmpp__delim_list_empty_m {0,0,0}
extern const cmpp__delim_list cmpp__delim_list_empty;

CMPP_PRIVATE cmpp__delim * cmpp__delim_list_push(cmpp *pp, cmpp__delim_list *li);
static inline cmpp__delim * cmpp__delim_list_get(cmpp__delim_list const *li){
  return li->n ? li->list+(li->n-1) : NULL;
}
static inline void cmpp__delim_list_pop(cmpp__delim_list *li){
  assert(li->n);
  if( li->n ) cmpp__delim_cleanup(li->list + --li->n);
}
static inline void cmpp__delim_list_reuse(cmpp__delim_list *li){
  while( li->n ) cmpp__delim_cleanup(li->list + --li->n);
}

/**
   An untested experiment: an output buffer proxy. Integrating this
   fully would require some surgery, but it might also inspire me to
   do the same with input and stream it rather than slurp it all at
   once.
*/
#define CMPP__OBUF 0

typedef struct cmpp__obuf cmpp__obuf;
#if CMPP__OBUF
/**
   An untested experiment.
*/
struct cmpp__obuf {
  /** Start of the output buffer. */
  unsigned char * begin;
  /** One-after-the-end of this->begin. Virtual EOF. */
  unsigned char const * end;
  /** Current write position. Must initially be
      this->begin. */
  unsigned char * cursor;
  /**
     True if this object owns this->begin, which must have been
     allocated using cmpp_malloc() or cmpp_realloc().
  */
  bool ownsMemory;
  /** Propagating result code. */
  int rc;
  /**
     The output channel to buffer for. Flushing
  */
  cmpp_outputer dest;
};

#define cmpp__obuf_empty_m {                      \
  .begin=0, .end=0, .cursor=0, .ownsMemory=false, \
  .rc=0, .dest=cmpp_outputer_empty_m              \
}
extern const cmpp__obuf cmpp__obuf_empty;
extern const cmpp_outputer cmpp_outputer_obuf;
#endif /* CMPP__OBUF */
/**
   The main public-API context type for this library.
*/
struct cmpp_pimpl {
  /* Internal workhorse. */
  struct {
    sqlite3 * dbh;
    /**
       Optional filename. Memory is owned by this object.
    */
    char * zName;
  } db;
  /**
     Current directive context. It's const, primarily to help protect
     cmpp_dx_f()'s from inadvertent side effects of changes which
     lower-level APIs might make to it. Maybe it shouldn't be: if it
     were not then we could update dx->zDelim from
     cmpp__delimiter_set().
  */
  cmpp_dx const * dx;
  /* Output channel. */
  cmpp_outputer out;
  /**
     Delimiters version 2.
   */
  struct {
    /**
       Directive delimiter.
    */
    cmpp__delim_list d;
    /**
       @token@ delimiters.
    */
    cmpp__delim_list at;
  } delim;
  struct {

#define CMPP__SEL_V_FROM(N)            \
    "(SELECT v FROM " CMPP__DB_MAIN_NAME ".vdef WHERE k=?" #N \
    " ORDER BY source LIMIT 1)"

    /**
       One entry for each distinct query used by cmpp: E(X,SQL), where
       X is the member's name and SQL is its SQL.
    */
#define CMPP_SAVEPOINT_NAME "_cmpp_"
#define CmppStmt_map(E)                                \
    E(sdefIns,                                         \
      "INSERT INTO "                                   \
      CMPP__DB_MAIN_NAME ".sdef"                       \
      "(t,k,v) VALUES(?1,?2,?3) RETURNING id")         \
    E(defIns,                                          \
      "INSERT OR REPLACE INTO "                        \
      CMPP__DB_MAIN_NAME ".def"                        \
      "(t,k,v) VALUES(?1,?2,?3)")                      \
    E(defDel,                                          \
      "DELETE FROM "                                   \
      CMPP__DB_MAIN_NAME ".def"                        \
      " WHERE k GLOB ?1")                              \
    E(sdefDel,                                         \
      "DELETE FROM "                                   \
      CMPP__DB_MAIN_NAME ".sdef"                       \
      " WHERE k=?1 AND id>=?2")                        \
    E(defHas,                                          \
      "SELECT 1 FROM "                                 \
      CMPP__DB_MAIN_NAME ".vdef"                       \
      " WHERE k = ?1")                                 \
    E(defGet,                                          \
      "SELECT source,t,k,v FROM "                      \
      CMPP__DB_MAIN_NAME ".vdef"                       \
      " WHERE k = ?1 ORDER BY source LIMIT 1")         \
    E(defGetBool,                                      \
      "SELECT cmpp_truthy(v) FROM "                    \
      CMPP__DB_MAIN_NAME ".vdef"                       \
      " WHERE k = ?1"                                  \
      " ORDER BY source LIMIT 1")                      \
    E(defGetInt,                                       \
      "SELECT CAST(v AS INTEGER)"                      \
      " FROM " CMPP__DB_MAIN_NAME ".vdef"              \
      " WHERE k = ?1"                                  \
      " ORDER BY source LIMIT 1")                      \
    E(defSelAll, "SELECT t,k,v"                        \
      " FROM " CMPP__DB_MAIN_NAME ".vdef"              \
      " ORDER BY source, k")                           \
    E(inclIns," INSERT OR FAIL INTO "                  \
      CMPP__DB_MAIN_NAME ".incl("                      \
      " file,srcFile, srcLine"                         \
      ") VALUES(?,?,?)")                               \
    E(inclDel, "DELETE FROM "                          \
      CMPP__DB_MAIN_NAME ".incl WHERE file=?")         \
    E(inclHas, "SELECT 1 FROM "                        \
      CMPP__DB_MAIN_NAME ".incl WHERE file=?")         \
    E(inclPathAdd, "INSERT INTO "                      \
      CMPP__DB_MAIN_NAME ".inclpath(priority,dir) "    \
      "VALUES(coalesce(?1,0),?2) "                     \
      "ON CONFLICT DO NOTHING "                        \
      "RETURNING rowid /*xlates to 0 on conflict*/")   \
    E(inclPathRmId, "DELETE FROM "                     \
      CMPP__DB_MAIN_NAME ".inclpath WHERE rowid=?1 "   \
      "RETURNING rowid")                               \
    E(inclSearch,                                      \
      "SELECT ?1 fn WHERE cmpp_file_exists(fn) "       \
      "UNION ALL SELECT fn FROM ("                     \
      " SELECT replace(dir||'/'||?1, '//','/') AS fn " \
      " FROM " CMPP__DB_MAIN_NAME ".inclpath"          \
      " WHERE cmpp_file_exists(fn) "                   \
      " ORDER BY priority DESC, rowid LIMIT 1"         \
      ")")                                             \
    E(cmpVV, "SELECT cmpp_compare(?1,?2)")             \
    E(cmpDV,                                           \
      "SELECT cmpp_compare("                           \
      CMPP__SEL_V_FROM(1) ", ?2"                       \
      ")")                                             \
    E(cmpVD,                                           \
      "SELECT cmpp_compare("                           \
      "?1," CMPP__SEL_V_FROM(2)                        \
      ")")                                             \
    E(cmpDD,                                           \
      "SELECT cmpp_compare("                           \
      CMPP__SEL_V_FROM(1)                              \
      ","                                              \
      CMPP__SEL_V_FROM(2)                              \
      ")")                                             \
    E(dbAttach,                                        \
      "ATTACH ?1 AS ?2")                               \
    E(dbDetach,                                        \
      "DETACH ?1")                                     \
    E(spBegin, "SAVEPOINT " CMPP_SAVEPOINT_NAME)       \
    E(spRollback,                                      \
      "ROLLBACK TO SAVEPOINT " CMPP_SAVEPOINT_NAME)    \
    E(spRelease,                                       \
      "RELEASE SAVEPOINT " CMPP_SAVEPOINT_NAME)        \
    E(insTtype,                                        \
      "INSERT INTO " CMPP__DB_MAIN_NAME ".ttype"       \
      "(t,n,s) VALUES(?1,?2,?3)")                      \
    E(selPathSearch,                                   \
   /* sqlite.org/forum/forumpost/840c98a8e87c2207 */   \
      "WITH path(basename, sep, ext, path) AS (\n"     \
      "  select\n"                                     \
      "  ?1 basename,\n"                               \
      "  ?2 sep,\n"                                    \
      "  ?3 ext,\n"                                    \
      "  ?4 path\n"                                    \
      "),\n"                                           \
      "pathsplit(i, l, c, r) AS (\n"                   \
      "--   i = sequential ID\n"                       \
      "--   l = Length remaining\n"                    \
      "--   c = text remaining\n"                      \
      "--   r = current unpacked value\n"              \
      "  SELECT 1,\n"                                  \
      "         length(p.path)+length(p.sep),\n"       \
      "         p.path||p.sep, ''\n"                   \
      "  FROM path p\n"                                \
      "  UNION ALL\n"                                  \
      "  SELECT i+1, instr( c, p.sep ) l,\n"           \
      "    substr( c, instr( c, p.sep ) + 1) c,\n"     \
      "    trim( substr( c, 1,\n"                      \
      "            instr( c, p.sep) - 1) ) r\n"        \
      "  FROM pathsplit, path p\n"                     \
      "  WHERE l > 0\n"                                \
      "),\n"                                           \
      "thefile (f) AS (\n"                             \
      "  select basename f FROM path\n"                \
      "  union all\n"                                  \
      "  select basename||ext\n"                       \
      "  from path where ext is not null\n"            \
      ")\n"                                            \
      "select 0 i, replace(f,'//','/') AS fn\n"        \
      "from thefile where cmpp_file_exists(fn)\n"      \
      "union all\n"                                    \
      "select i, replace(r||'/'||f,'//','/') fn\n"     \
      "from pathsplit, thefile\n"                      \
      "where r<>'' and cmpp_file_exists(fn)\n"         \
      "order by i\n"                                   \
      "limit 1;")

    /* trivia: selPathSearch (^^^) was generated using
       cmpp's #c-code directive. */

#define E(N,S) sqlite3_stmt * N;
    CmppStmt_map(E)
#undef E

  } stmt;

  /** Error state. */
  struct {
    /** Result code. */
    int code;
    /** Error string owned by this object. */
    char * zMsg;
    /** Either this->zMsg or an external error string. */
    char const * zMsgC;
  } err;

  /** State for SQL tracing. */
  struct {
    bool expandSql;
    cmpp_size_t counter;
    cmpp_outputer out;
  } sqlTrace;

  struct {
    /** If set properly, cmpp_dtor() will free this
        object, else it will not. */
    void const * allocStamp;
    /**
       How many dirs we believe are in the #include search list. We
       only do this for the sake of the historical "if no path was
       added, assume '.'" behavior. This really ought to go away.
    */
    unsigned nIncludeDir;
    /**
       The current depth of cmpp_process_string() calls. We do this so
       the directory part of #include'd files can get added to the
       #include path and be given a higher priority than previous
       include path entries in the stack.
    */
    int nDxDepth;
    /* Number of active #savepoints. */
    unsigned nSavepoint;
    /* If >0, enables certain debugging output. */
    char doDebug;
    /* If true, chomp() files read via -Fx=file. */
    unsigned char chompF;
    /* Flags passed to cmpp_ctor(). */
    cmpp_flag32_t newFlags;

    /**
       An ugly hack for getting cmpp_d_register() to get
       syntactically-illegal directive names, like "@policy",
       to register.
     */
    bool isInternalDirectiveReg;
    /**
       True if the next directive is the start of a [call]. This is
       used for:

       1) To set cmpp_dx::isCall, which is useful in certain
          directives.

       2) So that the cmpp_dx object created for the call can inherit
          the line number from its parent context. That's significant
          for error reporting.

       3) So that #2's cmpp_dx object can communicate that flag to
          cmpp_dx_next().
    */
    bool nextIsCall;

    /**
       True until the cmpp's (sometimes) lazy init has been run. This
       is essentially a kludge to work around a wrench cmpp_reset()
       throws into cmpp state. Maybe we should just remove
       cmpp_reset() from the interface, since error recovery in this
       context is not really a thing.
    */
    bool needsLazyInit;
  } flags;

  /** Policies. */
  struct {
    /** @token@-parsing policy. */
    PodList__atpol at;
    /** Policy towards referencing undefined symbols. */
    PodList__unpol un;
  } policy;

  /**
     Directive state.
  */
  struct {
    /**
       Runtime-installed directives.
    */
    CmppDList list;
    /**
       Directive autoloader/auto-registerer.
    */
    cmpp_d_autoloader autoload;
  } d;

  struct {
    /**
       List of DLL handles opened by cmpp_module_extract().
    */
    CmppSohList sohList;
    /**
       Search path for DLLs, delimited by this->pathSep.
    */
    cmpp_b path;
    /**
       File extension for DLLs.
    */
    char const * soExt;
    /** Separator char for this->path. */
    char pathSep;
  } mod;

  struct {
    /**
       Buffer cache. Managed by cmpp_b_borrow() and cmpp_b_return().
    */
    cmpp_b_list buf;
    /** How/whether this->list is sorted. */
    enum cmpp_b_list_e bufSort;
    /**
       Head of the free-list.
     */
    cmpp_args_pimpl * argPimpl;
  } recycler;
};

/** IDs Distinct for each cmpp::stmt member. */
enum CmppStmt_e {
  CmppStmt_none = 0,
#define E(N,S) CmppStmt_ ## N,
  CmppStmt_map(E)
#undef E
};

static inline cmpp__delim * cmpp__pp_delim(cmpp const *pp){
  return cmpp__delim_list_get(&pp->pimpl->delim.d);
}
static inline char const * cmpp__pp_zdelim(cmpp const *pp){
  cmpp__delim const * const d = cmpp__pp_delim(pp);
  return d ? (char const *)d->open.z : NULL;
}
#define cmpp__dx_delim(DX) cmpp__pp_delim(DX->pp)
#define cmpp__dx_zdelim(DX) cmpp__pp_zdelim(DX->pp)

/**
   Emit [z,(char*)z+n) to the given output channel if
   (A) pOut->out is not NULL and (B) pp has no error state and (C)
   n>0.  On error, pp's error state is updated. Returns pp->err.code.

   Skip level is not honored.
*/
CMPP_PRIVATE int cmpp__out2(cmpp *pp, cmpp_outputer *pOut, void const *z, cmpp_size_t n);

CMPP_PRIVATE void cmpp__err_clear(cmpp *pp);


/**
   Initialize pp->db.dbh. If it's already open or ppCode!=0
   then ppCode is returned.
*/
int cmpp__db_init(cmpp *pp);

/**
  Returns the pp->pimpl->stmt.X corresponding to `which`, initializing it if
  needed. If it returns NULL then either this was called when pp has
  its error state set or this function will set the error state.

  If prepEvenIfErr is true then the ppCode check is bypassed, but it
  will still fail if pp->pimpl->db is not opened or if the preparation itself
  fails.
*/
sqlite3_stmt * cmpp__stmt(cmpp * pp, enum CmppStmt_e which,
                          bool prepEvenIfErr);

/**
   Reminder to self: this must return an SQLITE_... code, not a
   CMPP_RC_... code.

   On success it returns 0, SQLITE_ROW, or SQLITE_DONE. On error it
   returns another non-0 SQLITE_... code and updates pp->pimpl->err.

   This is a no-op if called when pp has an error set, returning
   SQLITE_ERROR.

   If resetIt is true, q is passed to cmpp__stmt_reset(), else the
   caller must eventually reset it.
*/
int cmpp__step(cmpp * const pp, sqlite3_stmt * const q, bool resetIt);

/** Resets and clear bindings from q (if q is not NULL). */
void cmpp__stmt_reset(sqlite3_stmt * const q);

/**
   Expects an SQLite result value. If it's SQLITE_OK, SQLITE_ROW, or
   SQLITE_DONE, 0 is returned without side-effects, otherwise pp->err
   is updated with pp->db's current error state. zMsgSuffix is an
   optional prefix for the error message.
*/
int cmpp__db_rc(cmpp *pp, int dbRc, char const *zMsgSuffix);

/* Proxy for sqlite3_bind_int64(). */
int cmpp__bind_int(cmpp *pp, sqlite3_stmt *pStmt, int col, int64_t val);

/**
   Proxy for cmpp__bind_text() which encodes val as a string.

   For queries which compare values, it's important that they all have
   the same type, so some cases where we might want an int needs to be
   bound as text instead. See #query for one such case.
*/
int cmpp__bind_int_text(cmpp *pp, sqlite3_stmt *pStmt, int col, int64_t val);

/* Proxy for sqlite3_bind_null(). */
int cmpp__bind_null(cmpp *pp, sqlite3_stmt *pStmt, int col);

/* Proxy for sqlite3_bind_text() which updates pp->err on error. */
int cmpp__bind_text(cmpp *pp,sqlite3_stmt *pStmt, int col,
                    unsigned const char * zStr);

/* Proxy for sqlite3_bind_text() which updates pp->err on error. */
int cmpp__bind_textn(cmpp *pp,sqlite3_stmt *pStmt, int col,
                     unsigned const char *zStr, cmpp_ssize_t len);

/**
   Adds zDir to the include path, using the given priority value (use
   0 except for the implicit cwd path which #include should (but does
   not yet) set). If pRowid is not NULL then *pRowid gets set to
   either 0 (if zDir was already in the path) or the row id of the
   newly-inserted record, which can later be used to delete just that
   entry.

   If this returns a non-zero value via pRowid, the caller is
   obligated to eventually pass *pRowid to cmpp__include_dir_rm_id(),
   even if pp is in an error state.

   TODO: normalize zDir (at least remove trailing slashes) before
   insertion to avoid that both a/b and a/b/ get be inserted.
*/
int cmpp__include_dir_add(cmpp *pp, const char * zDir, int priority, int64_t * pRowid);

/**
   Deletes the include path entry with the given rowid. This will make
   make the attempt even if pp is in an error state but also retains
   any existing error rather than overwriting it if this operation
   somehow fails. Returns pp's error code.

   It is not an error for the given entry to not exist.
*/
int cmpp__include_dir_rm_id(cmpp *pp, int64_t pRowid);


#if 0
/**
   Proxy for sqlite3_bind_text(). It uses sqlite3_str_vappendf() so
   supports all of its formatting options.
*/
int cmpp__bind_textv(cmpp*pp, sqlite3_stmt *pStmt, int col,
                     const char * zFmt, ...);
#endif

/**
   Proxy for sqlite3_str_finish() which updates pp's error state if s
   has error state. Returns s's string on success and NULL on
   error. The returned string must eventualy be passed to
   cmpp_mfree(). It also, it turns out, returns NULL if s is empty, so
   callers must check pp->err to see if NULL is an error.

   If n is not NULL then on success it is set to the byte length of
   the returned string.
*/
char * cmpp_str_finish(cmpp *pp, sqlite3_str *s, int * n);

/**
   Searches pp's list of directives. If found, return it else return
   NULL. See cmpp__d_search3().
*/
cmpp_d const * cmpp__d_search(cmpp *pp, const char *zName);

/**
   Flags for use with the final argument to
   cmpp__d_search3().
*/
enum cmpp__d_search3_e {
  /** Internal delayed-registered directives. */
  cmpp__d_search3_F_DELAYED    = 0x01,
  /** Directive autoloader. */
  cmpp__d_search3_F_AUTOLOADER = 0x02,
  /** Search for a DLL. */
  cmpp__d_search3_F_DLL        = 0x04,
  /** Options which do not trigger DLL lookup. */
  cmpp__d_search3_F_NO_DLL    = 0
  | cmpp__d_search3_F_DELAYED
  | cmpp__d_search3_F_AUTOLOADER,
  /** All options. */
  cmpp__d_search3_F_ALL        = 0
  | cmpp__d_search3_F_DELAYED
  | cmpp__d_search3_F_AUTOLOADER
  | cmpp__d_search3_F_DLL
};

/**
   Like cmpp__d_search() but if no match is found then it will search
   through its other options and, if found, register it.

   The final argument specifies where to search. cmpp__d_search()
   always checked first. After that, depending on "what", the search
   order is: (1) internal delayed-load modules, (2) autoloader, (3)
   DLL.

   This may update pp's error state, in which case it will return
   NULL.
*/
cmpp_d const * cmpp__d_search3(cmpp *pp, const char *zName,
                               cmpp_flag32_t what);

/**
   Sets pp's error state (A) if it's not set already and (B) if
   !cmpp_is_legal_key(zKey). If permitEqualSign is true then '=' is
   legal (to support legacy CLI pieces). Returns ppCode.
*/
int cmpp__legal_key_check(cmpp *pp, unsigned char const *zKey,
                          cmpp_ssize_t nKey,
                          bool permitEqualSign);

/**
   Appends DLL handle soh to soli. Returns 0 on success, CMPP_RC_OOM
   on error. If pp is not NULL then its error state is updated as
   well.

   Results are undefined if soli was not cleanly initialized (by
   copying CmppSohList_empty or using CmppSohList_new()).

   Special case: if built without DLL-closing support, this is a no-op
   returning 0.
*/
int CmppSohList_append(cmpp *pp, CmppSohList *soli, void *soh);

/** True if arg is of type cmpp_TT_Word and it looks like it
    _might_ be a filename or flag argument. Might. */
bool cmpp__arg_wordIsPathOrFlag(cmpp_arg const * const arg);

/**
   Helper for #query and friends. Binds aVal's value to column bindNdx
   of q.

   It expands cmpp_TT_StringAt and cmpp_TT_Word aVal. cmpp_TT_String
   and cmpp_TT_Int are bound as strings. A cmpp_TT_GroupParen aVal is
   eval'ed as an integer and that int gets bound as a string.

   This function strictly binds everything as strings, even if the
   value being bound is of type cmpp_TT_Int or cmpp_TT_GroupParen, so that
   comparison queries will work as expected.

   Returns ppCode.
*/
int cmpp__bind_arg(cmpp_dx * const dx, sqlite3_stmt * q,
                   int bindNdx, cmpp_arg const * aVal);

/**
   Helper for #query's bind X part, where aGroup is that X.

   A wrapper around cmpp__bind_arg(). Requires aGroup->ttype to be
   either cmpp_TT_GroupBrace or cmpp_TT_GroupSquiggly and to have
   non-empty content. cmpp_TT_GroupBrace treats it as a list of values
   to bind. cmpp_TT_GroupSquiggly expects sets of 3 tokens per stmt
   column in one of these forms:

   :bindName -> value
   $bindName -> value

   Each LHS refers to a same-named binding in q's SQL, including the
   ':' or '$' prefix. (SQLite supports an '@' prefix but we don't
   allow it here to avoid confusion with cmpp_TT_StringAt tokens.)

   Each bound value is passed to cmpp__bind_arg() for processing.

   On success, each aGroup entry is bound to q. On error q's state is
   unspecified. Returns ppCode.

   See cmpp__bind_arg() for notes about the bind data type.
*/
int cmpp__bind_group(cmpp_dx * const dx, sqlite3_stmt * const q,
                     cmpp_arg const * const aGroup);

/**
   Returns true if the given key is already in the `#define` list,
   else false. Sets pp's error state on db error.

   nName is the length of the key part of zName (which might have
   a following =y part. If it's negative, strlen() is used to
   calculate it.
*/
int cmpp_has(cmpp *pp, const char * zName, cmpp_ssize_t nName);

/**
   Returns true if the given key is already in the `#define` list, and
   it has a truthy value (is not empty and not equal to '0'), else
   false. If zName contains an '=' then only the part preceding that
   is used as the key.

   nName is the length of zName, or <0 to use strlen() to figure
   it out.

   Updates ppCode on error.
*/
int cmpp__get_bool(cmpp *pp, unsigned const char * zName,
                   cmpp_ssize_t nName);

/**
   Fetches the given define. If found, sets *pOut to it, else pOut is
   unmodified. Returns pp->err.code. If bRequire is true and no entry
   is found p->err.code is updated.
*/
int cmpp__get_int(cmpp *pp, unsigned const char * zName,
                  cmpp_ssize_t nName, int *pOut);

/**
   Searches for a define where (k GLOB zName). If one is found, a copy
   of it is assigned to *zVal (the caller must eventually db_free()
   it), *nVal (if nVal is not NULL) is assigned its strlen, and
   returns non-0. If no match is found, 0 is returned and neither
   *zVal nor *nVal are modified. If more than one result matches, a
   fatal error is triggered.

   It is legal for *zVal to be NULL (and *nVal to be 0) if it returns
   non-0. That just means that the key was defined with no value part.

   Fixme: return 0 on success and set output *gotOne=0|1.
*/
int cmpp__get(cmpp *pp, unsigned const char * zName,
              cmpp_ssize_t nName,
              unsigned char **zVal, unsigned int *nVal);

/**
   Like cmp__get() but puts its output in os.
*/
int cmpp__get_b(cmpp *pp, unsigned const char * zName,
                cmpp_ssize_t nName, cmpp_b * os,
                bool enforceUndefPolicy);


/**
   Helper for #query and friends.

   It expects that q has just been stepped.  For each column in the
   row, it sets a define named after the column.  If q has row data
   then the values come from there. If q has no row then: if
   defineIfNoRow is true then it defines each column name to an empty
   value else it defines nothing.
*/
int cmpp__define_from_row(cmpp * const pp, sqlite3_stmt * const q,
                          bool defineIfNoRow);

/** Start a new savepoint for dx. */
int cmpp__dx_sp_begin(cmpp_dx * const dx);
/** Commit and close dx's current savepoint. */
int cmpp__dx_sp_commit(cmpp_dx * const dx);
/** Roll back and close dx's current savepoint. */
int cmpp__dx_sp_rollback(cmpp_dx * const dx);

/**
   Append's dx's file/line information to sstr. It returns void
   because that's how sqlite3_str_appendf() and friends work.
*/
void cmpp__dx_append_script_info(cmpp_dx const * dx,
                                 sqlite3_str * sstr);

/**
   If zName matches one of the delayed-load directives, that directive
   is registered and 0 is returned. CMPP_RC_NO_DIRECTIVE is returned if
   no match is found, but pp's error state is not updated in that
   case. If a match is found and registration fails, that result code
   will propagate via pp.
*/
int cmpp__d_delayed_load(cmpp *pp, char const *zName);

void cmpp__dump_defines(cmpp *pp, cmpp_FILE * fp, int bIndent);

/**
   Like cmpp_tt_cstr(), but if bSymbolName is false then it returns
   the higher-level token name, which is NULL for most token types.
*/
char const * cmpp__tt_cstr(int tt, bool bSymbolName);

/**
   Expects **zPos to be one of ('(' '{' '[' '"' '\'') and zEnd to be
   the logical EOF for *zPos.

   This looks for a matching closing token, accounting for nesting. On
   success, returns 0 and sets *zPos to the closing character.

   On error it update's pp's error state and returns that code. pp may
   be NULL.

   If pNl is not NULL then *pNl is incremented for each '\n' character
   seen while looking for the closing character.
*/
int cmpp__find_closing2(cmpp *pp,
                        unsigned char const **zPos,
                        unsigned char const *zEnd,
                        cmpp_size_t *pNl);

#define cmpp__find_closing(PP,Z0,Z1) \
  cmpp__find_closing2(PP, Z0, Z1, NULL)

static inline cmpp_size_t cmpp__strlen(char const *z, cmpp_ssize_t n){
  return n<0 ? (cmpp_size_t)strlen(z) : (cmpp_size_t)n;
}
static inline cmpp_size_t cmpp__strlenu(unsigned char const *z, cmpp_ssize_t n){
  return n<0 ? (cmpp_size_t)strlen((char const *)z) : (cmpp_size_t)n;
}

/**
   If ppCode is not set and pol resolves to cmpp_atpol_OFF then this
   updates ppCode with a message about the lack of support for
   at-strings. If cmpp_atpol_CURRENT==pol then pp's current policy is
   checked. Returns ppCode.
*/
int cmpp__StringAtIsOk(cmpp * const pp, cmpp_atpol_e pol);

/**
   "define"s zKey to zVal, recording the value type as tType.
*/
int cmpp__define2(cmpp *pp,
                  unsigned char const * zKey,
                  cmpp_ssize_t nKey,
                  unsigned char const *zVal,
                  cmpp_ssize_t nVal,
                  cmpp_tt tType);

/**
   Evals pArgs's arguments as an integer expression. On success, sets
   *pResult to the value.

   Returns ppCode.
*/
int cmpp__args_evalToInt(cmpp_dx * dx, cmpp_args const *pArgs,
                         int * pResult);

/** Passes the contents of arg through to cmpp__args_evalToInt(). */
int cmpp__arg_evalSubToInt(cmpp_dx *dx, cmpp_arg const *arg,
                           int * pResult);

/**
   Evaluated arg as an integer/bool, placing the result in *pResult
   and setting *pNext to the first argument to arg's right which this
   routine did not consume. Non-0 on error and all that.
*/
int cmpp__arg_toBool(cmpp_dx * dx, cmpp_arg const *arg,
                     int * pResult, cmpp_arg const **pNext);

/**
   If thisTtype==cmpp_TT_AnyType or thisTtype==arg->ttype and arg->z
   looks like it might contain an at-string then os is re-used to hold
   the @token@-expanded version of arg's content. os is unconditionally
   passed to cmpp_b_reuse() before it begines work.

   It uses the given atPolicy to determine whether or not the content
   is expanded, as per cmpp_dx_out_expand().

   Returns 0 on success. If it expands content then *pExp is set to
   os->z, else *pExp is set to arg->z. If nExp is not NULL then *nExp
   gets set to the length of *pExp (geither os->n or arg->n).

   Returns ppCode.

   Much later: what does this give us that cmpp_arg_to_b()
   doesn't? Oh - that one calls into this one. i.e. this one is
   lower-level.
*/
int cmpp__arg_expand_ats(cmpp_dx const * const dx,
                         cmpp_b * os,
                         cmpp_atpol_e atPolicy,
                         cmpp_arg const * const arg,
                         cmpp_tt thisTtype,
                         unsigned char const **pExp,
                         cmpp_size_t * nExp);

typedef struct cmpp_argOp cmpp_argOp;
typedef void (*cmpp_argOp_f)(cmpp_dx *dx,
                             cmpp_argOp const *op,
                             cmpp_arg const *vLhs,
                             cmpp_arg const **pvRhs,
                             int *pResult);
struct cmpp_argOp {
  int ttype;
  /* 1 or 2 */
  unsigned char arity;
  /* 0=none/left, 1=right (unary ops only) */
  signed char   assoc;
  cmpp_argOp_f   xCall;
};

cmpp_argOp const * cmpp_argOp_for_tt(cmpp_tt tt);


bool cmpp__is_int(unsigned char const *z, unsigned n,
                  int *pOut);
bool cmpp__is_int64(unsigned char const *z, unsigned n, int64_t *pOut);

char const * cmpp__atpol_name(cmpp *pp, cmpp_atpol_e p);
char const * cmpp__unpol_name(cmpp *pp, cmpp_unpol_e p);

/**
   Uncerimoniously bitwise-replaces pp's output channel with oNew. It
   does _not_ clean up the previous channel, on the assumption that
   the caller is taking any necessary measures.

   Apropos necessary measures for cleanup: if oPrev is not NULL,
   *oPrev is set to a bitwise copy of the previous channel.

   Intended usage:

   ```
   cmpp_outputer oMine = cmpp_outputer_b;
   cmpp_b bMine = cmpp_b_empty;
   cmpp_outputer oOld = {0};
   oMine.state = &bMine;
   cmpp_outputer_swap(pp, &myOut, &oOld);
   ...do some work then ALWAYS do...
   cmpp_outputer_swap(pp, &oOld, &oMine);
   ```

   Because this involves bitwise copying, care must be taken with
   stream state, e.g. bMine.z (above) can be reallocated, so we have
   to be sure to swap it back before using bMine again.
*/
void cmpp__outputer_swap(cmpp *pp, cmpp_outputer const *oNew,
                         cmpp_outputer *oPrev);

/**
   Init code which is usually run as part of the ctor but may have to
   be run later, after cmpp_reset(). We can't run it from cmpp_reset()
   because that could leave post-reset in an error state, which is
   icky. This call is a no-op after the first.
*/
int cmpp__lazy_init(cmpp *pp);

CMPP_NORETURN void cmpp__fatalv_base(char const *zFile, int line,
                                     char const *zFmt, va_list);
#define cmpp__fatalv(...) cmpp__fatalv_base(__FILE__,__LINE__,__VA_ARGS__)
CMPP_NORETURN void cmpp__fatal_base(char const *zFile, int line,
                                    char const *zFmt, ...);
#define cmpp__fatal(...) cmpp__fatal_base(__FILE__,__LINE__,__VA_ARGS__)

/**
   Outputs a printf()-formatted message to stderr.
*/
void g_stderr(char const *zFmt, ...);
#define g_warn(zFmt,...) g_stderr("%s:%d %s() " zFmt "\n", __FILE__, __LINE__, __func__, __VA_ARGS__)
#define g_warn0(zMsg) g_stderr("%s:%d %s() %s\n", __FILE__, __LINE__, __func__, zMsg)
#if 0
#define g_debug(PP,lvl,pfexpr) (void)0
#else
#define g_debug(PP,lvl,pfexpr)                         \
  if(lvl<=(PP)->pimpl->flags.doDebug) {                \
    if( (PP)->pimpl->dx ){                             \
      g_stderr("%s:%" CMPP_SIZE_T_PFMT ": ",           \
               (PP)->pimpl->dx->sourceName,            \
               (PP)->pimpl->dx->pimpl->dline.lineNo);  \
    }                                                  \
    g_stderr("%s():%d: ",                              \
             __func__,__LINE__);                       \
    g_stderr pfexpr;                                   \
  } (void)0
#endif

/** Returns true if zFile is readable, else false. */
bool cmpp__file_is_readable(char const *zFile);

#define ustr_c(X) ((unsigned char const *)X)
#define ustr_nc(X) ((unsigned char *)X)
#define ppCode pp->pimpl->err.code
#define dxppCode dx->ppCode
#define cmpp__pi(PP) cmpp_pimpl * const pi = PP->pimpl
#define cmpp__dx_pi(DX) cmpp_dx_pimpl * const dpi = DX->pimpl
#define serr(...) cmpp_err_set(pp, CMPP_RC_SYNTAX, __VA_ARGS__)
#define dxserr(...) cmpp_err_set(dx->pp, CMPP_RC_SYNTAX, __VA_ARGS__)
#define cmpp__li_reserve1_size(li,nInitial)                       \
  (li->n ? (li->n==li->nAlloc ? li->nAlloc * 2 : li->n+1) : nInitial)

#define MARKER(pfexp)                                               \
  do{ printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__);   \
    printf pfexp;                                                   \
  } while(0)

#endif /* include guard */
/*
** 2022-11-12:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses the core of libcmpp (it used to house all of it,
** but the library grew beyond the confines of a single file).
**
** See the accompanying c-pp.h and README.md and/or c-pp.h for more
** details.
*/
#include "sqlite3.h"

char const * cmpp_version(void){ return CMPP_VERSION; }

const cmpp__delim cmpp__delim_empty = cmpp__delim_empty_m;
const cmpp__delim_list cmpp__delim_list_empty = cmpp__delim_list_empty_m;
const cmpp_outputer cmpp_outputer_empty = cmpp_outputer_empty_m;
const cmpp_outputer cmpp_outputer_FILE = {
  .out = cmpp_output_f_FILE,
  .flush = cmpp_flush_f_FILE,
  .cleanup = cmpp_outputer_cleanup_f_FILE,
  .state = NULL
};
const cmpp_b_list cmpp_b_list_empty =
  cmpp_b_list_empty_m;
const cmpp_outputer cmpp_outputer_b = {
  .out = cmpp_output_f_b,
  .flush = 0,
  .cleanup = cmpp_outputer_cleanup_f_b,
  .state = NULL
};

/**
   Default delimiters for @tokens@.
*/
static const cmpp__delim delimAtDefault = {
  .open  = { .z = ustr_c("@"), .n = 1 },
  .close = { .z = ustr_c("@"), .n = 1 },
  .zOwns = NULL
};

static const cmpp_api_thunk cmppApiMethods = {
#define A(V)
#define V(N,T,V) .N = V,
#define F(N,T,P) .N = cmpp_ ##N,
#define O(N,T) .N = &cmpp_ ##N,
  cmpp_api_thunk_map(A,V,F,O)
#undef F
#undef O
#undef V
#undef A
};

/* Fatally exits the app with the given printf-style message. */

CMPP__EXPORT(bool, cmpp_isspace)(int ch){
  return ' '==ch || '\t'==ch;
}

//CMPP__EXPORT(int, cmpp_isnl)(char const * z, char const *zEnd){}
static inline int cmpp_isnl(unsigned char const * z, unsigned char const *zEnd){
  //assert(z<zEnd && "Caller-level pointer mis-traversal");
  switch( z<zEnd ? *z : 0 ){
    case 0: return 0;
    case '\r': return ((z+1<zEnd) && '\n'==z[1]) ? 2 : 0;
    default: return '\n'==*z;
  }
}

static inline bool cmpp_issnl(int ch){
  /* TODO: replace this in line with cmpp_isnl(), but it needs
     a new interface for that. It's only used in two places and they
     have different traversal directions, so we can probably
     get rid of this and do the direct CRNL check in each of those
     places. */
  return ' '==ch || '\t'==ch || '\n'==ch;
}

CMPP__EXPORT(void, cmpp_skip_space)(
  unsigned char const **p,
  unsigned char const *zEnd
){
  assert( *p <= zEnd );
  unsigned char const * z = *p;
  while( z<zEnd && cmpp_isspace(*z) ) ++z;
  *p = z;
}

CMPP__EXPORT(void, cmpp_skip_snl)( unsigned char const **p,
                                   unsigned char const *zEnd ){
  unsigned char const * z = *p;
  /* FIXME: CRNL. */
  while( z<zEnd && cmpp_issnl(*z) ) ++z;
  *p = z;
}

CMPP__EXPORT(void, cmpp_skip_space_trailing)( unsigned char const *zBegin,
                                              unsigned char const **p ){
  assert( *p >= zBegin );
  unsigned char const * z = *p;
  while( z>zBegin && cmpp_isspace(z[-1]) ) --z;
  *p = z;
}

CMPP__EXPORT(void, cmpp_skip_snl_trailing)( unsigned char const *zBegin,
                                            unsigned char const **p ){
  assert( *p >= zBegin );
  unsigned char const * z = *p;
  /* FIXME: CRNL. */
  while( z>zBegin && cmpp_issnl(*z) ) --z;
  *p = z;
}

/* Set pp's error state. */
static int cmpp__errv(cmpp *pp, int rc, char const *zFmt, va_list);
/**
   Sets pp's error state.
*/
CMPP__EXPORT(int, cmpp_err_set)(cmpp *pp, int rc, char const *zFmt, ...);
#define cmpp__err cmpp_err_set
#define cmpp_dx_err cmpp_dx_err_set

/* Open/close pp's output channel. */
static int cmpp__out_fopen(cmpp *pp, const char *zName);
static void cmpp__out_close(cmpp *pp);

#define CmppKvp_empty_m \
  {CmppSnippet_empty_m,CmppSnippet_empty_m,CmppKvp_op_none}
const CmppKvp CmppKvp_empty = CmppKvp_empty_m;

/* Wrapper around a cmpp_FILE handle. Legacy stuff from when we just
   supported cmpp_FILE input. */
typedef struct FileWrapper FileWrapper;
struct FileWrapper {
  /* File's name. */
  char const *zName;
  /* cmpp_FILE handle. */
  cmpp_FILE * pFile;
  /* Where FileWrapper_slurp() stores the file's contents. */
  unsigned char * zContent;
  /* Size of this->zContent, as set by FileWrapper_slurp(). */
  cmpp_size_t nContent;
};
#define FileWrapper_empty_m {0,0,0,0}
static const FileWrapper FileWrapper_empty = FileWrapper_empty_m;

/**
   Proxy for cmpp_fclose() and frees all memory owned by p. It is not
   an error if p is already closed.
*/
static void FileWrapper_close(FileWrapper * p);

/** Proxy for cmpp_fopen(). Closes p first if it's currently opened. */
static int FileWrapper_open(FileWrapper * p, const char * zName, const char *zMode);

/* Populates p->zContent and p->nContent from p->pFile. */
//static int FileWrapper_slurp(FileWrapper * p, int bCloseFile );

/**
   If p->zContent ends in \n or \r\n, that part is replaced with 0 and
   p->nContent is adjusted. Returns true if it chomps, else false.
*/
static bool FileWrapper_chomp(FileWrapper * p);

/*
** Outputs a printf()-formatted message to stderr.
*/
static void g_stderrv(char const *zFmt, va_list);

CMPP__EXPORT(char const *, cmpp_rc_cstr)(int rc){
  switch((cmpp_rc_e)rc){
#define E(N,V,H) case N: return # N;
    cmpp_rc_e_map(E)
#undef E
  }
  return NULL;
}

CMPP__EXPORT(void, cmpp_mfree)(void *p){
  /* This MUST be a proxy for sqlite3_free() because allocate memory
     exclusively using sqlite3_malloc() and friends. */
  sqlite3_free(p);
}

CMPP__EXPORT(void *, cmpp_mrealloc)(void * p, size_t n){
  return sqlite3_realloc64(p, n);
}

CMPP__EXPORT(void *, cmpp_malloc)(size_t n){
#if 1
  return sqlite3_malloc64(n);
#else
  void * p = sqlite3_malloc64(n);
  if( p ) memset(p, 0, n);
  return p;
#endif
}

cmpp_FILE * cmpp_fopen(const char *zName, const char *zMode){
  cmpp_FILE *f;
  if(zName && ('-'==*zName && !zName[1])){
    f = (strchr(zMode, 'w') || strchr(zMode,'+'))
      ? stdout
      : stdin
      ;
  }else{
    f = fopen(zName, zMode);
  }
  return f;
}

void cmpp_fclose( cmpp_FILE * f ){
  if(f && (stdin!=f) && (stdout!=f) && (stderr!=f)){
    fclose(f);
  }
}

int cmpp_slurp(cmpp_input_f fIn, void *sIn,
               unsigned char **pOut, cmpp_size_t * nOut){
  unsigned char zBuf[1024 * 16];
  unsigned char * pDest = 0;
  unsigned nAlloc = 0;
  unsigned nOff = 0;
  int rc = 0;
  cmpp_size_t nr = 0;
  while( 0==rc ){
    nr = sizeof(zBuf);
    if( (rc = fIn(sIn, zBuf, &nr)) ){
      break;
    }
    if(nr>0){
      if(nAlloc < nOff + nr + 1){
        nAlloc = nOff + nr + 1;
        pDest = cmpp_mrealloc(pDest, nAlloc);
      }
      memcpy(pDest + nOff, zBuf, nr);
      nOff += nr;
    }else{
      break;
    }
  }
  if( 0==rc ){
    if(pDest) pDest[nOff] = 0;
    *pOut = pDest;
    *nOut = nOff;
  }else{
    cmpp_mfree(pDest);
  }
  return rc;
}

void FileWrapper_close(FileWrapper * p){
  if(p->pFile) cmpp_fclose(p->pFile);
  if(p->zContent) cmpp_mfree(p->zContent);
  *p = FileWrapper_empty;
}

int FileWrapper_open(FileWrapper * p, const char * zName,
                     const char * zMode){
  FileWrapper_close(p);
  if( (p->pFile = cmpp_fopen(zName, zMode)) ){
    p->zName = zName;
    return 0;
  }else{
    return cmpp_errno_rc(errno, CMPP_RC_IO);
  }
}

int FileWrapper_slurp(FileWrapper * p, int bCloseFile){
  assert(!p->zContent);
  assert(p->pFile);
  int const rc = cmpp_slurp(cmpp_input_f_FILE, p->pFile,
                            &p->zContent, &p->nContent);
  if( bCloseFile ){
    cmpp_fclose(p->pFile);
    p->pFile = 0;
  }
  return rc;
}

CMPP__EXPORT(bool, cmpp_chomp)(unsigned char * z, cmpp_size_t * n){
  if( *n && '\n'==z[*n-1] ){
    z[--*n] = 0;
    if( *n && '\r'==z[*n-1] ){
      z[--*n] = 0;
    }
    return true;
  }
  return false;
}

bool FileWrapper_chomp(FileWrapper * p){
  return cmpp_chomp(p->zContent, &p->nContent);
}


#if 0
/**
   Returns the number newline characters between the given starting
   point and inclusive ending point. Results are undefined if zFrom is
   greater than zTo.
*/
static unsigned cmpp__count_lines(unsigned char const * zFrom,
                                  unsigned char const *zTo);

unsigned cmpp__count_lines(unsigned char const * zFrom,
                           unsigned char const *zTo){
  unsigned ln = 0;
  assert(zFrom && zTo);
  assert(zFrom <= zTo);
  for(; zFrom < zTo; ++zFrom){
    if((unsigned char)'\n' == *zFrom) ++ln;
  }
  return ln;
}
#endif

char const * cmpp__tt_cstr(int tt, bool bSymbolName){
  switch(tt){
#define E(N,TOK) case cmpp_TT_ ## N: \
    return bSymbolName ? "cmpp_TT_" #N : TOK;
    cmpp_tt_map(E)
#undef E
  }
  return NULL;
}

char const * cmpp_tt_cstr(int tt){
  return cmpp__tt_cstr(tt, true);
}

/** Flags and constants related to CmppLvl. */
enum CmppLvl_e {
  /**
     Flag indicating that all ostensible output for a CmpLevel should
     be elided. This also suppresses non-flow-control directives from
     being processed.
  */
  CmppLvl_F_ELIDE = 0x01,
  /**
     Mask of CmppLvl::flags which are inherited when
     CmppLvl_push() is used.
  */
  CmppLvl_F_INHERIT_MASK = CmppLvl_F_ELIDE
};

//static const CmppDLine CmppDLine_empty = CmppDLine_empty_m;

/** Free all memory owned by li but does not free li. */
static void CmppLvlList_cleanup(CmppLvlList *li);

/**
   Allocate a list entry, owned by li, and return it (cleanly zeroed
   out). Returns NULL and updates pp->err on error.  It is expected
   that the caller will populate the entry's zName using
   sqlite3_mprintf() or equivalent.
*/
static CmppLvl * CmppLvlList_push(cmpp *pp, CmppLvlList *li);

/** Returns the most-recently-appended element of li back to li's
    free-list. It expects to receive that value as a sanity-checking
    measure and may fail fatally of that's not upheld. */
static void CmppLvlList_pop(cmpp *pp, CmppLvlList *li, CmppLvl * lvl);

static const cmpp_dx_pimpl cmpp_dx_pimpl_empty =
  cmpp_dx_pimpl_empty_m;

#define cmpp_dx_empty_m { \
  .pp=0,                  \
  .d=0,                   \
  .sourceName=0,          \
  .args={                 \
    .z=0, .nz=0,          \
    .argc=0, .arg0=0      \
  },                      \
  .pimpl = 0              \
}

const cmpp_dx cmpp_dx_empty = cmpp_dx_empty_m;
#define cmpp_d_empty_m {{0,0},0,0,cmpp_d_impl_empty_m}
//static const cmpp_d cmpp_d_empty = cmpp_d_empty_m;

static const CmppDList_entry CmppDList_entry_empty =
  CmppDList_entry_empty_m;

/** Free all memory owned by li but does not free li. */
static void CmppDList_cleanup(CmppDList *li);
/**
   Allocate a list entry, owned by li, and return it (cleanly zeroed
   out). Returns NULL and updates pp->err on error.  It is expected
   that the caller will populate the entry's zName using
   sqlite3_mprintf() or equivalent.
*/
static CmppDList_entry * CmppDList_append(cmpp *pp, CmppDList *li);
/** Returns the most-recently-appended element of li back to li's
    free-list. */
static void CmppDList_unappend(CmppDList *li);
/** Resets li's list for re-use but does not free it. Returns li. */
//static CmppDList * CmppDList_reuse(CmppDList *li);
static CmppDList_entry * CmppDList_search(CmppDList const * li,
                                          char const *zName);

/** Reset dx and free any memory it may own. */
static void cmpp_dx_cleanup(cmpp_dx * const dx);
/**
   Reset some of dx's parsing-related state in prep for fetching the
   next line.
*/
static void cmpp_dx__reset(cmpp_dx * const dx);

/* Returns dx's current directive. */
static inline cmpp_d const * cmpp_dx_d(cmpp_dx const * const dx){
  return dx->d;
}

static const cmpp_pimpl cmpp_pimpl_empty = {
  .db = {
    .dbh = 0,
    .zName = 0
  },
  .dx = 0,
  .out = cmpp_outputer_empty_m,
  .delim = {
    .d = cmpp__delim_list_empty_m,
    .at = cmpp__delim_list_empty_m
  },
  .stmt = {
#define E(N,S) .N = 0,
  CmppStmt_map(E)
#undef E
  },
  .err = {
    .code = 0,
    .zMsg = 0,
    .zMsgC = 0
  },
  .sqlTrace = {
    .expandSql = false,
    .counter = 0,
    .out = cmpp_outputer_empty_m
  },
  .flags = {
    .allocStamp = 0,
    .nIncludeDir = 0,
    .nDxDepth = 0,
    .nSavepoint = 0,
    .doDebug = 0,
    .chompF = 0,
    .newFlags = 0,
    .isInternalDirectiveReg = false,
    .nextIsCall = false,
    .needsLazyInit = true
  },
  .policy = {
    .at = {0,0,0},
    .un = {0,0,0}
  },
  .d = {
    .list = CmppDList_empty_m,
    .autoload = cmpp_d_autoloader_empty_m
  },
  .mod = {
    .sohList = CmppSohList_empty_m,
    .path = cmpp_b_empty_m,
    .soExt = CMPP_PLATFORM_EXT_DLL,
    /* Yes, '*'. It makes sense in context. */
    .pathSep = '*'
    // 0x1e /* "record separator" */ doesn't work. Must be non-ctrl.
  },
  .recycler = {
    .buf = cmpp_b_list_empty_m,
    .bufSort = cmpp_b_list_UNSORTED,
    .argPimpl = NULL
  }
};

#if 0
static inline int cmpp__out(cmpp *pp, void const *z, cmpp_size_t n){
  return cmpp__out2(pp, &pp->out, z, n);
}
#endif

/**
   Returns an approximate cmpp_tt for the given SQLITE_... value from
   sqlite3_column_type() or sqlite3_value_type().
*/
static cmpp_tt cmpp__tt_for_sqlite(int sqType);

/**
   Init code which is usually run as part of the ctor but may have to
   be run later, after cmpp_reset(). We can't run it from cmpp_reset()
   because that could leave post-reset in an error state, which is
   icky.
*/
int cmpp__lazy_init(cmpp *pp){
  if( !ppCode && pp->pimpl->flags.needsLazyInit ){
    pp->pimpl->flags.needsLazyInit = false;
    cmpp__delim_list * li = &pp->pimpl->delim.d;
    if( !li->n ) cmpp_delimiter_push(pp, NULL);
    li = &pp->pimpl->delim.at;
    if( !li->n ) cmpp_atdelim_push(pp, NULL, NULL);
#if defined(CMPP_CTOR_INSTANCE_INIT)
    if( !ppCode ){
      extern int CMPP_CTOR_INSTANCE_INIT(cmpp*);
      int const rc = CMPP_CTOR_INSTANCE_INIT(pp);
      if( rc && !ppCode ){
        cmpp__err(pp, rc,
                  "Initialization via CMPP_CTOR_INSTANCE_INIT() failed "
                  "with code %d/%s.", rc, cmpp_rc_cstr(rc) );
      }
    }
#endif
  }
  return ppCode;
}

static void cmpp__wipe_policies(cmpp *pp){
  if( 0==ppCode ){
    PodList__atpol_reserve(pp, &cmpp__epol(pp,at), 0);
    PodList__unpol_reserve(pp, &cmpp__epol(pp,un), 0);
    if( 0==ppCode ){
      PodList__atpol_wipe(&cmpp__epol(pp,at), cmpp_atpol_DEFAULT);
      PodList__unpol_wipe(&cmpp__epol(pp,un), cmpp_unpol_DEFAULT);
    }
  }
}

CMPP__EXPORT(int, cmpp_ctor)(cmpp **pOut, cmpp_ctor_cfg const * cfg){
  cmpp_pimpl * pi = 0;
  cmpp * pp = 0;
  void * const mv = cmpp_malloc(sizeof(cmpp) + sizeof(*pi));
  if( mv ){
    if( !cfg ){
      static const cmpp_ctor_cfg dfltCfg = {0};
      cfg = &dfltCfg;
    }
    cmpp const x = {
      .api = &cmppApiMethods,
      .pimpl = (cmpp_pimpl*)((unsigned char *)mv + sizeof(cmpp))
      /* ^^^ (T const * const) members */
    };
    memcpy(mv, &x, sizeof(x))
      /* FWIW, i'm convinced that this is a legal way to transfer
         these const-pointers-to-const.  If not, we'll need to change
         those cmpp members from (T const * const) to (T const *). */;
    pp = mv;
    assert(pp->api == &cmppApiMethods);
    assert(pp->pimpl);
    pi = pp->pimpl;
    *pOut = pp;
    *pi = cmpp_pimpl_empty;
    assert( pi->flags.needsLazyInit );
    pi->flags.newFlags = cfg->flags;
    pi->flags.allocStamp = &cmpp_pimpl_empty;
    if( cfg->dbFile ){
      pi->db.zName = sqlite3_mprintf("%s", cfg->dbFile);
      cmpp_check_oom(pp, pi->db.zName);
    }
    if( 0==ppCode ){
      cmpp__wipe_policies(pp);
      cmpp__lazy_init(pp);
    }
  }
  return pp ? ppCode : CMPP_RC_OOM;
}

CMPP__EXPORT(void, cmpp_reset)(cmpp *pp){
  cmpp__pi(pp);
  cmpp_outputer_cleanup(&pi->sqlTrace.out);
  pi->sqlTrace.out = cmpp_outputer_empty;
  if( pi->d.autoload.dtor ){
    pi->d.autoload.dtor(pi->d.autoload.state);
  }
  pi->d.autoload = cmpp_pimpl_empty.d.autoload;
  cmpp_b_clear(&pi->mod.path);
  if( pi->stmt.spRelease && pi->stmt.spRollback ){
    /* Cleanly kill all savepoint levels. This is truly superfluous,
       as they'll all be rolled back (if the db is persistent) or
       nuked (if using a :memory: db) momentarily. However, we'll
       eventually need this for a partial-clear operation which leaves
       the db and custom directives intact. For now it lives here but
       will eventually move to wherever that ends up being.

       2025-11-16: or not. It's fine here, really.
    */
    sqlite3_reset(pi->stmt.spRelease);
    while( SQLITE_DONE==sqlite3_step(pi->stmt.spRelease) ){
      sqlite3_reset(pi->stmt.spRollback);
      sqlite3_step(pi->stmt.spRollback);
      sqlite3_reset(pi->stmt.spRelease);
    }
  }
  cmpp__out_close(pp);
  CmppDList_cleanup(&pi->d.list);
#define E(N,S) \
  if(pi->stmt.N) {sqlite3_finalize(pi->stmt.N); pi->stmt.N = 0;}
  CmppStmt_map(E) (void)0;
#undef E
  if( pi->db.dbh ){
    if( SQLITE_TXN_WRITE==sqlite3_txn_state(pi->db.dbh, NULL) ){
      sqlite3_exec(pi->db.dbh, "COMMIT;", 0, 0, NULL)
        /* ignoring error */;
    }
    sqlite3_close(pi->db.dbh);
    pi->db.dbh = 0;
  }
  cmpp__delim_list_reuse(&pi->delim.d);
  cmpp__delim_list_reuse(&pi->delim.at);
  //why? cmpp_b_list_reuse(&pi->cache.buf);
  cmpp__err_clear(pp);
  {/* Zero out pi but save some pieces for later, when pp is
      cmpp_dtor()'d */
    cmpp_pimpl const tmp = *pi;
    *pi = cmpp_pimpl_empty;
    pi->db = tmp.db /* restore db.zName */;
    pi->recycler = tmp.recycler;
    pi->policy = tmp.policy;
    pi->delim = tmp.delim;
    pi->mod.sohList = tmp.mod.sohList;
    cmpp__wipe_policies(pp);
    pi->flags.allocStamp = tmp.flags.allocStamp;
    pi->flags.newFlags = tmp.flags.newFlags;
    pi->flags.needsLazyInit = true;
  }
}

static void cmpp__delim_list_cleanup(cmpp__delim_list *li);

CMPP__EXPORT(void, cmpp_dtor)(cmpp *pp){
  if( pp ){
    cmpp__pi(pp);
    cmpp_reset(pp);
    cmpp_mfree(pi->db.zName);
    PodList__atpol_finalize(&cmpp__epol(pp,at));
    assert(!cmpp__epol(pp,at).na);
    PodList__unpol_finalize(&cmpp__epol(pp,un));
    assert(!cmpp__epol(pp,un).na);
    cmpp_b_list_cleanup(&pi->recycler.buf);
    cmpp__delim_list_cleanup(&pi->delim.d);
    cmpp__delim_list_cleanup(&pi->delim.at);
    for( cmpp_args_pimpl * apNext = 0,
           * ap = pi->recycler.argPimpl;
         ap; ap = apNext ){
      apNext = ap->nextFree;
      ap->nextFree = 0;
      cmpp_args_pimpl_cleanup(ap);
      cmpp_mfree(ap);
    }
    CmppSohList_close(&pi->mod.sohList);
    if( &cmpp_pimpl_empty==pi->flags.allocStamp ){
      pi->flags.allocStamp = 0;
      cmpp_mfree(pp);
    }
  }
}

CMPP__EXPORT(bool, cmpp_is_safemode)(cmpp const * pp){
  return pp ? 0!=(cmpp_ctor_F_SAFEMODE & pp->pimpl->flags.newFlags) : false;
}

/** Sets ppCode if m is NULL. Returns ppCode. */
CMPP__EXPORT(int, cmpp_check_oom)(cmpp * const pp, void const * const m ){
  int rc;
  if( pp ){
    if( !m ){
      //assert(!"oom");
      cmpp__err(pp, CMPP_RC_OOM, 0);
    }
    rc = ppCode;
  }else{
    rc = m ? 0 : CMPP_RC_OOM;
  }
  return rc;
}

//CxMPP_WASM_EXPORT
void *cmpp__malloc(cmpp *pp, cmpp_size_t n){
  void *p = 0;
  if( 0==ppCode ){
    p = cmpp_malloc(n);
    cmpp_check_oom(pp, p);
  }
  return p;
}

/**
   If ppCode is not 0 then it flushes pp's output channel. If that
   fails, it sets ppCode. Returns ppCode.
*/
static int cmpp__flush(cmpp *pp){
  if( !ppCode && pp->pimpl->out.flush ){
    int const rc = pp->pimpl->out.flush(pp->pimpl->out.state);
    if( rc && !ppCode ){
      cmpp_err_set(pp, rc, "Flush failed.");
    }
  }
  return ppCode;
}

void cmpp__out_close(cmpp *pp){
  cmpp__flush(pp)/*ignoring result*/;
  cmpp_outputer_cleanup(&pp->pimpl->out);
  pp->pimpl->out = cmpp_pimpl_empty.out;
}

int cmpp__out_fopen(cmpp *pp, const char *zName){
  cmpp__out_close(pp);
  if( !ppCode ){
    cmpp_FILE * const f = cmpp_fopen(zName, "wb");
    if( f ){
      ppCode = 0;
      pp->pimpl->out = cmpp_outputer_FILE;
      pp->pimpl->out.state = f;
      pp->pimpl->out.name = zName;
    }else{
      ppCode = cmpp__err(
        pp, cmpp_errno_rc(errno, CMPP_RC_IO),
        "Error opening file %s", zName
      );
    }
  }
  return ppCode;
}

static int cmpp__FileWrapper_open(cmpp *pp, FileWrapper * fw,
                                  const char * zName,
                                  const char * zMode){
  int const rc = FileWrapper_open(fw, zName, zMode);
  if( rc ){
    cmpp__err(pp, rc, "Error %s opening file [%s] "
                  "with mode [%s]",
                  cmpp_rc_cstr(rc), zName, zMode);
  }
  return ppCode;
}

static int cmpp__FileWrapper_slurp(cmpp* pp, FileWrapper * fw){
  assert( fw->pFile );
  int const rc = FileWrapper_slurp(fw, 1);
  if( rc ){
    cmpp__err(pp, rc, "Error %s slurping file %s",
              cmpp_rc_cstr(rc), fw->zName);
  }
  return ppCode;
}

void g_stderrv(char const *zFmt, va_list va){
  vfprintf(0 ? stdout : stderr, zFmt, va);
}

void g_stderr(char const *zFmt, ...){
  va_list va;
  va_start(va, zFmt);
  g_stderrv(zFmt, va);
  va_end(va);
}

CMPP__EXPORT(char const *, cmpp_dx_delim)(cmpp_dx const *dx){
  return (char const *)cmpp__dx_zdelim(dx);
}

int cmpp__out2(cmpp *pp, cmpp_outputer *pOut,
               void const *z, cmpp_size_t n){
  assert( pOut );
  if( !ppCode && pOut->out && n ){
    int const rc = pOut->out(pOut->state, z, n);
    if( rc ){
      cmpp__err(pp, rc,
                "Write of %" CMPP_SIZE_T_PFMT
                " bytes to output stream failed.", n);
    }
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_dx_out_raw)(cmpp_dx * dx, void const *z, cmpp_size_t n){
  if( dxppCode || cmpp_dx_is_eliding(dx) ) return dxppCode;
  return cmpp__out2(dx->pp, &dx->pp->pimpl->out, z, n);
}

CMPP__EXPORT(int, cmpp_outfv2)(cmpp *pp, cmpp_outputer *out, char const *zFmt, va_list va){
  assert( out );
  if( !ppCode && zFmt && *zFmt && out->out ){
    char * s = sqlite3_vmprintf(zFmt, va);
    if( 0==cmpp_check_oom(pp, s) ){
      cmpp__out2(pp, out, s, cmpp__strlen(s, -1));
    }
    cmpp_mfree(s);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_outf2)(cmpp *pp, cmpp_outputer *out, char const *zFmt, ...){
  assert( out );
  if( !ppCode && zFmt && *zFmt && out->out ){
    va_list va;
    va_start(va, zFmt);
    cmpp_outfv2(pp, out, zFmt, va);
    va_end(va);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_outfv)(cmpp *pp, char const *zFmt, va_list va){
  return cmpp_outfv2(pp, &pp->pimpl->out, zFmt, va);
}

CMPP__EXPORT(int, cmpp_outf)(cmpp *pp, char const *zFmt, ...){
  if( !ppCode ){
    va_list va;
    va_start(va, zFmt);
    cmpp_outfv2(pp, &pp->pimpl->out, zFmt, va);
    va_end(va);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_dx_outf)(cmpp_dx *dx, char const *zFmt, ...){
  if( !dxppCode && zFmt && *zFmt && dx->pp->pimpl->out.out ){
    va_list va;
    va_start(va, zFmt);
    cmpp_outfv(dx->pp, zFmt, va);
    va_end(va);
  }
  return dxppCode;
}

static int cmpp__affirm_undef_policy(cmpp *pp,
                                     unsigned char const *zName,
                                     cmpp_size_t nName){
  if( 0==ppCode
      && cmpp_unpol_ERROR==cmpp__policy(pp,un) ){
    cmpp__err(pp, CMPP_RC_NOT_DEFINED,
              "Key '%.*s' was not found and the undefined-value "
              "policy is 'error'.",
              (int)nName, zName);
  }
  return ppCode;
}

static int cmpp__out_expand(cmpp * pp, cmpp_outputer * pOut,
                            unsigned char const * zFrom,
                            cmpp_size_t n, cmpp_atpol_e atPolicy){
  enum state_e {
    /* looking for @token@ opening @ */
    state_opening,
    /* looking for @token@ closing @ */
    state_closing
  };
  cmpp__pi(pp);
  if( ppCode ) return ppCode;
  if( cmpp_atpol_CURRENT==atPolicy ) atPolicy = cmpp__policy(pp,at);
  assert( cmpp_atpol_invalid!=atPolicy );
  unsigned char const *zLeft = zFrom;
  unsigned char const * const zEnd = zFrom + n;
  unsigned char const *z =
    (cmpp_atpol_OFF==atPolicy || cmpp_atpol_invalid==atPolicy)
    ? zEnd
    : zLeft;
  unsigned char const chEol = (unsigned char)'\n';
  cmpp__delim const * delim =
    cmpp__delim_list_get(&pp->pimpl->delim.at);
  if( !delim && z<zEnd ){
    return cmpp_err_set(pp, CMPP_RC_ERROR,
                        "@token@ delimiter stack is empty.");
  }
  enum state_e state = state_opening;
  cmpp_b * const bCall = cmpp_b_borrow(pp);
  cmpp_b * const bVal = cmpp_b_borrow(pp);
  if( !bCall || !bVal ) return ppCode;
  if(0) g_warn("looking to expand from %d bytes: [%.*s]", (int)n,
               (int)n,zLeft);
  if( !pOut ){
    if( 0 && atPolicy!=cmpp__policy(pp,at) ){
      /* This might be too strict. It was initially in place to ensure
         that we did not _accidentally_ do @token@ parsing to the main
         output channel. We frequently use it internally on other
         output channels to buffer/build results.

         Advantage to removing this check: #query could impose
         its own at-policy without having to use an intermediary
         buffer.

         Disadvantage: less protection against accidentally
         @-filtering the output when we shouldn't.
      */
      return cmpp_err_set(pp, CMPP_RC_MISUSE,
                         "%s(): when sending to the default "
                         "output channel, the @policy must be "
                         "cmpp_atpol_CURRENT.", __func__);
    }
    pOut = &pi->out;
  }
  assert( pi->dx ? !cmpp_dx_is_eliding(pi->dx) : 1 );

#define tflush                                                          \
  if(z>zEnd) z=zEnd;                                                    \
  if(zLeft<z){                                                          \
    if(0) g_warn("flush %d [%.*s]", (int)(z-zLeft), (int)(z-zLeft), zLeft); \
    cmpp__out2(pp, pOut, zLeft, (z-zLeft));                             \
  } zLeft = z
  cmpp_dx_pimpl * const dxp = pp->pimpl->dx ? pp->pimpl->dx->pimpl : NULL;
  for( ; z<zEnd && 0==ppCode; ++z ){
    zLeft = z;
    for( ;z<zEnd && 0==ppCode; ++z ){
    again:
      if( chEol==*z ){
#if 0
        broken;
        if( dxp && dxp->flags.countLines ){
          ++dxp->lineNo;
        }
#endif
        state = state_opening;
        continue;
      }
      if( state_opening==state ){
        if( z + delim->open.n < zEnd
            && 0==memcmp(z, delim->open.z, delim->open.n) ){
          tflush;
          z += delim->open.n;
          if( 0 ) g_warn("zLeft..z=[%.*s]", (int)(z-zLeft), zLeft);
          if( 0 ){
            g_warn("\nzLeft..=[%s]\nz=[%s]", zLeft, z);
          }
          state = state_closing;
#if 1
          /* Handle call of @[directive ...args]@

             i'm not a huge fan of this syntax, but that may go away
             if we replace the single-char separator with a pair of
             opening/closing delimiters.
          */
          if( z<zEnd && '['==*z ){
            unsigned char const * zb = z;
            cmpp_size_t nl = 0;
            //g_warn("Scanning: <<%.*s>>", (int)(zEnd-zb), zb);
            if( cmpp__find_closing2(pp, &zb, zEnd, &nl) ){
              break;
            }
            //g_warn("Found: <<%.*s>>", (int)(zb+1-z), z);
            if( zb + delim->close.n >= zEnd
                || 0!=memcmp(zb+1, delim->close.z, delim->close.n) ){
              serr("Expecting '%s' after closing ']'.", delim->close.z);
              break;
            }
            if( nl && dxp && dxp->flags.countLines ){
              dxp->pos.lineNo +=nl;
            }
            cmpp_call_str(pp, z+delim->open.n,
                          (zb - z - delim->open.n),
                          cmpp_b_reuse(bCall), 0);
            if( 0==ppCode ){
              cmpp__out2(pp, pOut, bCall->z, bCall->n);
              state = state_opening;
              zLeft = z = zb + delim->close.n + 1;
              //g_warn("post-@[]@ z=%.*s", (int)(zEnd-z), z);
            }
          }
#endif
          if( z>=zEnd ) break;
          goto again /* avoid adjusting z again */;
        }
      }else{/*we're looking for delim->closer*/
        assert( state_closing==state );
        if( z + delim->close.n <= zEnd
            && 0==memcmp(z, delim->close.z, delim->close.n ) ){
          /* process the ... part of @...@ */
          assert( state_closing==state );
          assert( zLeft<z );
          assert( z<=zEnd );
          if( 0 ) g_warn("zLeft..z=[%.*s]", (int)(z-zLeft), zLeft);
          if( 0 ) g_warn("zLeft..=%s", zLeft);
          assert( 0==memcmp(zLeft, delim->open.z, delim->open.n) );
          unsigned char const *zKey =
            zLeft + delim->open.n;
          cmpp_ssize_t const nKey = z - zLeft - delim->open.n;
          if( 0 ) g_warn("nKey=%d zKey=[%.*s]", nKey, nKey, zKey);
          assert( nKey>= 0 );
          if( !nKey ){
            serr("Empty key is not permitted in %s...%s.",
                 delim->open.z, delim->close.z);
            break;
          }
          if( cmpp__get_b(pp, zKey, nKey, cmpp_b_reuse(bVal), true) ){
            if(0){
              g_warn("nVal=%d zVal=[%.*s]", (int)bVal->n,
                     (int)bVal->n, bVal->z);
            }
            if( bVal->n ){
              cmpp__out2(pp, pOut, bVal->z, bVal->n);
            }else{
              /* Elide it */
            }
            zLeft = z + delim->close.n;
            assert( zLeft<=zEnd );
          }else if( !ppCode ){
            assert( !bVal->n );
            /* No matching define . */
            switch( atPolicy ){
              case cmpp_atpol_ELIDE:  zLeft = z + delim->close.n; break;
              case cmpp_atpol_RETAIN: tflush; break;
              case cmpp_atpol_ERROR:
                cmpp__err(pp, CMPP_RC_NOT_DEFINED,
                          "Undefined %skey%s: %.*s",
                          delim->open.z, delim->close.z, nKey, zKey);
                break;
              case cmpp_atpol_invalid:
              case cmpp_atpol_CURRENT:
              case cmpp_atpol_OFF:
                assert(!"this shouldn't be reachable" );
                cmpp__err(pp, CMPP_RC_ERROR, "Unhandled atPolicy #%d",
                          atPolicy);
                break;
            }
          }/* process @...@ */
          state = state_opening;
          assert( z<=zEnd );
        }/*matched a closer*/
      }/*state_closer==state*/
      assert( z<=zEnd );
    }/*per-line loop*/
  }/*outer loop*/
#if 0
  if( 0==ppCode && state_closer==state ){
    serr("Opening '%s' found without a closing '%s'.",
         delim->open.z, delim->close.z);
  }
#endif
  tflush;
#undef tflush
  cmpp_b_return(pp, bCall);
  cmpp_b_return(pp, bVal);
  return ppCode;
}

CMPP__EXPORT(int, cmpp_dx_out_expand)(cmpp_dx const * const dx,
                                      cmpp_outputer * pOut,
                                      unsigned char const * zFrom,
                                      cmpp_size_t n,
                                      cmpp_atpol_e atPolicy){
  if( dxppCode || cmpp_dx_is_eliding(dx) ) return dxppCode;
  return cmpp__out_expand(dx->pp, pOut, zFrom, n, atPolicy);
}

CmppLvl * CmppLvl_get(cmpp_dx const *dx){
  return dx->pimpl->dxLvl.n
    ? dx->pimpl->dxLvl.list[dx->pimpl->dxLvl.n-1]
    : 0;
}

static const CmppLvl CmppLvl_empty = CmppLvl_empty_m;

CmppLvl * CmppLvl_push(cmpp_dx *dx){
  CmppLvl * p = 0;
  if( !dxppCode ){
    CmppLvl * const pPrev = CmppLvl_get(dx);
    p = CmppLvlList_push(dx->pp, &dx->pimpl->dxLvl);
    if( p ){
      *p = CmppLvl_empty;
      p->lineNo = dx->pimpl->dline.lineNo;
      //p->d = dx->d;
      if( pPrev ){
        p->flags = (CmppLvl_F_INHERIT_MASK & pPrev->flags);
        //if(CLvl_isSkip(pPrev)) p->flags |= CmppLvl_F_ELIDE;
      }
    }
  }
  return p;
}

void CmppLvl_pop(cmpp_dx *dx, CmppLvl * lvl){
  CmppLvlList_pop(dx->pp, &dx->pimpl->dxLvl, lvl);
}

void CmppLvl_elide(CmppLvl *lvl, bool on){
  if( on ) lvl->flags |=  CmppLvl_F_ELIDE;
  else     lvl->flags &= ~CmppLvl_F_ELIDE;
}

bool CmppLvl_is_eliding(CmppLvl const *lvl){
  return lvl && !!(lvl->flags & CmppLvl_F_ELIDE);
}

#if 0
void cmpp_dx_elide_mode(cmpp_dx *dx, bool on){
  CmppLvl_elide(CmppLvl_get(dx), on);
}
#endif

bool cmpp_dx_is_eliding(cmpp_dx const *dx){
  return CmppLvl_is_eliding(CmppLvl_get(dx));
}


char * cmpp_str_finish(cmpp *pp, sqlite3_str *s, int * n){
  char * z = 0;
  int const rc = sqlite3_str_errcode(s);
  cmpp__db_rc(pp, rc, "sqlite3_str_errcode()");
  if(0==rc){
    int const nStr = sqlite3_str_length(s);
    if(n) *n = nStr;
    z = sqlite3_str_finish(s);
    if( !z ){
      assert( 0==nStr && "else rc!=0" );
    }
  }else{
    cmpp_mfree( sqlite3_str_finish(s) );
  }
  return z;
}

int cmpp__bind_int(cmpp *pp, sqlite3_stmt *pStmt, int col, int64_t val){
  return ppCode
    ? ppCode
    : cmpp__db_rc(pp, sqlite3_bind_int64(pStmt, col, val),
                     "from cmpp__bind_int()");
}

int cmpp__bind_int_text(cmpp *pp, sqlite3_stmt *pStmt, int col,
                        int64_t val){
  unsigned char buf[32];
  snprintf((char *)buf, sizeof(buf), "%" PRIi64, val);
  return cmpp__bind_textn(pp, pStmt, col, buf, -1);
}

int cmpp__bind_null(cmpp *pp, sqlite3_stmt *pStmt, int col){
  return ppCode
    ? ppCode
    : cmpp__db_rc(pp, sqlite3_bind_null(pStmt, col),
                     "from cmpp__bind_null()");
}

static int cmpp__bind_textx(cmpp *pp, sqlite3_stmt *pStmt, int col,
                            unsigned const char * zStr, cmpp_ssize_t n,
                            void (*dtor)(void *)){
  if( 0==ppCode ){
    cmpp__db_rc(
      pp, (zStr && n)
      ? sqlite3_bind_text(pStmt, col,
                          (char const *)zStr,
                          (int)n, dtor)
      : sqlite3_bind_null(pStmt, col),
      sqlite3_sql(pStmt)
    );
  }
  return ppCode;
}

int cmpp__bind_textn(cmpp *pp, sqlite3_stmt *pStmt, int col,
                     unsigned const char * zStr, cmpp_ssize_t n){
  return cmpp__bind_textx(pp, pStmt, col, zStr, (int)n,
                          SQLITE_TRANSIENT);
}

int cmpp__bind_text(cmpp *pp, sqlite3_stmt *pStmt, int col,
                    unsigned const char * zStr){
  return cmpp__bind_textn(pp, pStmt, col, zStr, -1);
}

#if 0
int cmpp__bind_textv(cmpp*pp, sqlite3_stmt *pStmt, int col,
                     const char * zFmt, ...){
  if( 0==p->err.code ){
    int rc;
    sqlite3_str * str = sqlite3_str_new(pp->pimpl->db.dbh);
    int n = 0;
    char * z;
    va_list va;
    if( !str ) return ppCode;
    va_start(va,zFmt);
    sqlite3_str_vappendf(str, zFmt, va);
    va_end(va);
    z = cmpp_str_finish(str, &n);
    cmpp__db_rc(
      pp, z
      ? sqlite3_bind_text(pStmt, col, z, n, sqlite3_free)
      : sqlite3_bind_null(pStmt, col),
      sqlite3_sql(pStmt)
    );
    cmpp_mfree(z);
  }
  return p->err.code;
}
#endif

void cmpp_outputer_set(cmpp *pp, cmpp_outputer const *out,
                       char const *zName){
  cmpp__pi(pp);
  cmpp_outputer_cleanup(&pi->out);
  if( out ) pi->out = *out;
  else pi->out = cmpp_outputer_empty;
  pi->out.name = zName;
}

void cmpp__outputer_swap(cmpp *pp, cmpp_outputer const *oNew,
                        cmpp_outputer *oPrev){
  if( oPrev ){
    *oPrev = pp->pimpl->out;
  }
  pp->pimpl->out = *oNew;
}

#if 0
static void delim__list_dump(cmpp const *pp){
  cmpp__delim_list const *li = &pp->pimpl->delim.d;
  if( li->n ){
    g_warn0("delimiter stack:");
    for(cmpp_size_t i = 0; i < li->n; ++i ){
      g_warn("#%d: %s", (int)i, li->list[i].z);
    }
  }

}
#endif

static bool cmpp__valid_delim(cmpp * const pp,
                                char const *z,
                                char const *zEnd){
  char const * const zB = z;
  for( ; z < zEnd; ++z ){
    if( *z<33 || 127==*z ){
      cmpp_err_set(pp, CMPP_RC_SYNTAX,
                   "Delimiters may not contain "
                   "control characters.");
      return false;
    }
  }
  if( zB==z ){
    cmpp_err_set(pp, CMPP_RC_SYNTAX,
                 "Delimiters may not be empty.");
  }
  return z>zB;
}

CMPP__EXPORT(int, cmpp_delimiter_set)(cmpp *pp, char const *zDelim){
  if( ppCode ) return ppCode;
  unsigned n;
  if( zDelim ){
    n = cmpp__strlen(zDelim, -1);
    if( !cmpp__valid_delim(pp, zDelim, zDelim+n) ){
      return ppCode;
    }else if( n>12 /* arbitrary but seems sensible enough */ ){
      return cmpp__err(pp, CMPP_RC_MISUSE,
                       "Invalid delimiter (too long): %s", zDelim);
    }
  }
  cmpp__pi(pp);
  if( pi->delim.d.n ){
    cmpp__delim * const delim = cmpp__pp_delim(pp);
    if( !cmpp_check_oom(pp, delim) ){
      cmpp__delim_cleanup(delim);
      if( zDelim ){
        delim->open.n = n;
        delim->open.z = delim->zOwns =
          (unsigned char*)sqlite3_mprintf("%.*s", n, zDelim);
        cmpp_check_oom(pp, delim->zOwns);
      }else{
        assert( delim->open.z );
        assert( !delim->zOwns );
        assert( delim->open.n==sizeof(CMPP_DEFAULT_DELIM)-1 );
      }
    }
  }else{
    assert(!"Cannot set delimiter on an empty stack!");
    cmpp_err_set(pp, CMPP_RC_MISUSE,
                 "Directive delimter stack is empty.");
  }
  return ppCode;
}

CMPP__EXPORT(void, cmpp_delimiter_get)(cmpp const *pp, char const **zDelim){
  cmpp__delim const * d = cmpp__pp_delim(pp);
  if( !d ) d = &cmpp__delim_empty;
  *zDelim = (char const *)d->open.z;
}

CMPP__EXPORT(int, cmpp_delimiter_push)(cmpp *pp, char const *zDelim){
  cmpp__delim * const d =
    cmpp__delim_list_push(pp, &pp->pimpl->delim.d);
  if( d && cmpp_delimiter_set(pp, zDelim) ){
    cmpp__delim_list_pop(&pp->pimpl->delim.d);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_delimiter_pop)(cmpp *pp){
  cmpp__delim_list * const li = &pp->pimpl->delim.d;
  if( li->n ){
    //g_warn("Popping delimiter: %s", cmpp__pp_zdelim(pp));
    cmpp__delim_list_pop(li);
    if( 0 && li->n ){
      g_warn("restored delimiter: %s", cmpp__pp_zdelim(pp));
    }
  }else if( !ppCode ){
    assert(!"Attempt to pop an empty delimiter stack.");
    cmpp_err_set(pp, CMPP_RC_MISUSE,
                 "Cannot pop an empty delimiter stack.");
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_atdelim_set)(cmpp * const pp,
                                    char const *zOpen,
                                    char const *zClose){
  if( 0==ppCode ){
    cmpp__pi(pp);
    cmpp__delim * const d = pi->delim.at.n
      ? &pi->delim.at.list[pi->delim.at.n-1]
      : NULL;
    assert( d );
    if( !d ){
      return cmpp__err(pp, CMPP_RC_MISUSE,
                       "@token@ delimiter stack is currently empty.");
    }
    if( 0==zOpen ){
      zOpen = (char const *)delimAtDefault.open.z;
      zClose = (char const *)delimAtDefault.close.z;
    }else if( 0==zClose ){
      zClose = zOpen;
    }
    cmpp_size_t const nO = cmpp__strlen(zOpen, -1);
    cmpp_size_t const nC = cmpp__strlen(zClose, -1);
    assert( zOpen && zClose );
    if( !cmpp__valid_delim(pp, zOpen, zOpen+nO)
        || !cmpp__valid_delim(pp, zClose, zClose+nC) ){
      return ppCode;
    }
    cmpp_b b = cmpp_b_empty
      /* Don't use cmpp_b_borrow() here because we'll unconditionally
         transfer ownership of b.z to d. */;
    if( 0==cmpp_b_reserve3(pp, &b, nO + nC + 2) ){
#ifndef NDEBUG
      unsigned char const * const zReallocCheck = b.z;
#endif
      /* Copy the open/close tokens to a single string to simplify
         management. */
      cmpp_b_append4(pp, &b, zOpen, nO);
      cmpp_b_append_ch(&b, '\0');
      cmpp_b_append4(pp, &b, zClose, nC);
      assert( zReallocCheck==b.z
              && "Else buffer was not properly pre-sized" );
      cmpp__delim_cleanup(d);
      d->open.z = b.z;
      d->open.n = nO;
      d->close.z = d->open.z + nO + 1/*NUL*/;
      d->close.n = nC;
      d->zOwns = b.z;
      b = cmpp_b_empty /* transfer memory ownership */;
    }
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_atdelim_push)(cmpp *pp, char const *zOpen,
                                     char const *zClose){
  cmpp__delim * const d =
    cmpp__delim_list_push(pp, &pp->pimpl->delim.at);
  if( d && cmpp_atdelim_set(pp, zOpen, zClose) ){
    cmpp__delim_list_pop(&pp->pimpl->delim.at);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_atdelim_pop)(cmpp *pp){
  cmpp__delim_list * const li = &pp->pimpl->delim.at;
  if( li->n ){
    //g_warn("Popping delimiter: %s", cmpp__pp_zdelim(pp));
    cmpp__delim_list_pop(li);
  }else if( !ppCode ){
    assert(!"Attempt to pop an empty @token@ delim stack.");
    cmpp_err_set(pp, CMPP_RC_MISUSE,
                 "Cannot pop an empty @token@ delimiter stack.");
  }
  return ppCode;
}

CMPP__EXPORT(void, cmpp_atdelim_get)(cmpp const * const pp,
                                     char const **zOpen,
                                     char const **zClose){
  cmpp__delim const * d
    = cmpp__delim_list_get(&pp->pimpl->delim.at);
  assert( d );
  if( !d ) d = &delimAtDefault;
  if( zClose ) *zClose = (char const *)d->close.z;
  if( zOpen ) *zOpen = (char const *)d->open.z;
}

#define cmpp__scan_int2(SZ,PFMT,Z,N,TGT)        \
  (N<SZ) && 1==sscanf((char const *)Z, "%" #SZ PFMT, TGT)

bool cmpp__is_int(unsigned char const *z, unsigned n,
                  int *pOut){
  int d = 0;
  return cmpp__scan_int2(16, PRIi32, z, n, pOut ? pOut : &d);
}

bool cmpp__is_int64(unsigned char const *z, unsigned n, int64_t *pOut){
  int64_t d = 0;
  return cmpp__scan_int2(24, PRIi64, z, n, pOut ? pOut : &d);
}
#undef cmpp__scan_int2

/**
   Impl for the -Fx=filename flag.

   TODO?: refactor to take an optional zVal to make it suitable for
   the public API. This impl requires that zKey contain
   "key=filename".
*/
static int cmpp__set_file(cmpp *pp, unsigned const char * zKey,
                          cmpp_ssize_t nKey){
  if(ppCode) return ppCode;
  CmppKvp kvp = CmppKvp_empty;
  nKey = cmpp__strlenu(zKey, nKey);
  if( CmppKvp_parse(pp, &kvp, zKey, nKey, CmppKvp_op_eq1) ){
    return ppCode;
  }
  sqlite3_stmt * q = 0;
  FileWrapper fw = FileWrapper_empty;
  if( cmpp__FileWrapper_open(pp, &fw, (char const *)kvp.v.z, "rb") ){
    assert(ppCode);
    return ppCode;
  }
  cmpp__FileWrapper_slurp(pp, &fw);
  q = cmpp__stmt(pp, CmppStmt_defIns, false);
  if( q && 0==cmpp__bind_textn(pp, q, 2, kvp.k.z, (int)kvp.k.n) ){
    //g_warn("zKey=%.*s", (int)kvp.k.n, kvp.k.z);
    if( pp->pimpl->flags.chompF ){
      FileWrapper_chomp(&fw);
    }
    if( fw.nContent ){
      cmpp__bind_textx(pp, q, 3, fw.zContent,
                       (cmpp_ssize_t)fw.nContent, sqlite3_free);
      fw.zContent = 0 /* transferred ownership */;
      fw.nContent = 0;
    }else{
      cmpp__bind_null(pp, q, 2);
    }
    cmpp__step(pp, q, true);
    g_debug(pp,2,("define: %s%s%s\n",
                  kvp.k.z,
                  kvp.v.z ? " with value " : "",
                  kvp.v.z ? (char const *)kvp.v.z : ""));
  }
  FileWrapper_close(&fw);
  return ppCode;
}

CMPP__EXPORT(int, cmpp_has)(cmpp *pp, const char * zName, cmpp_ssize_t nName){
  int rc = 0;
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defHas, false);
  if( q ){
    nName = cmpp__strlen(zName, nName);
    cmpp__bind_textn(pp, q, 1, ustr_c(zName), nName);
    if(SQLITE_ROW == cmpp__step(pp, q, true)){
      rc = 1;
    }else{
      rc = 0;
    }
    g_debug(pp,1,("has [%s] ?= %d\n",zName, rc));
  }
  return rc;
}

int cmpp__get_bool(cmpp *pp, unsigned const char *zName, cmpp_ssize_t nName){
  int rc = 0;
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defGetBool, false);
  if( q ){
    nName = cmpp__strlenu(zName, nName);
    cmpp__bind_textn(pp, q, 1, zName, nName);
    assert(0==ppCode);
    if(SQLITE_ROW == cmpp__step(pp, q, false)){
      rc = sqlite3_column_int(q, 0);
    }else{
      rc = 0;
      cmpp__affirm_undef_policy(pp, zName, nName);
    }
    cmpp__stmt_reset(q);
  }
  return rc;
}

int cmpp__get_int(cmpp *pp, unsigned const char * zName,
                  cmpp_ssize_t nName, int *pOut ){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defGetInt, false);
  if( q ){
    nName = cmpp__strlenu(zName, nName);
    cmpp__bind_textn(pp, q, 1, zName, nName);
    assert(0==ppCode);
    if(SQLITE_ROW == cmpp__step(pp, q, false)){
      *pOut = sqlite3_column_int(q,0);
    }else{
      cmpp__affirm_undef_policy(pp, zName, nName);
    }
    cmpp__stmt_reset(q);
  }
  return ppCode;
}

int cmpp__get_b(cmpp *pp, unsigned const char * zName,
                cmpp_ssize_t nName, cmpp_b * os, bool enforceUndefPolicy){
  int rc = 0;
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defGet, false);
  if( q ){
    nName = cmpp__strlenu(zName, nName);
    cmpp__bind_textn(pp, q, 1, zName, nName);
    int n = 0;
    if(SQLITE_ROW == cmpp__step(pp, q, false)){
      const unsigned char * z = sqlite3_column_text(q, 3);
      n = sqlite3_column_bytes(q, 3);
      cmpp_b_append4(pp, os, z, (cmpp_size_t)n);
      rc = 1;
    }else{
      if( enforceUndefPolicy ){
        cmpp__affirm_undef_policy(pp, zName, nName);
      }
      rc = 0;
    }
    cmpp__stmt_reset(q);
    g_debug(pp,1,("get-define [%.*s] ?= %d %.*s\n",
                  nName, zName, rc, os->n, os->z));
  }
  return rc;
}

int cmpp__get(cmpp *pp, unsigned const char * zName,
              cmpp_ssize_t nName, unsigned char **zVal,
              unsigned int *nVal){
  int rc = 0;
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defGet, false);
  if( q ){
    nName = cmpp__strlenu(zName, nName);
    cmpp__bind_textn(pp, q, 1, zName, nName);
    int n = 0;
    if(SQLITE_ROW == cmpp__step(pp, q, false)){
      const unsigned char * z = sqlite3_column_text(q, 3);
      n = sqlite3_column_bytes(q, 3);
      if( nVal ) *nVal = (unsigned)n;
      *zVal = ustr_nc(sqlite3_mprintf("%.*s", n, z))
        /* TODO? Return NULL for the n==0 case? */;
      if( n && cmpp_check_oom(pp, *zVal) ){
        assert(!*zVal);
      }else{
        rc = 1;
      }
    }else{
      cmpp__affirm_undef_policy(pp, zName, nName);
      rc = 0;
    }
    cmpp__stmt_reset(q);
    g_debug(pp,1,("get-define [%.*s] ?= %d %.*s\n",
                  nName, zName, rc,
                  *zVal ? n : 0,
                  *zVal ? (char const *)*zVal : "<NULL>"));
  }
  return rc;
}

CMPP__EXPORT(int, cmpp_undef)(cmpp *pp, const char * zKey,
                                unsigned int *nRemoved){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defDel, false);
  if( q ){
    unsigned int const n = strlen(zKey);
    cmpp__bind_textn(pp, q, 1, ustr_c(zKey), (cmpp_ssize_t)n);
    cmpp__step(pp, q, true);
    if( nRemoved ){
      *nRemoved = (unsigned)sqlite3_changes(pp->pimpl->db.dbh);
    }
    g_debug(pp,2,("undefine: %.*s\n",n, zKey));
  }
  return ppCode;
}

int cmpp__include_dir_add(cmpp *pp, const char * zDir, int priority, int64_t * pRowid){
  if( pRowid ) *pRowid = 0;
  if( !ppCode && zDir && *zDir ){
    sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_inclPathAdd, false);
    if( q ){
      /* TODO: normalize zDir before insertion so that a/b and a/b/
         are equivalent.  The relavent code is in another tree,
         awaiting a decision on whether to import it or re-base cmpp
         on top of that library (which would, e.g., replace cmpp_b
         with that one, which is more mature).
      */
      cmpp__bind_int(pp, q, 1, priority);
      cmpp__bind_textn(pp, q, 2, ustr_c(zDir), -1);
      int const rc = cmpp__step(pp, q, false);
      if( SQLITE_ROW==rc ){
        ++pp->pimpl->flags.nIncludeDir;
        if( pRowid ){
          *pRowid = sqlite3_column_int64(q, 0);
        }
      }
      cmpp__stmt_reset(q);
      /*g_warn("inclpath add: rc=%d rowid=%" PRIi64 " prio=%d %s",
        rc, pRowid ? *pRowid : 0, priority, zDir);*/
      g_debug(pp,2,("inclpath add: prio=%d %s\n", priority, zDir));
    }
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_include_dir_add)(cmpp *pp, const char * zDir){
  return cmpp__include_dir_add(pp, zDir, 0, NULL);
}

int cmpp__include_dir_rm_id(cmpp *pp, int64_t rowid){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_inclPathRmId, true);
  if( q ){
    /* Hoop-jumping to allow this to work even if pp's in an error
       state. */
    int rc = sqlite3_bind_int64(q, 1, rowid);
    if( 0==rc ){
      rc = sqlite3_step(q);
      if( SQLITE_ROW==rc ){
        --pp->pimpl->flags.nIncludeDir;
        rc = 0;
      }else if( SQLITE_DONE==rc ){
        rc = 0;
      }
    }
    if( rc && !ppCode ){
      cmpp__db_rc(pp, rc, sqlite3_sql(q));
    }
    cmpp__stmt_reset(q);
    g_debug(pp,2,("inclpath rm #%"PRIi64 "\n", rowid));
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_module_dir_add)(cmpp *pp, const char * zDirs){
#if CMPP_ENABLE_DLLS
  if( !ppCode ){
    cmpp_b * const ob = &pp->pimpl->mod.path;
    if( !zDirs && !ob->n ){
      zDirs = getenv("CMPP_MODULE_PATH");
      if( !zDirs ){
        zDirs = CMPP_MODULE_PATH;
      }
    }
    if( !zDirs || !*zDirs ) return 0;
    char const * z = zDirs;
    char const * const zEnd = zDirs + strlen(zDirs);
    if( 0==cmpp_b_reserve3(pp, ob, ob->n + (zEnd - z) + 3) ){
      unsigned char * zo = ob->z + ob->n;
      unsigned i = 0;
      for( ; z < zEnd && !ppCode; ++z ){
        switch( *z ){
          case CMPP_PATH_SEPARATOR:
            *zo++ = pp->pimpl->mod.pathSep;
            break;
          default:
            if( 1==++i && ob->n ){
              cmpp_b_append_ch(ob, pp->pimpl->mod.pathSep);
            }
            *zo++ = *z;
            break;
        }
      }
      *zo = 0;
      ob->n = (zo - ob->z);
    }
  }
  return ppCode;
#else
  return CMPP_RC_UNSUPPORTED;
#endif
}

CMPP__EXPORT(int, cmpp_db_name_set)(cmpp *pp, const char * zName){
  if( 0==ppCode ){
    cmpp__pi(pp);
    if( pi->db.dbh ){
      return cmpp__err(pp, CMPP_RC_MISUSE,
                       "DB name cannot be set after db initialization.");
    }
    if( zName ){
      char * const z = sqlite3_mprintf("%s", zName);
      if( 0==cmpp_check_oom(pp, z) ){
        cmpp_mfree(pi->db.zName);
        pi->db.zName = z;
      }
    }else{
      cmpp_mfree(pi->db.zName);
      pi->db.zName = 0;
    }
  }
  return ppCode;
}

bool cmpp__is_legal_key(unsigned char const *zName,
                        cmpp_size_t n,
                        unsigned char const **zErrPos,
                        bool equalIsLegal){
  if( !n || n>64/*arbitrary*/ ){
    if( zErrPos ) *zErrPos = 0;
    return false;
  }
  unsigned char const * z = zName;
  unsigned char const * const zEnd = zName + n;
  for( ; z<zEnd; ++z ){
    if( !((*z>='a' && *z<='z')
          || (*z>='A' && *z<='Z')
          || (z>zName &&
              ('-'==*z
               /* This is gonna bite us if we extend the expresions to
                  support +/-. Expressions currently parse X=Y (no
                  spaces) as the three tokens X = Y, but we'd need to
                  require a space between X-Y in expressions because
                  '-' is a legal symbol character. i've looked at
                  making '-' illegal but it's just too convenient for
                  use in define keys.  Once one is used to
                  tcl-style-naming of stuff, it's painful to have to go
                  back to snake_case.
               */
               || (*z>='0' && *z<='9')))
          || (*z>='.' && *z<='/')
          || (*z==':')
          || (*z=='_')
          || (equalIsLegal && z>zName && '='==*z)
          || (*z & 0x80)
        ) ){
      if( zErrPos ) *zErrPos = z;
      return false;
    }
  }
  return true;
}

bool cmpp_is_legal_key(unsigned char const *zName,
                       cmpp_size_t n,
                       unsigned char const **zErrPos){
  return cmpp__is_legal_key(zName, n, zErrPos, false);
}

int cmpp__legal_key_check(cmpp *pp, unsigned char const *zKey,
                          cmpp_ssize_t nKey, bool permitEqualSign){
  if( !ppCode ){
    unsigned char const *zAt = 0;
    nKey = cmpp__strlenu(zKey, nKey);
    if( !cmpp__is_legal_key(zKey, nKey, &zAt, permitEqualSign) ){
      cmpp__err(pp, CMPP_RC_SYNTAX,
                "Illegal character 0x%02x in key [%.*s]",
                (int)*zAt, nKey, zKey);
    }
  }
  return ppCode;
}

CMPP__EXPORT(bool, cmpp_next_chunk)(unsigned char const **zPos,
                     unsigned char const *zEnd,
                     unsigned char chSep,
                     cmpp_size_t *pCounter){
  assert( zPos );
  assert( *zPos );
  assert( zEnd );
  if( *zPos >= zEnd ) return false;
  unsigned char const * z = *zPos;
  while( z<zEnd ){
    if( chSep==*z ){
      ++z;
      if( pCounter ) ++*pCounter;
      break;
    }
    ++z;
  }
  if( *zPos==z ) return false;
  *zPos = z;
  return true;
}

/**
   Scans dx for the next newline. It updates ln to contain the result,
   which includes the trailing newline unless EOF is hit before a
   newline.

   Returns true if it has input, false at EOF. It has no error
   conditions beyond invalid dx->pimpl state, which "doesn't happen".
*/
//static
bool cmpp__dx_next_line(cmpp_dx * const dx, CmppDLine *ln){
  assert( !dxppCode );
  cmpp_dx_pimpl * const dxp = dx->pimpl;
  if(!dxp->pos.z) dxp->pos.z = dxp->zBegin;
  assert( dxp->zEnd );
  if( dxp->pos.z>=dxp->zEnd ){
    return false;
  }
  assert( (dxp->pos.z==dxp->zBegin || dxp->pos.z[-1]=='\n')
          && "Else we've mismanaged something.");
  cmpp__dx_pi(dx);
  ln->lineNo = dpi->pos.lineNo;
  ln->zBegin = dpi->pos.z;
  ln->zEnd = ln->zBegin;
  return cmpp_next_chunk(&ln->zEnd, dpi->zEnd, (unsigned char)'\n',
                         &dpi->pos.lineNo);
}

/**
   Scans [dx->pos.z,dx->zEnd) for a directive delimiter. Emits any
   non-delimiter output found along the way to dx->pp's output
   channel.

   This updates dx->pimpl->pos.z and dx->pimpl->pos.lineNo as it goes.

   If a delimiter is found, it sets *gotOne to true and updates
   dx->pimpl->dline to point to the remainder of that line. On no match
   *gotOne will be false and EOF will have been reached.

   Returns dxppCode. If it returns non-0 then the state of dx's
   tokenization pieces are unspecified. i.e. it's illegal to call this
   again without a reset.
*/
static int cmpp_dx_delim_search(cmpp_dx * const dx, bool * gotOne){
  if( dxppCode ) return dxppCode;
  cmpp_dx_pimpl * const dxp = dx->pimpl;
  if(!dxp->pos.z) dxp->pos.z = dxp->zBegin;
  if( dxp->pos.z>=dxp->zEnd ){
    *gotOne = false;
    return 0;
  }
  assert( (dxp->pos.z==dxp->zBegin || dxp->pos.z[-1]=='\n')
          && "Else we've mismanaged something.");
  cmpp__pi(dx->pp);
  cmpp__delim const * const delim = cmpp__dx_delim(dx);
  if(!delim) {
    return cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                           "The directive delimiter stack is empty.");
  }
  unsigned char const * const zD = delim->open.z;
  unsigned short const nD = delim->open.n;
  unsigned char const * const zEnd = dxp->zEnd;
  unsigned char const * zLeft = dxp->pos.z;
  unsigned char const * z = zLeft;
  assert(zD);
  assert(nD);
#if 0
  assert( 0==*zEnd && "Else we'll misinteract with strcspn()" );
  if( *zEnd ){
    return cmpp_dx_err(dx, CMPP_RC_RANGE,
                      "Input must be NUL-terminated.");
  }
#endif
  ++dxp->flags.countLines;
  while( z<zEnd && '\n'==*z ){
    /* Skip leading newlines. We have to delay the handling of
       leading whitepace until later so that:

       |  #if
       |^^ those two spaces do not get emitted.
    */
    ++dxp->pos.lineNo;
    ++z;
  }
#define tflush                                              \
  if( z>zEnd ) z=zEnd;                                      \
  if( z>zLeft && cmpp_dx_out_expand(dx, &pi->out, zLeft,    \
                                    (cmpp_size_t)(z-zLeft), \
                                    cmpp_atpol_CURRENT) ){  \
    --dxp->flags.countLines;                                \
    return dxppCode;                                        \
  } zLeft = z

  CmppDLine * const dline = &dxp->dline;
  bool atBOL = true /* At the start of a line? Successful calls to
                       this always end at either BOL or EOF. */;
  if( 0 ){
    g_warn("scanning... <<%.*s...>>",
           (zEnd-z)>20?20:(zEnd-z), z);
  }
  while( z<zEnd && !dxppCode ){
    if( !atBOL ){
      /* We're continuing the scan of a line on which the first bytes
         didn't match a delimiter. */
      while( z<zEnd ){
        while((z<zEnd && '\n'==*z)
              || (z+1<zEnd && '\r'==*z && '\n'==z[1]) ){
          ++dxp->pos.lineNo;
          z += 1 + ('\r'==*z);
          atBOL = true;
        }
        if( atBOL ){
          break;
        }
        ++z;
      }
      if( !atBOL ) break;
    }
    if( 0 ){
      g_warn("at BOL... <<%.*s...>>",
             (zEnd-z) > 20 ? 20 : (zEnd-z), z);
    }

    /* We're at BOL. Check for a delimiter with optional leading
       spaces. */
    tflush;
    cmpp_skip_space(&z, zEnd);
    int const skip = cmpp_isnl(z, zEnd);
    if( skip ){
      /* Special case: a line comprised solely of whitespace. If we
         don't catch this here, we won't recognize a delimiter which
         starts on the next line. */
      tflush;
      z += skip;
      ++dxp->pos.lineNo;
      continue;
    }
    if( 0 ){
      g_warn("at BOL... <<%.*s...>>",
             (zEnd-z) > 20 ? 20 : (zEnd-z), z);
    }
    if( z + nD>zEnd ){
      /* Too short for a delimiter. We'll catch the z+nD==zEnd corner
         case in a moment. */
      z = zEnd;
      break;
    }
    if( memcmp(z, zD, nD) ){
      /* Not a delimiter. Keep trying. */
      atBOL = false;
      ++z;
      continue;
    }

    /* z now points to a delimiter which sits at the start of a line
       (ignoring leading spaces). */
    z += nD /* skip the delimiter */;
    cmpp_skip_space(&z, zEnd) /* skip spaces immediately following
                                    the delimiter. */;
    if( z>=zEnd || cmpp_isnl(z, zEnd) ){
      dxserr("No directive name found after %s.", zD);
      /* We could arguably treat this as no match and pass this line
         through as-is but that currently sounds like a pothole. */
      break;
    }
    /* Set up dx->pimpl->dline to encompass the whole directive line sans
       delimiter and leading spaces. */
    dline->zBegin = z
      /* dx->pimpl->dline starts at the directive name and extends until the
         next EOL/EOF. We don't yet know if it's a legal directive
         name - cmpp_dx_next() figures that part out. */;
    dline->lineNo = dxp->pos.lineNo;
    /* Now find the end of the line or EOF, accounting for
       backslash-escaped newlines and _not_ requiring backslashes to
       escape newlines inside of {...}, (...), or [...]. We could also
       add the double-quotes to this, but let's start without that. */
    bool keepGoing = true;
    zLeft = z;
    while( keepGoing && z<zEnd ){
      switch( *z ){
        case '(': case '{': case '[':{
          zLeft = z;
          if( cmpp__find_closing2(dx->pp, &z, zEnd, &dxp->pos.lineNo) ){
            --dxp->flags.countLines;
            return dxppCode;
          }
          ++z /* group-closing character */;
          /*
            Sidebar: this only checks top-level groups. It is
            possible that an inner group is malformed, e.g.:

            { ( }

            It's also possible that that's perfectly legal for a
            specific use case.

            Such cases will, if they're indeed syntax errors, be
            recognized as such in the arguments-parsing
            steps. Catching them here would require that we
            recursively validate all of [zLeft,z) for group
            constructs, whereas that traversal happens as a matter of
            course in argument parsing. It would also require the
            assumption that such constructs are not legal, which is
            invalid once we start dealing with free-form input like
            #query SQL.
          */
          break;
        }
        case '\n':
          assert( z!=dline->zBegin && "Checked up above" );
          if( '\\'==z[-1]
              || (z>zLeft+1 && '\r'==z[-1] && '\\'==z[-2]) ){
            /* Backslash-escaped newline. */
            ++z;
          }else{
            /* EOL for this directive. */
            keepGoing = false;
          }
          ++dxp->pos.lineNo;
          break;
        default:
          ++z;
      }
    }
    assert( z==zEnd || '\n'==*z );
    dline->zEnd = z;
    dxp->pos.z = dline->zEnd + 1
      /* For the next call to this function, skip the trailing newline
         or EOF */;
    assert( dline->zBegin < dline->zEnd && "Was checked above" );
    if( 0 ){
      g_warn("line= %u <<%.*s>>", (dline->zEnd-dline->zBegin),
             (dline->zEnd-dline->zBegin), dline->zBegin);
    }
    *gotOne = true;
    assert( !dxppCode );
    --dxp->flags.countLines;
    return 0;
  }
  /* No directives found. We're now at EOL or EOF. Flush any pending
     LHS content. */
  tflush;
  dx->pimpl->pos.z = z;
  *gotOne = false;
  return dxppCode;
#undef tflush
}

int CmppKvp_parse(cmpp *pp, CmppKvp * p, unsigned char const *zKey,
                   cmpp_ssize_t nKey, CmppKvp_op_e opPolicy){
  if(ppCode) return ppCode;
  char chEq = 0;
  char opLen = 0;
  *p = CmppKvp_empty;
  p->k.z = zKey;
  p->k.n = cmpp__strlenu(zKey, nKey);
  switch( opPolicy ){
    case CmppKvp_op_none:// break;
    case CmppKvp_op_eq1:
      chEq = '=';
      opLen = 1;
      break;
    default:
      assert(!"don't use these");
      /* no longer todo: ==, !=, <=, <, >, >= */
      chEq = '=';
      opLen = 1;
      break;
  }
  assert( chEq );
  p->op = CmppKvp_op_none;
  unsigned const char * const zEnd = p->k.z + p->k.n;
  for(unsigned const char * zPos = p->k.z ; *zPos && zPos<zEnd ; ++zPos) {
    if( chEq==*zPos ){
      if( CmppKvp_op_none==opPolicy ){
        cmpp__err(pp, CMPP_RC_SYNTAX,
                      "Illegal operator in key: %s", zKey);
      }else{
        p->op = CmppKvp_op_eq1;
        p->k.n = (unsigned)(zPos - ustr_c(zKey));
        zPos += opLen;
        assert( zPos <= zEnd );
        p->v.z = zPos;
        p->v.n = (unsigned)(zEnd - zPos);
      }
      break;
    }
  }
  cmpp__legal_key_check(pp, p->k.z, p->k.n, false);
  return ppCode;
}

int cmpp_array_reserve(cmpp *pp, void **list, cmpp_size_t nDesired,
                      cmpp_size_t * nAlloc, unsigned sizeOfEntry){
  int rc = pp ? ppCode : 0;
  if( 0==rc && nDesired > *nAlloc ){
    cmpp_size_t const nA = nDesired < 10 ? 10 : nDesired;
    void * const p = cmpp_mrealloc(*list, sizeOfEntry * nA);
    rc = cmpp_check_oom(pp, p);
    if( p ){
      memset((unsigned char *)p +
             (sizeOfEntry * *nAlloc), 0,
             sizeOfEntry * (nA - *nAlloc));
      *list = p;
      *nAlloc = nA;
    }
  }
  return rc;
}

CmppLvl * CmppLvlList_push(cmpp *pp, CmppLvlList *li){
  CmppLvl * p = 0;
  assert( li->list ? li->nAlloc : 0==li->nAlloc );
  if( 0==ppCode
      && 0==CmppLvlList_reserve(pp, li,
                                cmpp__li_reserve1_size(li,5)) ){
    p = li->list[li->n];
    if( !p ){
      p = cmpp__malloc(pp, sizeof(*p));
    }
    if( p ){
      li->list[li->n++] = p;
      *p = CmppLvl_empty;
    }
  }
  return p;
}

void CmppLvlList_pop(cmpp * const pp, CmppLvlList * const li,
                     CmppLvl * const lvl){
  assert( li->n );
  if( li->n ){
    if( lvl==li->list[li->n-1] ){
      *lvl = CmppLvl_empty;
      cmpp_mfree(lvl);
      li->list[--li->n] = 0;
    }else{
      if( pp ){
        cmpp_err_set(pp, CMPP_RC_ASSERT,
                     "Misuse of %s(): not passed the top of the stack. "
                     "The CmppLvl stack is now out of whack.",
                     __func__);
      }else{
        cmpp__fatal("Misuse of %s(): not passed the top of the stack",
              __func__);
      }
      /* do not free it - CmppLvlList_cleanup() will catch it. */
    }
  }
}

void CmppLvlList_cleanup(CmppLvlList *li){
  const CmppLvlList CmppLvlList_empty = CmppLvlList_empty_m;
  while( li->nAlloc ){
    cmpp_mfree( li->list[--li->nAlloc] );
  }
  cmpp_mfree(li->list);
  *li = CmppLvlList_empty;
}

static inline void CmppDList_entry_clean(CmppDList_entry * const e){
  if( e->d.impl.dtor ){
    e->d.impl.dtor( e->d.impl.state );
  }
  cmpp_mfree(e->zName);
  *e = CmppDList_entry_empty;
}

#if 0
CmppDList * CmppDList_reuse(CmppDList *li){
  while( li->n ){
    CmppDList_entry_clean( li->list[--li->n] );
  }
  return li;
}
#endif

void CmppDList_cleanup(CmppDList *li){
  static const CmppDList CmppDList_empty = CmppDList_empty_m;
  while( li->n ){
    CmppDList_entry_clean( li->list[--li->n] );
    cmpp_mfree( li->list[li->n] );
    li->list[li->n] = 0;
  }
  cmpp_mfree(li->list);
  *li = CmppDList_empty;
}

void CmppDList_unappend(CmppDList *li){
  assert( li->n );
  if( li->n ){
    CmppDList_entry_clean(li->list[--li->n]);
  }
}


/** bsearch()/qsort() comparison for (cmpp_d**), sorting by name. */
static
int CmppDList_entry_cmp_pp(const void *p1, const void *p2){
  CmppDList_entry const * eL = *(CmppDList_entry const * const *)p1;
  CmppDList_entry const * eR = *(CmppDList_entry const * const *)p2;
  return eL->d.name.n==eR->d.name.n
    ? memcmp(eL->d.name.z, eR->d.name.z, eL->d.name.n)
    : strcmp((char const *)eL->d.name.z,
             (char const *)eR->d.name.z);
}

static void CmppDList_sort(CmppDList * const li){
  if( li->n>1 ){
    qsort(li->list, li->n, sizeof(CmppDList_entry*),
          CmppDList_entry_cmp_pp);
  }
}

CmppDList_entry * CmppDList_append(cmpp *pp, CmppDList *li){
  CmppDList_entry * p = 0;
  assert( li->list ? li->nAlloc : 0==li->nAlloc );
  if( 0==ppCode
      && 0==cmpp_array_reserve(pp, (void **)&li->list,
                               cmpp__li_reserve1_size(li, 15),
                               &li->nAlloc, sizeof(p)) ){
    p = li->list[li->n];
    if( !p ){
      li->list[li->n] = p = cmpp__malloc(pp, sizeof(*p));
    }
    if( p ){
      ++li->n;
      *p = CmppDList_entry_empty;
    }
  }
  return p;
}

CmppDList_entry * CmppDList_search(CmppDList const * li,
                                       char const *zName){
  if( li->n > 2 ){
    CmppDList_entry const key = {
      .d = {
        .name = {
          .z = zName,
          .n = strlen(zName)
        }
      }
    };
    CmppDList_entry const * pKey = &key;
    CmppDList_entry ** pRv
      = bsearch(&pKey, li->list, li->n, sizeof(li->list[0]),
                CmppDList_entry_cmp_pp);
    //g_warn("search in=%s out=%s", zName, (pRv ? (*pRv)->d.name.z : "<null>"));
    return pRv ? *pRv : 0;
  }else{
    cmpp_size_t const nName = cmpp__strlen(zName, -1);
    for( cmpp_size_t i = 0; i < li->n; ++i ){
      CmppDList_entry * const e = li->list[i];
      if( nName==e->d.name.n && 0==strcmp(zName, e->d.name.z) ){
        //g_warn("search in=%s out=%s", zName, e->d.name.z);
        return e;
      }
    }
    return 0;
  }
}

void cmpp__delim_cleanup(cmpp__delim *d){
  cmpp__delim const dd = cmpp__delim_empty_m;
  cmpp_mfree(d->zOwns);
  *d = dd;
  assert(!d->zOwns);
  assert(d->open.z);
  assert(0==strcmp((char*)d->open.z, CMPP_DEFAULT_DELIM));
  assert(d->open.n == sizeof(CMPP_DEFAULT_DELIM)-1);
}

cmpp__delim * cmpp__delim_list_push(cmpp *pp, cmpp__delim_list *li){
  cmpp__delim * p = 0;
  assert( li->list ? li->nAlloc : 0==li->nAlloc );
  if( 0==ppCode
      && 0==cmpp_array_reserve(pp, (void **)&li->list,
                               cmpp__li_reserve1_size(li,4),
                               &li->nAlloc, sizeof(cmpp__delim)) ){
    p = &li->list[li->n++];
    *p = cmpp__delim_empty;
  }
  return p;
}

void cmpp__delim_list_cleanup(cmpp__delim_list *li){
  while( li->nAlloc ) cmpp__delim_cleanup(li->list + --li->nAlloc);
  cmpp_mfree(li->list);
  *li = cmpp__delim_list_empty;
}

CMPP__EXPORT(int, cmpp_dx_next)(cmpp_dx * const dx, bool * pGotOne){
  if( dxppCode ) return dxppCode;

  CmppDLine * const tok = &dx->pimpl->dline;
  if( !dx->pimpl->zBegin ){
    *pGotOne = false;
    return 0;
  }
  assert(dx->pimpl->zEnd);
  assert(dx->pimpl->zEnd > dx->pimpl->zBegin);
  *pGotOne = false;
  cmpp_dx__reset(dx);
  bool foundDelim = false;
  if( cmpp_dx_delim_search(dx, &foundDelim) || !foundDelim ){
    return dxppCode;
  }
  if( cmpp_args__init(dx->pp, &dx->pimpl->args) ){
    return dxppCode;
  }
  cmpp_skip_space( &tok->zBegin, tok->zEnd );
  g_debug(dx->pp,2,("Directive @ line %u: <<%.*s>>\n",
                   tok->lineNo,
                   (int)(tok->zEnd-tok->zBegin), tok->zBegin));
  /* Normalize the directive's line and parse arguments */
  const unsigned lineLen = (unsigned)(tok->zEnd - tok->zBegin);
  if(!lineLen){
    return cmpp_dx_err(dx, CMPP_RC_SYNTAX,
                       "Line #%u has no directive after %s",
                       tok->lineNo, cmpp_dx_delim(dx));
  }
  unsigned char const * zi = tok->zBegin /* Start of input */;
  unsigned char const * ziEnd = tok->zEnd /* Input EOF */;
  cmpp_b * const bufLine =
    cmpp_b_reuse(&dx->pimpl->buf.line)
    /* Slightly-transformed copy of the input. */;
  if( cmpp_b_reserve3(dx->pp, bufLine, lineLen+1) ){
    return dxppCode;
  }
  unsigned char * zo = bufLine->z /* Start of output */;
  unsigned char const * const zoEnd =
    zo + bufLine->nAlloc /* Output EOF. */;
  g_debug(dx->pp,2,("Directive @ line %u len=%u <<%.*s>>\n",
                    tok->lineNo, lineLen, lineLen, tok->zBegin));
  //memset(bufLine->z, 0, bufLine->nAlloc);
#define out(CH) if(zo==zoEnd) break; (*zo++)=CH
  /*
      bufLine is now populated with a copy of the whole input line.
      Now normalize that buffer a bit before trying to parse it.
  */
  unsigned char const * zEsc = 0;
  cmpp_dx_pimpl * const pimpl = dx->pimpl;
  for( ; zi<ziEnd && *zi && zo<zoEnd;
       ++zi ){
    /* Write the line to bufLine for the upcoming args parsing to deal
       with. Strip backslashes from backslash-escaped newlines.  We
       leave the newlines intact so that downstream error reporting
       can get more precise location info. Backslashes which do not
       precede a newline are retained.
    */
    switch((int)*zi){
      case (int)'\\':
        if( !zi[1] || zi==ziEnd-1 ){
          // special case: ending input with a backslash
          out(*zi);
          zEsc = 0;
        }else if( zEsc ){
          assert( zEsc==zi-1 );
          /* Put them both back. */
          out(*zEsc);
          out(*zi);
          zEsc = 0;
        }else{
          zEsc = zi;
        }
        break;
      case (int)'\n':
        out(*zi);
        zEsc = 0;
        break;
      default:
        if(zEsc){
          assert( zEsc==zi-1 );
          out(*zEsc);
          zEsc = 0;
        }
        out(*zi);
        break;
    }
  }
  if( zo>=zoEnd ){
    return cmpp_dx_err(dx, CMPP_RC_RANGE,
                      "Ran out of argument-processing space.");
  }
  *zo = 0;
#undef out
  bufLine->n = (cmpp_size_t)(zo - bufLine->z);
  if( 0 ) g_warn("bufLine.n=%u line=<<%s>>", bufLine->n, bufLine->z);
  /* Line has now been normalized into bufLine->z. */
  for( zo = bufLine->z; zo<zoEnd && *zo; ++zo ){
    /* NULL-terminate the directive so we can search for it. */
    if( cmpp_isspace(*zo) ){
      *zo = 0;
      break;
    }
  }
  unsigned char * const zDirective = bufLine->z;
  dx->d = cmpp__d_search3(dx->pp, (char const *)zDirective,
                          cmpp__d_search3_F_ALL);
  if( dxppCode ){
    return dxppCode;
  }else if(!dx->d){
    return cmpp_dx_err(dx, CMPP_RC_NOT_FOUND,
                       "Unknown directive at line %"
                       CMPP_SIZE_T_PFMT ": %.*s\n",
                       (unsigned)tok->lineNo,
                       (int)bufLine->n, bufLine->z);
  }
  assert( zDirective == bufLine->z );
  const bool isCall
    = dx->pimpl->args.pimpl->isCall
    = dx->pimpl->flags.nextIsCall;
  dx->pimpl->flags.nextIsCall = false;
  if( isCall ){
    if( cmpp_d_F_NO_CALL & dx->d->flags ){
      return cmpp_dx_err(dx, CMPP_RC_SYNTAX,
                         "%s%s cannot be used in a [call] context.",
                         cmpp_dx_delim(dx),
                         dx->d->name.z);
    }
  }else if( cmpp_d_F_CALL_ONLY & dx->d->flags ){
    return cmpp_dx_err(dx, CMPP_RC_TYPE,
                       "'%s' is a call-only directive, "
                       "not legal here.", dx->d->name.z);
  }
  if( bufLine->n > dx->d->name.n ){
    dx->args.z = zDirective + dx->d->name.n + 1;
    assert( dx->args.z > bufLine->z );
    assert( dx->args.z <= bufLine->z+bufLine->n );
    dx->args.nz = cmpp__strlenu(dx->args.z, -1);
    assert( bufLine->nAlloc > dx->args.nz );
  }else{
    dx->args.z = ustr_c("\0");
    dx->args.nz = 0;
  }
  if( 0 ){
    g_warn("bufLine.n=%u zArgs offset=%u line=<<%s>>\nzArgs=<<%s>>",
           bufLine->n, (dx->args.z - zDirective), bufLine->z, dx->args.z);
  }
  cmpp_skip_snl(&dx->args.z, dx->args.z + dx->args.nz);
  if(0){
    g_warn("zArgs %u = <<%.*s>>", (int)dx->args.nz,
           (int)dx->args.nz, dx->args.z);
  }
  assert( !pimpl->buf.argsRaw.n );
  if( dx->args.nz ){
    if( 0 ){
      g_warn("lineLen=%u zargs len=%u: [%.*s]\n",
             (unsigned)lineLen,
             (int)dx->args.nz, (int)dx->args.nz,
             dx->args.z
      );
    }
    if( cmpp_b_append4(dx->pp, &pimpl->buf.argsRaw,
                            dx->args.z, dx->args.nz) ){
      return dxppCode;
    }
  }
  assert( !pimpl->args.arg0 );
  assert( !pimpl->args.argc );
  assert( !pimpl->args.pimpl->argOut.n );
  assert( !pimpl->args.pimpl->argli.n );
  assert( dx->args.z );
  if( //1 || //pleases valgrind. Well, it did at one point.
      !cmpp_dx_is_eliding(dx) || 0!=(cmpp_d_F_FLOW_CONTROL & dx->d->flags) ){
    if( cmpp_d_F_ARGS_LIST & dx->d->flags ){
      cmpp_dx_args_parse(dx, &pimpl->args);
    }else if( cmpp_d_F_ARGS_RAW & dx->d->flags ){
      /* Treat rest of line as one token */
      cmpp_arg * const arg =
        CmppArgList_append(dx->pp, &pimpl->args.pimpl->argli);
      if( !arg ) return dxppCode;
      pimpl->args.arg0 = arg;
      pimpl->args.argc = 1;
      arg->ttype = cmpp_TT_RawLine;
      arg->z = pimpl->buf.argsRaw.z;
      arg->n = pimpl->buf.argsRaw.n;
      //g_warn("arg->n/z=%u %s", (unsigned)arg->n, arg->z);
    }
  }
  if( 0==dxppCode ){
    dx->args.arg0 = pimpl->args.arg0;
    dx->args.argc = pimpl->args.argc;
  }
  *pGotOne = true;
  return dxppCode;
}

CMPP_EXPORT bool cmpp_dx_is_call(cmpp_dx * const dx){
  return dx->pimpl->args.pimpl->isCall;
}

CMPP__EXPORT(int, cmpp_d_register)(cmpp * pp, cmpp_d_reg const * r,
                    cmpp_d ** dOut){
  CmppDList_entry * e1 = 0, * e2 = 0;
  bool const isCallOnly =
    (cmpp_d_F_CALL_ONLY & r->opener.flags);
  if( ppCode ){
    goto end;
  }
  if( (cmpp_d_F_NOT_IN_SAFEMODE & (r->opener.flags | r->closer.flags))
      && (cmpp_ctor_F_SAFEMODE & pp->pimpl->flags.newFlags) ){
    cmpp__err(pp, CMPP_RC_ACCESS,
              "Directive %s%s flag cmpp_d_F_NOT_IN_SAFE_MODE is set "
              "and the preprocessor is running in safe mode.",
              cmpp__pp_zdelim(pp), r->name);
    goto end;
  }
  if( isCallOnly && r->closer.f ){
    cmpp__err(pp, CMPP_RC_MISUSE,
              "Call-only directives may not have a closing directive.");
    goto end;
  }
#if 0
  if( pp->pimpl->dx ){
    cmpp__err(pp, CMPP_RC_MISUSE,
              "Directives may not be added while a "
              "directive is running."
              /* because that might reallocate being-run directives.
                 2025-10-25: that's since been resolved but we need a
                 use case before enabling this.
              */);
    goto end;
  }
#endif
  if( !pp->pimpl->flags.isInternalDirectiveReg
      && !cmpp_is_legal_key(ustr_c(r->name),
                            cmpp__strlen(r->name,-1), NULL) ){
    cmpp__err(pp, CMPP_RC_RANGE,
              "\"%s\" is not a legal directive name.", r->name);
    goto end;
  }
  if( cmpp__d_search(pp, r->name) ){
    cmpp__err(pp, CMPP_RC_ALREADY_EXISTS,
              "Directive name '%s' is already in use.",
              r->name);
    goto end;
  }
  e1 = CmppDList_append(pp, &pp->pimpl->d.list);
  if( !e1 ) goto end;
  e1->d.impl.callback = r->opener.f;
  e1->d.impl.state = r->state;
  e1->d.impl.dtor = r->dtor;
  if( pp->pimpl->flags.isInternalDirectiveReg ){
    e1->d.flags = r->opener.flags;
  }else{
    e1->d.flags = r->opener.flags & cmpp_d_F_MASK;
  }
  e1->zName = sqlite3_mprintf("%s", r->name);
  if( 0==cmpp_check_oom(pp, e1->zName) ){
    //e1->reg = *r; e1->reg.zName = e1->zName;
    e1->d.name.z = e1->zName;
    e1->d.name.n = strlen(e1->zName);
    if( r->closer.f
        && (e2 = CmppDList_append(pp, &pp->pimpl->d.list)) ){
      e2->d.impl.callback = r->closer.f;
      e2->d.impl.state = r->state;
      if( pp->pimpl->flags.isInternalDirectiveReg ){
        e2->d.flags = r->closer.flags;
      }else{
        e2->d.flags = r->closer.flags & cmpp_d_F_MASK;
      }
      e1->d.closer = &e2->d;
      e2->zName = sqlite3_mprintf("/%s", r->name);
      if( 0==cmpp_check_oom(pp, e2->zName) ){
        e2->d.name.z = e2->zName;
        e2->d.name.n = e1->d.name.n + 1;
      }
    }
  }

end:
  if( ppCode ){
    if( e2 ) CmppDList_unappend(&pp->pimpl->d.list);
    if( e1 ) CmppDList_unappend(&pp->pimpl->d.list);
    else if( r->dtor ){
      r->dtor( r->state );
    }
  }else{
    CmppDList_sort(&pp->pimpl->d.list);
    if( dOut ){
      *dOut = &e1->d;
    }
    if( 0 ){
      g_warn("Registered: %s%s%s", e1->zName,
             e2 ? " and " : "",
             e2 ? e2->zName : "");
    }
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_dx_consume)(cmpp_dx * const dx, cmpp_outputer * const os,
                    cmpp_d const * const * const dClosers,
                    unsigned nClosers,
                    cmpp_flag32_t flags){
  assert( !dxppCode );
  bool gotOne = false;
  cmpp_outputer const oldOut = dx->pp->pimpl->out;
  bool const allowOtherDirectives =
    (flags & cmpp_dx_consume_F_PROCESS_OTHER_D);
  cmpp_d const * const d = cmpp_dx_d(dx);
  cmpp_size_t const lineNo = dx->pimpl->dline.lineNo;
  bool const pushAt = (cmpp_dx_consume_F_RAW & flags);
  if( pushAt && cmpp_atpol_push(dx->pp, cmpp_atpol_OFF) ){
    return dxppCode;
  }
  if( os ){
    dx->pp->pimpl->out = *os;
  }
  while( 0==dxppCode
         && 0==cmpp_dx_next(dx, &gotOne)
         /* ^^^^^^^ resets dx->d, dx->pimpl->args and friends */ ){
    if( !gotOne ){
      dxserr("No closing directive found for "
             "%s%s opened on line %" CMPP_SIZE_T_PFMT ".",
             cmpp_dx_delim(dx), d->name.z, lineNo);
    }else{
      cmpp_d const * const d2 = cmpp_dx_d(dx);
      gotOne = false;
      for( unsigned i = 0; !gotOne && i < nClosers; ++i ){
        gotOne = d2==dClosers[i];
      }
      //g_warn("gotOne=%d d2=%s", gotOne, d2->name.z);
      if( gotOne ) break;
      else if( !allowOtherDirectives ){
        dxserr("%s%s at line %" CMPP_SIZE_T_PFMT
               " may not contain %s%s.",
               cmpp_dx_delim(dx), d->name.z, lineNo,
               cmpp_dx_delim(dx), d2->name.z);
      }else{
        cmpp_dx_process(dx);
      }
    }
  }
  if( pushAt ){
    cmpp_atpol_pop(dx->pp);
  }
  if( os ){
    dx->pp->pimpl->out = oldOut;
  }
  return dxppCode;
}

CMPP__EXPORT(int, cmpp_dx_consume_b)(cmpp_dx * const dx, cmpp_b * const b,
                                     cmpp_d const * const * dClosers,
                                     unsigned nClosers, cmpp_flag32_t flags){
  cmpp_outputer oss = cmpp_outputer_b;
  oss.state = b;
  return cmpp_dx_consume(dx, &oss, dClosers, nClosers, flags);
}

char const * cmpp__atpol_name(cmpp *pp, cmpp_atpol_e p){
again:
  switch(p){
    case cmpp_atpol_CURRENT:{
      if( pp ){
        assert( p!=cmpp__policy(pp, at) );
        p = cmpp__policy(pp, at);
        pp = 0;
        goto again;
      }
      return NULL;
    }
    case cmpp_atpol_invalid: return NULL;
    case cmpp_atpol_OFF: return "off";
    case cmpp_atpol_RETAIN: return "retain";
    case cmpp_atpol_ELIDE: return "elide";
    case cmpp_atpol_ERROR: return "error";
  }
  return NULL;
}

cmpp_atpol_e cmpp_atpol_from_str(cmpp * const pp, char const *z){
  cmpp_atpol_e rv = cmpp_atpol_invalid;
  if( 0==strcmp(z,      "retain") )  rv = cmpp_atpol_RETAIN;
  else if( 0==strcmp(z, "elide") )   rv = cmpp_atpol_ELIDE;
  else if( 0==strcmp(z, "error") )   rv = cmpp_atpol_ERROR;
  else if( 0==strcmp(z, "off") )     rv = cmpp_atpol_OFF;
  if( pp ){
    if( cmpp_atpol_invalid==rv
        && 0==strcmp(z, "current") ){
      rv = cmpp__policy(pp,at);
    }else if( cmpp_atpol_invalid==rv ){
      cmpp__err(pp, CMPP_RC_RANGE,
                "Invalid @ policy value: %s."
                " Try one of retain|elide|error|off|current.", z);
    }else{
      cmpp__policy(pp,at) = rv;
    }
  }
  return rv;
}

int cmpp__StringAtIsOk(cmpp * pp, cmpp_atpol_e pol){
  if( 0==ppCode ){
    if( pol==cmpp_atpol_CURRENT ) pol=cmpp__policy(pp,at);
    if(cmpp_atpol_OFF==pol ){
      cmpp_err_set(pp, CMPP_RC_UNSUPPORTED,
                   "@policy is \"off\", so cannot use @\"strings\".");
    }
  }
  return ppCode;
}

cmpp__PodList_impl(PodList__atpol,cmpp_atpol_e)
cmpp__PodList_impl(PodList__unpol,cmpp_unpol_e)

int cmpp_atpol_push(cmpp * pp, cmpp_atpol_e pol){
  if( cmpp_atpol_CURRENT==pol ) pol = cmpp__policy(pp,at);
  assert( cmpp_atpol_CURRENT!=pol && "Else internal mismanagement." );
  if( 0==PodList__atpol_push(pp, &cmpp__epol(pp,at), pol)
      && 0!=cmpp_atpol_set(pp, pol)/*for validation*/ ){
    PodList__atpol_pop(&cmpp__epol(pp,at));
  }
  return ppCode;
}

void cmpp_atpol_pop(cmpp * pp){
  assert( cmpp__epol(pp,at).n );
  if( cmpp__epol(pp,at).n ){
    PodList__atpol_pop(&cmpp__epol(pp,at));
  }else if( !ppCode ){
    cmpp_err_set(pp, CMPP_RC_MISUSE,
                 "%s() called when no cmpp_atpol_push() is active.",
                 __func__);
  }
}

int cmpp_unpol_push(cmpp * pp, cmpp_unpol_e pol){
  if( 0==PodList__unpol_push(pp, &cmpp__epol(pp,un), pol)
      && cmpp_unpol_set(pp, pol)/*for validation*/ ){
    PodList__unpol_pop(&cmpp__epol(pp,un));
  }
  return ppCode;
}

void cmpp_unpol_pop(cmpp * pp){
  assert( cmpp__epol(pp,un).n );
  if( cmpp__epol(pp,un).n ){
    PodList__unpol_pop(&cmpp__epol(pp,un));
  }else if( !ppCode ){
    cmpp_err_set(pp, CMPP_RC_MISUSE,
                 "%s() called when no cmpp_unpol_push() is active.",
                 __func__);
  }
}

CMPP__EXPORT(cmpp_atpol_e, cmpp_atpol_get)(cmpp const * const pp){
  return cmpp__epol(pp,at).na
    ? cmpp__policy(pp,at) : cmpp_atpol_DEFAULT;
}

CMPP__EXPORT(int, cmpp_atpol_set)(cmpp * const pp, cmpp_atpol_e pol){
  if( 0==ppCode ){
    switch(pol){
      case cmpp_atpol_OFF:
      case cmpp_atpol_RETAIN:
      case cmpp_atpol_ELIDE:
      case cmpp_atpol_ERROR:
        assert(cmpp__epol(pp,at).na);
        cmpp__policy(pp,at) = pol;
        break;
      case cmpp_atpol_CURRENT:
        break;
      default:
        cmpp__err(pp, CMPP_RC_RANGE, "Invalid policy value: %d",
                  (int)pol);
    }
  }
  return ppCode;
}


char const * cmpp__unpol_name(cmpp *pp, cmpp_unpol_e p){
  (void)pp;
  switch(p){
    case cmpp_unpol_NULL: return "null";
    case cmpp_unpol_ERROR: return "error";
    case cmpp_unpol_invalid: return NULL;
  }
  return NULL;
}

cmpp_unpol_e cmpp_unpol_from_str(cmpp * const pp,
                                         char const *z){
  cmpp_unpol_e rv = cmpp_unpol_invalid;
  if( 0==strcmp(z, "null") )       rv = cmpp_unpol_NULL;
  else if( 0==strcmp(z, "error") ) rv = cmpp_unpol_ERROR;
  if( pp ){
    if( cmpp_unpol_invalid==rv
        && 0==strcmp(z, "current") ){
      rv = cmpp__policy(pp,un);
    }else if( cmpp_unpol_invalid==rv ){
      cmpp__err(pp, CMPP_RC_RANGE,
                "Invalid undefined key policy value: %s."
                " Try one of null|error.", z);
    }else{
      cmpp_unpol_set(pp, rv);
    }
  }
  return rv;
}

CMPP__EXPORT(cmpp_unpol_e, cmpp_unpol_get)(cmpp const * const pp){
  return cmpp__epol(pp,un).na
    ? cmpp__policy(pp,un) : cmpp_unpol_DEFAULT;
}

CMPP__EXPORT(int, cmpp_unpol_set)(cmpp * const pp, cmpp_unpol_e pol){
  if( 0==ppCode ){
    switch(pol){
      case cmpp_unpol_NULL:
      case cmpp_unpol_ERROR:
        cmpp__policy(pp,un) = pol;
        break;
      default:
        cmpp__err(pp, CMPP_RC_RANGE, "Invalid policy value: %d",
                  (int)pol);
    }
  }
  return ppCode;
}

/**
   Reminders to self re. savepoint tracking:

   cmpp_dx tracks per-input-source savepoints. We always want
   savepoints which are created via scripts to be limited to that
   script. cmpp instances, on the other hand, don't care about that.

   Thus we have two different APIs for starting/ending savepoints.
*/
CMPP__EXPORT(int, cmpp_sp_begin)(cmpp *pp){
  if( 0==ppCode ){
    sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_spBegin, true);
    assert( q || !"db init would have otherwise failed");
    if( q && SQLITE_DONE==cmpp__step(pp, q, true) ){
      ++pp->pimpl->flags.nSavepoint;
    }
  }
  return ppCode;
}

int cmpp__dx_sp_begin(cmpp_dx * const dx){
  if( 0==dxppCode && 0==cmpp_sp_begin(dx->pp) ){
    ++dx->pimpl->nSavepoint;
  }
  return dxppCode;
}

CMPP__EXPORT(int, cmpp_sp_rollback)(cmpp *const pp){
  /* Remember that rollback must (mostly) ignore the
     pending error state. */
  if( !pp->pimpl->flags.nSavepoint ){
    if( 0==ppCode ){
      cmpp__err(pp, CMPP_RC_MISUSE,
                "Cannot roll back: no active savepoint");
    }
  }else{
    sqlite3_stmt * q = cmpp__stmt(pp, CmppStmt_spRollback, true);
    assert( q || !"db init would have otherwise failed");
    if( q && SQLITE_DONE==cmpp__step(pp, q, true) ){
      q = cmpp__stmt(pp, CmppStmt_spRelease, true);
      if( q && SQLITE_DONE==cmpp__step(pp, q, true) ){
          --pp->pimpl->flags.nSavepoint;
      }
    }
  }
  return ppCode;
}

int cmpp__dx_sp_rollback(cmpp_dx * const dx){
  /* Remember that rollback must (mostly) ignore the pending error state. */
  if( !dx->pimpl->nSavepoint ){
    if( 0==dxppCode ){
      cmpp_dx_err(dx, CMPP_RC_MISUSE,
                  "Cannot roll back: no active savepoint");
    }
  }else{
    cmpp_sp_rollback(dx->pp);
    --dx->pimpl->nSavepoint;
  }
  return dxppCode;
}

CMPP__EXPORT(int, cmpp_sp_commit)(cmpp * const pp){
  if( 0==ppCode ){
    if( !pp->pimpl->flags.nSavepoint ){
      cmpp__err(pp, CMPP_RC_MISUSE,
                "Cannot commit: no active savepoint");
    }else{
      sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_spRelease, true);
      assert( q || !"db init would have otherwise failed");
      if( q && SQLITE_DONE==cmpp__step(pp, q, true) ){
        --pp->pimpl->flags.nSavepoint;
      }
    }
  }else{
    cmpp_sp_rollback(pp);
  }
  return ppCode;
}

int cmpp__dx_sp_commit(cmpp_dx * const dx){
  if( 0==dxppCode ){
    if( !dx->pimpl->nSavepoint ){
      cmpp_dx_err(dx, CMPP_RC_MISUSE,
                  "Cannot commit: no active savepoint");
    }else if( 0==cmpp_sp_commit(dx->pp) ){
      --dx->pimpl->nSavepoint;
    }
  }
  return dxppCode;
}

static void cmpp_dx_pimpl_reuse(cmpp_dx_pimpl *p){
#if 0
  /* no: we need most of the state to remain
     intact. */
  cmpp_dx_pimpl const tmp = *p;
  *p = cmpp_dx_pimpl_empty;
  p->buf = tmp.buf;
  p->args = tmp.args;
#endif
  cmpp_b_reuse(&p->buf.line);
  cmpp_b_reuse(&p->buf.argsRaw);
  cmpp_args_reuse(&p->args);
}

void cmpp_dx_pimpl_cleanup(cmpp_dx_pimpl *p){
  cmpp_b_clear(&p->buf.line);
  cmpp_b_clear(&p->buf.argsRaw);
  cmpp_args_cleanup(&p->args);
  *p = cmpp_dx_pimpl_empty;
}

void cmpp_dx__reset(cmpp_dx * const dx){
  dx->args = cmpp_dx_empty.args;
  cmpp_dx_pimpl_reuse(dx->pimpl);
  dx->d = 0;
  //no: dx->sourceName = 0;
}

void cmpp_dx_cleanup(cmpp_dx * const dx){
  unsigned prev = 0;
  CmppLvlList_cleanup(&dx->pimpl->dxLvl);
  while( dx->pimpl->nSavepoint && prev!=dx->pimpl->nSavepoint ){
    prev = dx->pimpl->nSavepoint;
    cmpp__dx_sp_rollback(dx);
  }
  cmpp_dx_pimpl_cleanup(dx->pimpl);
  memset(dx, 0, sizeof(*dx));
}

int cmpp__find_closing2(cmpp *pp,
                        unsigned char const **zPos,
                        unsigned char const *zEnd,
                        cmpp_size_t * pNl){
  unsigned char const * z = *zPos;
  unsigned char const opener = *z;
  unsigned char closer = 0;
  switch(opener){
    case '(':  closer = ')';  break;
    case '[':  closer = ']';  break;
    case '{':  closer = '}';  break;
    case '"':  case '\'': closer = opener; break;
    default:
      return cmpp__err(pp, CMPP_RC_MISUSE,
                       "Invalid starting char (0x%x) for %s()",
                       (int)opener, __func__);
  }
  int count = 1;
  for( ++z; z < zEnd; ++z ){
    if( closer == *z && 0==--count ){
      /* Have to check this first for the case of "" and ''. */
      break;
    }else if( opener == *z ){
      ++count;
    }else if( pNl && '\n'==*z ){
      ++*pNl;
    }
  }
  if( closer!=*z ){
    if( 0 ){
      g_warn("Closer=%dd Full range: <<%.*s>>", (int)*z,
             (zEnd - *zPos), *zPos);
    }
    //assert(!"here");
    cmpp__err(pp, CMPP_RC_SYNTAX,
              "Unbalanced %c%c: %.*s",
              opener, closer,
              (int)(z-*zPos), *zPos);
  }else{
    if( 0 ){
      g_warn("group: n=%u <<%.*s>>", (z + 1 - *zPos), (z +1 - *zPos), *zPos);
    }
    *zPos = z;
  }
  return ppCode;
}

cmpp_tt cmpp__tt_for_sqlite(int sqType){
  cmpp_tt rv;
  switch( sqType ){
    case SQLITE_INTEGER: rv = cmpp_TT_Int;    break;
    case SQLITE_NULL:    rv = cmpp_TT_Null;   break;
    default:             rv = cmpp_TT_String; break;
  }
  return rv;
}

int cmpp__define_from_row(cmpp * const pp, sqlite3_stmt * const q,
                          bool defineIfNoRow){
  if( 0==ppCode ){
    int const nCol = sqlite3_column_count(q);
    assert( sqlite3_data_count(q)>0 || defineIfNoRow);
    /* Create a #define for each column */
    bool const hasRow = sqlite3_data_count(q)>0;
    for( int i = 0; !ppCode && i < nCol; ++i ){
      char const * const zCol = sqlite3_column_name(q, i);
      if( hasRow ){
        unsigned char const * const zVal = sqlite3_column_text(q, i);
        int const nVal = sqlite3_column_bytes(q, i);
        cmpp_tt const ttype =
          cmpp__tt_for_sqlite(sqlite3_column_type(q,i));
        cmpp__define2(pp, ustr_c(zCol), -1, zVal, nVal, ttype);
      }else if(defineIfNoRow){
        cmpp__define2(pp, ustr_c(zCol), -1, ustr_c(""), 0, cmpp_TT_Null);
      }else{
        break;
      }
    }
  }
  return ppCode;
}

cmpp_d const * cmpp__d_search(cmpp *pp, const char *zName){
  cmpp_d const * d = 0;//cmpp__d_search(zName);
  if( !d ){
    CmppDList_entry const * e =
      CmppDList_search(&pp->pimpl->d.list, zName);
    if( e ) d = &e->d;
  }
  return d;
}

cmpp_d const * cmpp__d_search3(cmpp *pp, const char *zName,
                               cmpp_flag32_t what){
  cmpp_d const * d = cmpp__d_search(pp, zName);
  if( !d ){
    CmppDList_entry const * e = 0;
    if( cmpp__d_search3_F_DELAYED & what ){
      int rc = cmpp__d_delayed_load(pp, zName);
      if( 0==rc ){
        e = CmppDList_search(&pp->pimpl->d.list, zName);
      }else if( CMPP_RC_NO_DIRECTIVE!=rc ){
        assert( ppCode );
        return NULL;
      }
    }
    if( !e
        && (cmpp__d_search3_F_AUTOLOADER & what)
        && pp->pimpl->d.autoload.f
        && 0==pp->pimpl->d.autoload.f(pp, zName, pp->pimpl->d.autoload.state) ){
      e = CmppDList_search(&pp->pimpl->d.list, zName);
    }
#if CMPP_D_MODULE
    if( !e
        && !ppCode
        && (cmpp__d_search3_F_DLL & what) ){
      char * z = sqlite3_mprintf("libcmpp-d-%s", zName);
      cmpp_check_oom(pp, z);
      int rc = cmpp_module_load(pp, z, NULL);
      sqlite3_free(z);
      if( rc ){
        if( CMPP_RC_NOT_FOUND==rc ){
          cmpp__err_clear(pp);
        }
        return NULL;
      }
      e = CmppDList_search(&pp->pimpl->d.list, zName);
    }
#endif
    if( e ) d = &e->d;
  }
  return d;
}

int cmpp_dx_process(cmpp_dx * const dx){
  if( 0==dxppCode ){
    cmpp_d const * const d = cmpp_dx_d(dx);
    assert( d );
    if( !cmpp_dx_is_eliding(dx) || (d->flags & cmpp_d_F_FLOW_CONTROL) ){
      if( (cmpp_d_F_NOT_IN_SAFEMODE & d->flags)
          && (cmpp_ctor_F_SAFEMODE & dx->pp->pimpl->flags.newFlags) ){
        cmpp_dx_err(dx, CMPP_RC_ACCESS,
                    "Directive %s%s is disabled by safe mode.",
                    cmpp_dx_delim(dx), dx->d->name.z);
      }else{
        assert(d->impl.callback);
        d->impl.callback(dx);
      }
    }
  }
  return dxppCode;
}


static void cmpp_dx__setup_include_path(cmpp_dx * dx){
  /* Add the leading dir part of dx->sourceName as the
     highest-priority include path. It gets removed
     in cmpp_dx__teardown(). */
  assert( dx->sourceName );
  enum { BufSize = 512 * 4 };
  unsigned char buf[BufSize] = {0};
  unsigned char *z = &buf[0];
  cmpp_size_t n = cmpp__strlenu(dx->sourceName, -1);
  if( n > (unsigned)BufSize-1 ) return;
  memcpy(z, dx->sourceName, n);
  buf[n] = 0;
  cmpp_ssize_t i = n - 1;
  for( ; i > 0; --i ){
    if( '/'==z[i] || '\\'==z[i] ){
      z[i] = 0;
      n = i;
      break;
    }
  }
  if( n>(cmpp_size_t)i ){
    /* No path separator found. Assuming '.'. This is intended to
       replace the historical behavior of automatically adding '.'  if
       no -I flags are used. Potential TODO is getcwd() here instead
       of using '.' */
    n = 1;
    buf[0] = '.';
    buf[1] = 0;
  }
  int64_t rowid = 0;
  cmpp__include_dir_add(dx->pp, (char const*)buf,
                        dx->pp->pimpl->flags.nDxDepth,
                        &rowid);
  if( rowid ){
    //g_warn("Adding #include path #%" PRIi64 ": %s", rowid, z);
    dx->pimpl->shadow.ridInclPath = rowid;
  }
}

static int cmpp_dx__setup(cmpp *pp, cmpp_dx *dx,
                          unsigned char const * zIn,
                          cmpp_ssize_t nIn){
  if( 0==ppCode ){
    assert( dx->sourceName );
    assert( dx->pimpl );
    assert( pp==dx->pp );
    nIn = cmpp__strlenu(zIn, nIn);
    if( !nIn ) return 0;
    pp->pimpl->dx = dx;
    dx->pimpl->zBegin = zIn;
    dx->pimpl->zEnd = zIn + nIn;
    cmpp_define_shadow(pp, "__FILE__", (char const *)dx->sourceName,
                       &dx->pimpl->shadow.sidFile);
    ++dx->pp->pimpl->flags.nDxDepth;
    cmpp_dx__setup_include_path(dx);
  }
  return ppCode;
}

static void cmpp_dx__teardown(cmpp_dx *dx){
  if( dx->pimpl->shadow.ridInclPath>0 ){
    cmpp__include_dir_rm_id(dx->pp, dx->pimpl->shadow.ridInclPath);
    dx->pimpl->shadow.ridInclPath = 0;
  }
  if( dx->pimpl->shadow.sidFile ){
    cmpp_define_unshadow(dx->pp, "__FILE__",
                         dx->pimpl->shadow.sidFile);
  }
  --dx->pp->pimpl->flags.nDxDepth;
  cmpp_dx_cleanup(dx);
}

CMPP__EXPORT(int, cmpp_process_string)(
  cmpp *pp, const char * zName,
  unsigned char const * zIn,
  cmpp_ssize_t nIn
){
  if( !zName ) zName = "";
  if( 0==cmpp__db_init(pp) ){
    cmpp_dx const * const oldDx = pp->pimpl->dx;
    cmpp_dx_pimpl dxp = cmpp_dx_pimpl_empty;
    cmpp_dx dx = {
      .pp = pp,
      .sourceName = ustr_c(zName),
      .args = cmpp_dx_empty.args,
      .pimpl = &dxp
    };
    dxp.flags.nextIsCall = pp->pimpl->flags.nextIsCall;
    pp->pimpl->flags.nextIsCall = false;
    if( dxp.flags.nextIsCall ){
      assert( pp->pimpl->dx );
      dxp.pos.lineNo = pp->pimpl->dx->pimpl->pos.lineNo;
    }
    bool gotOne = false;
    (void)cmpp__stmt(pp, CmppStmt_sdefIns, true);
    (void)cmpp__stmt(pp, CmppStmt_inclPathAdd, true);
    (void)cmpp__stmt(pp, CmppStmt_inclPathRmId, true);
    (void)cmpp__stmt(pp, CmppStmt_sdefDel, true)
      /* hack: ensure that those queries are allocated now, as an
         error in processing may keep them from being created
         later. We might want to rethink the
         prepare-no-statements-on-error bits, but will have to go back
         and fix routines which currently rely on that. */;
    cmpp_dx__setup(pp, &dx, zIn, nIn);
    while(0==ppCode
          && 0==cmpp_dx_next(&dx, &gotOne)
          && gotOne){
      cmpp_dx_process(&dx);
    }
    if(0==ppCode && 0!=dx.pimpl->dxLvl.n){
      CmppLvl const * const lv = CmppLvl_get(&dx);
      cmpp_dx_err(&dx, CMPP_RC_SYNTAX,
                 "Input ended inside an unterminated nested construct "
                 "opened at [%s] line %" CMPP_SIZE_T_PFMT ".", zName,
                  lv ? lv->lineNo : (cmpp_size_t)0);
    }
    cmpp_dx__teardown(&dx);
    pp->pimpl->dx = oldDx;
  }
  if( !ppCode ){
    cmpp_outputer_flush(&pp->pimpl->out)
      /* We're going to ignore a result code just this once. */;
  }
  return ppCode;
}

int cmpp_process_file(cmpp *pp, const char * zName){
  if( 0==ppCode ){
    FileWrapper fw = FileWrapper_empty;
    if( 0==cmpp__FileWrapper_open(pp, &fw, zName, "rb")
        && 0==cmpp__FileWrapper_slurp(pp, &fw) ){
      cmpp_process_string(pp, zName, fw.zContent, fw.nContent);
    }
    FileWrapper_close(&fw);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_process_stream)(cmpp *pp, const char * zName,
                        cmpp_input_f src, void * srcState){
  if( 0==ppCode ){
    cmpp_b * const os = cmpp_b_borrow(pp);
    int const rc = os
      ? cmpp_stream(src, srcState, cmpp_output_f_b, os)
      : ppCode;
    if( 0==rc ){
      cmpp_process_string(pp, zName, os->z, os->n);
    }else{
      cmpp__err(pp, rc, "Error reading from input stream '%s'.", zName);
    }
    cmpp_b_return(pp, os);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_call_str)(
  cmpp *pp, unsigned char const * z, cmpp_ssize_t n,
  cmpp_b * dest, cmpp_flag32_t flags
){
  if( ppCode ) return ppCode;
  cmpp_args args = cmpp_args_empty;
  cmpp_b * const b = cmpp_b_borrow(pp);
  cmpp_b * const bo = cmpp_b_borrow(pp);
  cmpp_outputer oB = cmpp_outputer_b;
  if( !b || !bo ) return ppCode;
  cmpp__pi(pp);
  oB.state = bo;
  oB.name = pi->out.name;//"[call]";
  n = cmpp__strlenu(z, n);
  //g_warn("calling: <<%.*s>>", (int)n, z);
  unsigned char const * zEnd = z+n;
  cmpp_skip_snl(&z, zEnd);
  cmpp_skip_snl_trailing(z, &zEnd);
  n = (zEnd-z);
  if( !n ){
    cmpp_err_set(pp, CMPP_RC_SYNTAX,
                 "Empty [call] is not permitted.");
    goto end;
  }
  //g_warn("calling: <<%.*s>>", (int)n, z);
  cmpp__delim const * const delim = cmpp__pp_delim(pp);
  assert(delim);
  if( (cmpp_size_t)n<=delim->open.n
      || 0!=memcmp(z, delim->open.z, delim->open.n) ){
    /* If it doesn't start with the current delimiter,
       prepend one. */
    cmpp_b_reserve3(pp, b, delim->open.n + n + 2);
    cmpp_b_append4(pp, b, delim->open.z, delim->open.n);
  }
  cmpp_b_append4(pp, b, z, n);
  if( !ppCode ){
    cmpp_outputer oOld = cmpp_outputer_empty;
    pi->flags.nextIsCall = true
      /* Convey (indirectly) that the first cmpp_dx_next() call made
         via cmpp_process_string() is a call context. */;
    cmpp__outputer_swap(pp, &oB, &oOld);
    cmpp_process_string(pp, (char*)b->z, b->z, b->n);
    cmpp__outputer_swap(pp, &oOld, &oB);
    assert( !pi->flags.nextIsCall || ppCode );
    pi->flags.nextIsCall = false;
  }
  if( !ppCode ){
    unsigned char const * zz = bo->z;
    unsigned char const * zzEnd = bo->z + bo->n;
    if( cmpp_call_F_TRIM_ALL & flags ){
      cmpp_skip_snl(&zz, zzEnd);
      cmpp_skip_snl_trailing(zz, &zzEnd);
    }else if( 0==(cmpp_call_F_NO_TRIM & flags) ){
      cmpp_b_chomp(bo);
      zzEnd = bo->z + bo->n;
    }
    if( (zzEnd-zz) ){
      cmpp_b_append4(pp, dest, zz, (zzEnd-zz));
    }
  }
end:
  cmpp_b_return(pp, b);
  cmpp_b_return(pp, bo);
  cmpp_args_cleanup(&args);
  return ppCode;
}

CMPP__EXPORT(int, cmpp_errno_rc)(int errNo, int dflt){
  switch(errNo){
    /* Please expand on this as tests/use cases call for it... */
    case 0:
      return 0;
    case EINVAL:
      return CMPP_RC_MISUSE;
    case ENOMEM:
      return CMPP_RC_OOM;
    case EROFS:
    case EACCES:
    case EBUSY:
    case EPERM:
    case EDQUOT:
    case EAGAIN:
    case ETXTBSY:
      return CMPP_RC_ACCESS;
    case EISDIR:
    case ENOTDIR:
      return CMPP_RC_TYPE;
    case ENAMETOOLONG:
    case ELOOP:
    case ERANGE:
      return CMPP_RC_RANGE;
    case ENOENT:
    case ESRCH:
      return CMPP_RC_NOT_FOUND;
    case EEXIST:
    case ENOTEMPTY:
      return CMPP_RC_ALREADY_EXISTS;
    case EIO:
      return CMPP_RC_IO;
    default:
      return dflt;
  }
}

int cmpp_flush_f_FILE(void * _FILE){
  return fflush(_FILE) ? cmpp_errno_rc(errno, CMPP_RC_IO) : 0;
}

int cmpp_output_f_FILE( void * state,
                        void const * src, cmpp_size_t n ){
  return (1 == fwrite(src, n, 1, state ? (cmpp_FILE*)state : stdout))
    ? 0 : CMPP_RC_IO;
}

int cmpp_output_f_fd( void * state, void const * src, cmpp_size_t n ){
  int const fd = *((int*)state);
  ssize_t const wn = write(fd, src, n);
  return wn<0 ? cmpp_errno_rc(errno, CMPP_RC_IO) : 0;
}

int cmpp_input_f_FILE( void * state, void * dest, cmpp_size_t * n ){
  cmpp_FILE * f = state;
  cmpp_size_t const rn = *n;
  *n = (cmpp_size_t)fread(dest, 1, rn, f);
  return *n==rn ? 0 : (feof(f) ? 0 : CMPP_RC_IO);
}

int cmpp_input_f_fd( void * state, void * dest, cmpp_size_t * n ){
  int const fd = *((int*)state);
  ssize_t const rn = read(fd, dest, *n);
  if( rn<0 ){
    return cmpp_errno_rc(errno, CMPP_RC_IO);
  }else{
    *n = (cmpp_size_t)rn;
    return 0;
  }
}

void cmpp_outputer_cleanup_f_FILE(cmpp_outputer *self){
  if( self->state ){
    cmpp_fclose( self->state );
    self->name = NULL;
    self->state = NULL;
  }
}

CMPP__EXPORT(void, cmpp_outputer_cleanup_f_b)(cmpp_outputer *self){
  if( self->state ) cmpp_b_clear(self->state);
}

CMPP__EXPORT(int, cmpp_outputer_out)(cmpp_outputer *o, void const *p, cmpp_size_t n){
  return o->out ? o->out(o->state, p, n) : 0;
}

CMPP__EXPORT(int, cmpp_outputer_flush)(cmpp_outputer *o){
  return o->flush ? o->flush(o->state) : 0;
}

CMPP__EXPORT(void, cmpp_outputer_cleanup)(cmpp_outputer *o){
  if( o->cleanup ){
    o->cleanup( o );
  }
}

CMPP__EXPORT(int, cmpp_stream)( cmpp_input_f inF, void * inState,
                 cmpp_output_f outF, void * outState ){
  int rc = 0;
  enum { BufSize = 1024 * 4 };
  unsigned char buf[BufSize];
  cmpp_size_t rn = BufSize;
  while( 0==rc
         && (rn==BufSize)
         && (0==(rc=inF(inState, buf, &rn))) ){
    if(rn) rc = outF(outState, buf, rn);
  }
  return rc;
}

void cmpp__fatalv_base(char const *zFile, int line,
                  char const *zFmt, va_list va){
  cmpp_FILE * const fp = stderr;
  fflush(stdout);
  fprintf(fp, "\n%s:%d: ", zFile, line);
  if(zFmt && *zFmt){
    vfprintf(fp, zFmt, va);
    fputc('\n', fp);
  }
  fflush(fp);
  exit(1);
}

void cmpp__fatal_base(char const *zFile, int line,
                 char const *zFmt, ...){
  va_list va;
  va_start(va, zFmt);
  cmpp__fatalv_base(zFile, line, zFmt, va);
  va_end(va);
}

CMPP__EXPORT(int, cmpp_err_get)(cmpp *pp, char const **zMsg){
  if( zMsg && ppCode ) *zMsg = pp->pimpl->err.zMsg;
  return ppCode;
}

CMPP__EXPORT(int, cmpp_err_take)(cmpp *pp, char **zMsg){
  int const rc = ppCode;
  if( rc ){
    *zMsg = pp->pimpl->err.zMsg;
    pp->pimpl->err = cmpp_pimpl_empty.err;
  }
  return rc;
}

//CMPP_WASM_EXPORT
void cmpp__err_clear(cmpp *pp){
  cmpp_mfree(pp->pimpl->err.zMsg);
  pp->pimpl->err = cmpp_pimpl_empty.err;
}

CMPP__EXPORT(int, cmpp_err_has)(cmpp const * pp){
  return pp ? pp->pimpl->err.code : 0;
}

CMPP__EXPORT(void, cmpp_dx_pos_save)(cmpp_dx const * dx, cmpp_dx_pos *pos){
  *pos = dx->pimpl->pos;
}

CMPP__EXPORT(void, cmpp_dx_pos_restore)(cmpp_dx * dx, cmpp_dx_pos const * pos){
  dx->pimpl->pos = *pos;
}


//CMPP_WASM_EXPORT
void cmpp__dx_append_script_info(cmpp_dx const * dx,
                                 sqlite3_str * const sstr){
  sqlite3_str_appendf(
    sstr,
    "%s%s@ %s line %" CMPP_SIZE_T_PFMT,
    dx->d ? dx->d->name.z : "",
    dx->d ? " " : "",
    (dx->sourceName
     && 0==strcmp("-", (char const *)dx->sourceName))
    ? "<stdin>"
    : (char const *)dx->sourceName,
    dx->pimpl->dline.lineNo
  );
}

int cmpp__errv(cmpp *pp, int rc, char const *zFmt, va_list va){
  if( pp ){
    cmpp__err_clear(pp);
    ppCode = rc;
    if( 0==rc ) return rc;
    if( CMPP_RC_OOM==rc ){
    oom:
      pp->pimpl->err.zMsgC = "An allocation failed.";
      return pp->pimpl->err.code = CMPP_RC_OOM;
    }
    assert( !pp->pimpl->err.zMsg );
    if( pp->pimpl->dx || (zFmt && *zFmt) ){
      sqlite3_str * sstr = 0;
      sstr = sqlite3_str_new(pp->pimpl->db.dbh);
      if( pp->pimpl->dx ){
        cmpp__dx_append_script_info(pp->pimpl->dx, sstr);
        sqlite3_str_append(sstr, ": ", 2);
      }
      if( zFmt && *zFmt ){
        sqlite3_str_vappendf(sstr, zFmt, va);
      }else{
        sqlite3_str_appendf(sstr, "No error info provided.");
      }
      pp->pimpl->err.zMsgC =
        pp->pimpl->err.zMsg = sqlite3_str_finish(sstr);
      if( !pp->pimpl->err.zMsg ){
        goto oom;
      }
    }else{
      pp->pimpl->err.zMsgC = "No error info provided.";
    }
    rc = ppCode;
  }
  return rc;
}

//CMPP_WASM_EXPORT no - variadic
int cmpp_err_set(cmpp *pp, int rc,
                 char const *zFmt, ...){
  if( pp ){
    va_list va;
    va_start(va, zFmt);
    rc = cmpp__errv(pp, rc, zFmt, va);
    va_end(va);
  }
  return rc;
}

const cmpp_d_autoloader cmpp_d_autoloader_empty =
  cmpp_d_autoloader_empty_m;

CMPP__EXPORT(void, cmpp_d_autoloader_set)(cmpp *pp, cmpp_d_autoloader const * pNew){
  if( pp->pimpl->d.autoload.dtor ) pp->pimpl->d.autoload.dtor(pp->pimpl->d.autoload.state);
  if( pNew ) pp->pimpl->d.autoload = *pNew;
  else pp->pimpl->d.autoload = cmpp_d_autoloader_empty;
}

CMPP__EXPORT(void, cmpp_d_autoloader_take)(cmpp *pp, cmpp_d_autoloader * pOld){
  *pOld = pp->pimpl->d.autoload;
  pp->pimpl->d.autoload = cmpp_d_autoloader_empty;
}

//CMPP_WASM_EXPORT no - variadic
int cmpp_dx_err_set(cmpp_dx *dx, int rc,
                    char const *zFmt, ...){
  va_list va;
  va_start(va, zFmt);
  rc = cmpp__errv(dx->pp, rc, zFmt, va);
  va_end(va);
  return rc;
}

CMPP__EXPORT(int, cmpp_err_set1)(cmpp *pp, int rc, char const *zMsg){
  return cmpp_err_set(pp, rc, (zMsg && *zMsg) ? "%s" : 0, zMsg);
}

//no: CMPP_WASM_EXPORT
char * cmpp_path_search(cmpp *pp,
                        char const *zPath,
                        char pathSep,
                        char const *zBaseName,
                        char const *zExt){
  char * zrc = 0;
  if( !ppCode ){
    sqlite3_stmt * const q =
      cmpp__stmt(pp, CmppStmt_selPathSearch, false);
    if( q ){
      unsigned char sep[2] = {pathSep, 0};
      cmpp__bind_text(pp, q, 1, ustr_c(zBaseName));
      cmpp__bind_text(pp, q, 2, sep);
      cmpp__bind_text(pp, q, 3, ustr_c((zExt ? zExt : "")));
      cmpp__bind_text(pp, q, 4, ustr_c((zPath ? zPath: "")));
      int const dbrc = cmpp__step(pp, q, false);
      if( SQLITE_ROW==dbrc ){
        unsigned char const * s = sqlite3_column_text(q, 1);
        zrc = sqlite3_mprintf("%s", s);
        cmpp_check_oom(pp, zrc);
      }
      cmpp__stmt_reset(q);
    }
  }
  return zrc;
}

#if CMPP__OBUF
int cmpp__obuf_flush(cmpp__obuf * b){
  if( 0==b->rc && b->cursor > b->begin ){
    if( b->dest.out ){
      b->rc = b->dest.out(b->dest.state, b->begin,
                          b->cursor-b->begin);
    }
    b->cursor = b->begin;
  }
  if( 0==b->rc && b->dest.flush ){
    b->rc = b->dest.flush(b->dest.state);
  }
  return b->rc;
}

void cmpp__obuf_cleanup(cmpp__obuf * b){
  if( b ){
    cmpp__obuf_flush(b);/*ignoring result*/;
    if( b->ownsMemory ){
      cmpp_mfree(b->begin);
    }
    *b = cmpp__obuf_empty;
  }
}

int cmpp__obuf_write(cmpp__obuf * b, void const * src, cmpp_size_t n){
  assert( b );
  if( n && !b->rc && b->dest.out ){
    assert( b->end );
    assert( b->cursor );
    assert( b->cursor <= b->end );
    assert( b->end>b->begin );
    if( b->cursor + n >= b->end ){
      if( 0==cmpp_flush_f_obuf(b) ){
        if( b->cursor + n >= b->end ){
          /* Go ahead and write it all */
          b->rc = b->dest.out(b->dest.state, src, n);
        }else{
          goto copy_it;
        }
      }
    }else{
    copy_it:
      memcpy(b->cursor, src, n);
      b->cursor += n;
    }
  }
  return b->rc;
}

int cmpp_flush_f_obuf(void * b){
  return cmpp__obuf_flush(b);
}

int cmpp_output_f_obuf(void * state, void const * src, cmpp_size_t n){
  return cmpp__obuf_write(state, src, n);
}

void cmpp_outputer_cleanup_f_obuf(cmpp_outputer * o){
  cmpp__obuf_cleanup(o->state);
}
#endif /* CMPP__OBUF */

//cmpp__ListType_impl(cmpp__delim_list,cmpp__delim)
//cmpp__ListType_impl(CmppDList,CmppDList_entry*)
//cmpp__ListType_impl(CmppSohList,void*)
cmpp__ListType_impl(CmppArgList,cmpp_arg)
cmpp__ListType_impl(cmpp_b_list,cmpp_b*)
cmpp__ListType_impl(CmppLvlList,CmppLvl*)

/**
   Expects that *ndx points to the current argv entry and that it is a
   flag which expects a value. This function checks for --flag=val and
   (--flag val) forms. If a value is found then *ndx is adjusted (if
   needed) to point to the next argument after the value and *zVal is
   * pointed to the value. If no value is found then it returns false.
*/
static bool get_flag_val(int argc,
                        char const * const * argv, int * ndx,
                        char const **zVal){
  char const * zEq = strchr(argv[*ndx], '=');
  if( zEq ){
    *zVal = zEq+1;
    return 1;
  }else if(*ndx+1>=argc){
    return 0;
  }else{
    *zVal = argv[++*ndx];
    return 1;
  }
}

static
bool cmpp__arg_is_flag( char const *zFlag, char const *zArg,
                        char const **zValIfEqX );
bool cmpp__arg_is_flag( char const *zFlag, char const *zArg,
                        char const **zValIfEqX ){
  if( zValIfEqX ) *zValIfEqX = 0;
  if( 0==strcmp(zFlag, zArg) ) return true;
  char const * z = strchr(zArg,'=');
  if( z && z>zArg ){
    /* compare the part before the '=' */
    if( 0==strncmp(zFlag, zArg, z-zArg) ){
      if( !zFlag[z-zArg] ){
        if( zValIfEqX ) *zValIfEqX = z+1;
        return true;
      }
      /* Else it was a prefix match. */
    }
  }
  return false;
}

void cmpp__dump_defines(cmpp *pp, cmpp_FILE * fp, int bIndent){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defSelAll, false);
  if( q ){
    while( SQLITE_ROW==sqlite3_step(q) ){
      int const tt = sqlite3_column_int(q, 0);
      unsigned char const * zK = sqlite3_column_text(q, 1);
      unsigned char const * zV = sqlite3_column_text(q, 2);
      int const nK = sqlite3_column_bytes(q, 1);
      int const nV = sqlite3_column_bytes(q, 2);
      char const * zTt = cmpp__tt_cstr(tt, true);
      if( tt && zTt ) zTt += 3;
      else zTt = "String";
      fprintf(fp, "%s%.*s = [%s] %.*s\n", bIndent ? "\t" : "",
              nK, zK, zTt, nV, zV);
    }
    cmpp__stmt_reset(q);
  }
}

/**
   This is what was originally the main() of cmpp v1, back when it was
   a monolithic app. It still serves as the driver for main() but is
   otherwise unused.
*/
CMPP__EXPORT(int, cmpp_process_argv)(cmpp *pp, int argc,
                                     char const * const * argv){
  if( ppCode ) return ppCode;
  int nFile = 0    /* number of files/-e scripts seen */;

#define ARGVAL if( !zVal && !get_flag_val(argc, argv, &i, &zVal) ){ \
    cmpp__err(pp, CMPP_RC_MISUSE, "Missing value for flag '%s'", \
                  argv[i]);                                          \
    break;                                                           \
  }
#define M(X) cmpp__arg_is_flag(X, zArg, &zVal)
#define ISFLAG(X) else if(M(X))
#define ISFLAG2(X,Y) else if(M(X) || M(Y))
#define NOVAL if( zVal ){ \
    cmpp__err(pp,CMPP_RC_MISUSE,"Unexpected value for %s", zArg); \
    break; \
  } (void)0

#define open_output_if_needed                       \
  if( !pp->pimpl->out.out && cmpp__out_fopen(pp, "-") ) break

  cmpp__staticAssert(TT_None,0==(int)cmpp_TT_None);
  cmpp__staticAssert(Mask1,  cmpp_d_F_MASK_INTERNAL & cmpp_d_F_FLOW_CONTROL);
  cmpp__staticAssert(Mask2,  cmpp_d_F_MASK_INTERNAL & cmpp_d_F_NOT_SIMPLIFY);
  cmpp__staticAssert(Mask3,  0==(cmpp_d_F_MASK_INTERNAL & cmpp_d_F_MASK));

  for(int doIt = 0; doIt<2 && 0==ppCode; ++doIt){
    /**
       Loop through the flags twice. The first time we just validate
       and look for --help/-?. The second time we process the flags.
       This approach allows us to easily chain multiple files and
       flags:

       ./c-pp -Dfoo -o foo x.y -Ufoo -Dbar -o bar x.y

       Which, it turns out, is a surprisingly useful way to work.
    */
#define DOIT if(1==doIt)
    for(int i = 0; i < argc && 0==ppCode; ++i){
      char const * zVal = 0;
      int isNoFlag = 0;
      char const * zArg = argv[i];
      //g_stderr("i=%d zArg=%s\n", i, zArg);
      zVal = 0;
      while('-'==*zArg) ++zArg;
      if(zArg==argv[i]/*not a flag*/){
        zVal = zArg;
        goto do_infile;
      }
      //g_warn("zArg=%s", zArg);
      if( 0==strncmp(zArg,"no-",3) ){
        zArg += 3;
        isNoFlag = 1;
      }
      if( M("?") || M("help") ){
        NOVAL;
        cmpp__err(pp, CMPP_RC_HELP, "%s", argv[i]);
        break;
      }else if('D'==*zArg){
        ++zArg;
        if(!*zArg){
          cmpp__err(pp,CMPP_RC_MISUSE,"Missing key for -D");
        }else DOIT {
            cmpp_define_legacy(pp, zArg, 0);
        }
      }else if('F'==*zArg){
        ++zArg;
        if(!*zArg){
          cmpp__err(pp,CMPP_RC_MISUSE,"Missing key for -F");
        }else DOIT {
          cmpp__set_file(pp, ustr_c(zArg), -1);
        }
      }
      ISFLAG("e"){
        ARGVAL;
        DOIT {
          ++nFile;
          open_output_if_needed;
          cmpp_process_string(pp, "-e script",
                              (unsigned char const *)zVal, -1);
        }
      }else if('U'==*zArg){
        ++zArg;
        if(!*zArg){
          cmpp__err(pp,CMPP_RC_MISUSE,"Missing key for -U");
        }else DOIT {
            cmpp_undef(pp, zArg, NULL);
        }
      }else if('I'==*zArg){
        ++zArg;
        if(!*zArg){
          cmpp__err(pp,CMPP_RC_MISUSE,"Missing directory for -I");
        }else DOIT {
          cmpp_include_dir_add(pp, zArg);
        }
      }else if('L'==*zArg){
        ++zArg;
        if(!*zArg){
          cmpp__err(pp,CMPP_RC_MISUSE,"Missing directory for -L");
        }else DOIT {
          cmpp_module_dir_add(pp, zArg);
        }
      }
      ISFLAG2("o","outfile"){
        ARGVAL;
        DOIT {
          cmpp__out_fopen(pp, zVal);
        }
      }
      ISFLAG2("f","file"){
        ARGVAL;
      do_infile:
        DOIT {
          if( !pp->pimpl->mod.path.z ){
            cmpp_module_dir_add(pp, NULL);
          }
          ++nFile;
          if( 0
              && !pp->pimpl->flags.nIncludeDir
              && cmpp_include_dir_add(pp, ".") ){
            break;
          }
          open_output_if_needed;
          cmpp_process_file(pp, zVal);
        }
      }
      ISFLAG("@"){
        NOVAL;
        DOIT {
          assert( cmpp_atpol_DEFAULT_FOR_FLAG!=cmpp_atpol_OFF );
          cmpp_atpol_set(pp, isNoFlag
                         ? cmpp_atpol_OFF
                         : cmpp_atpol_DEFAULT_FOR_FLAG);
        }
      }
      ISFLAG("@policy"){
        ARGVAL;
        cmpp_atpol_from_str(pp, zVal);
      }
      ISFLAG("debug"){
        NOVAL;
        DOIT {
          pp->pimpl->flags.doDebug += isNoFlag ? -1 : 1;
        }
      }
      ISFLAG2("u","undefined-policy"){
        ARGVAL;
        cmpp_unpol_from_str(pp, zVal);
      }
      ISFLAG("sql-trace"){
        NOVAL;
        /* Needs to be set before the start of the second pass, when
           the db is inited. */
        DOIT {
          pp->pimpl->sqlTrace.expandSql = false;
        do_trace_flag:
          cmpp_outputer_cleanup(&pp->pimpl->sqlTrace.out);
          if( isNoFlag ){
            pp->pimpl->sqlTrace.out = cmpp_outputer_empty;
          }else{
            pp->pimpl->sqlTrace.out = cmpp_outputer_FILE;
            pp->pimpl->sqlTrace.out.state = stderr;
          }
        }
      }
      ISFLAG("sql-trace-x"){
        NOVAL;
        DOIT {
          pp->pimpl->sqlTrace.expandSql = true;
          goto do_trace_flag;
        }
      }
      ISFLAG("chomp-F"){
        NOVAL;
        DOIT pp->pimpl->flags.chompF = !isNoFlag;
      }
      ISFLAG2("d","delimiter"){
        ARGVAL;
        DOIT {
          cmpp_delimiter_set(pp, zVal);
        }
      }
      ISFLAG2("dd", "dump-defines"){
        DOIT {
          cmpp_FILE * const fp =
            /* tcl's exec treats output to stderr as failure.
               If we use [exec -ignorestderr] then it instead replaces
               stderr's output with its own message, invalidating
               test expectations. */
            1 ? stdout : stderr;
          fprintf(fp, "All %sdefine entries:\n",
                  cmpp__pp_zdelim(pp));
          cmpp__dump_defines(pp, fp, 1);
        }
      }
#if !defined(CMPP_OMIT_D_DB)
      ISFLAG2("db", "db-file"){
        /* Undocumented flag used for testing purposes. */
        ARGVAL;
        DOIT {
          cmpp_db_name_set(pp, zVal);
        }
      }
#endif
      ISFLAG("version"){
        NOVAL;
#if !defined(CMPP_OMIT_FILE_IO)
        fprintf(stdout, "c-pp version %s\nwith SQLite %s %s\n",
                cmpp_version(),
                sqlite3_libversion(),
                sqlite3_sourceid());
#endif
        doIt = 100;
        break;
      }
#if defined(CMPP_MAIN) && !defined(CMPP_MAIN_SAFEMODE)
      ISFLAG("safe-mode"){
        if( i>0 ){
          cmpp_err_set(pp, CMPP_RC_MISUSE,
                       "--%s, if used, must be the first argument.",
                       zArg);
          break;
        }
      }
#endif
      else{
        cmpp__err(pp,CMPP_RC_MISUSE,
                  "Unhandled flag: %s", argv[i]);
      }
    }
    DOIT {
      if(!nFile){
        /* We got no file arguments, so read from stdin. */
        if(0
           && !pp->pimpl->flags.nIncludeDir
           && cmpp_include_dir_add(pp, ".") ){
          break;
        }
        open_output_if_needed;
        cmpp_process_file(pp, "-");
      }
    }
#undef DOIT
  }
  return ppCode;
#undef ARGVAL
#undef M
#undef ISFLAG
#undef ISFLAG2
#undef NOVAL
#undef open_output_if_needed
}

void cmpp_process_argv_usage(char const *zAppName, cmpp_FILE *fOut){
#if defined(CMPP_OMIT_FILE_IO)
  (void)zAppName; (void)fOut;
#else
  fprintf(fOut, "%s version %s\nwith SQLite %s %s\n",
          zAppName ? zAppName : "c-pp",
          cmpp_version(),
          sqlite3_libversion(),
          sqlite3_sourceid());
  fprintf(fOut, "Usage: %s [flags] [infile...]\n", zAppName);
  fprintf(fOut,
          "Flags and filenames may be in any order and "
          "they are processed in that order.\n"
          "\nFlags:\n");
#define GAP "     "
#define arg(F,D) fprintf(fOut,"\n  %s\n" GAP "%s\n",F, D)
#if defined(CMPP_MAIN) && !defined(CMPP_MAIN_SAFEMODE)
  arg("--safe-mode",
      "Disables preprocessing directives which use the filesystem "
      "or invoke external processes. If used, it must be the first "
      "argument.");
#endif

  arg("-o|--outfile FILE","Send output to FILE (default=- (stdout)).\n"
      GAP "Because arguments are processed in order, this should\n"
      GAP "normally be given before -f.");
  arg("-f|--file FILE","Process FILE (default=- (stdin)).\n"
      GAP "All non-flag arguments are assumed to be the input files.");
  arg("-e SCRIPT",
      "Treat SCRIPT as a complete c-pp input and process it.\n"
      GAP "Doing anything marginally useful with this requires\n"
      GAP "using it several times, once per directive. It will not\n"
      GAP "work with " CMPP_DEFAULT_DELIM "if but is fine for "
      CMPP_DEFAULT_DELIM "expr, "
      CMPP_DEFAULT_DELIM "assert, and "
      CMPP_DEFAULT_DELIM "define.");
  arg("-DXYZ[=value]","Define XYZ to the given value (default=1).");
  arg("-UXYZ","Undefine all defines matching glob XYZ.");
  arg("-IXYZ","Add dir XYZ to the " CMPP_DEFAULT_DELIM "include path.");
  arg("-LXYZ","Add dir XYZ to the loadable module search path.");
  arg("-FXYZ=filename",
      "Define XYZ to the raw contents of the given file.\n"
      GAP "The file is not processed as by " CMPP_DEFAULT_DELIM"include.\n"
      GAP "Maybe it should be. Or maybe we need a new flag for that.");
  arg("-d|--delimiter VALUE", "Set directive delimiter to VALUE "
      "(default=" CMPP_DEFAULT_DELIM ").");
  arg("--@policy retain|elide|error|off",
      "Specifies how to handle @tokens@ (default=off).\n"
      GAP "off    = do not look for @tokens@\n"
      GAP "retain = parse @tokens@ and retain any undefined ones\n"
      GAP "elide  = parse @tokens@ and elide any undefined ones\n"
      GAP "error  = parse @tokens@ and error out for any undefined ones"
  );
  arg("-u|--undefined-policy NAME",
      "Sets the policy for how to handle references to undefined key:\n"
      GAP "null  = treat them as empty/falsy. This is the default.\n"
      GAP "error = trigger an error. This should probably be "
      "the default."
  );
  arg("-@", "Equivalent to --@policy=error.");
  arg("-no-@", "Equivalent to --@policy=off (the default).");
  arg("--sql-trace", "Send a trace of all SQL to stderr.");
  arg("--sql-trace-x",
      "Like --sql-trace but expand all bound values in the SQL.");
  arg("--no-sql-trace", "Disable SQL tracing (default).");
  arg("--chomp-F", "One trailing newline is trimmed from files "
      "read via -FXYZ=filename.");
  arg("--no-chomp-F", "Disable --chomp-F (default).");
#undef arg
#undef GAP
  fputs("\nFlags which require a value accept either "
        "--flag=value or --flag value. "
        "The exceptions are that the -D... and -F... flags "
        "require their '=' to be part of the flag (because they "
        "are parsed elsewhere).\n\n",fOut);
#endif /*CMPP_OMIT_FILE_IO*/
}

#if defined(CMPP_MAIN) /* add main() */
int main(int argc, char const * const * argv){
  int rc = 0;
  cmpp * pp = 0;
  cmpp_flag32_t newFlags = 0
#if defined(CMPP_MAIN_SAFEMODE)
    | cmpp_ctor_F_SAFEMODE
#endif
    ;
  cmpp_b bArgs = cmpp_b_empty;
  sqlite3_config(SQLITE_CONFIG_URI,1);
  {
    /* Copy argv to a string so we can #define it. This has proven
       helpful in testing, debugging, and output validation. */
    for( int i = 0; i < argc; ++i ){
      if( i ) cmpp_b_append_ch(&bArgs,' ');
      cmpp_b_append(&bArgs, argv[i], strlen(argv[i]));
    }
    if( (rc = bArgs.errCode) ) goto end;
    if( argc>1 && cmpp__arg_is_flag("--safe-mode", argv[1], NULL) ){
      newFlags |= cmpp_ctor_F_SAFEMODE;
      --argc;
      ++argv;
    }
  }
  cmpp_ctor_cfg const cfg = {
    .flags = newFlags
  };
  rc = cmpp_ctor(&pp, &cfg);
  if( rc ) goto end;
  /**
     Define CMPP_MAIN_INIT to the name of a function with the signature

     int (*)(cmpp*)

     to have it called here. The intent is that custom directives can
     be installed this way without having to edit this code.
  */
#if defined(CMPP_MAIN_INIT)
  extern int CMPP_MAIN_INIT(cmpp*);
  if( 0!=(rc = CMPP_MAIN_INIT(pp)) ){
    g_warn0("Initialization via CMPP_MAIN_INIT() failed");
    goto end;
  }
#endif
#if defined(CMPP_MAIN_AUTOLOADER)
  {
    extern int CMPP_MAIN_AUTOLOADER(cmpp*,char const *,void*);
    cmpp_d_autoloader al = cmpp_d_autoloader_empty;
    al.f = CMPP_MAIN_AUTOLOADER;
    cmpp_d_autoloader_set(pp, &al);
  }
#endif
  if( cmpp_define_v2(pp, "c-pp::argv", (char*)bArgs.z) ) goto end;
  cmpp_b_clear(&bArgs);
  rc = cmpp_process_argv(pp, argc-1, argv+1);
  switch( rc ){
    case 0: break;
    case CMPP_RC_HELP:
      rc = 0;
      cmpp_process_argv_usage(argv[0], stdout);
      break;
    default:
      break;
  }
end:
  cmpp_b_clear(&bArgs);
  if( pp ){
    char const *zErr = 0;
    rc = cmpp_err_get(pp, &zErr);
    if( rc && CMPP_RC_HELP!=rc ){
      g_warn("error %s: %s", cmpp_rc_cstr(rc), zErr);
    }
    cmpp_dtor(pp);
  }else if( rc && CMPP_RC_HELP!=rc ){
    g_warn("error #%d/%s", rc, cmpp_rc_cstr(rc));
  }
  sqlite3_shutdown();
  return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
#endif /* CMPP_MAIN */
/*
** 2022-11-12:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses the cmpp_b-related parts of libcmpp.
*/

const cmpp_b cmpp_b_empty = cmpp_b_empty_m;

CMPP__EXPORT(int, cmpp_b_append4)(cmpp * const pp,
                                  cmpp_b * const os,
                                  void const * src,
                                  cmpp_size_t n){
  if( !ppCode && cmpp_b_append(os, src, n) ){
    cmpp_check_oom(pp, 0);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_b_reserve3)(cmpp * const pp,
                                   cmpp_b * const os,
                                   cmpp_size_t n){
  if( !ppCode && cmpp_b_reserve(os, n) ){
    cmpp_check_oom(pp, 0);
  }
  return ppCode;
}


CMPP__EXPORT(void, cmpp_b_clear)(cmpp_b *s){
  if( s->z ) cmpp_mfree(s->z);
  *s = cmpp_b_empty;
}

CMPP__EXPORT(cmpp_b *, cmpp_b_reuse)(cmpp_b * const s){
  if( s->z ){
#if 1
    memset(s->z, 0, s->nAlloc)
      /* valgrind pushes for this, which is curious because
       cmpp_b_reserve[3]() memset()s new space to 0.

       Try the following without this block using one commit after
       [5f9c31d1da1d] (that'll be the commit that this comment and #if
       block were added):

       ##define foo
       ##if not defined a
       ##/if
       ##query define {select ?1 a} bind [1]

       There's a misuse complaint about a jump depending on
       uninitialized memory deep under cmpp__is_int(), in strlen(), on
       the "define" argument of the ##query.  It does not appear if
       the lines above it are removed, which indicates that it's at
       least semi-genuine. gcc v13.3.0, if it matters.
    */;
#else
    s->z[0] = 0;
#endif
    s->n = 0;
  }
  s->errCode = 0;
  return s;
}

CMPP__EXPORT(void, cmpp_b_swap)(cmpp_b * const l, cmpp_b * const r){
  if( l!=r ){
    cmpp_b const x = *l;
    *l = *r;
    *r = x;
  }
}

CMPP__EXPORT(int, cmpp_b_reserve)(cmpp_b *s, cmpp_size_t n){
  if( 0==s->errCode && s->nAlloc < n ){
    void * const m = cmpp_mrealloc(s->z, s->nAlloc + n);
    if( m ){
      memset((unsigned char *)m + s->nAlloc, 0, (n - s->nAlloc))
        /* valgrind convincingly recommends this. */;
      s->z = m;
      s->nAlloc += n;
    }else{
      s->errCode = CMPP_RC_OOM;
    }
  }
  return s->errCode;
}

CMPP__EXPORT(int, cmpp_b_append)(cmpp_b * os, void const *src,
                                 cmpp_size_t n){
  if(0==os->errCode){
    cmpp_size_t const nNeeded = os->n + n + 1;
    if( nNeeded>=os->nAlloc && cmpp_b_reserve(os, nNeeded) ){
      assert( CMPP_RC_OOM==os->errCode );
      return os->errCode;
    }
    memcpy(os->z + os->n, src, n);
    os->n += n;
    os->z[os->n] = 0;
    if( 0 ) {
      g_warn("n=%u z=[%.*s] nUsed=%d", (unsigned)n, (int)n,
             (char const*) src, (int)os->n);
    }
  }
  return os->errCode;
}

CMPP__EXPORT(int, cmpp_b_append_ch)(cmpp_b * os, char ch){
  if( 0==os->errCode
      && (os->n+1<os->nAlloc
          || 0==cmpp_b_reserve(os, os->n+2)) ){
    os->z[os->n++] = (unsigned char)ch;
    os->z[os->n] = 0;
  }
  return os->errCode;
}

CMPP__EXPORT(int, cmpp_b_append_i32)(cmpp_b * os, int32_t d){
  if( 0==os->errCode ){
    char buf[16] = {0};
    int const n = snprintf(buf, sizeof(buf), "%" PRIi32, d);
    cmpp_b_append(os, buf, (unsigned)n);
  }
  return os->errCode;
}

CMPP__EXPORT(int, cmpp_b_append_i64)(cmpp_b * os, int64_t d){
  if( 0==os->errCode ){
    char buf[32] = {0};
    int const n = snprintf(buf, sizeof(buf), "%" PRIi64, d);
    cmpp_b_append(os, buf, (unsigned)n);
  }
  return os->errCode;
}

CMPP__EXPORT(bool, cmpp_b_chomp)(cmpp_b * b){
  return cmpp_chomp(b->z, &b->n);
}

CMPP__EXPORT(void, cmpp_b_list_cleanup)(cmpp_b_list *li){
  while( li->nAlloc ){
    cmpp_b * const b = li->list[--li->nAlloc];
    if(b){
      cmpp_b_clear(b);
      cmpp_mfree(b);
    }
  }
  cmpp_mfree(li->list);
  *li = cmpp_b_list_empty;
}

CMPP__EXPORT(void, cmpp_b_list_reuse)(cmpp_b_list *li){
  while( li->n ){
    cmpp_b * const b = li->list[li->n--];
    if(b) cmpp_b_reuse(b);
  }
}

static cmpp_b * cmpp_b_list_push(cmpp_b_list *li){
  cmpp_b * p = 0;
  assert( li->list ? li->nAlloc : 0==li->nAlloc );
  if( !cmpp_b_list_reserve(NULL, li,
                           cmpp__li_reserve1_size(li, 20)) ){
    p = li->list[li->n];
    if( p ){
      cmpp_b_reuse(p);
    }else{
      p = cmpp_malloc(sizeof(*p));
      if( p ){
        li->list[li->n++] = p;
        *p = cmpp_b_empty;
      }
    }
  }
  return p;
}

/**
   bsearch()/qsort() comparison for (cmpp_b**), sorting by size,
   largest first and empty slots last.
*/
static int cmpp_b__cmp_desc(const void *p1, const void *p2){
  cmpp_b const * const eL = *(cmpp_b const **)p1;
  cmpp_b const * const eR = *(cmpp_b const **)p2;
  if( eL==eR ) return 0;
  else if( !eL ) return 1;
  else if (!eR ) return -1;
  return (int)(/*largest first*/eL->nAlloc - eR->nAlloc);
}

/**
   bsearch()/qsort() comparison for (cmpp_b**), sorting by size,
   smallest first and empty slots last.
*/
static int cmpp_b__cmp_asc(const void *p1, const void *p2){
  cmpp_b const * const eL = *(cmpp_b const **)p1;
  cmpp_b const * const eR = *(cmpp_b const **)p2;
  if( eL==eR ) return 0;
  else if( !eL ) return 1;
  else if (!eR ) return -1;
  return (int)(/*smallest first*/eR->nAlloc - eL->nAlloc);
}

/**
   Sort li's buffer list using the given policy. NULL entries always
   sort last. This is a no-op of how == cmpp_b_list_UNSORTED or
   li->n<2.
*/
static void cmpp_b_list__sort(cmpp_b_list * const li,
                              enum cmpp_b_list_e how){
  switch( li->n<2 ? cmpp_b_list_UNSORTED : how ){
    case cmpp_b_list_UNSORTED:
      break;
    case cmpp_b_list_DESC:
      qsort(li->list, li->n, sizeof(cmpp_b*), cmpp_b__cmp_desc);
      break;
    case cmpp_b_list_ASC:
      qsort(li->list, li->n, sizeof(cmpp_b*), cmpp_b__cmp_asc);
      break;
  }
}

CMPP__EXPORT(cmpp_b *, cmpp_b_borrow)(cmpp *pp){
  cmpp__pi(pp);
  cmpp_b_list * const li = &pi->recycler.buf;
  cmpp_b * b = 0;
  if( cmpp_b_list_UNSORTED==pi->recycler.bufSort ){
    pi->recycler.bufSort = cmpp_b_list_DESC;
    cmpp_b_list__sort(li, pi->recycler.bufSort);
    assert( cmpp_b_list_UNSORTED!=pi->recycler.bufSort
            || pi->recycler.buf.n<2 );
  }
  for( cmpp_size_t i = 0; i < li->n; ++i ){
    b = li->list[i];
    if( b ){
      li->list[i] = 0;
      assert( !b->n &&
              "Someone wrote to a buffer after giving it back" );
      if( i < li->n-1 ){
        pi->recycler.bufSort = cmpp_b_list_UNSORTED;
      }
      return cmpp_b_reuse(b);
    }
  }
  /**
     Allocate the list entry now and then remove the buffer from it to
     "borrow" it. We allocate now, instead of in cmpp_b_return(), so
     that that function has no OOM condition (handling it properly in
     higher-level code would be a mess).
  */
  b = cmpp_b_list_push(li);
  if( 0==cmpp_check_oom(pp, b) ) {
    assert( b==li->list[li->n-1] );
    li->list[li->n-1] = 0;
  }
  return b;
}

CMPP__EXPORT(void, cmpp_b_return)(cmpp *pp, cmpp_b *b){
  if( !b ) return;
  cmpp__pi(pp);
  cmpp_b_list * const li = &pi->recycler.buf;
  for( cmpp_size_t i = 0; i < li->n; ++i ){
    if( !li->list[i] ){
      li->list[i] = cmpp_b_reuse(b);
      pi->recycler.bufSort = cmpp_b_list_UNSORTED;
      return;
    }
  }
  assert( !"This shouldn't be possible - no slot in recycler.buf" );
  cmpp_b_clear(b);
  cmpp_mfree(b);
}

CMPP__EXPORT(int, cmpp_output_f_b)(
  void * state, void const * src, cmpp_size_t n
){
  if( state ){
    return cmpp_b_append(state, src, n);
  }
  return 0;
}

#if CMPP__OBUF
int cmpp__obuf_flush(cmpp__obuf * b);
int cmpp__obuf_write(cmpp__obuf * b, void const * src, cmpp_size_t n);
void cmpp__obuf_cleanup(cmpp__obuf * b);
int cmpp_output_f_obuf(void * state, void const * src, cmpp_size_t n);
int cmpp_flush_f_obuf(void * state);
void cmpp_outputer_cleanup_f_obuf(cmpp_outputer * o);
const cmpp__obuf cmpp__obuf_empty = cmpp__obuf_empty_m;
const cmpp_outputer cmpp_outputer_obuf = {
  .out = cmpp_output_f_obuf,
  .flush = cmpp_flush_f_obuf,
  .cleanup = cmpp_outputer_cleanup_f_obuf
};
#endif
/*
** 2025-11-07:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses the db-related pieces of libcmpp.
*/

/**
   A proxy for sqlite3_prepare() which updates pp->pimpl->err on error.
*/
static int cmpp__prepare(cmpp *pp, sqlite3_stmt **pStmt,
                         const char * zSql, ...){
  /* We need for pp->pimpl->stmt.sp* to work regardless of pending errors so
     that we can, when appropriate, create the rollback statements. */
  sqlite3_str * str = sqlite3_str_new(pp->pimpl->db.dbh);
  char * z = 0;
  int n = 0;
  va_list va;
  assert( pp->pimpl->db.dbh );
  va_start(va, zSql);
  sqlite3_str_vappendf(str, zSql, va);
  va_end(va);
  z = cmpp_str_finish(pp, str, &n);
  if( z ){
    int const rc = sqlite3_prepare_v2(pp->pimpl->db.dbh, z, n, pStmt, 0);
    cmpp__db_rc(pp, rc, z);
    sqlite3_free(z);
  }
  return ppCode;
}

sqlite3_stmt * cmpp__stmt(cmpp * pp, enum CmppStmt_e which,
                          bool prepEvenIfErr){
  if( !pp->pimpl->db.dbh && cmpp__db_init(pp) ) return NULL;
  sqlite3_stmt ** q = 0;
  char const * zSql = 0;
  switch(which){
    default:
      cmpp__fatal("Maintenance required: not a valid CmppStmt ID: %d", which);
      return NULL;
#define E(N,S) case CmppStmt_ ## N: zSql = S; q = &pp->pimpl->stmt.N; break;
    CmppStmt_map(E)
#undef E
  }
  assert( q );
  assert( zSql && *zSql );
  if( !*q && (!ppCode || prepEvenIfErr) ){
    cmpp__prepare(pp, q, "%s", zSql);
  }
  return *q;
}

void cmpp__stmt_reset(sqlite3_stmt * const q){
  if( q ){
    sqlite3_clear_bindings(q);
    sqlite3_reset(q);
  }
}

static inline int cmpp__stmt_is_sp(cmpp const * const pp,
                                   sqlite3_stmt const * const q){
  return q==pp->pimpl->stmt.spBegin
    || q==pp->pimpl->stmt.spRelease
    || q==pp->pimpl->stmt.spRollback;
}

int cmpp__step(cmpp * const pp, sqlite3_stmt * const q, bool resetIt){
  int rc = SQLITE_ERROR;
  assert( q );
  if( !ppCode || cmpp__stmt_is_sp(pp,q) ){
    rc = sqlite3_step(q);
    cmpp__db_rc(pp, rc, sqlite3_sql(q));
  }
  if( resetIt /* even if ppCode!=0 */ ) cmpp__stmt_reset(q);
  assert( 0!=rc );
  return rc;
}


/**
   Expects an SQLITE_... result code and returns an approximate match
   from cmpp_rc_e. It specifically treats SQLITE_ROW and SQLITE_DONE
   as non-errors, returning 0 for those.
*/
static int cmpp__db_errcode(sqlite3 * const db, int sqliteCode);
int cmpp__db_errcode(sqlite3 * const db, int sqliteCode){
  (void)db;
  int rc = 0;
  switch(sqliteCode & 0xff){
    case SQLITE_ROW:
    case SQLITE_DONE:
    case SQLITE_OK: rc = 0; break;
    case SQLITE_NOMEM: rc = CMPP_RC_OOM; break;
    case SQLITE_CORRUPT: rc = CMPP_RC_CORRUPT; break;
    case SQLITE_TOOBIG:
    case SQLITE_FULL:
    case SQLITE_RANGE: rc = CMPP_RC_RANGE; break;
    case SQLITE_NOTFOUND: rc = CMPP_RC_NOT_FOUND; break;
    case SQLITE_PERM:
    case SQLITE_AUTH:
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
    case SQLITE_READONLY: rc = CMPP_RC_ACCESS; break;
    case SQLITE_CANTOPEN:
    case SQLITE_IOERR: rc = CMPP_RC_IO; break;
    case SQLITE_NOLFS: rc = CMPP_RC_UNSUPPORTED; break;
    default:
      //MARKER(("sqlite3_errcode()=0x%04x\n", rc));
      rc = CMPP_RC_DB; break;
  }
  return rc;
}

int cmpp__db_rc(cmpp *pp, int dbRc, char const *zMsg){
  switch(dbRc){
    case 0:
    case SQLITE_DONE:
    case SQLITE_ROW:
      return 0;
    default:
      return cmpp_err_set(
        pp, cmpp__db_errcode(pp->pimpl->db.dbh, dbRc),
        "SQLite error #%d: %s%s%s",
        dbRc,
                       pp->pimpl->db.dbh
        ? sqlite3_errmsg(pp->pimpl->db.dbh)
        : "<no db handle>",
        zMsg ? ": " : "",
        zMsg ? zMsg : ""
      );
  }
}

/**
   The base "define" impl.  Requires q to be an INSERT for one of the
   define tables and have the (t,k,v) columns set up to bind to ?1,
   ?2, and ?3.
*/
static
int cmpp__define_impl(cmpp * const pp,
                      sqlite3_stmt * const q,
                      unsigned char const * zKey,
                      cmpp_ssize_t nKey,
                      unsigned char const *zVal,
                      cmpp_ssize_t nVal,
                      int tType,
                      bool resetStmt){
  if( 0==ppCode){
    assert( q );
    nKey = cmpp__strlenu(zKey, nKey);
    nVal = cmpp__strlenu(zVal, nVal);
    if( 0==cmpp__bind_textn(pp, q, 2, zKey, (int)nKey)
        && 0==cmpp__bind_int(pp, q, 1, tType) ){
      //g_stderr("zKey=%s\nzVal=%s\nzEq=%s\n", zKey, zVal, zEq);
      /* TODO? if tType==cmpp_TT_Blob, bind it as a blob */
      if( zVal ){
        if( nVal ){
          cmpp__bind_textn(pp, q, 3, zVal, (int)nVal);
        }else{
          /* Arguable */
          cmpp__bind_null(pp, q, 3);
        }
      }else{
        cmpp__bind_int(pp, q, 3, 1);
      }
      cmpp__step(pp, q, resetStmt);
      g_debug(pp,2,("define: %s [%s]=[%.*s]\n",
                    cmpp_tt_cstr(tType), zKey, (int)nVal, zVal));
    }
  }
  return ppCode;
}

int cmpp__define2(cmpp *pp,
                  unsigned char const * zKey,
                  cmpp_ssize_t nKey,
                  unsigned char const *zVal,
                  cmpp_ssize_t nVal,
                  cmpp_tt tType){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defIns, false);
  if( q ){
    cmpp__define_impl(pp, q, zKey, nKey, zVal, nVal, tType, true);
  }
  return ppCode;
}

/**
   The legacy variant of define() which accepts X=Y in zKey. This
   continues to exist because it's convenient for passing args from
   main().
*/
static int cmpp__define_legacy(cmpp *pp, const char * zKey, char const *zVal,
                               cmpp_tt ttype ){

  if(ppCode) return ppCode;
  CmppKvp kvp = CmppKvp_empty;
  if( CmppKvp_parse(pp, &kvp, ustr_c(zKey), -1,
                     zVal
                     ? CmppKvp_op_none
                     : CmppKvp_op_eq1) ) {
    return ppCode;
  }
  if( kvp.v.z ){
    if( zVal ){
      assert(!"cannot happen - CmppKvp_op_none will prevent it");
      return cmpp_err_set(pp, CMPP_RC_MISUSE,
                          "Cannot assign two values to [%.*s] [%.*s] [%s]",
                          kvp.k.n, kvp.k.z, kvp.v.n, kvp.v.z, zVal);
    }
  }else{
    kvp.v.z = (unsigned char const *)zVal;
    kvp.v.n = zVal ? (int)strlen(zVal) : 0;
  }
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_defIns, false);
  if( !q ) return ppCode;
  int64_t intCheck = 0;
  switch( ttype ){
    case cmpp_TT_Unknown:
      if(kvp.v.n){
        if( cmpp__is_int64(kvp.v.z, kvp.v.n, &intCheck) ){
          ttype = cmpp_TT_Int;
          if( '+'==*kvp.v.z ){
            ++kvp.v.z;
            --kvp.v.n;
          }
        }else{
          ttype = cmpp_TT_String;
        }
      }else if( kvp.v.z ){
        ttype = cmpp_TT_String;
      }else{
        ttype = cmpp_TT_Int;
        intCheck = 1 /* No value ==> value of 1. */;
      }
      break;
    case cmpp_TT_Int:
      if( !cmpp__is_int64(kvp.v.z, kvp.v.n, &intCheck) ){
        ttype = cmpp_TT_String;
      }
      break;
    default:
      break;
  }
  if( 0==cmpp__bind_textn(pp, q, 2, kvp.k.z, kvp.k.n)
      && 0==cmpp__bind_int(pp, q, 1, ttype) ){
    //g_stderr("zKey=%s\nzVal=%s\nzEq=%s\n", zKey, zVal, zEq);
    switch( ttype ){
      case cmpp_TT_Int:
        cmpp__bind_int(pp, q, 3, intCheck);
        break;
      case cmpp_TT_Null:
        cmpp__bind_null(pp, q, 3);
        break;
      default:
        cmpp__bind_textn(pp, q, 3, kvp.v.z, (int)kvp.v.n);
        break;
    }
    cmpp__step(pp, q, true);
    g_debug(pp,2,("define: [%.*s]=[%.*s]\n",
                  kvp.k.n, kvp.k.z,
                  kvp.v.n, kvp.v.z));
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_define_legacy)(cmpp *pp, const char * zKey, char const *zVal){
  return cmpp__define_legacy(pp, zKey, zVal, cmpp_TT_Unknown);
}

CMPP__EXPORT(int, cmpp_define_v2)(cmpp *pp, const char * zKey, char const *zVal){
  return cmpp__define2(pp, ustr_c(zKey), -1, ustr_c(zVal), -1,
                       cmpp_TT_String);
}

static
int cmpp__define_shadow(cmpp *pp, unsigned char const *zKey,
                        cmpp_ssize_t nKey,
                        unsigned char const *zVal,
                        cmpp_ssize_t nVal,
                        int ttype,
                        int64_t * pId){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_sdefIns, false);
  if( q ){
    if( 0==cmpp__define_impl(pp, q, zKey, nKey, zVal, nVal, ttype, false)
        && pId ){
      *pId = sqlite3_column_int64(q, 0);
      assert( *pId );
    }
    cmpp__stmt_reset(q);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_define_shadow)(cmpp *pp, char const *zKey,
                       char const *zVal, int64_t *pId){
  assert( pId );
  return cmpp__define_shadow(pp, ustr_c(zKey), -1,
                             ustr_c(zVal), -1, cmpp_TT_String, pId);
}

static
int cmpp__define_unshadow(cmpp *pp, unsigned char const *zKey,
                          cmpp_ssize_t nKey, int64_t id){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_sdefDel, false);
  if( q ){
    cmpp__bind_textn(pp, q, 1, zKey, (int)nKey);
    cmpp__bind_int(pp, q, 2, id);
    cmpp__step(pp, q, true);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_define_unshadow)(cmpp *pp, char const *zKey, int64_t id){
  return cmpp__define_unshadow(pp, ustr_c(zKey), -1, id);
}

/*
** This sqlite3_trace_v2() callback outputs tracing info using
** ((cmpp*)c)->sqlTrace.pFile.
*/
static int cmpp__db_sq3TraceV2(unsigned dx,void*c,void*p,void*x){
  switch(dx){
    case SQLITE_TRACE_STMT:{
      char const * const zSql = x;
      cmpp * const pp = c;
      cmpp__pi(pp);
      if(pi->sqlTrace.out.out){
        char * const zExp = pi->sqlTrace.expandSql
          ? sqlite3_expanded_sql((sqlite3_stmt*)p)
          : 0;
        sqlite3_str * const s = sqlite3_str_new(pi->db.dbh);
        if( pi->dx ){
          cmpp__dx_append_script_info(pi->dx, s);
          sqlite3_str_appendchar(s, 1, ':');
          sqlite3_str_appendchar(s, 1, ' ');
        }
        sqlite3_str_appendall(s, zExp ? zExp : zSql);
        sqlite3_str_appendchar(s, 1, '\n');
        int const n = sqlite3_str_length(s);
        if( n ){
          char * const z = sqlite3_str_finish(s);
          if( z ){
            cmpp__out2(pp, &pi->sqlTrace.out, z, (cmpp_size_t)n);
            sqlite3_free(z);
          }
        }
        sqlite3_free(zExp);
      }
      break;
    }
  }
  return 0;
}

#include <sys/stat.h>
/*
** sqlite3 UDF which returns true if its argument refers to an
** accessible file, else false.
*/
static void cmpp__udf_file_exists(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName;
  (void)(argc);  /* Unused parameter */
  zName = (const char*)sqlite3_value_text(argv[0]);
  if( 0!=zName ){
    struct stat sb;
    sqlite3_result_int(context, stat(zName, &sb)
                       ? 0
                       : S_ISREG(sb.st_mode));
  }
}

static void cmpp__udf_truthy(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  (void)(argc);  /* Unused parameter */
  assert(1==argc);
  int buul = 0;
  sqlite3_value * const sv = argv[0];
  switch( sqlite3_value_type(sv) ){
    case SQLITE_NULL:
      break;
    case SQLITE_FLOAT:
      buul = 0.0!=sqlite3_value_double(sv);
      break;
    case SQLITE_INTEGER:
      buul = 0!=sqlite3_value_int(sv);
      break;
    case SQLITE_TEXT:
    case SQLITE_BLOB:{
      int const n = sqlite3_value_bytes(sv);
      if( n>1 ) buul = 1;
      else if( 1==n ){
        const char *z =
          (const char*)sqlite3_value_text(sv);
        buul = z
          ? 0!=strcmp(z,"0")
          : 0;
      }
    }
  }
  sqlite3_result_int(context, buul);
}

/**
   SQLite3 UDF which compares its two arguments using memcmp()
   semantics. NULL will compare equal to NULL, but less than anything
   else.
*/
static void cmpp__udf_compare(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  (void)(argc);  /* Unused parameter */
  assert(2==argc);
  sqlite3_value * const v1 = argv[0];
  sqlite3_value * const v2 = argv[1];
  unsigned char const * const z1 = sqlite3_value_text(v1);
  unsigned char const * const z2 = sqlite3_value_text(v2);
  int const n1 = sqlite3_value_bytes(v1);
  int const n2 = sqlite3_value_bytes(v2);
  int rv;
  if( !z1 ){
    rv = z2 ? -1 : 0;
  }else if( !z2 ){
    rv = 1;
  }else{
    rv = strncmp((char const *)z1, (char const *)z2, n1>n2 ? n1 : n2);
  }
  if(0) g_stderr("udf_compare (%s,%s) = %d\n", z1, z2, rv);
  sqlite3_result_int(context, rv);
}

int cmpp__db_init(cmpp *pp){
  cmpp__pi(pp);
  if( pi->db.dbh || ppCode ) return ppCode;
  int rc;
  char * zErr = 0;
  const char * zDrops =
    "BEGIN EXCLUSIVE;"
    "DROP TABLE IF EXISTS " CMPP__DB_MAIN_NAME ".def;"
    "DROP TABLE IF EXISTS " CMPP__DB_MAIN_NAME ".incl;"
    "DROP TABLE IF EXISTS " CMPP__DB_MAIN_NAME ".inclpath;"
    "DROP TABLE IF EXISTS " CMPP__DB_MAIN_NAME ".predef;"
    "DROP TABLE IF EXISTS " CMPP__DB_MAIN_NAME ".ttype;"
    "DROP VIEW  IF EXISTS "  CMPP__DB_MAIN_NAME ".vdef;"
    "COMMIT;"
    ;
  const char * zSchema =
    "BEGIN EXCLUSIVE;"
    "CREATE TABLE " CMPP__DB_MAIN_NAME ".def("
    /* ^^^ defines */
      "t INTEGER DEFAULT NULL,"
      /*^^ type: cmpp_tt or NULL */
      "k TEXT PRIMARY KEY NOT NULL,"
      "v TEXT DEFAULT NULL"
    ") WITHOUT ROWID;"

    "CREATE TABLE " CMPP__DB_MAIN_NAME ".incl("
    /* ^^^ files currently being included */
      "file TEXT PRIMARY KEY NOT NULL,"
      "srcFile TEXT DEFAULT NULL,"
      "srcLine INTEGER DEFAULT 0"
    ") WITHOUT ROWID;"

    "CREATE TABLE " CMPP__DB_MAIN_NAME ".inclpath("
    /* ^^^ include path. We use (ORDER BY priority DESC, rowid) to
       make their priority correct. priority should only be set by the
       #include directive for its cwd entry. */
      "priority INTEGER DEFAULT 0," /* higher sorts first */
      "dir TEXT UNIQUE NOT NULL ON CONFLICT IGNORE"
    ");"

    "CREATE TABLE " CMPP__DB_MAIN_NAME ".modpath("
    /* ^^^ module path. We use ORDER BY ROWID to make their
       priority correct. */
      "dir TEXT PRIMARY KEY NOT NULL ON CONFLICT IGNORE"
    ");"

    "CREATE TABLE " CMPP__DB_MAIN_NAME ".predef("
    /* ^^^ pre-defines */
      "t INTEGER DEFAULT NULL," /* a cmpp_tt or NULL */
      "k TEXT PRIMARY KEY NOT NULL,"
      "v TEXT DEFAULT NULL"
    ") WITHOUT ROWID;"
    "INSERT INTO " CMPP__DB_MAIN_NAME ".predef (t,k,v)"
    " VALUES(NULL,'cmpp::version','" CMPP_VERSION "')"
    ";"

    /**
       sdefs - "scoped defines" or "shadow defines". The problem these
       solve is the one of supporting a __FILE__ define in cmpp input
       sources, such that it remains valid both before and after an
       #include, but has a new name in the scope of an #include. We
       can't use savepoints for that because they're a nuclear option
       affecting _all_ #defines in the #include'd file, whereas we
       normally want #defines to stick around across files.

       See cmpp_define_shadow() and cmpp_define_unshadow().
    */
    "CREATE TABLE " CMPP__DB_MAIN_NAME ".sdef("
     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "t INTEGER DEFAULT NULL," /* a cmpp_tt or NULL */
      "k TEXT NOT NULL,"
      "v TEXT DEFAULT NULL"
    ");"

    /**
       vdef is a view consolidating the various #define stores. It's
       intended to be used for all general-purpose fetching of defines
       and it orders the results such that the library's defines
       supercede all others, then scoped keys, then client-level
       defines.

       To push a new sdef we simply insert into sdef. Then vdef will
       order the newest sdef before any entry from the def table.
    */
    "CREATE VIEW " CMPP__DB_MAIN_NAME ".vdef(source,t,k,v) AS"
    " SELECT NULL,t,k,v FROM " CMPP__DB_MAIN_NAME ".predef"
    /* ------^^^^ sorts before numbers */
    " UNION ALL"
    " SELECT -rowid,t,k,v FROM " CMPP__DB_MAIN_NAME ".sdef"
    /* ^^^^ sorts newest of matching keys first */
    " UNION ALL"
    " SELECT 0,t,k,v FROM " CMPP__DB_MAIN_NAME ".def"
    " ORDER BY 1, 3"
    ";"

#if 0
    "CREATE TABLE " CMPP__DB_MAIN_NAME ".ttype("
    /* ^^^ token types */
      "t INTEGER PRIMARY KEY NOT NULL,"
      /*^^ type: cmpp_tt */
      "n TEXT NOT NULL,"
      /*^^ cmpp_TT_... name. */
      "s TEXT DEFAULT NULL"
      /* Symbolic or directive name, if any. */
    ");"
#endif

    "COMMIT;"
    "BEGIN EXCLUSIVE;"
    ;
  cmpp__err_clear(pp);
  int openFlags = SQLITE_OPEN_READWRITE;
  if( pi->db.zName ){
    openFlags |= SQLITE_OPEN_CREATE;
  }
  rc = sqlite3_open_v2(
    pi->db.zName ? pi->db.zName : ":memory:",
    &pi->db.dbh, openFlags, 0);
  if(rc){
    cmpp__db_rc(pp, rc, pi->db.zName
                ? pi->db.zName
                : ":memory:");
    sqlite3_close(pi->db.dbh);
    pi->db.dbh = 0;
    assert(ppCode);
    return rc;
  }
  sqlite3_busy_timeout(pi->db.dbh, 5000);
  sqlite3_db_config(pi->db.dbh, SQLITE_DBCONFIG_MAINDBNAME,
                    CMPP__DB_MAIN_NAME);
  rc = sqlite3_trace_v2(pi->db.dbh, SQLITE_TRACE_STMT,
                        cmpp__db_sq3TraceV2, pp);
  if( cmpp__db_rc(pp, rc, "Installing tracer failed") ){
    goto end;
  }
  //g_warn("Schema:\n%s\n",zSchema);
  struct {
    /* SQL UDFs */
    char const * const zName;
    void (*xUdf)(sqlite3_context *,int,sqlite3_value **);
    int arity;
    int flags;
  } aFunc[] = {
    {
      .zName = "cmpp_file_exists",
      .xUdf  = cmpp__udf_file_exists,
      .arity = 1,
      .flags = SQLITE_UTF8 | SQLITE_DIRECTONLY
    },
    {
      .zName = "cmpp_truthy",
      .xUdf  = cmpp__udf_truthy,
      .arity = 1,
      .flags = SQLITE_UTF8 | SQLITE_DIRECTONLY | SQLITE_DETERMINISTIC
    },
    {
      .zName = "cmpp_compare",
      .xUdf  = cmpp__udf_compare,
      .arity = 2,
      .flags = SQLITE_UTF8 | SQLITE_DIRECTONLY | SQLITE_DETERMINISTIC
    }
  };
  assert( 0==rc );
  for( unsigned int i = 0; 0==rc && i < sizeof(aFunc)/sizeof(aFunc[0]); ++i ){
    rc = sqlite3_create_function(
      pi->db.dbh, aFunc[i].zName, aFunc[i].arity,
      aFunc[i].flags, 0, aFunc[i].xUdf, 0, 0
    );
  }
  if( cmpp__db_rc(pp, rc, "UDF registration failed.") ){
    return ppCode;
  }
  if( pi->db.zName ){
    /* Drop all cmpp tables when using a persistent db so that we are
       not beholden to a given structure.  TODO: a config flag to
       toggle this. */
    rc = sqlite3_exec(pi->db.dbh, zDrops, 0, 0, &zErr);
  }
  if( !rc ){
    rc = sqlite3_exec(pi->db.dbh, zSchema, 0, 0, &zErr);
  }

  if( !rc ){
    extern int sqlite3_series_init(sqlite3 *, char **, const sqlite3_api_routines *);
    rc = sqlite3_series_init(pi->db.dbh, &zErr, NULL);
  }

  if(rc){
    if( zErr ){
      cmpp_err_set(pp, cmpp__db_errcode(pi->db.dbh, rc),
                   "SQLite error #%d initializing DB: %s", rc, zErr);
      sqlite3_free(zErr);
    }else{
      cmpp_err_set(pp, cmpp__db_errcode(pi->db.dbh, rc),
                   "SQLite error #%d initializing DB", rc);
    }
    goto end;
  }

  while(0){
    /* Insert the ttype mappings. We don't yet make use of this but
       only for lack of a use case ;). */
    sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_insTtype, false);
    if( !q ) goto end;
#define E(N,STR)                                                      \
    cmpp__bind_int(pp, q, 1, cmpp_TT_ ## N);                               \
    cmpp__bind_textn(pp, q, 2,                                        \
                     ustr_c("cmpp_TT_ " # N), sizeof("cmpp_TT_" # N)-1);        \
    if( STR ) cmpp__bind_textn(pp, q, 3, ustr_c(STR), sizeof(STR)-1); \
    else cmpp__bind_null(pp, q, 3);                                   \
    if( SQLITE_DONE!=cmpp__step(pp, q, true) ) return ppCode;
    cmpp_tt_map(E)
#undef E
    sqlite3_finalize(q);
    pi->stmt.insTtype = 0;
    break;
  }

end:
  if( !ppCode ){
    /*
    ** Keep us from getting in the situation later that delayed
    ** preparation if one of the savepoint statements fails (e.g. due
    ** to OOM or memory corruption).
    */
    cmpp__stmt(pp, CmppStmt_spBegin, false);
    cmpp__stmt(pp, CmppStmt_spRelease, false);
    cmpp__stmt(pp, CmppStmt_spRollback, false);
    cmpp__lazy_init(pp);
  }
  return ppCode;
}
/*
** 2022-11-12:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses the core cmpp_dx_f() implementations of libcmpp.
*/

static int cmpp__dx_err_just_once(cmpp_dx *dx, cmpp_arg const *arg){
  return cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "'%s' may only be used once.",
                         arg->z);
}

/* No-op cmpp_dx_f() impl. */
static void cmpp_dx_f_noop(cmpp_dx *dx){
  (void)dx;
}

/**
   cmpp_kav_each_f() impl for use by #define {k->v}.
*/
static int cmpp_kav_each_f_define__group(
  cmpp_dx *dx,
  unsigned char const *zKey, cmpp_size_t nKey,
  unsigned char const *zVal, cmpp_size_t nVal,
  void* callbackState
){
  if( (callbackState==dx)
      && cmpp_has(dx->pp, (char const*)zKey, nKey) ){
    return dxppCode;
  }
  return cmpp__define2(dx->pp, zKey, nKey, zVal, nVal, cmpp_TT_String);
}

/* #error impl. */
static void cmpp_dx_f_error(cmpp_dx *dx){
  const char *zBegin = (char const *)dx->args.z;
  unsigned n = (unsigned)dx->args.nz;
  if( n>2 && (('"' ==*zBegin || '\''==*zBegin) && zBegin[n-1]==*zBegin) ){
    ++zBegin;
    n -= 2;
  }
  if( n ){
    cmpp_dx_err_set(dx, CMPP_RC_ERROR, "%.*s", n, zBegin);
  }else{
    cmpp_dx_err_set(dx, CMPP_RC_ERROR, "(no additional info)");
  }
}

/* Impl. for #define. */
static void cmpp_dx_f_define(cmpp_dx *dx){
  cmpp_d const * const d = dx->d;
  assert(d);
  if( !dx->args.arg0 ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                "Expecting one or more arguments");
    return;
  }
  cmpp_arg const * aKey = 0;
  int argNdx = 0;
  int nChomp = 0;
  unsigned nHeredoc = 0;
  unsigned char acHeredoc[128] = {0} /* TODO: cmpp_args_clone() */;
  bool ifNotDefined = false /* true if '?' arg */;
  cmpp_arg const *aAppend = 0;
#define checkIsDefined(ARG) \
    if(ifNotDefined && (cmpp_has(dx->pp, (char const*)ARG->z, ARG->n) \
                        || dxppCode)) break

  for( cmpp_arg const * arg = dx->args.arg0;
       0==dxppCode && arg;
       arg = arg->next, ++argNdx ){
    //g_warn("arg=%s", arg->z);
    if( 0==argNdx && cmpp_arg_equals(arg, "?") ){
      /* Only set the key if it's not already defined. */
      ifNotDefined = true;
      continue;
    }
    switch( arg->ttype ){
      case cmpp_TT_ShiftL3:
        ++nChomp;
        /* fall through */
      case cmpp_TT_ShiftL:
        if( arg->next || argNdx<1 ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "Ill-placed '%s'.", arg->z);
        }else if( arg->n >= sizeof(acHeredoc)-1 ){
          cmpp_dx_err_set(dx, CMPP_RC_RANGE,
                      "Heredoc name is too large.");
        }else if( !aKey ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "Missing key before %s.",
                          cmpp__tt_cstr(cmpp_TT_ShiftL, false));
        }else{
          assert( aKey );
          nHeredoc = aKey->n;
          memcpy(acHeredoc, aKey->z, aKey->n+1/*NUL*/);
        }
        break;
      case cmpp_TT_OpEq:
        if( 1 /*seenEq || argNdx!=1 || !arg->next*/ ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "Ill-placed '%s'.", arg->z);
          break;
        }
        continue;
      case cmpp_TT_StringAt:
        if( cmpp__StringAtIsOk(dx->pp, cmpp_atpol_CURRENT) ){
          break;
        }
        /* fall through */
      case cmpp_TT_Int:
      case cmpp_TT_String:
      case cmpp_TT_Word:
        if( cmpp_arg_isflag(arg,"-chomp") ){
          ++nChomp;
          break;
        }
        if( cmpp_arg_isflag(arg,"-append")
            || cmpp_arg_isflag(arg,"-a") ){
          aAppend = arg->next;
          if( !aAppend ){
            cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                            "Expecting argument for %s",
                            arg->z);
          }
          arg = aAppend;
          break;
        }
        if( aKey ){
          /* This is the second arg - the value */
          checkIsDefined(aKey);
          cmpp_b * const os = cmpp_b_borrow(dx->pp);
          cmpp_b * const ba = aAppend ? cmpp_b_borrow(dx->pp) : 0;
          while( os ){
            if( ba ){
              cmpp__get_b(dx->pp, aKey->z, aKey->n, ba, false);
              if( dxppCode ) break;
              if( 0 ){
                g_warn("key=%s\n", aKey->z);
                g_warn("ba=%u %.*s\n", ba->n, ba->n, ba->z);
              }
            }
            if( cmpp_arg_to_b(dx, arg, os,
                              cmpp_arg_to_b_F_BRACE_CALL) ) break;
            cmpp_b * const which = (ba && ba->n) ? ba : os;
            if( which==ba && os->n ){
              if( ba->n ) cmpp_b_append4(dx->pp, ba, aAppend->z, aAppend->n);
              cmpp_b_append4(dx->pp, ba, os->z, os->n);
            }
            cmpp__define2(dx->pp, aKey->z, aKey->n, which->z, which->n,
                          arg->ttype);
            if( 0 ){
              g_warn("aKey=%u z=[%.*s]\n", aKey->n, (int)aKey->n, aKey->z);
              g_warn("nExp=%u z=[%.*s]\n", which->n, (int)which->n, which->z);
            }
            break;
          }
          cmpp_b_return(dx->pp, os);
          cmpp_b_return(dx->pp, ba);
          aKey = 0;
        }else if( cmpp_TT_Word!=arg->ttype ){
          cmpp_dx_err_set(dx, CMPP_RC_TYPE,
                      "Expecting a define-name token here.");
        }else if( arg->next ){
          aKey = arg;
        }else{
          /* No value = a value of 1. */
          checkIsDefined(arg);
          cmpp__define2(dx->pp, arg->z, arg->n,
                        ustr_c("1"), 1, cmpp_TT_Int);
        }
        break;
      case cmpp_TT_GroupSquiggly:
        assert( !acHeredoc[0] );
        if( (ifNotDefined ? argNdx>1 : argNdx>0) || arg->next ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "{...} must be the only argument.")
            /* This is for simplicity's sake. */;
        }else{
          cmpp_kav_each(dx, arg->z, arg->n,
                        cmpp_kav_each_f_define__group,
                        ifNotDefined ? dx : NULL,
                        cmpp_kav_each_F_NOT_EMPTY
                        | cmpp_kav_each_F_CALL_VAL
                        | cmpp_kav_each_F_PARENS_EXPR
                        //TODO cmpp_kav_each_F_IF_UNDEF
          );
        }
        aKey = 0;
        break;
      case cmpp_TT_GroupParen:{
        if( !aKey ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "(...) is not permitted as a key.");
          break;
        }
        checkIsDefined(aKey);
        int d = 0;
        if( 0==cmpp__arg_evalSubToInt(dx, arg, &d) ){
          char exprBuf[32] = {0};
          cmpp_size_t nVal =
            (cmpp_size_t)snprintf(&exprBuf[0],
                                  sizeof(exprBuf), "%d", d);
          assert(nVal>0);
          cmpp__define2(dx->pp, aKey->z, aKey->n,
                        ustr_c(&exprBuf[0]), nVal, cmpp_TT_Int);
        }
        break;
      }
      case cmpp_TT_GroupBrace:{
        if( !aKey ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "[...] is not permitted as a key.");
          break;
        }
        checkIsDefined(aKey);
        cmpp_b * const b = cmpp_b_borrow(dx->pp);
        if( b && 0==cmpp_call_str(dx->pp, arg->z, arg->n, b, 0) ){
          cmpp__define2(dx->pp, aKey->z, aKey->n,
                        b->z, b->n, cmpp_TT_AnyType);
        }
        cmpp_b_return(dx->pp, b);
        break;
      }
      default:
        // TODO: treat (...) as an expression
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Unhandled arg type %s: %s",
                        cmpp__tt_cstr(arg->ttype, true), arg->z);
        break;
    }
  }
  if( 0==nHeredoc && nChomp ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "-chomp can only be used with <<.");
  }
  if( 0==dxppCode && nHeredoc ){
    // Process (#define KEY <<)
    cmpp_b * const os = cmpp_b_borrow(dx->pp);
    assert( dx->d->closer );
    if( os &&
        0==cmpp_dx_consume_b(dx, os, &dx->d->closer, 1,
                             cmpp_dx_consume_F_PROCESS_OTHER_D) ){
      while( nChomp-- && cmpp_b_chomp(os) ){}
      g_debug(dx->pp,2,("define  heredoc: [%s]=[%.*s]\n",
                       acHeredoc, (int)os->n, os->z));
      if( !ifNotDefined
          || !cmpp_has(dx->pp, (char const*)acHeredoc, nHeredoc) ){
        cmpp__define2(
          dx->pp, acHeredoc, nHeredoc, os->z, os->n, cmpp_TT_String
        );
      }
    }
    cmpp_b_return(dx->pp, os);
  }
#undef checkIsDefined
  return;
}

/* Impl. for #undef */
static void cmpp_dx_f_undef(cmpp_dx *dx){
  if( !dx->args.arg0 ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                "Expecting one or more arguments");
    return;
  }
  cmpp_d const * const d = dx->d;
  for( cmpp_arg const * arg = dx->args.arg0;
       0==dxppCode && arg;
       arg = arg->next ){
    if( 0 ){
      g_stderr("  %s: %s %p n=%d %.*s\n", d->name.z,
               cmpp__tt_cstr(arg->ttype, true), arg->z,
               (int)arg->n, (int)arg->n, arg->z);
    }
    if( cmpp_TT_Word==arg->ttype ){
#if 0
      /* Too strict? */
      if( 0==cmpp__legal_key_check(dx->pp, arg->z,
                                   (cmpp_ssize_t)arg->n, false) ) {
        cmpp_undef(dx->pp, (char const *)arg->z);
      }
#else
      cmpp_undef(dx->pp, (char const *)arg->z, NULL);
#endif
    }else{
      cmpp_err_set(dx->pp, CMPP_RC_MISUSE, "Invalid arg for %s: %s",
                d->name.z, arg->z);
    }
  }
}

/* Impl. for #once. */
static void cmpp_dx_f_once(cmpp_dx *dx){
  cmpp_d const * const d = dx->d;
  assert(d);
  assert(d->closer);
  if( dx->args.arg0 ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Expecting no arguments");
    return;
  }
  cmpp_dx_pimpl * const dxp = dx->pimpl;
  cmpp_b * const b = cmpp_b_borrow(dx->pp);
  if( !b ) return;
  cmpp_b_append_ch(b, '#');
  cmpp_b_append4(dx->pp, b, d->name.z, d->name.n);
  cmpp_b_append_ch(b, ':');
  cmpp__get_b(dx->pp, ustr_c("__FILE__"), 8, b, true)
    /* Wonky return semantics. */;
  if( b->errCode
      || dxppCode
      || cmpp_b_append_ch(b, ':')
      || cmpp_b_append_i32(b, (int)dxp->pos.lineNo) ){
    goto end;
  }
  //g_debug(dx->pp,1,("#once key: %s", b->z));
  int const had = cmpp_has(dx->pp, (char const *)b->z, b->n);
  if( dxppCode ) goto end;
  else if( had ){
    CmppLvl * const lvl = CmppLvl_push(dx);
    if( lvl ){
      CmppLvl_elide(lvl, true);
      cmpp_outputer devNull = cmpp_outputer_empty;
      cmpp_dx_consume(dx, &devNull, &d->closer, 1,
                      cmpp_dx_consume_F_PROCESS_OTHER_D);
      CmppLvl_pop(dx, lvl);
    }
  }else if( !cmpp_define_v2(dx->pp, (char const*)b->z, "1") ){
    cmpp_dx_consume(dx, NULL, &d->closer, 1,
                    cmpp_dx_consume_F_PROCESS_OTHER_D);
  }
end:
  cmpp_b_return(dx->pp, b);
  return;
}


/* Impl. for #/define, /#query, /#pipe. */
CMPP__EXPORT(void, cmpp_dx_f_dangling_closer)(cmpp_dx *dx){
  cmpp_d const * const d = dx->d;
  char const * const zD = cmpp_dx_delim(dx);
  dxserr("%s%s used without its opening directive.",
         zD, d->name.z);
}

#ifndef CMPP_OMIT_D_INCLUDE
static int cmpp__including_has(cmpp *pp, unsigned const char * zName){
  int rc = 0;
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_inclHas, false);
  if( q && 0==cmpp__bind_text(pp, q, 1, zName) ){
    if(SQLITE_ROW == cmpp__step(pp, q, true)){
      rc = 1;
    }else{
      rc = 0;
    }
    g_debug(pp,2,("inclpath has [%s] = %d\n",zName, rc));
  }
  return rc;
}

/**
   Returns a resolved path of PREFIX+'/'+zKey, where PREFIX is one of
   the `#include` dirs (cmpp_include_dir_add()). If no file match is
   found, NULL is returned. Memory must eventually be passed to
   cmpp_mfree() to free it.
*/
static char * cmpp__include_search(cmpp *pp, unsigned const char * zKey,
                                   int * nVal){
  char * zName = 0;
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_inclSearch, false);
  if( nVal ) *nVal = 0;
  if( q && 0==cmpp__bind_text(pp, q, 1, zKey) ){
    int const rc = cmpp__step(pp, q, false);
    if(SQLITE_ROW==rc){
      const unsigned char * z = sqlite3_column_text(q, 0);
      int const n = sqlite3_column_bytes(q,0);
      zName = n ? sqlite3_mprintf("%.*s", n, z) : 0;
      if( n ) cmpp_check_oom(pp, zName);
      if( nVal ) *nVal = n;
    }
    cmpp__stmt_reset(q);
  }
  return zName;
}

/**
   Removes zKey from the currently-being-`#include`d list
   list.
*/
static int cmpp__include_rm(cmpp *pp, unsigned const char * zKey){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_inclDel, false);
  if( q ){
    cmpp__bind_text(pp, q, 1, ustr_c(zKey));
    cmpp__step(pp, q, true);
    g_debug(pp,2,("incl rm [%s]\n", zKey));
  }
  return ppCode;
}

#if 0
/*
** Sets pp's error state if the `#include` list contains the given
** key.
*/
static int cmpp__including_check(cmpp *pp, const char * zKey);
int cmpp__including_check(cmpp *pp, const char * zName){
  if( !ppCode ){
    if(cmpp__including_has(pp, zName)){
      cmpp__err(pp, CMPP_RC_MISUSE,
                    "Recursive include detected: %s\n", zName);
    }
  }
  return ppCode;
}
#endif


/**
   Adds the given filename to the list of being-`#include`d files,
   using the given source file name and line number of error reporting
   purposes. If recursion is later detected.
*/
static int cmpp__including_add(cmpp *pp, unsigned const char * zKey,
                               unsigned const char * zSrc, cmpp_size_t srcLine){
  sqlite3_stmt * const q = cmpp__stmt(pp, CmppStmt_inclIns, false);
  if( q ){
    cmpp__bind_text(pp, q, 1, zKey);
    cmpp__bind_text(pp, q, 2, zSrc);
    cmpp__bind_int(pp, q, 3, srcLine);
    cmpp__step(pp, q, true);
    g_debug(pp,2,("is-including-file add [%s] from [%s]:%"
                  CMPP_SIZE_T_PFMT "\n", zKey, zSrc, srcLine));
  }
  return ppCode;
}

/* Impl. for #include. */
static void cmpp_dx_f_include(cmpp_dx *dx){
  char * zResolved = 0;
  int nResolved = 0;
  cmpp_b * const ob = cmpp_b_borrow(dx->pp);
  bool raw = false;
  cmpp_args args = cmpp_args_empty;
  if( !ob || cmpp_dx_args_clone(dx, &args) ){
    goto end;
  }
  assert(args.pimpl && args.pimpl->pp==dx->pp);
  cmpp_arg const * arg = args.arg0;
  for( ; arg; arg = arg->next){
#define FLAG(X)if( cmpp_arg_isflag(arg, X) )
    FLAG("-raw"){
      raw = true;
      continue;
    }
    break;
#undef FLAG
  }
  if( !arg ){
    cmpp_dx_err_set(dx, CMPP_RC_SYNTAX,
                "Expecting at least one filename argument.");
  }
  for( ; !dxppCode && arg; arg = arg->next ){
    cmpp_flag32_t a2bf = cmpp_arg_to_b_F_BRACE_CALL;
    if( cmpp_TT_Word==arg->ttype && cmpp__arg_wordIsPathOrFlag(arg) ){
      a2bf |= cmpp_arg_to_b_F_NO_DEFINES;
    }
    if( cmpp_arg_to_b(dx, arg, cmpp_b_reuse(ob), a2bf) ){
      break;
    }
    //g_stderr("zFile=%s zResolved=%s\n", zFile, zResolved);
    if(!raw && cmpp__including_has(dx->pp, ob->z)){
      /* Note that different spellings of the same filename
      ** will elude this check, but that seems okay, as different
      ** spellings means that we're not re-running the exact same
      ** invocation. We might want some other form of multi-include
      ** protection, rather than this, however. There may well be
      ** sensible uses for recursion. */
      cmpp_dx_err_set(dx, CMPP_RC_RANGE, "Recursive include of file: %s",
                  ob->z);
      break;
    }
    cmpp_mfree(zResolved);
    nResolved = 0;
    zResolved = cmpp__include_search(dx->pp, ob->z, &nResolved);
    if(!zResolved){
      if( !dxppCode ){
        cmpp_dx_err_set(dx, CMPP_RC_NOT_FOUND, "file not found: %s", ob->z);
      }
      break;
    }
    if( raw ){
      if( !dx->pp->pimpl->out.out ) break;
      FILE * const fp = cmpp_fopen(zResolved, "r");
      if( fp ){
        int const rc = cmpp_stream(cmpp_input_f_FILE, fp,
                                   dx->pp->pimpl->out.out,
                                   dx->pp->pimpl->out.state);
        if( rc ){
          cmpp_dx_err_set(dx, rc, "Unknown error streaming file %s.",
                      arg->z);
        }
        cmpp_fclose(fp);
      }else{
        cmpp_dx_err_set(dx, cmpp_errno_rc(errno, CMPP_RC_IO),
                    "Unknown error opening file %s.", arg->z);
      }
    }else{
      cmpp__including_add(dx->pp, ob->z, ustr_c(dx->sourceName),
                          dx->pimpl->dline.lineNo);
      cmpp_process_file(dx->pp, zResolved);
      cmpp__include_rm(dx->pp, ob->z);
    }
  }
end:
  cmpp_mfree(zResolved);
  cmpp_args_cleanup(&args);
  cmpp_b_return(dx->pp, ob);
}
#endif /* #ifndef CMPP_OMIT_D_INCLUDE */

/**
   cmpp_dx_f() callback state for cmpp_dx_f_if(): pointers to the
   various directives of that family.
*/
struct CmppIfState {
  cmpp_d * dIf;
  cmpp_d * dElif;
  cmpp_d * dElse;
  cmpp_d const * dEndif;
};
typedef struct CmppIfState CmppIfState;

/* Version 2 of #if. */
static void cmpp_dx_f_if(cmpp_dx *dx){
  /* Reminder to self:

     We need to be able to recurse, even in skip mode, for #if nesting
     to work.  That's not great because it means we are evaluating
     stuff we ideally should be skipping over, but it's keeping the
     current tests working as-is. We can/do, however, avoid evaluating
     expressions and such when recursing via skip mode. If we can
     eliminate that here, by keeping track of the #if stack depth,
     then we can possibly eliminate the whole CmppLvl_F_ELIDE
     flag stuff.

     The more convoluted version 1 #if (which this replaced not hours
     ago) kept track of the skip state across a separate directive
     function for #if and #/if. That was more complex but did avoid
     having to recurse into #if in order to straighten out #elif and
     #else. Update: tried a non-recursive variant involving moving
     this function's gotTruth into the CmppLvl object() and
     managing the CmppLvl stack here, but it just didn't want to
     work for me and i was too tired to figure out why.
  */
  int gotTruth = 0 /*expr result*/;
  CmppIfState const * const cis = dx->d->impl.state;
  cmpp_d const * dClosers[] = {
    cis->dElif, cis->dElse, cis->dEndif
  };
  CmppLvl * lvl = 0;
  CmppDLine const dline = dx->pimpl->dline;
  cmpp_args args = cmpp_args_empty;
  char delim[20] = {0};
#define skipOn CmppLvl_elide((lvl), true)
#define skipOff CmppLvl_elide((lvl), false)

  assert( dx->d==cis->dIf );
  if( !dx->args.arg0 ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "Expecting an expression.");
    return;
  }
  snprintf(delim, sizeof(delim), "%s", cmpp_dx_delim(dx));
  delim[sizeof(delim)-1] = 0;
  lvl = CmppLvl_push(dx);
  if( !lvl ) goto end;
  if( cmpp_dx_is_eliding(dx) ){
    gotTruth = 1;
  }else if( cmpp__args_evalToInt(dx, &dx->pimpl->args, &gotTruth) ){
    goto end;
  }else if( !gotTruth ){
    skipOn;
  }

  cmpp_d const * dPrev = dx->d;
  cmpp_outputer devNull = cmpp_outputer_empty;
  while( !dxppCode ){
    dPrev = dx->d;
    bool const isFinal = dPrev==cis->dElse
      /* true if expecting an #/if. */;
    if( cmpp_dx_consume(dx,
                        CmppLvl_is_eliding(lvl) ? &devNull : NULL,
                        isFinal ? &cis->dEndif : dClosers,
                        isFinal ? 1 : sizeof(dClosers)/sizeof(dClosers[0]),
                        cmpp_dx_consume_F_PROCESS_OTHER_D) ){
      break;
    }
    cmpp_d const * const d2 = dx->d;
    if( !d2 ){
      dxserr("Reached end of input in an untermined %s%s opened "
             "at line %" CMPP_SIZE_T_PFMT ".",
             delim, cis->dIf->name.z, dline.lineNo);
    }
    if( d2==cis->dEndif ){
      break;
    }else if( isFinal ){
      assert(!"cannot happen - caught by consume()");
      dxserr("Expecting %s%s to close %s%s.",
             delim, cis->dEndif->name.z,
             delim, dPrev->name.z);
      break;
    }else if( gotTruth ){
      skipOn;
      continue;
    }else if( d2==cis->dElif ){
      if( 0==cmpp_dx_args_parse(dx, &args)
          && 0==cmpp__args_evalToInt(dx, &args, &gotTruth) ){
        if( gotTruth ) skipOff;
        else skipOn;
      }
      continue;
    }else{
      assert( d2==cis->dElse
              && "Else (haha!) we cannot have gotten here" );
      skipOff;
      continue;
    }
    assert(!"unreachable");
  }

#undef skipOff
#undef skipOn
end:
  cmpp_args_cleanup(&args);
  if( lvl ){
    bool const lvlIsOk = CmppLvl_get(dx)==lvl;
    CmppLvl_pop(dx, lvl);
    if( !lvlIsOk && !dxppCode ){
      assert(!"i naively believe that this is not possible");
      cmpp_dx_err_set(dx, CMPP_RC_SYNTAX,
                      "Mis-terminated %s%s opened at line "
                      "%" CMPP_SIZE_T_PFMT ".",
                      delim, cis->dIf->name.z, dline.lineNo);
    }
  }
  return;
}

/* Version 2 of #elif, #else, and #/if. */
static void cmpp_dx_f_if_dangler(cmpp_dx *dx){
  CmppIfState const * const cis = dx->d->impl.state;
  char const *zDelim = cmpp_dx_delim(dx);
  cmpp_dx_err_set(dx, CMPP_RC_SYNTAX,
              "%s%s with no matching %s%s",
              zDelim, dx->d->name.z,
              zDelim, cis->dIf->name.z);
}

static void cmpp__dump_sizeofs(cmpp_dx*dx){
  (void)dx;
#define SO(X) printf("sizeof(" # X ") = %u\n", (unsigned)sizeof(X))
  SO(cmpp);
  SO(cmpp_api_thunk);
  SO(cmpp_arg);
  SO(cmpp_args);
  SO(cmpp_args_pimpl);
  SO(cmpp_b);
  SO(cmpp_d);
  SO(cmpp_d_reg);
  SO(cmpp__delim);
  SO(cmpp__delim_list);
  SO(cmpp_dx);
  SO(cmpp_dx_pimpl);
  SO(cmpp_outputer);
  SO(cmpp_pimpl);
  SO(((cmpp_pimpl*)0)->stmt);
  SO(((cmpp_pimpl*)0)->policy);
  SO(CmppArgList);
  SO(CmppDLine);
  SO(CmppDList);
  SO(CmppDList_entry);
  SO(CmppLvl);
  SO(CmppSnippet);
  SO(PodList__atpol);
  printf("cmpp_TT__last   = %d\n",
         cmpp_TT__last);
#undef SO
}


/* Impl. for #pragma. */
static void cmpp_dx_f_pragma(cmpp_dx *dx){
  cmpp_arg const * arg = dx->args.arg0;
  if(!arg){
    cmpp_dx_err_set(dx, CMPP_RC_SYNTAX, "Expecting an argument");
    return;
  }else if(arg->next){
    cmpp_dx_err_set(dx, CMPP_RC_SYNTAX, "Too many arguments");
    return;
  }
  const char * const zArg = (char const *)arg->z;
#define M(X) 0==strcmp(zArg,X)
  if(M("defines")){
    cmpp__dump_defines(dx->pp, stderr, 1);
  }else if(M("sizeof")){
    cmpp__dump_sizeofs(dx);
  }else if(M("chomp-F")){
    dx->pp->pimpl->flags.chompF = 1;
  }else if(M("no-chomp-F")){
    dx->pp->pimpl->flags.chompF = 0;
  }else if(M("api-thunk")){
    /* Generate macros for CMPP_API_THUNK and friends from
       cmpp_api_thunk_map. */
    char const * zName = "CMPP_API_THUNK_NAME";
    char buf[256];
#define out(FMT,...) snprintf(buf, sizeof(buf), FMT,__VA_ARGS__); \
    cmpp_dx_out_raw(dx, buf, strlen(buf))
    if( 0 ){
      out("/* libcmpp API thunk. */\n"
          "static cmpp_api_thunk const * %s = 0;\n"
          "#define cmpp_api_init(PP) %s = (PP)->api\n", zName, zName);
    }
#define A(V)                                          \
    if(V<=cmpp_api_thunk_version) {                   \
      out("/* Thunk APIs which follow are available as of " \
        "version %d... */\n",V);                      \
    }
#define V(N,T,V)
#define F(N,T,P) out("#define cmpp_%s %s->%s\n", # N, zName, # N);
#define O(N,T) out("#define cmpp_%s (*%s->%s)\n", # N, zName, # N);
cmpp_api_thunk_map(A,V,F,O)
#undef V
#undef F
#undef O
#undef A
#undef out
  }else{
    cmpp_dx_err_set(dx, CMPP_RC_NOT_FOUND, "Unknown pragma: %s", zArg);
  }
#undef M
}

/* Impl. for #savepoint. */
static void cmpp_dx_f_savepoint(cmpp_dx *dx){
  if(!dx->args.arg0 || dx->args.arg0->next){
    cmpp_dx_err_set(dx, CMPP_RC_SYNTAX, "Expecting one argument");
  }else{
    const char * const zArg = (const char *)dx->args.arg0->z;
#define M(X) else if( 0==strcmp(zArg,X) )
    if( 0 ){}
    M("begin"){
      cmpp__dx_sp_begin(dx);
    }
    M("rollback"){
      cmpp__dx_sp_rollback(dx);
    }M("commit"){
      cmpp__dx_sp_commit(dx);
    }else{
      cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                  "Unknown savepoint option: %s", zArg);
    }
  }
#undef M
}

/* #stderr impl. */
static void cmpp_dx_f_stderr(cmpp_dx *dx){
  if(dx->args.z){
    g_stderr("%s:%" CMPP_SIZE_T_PFMT ": %.*s\n", dx->sourceName,
             dx->pimpl->dline.lineNo,
             (int)dx->args.nz, dx->args.z);
  }else{
    cmpp_d const * d = dx->d;
    g_stderr("%s:%" CMPP_SIZE_T_PFMT ": (no %s%s argument)\n",
             dx->sourceName, dx->pimpl->dline.lineNo,
             cmpp_dx_delim(dx), d->name.z);
  }
}

/**
   Manages both the @token@ policy and the delimiters.

   #@ ?push? policy NAME ?<<?
   #@ ?push? delimiter OPEN CLOSE ?<<?
   #@ ?push? policy NAME delimiter OPEN CLOSE ?<<?
   #@ pop policy
   #@ pop delimiter
   #@ pop policy delimiter
   #@ pop both

   Function call forms:

   [@ policy]
   [@ delimiter]
*/
static void cmpp_dx_f_at(cmpp_dx *dx){
  cmpp_arg const * arg = dx->args.arg0;
  if( !arg ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Expecting arguments.");
    return;
  }
  enum ops { op_none, op_set, op_push, op_pop, op_heredoc };
  enum popWhichE { pop_policy = 0x01, pop_delim = 0x02,
                   pop_both = pop_policy | pop_delim };
  enum ops op = op_none          /* what to do */;
  int popWhich = 0               /* what to pop */;
  bool gotPolicy = false;
  bool checkedCallForm = !cmpp_dx_is_call(dx);
  cmpp_arg const * argDelimO = 0 /* @token@ opener */;
  cmpp_arg const * argDelimC = 0 /* @token@ closer */;
  cmpp__pi(dx->pp);
  cmpp_atpol_e polNew = cmpp_atpol_get(dx->pp);
  for( ; arg; arg = arg ? arg->next : NULL ){
    //g_warn("arg=%s", arg->z);
    if( !checkedCallForm ){
      assert( cmpp_dx_is_call(dx) );
      checkedCallForm = true;
      if( cmpp_arg_equals(arg, "policy") ){
        char const * z =
          cmpp__atpol_name(dx->pp, cmpp__policy(dx->pp,at));
        if( z ){
          cmpp_dx_out_raw(dx, z, strlen(z));
        }
      }else if( cmpp_arg_equals(arg, "delimiter") ){
        char const * zO = 0;
        char const * zC = 0;
        cmpp_atdelim_get(dx->pp, &zO, &zC);
        if( zC ){
          cmpp_dx_out_raw(dx, zO, strlen(zO));
          cmpp_dx_out_raw(dx, " ", 1);
          cmpp_dx_out_raw(dx, zC, strlen(zC));
        }
        goto end;
      }else{
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "In call form, '%s' expects one of "
                        "'policy' or delimiter'.");
      }
      goto end;
    }/* checkedCallForm */
    if( !argDelimC && op_none==op ){
      /* Look for push|pop. */
      if( cmpp_arg_equals(arg, "pop") ){
        arg = arg->next;
        if( !arg ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "'pop' expects arguments of 'policy' "
                          "and/or 'delimiter' and/or 'both'.");
          goto end;
        }
        for( ; arg; arg = arg->next ){
          if( 0==(pop_policy & popWhich)
              && cmpp_arg_equals(arg, "policy") ){
            popWhich |= pop_policy;
          }else if( 0==(pop_delim & popWhich)
                    && cmpp_arg_equals(arg, "delimiter") ){
            popWhich |= pop_delim;
          }else if( 0==(pop_both & popWhich)
                    && cmpp_arg_equals(arg, "both") ){
            popWhich |= pop_both;
          }else{
            cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                            "Invalid argument to 'pop': ", arg->z);
            goto end;
          }
        }
        assert( !arg );
        op = op_pop;
        break;
      }/* pop */
      if( cmpp_arg_equals(arg, "push") ){
        op = op_push;
        continue;
      }
      if( cmpp_arg_equals(arg, "set") ){
        /* set is implied if neither of push/pop are and we get
           a policy name. */
        op = op_set;
        continue;
      }
      /* Fall through */
    }/* !argDelimC && op_none==op */
    if( !gotPolicy && cmpp_arg_equals(arg, "policy") ){
      arg = arg->next;
      if( !arg ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "'policy' requires a policy name argument.");
        goto end;
      }
      polNew = cmpp_atpol_from_str(NULL, (char const*)arg->z);
      if( cmpp_atpol_invalid==polNew ){
        cmpp_atpol_from_str(dx->pp, (char const*)arg->z)
          /* Will set the error state to something informative. */;
        goto end;
      }
      if( op_none==op ) op = op_set;
      gotPolicy = true;
      continue;
    }
    if( !argDelimC && cmpp_arg_equals(arg, "delimiter") ){
      assert( !argDelimO && !argDelimC );
      argDelimO = arg->next;
      argDelimC = argDelimO ? argDelimO->next : NULL;
      if( !argDelimC ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "'delimiter' requires two arguments.");
        goto end;
      }
      arg = argDelimC->next;
      continue;
    }
    if( op_pop!=op ){
      if( cmpp_arg_equals(arg,"<<") ){
        if( arg->next ) {
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "'%s' must be the final argument.",
                          arg->z);
          goto end;
        }
        op = op_heredoc;
        break;
      }
    }
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "Unhandled argument: %s", arg->z);
    return;
  }/*arg collection*/

  assert( !dxppCode );
  assert( cmpp_atpol_invalid!=polNew );

#define popcheck(LIST)     \
    if(dxppCode) goto end; \
    if(!LIST.n) goto bad_pop

  if( op_pop==op ){
    assert( popWhich>0 && popWhich<=3 );
    if( pop_policy & popWhich ){
      popcheck(pi->policy.at);
      cmpp_atpol_pop(dx->pp);
    }
    if( pop_delim & popWhich ){
      popcheck(pi->delim.at);
      cmpp_atdelim_pop(dx->pp);
    }
    goto end;
  }

  assert( op_set==op || op_push==op || op_heredoc==op );
  if( argDelimC ){
    /* Push or set the @token@ delimiters */
    if( 0 ){
      g_warn("%s @delims@: %s %s", (op_set==op) ? "set" : "push",
             argDelimO->z, argDelimC->z);
    }
    if( op_push==op || op_heredoc==op ){
      if( cmpp_atdelim_push(dx->pp, (char const*)argDelimO->z,
                            (char const*)argDelimC->z) ){
        goto end;
      }
      argDelimO = 0 /* Re-use argDelimC as a flag in case we need to
                       roll this back on an error below. */;
    }else{
      assert( op_set==op );
      if( cmpp_atdelim_set(dx->pp, (char const*)argDelimO->z,
                           (char const*)argDelimC->z) ){
        goto end;
      }
      argDelimO = argDelimC = 0;
    }
  }

  assert( !dxppCode );
  assert( !argDelimO );
  if( op_heredoc==op ){
    if( cmpp_atpol_push(dx->pp, polNew) ){
      if( argDelimC ){
        popcheck(pi->delim.at);
        cmpp_atdelim_pop(dx->pp);
      }
    }else{
      bool const pushedDelim = NULL!=argDelimC;
      assert( dx->d->closer );
      cmpp_dx_consume(dx, NULL, &dx->d->closer, 1,
                      cmpp_dx_consume_F_PROCESS_OTHER_D)
        /* !Invalidates argDelimO and argDelimC! */;
      popcheck(pi->policy.at);
      cmpp_atpol_pop(dx->pp);
      if( pushedDelim ) cmpp_atdelim_pop(dx->pp);
    }
  }else if( op_push==op ){
    if( cmpp_atpol_push(dx->pp, polNew) && argDelimC ){
      /* Roll back delimiter push */
      cmpp_atdelim_pop(dx->pp);
    }
  }else{
     assert( op_set==op );
     if( cmpp__policy(dx->pp,at)!=polNew ){
       cmpp_atpol_set(dx->pp, polNew);
     }
  }
end:
  return;
bad_pop:
  cmpp_dx_err_set(dx, CMPP_RC_RANGE,
                  "Cannot pop an empty stack.");
#undef popcheck
}


static void cmpp_dx_f_expr(cmpp_dx *dx){
  int rv = 0;
  assert( dx->args.z );
  if( 0 ){
    g_stderr("%s() argc=%d arg0 [%.*s]\n", __func__, dx->args.argc,
             dx->args.arg0->n, dx->args.arg0->z);
    g_stderr("%s() dx->args.z [%.*s]\n", __func__,
             (int)dx->args.nz, dx->args.z);
  }
  if( !dx->args.argc ){
    dxserr("An empty expression is not permitted.");
    return;
  }
#if 0
  for( cmpp_arg const * a = dx->args.arg0; a; a = a->next ){
    g_stderr("got type=%s n=%u z=%.*s\n",
             cmpp__tt_cstr(a->ttype, true),
             (unsigned)a->n, (int)a->n, a->z);
  }
#endif
  if( 0==cmpp__args_evalToInt(dx, &dx->pimpl->args, &rv) ){
    if( 'a'==dx->d->name.z[0] ){
      if( !rv ){
        cmpp_dx_err_set(dx, CMPP_RC_ASSERT, "Assertion failed: %s",
                   dx->pimpl->buf.argsRaw.z);
      }
    }else{
      char buf[60];
      snprintf(buf, sizeof(buf), "%d\n", rv);
      cmpp_dx_out_raw(dx, buf, strlen(buf));
    }
  }
}

static void cmpp_dx_f_undef_policy(cmpp_dx *dx){
  cmpp_unpol_e up = cmpp_unpol_invalid;
  int nSeen = 0;
  cmpp_arg const * arg = dx->args.arg0;
  enum ops { op_set, op_push, op_pop };
  enum ops op = op_set;
  if( !dx->args.argc ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Expecting one of: error, null");
    return;
  }
again:
  ++nSeen;
  if( cmpp_arg_equals(arg,"error") ) up = cmpp_unpol_ERROR;
  else if( cmpp_arg_equals(arg,"null") ) up = cmpp_unpol_NULL;
  else if( 1==nSeen ){
    if( cmpp_arg_equals(arg, "push") ){
      if( !arg->next ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Expecting argument to 'push'.");
        return;
      }
      op = op_push;
      arg = arg->next;
      goto again;
    }else if( cmpp_arg_equals(arg, "pop") ){
      if( arg->next ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Extra argument after 'pop': %s",
                        arg->next->z);
        return;
      }
      op = op_pop;
    }
  }
  if( op_pop!=op && cmpp_unpol_invalid==up ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Unhandled undefined-policy '%s'."
                    " Try one of: error, null",
                    arg->z);
  }else if( op_set==op ){
    cmpp_unpol_set(dx->pp, up);
  }else if( op_push==op ){
    cmpp_unpol_push(dx->pp, up);
  }else{
    assert( op_pop==op );
    if( !cmpp__epol(dx->pp,un).n ){
      cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                      "No %s%s push is active.",
                      cmpp_dx_delim(dx), dx->d->name.z);
    }else{
      cmpp_unpol_pop(dx->pp);
    }
  }
}

#ifndef CMPP_OMIT_D_DB
/* Impl. for #attach. */
static void cmpp_dx_f_attach(cmpp_dx *dx){
  if( 3!=dx->args.argc ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                "%s expects: STRING as NAME", dx->d->name.z);
    return;
  }
  cmpp_arg const * pNext = 0;
  cmpp_b osDbFile = cmpp_b_empty;
  cmpp_b osSchema = cmpp_b_empty;
  for( cmpp_arg const * arg = dx->args.arg0;
       0==dxppCode && arg;
       arg = pNext ){
    pNext = arg->next;
    if( !osDbFile.n ){
      if( 0==cmpp_arg_to_b(dx, arg, &osDbFile,
                           cmpp_arg_to_b_F_BRACE_CALL)
          && !osDbFile.n ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Empty db file name is not permitted. "
                        "If '%s' is intended as a value, "
                        "it should be quoted.", arg->z);
        break;
      }
      assert( pNext );
      if( !pNext || !cmpp_arg_equals(pNext, "as") ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Expecting 'as' after db file name.");
        break;
      }
      pNext = pNext->next;
    }else if( !osSchema.n ){
      if( 0==cmpp_arg_to_b(dx, arg, &osSchema,
                           cmpp_arg_to_b_F_BRACE_CALL)
          && !osSchema.n ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Empty db schema name is not permitted."
                        "If '%s' is intended as a value, "
                        "it should be quoted.",
                        arg->z);
        break;
      }
    }
  }
  if( dxppCode ) goto end;
  if( !osSchema.n ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "Missing schema name.");
    goto end;
  }
  sqlite3_stmt * const q =
    cmpp__stmt(dx->pp, CmppStmt_dbAttach, false);
  if( q ){
    cmpp__bind_textn(dx->pp, q, 1, osDbFile.z, osDbFile.n);
    cmpp__bind_textn(dx->pp, q, 2, osSchema.z, osSchema.n);
    cmpp__step(dx->pp, q, true);
  }
end:
  cmpp_b_clear(&osDbFile);
  cmpp_b_clear(&osSchema);
}

/* Impl. for #detach. */
static void cmpp_dx_f_detach(cmpp_dx *dx){
  cmpp_d const * d = dx->d;
  if( 1!=dx->args.argc ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "%s expects: NAME", d->name.z);
    return;
  }
  cmpp_arg const * const arg = dx->args.arg0;
  cmpp_b os = cmpp_b_empty;
  if( cmpp_arg_to_b(dx, arg, &os, cmpp_arg_to_b_F_BRACE_CALL) ){
    goto end;
  }
  if( !os.n ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Empty db schema name is not permitted.");
    goto end;
  }
  sqlite3_stmt * const q =
    cmpp__stmt(dx->pp, CmppStmt_dbDetach, false);
  if( q ){
    cmpp__bind_textn(dx->pp, q, 1, os.z, os.n);
    cmpp__step(dx->pp, q, true);
  }
end:
  cmpp_b_clear(&os);
}
#endif /* #ifndef CMPP_OMIT_D_DB */

static void cmpp_dx_f_delimiter(cmpp_dx *dx){
  cmpp_arg const * arg = dx->args.arg0;
  enum ops { op_none, op_set, op_push, op_pop };
  enum ops op = op_none;
  cmpp_arg const * argD = 0;
  bool doHeredoc = false;
  bool const isCall = cmpp_dx_is_call(dx);
  for( ; arg; arg = arg->next ){
    if( op_none==op ){
      /* Look for push|pop. */
      if( cmpp_arg_equals(arg, "push") ){
        op = op_push;
        continue;
      }else if( cmpp_arg_equals(arg, "pop") ){
        if( arg->next ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "'pop' expects no arguments.");
          return;
        }
        op = op_pop;
        break;
      }
      /* Fall through */
    }
    if( !argD ){
      if( op_none==op ) op = op_set;
      argD = arg;
      continue;
    }else if( !doHeredoc && cmpp_arg_equals(arg,"<<") ){
      if( isCall ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "'%s' is not legal in [call] form.", arg->z);
        return;
      }else if( arg->next ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "'%s' must be the final argument.", arg->z);
          return;
      }
      op = op_push;
      doHeredoc = true;
      continue;
    }
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "Unhandled arg: %s", arg->z);
    return;
  }
  if( op_pop==op ){
    cmpp_delimiter_pop(dx->pp);
  }else if( !argD ){
    if( isCall ){
      cmpp__delim const * const del = cmpp__dx_delim(dx);
      if( del ) cmpp_dx_out_raw(dx, del->open.z, del->open.n);
    }else{
      cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "No delimiter specified.");
    }
    return;
  }else{
    char const * const z =
      (0==strcmp("default",(char*)argD->z))
      ? NULL
      : (char const*)argD->z;
    if( op_push==op ){
      cmpp_delimiter_push(dx->pp, (char const*)argD->z);
    }else{
      assert( op_set==op );
      if( doHeredoc ) cmpp_delimiter_push(dx->pp, z);
      else cmpp_delimiter_set(dx->pp, z);
    }
  }
  if( !cmpp_dx_err_check(dx) ){
    if( isCall ){
      cmpp__delim const * const del = cmpp__dx_delim(dx);
      if( del ) cmpp_dx_out_raw(dx, del->open.z, del->open.n);
    }else if( doHeredoc ){
      assert( op_push==op );
      cmpp_dx_consume(dx, NULL, &dx->d->closer, 1,
                      cmpp_dx_consume_F_PROCESS_OTHER_D);
      cmpp_delimiter_pop(dx->pp);
    }
  }
}

#ifndef NDEBUG
/* Experimenting grounds. */
static void cmpp_dx_f_experiment(cmpp_dx *dx){
  void * st = dx->d->impl.state;
  (void)st;
  g_warn("raw args: %s", dx->pimpl->buf.argsRaw.z);
  g_warn("argc=%u", dx->args.argc);
  g_warn("isCall=%d\n", cmpp_dx_is_call(dx));
  if( 1 ){
    for( cmpp_arg const * a = dx->args.arg0; a; a = a->next ){
      g_stderr("got type=%s n=%u z=%.*s\n",
               cmpp__tt_cstr(a->ttype, true),
               (unsigned)a->n, (int)a->n, a->z);
    }
  }
  if( 0 ){
    int rv = 0;
    if( 0==cmpp__args_evalToInt(dx, &dx->pimpl->args, &rv) ){
      g_stderr("expr result: %d\n", rv);
    }
  }

  if( 0 ){
    char const * zIn = "a strspn test @# and @";
    g_stderr("strlen  : %u\n", (unsigned)strlen(zIn));
    g_stderr("strspn 1:  %u, %u\n",
             (unsigned)strspn(zIn, "#@"),
             (unsigned)strspn(zIn, "@#"));
    g_stderr("strcspn 2: %u, %u\n",
             (unsigned)strcspn(zIn, "#@"),
             (unsigned)strcspn(zIn, "@#"));
    g_stderr("strcspn 3: %u, %u\n",
             (unsigned)strcspn(zIn, "a strspn"),
             (unsigned)strcspn(zIn, "nope"));
  }

  if( 1 ){
    cmpp__dump_sizeofs(dx);
  }
}
#endif /* #ifndef NDEBUG */

#ifndef CMPP_OMIT_D_DB

/**
   Helper for #query and friends. Expects arg to be an SQL value. If
   arg->next is "bind" then this consumes the following two arguments(
   "bind" BIND_ARG), where BIND_ARG must be one of either
   cmpp_TT_GroupSquiggly or cmpp_TT_GroupBrace.

   If it returns 0 then:

   - If "bind" was found then *pBind is set to the BIND_ARG argument
     and *pNext is set to the one after that.

   - Else *pBind is set to NULL and and *pNext is set to
     arg->next.

   In either case, *pNext may be set to NULL.
*/
static
int cmpp__consume_sql_args(cmpp *pp, cmpp_arg const *arg,
                           cmpp_arg const **pBind,
                           cmpp_arg const **pNext){
  if( 0==ppCode ){
    *pBind = 0;
    cmpp_arg const *pN = arg->next;
    if( pN && cmpp_arg_equals(pN, "bind") ){
      pN = pN->next;
      if( !pN || (
            cmpp_TT_GroupSquiggly!=pN->ttype
            && cmpp_TT_GroupBrace!=pN->ttype
          ) ){
        return serr("Expecting {...} or [...] after 'bind'.");
      }
      *pBind = pN;
      *pNext = pN->next;
    } else {
      *pBind = 0;
      *pNext = pN;
    }
  }
  return ppCode;
}

/**
   cmpp_kav_each_f() impl for used by #query's `bind {...}` argument.
*/
static int cmpp_kav_each_f_query__bind(
  cmpp_dx * const dx,
  unsigned char const * const zKey, cmpp_size_t nKey,
  unsigned char const * const zVal, cmpp_size_t nVal,
  void * const callbackState
){
  /* Expecting: :bindName -> bindValue */
  if( ':'!=zKey[0] && '$'!=zKey[0] /*&& '@'!=zKey[0]*/ ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Bind keys must start with ':' or '$'.");
  }else{
    sqlite3_stmt * const q = callbackState;
    assert( q );
    int const bindNdx =
      sqlite3_bind_parameter_index(q, (char const*)zKey);
    if( bindNdx ){
      cmpp__bind_textn(dx->pp, q, bindNdx, zVal, nVal);
    }else{
      cmpp_err_set(dx->pp, CMPP_RC_RANGE, "Invalid bind name: %.*s",
                   (int)nKey, zKey);
    }
  }
  return dxppCode;
}

int cmpp__bind_group(cmpp_dx * const dx, sqlite3_stmt * const q,
                     cmpp_arg const * const aGroup){
  if( dxppCode ) return dxppCode;
  if( cmpp_TT_GroupSquiggly==aGroup->ttype ){
    return cmpp_kav_each(
      dx, aGroup->z, aGroup->n,
      cmpp_kav_each_f_query__bind, q,
      cmpp_kav_each_F_NOT_EMPTY
      | cmpp_kav_each_F_CALL_VAL
      | cmpp_kav_each_F_PARENS_EXPR
    );
  }
  if( cmpp_TT_GroupBrace!=aGroup->ttype ){
    return cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                           "Expecting {...} or [...] "
                           "for SQL binding list.");
  }
  int bindNdx = 0;
  cmpp_args args = cmpp_args_empty;
  cmpp_args_parse(dx, &args, aGroup->z, aGroup->n, 0);
  if( !args.argc && !dxppCode ){
    cmpp_err_set(dx->pp, CMPP_RC_RANGE,
                 "Empty SQL bind list is not permitted.");
    /* Keep going so we can clean up a partially-parsed args. */
  }
  for( cmpp_arg const * aVal = args.arg0;
       !dxppCode && aVal;
       aVal = aVal->next ){
    ++bindNdx;
    if( 0 ){
      g_warn("bind #%d %s <<%s>>", bindNdx,
             cmpp__tt_cstr(aVal->ttype, true), aVal->z);
    }
    cmpp__bind_arg(dx, q, bindNdx, aVal);
  }
  cmpp_args_cleanup(&args);
  return dxppCode;
}

/** #query impl */
static void cmpp_dx_f_query(cmpp_dx *dx){
  //cmpp_d const * d = cmpp_dx_d(dx);
  if( !dx->args.arg0 ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                "Expecting one or more arguments");
    return;
  }
  cmpp * const pp = dx->pp;
  sqlite3_stmt * q = 0;
  cmpp_b * const obBody = cmpp_b_borrow(dx->pp);
  cmpp_b * const sql = cmpp_b_borrow(dx->pp);
  cmpp_outputer obNull = cmpp_outputer_empty;
  //cmpp_b obBindArgs = cmpp_b_empty;
  cmpp_args args = cmpp_args_empty
    /* We need to copy the args or do some arg-type-specific work to
       copy the memory for specific cases. */;
  int nChomp = 0;
  bool spStarted = false;
  bool seenDefine = false;
  bool batchMode = false;
  cmpp_arg const * pNext = 0;
  cmpp_arg const * aBind = 0;
  cmpp_d const * const dNoRows = dx->d->impl.state;
  cmpp_d const * const dClosers[2] = {dx->d->closer, dNoRows};

  if( !obBody || !sql ) goto cleanup;

  assert( dNoRows );
  if( cmpp_dx_args_clone(dx, &args) ){
    goto cleanup;
  }
  //g_warn("args.argc=%d", args.argc);
  for( cmpp_arg const * arg = args.arg0;
       0==dxppCode && arg;
       arg = pNext ){
    //g_warn("arg=%s <<%s>>", cmpp_tt_cstr(arg->ttype), arg->z);
    pNext = arg->next;
    if( cmpp_arg_equals(arg, "define") ){
      if( seenDefine ){
        cmpp__dx_err_just_once(dx, arg);
        goto cleanup;
      }
      seenDefine = true;
      continue;
    }
    if( cmpp_arg_equals(arg, "-chomp") ){
      ++nChomp;
      continue;
    }
    if( cmpp_arg_equals(arg, "-batch") ){
      if( batchMode ){
        cmpp__dx_err_just_once(dx, arg);
        goto cleanup;
      }
      batchMode = true;
      continue;
    }
    if( !sql->n ){
      if( cmpp__consume_sql_args(pp, arg, &aBind, &pNext) ){
        goto cleanup;
      }
      if( cmpp_arg_to_b(dx, arg, sql, cmpp_arg_to_b_F_BRACE_CALL) ){
        goto cleanup;
      }
      //g_warn("SQL: <<%s>>", sql->z);
      continue;
    }
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "Unhandled arg: %s", arg->z);
    goto cleanup;
  }
  if( ppCode ) goto cleanup;
  if( seenDefine ){
    if( nChomp ){
      serr("-chomp and define may not be used together.");
      goto cleanup;
    }else if( batchMode ){
      serr("-batch and define may not be used together.");
      goto cleanup;
    }
  }
  if( !sql->n ){
    serr("Expecting an SQL-string argument.");
    goto cleanup;
  }

  if( batchMode ){
    if( aBind ){
      serr("Bindable values may not be used with -batch.");
      goto cleanup;
    }
    char *zErr = 0;
    cmpp__pi(dx->pp);
    int rc = sqlite3_exec(pi->db.dbh, (char const *)sql->z, 0, 0, &zErr);
    rc = cmpp__db_rc(dx->pp, rc, zErr);
    sqlite3_free(zErr);
    goto cleanup;
  }

  if( cmpp__db_rc(pp, sqlite3_prepare_v2(
                    pp->pimpl->db.dbh, (char const *)sql->z,
                    (int)sql->n, &q, 0), 0) ){
    goto cleanup;
  }else if( !q ){
    cmpp_err_set(pp, CMPP_RC_RANGE,
              "Empty SQL is not permitted.");
    goto cleanup;
  }
  //g_warn("SQL via stmt: <<%s>>", sqlite3_sql(q));
  int const nCol = sqlite3_column_count(q);
  if( !nCol ){
    cmpp_err_set(pp, CMPP_RC_RANGE,
              "SQL does not have any result columns.");
    goto cleanup;
  }
  if( !seenDefine ){
    if( cmpp_sp_begin(pp) ) goto cleanup;
    spStarted = true;
  }

  if( aBind && cmpp__bind_group(dx, q, aBind) ){
    goto cleanup;
  }

  bool gotARow = false;
  cmpp_dx_pos dxPosStart;
  cmpp_flag32_t const consumeFlags = cmpp_dx_consume_F_PROCESS_OTHER_D;
  cmpp_dx_pos_save(dx, &dxPosStart);
  int const nChompOrig = nChomp;
  while( 0==ppCode ){
    int const dbrc = cmpp__step(pp, q, false);
    if( SQLITE_ROW==dbrc ){
      nChomp = nChompOrig;
      gotARow = true;
      if( cmpp__define_from_row(pp, q, false) ) break;
      if( seenDefine ) break;
      cmpp_dx_pos_restore(dx, &dxPosStart);
      cmpp_b_reuse(obBody);
      /* If it weren't for -chomp, we wouldn't need to
         buffer this. */
      if( cmpp_dx_consume_b(dx, obBody, dClosers,
                            sizeof(dClosers)/sizeof(dClosers[0]),
                            consumeFlags) ){
        goto cleanup;
      }
      assert( dx->d == dClosers[0] || dx->d == dClosers[1] );
      while( nChomp-- && cmpp_b_chomp(obBody) ){}
      if( obBody->n && cmpp_dx_out_raw(dx, obBody->z, obBody->n) ) break;
      if( dx->d == dNoRows ){
        if( cmpp_dx_consume(dx, &obNull, dClosers, 1/*one!*/,
                            consumeFlags) ){
          goto cleanup;
        }
        assert( dx->d == dClosers[0] );
        /* TODO? chomp? */
      }
      continue;
    }
    if( 0==ppCode && seenDefine ){
      /* If we got here, there was no result row. */
      cmpp__define_from_row(pp, q, true);
    }
    break;
  }/*result row loop*/
  cmpp__stmt_reset(q);
  if( ppCode ) goto cleanup;

  while( !seenDefine && !gotARow ){
    /* No result rows. Skip past the body, emitting the #query:no-rows
       content, if any. We disable @token processing for that first
       step because (A) the output is not going anywhere, so no need
       to expand it (noting that expanding may have side effects via
       @[call...]@) and (B) the @tokens@ referring to this query's
       results will not have been set because there was no row to set
       them from, so @expanding@ them would fail. */
    cmpp_atpol_e const atpol = cmpp_atpol_get(dx->pp);
    if( cmpp_atpol_set(dx->pp, cmpp_atpol_OFF) ) break;
    cmpp_dx_consume(dx, &obNull, dClosers,
                    sizeof(dClosers)/sizeof(dClosers[0]),
                    consumeFlags);
    cmpp_atpol_set(dx->pp, atpol);
    if( dxppCode ) break;
    assert( dx->d == dClosers[0] || dx->d == dClosers[1] );
    if( dx->d == dNoRows ){
      if( cmpp_dx_consume(dx, 0, dClosers, 1/*one!*/,
                          consumeFlags) ){
        break;
      }
      assert( dx->d == dClosers[0] );
      /* TODO? chomp? */
    }
    break;
  }

cleanup:
  cmpp_args_cleanup(&args);
  cmpp_b_return(dx->pp, obBody);
  cmpp_b_return(dx->pp, sql);
  sqlite3_finalize(q);
  if( spStarted ) cmpp_sp_rollback(pp);
}
#endif /* #ifndef CMPP_OMIT_D_DB */

#ifndef CMPP_OMIT_D_PIPE
/** #pipe impl. */
static void cmpp_dx_f_pipe(cmpp_dx *dx){
  //cmpp_d const * d = cmpp_dx_d(dx);
  unsigned char const * zArgs = dx->args.z;
  assert( dx->args.arg0->n == dx->args.nz );
  unsigned char const * const zArgsEnd = zArgs + dx->args.nz;
  if( zArgs==zArgsEnd ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
               "Expecting a command and arguments to pipe.");
    return;
  }
  cmpp_FILE * fpToChild = 0;
  int nChompIn = 0, nChompOut = 0;
  cmpp_b * const chout = cmpp_b_borrow(dx->pp);
  cmpp_b * const cmd = cmpp_b_borrow(dx->pp);
  cmpp_b * const body = cmpp_b_borrow(dx->pp);
  cmpp_b * const bArg = cmpp_b_borrow(dx->pp)
    /* arg parsing and the initial command name part of the
       external command. */;
  cmpp_args cmdArgs = cmpp_args_empty;
  /* TODOs and FIXMEs:

     We need flags to optionally @token@-parse before and/or after
     filtering.
  */
  bool seenDD = false /* true if seen "--" or [...] */;
  bool doCapture = true /* true if we need a closing /pipe */;
  bool argsAsGroup = false /* true if args is [...] */;
  bool dumpDebug = false;
  cmpp_flag32_t popenFlags = 0;
  cmpp_popen_t po = cmpp_popen_t_empty;
  if( cmpp_b_reserve3(dx->pp, cmd, zArgsEnd-zArgs + 1)
      || cmpp_b_reserve3(dx->pp, bArg, cmd->nAlloc) ){
    goto cleanup;
  }

  unsigned char * zOut = bArg->z;
  unsigned char const * const zOutEnd = bArg->z + bArg->nAlloc - 1;
  while( 0==dxppCode ){
    cmpp_arg arg = cmpp_arg_empty;
    zOut = bArg->z;
    if( cmpp_arg_parse(dx, &arg, &zArgs, zArgsEnd,
                       &zOut, zOutEnd) ){
      goto cleanup;
    }
    if( cmpp_arg_equals(&arg, "--") ){
      zOut = bArg->z;
      if( cmpp_arg_parse(dx, &arg, &zArgs, zArgsEnd,
                         &zOut, zOutEnd) ){
        goto cleanup;
      }
      if( !arg.n ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Expecting external command name "
                        "or [...] after --.");
        goto cleanup;
      }
    do_arg_list:
      seenDD = true;
      cmpp_flag32_t a2bFlags = cmpp_arg_to_b_F_BRACE_CALL;
      if( cmpp_TT_GroupBrace==arg.ttype ){
        argsAsGroup = true;
        a2bFlags |= cmpp_arg_to_b_F_NO_BRACE_CALL;
      }else if( cmpp__arg_wordIsPathOrFlag(&arg) ){
        /* If it looks like it is a path, do not
           expand it as a word. */
        arg.ttype = cmpp_TT_String;
      }
      if( cmpp_arg_to_b(dx, &arg, cmd, a2bFlags)
          || (!argsAsGroup && cmpp_b_append_ch(cmd, ' ')) ){
        goto cleanup;
      }
      //g_warn("command: [%s]=>%s", arg.z, cmd->z);
      if( cmd->n<2 ){
        cmpp_dx_err_set(dx, CMPP_RC_RANGE,
                        "Command name '%s' resolves to empty. "
                        "This is most commonly caused by not "
                        "quoting it but it can also mean that it "
                        "is an unknown define key.", arg.z);
        goto cleanup;
      }
      //g_warn("arg=%s", arg.z);
      //g_warn("cmd=%s", cmd->z);
      break;
    }
    if( cmpp_TT_GroupBrace==arg.ttype ){
      goto do_arg_list;
    }
#define FLAG(X)if( cmpp_arg_isflag(&arg, X) )
    FLAG("-no-input"){
      doCapture = false;
      continue;
    }
    FLAG("-chomp-output"){
      ++nChompOut;
      continue;
    }
    FLAG("-chomp"){
      ++nChompIn;
      continue;
    }
    FLAG("-exec-direct"){
      popenFlags |= cmpp_popen_F_DIRECT;
      continue;
    }
    FLAG("-path"){
      popenFlags |= cmpp_popen_F_PATH;
      continue;
    }
    FLAG("-debug"){
      dumpDebug = true;
      continue;
    }
#undef FLAG
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Unhandled argument: %s. %s%s requires -- "
                    "before its external command name.",
                    arg.z, cmpp_dx_delim(dx),
                    dx->d->name.z);
    goto cleanup;
  }

  if( !seenDD ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "%s%s requires a -- before the name of "
                    "its external app.",
                    cmpp_dx_delim(dx), dx->d->name.z);
    goto cleanup;
  }

  //g_warn("zArgs n=%u zArgs=%s", (unsigned)(zArgsEnd-zArgs), zArgs);
  /* dx->pimpl->args gets overwritten by cmpp_dx_consume(), so we have to copy
     the args. */
  if( argsAsGroup ){
    assert( cmd->z );
    if( cmpp_args_parse(dx, &cmdArgs, cmd->z, cmd->n, 0) ){
      goto cleanup;
    }
  }else{
    /* zArgs can have newlines in it. We need to strip those out
       before passing it on. We elide them entirely, as opposed to
       replacing them with a space. */
    cmpp_skip_snl(&zArgs, zArgsEnd);
    if( cmpp_b_reserve3(dx->pp, cmd, cmd->n + (zArgsEnd-zArgs) + 1) ){
      goto cleanup;
    }
    unsigned char * zo = cmd->z + cmd->n;
    unsigned char const *zi = zArgs;
#if !defined(NDEBUG)
    unsigned char const * zoEnd = cmd->z + cmd->nAlloc;
#endif
    for( ; zi<zArgsEnd; ++zi){
      if( '\n'!=*zi && '\r'!=*zi ) *zo++ = *zi;
    }
    assert( zoEnd > zo );
    *zo = 0;
    cmd->n = zo - cmd->z;
  }
  assert( !dxppCode );

  if( doCapture ){
    assert( dx->d->closer );
    if( cmpp_dx_consume_b(dx, body, &dx->d->closer, 1,
                          cmpp_dx_consume_F_PROCESS_OTHER_D) ){
      goto cleanup;
    }
    while( nChompIn-- && cmpp_b_chomp(body) ){}
    po.fpToChild = &fpToChild;
  }

  if( dumpDebug ){
    g_warn("%s%s -debug: cmd argsAsGroup=%d n=%u z=%s",
           cmpp_dx_delim(dx), dx->d->name.z,
           (int)argsAsGroup,
           (unsigned)cmd->n, cmd->z);
  }
  if( argsAsGroup ){
    cmpp_popen_args(dx, &cmdArgs, &po);
  }else{
    unsigned char const * z = cmd->z;
    //cmpp_skip_snl(&z, cmd->z + cmd->n);
    cmpp_popen(dx->pp, z, popenFlags, &po);
  }
  if( dxppCode ) goto cleanup;
  int rc = 0;
  if( doCapture ){
    /* Bug: if body is too bug (no idea how much that is), this will
       block while waiting on input from the child. This can easily
       happen with #include -raw. */
#if 0
    /* Failed attempt to work around it. */
    assert( fpToChild );
    enum { BufSize = 128 };
    unsigned char buf[BufSize];
    cmpp_size_t nLeft = body->n;
    unsigned char const * z = body->z;
    while( nLeft>0 && !dxppCode ){
      cmpp_size_t nWrite = nLeft < BufSize ? nLeft : BufSize;
      g_warn("writing %u to child...", (unsigned)nWrite);
      rc = cmpp_output_f_FILE(fpToChild, z, nWrite);
      if( rc ){
        cmpp_dx_err_set(dx, rc, "Error feeding stdin to piped process.");
        break;
      }
      z += nWrite;
      nLeft -= nWrite;
      fflush(fpToChild);
      cmpp_size_t nRead = BufSize;
      rc = cmpp_input_f_fd(&po.fdFromChild, &buf[0], &nRead);
      if( rc ) goto err_reading;
      cmpp_b_append4(dx->pp, &chout, buf, nRead);\
    }
    if( !dxppCode ){
      g_warn0("reading from child...");
      rc = cmpp_stream( cmpp_input_f_fd, &po.fdFromChild,
                        cmpp_output_f_b, chout );
      if( rc ) goto err_reading;
    }
    g_warn0("I/O done");
#else
    //g_warn("writing %u bytes to child...", (unsigned)body->n);
    rc = cmpp_output_f_FILE(fpToChild, body->z, body->n);
    if( rc ){
      cmpp_dx_err_set(dx, rc, "Error feeding stdin to piped process.");
      goto cleanup;
    }
    //g_warn("wrote %u bytes to child.", (unsigned)body->n);
    fclose(fpToChild);
    fpToChild = 0;
    if( dxppCode ) goto cleanup;
    goto stream_chout;
#endif
  }else{
  stream_chout:
    //g_warn0("waiting on child...");
    rc = cmpp_stream(cmpp_input_f_fd, &po.fdFromChild,
                     cmpp_output_f_b, chout);
    //g_warn0("I/O done");
    if( rc ){
      //err_reading:
      cmpp_dx_err_set(dx, rc, "Error reading stdout from piped process.");
      goto cleanup;
    }
  }
  while( nChompOut-- && cmpp_b_chomp(chout) ){}
  //g_warn("Read in:\n%.*s", (int)chout->n, chout->z);
  cmpp_dx_out_raw(dx, chout->z, chout->n);

cleanup:
  cmpp_args_cleanup(&cmdArgs);
  cmpp_b_return(dx->pp, chout);
  cmpp_b_return(dx->pp, cmd);
  cmpp_b_return(dx->pp, body);
  cmpp_b_return(dx->pp, bArg);
  cmpp_pclose(&po);
}
#endif /* #ifndef CMPP_OMIT_D_PIPE */

/**
   #sum ...args

   Emits the sum of its arguments, treating each as an
   integer. Non-integer arguments are silently skipped.
*/
static void cmpp_dx_f_sum(cmpp_dx *dx){
  int64_t n = 0, i = 0;
  cmpp_b b = cmpp_b_empty;
  for( cmpp_arg const * arg = dx->args.arg0;
       arg && !cmpp_dx_err_check(dx); arg = arg->next ){
    if( 0==cmpp_arg_to_b(dx, arg, cmpp_b_reuse(&b),
                         cmpp_arg_to_b_F_BRACE_CALL)
        && cmpp__is_int64(b.z, b.n, &i) ){
      n += i;
    }
  }
  cmpp_b_append_i64(cmpp_b_reuse(&b), n);
  cmpp_dx_out_raw(dx, b.z, b.n);
  cmpp_b_clear(&b);
}

/**
   #arg ?flags? the-arg

   -trim-left
   -trim-right
   -trim: trim both sides

   It sends its arg to cmpp_arg_to_b() to expand it, optionally
   trims the result, and emits that value.

   This directive is not expected to be useful except, perhaps in
   testing cmpp itself. Its trim flags, in particular, aren't commonly
   useful because #arg is only useful in a function call context and
   those unconditionally trim their output.
*/
static void cmpp_dx_f_arg(cmpp_dx *dx){
  cmpp_flag32_t a2bFlags = cmpp_arg_to_b_F_BRACE_CALL;
  bool trimL = false, trimR = false;
  cmpp_arg const * arg = dx->args.arg0;
  for( ; arg && !cmpp_dx_err_check(dx); arg = arg->next ){
#define FLAG(X)if( cmpp_arg_isflag(arg, X) )
    FLAG("-raw") {
      a2bFlags = cmpp_arg_to_b_F_FORCE_STRING;
      continue;
    }
    FLAG("-trim-left")  { trimL=true; continue; }
    FLAG("-trim-right") { trimR=true; continue; }
    FLAG("-trim")       { trimL=trimR=true; continue; }
#undef FLAG
    break;
  }
  if( arg ){
    cmpp_b * const b = cmpp_b_borrow(dx->pp);
    if( b && 0==cmpp_arg_to_b(dx, arg, b, a2bFlags) ){
      unsigned char const * zz = b->z;
      unsigned char const * zzEnd = b->z + b->n;
      if( trimL ) cmpp_skip_snl(&zz, zzEnd);
      if( trimR ) cmpp_skip_snl_trailing(zz, &zzEnd);
      if( zzEnd-zz ){
        cmpp_dx_out_raw(dx, zz, zzEnd-zz);
      }
    }
    cmpp_b_return(dx->pp, b);
  }else if( !cmpp_dx_err_check(dx) ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "Expecting an argument.");
  }
}

/**
   #join ?flags? ...args

   -s SEPARATOR: sets the separator for its RHS arguments. Default=space.

   -nl: append a newline (will be stripped by [call]s!). This is the default
   when !cmpp_dx_is_call(dx).

   -nonl: do not append a newline. Default when dx->isCall.
*/
static void cmpp_dx_f_join(cmpp_dx *dx){
  cmpp_b * const b = cmpp_b_borrow(dx->pp);
  cmpp_b * const bSep = cmpp_b_borrow(dx->pp);
  cmpp_flag32_t a2bFlags = cmpp_arg_to_b_F_BRACE_CALL;
  bool addNl = !cmpp_dx_is_call(dx);
  int n = 0;
  if( !b || !bSep ) goto end;
  if( !dx->args.argc ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "%s%s expects ?flags? ...args",
                    cmpp_dx_delim(dx), dx->d->name.z);
    goto end;
  }
  cmpp_b_append_ch(bSep, ' ');
  cmpp_check_oom(dx->pp, bSep->z);
  for( cmpp_arg const * arg = dx->args.arg0; arg
         && !b->errCode
         && !bSep->errCode
         && !cmpp_dx_err_check(dx);
       arg = arg->next ){
#define FLAG(X)if( cmpp_arg_isflag(arg, X) )
    FLAG("-s"){
      if( !arg->next ){
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Missing SEPARATOR argument to -s.");
        break;
      }
      cmpp_arg_to_b(dx, arg->next,
                    cmpp_b_reuse(bSep),
                    cmpp_arg_to_b_F_BRACE_CALL);
      arg = arg->next;
      continue;
    }
    //FLAG("-nl"){ addNl=true; continue; }
    FLAG("-nonl"){ addNl=false; continue; }
#undef FLAG
    if( n++ && cmpp_dx_out_raw(dx, bSep->z, bSep->n) ){
      break;
    }
    if( cmpp_arg_to_b(dx, arg, cmpp_b_reuse(b), a2bFlags) ){
      break;
    }
    cmpp_dx_out_raw(dx, b->z, b->n);
  }
  if( !cmpp_dx_err_check(dx) ){
    if( !n ){
      cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                      "Expecting at least one argument.");
    }else if( addNl ){
      cmpp_dx_out_raw(dx, "\n", 1);
    }
  }
end:
  cmpp_b_return(dx->pp, b);
  cmpp_b_return(dx->pp, bSep);
}


/* Impl. for #file */
static void cmpp_dx_f_file(cmpp_dx *dx){
  if( !dx->args.arg0 ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Expecting one or more arguments");
    return;
  }
  cmpp_d const * const d = dx->d;
  enum e_op {
    op_none, op_exists, op_join
  };
  cmpp_b * const b0 = cmpp_b_borrow(dx->pp);
  if( !b0 ) goto end;
  enum e_op op = op_none;
  cmpp_arg const * opArg = 0;
  cmpp_arg const * arg = 0;
  for( arg = dx->args.arg0;
       0==dxppCode && arg;
       arg = arg->next ){
    if( op_none==op ){
      if( cmpp_arg_equals(arg, "exists") ){
        op = op_exists;
        opArg = arg->next;
        arg = opArg->next;
        if( arg ){
          cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                          "%s%s exists: too many arguments",
                          cmpp_dx_delim(dx), d->name.z);
          goto end;
        }
        break;
      }else if( cmpp_arg_equals(arg, "join") ){
        op = op_join;
        if( !arg->next ) goto missing_arg;
        arg = arg->next;
        break;
      }else{
        cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                        "Unknown %s%s command: %s",
                        cmpp_dx_delim(dx), d->name.z, arg->z);
        goto end;
      }
      cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "%s%s unhandled argument: %s",
                      cmpp_dx_delim(dx), d->name.z, arg->z);
      goto end;
    }
  }
  switch( op ){
    case op_none: goto missing_arg;
    case op_join: {
      int i = 0;
      cmpp_flag32_t const bFlags = cmpp_arg_to_b_F_BRACE_CALL;
      for( ; arg; arg = arg->next, ++i ){
        if( cmpp_arg_to_b(dx, arg, cmpp_b_reuse(b0), bFlags)
            || (i && cmpp_dx_out_raw(dx, "/", 1))
            || (b0->n && cmpp_dx_out_raw(dx, b0->z, b0->n)) ){
          break;
        }
      }
      cmpp_dx_out_raw(dx, "\n", 1);
      break;
    }
    case op_exists: {
      assert( opArg );
      bool const b = cmpp__file_is_readable((char const *)opArg->z);
      cmpp_dx_out_raw(dx, b ? "1\n" : "0\n", 2);
      break;
    }
  }
end:
  cmpp_b_return(dx->pp, b0);
  return;
missing_arg:
  if( arg ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "%s%s %s: missing argument",
                    cmpp_dx_delim(dx), d->name.z, arg->z );
  }else{
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "%s%s: missing subcommand",
                    cmpp_dx_delim(dx), d->name.z);
  }
  goto end;
}


/**
   #cmp LHS op RHS
*/
static void cmpp_dx_f_cmp(cmpp_dx *dx){
  cmpp_b * const bL = cmpp_b_borrow(dx->pp);
  cmpp_b * const bR = cmpp_b_borrow(dx->pp);
  cmpp_flag32_t a2bFlags = cmpp_arg_to_b_F_BRACE_CALL;
  if( !bL || !!bR ) goto end;
  for( cmpp_arg const * arg = dx->args.arg0; arg
         && !cmpp_dx_err_check(dx);
       arg = arg->next ){
    if( !bL->z ){
      cmpp_arg_to_b(dx, arg, bL, a2bFlags);
      continue;
    }
    if( !bR->z ){
      cmpp_arg_to_b(dx, arg, bR, a2bFlags);
      continue;
    }
    goto usage;
  }

  if( cmpp_dx_err_check(dx) ) goto end;
  if( !bL->z || !bR->z ){
  usage:
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Usage: LHS RHS");
    goto end;
  }
  assert( bL->z );
  assert( bR->z );
  char cbuf[20];
  int const cmp = strcmp((char*)bL->z, (char*)bR->z);
  int const n = snprintf(cbuf, sizeof(cbuf), "%d", cmp);
  assert(n>0);
  cmpp_dx_out_raw(dx, cbuf, (cmpp_size_t)n);

end:
  cmpp_b_return(dx->pp, bL);
  cmpp_b_return(dx->pp, bR);
}


#if 0
/* Impl. for dummy placeholder. */
static void cmpp_dx_f_todo(cmpp_dx *dx){
  cmpp_d const * d = cmpp_dx_d(dx);
  g_warn("TODO: directive handler for %s", d->name.z);
}
#endif

/**
   If zName matches one of the delayed-load directives, that directive
   is registered and 0 is returned. CMPP_RC_NO_DIRECTIVE is returned if
   no match is found, but pp's error state is not updated in that
   case. If a match is found and registration fails, that result code
   will propagate via pp.
*/
int cmpp__d_delayed_load(cmpp *pp, char const *zName){
  if( ppCode ) return ppCode;
  int rc = CMPP_RC_NO_DIRECTIVE;
  unsigned const nName = strlen(zName);

  pp->pimpl->flags.isInternalDirectiveReg = true;

#define M(NAME) (nName==sizeof(NAME)-1 && 0==strcmp(zName,NAME))
#define M_OC(NAME) (M(NAME) || M("/" NAME))
#define M_IF(NAME) if( M(NAME) )
#define CF(X) cmpp_d_F_ ## X
#define F_A_RAW CF(ARGS_RAW)
#define F_A_LIST CF(ARGS_LIST)
#define F_EXPR CF(ARGS_LIST) | CF(NOT_SIMPLIFY)
#define F_UNSAFE cmpp_d_F_NOT_IN_SAFEMODE
#define F_NC cmpp_d_F_NO_CALL
#define F_CALL cmpp_d_F_CALL_ONLY
#define DREG0(SYMNAME, NAME, OPENER, OFLAGS, CLOSER, CFLAGS) \
  cmpp_d_reg SYMNAME = {  \
    .name = NAME,         \
    .opener = {           \
      .f = OPENER,        \
      .flags = OFLAGS     \
    },                    \
    .closer = {           \
      .f = CLOSER,        \
      .flags = CFLAGS     \
    },                    \
    .dtor = 0,            \
    .state = 0            \
  }

#define DREG(NAME, OPENER, OFLAGS, CLOSER, CFLAGS )         \
  DREG0(const rReg, NAME, OPENER, OFLAGS, CLOSER, CFLAGS ); \
  rc = cmpp_d_register(pp, &rReg, NULL);                    \
  goto end

  /* The #if family requires some hand-holding... */
  if( M_OC("if") || M("elif") || M("else") ) {
    DREG0(rIf,   "if",
          cmpp_dx_f_if, F_EXPR | F_NC | CF(FLOW_CONTROL),
          cmpp_dx_f_if_dangler, 0);
    DREG0(rElif, "elif",
          cmpp_dx_f_if_dangler, F_NC,
          0, 0);
    DREG0(rElse, "else",
          cmpp_dx_f_if_dangler, F_NC,
          0, 0);
    CmppIfState * const cis = cmpp__malloc(pp, sizeof(*cis));
    if( !cis ) goto end;
    memset(cis, 0, sizeof(*cis));
    rIf.state = cis;
    rIf.dtor  = cmpp_mfree;
    if( cmpp_d_register(pp, &rIf, &cis->dIf)
        /* rIf must be first to avoid leaking cis on error */
        || cmpp_d_register(pp, &rElif, &cis->dElif)
        || cmpp_d_register(pp, &rElse, &cis->dElse) ){
      rc = ppCode;
    }else{
      assert( cis->dIf && cis->dElif && cis->dElse );
      assert( !cis->dEndif );
      assert( cis == cis->dIf->impl.state );
      assert( cmpp_mfree==cis->dIf->impl.dtor );
      cis->dElif->impl.state
        = cis->dElse->impl.state
        = cis;
      cis->dElif->closer
        = cis->dElse->closer
        = cis->dEndif
        = cis->dIf->closer;
      rc = 0;
    }
    goto end;
  }/* #if and friends */

  /* Basic core directives... */
#define M_IF_CORE(N,OPENER,OFLAGS,CLOSER,CFLAGS)  \
  if( M_OC(N) ){                                  \
    DREG(N, OPENER, OFLAGS, CLOSER, CFLAGS);      \
  } (void)0

  M_IF_CORE("@",                cmpp_dx_f_at,           F_A_LIST,
                                cmpp_dx_f_dangling_closer, 0);
  M_IF_CORE("arg",              cmpp_dx_f_arg,          F_A_LIST, 0, 0);
  M_IF_CORE("assert",           cmpp_dx_f_expr,         F_EXPR, 0, 0);
  M_IF_CORE("cmp",              cmpp_dx_f_cmp,          F_A_LIST, 0, 0);
  M_IF_CORE("define",           cmpp_dx_f_define,       F_A_LIST,
                                cmpp_dx_f_dangling_closer, 0);
  M_IF_CORE("delimiter",        cmpp_dx_f_delimiter,    F_A_LIST,
                                cmpp_dx_f_dangling_closer, 0);
  M_IF_CORE("error",            cmpp_dx_f_error,        F_A_RAW, 0, 0);
  M_IF_CORE("expr",             cmpp_dx_f_expr,         F_EXPR, 0, 0);
  M_IF_CORE("join",             cmpp_dx_f_join,         F_A_LIST, 0, 0);
  M_IF_CORE("once",             cmpp_dx_f_once,         F_A_LIST | F_NC,
                                cmpp_dx_f_dangling_closer, 0);
  M_IF_CORE("pragma",           cmpp_dx_f_pragma,       F_A_LIST, 0, 0);
  M_IF_CORE("savepoint",        cmpp_dx_f_savepoint,    F_A_LIST, 0, 0);
  M_IF_CORE("stderr",           cmpp_dx_f_stderr,       F_A_RAW, 0, 0);
  M_IF_CORE("sum",              cmpp_dx_f_sum,          F_A_LIST, 0, 0);
  M_IF_CORE("undef",            cmpp_dx_f_undef,        F_A_LIST, 0, 0);
  M_IF_CORE("undefined-policy", cmpp_dx_f_undef_policy, F_A_LIST, 0, 0);
  M_IF_CORE("//",               cmpp_dx_f_noop,         F_A_RAW, 0, 0);
  M_IF_CORE("file",             cmpp_dx_f_file,
                                F_A_LIST | F_UNSAFE, 0, 0);

#undef M_IF_CORE


  /* Directives which can be disabled via build flags or
   flags to cmpp_ctor()... */
#define M_IF_FLAGGED(NAME,FLAG,OPENER,OFLAGS,CLOSER,CFLAGS) \
  M_IF(NAME) {                                \
    if( 0==(FLAG & pp->pimpl->flags.newFlags) ) {    \
      DREG(NAME,OPENER,OFLAGS,CLOSER,CFLAGS); \
    }                                         \
    goto end;                                 \
  }

#ifndef CMPP_OMIT_D_INCLUDE
  M_IF_FLAGGED("include", cmpp_ctor_F_NO_INCLUDE,
               cmpp_dx_f_include, F_A_LIST | F_UNSAFE,
               0, 0);
#endif

#ifndef CMPP_OMIT_D_PIPE
  M_IF_FLAGGED("pipe", cmpp_ctor_F_NO_PIPE,
               cmpp_dx_f_pipe, F_A_RAW | F_UNSAFE,
               cmpp_dx_f_dangling_closer, 0);
#endif

#ifndef CMPP_OMIT_D_DB
  M_IF_FLAGGED("attach", cmpp_ctor_F_NO_DB,
               cmpp_dx_f_attach, F_A_LIST | F_UNSAFE,
               0, 0);
  M_IF_FLAGGED("detach", cmpp_ctor_F_NO_DB,
               cmpp_dx_f_detach, F_A_LIST | F_UNSAFE,
               0, 0);
  if( 0==(cmpp_ctor_F_NO_DB & pp->pimpl->flags.newFlags)
      && (M_OC("query") || M("query:no-rows")) ){
    DREG0(rQ, "query", cmpp_dx_f_query, F_A_LIST | F_UNSAFE,
          cmpp_dx_f_dangling_closer, 0);
    cmpp_d * dQ = 0;
    rc = cmpp_d_register(pp, &rQ, &dQ);
    if( 0==rc ){
      /*
        It would be preferable to delay registration of query:no-rows
        until we need it, but doing so causes an error when:

        |#if 0
        |#query
        |...
        |#query:no-rows   HERE
        |...
        |#/query
        |#/if

        Because query:no-rows won't have been registered, and unknown
        directives are an error even in skip mode. Maybe they
        shouldn't be. Maybe we should just skip them in skip mode.
        That's only been an issue since doing delayed registration of
        directives, so it's not come up until recently (as of
        2025-10-27). i was so hoping to be able to get _rid_ of skip
        mode at some point.
      */
      cmpp_d * dNoRows = 0;
      cmpp_d_reg const rNR = {
        .name = "query:no-rows",
        .opener = {
          .f = cmpp_dx_f_dangling_closer,
          .flags = F_NC
        }
      };
      rc = cmpp_d_register(pp, &rNR, &dNoRows);
      if( 0==rc ){
        dNoRows->closer = dQ->closer;
        assert( !dQ->impl.state );
        dQ->impl.state = dNoRows;
      }
    }
    goto end;
  }
#endif /*CMPP_OMIT_D_DB*/

#if CMPP_D_MODULE
  extern void cmpp_dx_f_module(cmpp_dx *);
  M_IF_FLAGGED("module", cmpp_ctor_F_NO_MODULE,
               cmpp_dx_f_module, F_A_LIST | F_UNSAFE,
               0, 0);
#endif

#undef M_IF_FLAGGED

#ifndef NDEBUG
  M_IF("experiment"){
    DREG("experiment", cmpp_dx_f_experiment,
         F_A_LIST | F_UNSAFE, 0, 0);
  }
#endif

end:
#undef DREG
#undef DREG0
#undef F_EXPR
#undef F_A_RAW
#undef F_A_LIST
#undef F_UNSAFE
#undef F_NC
#undef F_CALL
#undef CF
#undef M
#undef M_OC
#undef M_IF
  pp->pimpl->flags.isInternalDirectiveReg = false;
  return ppCode ? ppCode : rc;
}
/*
** 2026-02-07:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses filesystem-related APIs libcmpp.
*/

#include <unistd.h>

/**
   There are APIs i'd _like_ to have here, but the readily-available
   code for them BSD license, so can't be pasted in here. Examples:

   - Filename canonicalization.

   - Cross-platform getcwd() (see below).

   - Windows support. This requires, in addition to the different
   filesystem APIs, converting strings into something it can use.

   All of that adds up to infrastructure... which already exists
   elsewhere but can't be copied here while retaining this project's
   license.
*/

bool cmpp__file_is_readable(char const *zFile){
  return 0==access(zFile, R_OK);
}

#if 0
FILE *cmpp__fopen(const char *zName, const char *zMode){
  FILE *f;
  if(zName && ('-'==*zName && !zName[1])){
    f = (strchr(zMode, 'w') || strchr(zMode,'+'))
      ? stdout
      : stdin
      ;
  }else{
    f = fopen(zName, zMode);
  }
  return f;
}

void cmpp__fclose( FILE * f ){
  if(f && (stdin!=f) && (stdout!=f) && (stderr!=f)){
    fclose(f);
  }
}
#endif
/*
** 2025-11-07:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses the arguments-handling-related pieces for libcmpp.
*/

const cmpp_args_pimpl cmpp_args_pimpl_empty =
  cmpp_args_pimpl_empty_m;
const cmpp_args cmpp_args_empty = cmpp_args_empty_m;
const cmpp_arg cmpp_arg_empty = cmpp_arg_empty_m;

//just in case these ever get dynamic state
void cmpp_arg_cleanup(cmpp_arg *arg){
  if( arg ) *arg = cmpp_arg_empty;
}

//just in case these ever get dynamic state
void cmpp_arg_reuse(cmpp_arg *arg){
  if( arg ) *arg = cmpp_arg_empty;
}

/** Resets li's list for re-use but does not free it. Returns li. */
static CmppArgList * CmppArgList_reuse(CmppArgList *li){
  for(cmpp_size_t n = li->nAlloc; n; ){
    cmpp_arg_reuse( &li->list[--n] );
    assert( !li->list[n].next );
  }
  li->n = 0;
  return li;
}

/** Free all memory owned by li but does not free li. */
void CmppArgList_cleanup(CmppArgList *li){
  const CmppArgList CmppArgList_empty = CmppArgList_empty_m;
  while( li->nAlloc ){
    cmpp_arg_cleanup( &li->list[--li->nAlloc] );
  }
  cmpp_mfree(li->list);
  *li = CmppArgList_empty;
}

/** Returns the most-recently-appended arg of li back to li's
    free-list. */
static void CmppArgList_unappend(CmppArgList *li){
  assert( li->n );
  if( li->n ){
    cmpp_arg_reuse( &li->list[--li->n] );
  }
}

cmpp_arg * CmppArgList_append(cmpp *pp, CmppArgList *li){
  cmpp_arg * p = 0;
  assert( li->list ? li->nAlloc : 0==li->nAlloc );
  if( 0==ppCode
      && 0==CmppArgList_reserve(pp, li,
                                cmpp__li_reserve1_size(li,10)) ){
    p = &li->list[li->n++];
    cmpp_arg_reuse( p );
  }
  return p;
}

void cmpp_args_pimpl_cleanup(cmpp_args_pimpl *p){
  assert( !p->nextFree );
  cmpp_b_clear(&p->argOut);
  CmppArgList_cleanup(&p->argli);
  *p = cmpp_args_pimpl_empty;
}

static void cmpp_args_pimpl_reuse(cmpp_args_pimpl *p){
  assert( !p->nextFree );
  cmpp_b_reuse(&p->argOut);
  CmppArgList_reuse(&p->argli);
  assert( !p->argOut.n );
  assert( !p->argli.n );
}

static void cmpp_args_pimpl_return(cmpp *pp, cmpp_args_pimpl *p){
  if( p ){
    assert( p->pp );
    cmpp__pi(pp);
    assert( !p->nextFree );
    cmpp_args_pimpl_reuse(p);
    p->nextFree = pi->recycler.argPimpl;
    pi->recycler.argPimpl = p;
  }
}

static cmpp_args_pimpl * cmpp_args_pimpl_borrow(cmpp *pp){
  cmpp__pi(pp);
  cmpp_args_pimpl * p = 0;
  if( pi->recycler.argPimpl ){
    p = pi->recycler.argPimpl;
    pi->recycler.argPimpl = p->nextFree;
    p->nextFree = 0;
    p->pp = pp;
    assert( !p->argOut.n && "Buffer was used when not borrowed" );
  }else{
    p = cmpp__malloc(pp, sizeof(*p));
    if( 0==cmpp_check_oom(pp, p) ) {
      *p = cmpp_args_pimpl_empty;
      p->pp = pp;
    }
  }
  return p;
}

CMPP__EXPORT(void, cmpp_args_cleanup)(cmpp_args *a){
  if( a ){
    if( a->pimpl ){
      cmpp * const pp = a->pimpl->pp;
      assert( pp );
      if( pp ){
        cmpp_args_pimpl_return(pp, a->pimpl);
      }else{
        cmpp_args_pimpl_cleanup(a->pimpl);
        cmpp_mfree(a->pimpl);
      }
    }
    *a = cmpp_args_empty;
  }
}

CMPP__EXPORT(void, cmpp_args_reuse)(cmpp_args *a){
  cmpp_args_pimpl * const p = a->pimpl;
  if( p ) cmpp_args_pimpl_reuse(p);
  *a = cmpp_args_empty;
  a->pimpl = p;
}

int cmpp_args__init(cmpp * pp, cmpp_args * a){
  if( 0==ppCode ){
    if( a->pimpl ){
      assert( a->pimpl->pp == pp );
      cmpp_args_reuse(a);
      assert(! a->pimpl->argOut.n );
      assert( a->pimpl->pp == pp );
    }else{
      a->pimpl = cmpp_args_pimpl_borrow(pp);
      assert( !a->pimpl || a->pimpl->pp==pp );
    }
  }
  return ppCode;
}

/**
   Declare cmpp_argOp_f_NAME().
*/
#define cmpp_argOp_decl(NAME)                                \
  static void cmpp_argOp_f_ ## NAME (cmpp_dx *dx,            \
                                     cmpp_argOp const *op,   \
                                     cmpp_arg const *vLhs,   \
                                     cmpp_arg const **pvRhs, \
                                     int *pResult)
cmpp_argOp_decl(compare);

#if 0
cmpp_argOp_decl(logical1);
cmpp_argOp_decl(logical2);
cmpp_argOp_decl(defined);
#endif

static const struct {
  const cmpp_argOp opAnd;
  const cmpp_argOp opOr;
  const cmpp_argOp opGlob;
  const cmpp_argOp opNotGlob;
  const cmpp_argOp opNot;
  const cmpp_argOp opDefined;
#define cmpp_argOps_cmp_map(E) E(Eq) E(Neq) E(Lt) E(Le) E(Gt) E(Ge)
#define E(NAME) const cmpp_argOp op ## NAME;
  cmpp_argOps_cmp_map(E)
#undef E
} cmpp_argOps = {
  .opAnd = {
    .ttype = cmpp_TT_OpAnd,
    .arity = 2,
    .assoc = 0,
    .xCall = 0//cmpp_argOp_f_logical2
  },
  .opOr = {
    .ttype = cmpp_TT_OpOr,
    .arity = 2,
    .assoc = 0,
    .xCall = 0//cmpp_argOp_f_logical2
  },
  .opGlob = {
    .ttype = cmpp_TT_OpGlob,
    .arity = 2,
    .assoc = 0,
    .xCall = 0//cmpp_argOp_f_glob
  },
  .opNotGlob = {
    .ttype = cmpp_TT_OpNotGlob,
    .arity = 2,
    .assoc = 0,
    .xCall = 0//cmpp_argOp_f_glob
  },
  .opNot = {
    .ttype = cmpp_TT_OpNot,
    .arity = 1,
    .assoc = 1,
    .xCall = 0//cmpp_argOp_f_logical1
  },
  .opDefined = {
    .ttype = cmpp_TT_OpDefined,
    .arity = 1,
    .assoc = 1,
    .xCall = 0//cmpp_argOp_f_defined
  },
  /* Comparison ops... */
#define E(NAME) .op ## NAME = { \
  .ttype = cmpp_TT_Op ## NAME, .arity = 2, .assoc = 0, \
  .xCall = cmpp_argOp_f_compare },
  cmpp_argOps_cmp_map(E)
#undef E
};

cmpp_argOp const * cmpp_argOp_for_tt(cmpp_tt tt){
  switch(tt){
    case cmpp_TT_OpAnd:     return &cmpp_argOps.opAnd;
    case cmpp_TT_OpOr:      return &cmpp_argOps.opOr;
    case cmpp_TT_OpGlob:     return &cmpp_argOps.opGlob;
    case cmpp_TT_OpNot:     return &cmpp_argOps.opNot;
    case cmpp_TT_OpDefined: return &cmpp_argOps.opDefined;
#define E(NAME) case cmpp_TT_Op ## NAME: return &cmpp_argOps.op ## NAME;
  cmpp_argOps_cmp_map(E)
#undef E
    default: return NULL;
  }
}
#define argOp(ARG) cmpp_argOp_for_tt((ARG)->ttype)

#if 0
cmpp_argOp_decl(logical1){
  assert( cmpp_TT_OpNot==op->ttype );
  assert( !vRhs );
  assert( vLhs );
  if( 0==cmpp__arg_toBool(dx, vLhs, pResult) ){
    *pResult = !*pResult;
  }
}

cmpp_argOp_decl(logical2){
  assert( vRhs );
  assert( vLhs );
  int vL = 0;
  int vR = 0;
  if( 0==cmpp__arg_toBool(dx, vLhs, &vL)
      && 0==cmpp__arg_toBool(dx, vRhs, &vR) ){
    switch( op->ttype ){
      case cmpp_TT_OpAnd: *pResult = vL && vR; break;
      case cmpp_TT_OpOr:  *pResult = vL || vR; break;
      default:
        cmpp__fatal("Cannot happen: illegal op mapping");
    }
  }
}

cmpp_argOp_decl(defined){
  assert( cmpp_TT_OpDefined==op->ttype );
  assert( !vRhs );
  assert( vLhs );
  if( cmpp_TT_Word==vLhs->ttype ){
    *pResult = cmpp_has(pp, (char const *)vLhs->z, vLhs->n);
    if( !*pResult && vLhs->n>1 && '#'==vLhs->z[0] ){
      *pResult = !!cmpp__d_search3(pp, vLhs->z+1,
                                   cmpp__d_search3_F_NO_DLL);
    }
  }else{
    cmpp__err(pp, CMPP_RC_TYPE, "Invalid token type %s for %s",
              cmpp__tt_cstr(vLhs->ttype, true),
              cmpp__tt_cstr(op->ttype, false));
  }
}
#endif

#if 0
static cmpp_argOp const * cmpp_argOp_isCompare(cmpp_tt tt){
  cmpp_argOp const * const p = cmpp_argOp_for_tt(tt);
  switch( p ? p->ttype : cmpp_TT_None ){
#define E(NAME) case cmpp_TT_Op ## NAME: return p;
  cmpp_argOps_cmp_map(E)
#undef E
      return p;
    case cmpp_TT_None:
    default:
      return NULL;
  }
}
#endif

/**
   An internal helper for cmpp_argOp_...(). It binds some value of
   *paArg to column bindNdx of query q and sets *paArg to the next
   argument to be consumed. This function expects that q is set up to
   do the right thing when *paArg is a Word-type value (see
   cmpp_argOp_f_compare()).
*/
static void cmpp_argOp__cmp_bind(cmpp_dx * const dx,
                                 sqlite3_stmt * const q,
                                 int bindNdx,
                                 cmpp_arg const ** paArg){
  cmpp_arg const * const arg = *paArg;
  assert(arg);
  switch( dxppCode ? 0 : arg->ttype ){
    case 0: break;
    case cmpp_TT_Word:
      /* In this case, q is supposed to be set up to use
         CMPP__SEL_V_FROM(bindNdx), i.e. it expects the verbatim word
         and performs the expansion to its value in the query. */
      cmpp__bind_textn(dx->pp, q, bindNdx, arg->z, arg->n);
      *paArg = arg->next;
      break;
    case cmpp_TT_StringAt:
    case cmpp_TT_String:
    case cmpp_TT_Int:{
      cmpp__bind_arg(dx, q, bindNdx, arg);
      *paArg = arg->next;
      break;
    }
    case cmpp_TT_OpNot:
    case cmpp_TT_OpDefined:
    case cmpp_TT_GroupParen:{
      int rv = 0;
      if( 0==cmpp__arg_toBool(dx, arg, &rv, paArg) ){
        cmpp__bind_int(dx->pp, q, bindNdx, rv);
      }
      *paArg = arg->next;
      break;
    }
      /* TODO? cmpp_TT_GroupParen */
    default:
      cmpp_dx_err_set(dx, CMPP_RC_TYPE,
                      "Invalid argument type (%s) for the comparison "
                      "queries: %s",
                      cmpp_tt_cstr(arg->ttype), arg->z);
  }
}

/**
   Internal helper for cmp_argOp_...().

   Expects q to be a query with an integer in result column 0.  This
   steps/resets the query and applies the given comparison operator's
   logic to column 0's value, placing the result of the operator in
   *pResult.

   If q has no result row, a default value of 0 is assumed.
*/
static void cmpp_argOp__cmp_apply(cmpp * const pp,
                                 cmpp_argOp const * const op,
                                 sqlite3_stmt * const q,
                                 int * const pResult){
  if( 0==ppCode ){
    int rc = cmpp__step(pp, q, false);
    assert( SQLITE_ROW==rc || ppCode );
    if( SQLITE_ROW==rc ){
      rc = sqlite3_column_int(q, 0);
    }else{
      rc = 0;
    }
    switch( op->ttype ){
      case 0: break;
      case cmpp_TT_OpEq:  *pResult = 0==rc; break;
      case cmpp_TT_OpNeq: *pResult = 0!=rc; break;
      case cmpp_TT_OpLt:  *pResult = rc<0;  break;
      case cmpp_TT_OpLe:  *pResult = rc<=0; break;
      case cmpp_TT_OpGt:  *pResult = rc>0;  break;
      case cmpp_TT_OpGe:  *pResult = rc>=0; break;
      default:
        cmpp__fatal("Cannot happen: invalid arg mapping");
    }
  }
  cmpp__stmt_reset(q);
}

/**
   Applies *paRhs as the RHS of an integer binary operator, the LHS of
   which is the lhs argument. The result is put in *pResult. On
   success *paRhs is set to the next argument for the expression to
   parse.
*/
static void cmpp_argOp_applyTo(cmpp_dx *dx,
                               cmpp_argOp const * const op,
                               int lhs,
                               cmpp_arg const ** paRhs,
                               int * pResult){
  sqlite3_stmt * q = 0;
  cmpp_arg const * aRhs = *paRhs;
  assert(aRhs);
  q = cmpp_TT_Word==aRhs->ttype
    ? cmpp__stmt(dx->pp, CmppStmt_cmpVD, false)
    : cmpp__stmt(dx->pp, CmppStmt_cmpVV, false);
  if( q ){
    char numbuf[32];
    int const nNum = snprintf(numbuf, sizeof(numbuf), "%d", lhs);
    cmpp__bind_textn(dx->pp, q, 1, ustr_c(numbuf), nNum);
    cmpp_argOp__cmp_bind(dx, q, 2, paRhs);
    cmpp_argOp__cmp_apply(dx->pp, op, q, pResult);
  }
}

cmpp_argOp_decl(compare){
  cmpp_arg const * const vRhs = *pvRhs;
  sqlite3_stmt * q = 0;
  /* Select which query to use, depending on whether each
     of the LHS/RHS are Word tokens. For Word tokens
     the corresponding query columns get bound to
     a subquery which resolves the word. Non-word
     tokens get bound as-is. */
  if( cmpp_TT_Word==vLhs->ttype ){
    q = cmpp_TT_Word==vRhs->ttype
      ? cmpp__stmt(dx->pp, CmppStmt_cmpDD, false)
      : cmpp__stmt(dx->pp, CmppStmt_cmpDV, false);
    if(0){
      g_warn("\nvLhs=%s %s\nvRhs=%s %s\n",
             cmpp_tt_cstr(vLhs->ttype), vLhs->z,
             cmpp_tt_cstr(vRhs->ttype), vRhs->z);
    }
  }else if( cmpp_TT_Word==vRhs->ttype ){
    q = cmpp__stmt(dx->pp, CmppStmt_cmpVD, false);
  }else{
    q = cmpp__stmt(dx->pp, CmppStmt_cmpVV, false);
  }
  if( q ){
    //cmpp__bind_textn(pp, q, 1, vLhs->z, vLhs->n);
    cmpp_argOp__cmp_bind(dx, q, 1, &vLhs);
    cmpp_argOp__cmp_bind(dx, q, 2, pvRhs);
    cmpp_argOp__cmp_apply(dx->pp, op, q, pResult);
  }
}

#undef cmpp_argOp_decl

#if 0
static inline int cmpp_dxt_isBinOp(cmpp_tt tt){
  cmpp_argOp const * const a = cmpp_argOp_for_tt(tt);
  return a ? 2==a->arity : 0;
}

static inline int cmpp_dxt_isUnaryOp(cmpp_tt tt){
  return tt==cmpp_TT_OpNot || cmpp_TT_OpDefined;
}

static inline int cmpp_dxt_isGroup(cmpp_tt tt){
  return tt==cmpp_TT_GroupParen || tt==cmpp_TT_GroupBrace || cmpp_TT_GroupSquiggly;
}
#endif

int cmpp__arg_evalSubToInt(cmpp_dx *dx,
                           cmpp_arg const *arg,
                           int * pResult){
  cmpp_args sub = cmpp_args_empty;
  if( 0==cmpp_args_parse(dx, &sub, arg->z, arg->n, 0) ){
    cmpp__args_evalToInt(dx, &sub, pResult);
  }
  cmpp_args_cleanup(&sub);
  return dxppCode;
}

int cmpp__args_evalToInt(cmpp_dx * const dx,
                        cmpp_args const *pArgs,
                        int * pResult){
  if( dxppCode ) return dxppCode;

  cmpp_arg const * pNext = 0;
  cmpp_arg const * pPrev = 0;
  int result = *pResult;
  cmpp_b osL = cmpp_b_empty;
  cmpp_b osR = cmpp_b_empty;
  static int level = 0;
  ++level;

#define lout(fmt,...) if(0) g_stderr("%.*c" fmt, level*2, ' ', __VA_ARGS__)

  //lout("START %s(): %s\n", __func__, pArgs->pimpl->buf.argsRaw.z);
  for( cmpp_arg const *arg = pArgs->arg0;
       arg && 0==dxppCode;
       pPrev = arg, arg = pNext ){
    pNext = arg->next;
    if( cmpp_TT_Noop==arg->ttype ){
      arg = pPrev /* help the following arg to DTRT */;
      continue;
    }
    cmpp_argOp const * const thisOp = argOp(arg);
    cmpp_argOp const * const nextOp = pNext ? argOp(pNext) : 0;
    if( 0 ){
      lout("arg: %s @%p %s\n",
           cmpp__tt_cstr(arg->ttype, true), arg, arg->z);
      if(1){
        if( pPrev ) lout("  prev arg: %s %s\n",
                         cmpp__tt_cstr(pPrev->ttype, true), pPrev->z);
        if( pNext ) lout("  next arg: %s %s\n",
                         cmpp__tt_cstr(pNext->ttype, true), pNext->z);
      }
    }
    if( thisOp ){ /* Basic validation */
      if( !pNext ){
        dxserr("Missing '%s' RHS.",
               cmpp__tt_cstr(thisOp->ttype, false));
        break;
      }else if( !pPrev && 2==thisOp->arity  ){
        dxserr("Missing %s LHS.",
               cmpp__tt_cstr(thisOp->ttype, false));
        break;
      }
      if( nextOp && nextOp->arity>1 ){
        dxserr("Invalid '%s' RHS: %s", arg->z, pNext->z);
        break;
      }
    }

    switch( arg->ttype ){

      case cmpp_TT_OpNot:
      case cmpp_TT_OpDefined:
        if( pPrev && !argOp(pPrev) ){
          cmpp_dx_err_set(dx, CMPP_RC_CANNOT_HAPPEN,
                          "We expected to have consumed '%s' by "
                          "this point.",
                          pPrev->z);
        }else{
          cmpp__arg_toBool(dx, arg, &result, &pNext);
        }
        break;

      case cmpp_TT_OpAnd:
      case cmpp_TT_OpOr:{
        assert( pNext );
        assert( pPrev );
        /* Reminder to self: we can't add short-circuiting of the RHS
           right now because the handling of chained unary ops on the
           RHS is handled via cmpp__arg_toBool(). */
        int rv = 0;
        if( 0==cmpp__arg_toBool(dx, pNext, &rv, &pNext) ){
          if( cmpp_TT_OpAnd==arg->ttype ) result = result && rv;
          else result = result || rv;
        }
        //g_warn("post-and/or pNext=%s\n", pNext ? pNext->z : 0);
        break;
      }

      case cmpp_TT_OpNotGlob:
      case cmpp_TT_OpGlob:{
        assert( pNext );
        assert( pPrev );
        assert( pNext!=arg );
        assert( pPrev!=arg );
        if( cmpp_arg_to_b(dx, pNext, &osL, 0) ){
          break;
        }
        unsigned char const * const zGlob = osL.z;
        if( 0==cmpp_arg_to_b(dx, pPrev, &osR, 0) ){
          if( 0 ){
            g_warn("zGlob=[%s] z=[%s]", zGlob, osR.z);
          }
          result = 0==sqlite3_strglob((char const *)zGlob,
                                      (char const *)osR.z);
          if( cmpp_TT_OpNotGlob==arg->ttype ){
            result = !result;
          }
          //g_warn("\nzGlob=%s\nz=%s\nresult=%d", zGlob, z, result);
        }
        pNext = pNext->next;
        break;
      }

#define E(NAME) case cmpp_TT_Op ## NAME:
      cmpp_argOps_cmp_map(E) {
        cmpp_argOp const * const prevOp = pPrev ? argOp(pPrev) : 0;
        if( prevOp ){
          /* Chained operators */
          cmpp_argOp_applyTo(dx, thisOp, result, &pNext, &result);
        }else{
          assert( pNext );
          assert( pPrev );
          assert( thisOp );
          assert( thisOp->xCall );
          thisOp->xCall(dx, thisOp, pPrev, &pNext, &result);
        }
        break;
      }
#undef E

#define checkConsecutiveNonOps                             \
      if( pPrev && !argOp(pPrev) ){                        \
        dxserr("Illegal consecutive non-operators: %s %s", \
               pPrev->z, arg->z);                          \
        break;                                             \
      }(void)0

      case cmpp_TT_Int:
      case cmpp_TT_String:
        checkConsecutiveNonOps;
        if( !cmpp__is_int(arg->z, arg->n, &result) ){
          /* This is mostly for and/or ops. glob will reach back and
             grab arg->z. */
          result = 0;
        }
        break;
      case cmpp_TT_Word:
        checkConsecutiveNonOps;
        cmpp__get_int(dx->pp, arg->z, arg->n, &result);
        break;
      case cmpp_TT_GroupParen:{
        checkConsecutiveNonOps;
        cmpp_args sub = cmpp_args_empty;
        if( 0==cmpp_args_parse(dx, &sub, arg->z, arg->n, 0) ){
          cmpp__args_evalToInt(dx, &sub, &result);
        }
        cmpp_args_cleanup(&sub);
        break;
      }
      case cmpp_TT_GroupBrace:{
        checkConsecutiveNonOps;
        cmpp_b b = cmpp_b_empty;
        if( 0==cmpp_call_str(dx->pp, arg->z, arg->n, &b, 0) ){
          cmpp__is_int(b.z, b.n, &result);
        }
        cmpp_b_clear(&b);
        break;
      }
#undef checkConsecutiveNonOps
      default:
        assert( arg->z );
        dxserr("Illegal expression token %s: %s",
               cmpp__tt_cstr(arg->ttype, true), arg->z);
    }/*switch(arg->ttype)*/
  }/* foreach arg */
  if( 0 ){
    lout("END   %s() result=%d\n",  __func__, result);
  }
  --level;
  if( !dxppCode ){
    *pResult = result;
  }
  cmpp_b_clear(&osL);
  cmpp_b_clear(&osR);
  return dxppCode;
#undef lout
}

#undef argOp
#undef cmpp_argOp_decl

static inline cmpp_tt cmpp_dxt_is_group(cmpp_tt ttype){
  switch(ttype){
    case cmpp_TT_GroupParen:
    case cmpp_TT_GroupBrace:
    case cmpp_TT_GroupSquiggly:
      return ttype;
    default:
      return cmpp_TT_None;
  }
}

int cmpp_args_parse(cmpp_dx * const dx,
                    cmpp_args * const pArgs,
                    unsigned char const * const zInBegin,
                    cmpp_ssize_t nIn,
                    cmpp_flag32_t flags){
  assert( zInBegin );
  unsigned char const * const zInEnd =
    zInBegin + cmpp__strlenu(zInBegin, nIn);

  if( cmpp_args__init(dx->pp, pArgs) ) return dxppCode;
  if( 0 ){
    g_warn("whole input = <<%.*s>>", (int)(zInEnd-zInBegin),
           zInBegin);
  }
  unsigned char const * zPos = zInBegin;
  cmpp_size_t const nBuffer =
    /* Buffer size for our copy of the args. We need to know the
       size before we start so that we can have each arg reliably
       point back into this without it being reallocated during
       parsing. */
    (cmpp_size_t)(zInEnd - zInBegin)
    /* Plus we need one final NUL and one NUL byte per argument, but
       we don't yet know how many arguments we will have, so let's
       estimate... */
    + ((cmpp_size_t)(zInEnd - zInBegin))/3
    + 5/*fudge room*/;
  cmpp_b * const buffer = &pArgs->pimpl->argOut;
  assert( !buffer->n );
  if( cmpp_b_reserve3(dx->pp, buffer, nBuffer) ){
    return dxppCode;
  }
  unsigned char * zOut = buffer->z;
  unsigned char const * const zOutEnd = zOut + buffer->nAlloc - 1;
  cmpp_arg * prevArg = 0;
#if !defined(NDEBUG)
  unsigned char const * const zReallocCheck = buffer->z;
#endif

  if(0) g_warn("pre-parsed line: %.*s", (zInEnd - zInBegin),
               zInBegin);
  pArgs->arg0 = NULL;
  pArgs->argc = 0;
  for( int i = 0; zPos<zInEnd; ++i){
    //g_stderr("i=%d prevArg=%p\n",i, prevArg);
    cmpp_arg * const arg =
      CmppArgList_append(dx->pp, &pArgs->pimpl->argli);
    if( !arg ) return dxppCode;
    assert( pArgs->pimpl->argli.n );
    if( 0 ) g_warn("zPos=<<%.*s>>", (int)(zInEnd-zPos), zPos);
    if( cmpp_arg_parse(dx, arg, &zPos, zInEnd, &zOut, zOutEnd) ){
      if( 0 ) g_warn("zPos=<<%.*s>>", (int)(zInEnd-zPos), zPos);
      break;
    }
    if( 0 ){
      g_warn("#%d zPos=<<%.*s>>", i, (int)(zInEnd-zPos), zPos);
      g_warn("#%d arg n=%u z=<<%.*s>> %s", i, (int)arg->n, (int)arg->n, arg->z, arg->z);
    }
    assert( zPos<=zInEnd );
    if( 0 ){
      g_stderr("ttype=%d %s n=%u z=%.*s\n", arg->ttype,
               cmpp__tt_cstr(arg->ttype, true),
               (unsigned)arg->n, (int)arg->n, arg->z);
    }
    if( cmpp_TT_Eof==arg->ttype ){
      CmppArgList_unappend(&pArgs->pimpl->argli);
      break;
    }
    switch( 0==(flags & cmpp_args_F_NO_PARENS)
            ? cmpp_dxt_is_group( arg->ttype )
            : 0 ){
      case cmpp_TT_GroupParen:{
        /* Sub-expression. We tokenize it here just to ensure that we
           can, so we can fail earlier rather than later. This is why
           we need a recycler for the cmpp_args buffer memory. */
        cmpp_args sub = cmpp_args_empty;
        cmpp_args_parse(dx, &sub, arg->z, arg->n, flags);
        //g_stderr("Parsed sub-expr: %s\n", sub.buffer.z);
        cmpp_args_cleanup(&sub);
        break;
      }
      case cmpp_TT_GroupBrace:
      case cmpp_TT_GroupSquiggly:
      default: break;
    }
    if( dxppCode ) break;
    if( prevArg ){
      assert( !prevArg->next );
      prevArg->next = arg;
    }
    prevArg = arg;
  }/*foreach input char*/
  //g_stderr("rc=%s argc=%d\n", cmpp_rc_cstr(dxppCode), pArgs->args.n);
  if( 0==dxppCode ){
    pArgs->argc = pArgs->pimpl->argli.n;
    assert( !pArgs->arg0 );
    if( pArgs->argc ) pArgs->arg0 = pArgs->pimpl->argli.list;
    if( zOut<zInEnd ) *zOut = 0;
    if( 0 ){
      for( cmpp_arg const * a = pArgs->arg0; a; a = a->next ){
        g_stderr("  got: %s %.*s\n", cmpp__tt_cstr(a->ttype, true),
                 a->n, a->z);
      }
    }
  }
  assert(zReallocCheck==buffer->z
         && "Else buffer was reallocated, invalidating argN->z");
  return dxppCode;
}

CMPP__EXPORT(int, cmpp_args_clone)(cmpp *pp, cmpp_arg const * const a0,
                                        cmpp_args * const dest){
  if( cmpp_args__init(pp, dest) || !a0 ) return ppCode;
  cmpp_b * const ob = &dest->pimpl->argOut;
  CmppArgList * const argli = &dest->pimpl->argli;
  unsigned int i = 0;
  cmpp_size_t nReserve = 0 /* arg buffer mem to preallocate */;

  assert( !ob->n );
  assert( !dest->arg0 );
  assert( !dest->argc );
  assert( !argli->n );

  /* Preallocate ob->z to fit a copy of a0's args. */
  for( cmpp_arg const * a = a0; a; ++i, a = a->next ){
    nReserve += a->n + 1/*NUL byte*/;
  }
  if( cmpp_b_reserve3(pp, ob, nReserve+1)
      || CmppArgList_reserve(pp, argli, i) ){
    goto end;
  }
  assert( argli->nAlloc>=i );
  i = 0;
#ifndef NDEBUG
  unsigned char const * const zReallocCheck = ob->z;
#endif
  for( cmpp_arg const * a = a0; a; ++i, a = a->next ){
    cmpp_arg * const aNew = &argli->list[i];
    aNew->n = a->n;
    aNew->z = ob->z + ob->n;
    aNew->ttype = a->ttype;
    if( i ) argli->list[i-1].next = aNew;
    assert( !a->z[a->n] && "Expecting a NUL byte there" );
    cmpp_b_append4(pp, ob, a->z, a->n+1/*NUL byte*/);
    if( 0 ){
      g_warn("arg#%d=%s <<<%.*s>>> %s", i, cmpp_tt_cstr(a->ttype),
             (int)a->n, a->z, a->z);
    }
    assert( zReallocCheck==ob->z
            && "This cannot fail: ob->z was pre-allocated" );
  }
  dest->argc = i;
  dest->arg0 = i ? &argli->list[0] : 0;
end:
  if( ppCode ){
    cmpp_args_reuse(dest);
  }
  return ppCode;
}

CMPP__EXPORT(int, cmpp_dx_args_clone)(cmpp_dx * dx, cmpp_args *pOut){
  return cmpp_args_clone(dx->pp, dx->args.arg0, pOut);
}

char * cmpp_arg_strdup(cmpp *pp, cmpp_arg const *arg){
  char * z = 0;
  if( 0==ppCode ){
    z = sqlite3_mprintf("%s",arg->z);
    cmpp_check_oom(pp, z);
  }
  return z;
}

static cmpp_tt cmpp_tt_forWord(unsigned char const *z, unsigned n,
                               cmpp_tt dflt){
  static const struct {
#define E(NAME,STR) struct CmppSnippet NAME;
    cmpp_tt_map(E)
#undef E
  } ttStr = {
#define E(NAME,STR)                                   \
    .NAME = {(unsigned char const *)STR,sizeof(STR)-1},
    cmpp_tt_map(E)
#undef E
  };
#define CASE(NAME) if( 0==memcmp(ttStr.NAME.z, z, n) ) return cmpp_TT_ ## NAME
  switch( n ){
    case 1:
      CASE(OpEq);
      CASE(Plus);
      CASE(Minus);
      break;
    case 2:
      CASE(OpOr);
      CASE(ShiftL);
      //CASE(ShiftR);
      //CASE(ArrowL);
      CASE(ArrowR);
      CASE(OpNeq);
      CASE(OpLt);
      CASE(OpLe);
      CASE(OpGt);
      CASE(OpGe);
      break;
    case 3:
      CASE(OpAnd);
      CASE(OpNot);
      CASE(ShiftL3);
      break;
    case 4:
      CASE(OpGlob);
      break;
    case 7:
      CASE(OpDefined);
      break;
#undef CASE
  }
#if 0
  bool b = cmpp__is_int(z, n, NULL);
  if( 1|| !b ){
    g_warn("is_int(%s)=%d", z, b);
  }
  return b ? cmpp_TT_Int : dflt;
#else
  return cmpp__is_int(z, n, NULL) ? cmpp_TT_Int : dflt;
#endif
}

int cmpp_arg_parse(cmpp_dx * const dx, cmpp_arg *pOut,
                   unsigned char const **pzIn,
                   unsigned char const *zInEnd,
                   unsigned char ** pzOut,
                   unsigned char const * zOutEnd){
  unsigned char const * zi = *pzIn;
  unsigned char * zo = *pzOut;
  cmpp_tt ttype = cmpp_TT_None;

#if 0
  // trying to tickle valgrind
  for(unsigned char const *x = zi; x < zInEnd; ++x ){
    assert(*x);
  }
#endif
  cmpp_arg_reuse( pOut );
  cmpp_skip_snl( &zi, zInEnd );
  if( zi>=zInEnd ){
    *pzIn = zi;
    pOut->ttype = cmpp_TT_Eof;
    return 0;
  }
#define out(CH) if(zo>=zOutEnd) goto notEnoughOut; *zo++ = CH
#define eot_break if( cmpp_TT_None!=ttype ){ keepGoing = 0; break; } (void)0
  pOut->z = zo;
  bool keepGoing = true;
  for( ; keepGoing
         && 0==dxppCode
         && zi<zInEnd
         && zo<zOutEnd; ){
    cmpp_tt ttOverride = cmpp_TT_None;
    switch( (int)*zi ){
      case 0: keepGoing = false; break;
      case ' ': case '\t': case '\n': case '\r':
        eot_break;
        cmpp_skip_snl( &zi, zInEnd );
        break;
      case '-':
        if ('>'==zi[1] ){
          ttype = cmpp_TT_ArrowR;
          out(*zi++);
          out(*zi++);
          keepGoing = false;
        }else{
          goto do_word;
        }
        break;
      case '=':
        eot_break; keepGoing = false; ttype = cmpp_TT_OpEq; out(*zi++); break;
#define opcmp(CH,TT,TTEQ,TTSHIFT,TTARROW)                        \
      case CH: eot_break; keepGoing = false; ttype = TT; out(*zi++); \
        if( zi<zInEnd && '='==*zi ){ out(*zi++); ttype = TTEQ; } \
        else if( zi<zInEnd && CH==*zi ){ out(*zi++); ttype = TTSHIFT; } \
        else if( (int)TTARROW && zi<zInEnd && '-'==*zi ){ out(*zi++); ttype = TTARROW; }

        opcmp('>',cmpp_TT_OpGt,cmpp_TT_OpGe,cmpp_TT_ShiftR,0) break;
        opcmp('<',cmpp_TT_OpLt,cmpp_TT_OpLe,cmpp_TT_ShiftL,cmpp_TT_ArrowL)
        if( cmpp_TT_ShiftL==ttype && zi<zInEnd && '<'==zi[0] ) {
          out(*zi++);
          ttype = cmpp_TT_ShiftL3;
        }
        break;
#undef opcmp
      case '!':
        eot_break;
        keepGoing = false;
        out(*zi++);
        if( zi < zInEnd && '='==*zi ){
          ttype = cmpp_TT_OpNeq;
          out('=');
          ++zi;
        }else{
          while( zi < zInEnd && '!'==*zi ){
            out(*zi++);
          }
          ttype = cmpp_TT_OpNot;
        }
        break;
      case '@':
        if( zi+2 >= zInEnd || ('"'!=zi[1] && '\''!=zi[1]) ){
          goto do_word;
        }
        //if( cmpp__StringAtIsOk(dx->pp) ) break;
        ttOverride = cmpp_TT_StringAt;
        ++zi /* consume opening '@' */;
        //g_stderr("@-string override\n");
        /* fall through */
      case '"':
      case '\'': {
        /* Parse a string. We do not support backslash-escaping of any
           sort here. Strings which themselves must contain quotes
           should use the other quote type. */
        keepGoing = false;
        if( cmpp_TT_None!=ttype ){
          cmpp_dx_err_set(dx, CMPP_RC_SYNTAX,
                          "Misplaced quote character near: %.*s",
                          (int)(zi+1 - *pzIn), *pzIn);
          break;
        }
        unsigned char const * zQuoteAt = zi;
        if( cmpp__find_closing(dx->pp, &zQuoteAt, zInEnd) ){
          break;
        }
        assert( zi+1 <= zQuoteAt );
        assert( *zi == *zQuoteAt );
        if( (zQuoteAt - zi - 2) >= (zOutEnd-zo) ){
          goto notEnoughOut;
        }
        memcpy(zo, zi+1, zQuoteAt - zi - 1);
        //g_warn("string=<<%.*s>>", (zQuoteAt-zi-1), zo);
        zo += zQuoteAt - zi - 1;
        zi = zQuoteAt + 1/* closing quote */;
        ttype = (cmpp_TT_None==ttOverride ? cmpp_TT_String : ttOverride);
        break;
      }
      case '[':
      case '{':
      case '(': {
        /* Slurp these as a single token for later sub-parsing */
        keepGoing = false;
        unsigned char const * zAt = zi;
        if( cmpp__find_closing(dx->pp, &zi, zInEnd) ) break;
        /* Transform the output, eliding the open/close characters and
           trimming spaces. We need to keep newlines intact, as the
           content may be free-form, intended for other purposes, e.g.
           the #pipe or #query directives. */
        ttype = ('('==*zAt
                 ? cmpp_TT_GroupParen
                 : ('['==*zAt
                    ? cmpp_TT_GroupBrace
                    : cmpp_TT_GroupSquiggly));
        ++zAt /* consume opening brace */;
        /* Trim leading and trailing space, but retain tabs and all but
           the first and last newline. */
        cmpp_skip_space(&zAt, zi);
        if( zAt<zInEnd ){
          if( '\n'==*zAt ) ++zAt;
          else if(zAt+1<zInEnd && '\r'==*zAt && '\n'==zAt[1]) zAt+=2;
        }
        for( ; zAt<zi; ++zAt ){
          out(*zAt);
        }
        if(0) g_warn("parse1: group n=%u [%.*s]\n",
                     (zi-zAt), (zi-zAt), zAt);
        while( zo>*pzOut && ' '==zo[-1] ) *--zo = 0;
        if( zo>*pzOut && '\n'==zo[-1] ){
          *--zo = 0;
          if( zo>*pzOut && '\r'==zo[-1] ){
            *--zo = 0;
          }
        }
        ++zi /* consume the closer */;
        break;
      }
      default:
        ; do_word:
        out(*zi++);
        ttype = cmpp_TT_Word;
        break;
    }
    //g_stderr("kg=%d char=%d %c\n", keepGoing, (int)*zi, *zi);
  }
  if( dxppCode ){
    /* problem already reported */
  }else if( zo>=zOutEnd-1 ){
  notEnoughOut:
    cmpp_dx_err_set(dx, CMPP_RC_RANGE,
                    "Ran out of output space (%u bytes) while "
                    "parsing an argument", (unsigned)(zOutEnd-*pzOut));
  }else{
    pOut->n = (zo - *pzOut);
    if( cmpp_TT_None==ttype ){
      pOut->ttype = cmpp_TT_Eof;
    }else if( cmpp_TT_Word==ttype && pOut->n ){
      pOut->ttype = cmpp_tt_forWord(pOut->z, pOut->n, ttype);
    }else{
      pOut->ttype = ttype;
    }
    *zo++ = 0;
    *pzIn = zi;
    *pzOut = zo;
    switch( pOut->ttype ){
      case cmpp_TT_Int:
        if( '+'==*pOut->z ){ /* strip leading + */
          ++pOut->z;
          --pOut->n;
        }
        break;
      default:
        break;
    }
    if(0){
      g_stderr("parse1: %s n=%u <<%.*s>>",
               cmpp__tt_cstr(pOut->ttype, true), pOut->n,
               pOut->n, pOut->z);
    }
  }
#undef out
#undef eot_break
  return dxppCode;
}

int cmpp__arg_toBool(cmpp_dx * const dx, cmpp_arg const *arg,
                    int * pResult, cmpp_arg const **pNext){
  switch( dxppCode ? 0 : arg->ttype ){
    case 0: break;

    case cmpp_TT_Word:
      *pNext = arg->next;
      *pResult = cmpp__get_bool(dx->pp, arg->z, arg->n);
      break;

    case cmpp_TT_Int:
      *pNext = arg->next;
      cmpp__is_int(arg->z, arg->n, pResult)/*was already validated*/;
      break;

    case cmpp_TT_String:
    case cmpp_TT_StringAt:{
      unsigned char const * z = 0;
      cmpp_size_t n = 0;
      cmpp_b os = cmpp_b_empty;
      if( 0==cmpp__arg_expand_ats(dx, &os, cmpp_atpol_CURRENT,
                                 arg, cmpp_TT_StringAt, &z, &n) ){
        *pNext = arg->next;
        *pResult = n>0 && 0!=memcmp("0\0", z, 2);
      }
      cmpp_b_clear(&os);
      break;
    }

    case cmpp_TT_GroupParen:{
      *pNext = arg->next;
      cmpp_args sub = cmpp_args_empty;
      if( 0==cmpp_args_parse(dx, &sub, arg->z, arg->n, 0) ){
        cmpp__args_evalToInt(dx, &sub, pResult);
      }
      cmpp_args_cleanup(&sub);
      break;
    }

    case cmpp_TT_OpDefined:
      if( !arg->next ){
        dxserr("Missing '%s' RHS.", arg->z);
      }else if( cmpp_TT_Word!=arg->next->ttype ){
        dxserr( "Invalid '%s' RHS: %s", arg->z, arg->next->z);
      }else{
        cmpp_arg const * aOperand = arg->next;
        *pNext = aOperand->next;
        if( aOperand->n>1
            && '#'==aOperand->z[0]
            && !!cmpp__d_search3(dx->pp, (char const*)aOperand->z+1,
                                 cmpp__d_search3_F_NO_DLL) ){
          *pResult = 1;
        }else{
          *pResult = cmpp_has(dx->pp, (char const *)aOperand->z,
                              aOperand->n);
        }
      }
      break;

    case cmpp_TT_OpNot:{
      assert( arg->next && "See cmpp_args__not_simplify()");
      assert( cmpp_TT_OpNot!=arg->next->ttype && "See cmpp_args__not_simplify()");
      if( 0==cmpp__arg_toBool(dx, arg->next, pResult, pNext) ){
        *pResult = !*pResult;
      }
      break;
    }

    default:
      dxserr("Invalid token type %s for %s(): %s",
             cmpp__tt_cstr(arg->ttype, true), __func__, arg->z);
      break;
  }
  return dxppCode;
}

CMPP__EXPORT(int, cmpp_arg_to_b)(cmpp_dx * const dx, cmpp_arg const *arg,
                                 cmpp_b * ob, cmpp_flag32_t flags){
  /**
     Reminder to self: this function specifically does not do any
     expression evaluation of its arguments. Please avoid the
     temptation to make it do so. Unless it proves necessary.  Or
     useful. Even then, though, consider the implications deeply
     before doing so.
  */
  switch( dxppCode
          ? 0
          : ((cmpp_arg_to_b_F_FORCE_STRING & flags)
             ? cmpp_TT_String : arg->ttype) ){

    case 0:
      break;
    case cmpp_TT_Word:
      if( 0==(flags & cmpp_arg_to_b_F_NO_DEFINES) ){
        cmpp__get_b(dx->pp, arg->z, arg->n, ob, true);
        break;
      }
      goto theDefault;
    case cmpp_TT_StringAt:{
      unsigned char const * z = 0;
      cmpp_size_t n = 0;
      if( 0 ){
        g_warn("ob->z [%.*s] [%s]", (int)ob->n, ob->z, ob->z);
      }
      if( 0==cmpp__arg_expand_ats(dx, ob, cmpp_atpol_CURRENT, arg,
                                  cmpp_TT_StringAt, &z, &n)
          && 0 ){
        g_warn("expanded at [%.*s] [%s]", (int)n, z, z);
        g_warn("ob->z [%.*s] [%s]", (int)ob->n, ob->z, ob->z);
      }
      break;
    }
    case cmpp_TT_GroupBrace:
      if( !(cmpp_arg_to_b_F_NO_BRACE_CALL & flags)
          && (cmpp_arg_to_b_F_BRACE_CALL & flags) ){
        cmpp_call_str(dx->pp, arg->z, arg->n, ob, 0);
        break;
      }
      /* fall through */
    default: {
      theDefault: ;
      cmpp_outputer oss = cmpp_outputer_b;
      oss.state = ob;//no: cmpp_b_reuse(ob); Append instead.
      cmpp__out2(dx->pp, &oss, arg->z, arg->n);
      break;
    }
  }
  return dxppCode;
}

int cmpp__bind_arg(cmpp_dx * const dx, sqlite3_stmt * const q,
                   int bindNdx, cmpp_arg const * const arg){

  if( 0 ){
    g_warn("bind #%d %s <<%.*s>>", bindNdx,
           cmpp__tt_cstr(arg->ttype, true),
           (int)arg->n, arg->z);
  }
  switch( arg->ttype ){
    default:
    case cmpp_TT_Int:
    case cmpp_TT_String:
      cmpp__bind_textn(dx->pp, q, bindNdx, arg->z, (int)arg->n);
      break;

    case cmpp_TT_Word:
    case cmpp_TT_StringAt:{
      cmpp_b os = cmpp_b_empty;
      if( 0==cmpp_arg_to_b(dx, arg, &os, 0) ){
        if( 0 ){
          g_warn("bind #%d <<%s>> => <<%.*s>>",
                 bindNdx, arg->z, (int)os.n, os.z);
        }
        cmpp__bind_textn(dx->pp, q, bindNdx, os.z, (int)os.n);
      }
      cmpp_b_clear(&os);
      break;
    }

    case cmpp_TT_GroupParen:{
      cmpp_args sub = cmpp_args_empty;
      int i = 0;
      if( 0==cmpp_args_parse(dx, &sub, arg->z, arg->n, 0)
          && 0==cmpp__args_evalToInt(dx, &sub, &i) ){
        /* See comment above about cmpp_TT_Int. */
        cmpp__bind_int_text(dx->pp, q, bindNdx, i);
      }
      cmpp_args_cleanup(&sub);
      break;
    }

    case cmpp_TT_GroupBrace:{
      cmpp_b b = cmpp_b_empty;
      cmpp_call_str(dx->pp, arg->z, arg->n, &b, 0);
      cmpp__bind_textn(dx->pp, q, bindNdx, b.z, b.n);
      cmpp_b_clear(&b);
      break;
    }

  }
  return dxppCode;
}

/**
   If a is in li->list, return its non-const pointer from li->list
   (O(1)), else return NULL.
*/
static cmpp_arg * CmppArgList_arg_nc(CmppArgList *li, cmpp_arg const * a){
  if( li->nAlloc && a>=li->list && a<(li->list + li->nAlloc) ){
    return li->list + (a - li->list);
  }
  return NULL;
}

/**
   To be called only by cmpp_dx_args_parse() and only if the current
   directive asked for it via cmpp_d::flags cmpp_d_F_NOT_SIMPLIFY.

   Filter chains of "not" operators from pArgs, removing unnecessary
   ones. Also collapse "not glob" into a single cmpp_TT_OpNotGlob argument.
   Performs some basic validation as well to simplify downstream
   operations.  Returns p->err.code and is a no-op if that's set
   before this is called.
*/
static int cmpp_args__not_simplify(cmpp * const pp, cmpp_args *pArgs){
  cmpp_arg * pPrev = 0;
  cmpp_arg * pNext = 0;
  CmppArgList * const ali = &pArgs->pimpl->argli;
  pArgs->argc = 0;
  for( cmpp_arg * arg = ali->n ? &ali->list[0] : NULL;
       arg && !ppCode;
       pPrev=arg, arg = pNext ){
    pNext = CmppArgList_arg_nc(ali, arg->next);
    assert( pNext || !arg->next );
    if( cmpp_TT_OpNot==arg->ttype ){
      if( !pNext ){
        serr("Missing '%s' RHS", arg->z);
        break;
      }
      cmpp_argOp const * const nop = cmpp_argOp_for_tt(pNext->ttype);
      if( nop && nop->arity>1 && cmpp_TT_OpGlob!=nop->ttype ){
        serr("Illegal '%s' RHS: binary '%s' operator",
             arg->z, pNext->z);
        break;
      }
      int bNeg = 1;
      if( '!'==*arg->z ){
        /* odd number of ! == negate */
        bNeg = arg->n & 1;
      }
      while( pNext && cmpp_TT_OpNot==pNext->ttype ){
        bNeg = !bNeg;
        arg->next = pNext = CmppArgList_arg_nc(ali, pNext->next);
      }
      if( pNext && cmpp_TT_OpGlob==pNext->ttype ){
        /* Transform it to a cmpp_TT_OpNotGlob or cmpp_TT_OpGlob. */
        assert( pNext->z > arg->z + arg->n );
        arg->n = pNext->z + pNext->n - arg->z;
        arg->next = pNext->next;
        arg->ttype = bNeg
          ? cmpp_TT_OpNotGlob
          : pNext->ttype;
        ++pArgs->argc;
      }else if( pPrev ){
        if( bNeg ){
          ++pArgs->argc;
        }else{
          /* Snip this node out. */
          pPrev->next = pNext;
        }
      }else{
        assert( 0==pArgs->argc );
        ++pArgs->argc;
        if( !bNeg ){
          arg->ttype = cmpp_TT_Noop;
        }
      }
      /* Potential bug in waiting/fixme: by eliding all nots we are
      ** changing the behavior from forced coercion to bool to
      ** coercion to whatever the LHS wants. */
    }else{
      ++pArgs->argc;
    }
  }
  pArgs->arg0 = pArgs->argc ? &ali->list[0] : NULL;
  return ppCode;
}

CMPP__EXPORT(int, cmpp_dx_args_parse)(cmpp_dx *dx,
                                           cmpp_args *args){
  if( !dxppCode
      && 0==cmpp_args_parse(dx, args, dx->args.z, dx->args.nz,
                            cmpp_args_F_NO_PARENS)
      && (cmpp_d_F_NOT_SIMPLIFY & dx->d->flags) ){
    cmpp_args__not_simplify(dx->pp, args);
  }
  return dxppCode;
}

/* Helper for cmpp_kav_each() and friends. */
static
int cmpp__each_parse_args(cmpp_dx *dx,
                          cmpp_args *args,
                          unsigned char const *zBegin,
                          cmpp_ssize_t nz,
                          cmpp_flag32_t flags){
  if( 0==cmpp_args_parse(dx, args, zBegin, nz, cmpp_args_F_NO_PARENS) ){
    if( !args->argc
        && (cmpp_kav_each_F_NOT_EMPTY & flags) ){
      cmpp_err_set(dx->pp, CMPP_RC_RANGE,
                   "Empty list is not permitted here.");
    }
  }
  return dxppCode;
}

/* Helper for cmpp_kav_each() and friends. */
static
int cmpp__each_paren_expr(cmpp_dx *dx, cmpp_arg const * arg,
                          unsigned char * pOut, size_t nOut){
  cmpp_args sub = cmpp_args_empty;
  int rc = cmpp_args_parse(dx, &sub, arg->z, arg->n, 0);
  if( 0==rc ){
    int d = 0;
    rc = cmpp__args_evalToInt(dx, &sub, &d);
    if( 0==rc ){
      snprintf((char *)pOut, nOut, "%d", d);
    }
  }
  cmpp_args_cleanup(&sub);
  return rc;
}

CMPP__EXPORT(int, cmpp_kav_each)(cmpp_dx *dx,
                                 unsigned char const *zBegin,
                                 cmpp_ssize_t nIn,
                                 cmpp_kav_each_f callback,
                                 void *callbackState,
                                 cmpp_flag32_t flags){
  if( dxppCode ) return dxppCode;
  /* Reminder to self: we cannot reuse internal buffers here because a
     callback could recurse into this or otherwise use APIs which use
     those same buffers. */
  cmpp_b bKey = cmpp_b_empty;
  cmpp_b bVal = cmpp_b_empty;
  bool const reqArrow = 0==(cmpp_kav_each_F_NO_ARROW & flags);
  cmpp_args args = cmpp_args_empty;
  unsigned char exprBuf[32] = {0};
  cmpp_size_t const nz = cmpp__strlenu(zBegin,nIn);
  unsigned char const * const zEnd = zBegin + nz;
  cmpp_flag32_t a2bK = 0, a2bV = 0 /*cmpp_arg_to_b() flags*/;
  assert( zBegin );
  assert( zEnd );
  assert( zEnd>=zBegin );

  if( cmpp__each_parse_args(dx, &args, zBegin, nz,  flags) ){
    goto cleanup;
  }else if( reqArrow && 0!=args.argc%3 ){
    cmpp_err_set(dx->pp, CMPP_RC_RANGE,
                 "Expecting a list of 3 tokens per entry: "
                 "KEY -> VALUE");
  }else if( !reqArrow && 0!=args.argc%2 ){
    cmpp_err_set(dx->pp, CMPP_RC_RANGE,
                 "Expecting a list of 2 tokens per entry: "
                 "KEY VALUE");
  }
  if( cmpp_kav_each_F_CALL_KEY & flags ){
    a2bK |= cmpp_arg_to_b_F_BRACE_CALL;
    flags |= cmpp_kav_each_F_EXPAND_KEY;
  }
  if( cmpp_kav_each_F_CALL_VAL & flags ){
    a2bV |= cmpp_arg_to_b_F_BRACE_CALL;
    flags |= cmpp_kav_each_F_EXPAND_VAL;
  }
  cmpp_arg const * aNext = 0;
  for( cmpp_arg const * aKey = args.arg0;
       !dxppCode && aKey;
       aKey = aNext ){
    aNext = aKey->next;
    cmpp_arg const * aVal = aKey->next;
    if( !aVal ){
      dxserr("Expecting %s after key '%s'.",
             (reqArrow ? "->" : "a value"),
             aKey->z);
      break;
    }
    if( reqArrow ){
      if( cmpp_TT_ArrowR!=aVal->ttype ){
        dxserr("Expecting -> after key '%s'.", aKey->z);
        break;
      }
      aVal = aVal->next;
      if( !aVal ){
        dxserr("Expecting a value after '%s' ->.", aKey->z);
        break;
      }
    }
    //g_warn("\nkey=[%s]\nval=[%s]", aKey->z, aVal->z);
    /* Expand the key/value parts if needed... */
    unsigned char const *zKey;
    unsigned char const *zVal;
    cmpp_size_t nKey, nVal;
    if( cmpp_kav_each_F_EXPAND_KEY & flags ){
      if( cmpp_arg_to_b(dx, aKey, cmpp_b_reuse(&bKey),
                             a2bK) ){
        break;
      }
      zKey = bKey.z;
      nKey = bKey.n;
    }else{
      zKey = aKey->z;
      nKey = aKey->n;
    }
    if( cmpp_TT_GroupParen==aVal->ttype
        && (cmpp_kav_each_F_PARENS_EXPR & flags) ){
      if( cmpp__each_paren_expr(dx, aVal, &exprBuf[0],
                                sizeof(exprBuf)-1) ){
        break;
      }
      zVal = &exprBuf[0];
      nVal = cmpp__strlenu(zVal, -1);
    }else if( cmpp_kav_each_F_EXPAND_VAL & flags ){
      if( cmpp_arg_to_b(dx, aVal, cmpp_b_reuse(&bVal),
                             a2bV) ){
        break;
      }
      zVal = bVal.z;
      nVal = bVal.n;
    }else{
      zVal = aVal->z;
      nVal = aVal->n;
    }
    aNext = aVal->next;
    if( 0!=callback(dx, zKey, nKey, zVal, nVal, callbackState) ){
      break;
    }
  }
cleanup:
  cmpp_b_clear(&bKey);
  cmpp_b_clear(&bVal);
  cmpp_args_cleanup(&args);
  return dxppCode;
}

CMPP__EXPORT(int, cmpp_str_each)(cmpp_dx *dx,
                                 unsigned char const *zBegin,
                                 cmpp_ssize_t nIn,
                                 cmpp_kav_each_f callback, void *callbackState,
                                 cmpp_flag32_t flags){
  g_warn0("UNTESTED!");
  if( dxppCode ) return dxppCode;
  /* Reminder to self: we cannot reuse internal buffers here because a
     callback could recurse into this or otherwise use APIs which use
     those same buffers. */
  cmpp_b ob = cmpp_b_empty;
  cmpp_args args = cmpp_args_empty;
  unsigned char exprBuf[32] = {0};
  cmpp_size_t const nz = cmpp__strlenu(zBegin,nIn);
  unsigned char const * const zEnd = zBegin + nz;
  assert( zBegin );
  assert( zEnd );
  assert( zEnd>=zBegin );

  if( cmpp__each_parse_args(dx, &args, zBegin, nz,  flags) ){
    goto cleanup;
  }
  cmpp_arg const * aNext = 0;
  for( cmpp_arg const * arg = args.arg0;
       !dxppCode && arg;
       arg = aNext ){
    aNext = arg->next;
    //g_warn("\nkey=[%s]\nval=[%s]", arg->z, aVal->z);
    /* Expand the key/value parts if needed... */
    unsigned char const *zVal;
    cmpp_size_t nVal;
    if( cmpp_TT_GroupParen==arg->ttype
        && (cmpp_kav_each_F_PARENS_EXPR & flags) ){
      if( cmpp__each_paren_expr(dx, arg, &exprBuf[0],
                                sizeof(exprBuf)-1) ){
        break;
      }
      zVal = &exprBuf[0];
      nVal = cmpp__strlenu(zVal, -1);
    }else if( cmpp_kav_each_F_EXPAND_VAL & flags ){
      if( cmpp_arg_to_b(dx, arg, cmpp_b_reuse(&ob), 0) ){
        break;
      }
      zVal = ob.z;
      nVal = ob.n;
    }else{
      zVal = arg->z;
      nVal = arg->n;
    }
    if( 0!=callback(dx, arg->z, arg->n, zVal, nVal, callbackState) ){
      break;
    }
  }
cleanup:
  cmpp_b_clear(&ob);
  cmpp_args_cleanup(&args);
  return dxppCode;
}

/**
   Returns true if z _might_ be a cmpp_TT_StringAt, else false. It may have
   false positives but won't have false negatives.

   This is only intended to be used on NUL-terminated strings, not a
   pointer into a cmpp input source.
*/
static bool cmpp__might_be_atstring(unsigned char const *z){
  char const * const x = strchr((char const *)z, '@');
  return x && !!strchr(x+1, '@');
}

int cmpp__arg_expand_ats(cmpp_dx const * const dx,
                         cmpp_b * os,
                         cmpp_atpol_e atPolicy,
                         cmpp_arg const * const arg,
                         cmpp_tt thisTtype,
                         unsigned char const **pExp,
                         cmpp_size_t * nExp){
  assert( os );
  cmpp_b_reuse(os);
  if( 0==dxppCode
      && (cmpp_TT_AnyType==thisTtype || thisTtype==arg->ttype)
      && cmpp__might_be_atstring(arg->z)
      && 0==cmpp__StringAtIsOk(dx->pp, atPolicy) ){
#if 0
    if( !os->nAlloc ){
      cmpp_b_reserve3(os, 128);
    }
#endif
    cmpp_outputer oos = cmpp_outputer_b;
    oos.state = os;
    assert( !os->n );
    if( !cmpp_dx_out_expand(dx, &oos, arg->z, arg->n,
                            atPolicy ) ){
      *pExp = os->z;
      if( nExp ) *nExp = os->n;
      if( 0 ){
        g_warn("os->n=%u os->z=[%.*s]\n", os->n, (int)os->n,
               os->z);
      }

    }
  }else if( !dxppCode ){
    *pExp = arg->z;
    if( nExp ) *nExp = arg->n;
  }
  return dxppCode;
}

bool cmpp__arg_wordIsPathOrFlag(
  cmpp_arg const * const arg
){
  return cmpp_TT_Word==arg->ttype
    && ('-'==(char)arg->z[0]
        || strchr((char*)arg->z, '.')
        || strchr((char*)arg->z, '-')
        || strchr((char*)arg->z, '/')
        || strchr((char*)arg->z, '\\'));
}
/*
** 2022-11-12:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses the cmpp_popen() pieces.
*/
#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L /* for fdopen() in stdio.h */
#endif
#include <signal.h>

const cmpp_popen_t cmpp_popen_t_empty = cmpp_popen_t_empty_m;

#if CMPP_PLATFORM_IS_UNIX
#include <signal.h>
static int cmpp__err_errno(cmpp *pp, int errNo, char const *zContext){
  return cmpp_err_set(pp, cmpp_errno_rc(errNo, CMPP_RC_ERROR),
                      "errno #%d: %s", errNo, zContext);
}
#endif

/**
   Uses fork()/exec() to run a command in a separate process and open
   a two-way stream to it.

   If azCmd is NULL then zCmd must contain the command to run and
   any flags. It is passed as the 4th argument to
   execl("/bin/sh", "/bin/sh", "-c", zCmd, NULL).

   If azCmd is not NULL then it must be suitable for use as the 2nd
   argument to execv(2). execv(X, azCmd) is used in this case, where
   X is (zCmd ? zCmd : azCmd[0]).

   Flags:

   - cmpp_popen_F_DIRECT: if azCmd is NULL and flags has this bit set then
     zCmd is instead passed to execl(zCmd, zCmd, NULL). That can only
     work if zCmd is a single command without arguments.
     cmpp_popen_F_DIRECT has no effect if azCmd is not NULL.

   - cmpp_popen_F_PATH: tells it to use execlp() or execvp(), which
     performs path lookup of its initial argument.

   On success:

   - po->fdFromChild is the child's stdout. Read from it to read from
   the child.

   - If po->fpToChild is not NULL then *po->fpToChild is set to the
   child's stdin. Write to it to send the child stuff. Be sure to
   flush() and/or close() it to keep it from hanging forever. If
   po->fpToChild is NULL then the stdin of the child is closed.

   - po->childPid will be set to the PID of the child process.

   On error: you know the drill.

   After calling this, the caller is obligated to pass po to
   cmpp_pclose(). If the caller fcloses() *po->fpToChild then they
   must set it to NULL so that passing it to cmpp_pclose() knows not
   to close it.

   Bugs: because the command is run via /bin/sh -c ...  we cannot tell
   if it's actually found. All we can tell is that /bin/sh ran.

   Also: this doesn't capture stderr, so commands should redirect
   stderr to stdout. Adding the child's stderr handle to cmpp_popen_t is
   a potential TODO without a current use case.
*/
static
int cmpp__popen_impl(cmpp *pp, unsigned char const *zCmd,
                     char * const * azCmd, cmpp_flag32_t flags,
                     cmpp_popen_t *po){
#if !CMPP_PLATFORM_IS_UNIX
  return cmpp__err(pp, CMPP_RC_UNSUPPORTED,
                   "Piping is not supported in this build.");
#else
  if( ppCode ) return ppCode;
#define shut(P,N) close(P[N])
  /** Attribution: this impl is derived from one found in
      the Fossil SCM. */
  int pin[2];
  int pout[2];

  po->fdFromChild = -1;
  if( po->fpToChild ) *po->fpToChild = 0;
  if( pipe(pin)<0 ){
    return cmpp__err_errno(pp, errno, "pipe(in) failed");
  }
  if( pipe(pout)<0 ){
    int const rc = cmpp__err_errno(pp, errno,
                                   "pipe(out) failed");
    shut(pin,0);
    shut(pin,1);
    return rc;
  }
  po->childPid = fork();
  if( po->childPid<0 ){
    int const rc = cmpp__err_errno(pp, errno, "fork() failed");
    shut(pin,0);
    shut(pin,1);
    shut(pout,0);
    shut(pout,1);
    return rc;
  }
  signal(SIGPIPE,SIG_IGN);
  if( po->childPid==0 ){
    /* The child process. */
    int fd;
    close(0);
    fd = dup(pout[0]);
    if( fd!=0 ) {
      cmpp__fatal("Error opening file descriptor 0.");
    };
    shut(pout,0);
    shut(pout,1);
    close(1);
    fd = dup(pin[1]);
    if(fd!=1) {
      cmpp__fatal("Error opening file descriptor 1.");
    };
    shut(pin,0);
    shut(pin,1);
    if( azCmd ){
      if( pp->pimpl->flags.doDebug>1 ){
        for( int i = 0; azCmd[i]; ++i ){
          g_warn("execv arg[%d]=%s", i, azCmd[i]);
        }
      }
      int (*exc)(const char *, char *const []) =
        (cmpp_popen_F_PATH & flags) ? execvp : execv;
      exc(zCmd ? (char*)zCmd : azCmd[0], azCmd);
      cmpp__fatal("execv() failed");
    }else{
      g_debug(pp,2,("zCmd=%s\n", zCmd));
      int (*exc)(const char *, char const *, ...) =
        (cmpp_popen_F_PATH & flags) ? execlp : execl;
      if( cmpp_popen_F_DIRECT & flags ){
        exc((char*)zCmd, (char*)zCmd, (char*)0);
      }else{
        exc("/bin/sh", "/bin/sh", "-c", zCmd, (char*)0);
      }
      cmpp__fatal("execl() failed");
    }
    /* not reached */
  }else{
    /* The parent process. */
    //cmpp_outputer_flush(&pp->pimpl->out.ch);
    po->fdFromChild = pin[0];
    shut(pin,1);
    shut(pout,0);
    if( po->fpToChild ){
      *po->fpToChild = fdopen(pout[1], "w");
      if( !*po->fpToChild ){
        shut(pin,0);
        shut(pout,1);
        po->fdFromChild = -1;
        cmpp__err_errno(pp, errno,
                        "Error opening child process's stdin "
                        "FILE handle from its descriptor.");
      }
    }else{
      shut(pout,1);
    }
    return ppCode;
  }
#undef shut
#endif
}

int cmpp_popen(cmpp *pp, unsigned char const *zCmd,
                cmpp_flag32_t flags, cmpp_popen_t *po){
  return cmpp__popen_impl(pp, zCmd, NULL, flags, po);
}

int cmpp_popenv(cmpp *pp, char * const * azCmd,
                cmpp_flag32_t flags, cmpp_popen_t *po){
  return cmpp__popen_impl(pp, NULL, azCmd, flags, po);
}

int cmpp_popen_args(cmpp_dx *dx, cmpp_args const * args,
                    cmpp_popen_t *po){
#if !CMPP_PLATFORM_IS_UNIX
  return cmpp__popen_impl(dx->pp, NULL, 0, po) /* will fail */;
#else
  if( dxppCode ) return dxppCode;
  enum { MaxArgs = 128 };
  char * argv[MaxArgs] = {0};
  cmpp_size_t offsets[MaxArgs] = {0};
  cmpp_b osAll = cmpp_b_empty;
  cmpp_b os1 = cmpp_b_empty;
  if( args->argc >= MaxArgs ){
    return cmpp_dx_err_set(dx, CMPP_RC_RANGE,
                           "Too many arguments (%d). Max is %d.",
                           args->argc, (int)MaxArgs);
  }
  int i = 0;
  for(cmpp_arg const * a = args->arg0;
      a; ++i, a = a->next ){
    offsets[i] = osAll.n;
    cmpp_flag32_t a2bFlags = cmpp_arg_to_b_F_BRACE_CALL;
    if( cmpp__arg_wordIsPathOrFlag(a) ){
      a2bFlags |= cmpp_arg_to_b_F_FORCE_STRING;
    }
    if( cmpp_arg_to_b(dx, a, cmpp_b_reuse(&os1), a2bFlags)
        || cmpp_b_append4(dx->pp, &osAll, os1.z, os1.n+1/*NUL*/) ){
      goto end;
    }
    assert( osAll.n > offsets[i] );
    if( 0 ){
      g_warn("execv arg[%d] = %s => %s", i, a->z,
             osAll.z+offsets[i]);
    }
  }
  argv[i] = 0;
  for( --i; i >= 0; --i ){
    argv[i] = (char*)(osAll.z + offsets[i]);
    if( 0 ){
      g_warn("execv arg[%d] = %s", i, argv[i]);
    }
  }
end:
  if( 0==dxppCode ){
    cmpp__popen_impl(dx->pp, NULL, argv, 0, po);
  }
  cmpp_b_clear(&osAll);
  cmpp_b_clear(&os1);
  return dxppCode;
#endif
}

int cmpp_pclose(cmpp_popen_t *po){
#if CMPP_PLATFORM_IS_UNIX
  if( po->fdFromChild>=0 ) close(po->fdFromChild);
  if( po->fpToChild && *po->fpToChild ) fclose(*po->fpToChild);
  int const childPid = po->childPid;
  *po = cmpp_popen_t_empty;
#if 1
  int wp, rc = 0;
  if( childPid>0 ){
    //kill(childPid, SIGINT); // really needed?
    do{
      wp = waitpid(childPid, &rc, WNOHANG);
      if( wp>0 ){
        if( WIFEXITED(rc) ){
          rc = WEXITSTATUS(rc);
        }else if( WIFSIGNALED(rc) ){
          rc = WTERMSIG(rc);
        }else{
          rc = 0/*???*/;
        }
      }
    } while( wp>0 );
  }
  return rc;
#elif 0
  while( waitpid(childPid, NULL, WNOHANG)>0 ){}
#else
  if( childPid>0 ){
    kill(childPid, SIGINT); // really needed?
    waitpid((pid_t)childPid, NULL, WNOHANG);
  }else{
    while( waitpid( (pid_t)0, NULL, WNOHANG)>0 ){}
  }
#endif
#endif
}
/*
** 2022-11-12:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
** This file houses the module-loading pieces libcmpp.
*/

#if CMPP_ENABLE_DLLS
static const CmppSohList CmppSohList_empty =
  CmppSohList_empty_m;
#endif

#if CMPP_ENABLE_DLLS
/**
   If compiled without CMPP_ENABLE_DLLS defined to a true value
   then this function always returns CMPP_RC_UNSUPPORTED and updates
   the error state of its first argument with information about that
   code.

   Its first argument is the controlling cmpp. It can actually be
   NULL - it's only used for reporting error details.

   Its second argument is the name of a DLL file.

   Its third argument is the name of a symbol in the given DLL which
   resolves to a cmpp_module pointer. This name may be NULL,
   in which case a default symbol name of "cmpp_module1" is used
   (which is only useful when plugins are built one per DLL).

   The fourth argument is the output pointer to store the
   resulting module handle in.

   The fifth argument is an optional list to append the DLL's
   native handle to. It may be NULL.

   This function tries to open a DLL named fname using the system's
   DLL loader. If none is found, CMPP_RC_NOT_FOUND is returned and the
   cmpp's error state is populated with info about the error. If
   one is found, it looks for a symbol in the DLL: if symName is not
   NULL and is not empty then the symbol "cmpp_module_symName" is
   sought, else "cmpp_module". (e.g. if symName is "foo" then it
   searches for a symbol names "cmpp_module_foo".) If no such symbol is
   found then CMPP_RC_NOT_FOUND (again) is returned and the
   cmpp's error state is populated, else the symbol is assumed to
   be a (cmpp_module*) and *mod is assigned to it.

   All errors update pp's error state but all are recoverable.

   Returns 0 on success.

   On success:

  - `*mod` is set to the module object. Its ownship is kinda murky: it
    lives in memory made available via the module loader. It remains
    valid memory until the DLL is closed. The module might also
    actually be statically linked with the application, in which case
    it will live as long as the app.

  - If soli is not NULL then the native DLL handle is appended to it.
    Allocation errors when appending the DLL handle to the target list
    are ignored - failure to retain a DLL handle for closing later is
    not considered critical (and it would be extraordinarily rare (and
    closing them outside of late-/post-main() cleanup is ill-advised,
    anyway)).

   @see cmpp_module_load()
   @see CMPP_MODULE_DECL
   @see CMPP_MODULE_IMPL2
   @see CMPP_MODULE_IMPL3
   @see CMPP_MODULE_IMPL_SOLO
   @see CMPP_MODULE_REGISTER2
   @see CMPP_MODULE_REGISTER3
*/
static
int cmpp__module_extract(cmpp * pp,
                         char const * dllFileName,
                         char const * symName,
                         cmpp_module const ** mod);
#endif

#if CMPP_ENABLE_DLLS && !defined(CMPP_OMIT_D_MODULE)
#  define CMPP_D_MODULE 1
#else
#  define CMPP_D_MODULE 0
#endif

#if CMPP_D_MODULE
/**
   The #module directive:

   #module dll ?moduleName?

   Uses cmpp_module_load(dx, dll, moduleName||NULL) to try to load a
   directive module.
*/
//static
void cmpp_dx_f_module(cmpp_dx *dx) {
  cmpp_arg const * aName = 0;
  cmpp_b obDll = cmpp_b_empty;
  for( cmpp_arg const *arg = dx->args.arg0;
       arg; arg = arg->next ){
    //MARKER(("arg %s=%s\n", cmpp_tt_cstr(arg->ttype), arg->z));
    if( cmpp_dx_err_check(dx) ) goto end;
    else if( !obDll.z ){
      cmpp_arg_to_b(
        dx, arg, &obDll,
        0//cmpp_arg_to_b_F_NO_DEFINES
      );
      continue;
    }else if( !aName ){
      aName = arg;
      continue;
    }
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Unhandled argument: %s", arg->z);
    goto end;
  }
  if( !obDll.z ){
    cmpp_dx_err_set(dx, CMPP_RC_MISUSE,
                    "Expecting a DLL name argument.");
    goto end;
  }
  cmpp_module_load(dx->pp, (char const *)obDll.z,
                   aName ? (char const *)aName->z : NULL);
end:
  cmpp_b_clear(&obDll);
  return;
#if 0
  missing_arg:
  cmpp_dx_err_set(dx, CMPP_RC_MISUSE, "Expecting an argument after %s.",
                  arg->z);
  return;
#endif
}
#endif /* #module */

/**
   Module loader pedantic licensing note: Most of cmpp's
   module-loading code was copied verbatim from another project[^1],
   but was written by the same author who relicenses it in
   cmpp.

   [^1]: https://fossil.wanderinghorse.net/r/cwal
*/
#if CMPP_ENABLE_DLLS
#if CMPP_HAVE_DLOPEN
typedef void * cmpp_soh;
#  include <dlfcn.h> /* this actually has a different name on some platforms! */
#elif CMPP_HAVE_LTDLOPEN
#  include <ltdl.h>
typedef lt_dlhandle cmpp_soh;
#elif CMPP_ENABLE_DLLS
#  error "We have no dlopen() impl for this configuration."
#endif

static cmpp_soh cmpp__dlopen(char const * fname,
                             char const **errMsg){
  static int once  = 0;
  cmpp_soh soh = 0;
  if(!once && ++once){
#if CMPP_HAVE_DLOPEN
    dlopen( 0, RTLD_NOW | RTLD_GLOBAL );
#elif CMPP_HAVE_LTDLOPEN
    lt_dlinit();
    lt_dlopen( 0 );
#endif
  }
#if CMPP_HAVE_DLOPEN
  soh = dlopen(fname, RTLD_NOW | RTLD_GLOBAL);
#elif CMPP_HAVE_LTDLOPEN
  soh = lt_dlopen(fname);
#endif
  if(!soh && errMsg){
#if CMPP_HAVE_DLOPEN
    *errMsg = dlerror();
#elif CMPP_HAVE_LTDLOPEN
    *errMsg = lt_dlerror();
#endif
  }
  return soh;
}

static
cmpp_module const * cmpp__dlsym(cmpp_soh soh,
                                         char const * mname){
  cmpp_module const ** sym =
#if CMPP_HAVE_DLOPEN
    dlsym(soh, mname)
#elif CMPP_HAVE_LTDLOPEN
    lt_dlsym(soh, mname)
#else
    NULL
#endif
    ;
  return sym ? *sym : NULL;
}

static void cmpp__dlclose(cmpp_soh soh){
  if( soh ) {
#if CMPP_CLOSE_DLLS
    /* MARKER(("Closing loaded module @%p.\n", (void const *)soh)); */
#if CMPP_HAVE_DLOPEN
    dlclose(soh);
#elif CMPP_HAVE_LTDLOPEN
    lt_dlclose(soh);
#endif
#endif
  }
}
#endif /* CMPP_ENABLE_DLLS */

#define CmppSohList_works (CMPP_ENABLE_DLLS && CMPP_CLOSE_DLLS)

int CmppSohList_append(cmpp *pp, CmppSohList *soli, void *soh){
#if CmppSohList_works
  int const rc = cmpp_array_reserve(pp, (void**)&soli->list,
                                    soli->n
                                    ? (soli->n==soli->nAlloc
                                       ? soli->nAlloc*2
                                       : soli->n+1)
                                    : 8,
                                    &soli->nAlloc, sizeof(void*));
  if( 0==rc ){
    soli->list[soli->n++] = soh;
  }
  return rc;
#else
  (void)pp; (void)soli; (void)soh;
  return 0;
#endif
}

void CmppSohList_close(CmppSohList *s){
#if CmppSohList_works
  while( s->nAlloc ){
    if( s->list[--s->nAlloc] ){
      //MARKER(("closing soh %p\n", s->list[s->nAlloc]));
      cmpp__dlclose(s->list[s->nAlloc]);
      s->list[s->nAlloc] = 0;
    }
  }
  cmpp_mfree(s->list);
  *s = CmppSohList_empty;
#else
  (void)s;
#endif
}

#if 0
/**
   Passes soli to CmppSohList_close() then frees soli. Results are
   undefined if soli is not NULL but was not returned from
   CmppSohList_new().

   Special case: if built without DLL-closing support, this is a no-op.
*/
//static void CmppSohList_free(CmppSohList *soli);
void CmppSohList_free(CmppSohList *s){
  if( s ){
#if CmppSohList_works
    CmppSohList_close(s);
    cmpp_mfree(s);
#endif
  }
}

/**
   Returns a new, cleanly-initialized CmppSohList or NULL
   on allocation error. The returned instance must eventually be
   passed to CmppSohList_free().

   Special case: if built without DLL-closing support, this returns a
   no-op singleton instance.
*/
//static CmppSohList * CmppSohList_new(void);
CmppSohList * CmppSohList_new(void){
#if CmppSohList_works
  CmppSohList * s = cmpp_malloc(sizeof(*s));
  if( s ) *s = CmppSohList_empty;
  return s;
#else
  static CmppSohList soli = CmppSohList_empty;
  return &soli;
#endif
}
#endif

#undef CmppSohList_works

#if CMPP_ENABLE_DLLS
/**
   Default entry point symbol name for loadable modules.  This must
   match the symbolic name defined by CMPP_MODULE_IMPL_SOLO().
*/
static char const * const cmppModDfltSym = "cmpp_module1";

/**
   Looks for a symbol in the given DLL handle. If symName is NULL or
   empty, the symbol "cmpp_module" is used, else the symbols
   ("cmpp_module__" + symName) is used. If it finds one, it casts it to
   cmpp_module and returns it. On error it may update pp's
   error state with the error information if pp is not NULL.

   Errors:

   - symName is too long.

   - cmpp__dlsym() lookup failure.
*/
static cmpp_module const *
cmpp__module_fish_out_entry_pt(cmpp * pp,
                               cmpp_soh soh,
                               char const * symName){
  enum { MaxLen = 128 };
  char buf[MaxLen] = {0};
  cmpp_size_t const slen = symName ? strlen(symName) : 0;
  cmpp_module const * mod = 0;
  if(slen > (MaxLen-20)){
    cmpp_err_set(pp, CMPP_RC_RANGE,
                 "DLL symbol name '%.*s' is too long. Max is %d.",
                 (int)slen, symName, (int)MaxLen-20);
  }else{
    if(symName && *symName){
      snprintf(buf, MaxLen,"cmpp_module__%s", symName);
      symName = &buf[0];
    }else{
      symName = cmppModDfltSym;
    }
    mod = cmpp__dlsym(soh, symName);
  }
  /*MARKER(("%s() [%s] ==> %p\n",__func__, symName,
    (void const *)mod));*/
  return mod;
}
#endif/*CMPP_ENABLE_DLLS*/

#if CMPP_ENABLE_DLLS
/**
   Tries to dlsym() the given cmpp_module symbol from the given
   DLL handle. On success, 0 is returned and *mod is assigned to the
   memory. On error, non-0 is returned and pp's error state may be
   updated.

   Ownership of the returned module ostensibly lies with the first
   argument, but that's not entirely true. If CMPP_CLOSE_DLLS is true
   then a copy of the module's pointer is stored in the engine for
   later closing. The memory itself is owned by the module loader, and
   "should" stay valid until the DLL is closed.
*/
static int cmpp__module_get_sym(cmpp * pp,
                                cmpp_soh soh,
                                char const * symName,
                                cmpp_module const ** mod){

  cmpp_module const * lm = 0;
  int rc = cmpp_err_has(pp);
  if( 0==rc ){
    lm = cmpp__module_fish_out_entry_pt(pp, soh, symName);
    rc = cmpp_err_has(pp);
  }
  if(0==rc){
    if(lm){
      *mod = lm;
    }else{
      cmpp__dlclose(soh);
      rc = cmpp_err_set(pp, CMPP_RC_NOT_FOUND,
                        "Did not find module entry point symbol '%s'.",
                        symName ? symName : cmppModDfltSym);
    }
  }
  return rc;
}
#endif/*CMPP_ENABLE_DLLS*/

#if !CMPP_ENABLE_DLLS
static int cmpp__err_no_dlls(cmpp * const pp){
  return cmpp_err_set(pp, CMPP_RC_UNSUPPORTED,
                      "No dlopen() equivalent is installed "
                      "for this build configuration.");
}
#endif

#if CMPP_ENABLE_DLLS
//no: CMPP_WASM_EXPORT
int cmpp__module_extract(cmpp * pp,
                         char const * fname,
                         char const * symName,
                         cmpp_module const ** mod){
  int rc = cmpp_err_has(pp);
  if( rc ) return rc;
  else if( cmpp_is_safemode(pp) ){
    return cmpp_err_set(pp, CMPP_RC_ACCESS,
                        "Cannot use DLLs in safe mode.");
  }else{
    cmpp_soh soh;
    char const * errMsg = 0;
    soh = cmpp__dlopen(fname, &errMsg);
    if(soh){
      if( pp ){
        CmppSohList_append(NULL/*alloc error here can be ignored*/,
                           &pp->pimpl->mod.sohList, soh);
      }
      cmpp_module const * x = 0;
      rc = cmpp__module_get_sym(pp, soh, symName, &x);
      if(!rc && mod) *mod = x;
      return rc;
    }else{
      return errMsg
        ? cmpp_err_set(pp, CMPP_RC_ERROR, "DLL open failed: %s",
                       errMsg)
        : cmpp_err_set(pp, CMPP_RC_ERROR,
                       "DLL open failed for unknown reason.");
    }
  }
}
#endif

//no: CMPP_WASM_EXPORT
int cmpp_module_load(cmpp * pp, char const * fname,
                     char const * symName){
#if CMPP_ENABLE_DLLS
  if( ppCode ){
    /* fall through */
  }else if( cmpp_ctor_F_SAFEMODE & pp->pimpl->flags.newFlags ){
    cmpp_err_set(pp, CMPP_RC_ACCESS,
                        "%s() is disallowed in safe-mode.");
  }else{
    cmpp__pi(pp);
    char * zName = 0;
    if( fname ){
      zName = cmpp_path_search(pp, (char const *)pi->mod.path.z,
                               pi->mod.pathSep, fname,
                               pi->mod.soExt);
      if( !zName ){
        return cmpp_err_set(pp, CMPP_RC_NOT_FOUND,
                            "Did not find [%s] or [%s%s] "
                            "in search path [%s].",
                            fname, fname, pi->mod.soExt,
                            pi->mod.path.z);
      }
    }
    cmpp_module const * mod = 0;
    if( 0==cmpp__module_extract(pp, zName, symName, &mod) ){
      assert(mod);
      assert(mod->init);
      int const rc = mod->init(pp);
      if( rc && !ppCode ){
        cmpp_err_set(pp, CMPP_RC_ERROR,
                     "Module %s::init() failed with code #%d/%s "
                     "without providing additional info.",
                     symName ? symName : "cmpp_module",
                     rc, cmpp_rc_cstr(rc));
      }
      cmpp_mfree(zName);
    }
  }
  return ppCode;
#else
  (void)fname; (void)symName;
  return cmpp__err_no_dlls(pp);
#endif
}
/*
** 2015-08-18, 2023-04-28
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file demonstrates how to create a table-valued-function using
** a virtual table.  This demo implements the generate_series() function
** which gives the same results as the eponymous function in PostgreSQL,
** within the limitation that its arguments are signed 64-bit integers.
**
** Considering its equivalents to generate_series(start,stop,step): A
** value V[n] sequence is produced for integer n ascending from 0 where
**  ( V[n] == start + n * step  &&  sgn(V[n] - stop) * sgn(step) >= 0 )
** for each produced value (independent of production time ordering.)
**
** All parameters must be either integer or convertable to integer.
** The start parameter is required.
** The stop parameter defaults to (1<<32)-1 (aka 4294967295 or 0xffffffff)
** The step parameter defaults to 1 and 0 is treated as 1.
**
** Examples:
**
**      SELECT * FROM generate_series(0,100,5);
**
** The query above returns integers from 0 through 100 counting by steps
** of 5.  In other words, 0, 5, 10, 15, ..., 90, 95, 100.  There are a total
** of 21 rows.
**
**      SELECT * FROM generate_series(0,100);
**
** Integers from 0 through 100 with a step size of 1.  101 rows.
**
**      SELECT * FROM generate_series(20) LIMIT 10;
**
** Integers 20 through 29.  10 rows.
**
**      SELECT * FROM generate_series(0,-100,-5);
**
** Integers 0 -5 -10 ... -100.  21 rows.
**
**      SELECT * FROM generate_series(0,-1);
**
** Empty sequence.
**
** HOW IT WORKS
**
** The generate_series "function" is really a virtual table with the
** following schema:
**
**     CREATE TABLE generate_series(
**       value,
**       start HIDDEN,
**       stop HIDDEN,
**       step HIDDEN
**     );
**
** The virtual table also has a rowid which is an alias for the value.
**
** Function arguments in queries against this virtual table are translated
** into equality constraints against successive hidden columns.  In other
** words, the following pairs of queries are equivalent to each other:
**
**    SELECT * FROM generate_series(0,100,5);
**    SELECT * FROM generate_series WHERE start=0 AND stop=100 AND step=5;
**
**    SELECT * FROM generate_series(0,100);
**    SELECT * FROM generate_series WHERE start=0 AND stop=100;
**
**    SELECT * FROM generate_series(20) LIMIT 10;
**    SELECT * FROM generate_series WHERE start=20 LIMIT 10;
**
** The generate_series virtual table implementation leaves the xCreate method
** set to NULL.  This means that it is not possible to do a CREATE VIRTUAL
** TABLE command with "generate_series" as the USING argument.  Instead, there
** is a single generate_series virtual table that is always available without
** having to be created first.
**
** The xBestIndex method looks for equality constraints against the hidden
** start, stop, and step columns, and if present, it uses those constraints
** to bound the sequence of generated values.  If the equality constraints
** are missing, it uses 0 for start, 4294967295 for stop, and 1 for step.
** xBestIndex returns a small cost when both start and stop are available,
** and a very large cost if either start or stop are unavailable.  This
** encourages the query planner to order joins such that the bounds of the
** series are well-defined.
**
** Update on 2024-08-22:
** xBestIndex now also looks for equality and inequality constraints against
** the value column and uses those constraints as additional bounds against
** the sequence range.  Thus, a query like this:
**
**     SELECT value FROM generate_series($SA,$EA)
**      WHERE value BETWEEN $SB AND $EB;
**
** Is logically the same as:
**
**     SELECT value FROM generate_series(max($SA,$SB),min($EA,$EB));
**
** Constraints on the value column can server as substitutes for constraints
** on the hidden start and stop columns.  So, the following two queries
** are equivalent:
**
**     SELECT value FROM generate_series($S,$E);
**     SELECT value FROM generate_series WHERE value BETWEEN $S and $E;
**
*/
#if 0
#include "sqlite3ext.h"
#else
#include "sqlite3.h"
#endif
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#ifndef SQLITE_OMIT_VIRTUALTABLE

/* series_cursor is a subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result.
**
** iOBase, iOTerm, and iOStep are the original values of the
** start=, stop=, and step= constraints on the query.  These are
** the values reported by the start, stop, and step columns of the
** virtual table.
**
** iBase, iTerm, iStep, and bDescp are the actual values used to generate
** the sequence.  These might be different from the iOxxxx values.
** For example in
**
**   SELECT value FROM generate_series(1,11,2)
**    WHERE value BETWEEN 4 AND 8;
**
** The iOBase is 1, but the iBase is 5.  iOTerm is 11 but iTerm is 7.
** Another example:
**
**   SELECT value FROM generate_series(1,15,3) ORDER BY value DESC;
**
** The cursor initialization for the above query is:
**
**   iOBase = 1        iBase = 13
**   iOTerm = 15       iTerm = 1
**   iOStep = 3        iStep = 3      bDesc = 1
**
** The actual step size is unsigned so that can have a value of
** +9223372036854775808 which is needed for querys like this:
**
**   SELECT value
**     FROM generate_series(9223372036854775807,
**                          -9223372036854775808,
**                          -9223372036854775808)
**    ORDER BY value ASC;
**
** The setup for the previous query will be:
**
**   iOBase = 9223372036854775807    iBase = -1
**   iOTerm = -9223372036854775808   iTerm = 9223372036854775807
**   iOStep = -9223372036854775808   iStep = 9223372036854775808  bDesc = 0
*/
typedef unsigned char u8;
typedef struct series_cursor series_cursor;
struct series_cursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  sqlite3_int64 iOBase;      /* Original starting value ("start") */
  sqlite3_int64 iOTerm;      /* Original terminal value ("stop") */
  sqlite3_int64 iOStep;      /* Original step value */
  sqlite3_int64 iBase;       /* Starting value to actually use */
  sqlite3_int64 iTerm;       /* Terminal value to actually use */
  sqlite3_uint64 iStep;      /* The step size */
  sqlite3_int64 iValue;      /* Current value */
  u8 bDesc;                  /* iStep is really negative */
  u8 bDone;                  /* True if stepped past last element */
};

/*
** Computed the difference between two 64-bit signed integers using a
** convoluted computation designed to work around the silly restriction
** against signed integer overflow in C.
*/
static sqlite3_uint64 span64(sqlite3_int64 a, sqlite3_int64 b){
  assert( a>=b );
  return (*(sqlite3_uint64*)&a) - (*(sqlite3_uint64*)&b);
}  

/*
** Add or substract an unsigned 64-bit integer from a signed 64-bit integer
** and return the new signed 64-bit integer.
*/
static sqlite3_int64 add64(sqlite3_int64 a, sqlite3_uint64 b){
  sqlite3_uint64 x = *(sqlite3_uint64*)&a;
  x += b;
  return *(sqlite3_int64*)&x;
}
static sqlite3_int64 sub64(sqlite3_int64 a, sqlite3_uint64 b){
  sqlite3_uint64 x = *(sqlite3_uint64*)&a;
  x -= b;
  return *(sqlite3_int64*)&x;
}

/*
** The seriesConnect() method is invoked to create a new
** series_vtab that describes the generate_series virtual table.
**
** Think of this routine as the constructor for series_vtab objects.
**
** All this routine needs to do is:
**
**    (1) Allocate the series_vtab object and initialize all fields.
**
**    (2) Tell SQLite (via the sqlite3_declare_vtab() interface) what the
**        result set of queries against generate_series will look like.
*/
static int seriesConnect(
  sqlite3 *db,
  void *pUnused,
  int argcUnused, const char *const*argvUnused,
  sqlite3_vtab **ppVtab,
  char **pzErrUnused
){
  sqlite3_vtab *pNew;
  int rc;

/* Column numbers */
#define SERIES_COLUMN_ROWID (-1)
#define SERIES_COLUMN_VALUE 0
#define SERIES_COLUMN_START 1
#define SERIES_COLUMN_STOP  2
#define SERIES_COLUMN_STEP  3

  (void)pUnused;
  (void)argcUnused;
  (void)argvUnused;
  (void)pzErrUnused;
  rc = sqlite3_declare_vtab(db,
     "CREATE TABLE x(value,start hidden,stop hidden,step hidden)");
  if( rc==SQLITE_OK ){
    pNew = *ppVtab = sqlite3_malloc( sizeof(*pNew) );
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    sqlite3_vtab_config(db, SQLITE_VTAB_INNOCUOUS);
  }
  return rc;
}

/*
** This method is the destructor for series_cursor objects.
*/
static int seriesDisconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

/*
** Constructor for a new series_cursor object.
*/
static int seriesOpen(sqlite3_vtab *pUnused, sqlite3_vtab_cursor **ppCursor){
  series_cursor *pCur;
  (void)pUnused;
  pCur = sqlite3_malloc( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

/*
** Destructor for a series_cursor.
*/
static int seriesClose(sqlite3_vtab_cursor *cur){
  sqlite3_free(cur);
  return SQLITE_OK;
}


/*
** Advance a series_cursor to its next row of output.
*/
static int seriesNext(sqlite3_vtab_cursor *cur){
  series_cursor *pCur = (series_cursor*)cur;
  if( pCur->iValue==pCur->iTerm ){
    pCur->bDone = 1;
  }else if( pCur->bDesc ){
    pCur->iValue = sub64(pCur->iValue, pCur->iStep);
    assert( pCur->iValue>=pCur->iTerm );
  }else{
    pCur->iValue = add64(pCur->iValue, pCur->iStep);
    assert( pCur->iValue<=pCur->iTerm );
  }
  return SQLITE_OK;
}

/*
** Return values of columns for the row at which the series_cursor
** is currently pointing.
*/
static int seriesColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  series_cursor *pCur = (series_cursor*)cur;
  sqlite3_int64 x = 0;
  switch( i ){
    case SERIES_COLUMN_START:  x = pCur->iOBase;     break;
    case SERIES_COLUMN_STOP:   x = pCur->iOTerm;     break;
    case SERIES_COLUMN_STEP:   x = pCur->iOStep;     break;
    default:                   x = pCur->iValue;     break;
  }
  sqlite3_result_int64(ctx, x);
  return SQLITE_OK;
}

#ifndef LARGEST_UINT64
#define LARGEST_INT64  ((sqlite3_int64)0x7fffffffffffffffLL)
#define LARGEST_UINT64 ((sqlite3_uint64)0xffffffffffffffffULL)
#define SMALLEST_INT64 ((sqlite3_int64)0x8000000000000000LL)
#endif

/*
** The rowid is the same as the value.
*/
static int seriesRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  series_cursor *pCur = (series_cursor*)cur;
  *pRowid = pCur->iValue;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int seriesEof(sqlite3_vtab_cursor *cur){
  series_cursor *pCur = (series_cursor*)cur;
  return pCur->bDone;
}

/* True to cause run-time checking of the start=, stop=, and/or step=
** parameters.  The only reason to do this is for testing the
** constraint checking logic for virtual tables in the SQLite core.
*/
#ifndef SQLITE_SERIES_CONSTRAINT_VERIFY
# define SQLITE_SERIES_CONSTRAINT_VERIFY 0
#endif

/*
** Return the number of steps between pCur->iBase and pCur->iTerm if
** the step width is pCur->iStep.
*/
static sqlite3_uint64 seriesSteps(series_cursor *pCur){
  if( pCur->bDesc ){
    assert( pCur->iBase >= pCur->iTerm );
    return span64(pCur->iBase, pCur->iTerm)/pCur->iStep;
  }else{
    assert( pCur->iBase <= pCur->iTerm );
    return span64(pCur->iTerm, pCur->iBase)/pCur->iStep;
  }
}

#if defined(SQLITE_ENABLE_MATH_FUNCTIONS) || defined(_WIN32)
/*
** Case 1 (the most common case):
** The standard math library is available so use ceil() and floor() from there.
*/
static double seriesCeil(double r){ return ceil(r); }
static double seriesFloor(double r){ return floor(r); }
#elif defined(__GNUC__) && !defined(SQLITE_DISABLE_INTRINSIC)
/*
** Case 2 (2nd most common): Use GCC/Clang builtins
*/
static double seriesCeil(double r){ return __builtin_ceil(r); }
static double seriesFloor(double r){ return __builtin_floor(r); }
#else
/*
** Case 3 (rarely happens): Use home-grown ceil() and floor() routines.
*/
static double seriesCeil(double r){
  sqlite3_int64 x;
  if( r!=r ) return r;
  if( r<=(-4503599627370496.0) ) return r;
  if( r>=(+4503599627370496.0) ) return r;
  x = (sqlite3_int64)r;
  if( r==(double)x ) return r;
  if( r>(double)x ) x++;
  return (double)x;
}
static double seriesFloor(double r){
  sqlite3_int64 x;
  if( r!=r ) return r;
  if( r<=(-4503599627370496.0) ) return r;
  if( r>=(+4503599627370496.0) ) return r;
  x = (sqlite3_int64)r;
  if( r==(double)x ) return r;
  if( r<(double)x ) x--;
  return (double)x;
}
#endif

/*
** This method is called to "rewind" the series_cursor object back
** to the first row of output.  This method is always called at least
** once prior to any call to seriesColumn() or seriesRowid() or
** seriesEof().
**
** The query plan selected by seriesBestIndex is passed in the idxNum
** parameter.  (idxStr is not used in this implementation.)  idxNum
** is a bitmask showing which constraints are available:
**
**   0x0001:    start=VALUE
**   0x0002:    stop=VALUE
**   0x0004:    step=VALUE
**   0x0008:    descending order
**   0x0010:    ascending order
**   0x0020:    LIMIT  VALUE
**   0x0040:    OFFSET  VALUE
**   0x0080:    value=VALUE
**   0x0100:    value>=VALUE
**   0x0200:    value>VALUE
**   0x1000:    value<=VALUE
**   0x2000:    value<VALUE
**
** This routine should initialize the cursor and position it so that it
** is pointing at the first row, or pointing off the end of the table
** (so that seriesEof() will return true) if the table is empty.
*/
static int seriesFilter(
  sqlite3_vtab_cursor *pVtabCursor,
  int idxNum, const char *idxStrUnused,
  int argc, sqlite3_value **argv
){
  series_cursor *pCur = (series_cursor *)pVtabCursor;
  int iArg = 0;                         /* Arguments used so far */
  int i;                                /* Loop counter */
  sqlite3_int64 iMin = SMALLEST_INT64;  /* Smallest allowed output value */
  sqlite3_int64 iMax = LARGEST_INT64;   /* Largest allowed output value */
  sqlite3_int64 iLimit = 0;             /* if >0, the value of the LIMIT */
  sqlite3_int64 iOffset = 0;            /* if >0, the value of the OFFSET */

  (void)idxStrUnused;

  /* If any constraints have a NULL value, then return no rows.
  ** See ticket https://sqlite.org/src/info/fac496b61722daf2
  */
  for(i=0; i<argc; i++){
    if( sqlite3_value_type(argv[i])==SQLITE_NULL ){
      goto series_no_rows;
    }
  }

  /* Capture the three HIDDEN parameters to the virtual table and insert
  ** default values for any parameters that are omitted.
  */
  if( idxNum & 0x01 ){
    pCur->iOBase = sqlite3_value_int64(argv[iArg++]);
  }else{
    pCur->iOBase = 0;
  }
  if( idxNum & 0x02 ){
    pCur->iOTerm = sqlite3_value_int64(argv[iArg++]);
  }else{
    pCur->iOTerm = 0xffffffff;
  }
  if( idxNum & 0x04 ){
    pCur->iOStep = sqlite3_value_int64(argv[iArg++]);
    if( pCur->iOStep==0 ) pCur->iOStep = 1;
  }else{
    pCur->iOStep = 1;
  }

  /* If there are constraints on the value column but there are
  ** no constraints on  the start, stop, and step columns, then
  ** initialize the default range to be the entire range of 64-bit signed
  ** integers.  This range will contracted by the value column constraints
  ** further below.
  */
  if( (idxNum & 0x05)==0 && (idxNum & 0x0380)!=0 ){
    pCur->iOBase = SMALLEST_INT64;
  }
  if( (idxNum & 0x06)==0 && (idxNum & 0x3080)!=0 ){
    pCur->iOTerm = LARGEST_INT64;
  }
  pCur->iBase = pCur->iOBase;
  pCur->iTerm = pCur->iOTerm;
  if( pCur->iOStep>0 ){  
    pCur->iStep = pCur->iOStep;
  }else if( pCur->iOStep>SMALLEST_INT64 ){
    pCur->iStep = -pCur->iOStep;
  }else{
    pCur->iStep = LARGEST_INT64;
    pCur->iStep++;
  }
  pCur->bDesc = pCur->iOStep<0;
  if( pCur->bDesc==0 && pCur->iBase>pCur->iTerm ){
    goto series_no_rows;
  }
  if( pCur->bDesc!=0 && pCur->iBase<pCur->iTerm ){
    goto series_no_rows;
  }

  /* Extract the LIMIT and OFFSET values, but do not apply them yet.
  ** The range must first be constrained by the limits on value.
  */
  if( idxNum & 0x20 ){
    iLimit = sqlite3_value_int64(argv[iArg++]);
    if( idxNum & 0x40 ){
      iOffset = sqlite3_value_int64(argv[iArg++]);
    }
  }

  /* Narrow the range of iMin and iMax (the minimum and maximum outputs)
  ** based on equality and inequality constraints on the "value" column.
  */
  if( idxNum & 0x3380 ){
    if( idxNum & 0x0080 ){    /* value=X */
      if( sqlite3_value_numeric_type(argv[iArg])==SQLITE_FLOAT ){
        double r = sqlite3_value_double(argv[iArg++]);
        if( r==seriesCeil(r)
         && r>=(double)SMALLEST_INT64
         && r<=(double)LARGEST_INT64
        ){
          iMin = iMax = (sqlite3_int64)r;
        }else{
          goto series_no_rows;
        }
      }else{
        iMin = iMax = sqlite3_value_int64(argv[iArg++]);
      }
    }else{
      if( idxNum & 0x0300 ){  /* value>X or value>=X */
        if( sqlite3_value_numeric_type(argv[iArg])==SQLITE_FLOAT ){
          double r = sqlite3_value_double(argv[iArg++]);
          if( r<(double)SMALLEST_INT64 ){
            iMin = SMALLEST_INT64;
          }else if( (idxNum & 0x0200)!=0 && r==seriesCeil(r) ){
            iMin = (sqlite3_int64)seriesCeil(r+1.0);
          }else{
            iMin = (sqlite3_int64)seriesCeil(r);
          }
        }else{
          iMin = sqlite3_value_int64(argv[iArg++]);
          if( (idxNum & 0x0200)!=0 ){
            if( iMin==LARGEST_INT64 ){
              goto series_no_rows;
            }else{
              iMin++;
            }
          }
        }
      }
      if( idxNum & 0x3000 ){   /* value<X or value<=X */
        if( sqlite3_value_numeric_type(argv[iArg])==SQLITE_FLOAT ){
          double r = sqlite3_value_double(argv[iArg++]);
          if( r>(double)LARGEST_INT64 ){
            iMax = LARGEST_INT64;
          }else if( (idxNum & 0x2000)!=0 && r==seriesFloor(r) ){
            iMax = (sqlite3_int64)(r-1.0);
          }else{
            iMax = (sqlite3_int64)seriesFloor(r);
          }
        }else{
          iMax = sqlite3_value_int64(argv[iArg++]);
          if( idxNum & 0x2000 ){
            if( iMax==SMALLEST_INT64 ){
              goto series_no_rows;
            }else{
              iMax--;
            }
          }
        }
      }
      if( iMin>iMax ){
        goto series_no_rows;
      }
    }

    /* Try to reduce the range of values to be generated based on
    ** constraints on the "value" column.
    */
    if( pCur->bDesc==0 ){
      if( pCur->iBase<iMin ){
        sqlite3_uint64 span = span64(iMin,pCur->iBase);
        pCur->iBase = add64(pCur->iBase, (span/pCur->iStep)*pCur->iStep);
        if( pCur->iBase<iMin ){
          if( pCur->iBase > sub64(LARGEST_INT64, pCur->iStep) ){
            goto series_no_rows;
          }
          pCur->iBase = add64(pCur->iBase, pCur->iStep);
        }
      }
      if( pCur->iTerm>iMax ){
        pCur->iTerm = iMax;
      }
    }else{
      if( pCur->iBase>iMax ){
        sqlite3_uint64 span = span64(pCur->iBase,iMax);
        pCur->iBase = sub64(pCur->iBase, (span/pCur->iStep)*pCur->iStep);
        if( pCur->iBase>iMax ){
          if( pCur->iBase < add64(SMALLEST_INT64, pCur->iStep) ){
            goto series_no_rows;
          }
          pCur->iBase = sub64(pCur->iBase, pCur->iStep);
        }
      }
      if( pCur->iTerm<iMin ){
        pCur->iTerm = iMin;
      }
    }
  }

  /* Adjust iTerm so that it is exactly the last value of the series.
  */
  if( pCur->bDesc==0 ){
    if( pCur->iBase>pCur->iTerm ){
      goto series_no_rows;
    }
    pCur->iTerm = sub64(pCur->iTerm,
                        span64(pCur->iTerm,pCur->iBase) % pCur->iStep);
  }else{
    if( pCur->iBase<pCur->iTerm ){
      goto series_no_rows;
    }
    pCur->iTerm = add64(pCur->iTerm,
                        span64(pCur->iBase,pCur->iTerm) % pCur->iStep);
  }

  /* Transform the series generator to output values in the requested
  ** order.
  */
  if( ((idxNum & 0x0008)!=0 && pCur->bDesc==0)
   || ((idxNum & 0x0010)!=0 && pCur->bDesc!=0)
  ){
    sqlite3_int64 tmp = pCur->iBase;
    pCur->iBase = pCur->iTerm;
    pCur->iTerm = tmp;
    pCur->bDesc = !pCur->bDesc;
  }

  /* Apply LIMIT and OFFSET constraints, if any */
  assert( pCur->iStep!=0 );
  if( idxNum & 0x20 ){
    if( iOffset>0 ){
      if( seriesSteps(pCur) < (sqlite3_uint64)iOffset ){
        goto series_no_rows;
      }else if( pCur->bDesc ){
        pCur->iBase = sub64(pCur->iBase, pCur->iStep*iOffset);
      }else{
        pCur->iBase = add64(pCur->iBase, pCur->iStep*iOffset);
      }
    }
    if( iLimit>=0 && seriesSteps(pCur) > (sqlite3_uint64)iLimit ){
      pCur->iTerm = add64(pCur->iBase, (iLimit - 1)*pCur->iStep);
    }
  }
  pCur->iValue = pCur->iBase;
  pCur->bDone = 0;
  return SQLITE_OK;

series_no_rows:
  pCur->iBase = 0;
  pCur->iTerm = 0;
  pCur->iStep = 1;
  pCur->bDesc = 0;
  pCur->bDone = 1;
  return SQLITE_OK;  
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the generate_series virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
**
** In this implementation idxNum is used to represent the
** query plan.  idxStr is unused.
**
** The query plan is represented by bits in idxNum:
**
**   0x0001  start = $num
**   0x0002  stop = $num
**   0x0004  step = $num
**   0x0008  output is in descending order
**   0x0010  output is in ascending order
**   0x0020  LIMIT $num
**   0x0040  OFFSET $num
**   0x0080  value = $num
**   0x0100  value >= $num
**   0x0200  value > $num
**   0x1000  value <= $num
**   0x2000  value < $num
**
** Only one of 0x0100 or 0x0200 will be returned.  Similarly, only
** one of 0x1000 or 0x2000 will be returned.  If the 0x0080 is set, then
** none of the 0xff00 bits will be set.
**
** The order of parameters passed to xFilter is as follows:
**
**    * The argument to start= if bit 0x0001 is in the idxNum mask
**    * The argument to stop= if bit 0x0002 is in the idxNum mask
**    * The argument to step= if bit 0x0004 is in the idxNum mask
**    * The argument to LIMIT if bit 0x0020 is in the idxNum mask
**    * The argument to OFFSET if bit 0x0040 is in the idxNum mask
**    * The argument to value=, or value>= or value> if any of
**      bits 0x0380 are in the idxNum mask
**    * The argument to value<= or value< if either of bits 0x3000
**      are in the mask
**
*/
static int seriesBestIndex(
  sqlite3_vtab *pVTab,
  sqlite3_index_info *pIdxInfo
){
  int i, j;              /* Loop over constraints */
  int idxNum = 0;        /* The query plan bitmask */
#ifndef ZERO_ARGUMENT_GENERATE_SERIES
  int bStartSeen = 0;    /* EQ constraint seen on the START column */
#endif
  int unusableMask = 0;  /* Mask of unusable constraints */
  int nArg = 0;          /* Number of arguments that seriesFilter() expects */
  int aIdx[7];           /* Constraints on start, stop, step, LIMIT, OFFSET,
                         ** and value.  aIdx[5] covers value=, value>=, and
                         ** value>,  aIdx[6] covers value<= and value< */
  const struct sqlite3_index_constraint *pConstraint;

  /* This implementation assumes that the start, stop, and step columns
  ** are the last three columns in the virtual table. */
  assert( SERIES_COLUMN_STOP == SERIES_COLUMN_START+1 );
  assert( SERIES_COLUMN_STEP == SERIES_COLUMN_START+2 );

  aIdx[0] = aIdx[1] = aIdx[2] = aIdx[3] = aIdx[4] = aIdx[5] = aIdx[6] = -1;
  pConstraint = pIdxInfo->aConstraint;
  for(i=0; i<pIdxInfo->nConstraint; i++, pConstraint++){
    int iCol;    /* 0 for start, 1 for stop, 2 for step */
    int iMask;   /* bitmask for those column */
    int op = pConstraint->op;
    if( op>=SQLITE_INDEX_CONSTRAINT_LIMIT
     && op<=SQLITE_INDEX_CONSTRAINT_OFFSET
    ){
      if( pConstraint->usable==0 ){
        /* do nothing */
      }else if( op==SQLITE_INDEX_CONSTRAINT_LIMIT ){
        aIdx[3] = i;
        idxNum |= 0x20;
      }else{
        assert( op==SQLITE_INDEX_CONSTRAINT_OFFSET );
        aIdx[4] = i;
        idxNum |= 0x40;
      }
      continue;
    }
    if( pConstraint->iColumn<SERIES_COLUMN_START ){
      if( (pConstraint->iColumn==SERIES_COLUMN_VALUE ||
           pConstraint->iColumn==SERIES_COLUMN_ROWID)
       && pConstraint->usable
      ){
        switch( op ){
          case SQLITE_INDEX_CONSTRAINT_EQ:
          case SQLITE_INDEX_CONSTRAINT_IS: {
            idxNum |=  0x0080;
            idxNum &= ~0x3300;
            aIdx[5] = i;
            aIdx[6] = -1;
#ifndef ZERO_ARGUMENT_GENERATE_SERIES
            bStartSeen = 1;
#endif
            break;
          }
          case SQLITE_INDEX_CONSTRAINT_GE: {
            if( idxNum & 0x0080 ) break;
            idxNum |=  0x0100;
            idxNum &= ~0x0200;
            aIdx[5] = i;
#ifndef ZERO_ARGUMENT_GENERATE_SERIES
            bStartSeen = 1;
#endif
            break;
          }
          case SQLITE_INDEX_CONSTRAINT_GT: {
            if( idxNum & 0x0080 ) break;
            idxNum |=  0x0200;
            idxNum &= ~0x0100;
            aIdx[5] = i;
#ifndef ZERO_ARGUMENT_GENERATE_SERIES
            bStartSeen = 1;
#endif
            break;
          }
          case SQLITE_INDEX_CONSTRAINT_LE: {
            if( idxNum & 0x0080 ) break;
            idxNum |=  0x1000;
            idxNum &= ~0x2000;
            aIdx[6] = i;
            break;
          }
          case SQLITE_INDEX_CONSTRAINT_LT: {
            if( idxNum & 0x0080 ) break;
            idxNum |=  0x2000;
            idxNum &= ~0x1000;
            aIdx[6] = i;
            break;
          }
        }
      }
      continue;
    }
    iCol = pConstraint->iColumn - SERIES_COLUMN_START;
    assert( iCol>=0 && iCol<=2 );
    iMask = 1 << iCol;
#ifndef ZERO_ARGUMENT_GENERATE_SERIES
    if( iCol==0 && op==SQLITE_INDEX_CONSTRAINT_EQ ){
      bStartSeen = 1;
    }
#endif
    if( pConstraint->usable==0 ){
      unusableMask |=  iMask;
      continue;
    }else if( op==SQLITE_INDEX_CONSTRAINT_EQ ){
      idxNum |= iMask;
      aIdx[iCol] = i;
    }
  }
  if( aIdx[3]==0 ){
    /* Ignore OFFSET if LIMIT is omitted */
    idxNum &= ~0x60;
    aIdx[4] = 0;
  }
  for(i=0; i<7; i++){
    if( (j = aIdx[i])>=0 ){
      pIdxInfo->aConstraintUsage[j].argvIndex = ++nArg;
      pIdxInfo->aConstraintUsage[j].omit =
         !SQLITE_SERIES_CONSTRAINT_VERIFY || i>=3;
    }
  }
  /* The current generate_column() implementation requires at least one
  ** argument (the START value).  Legacy versions assumed START=0 if the
  ** first argument was omitted.  Compile with -DZERO_ARGUMENT_GENERATE_SERIES
  ** to obtain the legacy behavior */
#ifndef ZERO_ARGUMENT_GENERATE_SERIES
  if( !bStartSeen ){
    sqlite3_free(pVTab->zErrMsg);
    pVTab->zErrMsg = sqlite3_mprintf(
        "first argument to \"generate_series()\" missing or unusable");
    return SQLITE_ERROR;
  }
#endif
  if( (unusableMask & ~idxNum)!=0 ){
    /* The start, stop, and step columns are inputs.  Therefore if there
    ** are unusable constraints on any of start, stop, or step then
    ** this plan is unusable */
    return SQLITE_CONSTRAINT;
  }
  if( (idxNum & 0x03)==0x03 ){
    /* Both start= and stop= boundaries are available.  This is the 
    ** the preferred case */
    pIdxInfo->estimatedCost = (double)(2 - ((idxNum&4)!=0));
    pIdxInfo->estimatedRows = 1000;
    if( pIdxInfo->nOrderBy>=1 && pIdxInfo->aOrderBy[0].iColumn==0 ){
      if( pIdxInfo->aOrderBy[0].desc ){
        idxNum |= 0x08;
      }else{
        idxNum |= 0x10;
      }
      pIdxInfo->orderByConsumed = 1;
    }
  }else if( (idxNum & 0x21)==0x21 ){
    /* We have start= and LIMIT */
    pIdxInfo->estimatedRows = 2500;
  }else{
    /* If either boundary is missing, we have to generate a huge span
    ** of numbers.  Make this case very expensive so that the query
    ** planner will work hard to avoid it. */
    pIdxInfo->estimatedRows = 2147483647;
  }
  pIdxInfo->idxNum = idxNum;
#ifdef SQLITE_INDEX_SCAN_HEX
  pIdxInfo->idxFlags = SQLITE_INDEX_SCAN_HEX;
#endif
  return SQLITE_OK;
}

/*
** This following structure defines all the methods for the 
** generate_series virtual table.
*/
static sqlite3_module seriesModule = {
  0,                         /* iVersion */
  0,                         /* xCreate */
  seriesConnect,             /* xConnect */
  seriesBestIndex,           /* xBestIndex */
  seriesDisconnect,          /* xDisconnect */
  0,                         /* xDestroy */
  seriesOpen,                /* xOpen - open a cursor */
  seriesClose,               /* xClose - close a cursor */
  seriesFilter,              /* xFilter - configure scan constraints */
  seriesNext,                /* xNext - advance a cursor */
  seriesEof,                 /* xEof - check for end of scan */
  seriesColumn,              /* xColumn - read data */
  seriesRowid,               /* xRowid - read data */
  0,                         /* xUpdate */
  0,                         /* xBegin */
  0,                         /* xSync */
  0,                         /* xCommit */
  0,                         /* xRollback */
  0,                         /* xFindMethod */
  0,                         /* xRename */
  0,                         /* xSavepoint */
  0,                         /* xRelease */
  0,                         /* xRollbackTo */
  0,                         /* xShadowName */
  0                          /* xIntegrity */
};

#endif /* SQLITE_OMIT_VIRTUALTABLE */

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_series_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  (void)pApi;
#ifndef SQLITE_OMIT_VIRTUALTABLE
  if( sqlite3_libversion_number()<3008012 && pzErrMsg!=0 ){
    *pzErrMsg = sqlite3_mprintf(
        "generate_series() requires SQLite 3.8.12 or later");
    return SQLITE_ERROR;
  }
  rc = sqlite3_create_module(db, "generate_series", &seriesModule, 0);
#endif
  return rc;
}
#define CMPP_D_DEMO
/*
** 2025-10-18:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
**
** This file contains demonstration client-side directives for the
** c-pp API.
*/
#if defined(CMPP_D_DEMO)
/* Only when building with the main c-pp app. */
#elif !defined(CMPP_MODULE_REGISTER1)
/**
  Assume a standalone module build. Arrange for the default module
  entry point to be installed so that cmpp_module_load() does not
  require that the user know the entry point name.
*/
#define CMPP_MODULE_STANDALONE
#define CMPP_API_THUNK
#include "libcmpp.h"
#endif

#include <stdlib.h>
#include <assert.h>
#include <string.h>

/**
   cmpp_d_autoload_f() impl for this file's directives and its close
   friends.
*/
int cmpp_d_autoload_f_demos(cmpp *pp, char const *dname, void *state);
/**
   Registers demo and utility directives with pp.
*/
int cmpp_module__demo_register(cmpp *pp);

/**
   Simply says hello and emits info about its arguments.
*/
static void cmpp_dx_f_demo1(cmpp_dx *dx){
  cmpp_dx_outf(dx, "Hello from %s%s\n",
               cmpp_dx_delim(dx), dx->d->name.z);
  for( cmpp_arg const * a = dx->args.arg0;
       0==cmpp_dx_err_check(dx) && a;
       a = a->next ){
    cmpp_dx_outf(dx, "arg type=%s n=%u z=%.*s\n",
                 cmpp_tt_cstr(a->ttype),
                 (unsigned)a->n, (int)a->n, a->z);
  }
}

/**
   Internal helper for other directives.  Emits an HTML <div> tag. If
   passed any arguments, each is assumed to be a CSS class name and is
   applied to the DIV. This does _not_ emit the lcosing DIV
   tag. Returns 0 on success.
*/
static int divOpener(cmpp_dx *dx){
  cmpp_dx_out_raw(dx, "<div", 4);
  int nClass = 0;
  for( cmpp_arg const * a = dx->args.arg0;
       0==cmpp_dx_err_check(dx) && a;
       a = a->next ){
    if( 1==++nClass ){
      cmpp_dx_out_raw(dx, " class='", 8);
    }else{
      cmpp_dx_out_raw(dx, " ", 1);
    }
    cmpp_dx_out_raw(dx, a->z, a->n);
  }
  return cmpp_dx_out_raw(dx, nClass ? "'>" : ">", 1 + !!nClass);
}

/**
   Opens an HTML DIV tag, as per divOpener().
*/
static void cmpp_dx_f_divOpen(cmpp_dx *dx){
  if( 0==divOpener(dx) ){
    int * const nDiv = dx->d->impl.state;
    assert( nDiv );
    ++*nDiv;
  }
}

/**
   Closes an HTML DIV tag which was opened by cmpp_dx_f_divOpen().
*/
static void cmpp_dx_f_divClose(cmpp_dx *dx){
  int * const nDiv = dx->d->impl.state;
  assert( nDiv );
  if( *nDiv > 0 ){
    --*nDiv;
  }else{
    char const * const zDelim = cmpp_dx_delim(dx);
    cmpp_dx_err_set(
      dx, CMPP_RC_MISUSE,
      "%s/%s was used without an opening %s%s directive",
      zDelim, dx->d->name.z, zDelim, dx->d->name.z
    );
  }
}

/**
   Another HTML DIV-inspired wrapper which consumes a block of input
   and wraps it in a DIV. This is functionally the same as the
   divOpen/divClose examples but demonstrates how to slurp up the
   content between the open/close directives from within the opening
   directive's callback.
*/
static void cmpp_dx_f_divWrapper(cmpp_dx *dx){
  if( divOpener(dx) ) return;
  cmpp_b os = {0};
  if( 0==cmpp_dx_consume_b(
        dx, &os, &dx->d->closer, 1,
        cmpp_dx_consume_F_PROCESS_OTHER_D
      )
      /* ^^^ says "read to the matching #/div" accounting for, and
         processing, nested directives). The #/div closing tag is
         identified by dx->d->closer. */
  ){
    cmpp_b_chomp( &os );
    /* Recall that most cmpp APIs become no-ops if dx->pp has an error
       set, so we don't strictly need to error-check these calls: */
    cmpp_dx_out_raw(dx, os.z, os.n);
    cmpp_dx_out_raw(dx, "</div>\n", 7);
  }
  cmpp_b_clear(&os);
}

/**
   A cmpp_d_autoload_f() impl for testing and demonstration
   purposes.
*/
int cmpp_d_autoload_f_demos(cmpp *pp, char const *dname, void *state){
  (void)state;
  cmpp_api_init(pp);

#define M(NAME) (0==strcmp(NAME,dname))
#define MOC(NAME) (M(NAME) || M("/"NAME))

#define DREG0(SYMNAME, NAME, OPENER, OFLAGS, CLOSER, CFLAGS)  \
  cmpp_d_reg SYMNAME = {  \
    .name = NAME,         \
    .opener = {           \
      .f = OPENER,        \
      .flags = OFLAGS     \
    },                    \
    .closer = {           \
      .f = CLOSER,        \
      .flags = CFLAGS     \
    },                    \
    .dtor = 0,            \
    .state = 0            \
  }

#define CHECK(NAME,CHECKCLOSER,OPENER,OFLAGS,CLOSER,CFLAGS) \
  if( M(NAME) || (CHECKCLOSER && M("/"NAME)) ){             \
    DREG0(reg,NAME,OPENER,OFLAGS,CLOSER,CFLAGS);            \
    return cmpp_d_register(pp, &reg, NULL);     \
  } (void)0


  CHECK("demo1", 0, cmpp_dx_f_demo1, cmpp_d_F_ARGS_LIST, 0, 0);

  CHECK("demo-div-wrapper", 1, cmpp_dx_f_divWrapper,
        cmpp_d_F_ARGS_LIST | cmpp_d_F_NO_CALL,
        cmpp_dx_f_dangling_closer, 0);

  if( MOC("demo-div") ){
    cmpp_d_reg const r = {
      .name = "demo-div",
      .opener = {
        .f = cmpp_dx_f_divOpen,
        .flags = cmpp_d_F_ARGS_LIST | cmpp_d_F_NO_CALL
      },
      .closer = {
        .f = cmpp_dx_f_divClose
      },
      .state = cmpp_malloc(sizeof(int)),
      .dtor = cmpp_mfree
    };
    /* State for one of the custom directives. */;
    int const rc = cmpp_d_register(pp, &r, NULL);
    if( 0==rc ){
      *((int*)r.state) = 0
        /* else reg.state was freed by cmpp_d_register() */;
    }
    return rc;
  }

#undef M
#undef MOC
#undef CHECK
#undef DREG0
  return CMPP_RC_NO_DIRECTIVE;
}

int cmpp_module__demo_register(cmpp *pp){
  cmpp_api_init(pp);
  int rc;
#define X(D) \
  rc = cmpp_d_autoload_f_demos(pp, D, NULL);    \
  if( rc && CMPP_RC_NO_DIRECTIVE!=rc ) goto end
  X("demo1");
  X("demo-div");
  X("demo-div-wrapper");
#undef X
end:
  return cmpp_err_get(pp, NULL);
}
CMPP_MODULE_REGISTER1(demo);
#endif /* include guard */
