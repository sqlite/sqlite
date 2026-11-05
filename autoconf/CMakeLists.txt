cmake_minimum_required(VERSION 3.18)
project(sqlite3 VERSION 3.42.0 HOMEPAGE_URL "http://www.sqlite.org")

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckFunctionExists)

set(ENABLE_SHARED ON CACHE BOOL "Build shared library")
set(ENABLE_STATIC ON CACHE BOOL "Build static library")
set(ENABLE_EDITLINE ON CACHE BOOL "Use BSD libedit")
set(ENABLE_READLINE ON CACHE BOOL "Use GNU readline")
set(ENABLE_THREADSAFE ON CACHE BOOL "Build a thread-safe library")
set(ENABLE_DYNAMIC_EXTENSIONS ON CACHE BOOL "Support loadable extensions")
set(ENABLE_MATH ON CACHE BOOL "SQL math functions")
set(ENABLE_FTS4 ON CACHE BOOL "Include fts4 support")
set(ENABLE_FTS3 OFF CACHE BOOL "Include fts3 support")
set(ENABLE_FTS5 ON CACHE BOOL "Include fts5 support")
set(ENABLE_RTREE ON CACHE BOOL "Include rtree support")
set(ENABLE_SESSION OFF CACHE BOOL "Enable the session extension")
set(ENABLE_DEBUG OFF CACHE BOOL "Build with debugging features enabled")
set(ENABLE_STATIC_SHELL ON CACHE BOOL "Statically link shell tool")

check_include_file(stdio.h HAVE_STDIO_H)
if(HAVE_STDIO_H)
    add_definitions(-DHAVE_STDIO_H=1)
endif()

check_include_file(stdlib.h HAVE_STDLIB_H)
if(HAVE_STDLIB_H)
    add_definitions(-DHAVE_STDLIB_H=1)
endif()

check_include_file(string.h HAVE_STRING_H)
if(HAVE_STRING_H)
    add_definitions(-DHAVE_STRING_H=1)
endif()

check_include_file(strings.h HAVE_STRINGS_H)
if(HAVE_STRINGS_H)
    add_definitions(-DHAVE_STRINGS_H=1)
endif()

check_include_file(inttypes.h HAVE_INTTYPES_H)
if(HAVE_INTTYPES_H)
    add_definitions(-DHAVE_INTTYPES_H=1)
endif()

check_include_file(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H=1)
endif()

check_include_file(unistd.h HAVE_UNISTD_H)
if(HAVE_UNISTD_H)
    add_definitions(-DHAVE_UNISTD_H=1)
endif()

check_include_file(sys/stat.h HAVE_SYS_STAT_H)
if(HAVE_SYS_STAT_H)
    add_definitions(-DHAVE_SYS_STAT_H=1)
endif()

check_include_file(sys/types.h HAVE_SYS_TYPES_H)
if(HAVE_SYS_TYPES_H)
    add_definitions(-DHAVE_SYS_TYPES_H=1)
endif()

if(HAVE_STDLIB_H AND HAVE_STRING_H)
    add_definitions(-DSTDC_HEADERS=1)
endif()

check_function_exists(fdatasync HAVE_FDATASYNC)
if(HAVE_FDATASYNC)
    add_definitions(-DHAVE_FDATASYNC=1)
endif()

check_function_exists(usleep HAVE_USLEEP)
if(HAVE_USLEEP)
    add_definitions(-DHAVE_USLEEP=1)
endif()

check_function_exists(fullfsync HAVE_FULLFSYNC)
if(HAVE_FULLFSYNC)
    add_definitions(-DHAVE_FULLFSYNC=1)
endif()

check_function_exists(localtime_r HAVE_LOCALTIME_R)
if(HAVE_LOCALTIME_R)
    add_definitions(-DHAVE_LOCALTIME_R=1)
endif()

check_function_exists(gmtime_r HAVE_GMTIME_R)
if(HAVE_GMTIME_R)
    add_definitions(-DHAVE_GMTIME_R=1)
endif()

check_function_exists(utime HAVE_UTIME)
if(HAVE_UTIME)
    add_definitions(-DHAVE_UTIME=1)
endif()

check_function_exists(mremap HAVE_MREMAP)
if(HAVE_MREMAP)
    add_definitions(-DHAVE_MREMAP=1)
endif()

check_function_exists(fchown HAVE_FCHOWN)
if(HAVE_FCHOWN)
    add_definitions(-DHAVE_FCHOWN=1)
endif()

check_function_exists(readlink HAVE_READLINK)
if(HAVE_READLINK)
    add_definitions(-DHAVE_READLINK=1)
endif()

check_function_exists(lstat HAVE_LSTAT)
if(HAVE_LSTAT)
    add_definitions(-DHAVE_LSTAT=1)
endif()

check_function_exists(gethostuuid HAVE_GETHOSTUUID)
if(HAVE_GETHOSTUUID)
    add_definitions(-DHAVE_GETHOSTUUID=1)
endif()

check_function_exists(posix_fallocate HAVE_POSIX_FALLOCATE)
if(HAVE_POSIX_FALLOCATE)
    add_definitions(-DHAVE_POSIX_FALLOCATE=1)
endif()

check_symbol_exists(strerror_r string.h HAVE_DECL_STRERROR_R)
if(HAVE_DECL_STRERROR_R)
    add_definitions(-DHAVE_DECL_STRERROR_R=1 -DHAVE_STRERROR_R=1)
endif()

check_function_exists(system _HAVE_SYSTEM)
if(NOT _HAVE_SYSTEM)
    set(SQLITE3_SHELL_DEFS SQLITE_NOHAVE_SYSTEM=1)
endif()

# Check pthead library
if(ENABLE_THREADSAFE)
    if(WIN32)
        add_definitions(-D_REENTRANT=1 -DSQLITE_THREADSAFE=1)
    else()
        check_include_file(pthread.h HAVE_PTHREAD_H)
        if(HAVE_PTHREAD_H)
            add_definitions(-DHAVE_PTHREAD_H=1)

            set(CMAKE_REQUIRED_LIBRARIES pthread)
            check_function_exists(pthread_create _HAVE_PTHREAD_CREATE)
            check_function_exists(pthread_mutexattr_init _HAVE_PTHREAD_MUTEXATTR_INIT)
            unset(CMAKE_REQUIRED_LIBRARIES)

            if(_HAVE_PTHREAD_CREATE AND _HAVE_PTHREAD_MUTEXATTR_INIT)
                add_definitions(-D_REENTRANT=1 -DSQLITE_THREADSAFE=1)
                set(PTHREAD_LIBRARIES pthread)
            else()
                add_definitions(-DSQLITE_THREADSAFE=0)
                set(ENABLE_THREADSAFE OFF CACHE BOOL "Build a thread-safe library" FORCE)
                message(WARNING "Missing pthread functions, thread-safe disabled!")
            endif()
        endif()
    endif()
else()
    add_definitions(-DSQLITE_THREADSAFE=0)
endif()

if(ENABLE_DYNAMIC_EXTENSIONS)
    if(NOT WIN32)
        check_include_file(dlfcn.h HAVE_DLFCN_H)
        if(HAVE_DLFCN_H)
            add_definitions(-DHAVE_DLFCN_H=1)

            set(CMAKE_REQUIRED_LIBRARIES dl)
            check_function_exists(dlopen _HAVE_DLOPEN)
            unset(CMAKE_REQUIRED_LIBRARIES)

            if(_HAVE_DLOPEN)
                if(NOT APPLE)
                    set(DL_LIBRARIES dl)
                endif()
            else()
                add_definitions(-DSQLITE_OMIT_LOAD_EXTENSION=1)
                set(ENABLE_DYNAMIC_EXTENSIONS OFF CACHE BOOL "Support loadable extensions" FORCE)
                message(WARNING "Missing dlopen function, dynamic extensions disabled!")
            endif()
        endif()
    endif()
else()
    add_definitions(-DSQLITE_OMIT_LOAD_EXTENSION=1)
endif()

if(ENABLE_MATH)
    if(WIN32)
        add_definitions(-DSQLITE_ENABLE_MATH_FUNCTIONS=1)
    else()
        set(CMAKE_REQUIRED_LIBRARIES m)
        check_function_exists(ceil _HAVE_MATH_FUNCTIONS)
        unset(CMAKE_REQUIRED_LIBRARIES)

        if(_HAVE_MATH_FUNCTIONS)
            add_definitions(-DSQLITE_ENABLE_MATH_FUNCTIONS=1)
            set(MATH_LIBRARIES m)
        else()
            set(ENABLE_MATH OFF CACHE BOOL "SQL math functions" FORCE)
            message(WARNING "Missing math function, SQL math functions disabled!")
        endif()
    endif()
endif()

# Check zlib
check_include_file(zlib.h HAVE_ZLIB_H)
if(HAVE_ZLIB_H)
    add_definitions(-DHAVE_ZLIB_H=1)

    set(CMAKE_REQUIRED_LIBRARIES z)
    check_function_exists(deflate SQLITE_HAVE_ZLIB)
    unset(CMAKE_REQUIRED_LIBRARIES)

    if(SQLITE_HAVE_ZLIB)
        add_definitions(-DSQLITE_HAVE_ZLIB=1)
        set(ZLIB_LIBRARIES z)
    endif()
endif()

# Check editline or readline
if(ENABLE_EDITLINE)
    check_include_file(editline/readline.h HAVE_EDITLINE_READLINE_H)
    if(HAVE_EDITLINE_READLINE_H)
        add_definitions(-DHAVE_EDITLINE_READLINE_H=1)

        set(CMAKE_REQUIRED_LIBRARIES edit)
        check_function_exists(readline HAVE_EDITLINE)
        unset(CMAKE_REQUIRED_LIBRARIES)

        if(HAVE_EDITLINE)
            add_definitions(-DHAVE_EDITLINE=1)
            set(READLINE_LIBRARIES edit)
            set(ENABLE_READLINE OFF CACHE BOOL "Use GNU readline" FORCE)
        else()
            set(ENABLE_EDITLINE OFF CACHE BOOL "Use BSD libedit" FORCE)
        endif()
    else()
        set(ENABLE_EDITLINE OFF CACHE BOOL "Use BSD libedit" FORCE)
    endif()
endif()

if(ENABLE_READLINE AND (NOT HAVE_EDITLINE))
    check_include_file(readline/readline.h HAVE_READLINE_READLINE_H)
    if(HAVE_READLINE_READLINE_H)
        add_definitions(-DHAVE_READLINE_READLINE_H=1)
        add_definitions(-DHAVE_READLINE=1)
        set(READLINE_LIBRARIES readline)
    else()
        set(ENABLE_READLINE OFF CACHE BOOL "Use GNU readline" FORCE)
    endif()
endif()

if(ENABLE_FTS4)
    add_definitions(-DSQLITE_ENABLE_FTS4=1)
endif()

if(ENABLE_FTS3)
    add_definitions(-DSQLITE_ENABLE_FTS3=1)
endif()

if(ENABLE_FTS5)
    add_definitions(-DSQLITE_ENABLE_FTS5=1)
endif()

if(ENABLE_RTREE)
    add_definitions(-DSQLITE_ENABLE_RTREE=1 -DSQLITE_ENABLE_GEOPOLY=1)
endif()

if(ENABLE_SESSION)
    add_definitions(-DSQLITE_ENABLE_SESSION=1 -DSQLITE_ENABLE_PREUPDATE_HOOK=1)
endif()

if(ENABLE_DEBUG)
    add_definitions(-DSQLITE_DEBUG=1
        -DSQLITE_ENABLE_SELECTTRACE=1
        -DSQLITE_ENABLE_WHERETRACE=1)
endif()

add_definitions(
    -DSQLITE_ENABLE_EXPLAIN_COMMENTS=1
    -DSQLITE_ENABLE_DBPAGE_VTAB=1
    -DSQLITE_ENABLE_STMTVTAB=1
    -DSQLITE_ENABLE_DBSTAT_VTAB=1)

if(WIN32)
    add_definitions(
        -DSQLITE_ENABLE_RBU=1
        -DSQLITE_ENABLE_OFFSET_SQL_FUNC=1
        -DSQLITE_ENABLE_BYTECODE_VTAB=1
        -DSQLITE_ENABLE_COLUMN_METADATA=1
        -DSQLITE_WIN32_USE_UUID=1)

    set(UUID_LIBRARIES Rpcrt4.lib)
endif()

if(APPLE)
    # Universal bianry
    set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)
endif()

# Check the user defined sqlite config header file
check_include_file(sqlite_cfg.h HAVE_SQLITE_CONFIG_H)
if(HAVE_SQLITE_CONFIG_H)
    add_definitions(-D_HAVE_SQLITE_CONFIG_H=1)
endif()

if(ENABLE_STATIC_SHELL)
    if(NOT EANBLED_STATIC)
        set(ENABLE_STATIC ON CACHE BOOL "Build static library" FORCE)
    endif()
else()
    if(NOT ENABLE_SHARED)
        set(ENABLE_SHARED ON CACHE BOOL "Build shared library" FORCE)
    endif()
endif()

message(STATUS "Build shared library... ${ENABLE_SHARED}")
message(STATUS "Build static library... ${ENABLE_STATIC}")
message(STATUS "Use BSD libedit... ${ENABLE_EDITLINE}")
message(STATUS "Use GNU readline... ${ENABLE_READLINE}")
message(STATUS "Build a thread-safe library... ${ENABLE_THREADSAFE}")
message(STATUS "Support loadable extensions... ${ENABLE_DYNAMIC_EXTENSIONS}")
message(STATUS "SQL math functions... ${ENABLE_MATH}")
message(STATUS "Include fts4 support... ${ENABLE_FTS4}")
message(STATUS "Include fts3 support... ${ENABLE_FTS3}")
message(STATUS "Include fts5 support... ${ENABLE_FTS5}")
message(STATUS "Include rtree support... ${ENABLE_RTREE}")
message(STATUS "Enable the session extension... ${ENABLE_SESSION}")
message(STATUS "Build with debugging features enabled... ${ENABLE_DEBUG}")
message(STATUS "Statically link shell tool... ${ENABLE_STATIC_SHELL}")

set(SQLITE3_LIB_SRC sqlite3.c)
set(SQLITE3_SHELL_SRC shell.c)

set(SQLITE3_DEP_LIBRARIES
    ${UUID_LIBRARIES}
    ${READLINE_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${PTHREAD_LIBRARIES}
    ${DL_LIBRARIES}
    ${MATH_LIBRARIES})

# SQLite3 pkg-config file
if(NOT WIN32)
    set(prefix ${CMAKE_INSTALL_PREFIX})
    set(exec_prefix "\$\{prefix\}")
    set(libdir "\$\{exec_prefix\}/lib")
    set(includedir "\$\{prefix\}/include")
    set(PACKAGE_VERSION ${PROJECT_VERSION})
    list(JOIN SQLITE3_DEP_LIBRARIES "\ -l" LIBS)
    set(LIBS -l${LIBS})
    configure_file(sqlite3.pc.in sqlite3.pc @ONLY)
endif()

if(ENABLE_SHARED)
    if(WIN32)
        set(SQLITE_API_DECL "SQLITE_API=__declspec(dllexport)")
    endif()

    add_library(sqlite3-shared SHARED ${SQLITE3_LIB_SRC})
    target_compile_definitions(sqlite3-shared PRIVATE ${SQLITE_API_DECL})
    target_link_libraries(sqlite3-shared ${SQLITE3_DEP_LIBRARIES})
    set_target_properties(sqlite3-shared PROPERTIES
        OUTPUT_NAME sqlite3
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

    install(TARGETS sqlite3-shared
        RUNTIME DESTINATION "bin"
        ARCHIVE DESTINATION "lib"
        LIBRARY DESTINATION "lib")
endif()

if(ENABLE_STATIC)
    if(WIN32)
        set(NAME_FIX_DECORATOR "_s")
    endif()

    add_library(sqlite3-static STATIC ${SQLITE3_LIB_SRC})
    set_target_properties(sqlite3-static PROPERTIES
        OUTPUT_NAME sqlite3${NAME_FIX_DECORATOR})

    install(TARGETS sqlite3-static
        EXPORT SQLite3Config
        RUNTIME DESTINATION "bin"
        ARCHIVE DESTINATION "lib"
        LIBRARY DESTINATION "lib")
endif()

add_executable(sqlite3 ${SQLITE3_SHELL_SRC})
target_compile_definitions(sqlite3 PRIVATE ${SQLITE3_SHELL_DEFS})
if(ENABLE_STATIC_SHELL)
add_dependencies(sqlite3 sqlite3-static)
    target_link_libraries(sqlite3 sqlite3-static ${SQLITE3_DEP_LIBRARIES})
else()
    add_dependencies(sqlite3 sqlite3-shared)
    target_link_libraries(sqlite3 sqlite3-shared)
endif()

install(FILES sqlite3.h sqlite3ext.h
    DESTINATION "include")

if(NOT WIN32)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sqlite3.pc
        DESTINATION "lib/pkgconfig")
endif()

install(TARGETS sqlite3
    RUNTIME DESTINATION "bin"
    ARCHIVE DESTINATION "lib"
    LIBRARY DESTINATION "lib")

