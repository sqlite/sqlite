Jaccwabyt 🐇
============================================================

**Jaccwabyt**: _JavaScript ⇄ C Struct Communication via WASM Byte
Arrays_

Welcome to Jaccwabyt, a JavaScript API which creates bindings for
WASM-compiled C structs, defining them in such a way that changes to
their state in JS are visible in C/WASM, and vice versa, permitting
two-way interchange of struct state with very little user-side
friction.

(If that means nothing to you, neither will the rest of this page!)

To the best of its creator's fallible knowledge, Jaccwabyt is the only
library of its kind (as of 2025-11). Aside from wrapping existing
structs, e.g. to integrate "legacy" C code into JS/WASM, it can also
model C structs without requiring a native C counterpart, a feature
which probably has only exceedingly obscure uses in JS-side
implementations for native callbacks.

How it works:

- The client provides a JSON-friendly description of a C struct,
  describing the names, sizes, and offsets of each member.
- Pass that description to a factory function to create
  a JS constructor for that C struct.
- That constructor allocates a block of heap memory of the C struct's
  size and maps it to the new JS-side struct instance.  Each instance
  inherits property interceptors for each struct member, such that
  fetching the C struct's members reads directly from the struct's
  memory and setting them writes to that memory. Similarly, these
  objects can be provided with memory constructed elsewhere, e.g.
  a struct pointer returned from a WASM function, and can proxy
  that memory via the struct's interface.
- Clients eventually call the `dispose()` method to free the
  instance's heap memory and disassociate the JS instance with its
  WASM-side resources.

Easy peasy!

**Build instructions**: [see Appendix B](#appendix-b).

**Browser compatibility**: this library requires a _recent_ browser
and makes no attempt whatsoever to accommodate "older" or
lesser-capable ones, where "recent," _very roughly_, means released in
mid-2018 or later, with late 2021 releases required for some optional
features in some browsers (e.g. [BigInt64Array][] in Safari). It also
relies on a couple non-standard, but widespread, features, namely
[TextEncoder][] and [TextDecoder][]. It is developed primarily on
Firefox and Chrome on Linux and all claims of Safari compatibility
are based solely on feature compatibility tables provided at
[MDN][].

**Non-browser compatibility**: this code does not target non-browser
JS engines and is completely untested on them. That said, it "might
work". These JS APIs do not use the DOM API in any way, so are not
specifically tied to a browser, but they _are_ fully untested in such
environments. This code is known to work with both [Emscripten][] builds
and [WASI-SDK][] SDK builds (at of this writing, 2025-11-08).

**64-bit WASM:** as of 2025-09-21 this API supports 64-bit WASM builds
but it has to be configured for it (see [](#api-binderfactory) for
details).

**Formalities:**

- Author: [Stephan Beal][sgb]
- Project Homes:
  - <https://fossil.wanderinghorse.net/r/jaccwabyt>\  
    Is the primary home but...
  - <https://sqlite.org/src/dir/ext/wasm/jaccwabyt>\  
    ... most development happens here.

The license for both this documentation and the software it documents
is the same as [sqlite3][], the project from which this spinoff
project was spawned:

-----

> 2022-06-30:
>
> The author disclaims copyright to this source code.  In place of a
> legal notice, here is a blessing:
>
> -  May you do good and not evil.
> -  May you find forgiveness for yourself and forgive others.
> -  May you share freely, never taking more than you give.

-----

<a id='overview'></a>
Table of Contents
============================================================

- [Overview](#overview)
  - [Architecture](#architecture)
- [Creating and Binding Structs](#creating-binding)
  - [Step 1: Configure Jaccwabyt](#step-1)
  - [Step 2: Struct Description](#step-2)
     - [`P` vs `p`](#step-2-pvsp)
  - [Step 3: Binding a Struct](#step-3)
  - [Step 4: Creating, Using, and Destroying Instances](#step-4)
- APIs
  - [Struct Binder Factory](#api-binderfactory)
  - [Struct Binder](#api-structbinder)
     - [Struct Description Objects](#struct-descr)
- [Struct Type](#api-structtype)
  - [Struct Constructors](#api-structctor)
  - [Struct Protypes](#api-structprototype)
  - [Struct Instances](#api-structinstance)
- Appendices
  - [Appendix A: Limitations, TODOs, etc.](#appendix-a)
  - [Appendix B: Build](#appendix-b)
  - [Appendix D: Debug Info](#appendix-d)
  - [Appendix G: Generating Struct Descriptions](#appendix-g)

<a id='overview'></a>
Overview
============================================================

Management summary: this JavaScript-only framework provides limited
two-way bindings between C structs and JavaScript objects, such that
changes to the struct in one environment are visible in the other.

Details...

It works by creating JavaScript proxies for C structs. Reads and
writes of the JS-side members are marshaled through a flat byte array
allocated from the WASM heap. As that heap is shared with the C-side
code, and the memory block is written using the same approach C does,
that byte array can be used to access and manipulate a given struct
instance from both JS and C.

Motivating use case: this API was initially developed as an
experiment to determine whether it would be feasible to implement,
completely in JS, custom "VFS" and "virtual table" objects for the
WASM build of [sqlite3][]. Doing so was going to require some form of
two-way binding of several structs.  Once the proof of concept was
demonstrated, a rabbit hole appeared and _down we went_... It has
since grown beyond its humble proof-of-concept origins and is believed
to be a useful (or at least interesting) tool for mixed JS/C
applications.

Portability notes:

- These docs sometimes use [Emscripten][] as a point of reference
  because it is the most widespread WASM toolchain, but this code is
  specifically designed to be usable in arbitrary WASM environments.
  It abstracts away a few Emscripten-specific features into
  configurable options. The build tree supports both [Emscripten][]
  and [WASI-SDK][] to demonstrate that it has no dependencies on
  either.
- This code is encapsulated into a single JavaScript function. It
  should be trivial to copy/paste into arbitrary WASM/JS-using
  projects.
- The source tree includes C code, but only for testing and
  demonstration purposes. It is not a core distributable.

<a id='architecture'></a>
Architecture
------------------------------------------------------------

<!--
bug(?) (fossil): using "center" shrinks pikchr too much.
-->

```pikchr
BSBF: box rad 0.3*boxht "StructBinderFactory" fit fill lightblue
BSB: box same "StructBinder" fit at 0.75 e of 0.7 s of BSBF.c
BST: box same "StructType<T>" fit at 1.5 e of BSBF
BSC: box same "Struct<T>" "Ctor" fit at 1.5 s of BST
BSI: box same "Struct<T>" "Instances" fit at 1 right of BSB.e
BC: box same at 0.25 right of 1.6 e of BST "C Structs" fit fill lightgrey

arrow -> from BSBF.s to BSB.w "Generates" aligned above
arrow -> from BSB.n to BST.sw "Contains" aligned above
arrow -> from BSB.s to BSC.nw "Generates" aligned below
arrow -> from BSC.ne to BSI.s "Constructs" aligned below
arrow <- from BST.se to BSI.n "Inherits" aligned above
arrow <-> from BSI.e to BC.s dotted "Shared" aligned above "Memory" aligned below
arrow -> from BST.e to BC.w dotted "Mirrors Struct" aligned above "Model From" aligned below
arrow -> from BST.s to BSC.n "Prototype of" aligned above
```

Its major classes and functions are:

- **[StructBinderFactory][StructBinderFactory]** is a factory function which
  accepts a configuration object to customize it for a given WASM
  environment. A client will typically call this only one time, with
  an appropriate configuration, to generate a single...
- **[StructBinder][]** is a factory function which converts an
  arbitrary number struct descriptions into...
- **[StructType][]** are [constructors][StructCtor], one per struct
  description, which inherit from
  **[`StructBinder.StructType`][StructType]** and are used to
  instantiate...
- **[Struct instances][StructInstance]** are objects representing
  individual instances of generated struct types.

An app may have any number of StructBinders, but will typically
need only one. Each StructBinder is effectively a separate
namespace for struct creation.


<a id='creating-binding'></a>
Creating and Binding Structs
============================================================

From the amount of documentation provided, it may seem that
creating and using struct bindings is a daunting task, but it
essentially boils down to:

1. [Confire Jaccwabyt for your WASM environment](#step-1). This is a
   one-time task per project and results is a factory function which
   can create new struct bindings.
2. [Create a JSON-format description of your C structs](#step-2). This is
   required once for each struct and required updating if the C
   structs change.
3. [Feed (2) to the function generated by (1)](#step-3) to create JS
   constuctor functions for each struct. This is done at runtime, as
   opposed to during a build-process step, and can be set up in such a
   way that it does not require any maintenance after its initial
   setup.
4. [Create and use instances of those structs](#step-4).

Detailed instructions for each of those steps follows...

<a id='step-1'></a>
Step 1: Configure Jaccwabyt for the Environment
------------------------------------------------------------

Jaccwabyt's highest-level API is a single function. It creates a
factory for processing struct descriptions, but does not process any
descriptions itself. This level of abstraction exist primarily so that
the struct-specific factories can be configured for a given WASM
environment. Its usage looks like:

>  
```javascript
const MyBinder = StructBinderFactory({
  // These config options are all required:
  heap: WebAssembly.Memory instance or a function which returns
        a Uint8Array or Int8Array view of the WASM memory,
  alloc:   function(howMuchMemory){...},
  dealloc: function(pointerToFree){...},
  pointerSize: 4 or 8 // WASM pointer size
});
```

See [](#api-binderfactory) for full details about the config options.

It also offers a number of other settings, but all are optional except
for the ones shown above. Those three config options abstract away
details which are specific to a given WASM environment. They provide
the WASM "heap" memory, the memory allocator, and the deallocator. In
a conventional Emscripten setup, that config might simply look like:

>  
```javascript
{
    heap:    Module?.asm?.memory || Module['wasmMemory'],
    //Or:
    // heap: ()=>Module['HEAP8'],
    alloc:   (n)=>Module['_malloc'](n),
    dealloc: (m)=>Module['_free'](m)
}
```

The StructBinder factory function returns a function which can then be
used to create bindings for our structs.

<a id='step-2'></a>
Step 2: Create a Struct Description
------------------------------------------------------------

The primary input for this framework is a JSON-compatible construct
which describes a struct we want to bind. For example, given this C
struct:

>  
```c
// C-side:
struct Foo {
  int member1;
  void * member2;
  int64_t member3;
};
```

Its JSON description looks like:

>  
```json
{
  "name": "Foo",
  "sizeof": 16,
  "members": {
    "member1": {"offset": 0,"sizeof": 4,"signature": "i"},
    "member2": {"offset": 4,"sizeof": 4,"signature": "p"},
    "member3": {"offset": 8,"sizeof": 8,"signature": "j"}
  }
}
```

This is described in more detail in [][StructBinder].

<a id='step-2-pvsp'></a>
### `P` vs `p` in Method Signatures

*This support is experimental and subject to change.*

The method signature letter `p` means "pointer," which, in WASM, means
either int32 or int64. `p` is treated as a point for most contexts,
while still also being a separate type for function signature purposes
(analog to how pointers in C are just a special use of unsigned
numbers). A capital `P` changes the semantics of plain member pointers
(but not, as of this writing, function pointer members) as follows:

When a `P`-type member is **set** via `myStruct.x=y`, if
[`(y instanceof StructType)`][StructType] then the value of `y.pointer` is
stored in `myStruct.x`. If `y` is neither a pointer nor a
[StructType][], an exception is triggered (regardless of whether `p`
or `P` is used).


<a id='step-3'></a>
Step 3: Binding the Struct
------------------------------------------------------------

We can now use the results of steps 1 and 2:

>  
```javascript
const MyStruct = MyBinder(myStructDescription);
```

That creates a new constructor function, `MyStruct`, which can be used
to instantiate new instances. The binder will throw if it encounters
any problems.

That's all there is to it.

> Sidebar: that function may modify the struct description object
and/or its sub-objects, or may even replace sub-objects, in order to
simplify certain later operations. If that is not desired, then feed
it a copy of the original, e.g. by passing it
`JSON.parse(JSON.stringify(structDefinition))`.

<a id='step-4'></a>
Step 4: Creating, Using, and Destroying Struct Instances
------------------------------------------------------------

Now that we have our constructor...

>  
```javascript
const my = new MyStruct();
```

It is important to understand that creating a new instance allocates
memory on the WASM heap. We must not simply rely on garbage collection
to clean up the instances because doing so will not free up the WASM
heap memory. The correct way to free up that memory is to use the
object's `dispose()` method.

The following usage pattern offers one way to easily ensure proper
cleanup of struct instances:

>  
```javascript
const my = new MyStruct();
try {
  console.log(my.member1, my.member2, my.member3);
  my.member1 = 12;
  assert(12 === my.member1);
  /* ^^^ it may seem silly to test that, but recall that assigning that
     property encodes the value into a byte array in heap memory, not
     a normal JS property. Similarly, fetching the property decodes it
     from the byte array. */
  // Pass the struct to C code which takes a MyStruct pointer:
  aCFunction( my.pointer );
} finally {
  my.dispose();
}
```

> Sidebar: the `finally` block will be run no matter how the `try`
exits, whether it runs to completion, propagates an exception, or uses
flow-control keywords like `return` or `break`. It is perfectly legal
to use `try`/`finally` without a `catch`, and doing so is an ideal
match for the memory management requirements of Jaccwaby-bound struct
instances.

It is often useful to wrap an existing instance of a C-side struct
without taking over ownership of its memory. That can be achieved by
simply passing a pointer to the constructor. For example:

```js
const m = new MyStruct( functionReturningASharedPtr() );
// calling m.dispose() will _not_ free the wrapped C-side instance
// but will trigger any ondispose handler.
```

Now that we have struct instances, there are a number of things we
can do with them, as covered in the rest of this document.


<a id='api'></a>
API Reference
============================================================

<a id='api-binderfactory'></a>
API: Binder Factory
------------------------------------------------------------

This is the top-most function of the API, from which all other
functions and types are generated. The binder factory's signature is:

>  
```
Function StructBinderFactory(object configOptions);
```

It returns a function which these docs refer to as a [StructBinder][]
(covered in the next section). It throws on error.

The binder factory supports the following options in its configuration
object argument:

- `pointerSize` (Added 2025-11-15 to replace `pointerIR`)  
  Optionally specify the WASM pointer size of 4 (32-bit) or 8
  (64-bit).  Any other truthy value triggers an exception. If
  `pointerSize` is not set then it will guess the size by `alloc()`ing
  one byte, checking the result type, and `dealloc()`ing it.

- `heap`  
  Must be either a `WebAssembly.Memory` instance representing the WASM
  heap memory OR a function which returns an Int8Array or Uint8Array
  view of the WASM heap. In the latter case the function should, if
  appropriate for the environment, account for the heap being able to
  grow. Jaccwabyt uses this property in such a way that it is legal
  for the WASM heap to grow at runtime.

- `alloc`  
  Must be a function semantically compatible with C's
  `malloc(3)`. That is, it is passed the number of bytes to allocate
  and it returns a pointer. On allocation failure it may either return
  0 or throw an exception. This API will throw an exception if
  allocation fails or will propagate whatever exception the allocator
  throws. The allocator _must_ use the same heap as the `heap` config
  option.

- `dealloc`  
  Must be a function semantically compatible with C's `free(3)`. That
  is, it takes a pointer returned from `alloc()` and releases that
  memory. It must never throw and must accept a value of 0/null to
  mean "do nothing".

- `realloc`  
  Optional but required for (eventual (and optional) realloc support
  of structs. If set, it must be a function semantically compatible
  with C's `realloc()`. See `alloc`, above, for other requirements.

- `bigIntEnabled` (bool=true if BigInt64Array is available, else false)  
  If true, the WASM bits this code is used with must have been
  compiled with int64 support (e.g. using Emscripten's `-sWASM_BIGINT`
  flag). If that's not the case, this flag should be set to false. If
  it's enabled, BigInt support is assumed to work and certain extra
  features are enabled. Trying to use features which requires BigInt
  when it is disabled (e.g. using 64-bit integer types) will trigger
  an exception.

- `memberPrefix` and `memberSuffix` (string="")  
  If set, struct-defined properties get bound to JS with this string
  as a prefix resp. suffix. This can be used to avoid symbol name
  collisions between the struct-side members and the JS-side ones
  and/or to make more explicit which object-level properties belong to
  the struct mapping and which to the JS side. This does not modify
  the values in the struct description objects, just the property
  names through which they are accessed via property access operations
  and the various a [StructInstance][] APIs (noting that the latter
  tend to permit both the original names and the names as modified by
  these settings).

- `log`  
  Optional function used for debugging output. By default
  `console.debug` is used but by default no debug output is generated.
  This API assumes that the function will space-separate each argument
  (like `console.debug` does). See [Appendix D](#appendix-d) for info
  about enabling debugging output.

<a id='api-structbinder'></a>
API: Struct Binder
------------------------------------------------------------

Struct Binders are factories which are created by the
[StructBinderFactory][].  A given Struct Binder can process any number
of distinct structs. In a typical setup, an app will have only one
shared Binder Factory and one Struct Binder. Struct Binders which are
created via different [StructBinderFactory][] calls are unrelated to each
other, sharing no state except, perhaps, indirectly via
[StructBinderFactory][] configuration (e.g. the memory heap).

These factories have two call signatures:

>  
```javascript
Function StructBinder([string structName,] object structDescription)
```

If the struct description argument has a `name` property then the name
argument is optional, otherwise it is required.

The returned object is a constructor for instances of the struct
described by its argument(s), each of which derives from
a separate [StructType][] instance.

StructBinder has the following members:

- `allocCString(str)`  
  Allocates a new UTF-8-encoded, NUL-terminated copy of the given JS
  string and returns its address relative to `config.heap()`. If
  allocation returns 0 this function throws. Ownership of the memory
  is transfered to the caller, who must eventually pass it to the
  configured `config.dealloc()` function.

- `config`  
  The configuration object passed to the [StructBinderFactory][],
  primarily for accessing the memory (de)allocator and memory. Modifying
  any of its "significant" configuration values may have undefined
  results.

- `adaptGet(key [,func])`  
  Gets or sets a "get adaptor" by name - an arbitrary client-defined
  string, e.g. `"to-js-string"`. Struct description objects may have
  their `adaptGet` property set to the name of a mapped getter to
  behave exactly as if that struct description had set the given
  function as its `get` property. This offers a JSON-friendly way of
  storing adaptor mappings, with the caveat that the adaptors need to
  be defined _somewhere_ outside of JSON (typically it should be done
  immediately after creating the StructBinder).

- `adaptSet(key [,func])`  
  The "set" counterpart of `adaptGet`.

- `ptrAdd(...)`  
  Coerces all of its arguments to the WASM pointer type, adds them
  together, and returns a result of that same type.  This is a
  workaround for mixed-BigInt/Number pointer math being illegal in JS.

The `structDescription` argument is described in detail in the
following section.

<a id='struct-descr'></a>
### Struct Description Object

C structs are described in a JSON-friendly format:

>  
```json
{
  "name": "MyStruct",
  "sizeof": 16,
  "members": {
    "member1": {"offset": 0,"sizeof": 4,"signature": "i"},
    "member2": {"offset": 4,"sizeof": 4,"signature": "p"},
    "member3": {"offset": 8,"sizeof": 8,"signature": "j"}
  }
}
```

Forewarning: these data _must_ match up with the C-side definition of
the struct (if any). See [Appendix G][appendix-g] for one way to
easily generate these from C code.

Every struct must have a `sizeof`. (Though we _could_ calculate it
based on the list of members, we don't. Actually, we do, then we throw
if the values don't match up.) The `name` is required as well but it
may optionally be passed as the first argument to
`StructBinder(structName,structDescription)`. the `name` property
represents the member's symbolic name, typically its native name.

Abstractly speaking, a struct description in an object with the
properties `sizeof`, `offset`, and either `signature` _or_
`member`. `offset` is optional only in the top-most object of a struct
description. Every sub-object (a.k.a. member description object)
requires the `offset` property.

Member description objects are those in the `members` property:

`"members": {"memberName": {...member description...}, ...}`

A struct description which has its own `members` object represents a
nested struct, with an identical description syntax to that of a
top-level struct except that nested structs require an `offset`
property.

Each entry in a struct/member description object maps the member's
name to its low-level layout and other metadata:

- `offset`  
  The byte offset from the start of the struct, as reported by C's
  `offsetof()` feature. For nested structs's members, this value is
  relative to the nested struct, not the parent struct.
- `sizeof`  
  As reported by C's `sizeof()`.
- `signature`  
  A type-id signature for this member. Described below.
- `readOnly [=false]`  
  Optional boolean. If set to true, the binding layer will throw if JS
  code tries to set that property.
- `zeroOnDispose [=false]`  
  If true, then the library will zero out the memory of instances of
  this struct when their `dispose()` method is called. Because
  `StructType.dispose()` does not free instances which wrap
  externally-provided memory, those instances are not wiped when
  disposed (doing so would likely interfere with other users of that
  memory). (There is no need for a `zeroOnAlloc` counterpart because
  newly-allocated instances are always zero-filled for sanity's
  sake.)
- `members`  
  This object describes the individual struct members, mapping their
  names to a member description object. `members` gets processed
  recursively. Any member with its own `members` property is a
  nested-struct, and the property accessor for such members will
  return an instance of the distinct StructType which wraps that
  member's native memory.  
  Nested-struct members cannot be assigned over. `signature` is
  illegal if `members` is set.
- `get`  
  Optional function. When fetching this member, this getter is passed
  `(K,V)`, where `K` is the struct member's key and `V` is the
  _native_ value. `get()`'s return value becomes the value of the
  property access operation. This enables custom "native-to-JS"
  conversions. If the member is a nested struct, the value passed to
  the getter is a StructType proxy object which provides access to its
  own members, a subset of the parent object's memory. In the context
  of the getter call, "this" is the object upon which the get is being
  performed.
- `set`  
  Optional function. When setting this member, this setter is passed
  `(K,V)`, where `K` is the struct member's key and `V` is the _JS_
  value the property is being assigned to. The `set()` return value is
  assigned to the _native_ struct member.  Thus `set()` _must_ return
  an appropriate numeric value and can perform "JS-to-native"
  conversions. `set` is not currently legal for nested struct values,
  but it is on their own non-nested-struct members. In the context of
  the setter call, "this" is the object upon which the set is being
  performed.
- `adaptGet` and `adaptSet`  
  JSON-friendly variants of `get` and `set`. Each may be assigned a
  string value, and each such string must be mapped with
  `StructBinder.adaptGet(key,func)`
  resp. `StructBinder.adaptSet(key,func)`. With that in place, these
  behave like `get` resp. `set`.
- `structName`  
  Optional descriptive name, possibly distinct from the `name`,
  primarily used for nested structs. The intent is that this be some
  form of the struct type's name, optionally with leading parts of
  this object is a nested struct.
- `name`  
  Is usually optional, and is always optional in `members` entries
  because their name is conveniently derived from their containing
  object. `name` must be provided only for the top-most struct. The
  intent is that `name` maps to the member's property name and that
  `structName` optionally be set for nested structs (it will be
  derived from the name if it's not set).

The order of the `members` entries is not important: their memory
layout is determined by their `offset` and `sizeof` members. The
`name` property is technically optional, but one of the steps in the
binding process requires that either it be passed an explicit name or
there be one in the struct description. The names of the `members`
entries need not match their C counterparts. Project conventions may
call for giving them different names in the JS side and the
[StructBinderFactory][] can be configured to automatically add a
prefix and/or suffix to their names.

Struct member "signatures" describe the data types of the members and
are an extended variant of the format used by Emscripten's
`addFunction()`. A signature for a non-function-pointer member, or
function pointer member which is to be modelled as an opaque pointer,
is a single letter. A signature for a function pointer may also be
modelled as a series of letters describing the call signature. The
supported letters are:

- **`v`** = `void` (only used as return type for function pointer members)
- **`i`** = `int32` (4 bytes)
- **`j`** = `int64` (8 bytes) is only really usable if this code is built
  with BigInt support (e.g. using the Emscripten `-sWASM_BIGINT` build
  flag). Without that, this API may throw when encountering the `j`
  signature entry.
- **`f`** = `float` (4 bytes)
- **`d`** = `double` (8 bytes)
- **`c`** = `int8` (1 byte) char - see notes below!
- **`C`** = `uint8` (1 byte) unsigned char - see notes below!
- **`p`** = `int32` (see notes below!)
- **`P`** = Like `p` but with extra handling. Described below.
- **`s`** = like `int32` but is a _hint_ that it's a pointer to a
  string so that _some_ (very limited) contexts may treat it as such,
  noting that such algorithms must, for lack of information to the
  contrary, assume both that the encoding is UTF-8 and that the
  pointer's member is NUL-terminated. If that is _not_ the case for a
  given string member, do not use `s`: use `i` or `p` instead and do
  any string handling yourself.

Noting that:

- **All of these types are numeric**. Attempting to set any
  struct-bound property to a non-numeric value will trigger an
  exception except in cases explicitly noted otherwise.
- **"Char" types**: WASM does not define an `int8` type, nor does its
  JS representation distinguish between signed and unsigned. This API
  treats `c` as `int8` and `C` as `uint8` for purposes of getting and
  setting values when using the `DataView` class. It is _not_
  recommended that client code use these types in new WASM-capable
  code, but they were added for the sake of binding some immutable
  legacy code to WASM.

> Sidebar: Emscripten's public docs do not mention `p`, but their
generated code includes `p` as an alias for `i`, presumably to mean
"pointer". Though `i` is legal for pointer types in the signature, `p`
is more descriptive, so this framework encourages the use of `p` for
pointer-type members. Using `p` for pointers also helps future-proof
the signatures against the eventuality that WASM eventually supports
64-bit pointers. Note that sometimes `p` _really_ means a
pointer-to-pointer. We simply have to be aware of when we need to deal
with pointers and pointers-to-pointers in JS code.

> Trivia: this API treates `p` as distinctly different from `i` in
some contexts, so its use is encouraged for pointer types.

Signatures in the form `x(...)` denote function-pointer members and
`x` denotes non-function members. Functions with no arguments use the
form `x()`. For function-type signatures, the strings are formulated
such that they can be passed to Emscripten's `addFunction()` after
stripping out the `(` and `)` characters. For good measure, to match
the public Emscripten docs, `p`, `c`, and `C`, should also be replaced
with `i`. In JavaScript that might look like:

>  
```
signature.replace(/[^vipPsjfdcC]/g,'').replace(/[pPscC]/g,'i');
```


<a id='api-structtype'></a>
API: Struct Type
------------------------------------------------------------

The StructType class is a property of the [StructBinder][] function.

Each constructor created by a [StructBinder][] inherits from _its own
instance_ of the StructType class, which contains state specific to
that struct type (e.g. the struct name and description metadata).
StructTypes which are created via different [StructBinder][] instances
are unrelated to each other, sharing no state except [StructBinderFactory][]
config options.

The StructType constructor cannot be called from client code. It is
only called by the [StructBinder][]-generated
[constructors][StructCtor]. The `StructBinder.StructType` object
has the following "static" properties (^Which are accessible from
individual instances via `theInstance.constructor`.):

- `addOnDispose(...value)`\  
  If this object has no `ondispose` property, this function creates it
  as an array and pushes the given value(s) onto it. If the object has
  a function-typed `ondispose` property, this call replaces it with an
  array and moves that function into the array. In all other cases,
  `ondispose` is assumed to be an array and the argument(s) is/are
  appended to it. Returns `this`. See `dispose()`, below, for where
  this applies.

- `allocCString(str)`  
  Identical to the [StructBinder][] method of the same name.

- `hasExternalPointer(object)`  
  Returns true if the given object's `pointer` member refers to an
  "external" object. That is the case when a pointer is passed to a
  [struct's constructor][StructCtor]. If true, the memory is owned by
  someone other than the object and must outlive the object.

- `isA(value)`  
  Returns true if its argument is a StructType instance _from the same
  [StructBinder][]_ as this StructType.

- `memberKey(string)`  
  Returns the given string wrapped in the configured `memberPrefix`
  and `memberSuffix` values. e.g. if passed `"x"` and `memberPrefix`
  is `"$"` then it returns `"$x"`. This does not verify that the
  property is actually a struct a member, it simply transforms the
  given string.  TODO(?): add a 2nd parameter indicating whether it
  should validate that it's a known member name.

The base StructType prototype has the following members, all of which
are inherited by [struct instances](#api-structinstance) and may only
legally be used with concrete struct instances unless noted otherwise:

- `dispose()`  
  Frees, if appropriate, the WASM-allocated memory which is allocated
  by the constructor. If this is not called before the JS engine
  cleans up the object, a leak in the WASM heap memory pool will result.  
  When `dispose()` is called, if the object has a property named `ondispose`
  then it is treated as follows:  
  - If it is a function, it is called with the struct object as its
    `this`.  That method must not throw - if it does, the exception
    will be ignored.
  - If it is an array, it may contain functions, pointers, other
    [StructType][] instances, and/or JS strings. If an entry is a
    function, it is called as described above. If it's a number, it's
    assumed to be a pointer and is passed to the `dealloc()` function
    configured for the parent [StructBinder][]. If it's a
    [StructType][] instance then its `dispose()` method is called. If
    it's a JS string, it's assumed to be a helpful description of the
    next entry in the list and is simply ignored. Strings are
    supported primarily for use as debugging information.
  - Some struct APIs will manipulate the `ondispose` member, creating
    it as an array or converting it from a function to array as
    needed. Most simply, `addOnDispose()` is used to manipulate the
    on-dispose data.

- `extraBytes` (integer, read-only)  
  If this instance was allocated with the `extraBytes` option, this is
  that value, else it is 0.

- `lookupMember(memberName,throwIfNotFound=true)`  
  Given the name of a mapped struct member, it returns the member
  description object. If not found, it either throws (if the 2nd
  argument is true) or returns `undefined` (if the second argument is
  false). The first argument may be either the member name as it is
  mapped in the struct description or that same name with the
  configured `memberPrefix` and `memberSuffix` applied, noting that
  the lookup in the former case is faster.\  
  This method may be called directly on the prototype, without a
  struct instance.

- `memberToJsString(memberName)`  
  Uses `this.lookupMember(memberName,true)` to look up the given
  member. If its signature is `s` then it is assumed to refer to a
  NUL-terminated, UTF-8-encoded string and its memory is decoded as
  such. If its signature is not one of those then an exception is
  thrown.  If its address is 0, `null` is returned. See also:
  `setMemberCString()`.

- `memberIsString(memberName [,throwIfNotFound=true])`  
  Uses `this.lookupMember(memberName,throwIfNotFound)` to look up the
  given member. Returns the member description object if the member
  has a signature of `s`, else returns false. If the given member is
  not found, it throws if the 2nd argument is true, else it returns
  false.

- `memberKey(string)`  
  Works identically to `StructBinder.StructType.memberKey()`.

- `memberKeys()`  
  Returns an array of the names of the properties of this object
  which refer to C-side struct counterparts.

- `memberSignature(memberName [,emscriptenFormat=false])`  
  Returns the signature for a given a member property, either in this
  framework's format or, if passed a truthy 2nd argument, in a format
  suitable for the 2nd argument to Emscripten's `addFunction()`.
  Throws if the first argument does not resolve to a struct-bound
  member name. The member name is resolved using `this.lookupMember()`
  and throws if the member is found mapped.

- `memoryDump()`  
  Returns a Uint8Array which contains the current state of this
  object's raw memory buffer. Potentially useful for debugging, but
  not much else. The memory is necessarily, for compatibility with C,
  written in the host platform's endianness. Since all WASM is
  little-endian, it's the same everywhere. Even so: it should not be
  used as a persistent serialization format because (A) any changes to
  the struct will invalidate older serialized data and (B) serializing
  member pointers is useless.

- `pointer` (number, read-only)  
  A read-only numeric property which is the "pointer" returned by the
  configured allocator when this object is constructed. After
  `dispose()` (inherited from [StructType][]) is called, this property
  has the `undefined` value. When calling C-side code which takes a
  pointer to a struct of this type, simply pass it `myStruct.pointer`.
  Whether this member is of type Number or BigInt depends on whether
  the WASM environment is 32-bit (Number) or 64-bit (BigInt).

- `ptrAdd(args...)`  
  Equivalent to [StructBinder][]`.ptrAdd(this.pointer, args...)`
  or [StructCtor][]`.ptrAdd(this.pointer, args...)`.

- `setMemberCString(memberName,str)`  
  Uses `StructType.allocCString()` to allocate a new C-style string,
  assign it to the given member, and add the new string to this
  object's `ondispose` list for cleanup when `this.dispose()` is
  called. This function throws if `lookupMember()` fails for the given
  member name, if allocation of the string fails, or if the member has
  a signature value of anything other than `s`. Returns `this`.  
  *Achtung*: calling this repeatedly will not immediately free the
  previous values because this code cannot know whether they are in
  use in other places, namely C. Instead, each time this is called,
  the prior value is retained in the `ondispose` list for cleanup when
  the struct is disposed of. Because of the complexities and general
  uncertainties of memory ownership and lifetime in such
  constellations, it is recommended that the use of C-string members
  from JS be kept to a minimum or that the relationship be one-way:
  let C manage the strings and only fetch them from JS using, e.g.,
  `memberToJsString()`.

- `zeroOnDispose` (bool, read-only)  
  True if this instance or its prototype were configured with
  the `zeroOnDispose` flag.


<a id='api-structctor'></a>
API: Struct Constructors
------------------------------------------------------------

Struct constructors (the functions returned from [StructBinder][])
are used for, intuitively enough, creating new instances of a given
struct type:

>  
```
const x = new MyStruct;
```

Normally they should be passed no arguments, but they optionally
accept a single argument: a WASM heap pointer address of memory which
the object will use for storage. It does _not_ take over ownership of
that memory and that memory must remain valid for at least as long as
this struct instance. This is used, for example, to proxy
static/shared C-side instances or those which we simply want to
get access to via this higher-level API for a while:

>  
```
const x = new MyStruct( someCFuncWhichReturnsAMyStructPointer() );
...
x.dispose(); // does NOT free or zero the memory
```

The JS-side construct does not own the memory in that case and has no
way of knowing when the C-side struct is destroyed. Results are
specifically undefined if the JS-side struct is used after the C-side
struct's member is freed. However, if the client has allocated that
memory themselves and wants to transfer ownership of it to the
instance, that can be done with:

>  
```
x.addOnDispose( thePtr );
```

Which will free `thePtr` when `x.dispose()` is called.

As of 2025-11-10, a third option is available:

>  
```
const x = new MyStruct({
  wrap: ptr,            // as per MyStruct(ptr)
  takeOwnership: bool,  // If true, take ownership of the wrap ptr.
  zeroOnDispose: bool , // if true, overrides MyStruct.structInfo.zeroOnDispose
  extraBytes: int       // bytes to alloc after the end of the struct
});
```

- If `wrap` is set then (A) it must be at least
  `MyStruct.structInfo.sizeof` of memory owned by the caller, (B) it
  _must_ have been allocated using the same allocator as Jaccwabyt is
  configured for, and (C) ownership of it is transfered to the new
  object. `zeroOnDispose` and `extraBytes` are disregarded if `wrap`
  is set. A falsy `wrap` value is equivalent to not providing one and
  a non-integer value, or a number less than 0, triggers an error.

- If `takeOwnership` is truthy then this object takes over ownership
  of the `wrap` pointer (if any). This flag is otherwise ignored.

- If `zeroOnDispose` or `extraBytes` are are used without `wrap`, each
  which is used is set as a read-only propperty on the resulting
  `MyStruct` object, except that `zeroOnDispose` is only recorded if
  it is truthy and `extraBytes` is only recorded if it is not 0 (a
  negative value, or non-integer, triggers an exception).

- `ondispose`: if set it is passed to the new object's
  `addOnDispose()` before the constructor returns, so may have any
  value legal for that method. Results are undefined if `ondispose`
  and `wrap` have the same pointer value (because `wrap` will already
  be cleaned up via `dispose()`, as if it had been passed to
  `addOnDispose()`).

In the case of `extraBytes`, a pointer to the tail of the memory can
be obtained by adding `theInstance.pointer` to
`theInstance.structInfo.sizeof`, with the caveat that `pointer` may be
a BigInt value (on 64-bit WASM), `sizeof` will be a Number and,
spoiler alert, `(BigInt(1) + Number(1))` is not legal. Thus this API
adds a small convenience method to work around that portability issue:

>  
```
const ptrTail = MyStruct.ptrAdd(theInstance.pointer, theInstance.extraBytes);
```

`typeof ptrTail` is `'bigint'` in 64-bit builds and `'number'` in
32-bit.  i.e. it's the same type as `theInstance.pointer`.
Equivalently, the inherited `MyStruct` instance method with the same
name adds an instance's own `pointer` value to its arguments:

```  
const ptrTail = theInstance.ptrAdd(theInstance.extraBytes);
```

These constructors have the following "static" members:

- `isA(value)`  
  Returns true if its argument was created by this constructor.

- `memberKey(string)`  
  Works exactly as documented for [StructType][].

- `memberKeys(string)`  
  Works exactly as documented for [StructType][].

- `ptrAdd(args...)`  
  Equivalent to [StructBinder][]`.ptrAdd(args...)`. The [_inherited_
  method with the same name][StructType] behaves differently.

- `structInfo`  
  The structure description passed to [StructBinder][] when this
  constructor was generated.

- `structName`  
  The structure name passed to [StructBinder][] when this constructor
  was generated.


<a id='api-structprototype'></a>
API: Struct Prototypes
------------------------------------------------------------

The prototypes of structs created via [the constructors described in
the previous section][StructCtor] are each a struct-type-specific
instance of [StructType][] and add the following struct-type-specific
properties to the mix:

- `structInfo`  
  The struct description metadata, as it was given to the
  [StructBinder][] which created this class.

- `structName`  
  The name of the struct, as it was given to the [StructBinder][] which
  created this class.

<a id='api-structinstance'></a>
API: Struct Instances
------------------------------------------------------------------------

Instances of structs created via [the constructors described
above][StructCtor]. Each inherits all of the methods and properties
from their constructor's prototype.

<a id='appendices'></a>
Appendices
============================================================

<a id='appendix-a'></a>
Appendix A: Limitations, TODOs, and Non-TODOs
------------------------------------------------------------

- This library only supports the basic set of member types supported
  by WASM: numbers (which includes pointers).

- Binding JS functions to struct instances, such that C can see and
  call JS-defined functions, is not as transparent as it really could
  be.  [The WhWasmUtil API][whwasmutil.js], and
  standalone subproject co-developed with Jaccwabyt, provides such a
  binding mechanism. There is some overlap between the two APIs and
  they arguably belong bundled together, but doing so would more than
  triple Jaccwabyt's size. (That said, the only known Jaccwabyt
  deployment uses both APIs, so maybe it's time to merge them (he says
  on 2025-11-10). As of this writing, jaccwabyt.js is 38k, half of
  which is comments/docs, whereas whwasmutil.js is 100kb and 75%
  docs).

<a id='appendix-b'></a>
Appendix B: Build
------------------------------------------------------------------------

In order to support both vanilla JS and ESM (ES6 module) builds from a
single source, this project uses [a preprocessor][c-pp], which requires
only a C compiler:

> $ make

The makefile requires GNU make, not BSD or POSIX make.

The resulting files are in the `js/` subdirectory, in both "vanilla"
JS and ESM formats.

This tree [includes all of the requisite sources](/dir/tool) and
requires no out-of-tree dependencies beyond the system's libc.


<a id='appendix-d'></a>
Appendix D: Debug Info
------------------------------------------------------------

The [StructBinderFactory][], [StructBinder][], and [StructType][] classes
all have the following "unsupported" method intended primarily
to assist in their own development, as opposed to being for use in
client code:

- `debugFlags(flags)` (integer)  
  An "unsupported" debugging option which may change or be removed at
  any time. Its argument is a set of flags to enable/disable certain
  debug/tracing output for property accessors: 0x01 for getters, 0x02
  for setters, 0x04 for allocations, 0x08 for deallocations. Pass 0 to
  disable all flags and pass a negative value to _completely_ clear
  all flags. The latter has the side effect of telling the flags to be
  inherited from the next-higher-up class in the hierarchy, with
  [StructBinderFactory][] being top-most, followed by [StructBinder][], then
  [StructType][].


<a id='appendix-g'></a>
Appendix G: Generating Struct Descriptions From C
------------------------------------------------------------

Struct definitions are _ideally_ generated from WASM-compiled C, as
opposed to simply guessing the sizeofs and offsets, so that the sizeof
and offset information can be collected using C's `sizeof()` and
`offsetof()` features (noting that struct padding may impact offsets
in ways which might not be immediately obvious, so writing them by
hand is _most certainly not recommended_).

How exactly the desciption is generated is necessarily
project-dependent. It's tempting say, "oh, that's easy! We'll just
write it by hand!" but that would be folly. The struct sizes and byte
offsets into the struct _must_ be precisely how C-side code sees the
struct or the runtime results are completely undefined.

The approach used in developing and testing _this_ software is...

Below is a complete copy/pastable example of how we can use a small
set of macros to generate struct descriptions from C99 or later into
static string memory. Simply add such a file to your WASM build,
arrange for its function to be exported[^export-func], and call it
from JS (noting that it requires environment-specific JS glue to
convert the returned pointer to a JS-side string). Use `JSON.parse()`
to process it, then feed the included struct descriptions into the
binder factory at your leisure.

------------------------------------------------------------

```c
#include <string.h> /* memset() */
#include <stddef.h> /* offsetof() */
#include <stdio.h>  /* snprintf() */
#include <stdint.h> /* int64_t */
#include <assert.h>

struct ExampleStruct {
  int v4;
  void * ppV;
  int64_t v8;
  void (*xFunc)(void*);
};
typedef struct ExampleStruct ExampleStruct;

const char * wasm__ctype_json(void){
  static char strBuf[512 * 8] = {0}
    /* Static buffer which must be sized large enough for
       our JSON. The string-generation macros try very
       hard to assert() if this buffer is too small. */;
  int n = 0, structCount = 0 /* counters for the macros */;
  char * pos = &strBuf[1]
    /* Write-position cursor. Skip the first byte for now to help
       protect against a small race condition */;
  char const * const zEnd = pos + sizeof(strBuf)
    /* one-past-the-end cursor (virtual EOF) */;
  if(strBuf[0]) return strBuf; // Was set up in a previous call.

  ////////////////////////////////////////////////////////////////////
  // First we need to build up our macro framework...

  ////////////////////////////////////////////////////////////////////
  // Core output-generating macros...
#define lenCheck assert(pos < zEnd - 100)
#define outf(format,...) \
  pos += snprintf(pos, ((size_t)(zEnd - pos)), format, __VA_ARGS__); \
  lenCheck
#define out(TXT) outf("%s",TXT)
#define CloseBrace(LEVEL) \
  assert(LEVEL<5); memset(pos, '}', LEVEL); pos+=LEVEL; lenCheck

  ////////////////////////////////////////////////////////////////////
  // Macros for emitting StructBinders...
#define StructBinder__(TYPE)                 \
  n = 0;                                     \
  outf("%s{", (structCount++ ? ", " : ""));  \
  out("\"name\": \"" # TYPE "\",");          \
  outf("\"sizeof\": %d", (int)sizeof(TYPE)); \
  out(",\"members\": {");
#define StructBinder_(T) StructBinder__(T)
// ^^^ extra indirection needed to expand CurrentStruct
#define StructBinder StructBinder_(CurrentStruct)
#define _StructBinder CloseBrace(2)
#define M(MEMBER,SIG)                                         \
  outf("%s\"%s\": "                                           \
       "{\"offset\":%d,\"sizeof\": %d,\"signature\":\"%s\"}", \
       (n++ ? ", " : ""), #MEMBER,                            \
       (int)offsetof(CurrentStruct,MEMBER),                   \
       (int)sizeof(((CurrentStruct*)0)->MEMBER),              \
       SIG)
  // End of macros.
  ////////////////////////////////////////////////////////////////////

  ////////////////////////////////////////////////////////////////////
  // With that out of the way, we can do what we came here to do.
  out("\"structs\": ["); {

// For each struct description, do...
#define CurrentStruct ExampleStruct
    StructBinder {
      M(v4,"i");
      M(ppV,"p");
      M(v8,"j");
      M(xFunc,"v(p)");
    } _StructBinder;
#undef CurrentStruct

  } out( "]"/*structs*/);
  ////////////////////////////////////////////////////////////////////
  // Done! Finalize the output...
  out("}"/*top-level wrapper*/);
  *pos = 0;
  strBuf[0] = '{'/*end of the race-condition workaround*/;
  return strBuf;

// If this file will ever be concatenated or #included with others,
// it's good practice to clean up our macros:
#undef StructBinder
#undef StructBinder_
#undef StructBinder__
#undef M
#undef _StructBinder
#undef CloseBrace
#undef out
#undef outf
#undef lenCheck
}
```

------------------------------------------------------------

<style>
div.content {
  counter-reset: h1 -1;
}
div.content h1, div.content h2, div.content h3 {
  border-radius: 0.25em;
  border-bottom: 1px solid #70707070;
}
div.content h1 {
  counter-reset: h2;
}
div.content h1::before, div.content h2::before, div.content h3::before {
  background-color: #a5a5a570;
  margin-right: 0.5em;
  border-radius: 0.25em;
}
div.content h1::before {
  counter-increment: h1;
  content: counter(h1) ;
  padding: 0 0.5em;
  border-radius: 0.25em;
}
div.content h2::before {
  counter-increment: h2;
  content: counter(h1) "." counter(h2);
  padding: 0 0.5em 0 1.75em;
  border-radius: 0.25em;
}
div.content h2 {
  counter-reset: h3;
}
div.content h3::before {
  counter-increment: h3;
  content: counter(h1) "." counter(h2) "." counter(h3);
  padding: 0 0.5em 0 2.5em;
}
div.content h3 {border-left-width: 2.5em}
</style>

[appendix-g]: #appendix-g
[BigInt64Array]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/BigInt64Array
[c-pp]: https://fossil.wanderinghorse.net/r/c-pp
[Emscripten]: https://emscripten.org
[jaccwabyt.js]: /file/jaccwabyt/jaccwabyt.c-pp.js
[MDN]: https://developer.mozilla.org/docs/Web/API
[sgb]: https://wanderinghorse.net/home/stephan/
[sqlite3]: https://sqlite.org
[StructBinder]: #api-structbinder
[StructBinderFactory]: #api-binderfactory
[StructCtor]: #api-structctor
[StructInstance]: #api-structinstance
[StructType]: #api-structtype
[TextDecoder]: https://developer.mozilla.org/en-US/docs/Web/API/TextDecoder
[TextEncoder]: https://developer.mozilla.org/en-US/docs/Web/API/TextEncoder
[WASI-SDK]: https://github.com/WebAssembly/wasi-sdk
[whwasmutil.js]: /file/wasmutil/whwasmutil.c-pp.js

[^export-func]: In Emscripten, add its name, prefixed with `_`, to the
  project's `EXPORT_FUNCTIONS` list.
