Jaccwabyt 🐇
====================

**Jaccwabyt**: _JavaScript ⇄ C Struct Communication via WASM Byte
Arrays_[^the-name]

Forewarning: if this is not being read from [its canonical home][src]
then some of the markdown formatting and links will not work.

[src]: https://fossil.wanderinghorse.net/r/c-pp
[^the-name]: Getting that name to work required some acrobatics, but
    the result was worth it.

<a id='overview'></a>
Table of Contents
====================

- [Brief Demo](#demo)
- [Architecture and Mental Model](#arch)
- [Struct Descriptors](#descriptors)
  - [Type signatures](#type-signatures)
     - [Type "s"](#type-s)
     - [Type "p" vs. "P"](#type-p)
  - [Adaptors](#adaptors)
- Class APIs
  - [StructBinderFactory][]: environment configuration
  - [StructBinder][]: struct codec generator
  - [StructType][]
     - [Constructor][StructCtor]
     - [Struct instances][StructInstance]
- Using the Classes
  - [StructBinder](#using-StructBinder)
  - [StructCtor](#using-StructCtor)
  - [StructType Instances](#using-StructInstance)
     - [Lifecycle and Memory Management](#lifecycle)
- [Nested Structs](#nested-structs)
- [64-bit WASM](#64bit)
- Appendices
  - [A: Limitations, TODOs, etc.](#appendix-a)
  - [B: Build](#appendix-b)
  - [D: Debug Info](#appendix-d)
  - [G: Generating Struct Descriptors](#appendix-g)
  - [L: License](#appendix-l)

-----

Welcome to Jaccwabyt, a JavaScript API which creates bindings for
WASM-compiled C structs, permitting two-way interchange of WASM-hosted
struct state with very little client-side friction.

(If that means nothing to you, neither will the rest of this page!)

To the best of its creator's fallible knowledge, Jaccwabyt is the only
library of its kind (as of 2025-11). It is a core component of the
[SQLite project's JS/WASM bindings](https://sqlite.org/wasm), where it
is binds JS-side virtual filesystems and virtual database tables to
WASM, and is known to work reliably.

Formalities:

- Canonical source code home: [][src]
- License: [SQLite Blessing](#appendix-l)
- Maintainer: [Stephan Beal](https://wanderinghorse.net)

-----
<a id='demo'></a>
Brief Usage Overview
=============================

Client-side, Jaccwabyt looks like...

>  
```javascript
// 1. Initialize for the current WASM environment. Needed once
//    per app session.
const StructBinder = StructBinderFactory({ 
  memberPrefix: "$",
  ... config info from the WASM environment
});

// 2. Convert a JSON-friendly struct descriptor into
//    a JS constructor. Needed once per struct type.
const StructCtor = StructBinder( structDescriptorObject );

// 3. Instiate an instance which wraps a WASM-hosted instance.
const s = new StructCtor( funcReturningAWasmPointer() );

// 4. Access the C-side members...
s.$x = 3;                 // Write to WASM-side Struct::x
console.debug(s.$y);      // Read from WASM-side Struct::y
console.debug(s.pointer); // WASM pointer value

// 5. Clean up (important!)
s.dispose(); // Memory ownership is configurable.
             // s.pointer is now falsy
```

-----
<a id='arch'></a>
Architecture and Mental Model
=============================

Jaccwabyt is made up of several layers:

- [StructBinderFactory][] takes a configuration describing the current
  WASM environment, e.g. Emscripten or WASI-SDK. Its result is...
- [StructBinder][] converts JSON-format struct descriptors into...
- [StructType][] are constructors for...
- [StructInstance][] is a colloquial term for StructType-derived
  objects.

In graphics, it looks something like:

```pikchr center
BSBF: box "StructBinderFactory" fit fill lightblue
BSB: box same "StructBinder" fit at 0.75 e of 0.9 s of BSBF.c
BSM: box rad 0.4*boxht "JSON-format C Structs" fit fill lightblue at 1.5s of BSBF
BST: box same "StructType<T>" fit at 1.5 e of BSBF
BSC: box same "Struct<T>" "Ctor" fit at 1.75 s of BST
BSI: box same "Struct<T>" "Instances" fit at 1.25 right of BSB.e
BC: box same at 0.25 right of 1.6 e of BST "C Structs" fit fill lightgrey

arrow -> from BSM.n to BSB.sw "Input" aligned above dotted
arrow -> from BSBF.s to BSB.nw "Generates" aligned above
arrow -> from BSB.n to BST.sw "Generates" aligned above
arrow -> from BSB.s to BSC.nw "Generates" aligned below
arrow -> from BSC.ne to BSI.s "Constructs" aligned below
arrow <-> from BSI.e to BC.s dotted "WASM heap" aligned below
arrow -> from BST.e to BC.w dotted "Mirrors Struct" aligned above "Model From" aligned below
arrow -> from BST.s to BSC.n "Prototype of" aligned above
```


-----

<a id='descriptors'></a>
Struct Descriptors
====================

Before making use of Jaccwabyt we need so-called descriptors for C
structs, JSON-friendly objects which describe their memory layout.
Jaccwabyt converts these into codecs for reading and writing WASM-side
representations of instances of those structs.

For example, given this C struct:

>  
```c
// C-side:
struct MyStruct {
  int member1;
  void * member2;
  int64_t member3;
};
```

Its JSON-format description might (depending on the build environment
and target) look like:

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
the struct (if any). See [](#appendix-g) for one way to
easily generate these from C code.

Every struct must have a `sizeof` (^Though we _could_ calculate the
sizeof based on the list of members, we don't. Actually, we do, then
we throw an exception if the values don't match up.). The `name` is
required as well but it may optionally be passed as the first argument
to `StructBinder(structName,structDescriptor)`. the `name` property
represents the member's symbolic name, typically (but not necessarily)
its native name.

Abstractly speaking, a struct descriptor in an object with the
properties `sizeof`, `offset`, and either `signature` _or_
`members`. `offset` is optional only in the top-most object of a struct
description. Every sub-object (a.k.a. member descriptor object)
requires the `offset` property.

<a id="member-descr"></a>
Member descriptor objects are those in the `members` property:

`"members": {"memberName": {...member descriptor...}, ...}`

A struct descriptor which has a `members` object represents a nested
struct, the interior of which follows the same struct descriptor
format as a top-level struct except that nested structs require an
`offset` property.

Each entry in a struct/member descriptor object maps the member's
name to its low-level layout and other metadata:

- `offset` (Number)   
  The byte offset from the start of the struct, as reported by C's
  `offsetof()` feature. For nested structs' members, this value is
  relative to the nested struct, not the parent struct.
- `sizeof` (Number)  
  As reported by C's `sizeof()`.
- `signature` (String)  
  A type-id signature for this member. See [](#type-signatures).
- `readOnly` (bool=false)  
  Optional boolean. If set to truthy, the binding layer will throw if
  JS code tries to set that property.
- `zeroOnDispose` (bool=false)  
  If truthy, then the library will zero out the memory of instances of
  this struct when their `dispose()` method is called. Because
  `StructType.dispose()` does not free instances which wrap
  externally-provided memory, those instances are not wiped when
  disposed (doing so would likely interfere with other users of that
  memory). (There is no need for a `zeroOnAlloc` counterpart because
  newly-allocated instances are always zero-filled for sanity's sake.)
- `members` (Object)  
  This object describes the individual struct members, mapping their
  names to a member descriptor object. `members` gets processed
  recursively. Any member with its own `members` property is a
  [nested-struct](#nested-structs), and the property accessor for such
  members will return an instance of the distinct StructType which
  wraps that
  member's native memory.  
  Nested-struct members cannot be assigned over. `signature` is
  illegal if `members` is set. The following properties are relevant
  only for `members` entries, not top-level structs:
   - `getter` See [](#adaptors)
   - `setter` See [](#adaptors)
- `name`  
  Is usually optional, and is always optional in `members` entries
  because their names are conveniently derived from their containing
  object. `name` must be provided only for the top-most struct. The
  intent is that `name` maps to the member's property name and that
  `structName` optionally be set for nested structs (it will be
  derived from the name if it's not set).
- `structName`  
  Optional descriptive name, possibly distinct from the `name`,
  primarily used for nested structs. The intent is that this be some
  form of the struct type's name, optionally fully-qualified with
  the containing structs' names.
- `autoCalcSizeOffset` (bool=false)  
  A "very nearly undocumented back door" which tells Jaccwabyt to
  automatically calculate member sizeofs and offsets for this struct
  and its sub-structs. This is provided _soley_ as a convenience to
  Jaccwabyt's own developer in testing using ad hoc JS structs which
  have no genuine C counterparts. This _must not_ be used with genuine
  C structs because it cannot account for padding. It _is_ safe (and
  terribly convenient) to use when there is no native counterpart, but
  genuine use cases for such a thing are probably limited to
  Jaccwabyt's own testing.

The order of the `members` entries is not important: their memory
layout is determined by their `offset` and `sizeof` members. The
`name` property is technically optional, but one of the steps in the
binding process requires that either it be passed an explicit name or
there be one in the struct descriptor. The names of the `members`
entries need not match their C counterparts. Project conventions may
call for giving them different names in the JS side and the
[StructBinderFactory][] can be configured to automatically add a
prefix and/or suffix to their names.

<a id='type-signatures'></a>
Type Signatures
--------------------

Struct member `signature` describes the data type of each member,
and uses an extended variant of the format used by Emscripten's
`addFunction()`. A signature for a non-function-pointer member, or
function pointer member which is to be modelled as an opaque pointer,
is a single letter. A signature for a function pointer may also be
modelled as a series of letters describing the call signature.

Signatures in the form `x(...)` denote function-pointer members and
the form `x` denotes non-function members. Functions with no arguments
use the form `x()`.

The supported signature letters are:

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
- **`p`** = `int32` (4 bytes) or `int64` (8 bytes). [Described
  below](#type-p).
- **`P`** = Like `p` but with extra handling for [StructType][]
  pointers. [Described below](#type-p).
- **`s`** = like `p` but is a _hint_ that it's a pointer to a string.
  [Described below](#type-s).

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


<a id='type-s'></a>
### Type `"s"` (C-string)

Type ID "s" is a special case of ["p"](#type-p). In some limited
contexts, described below, the library will treat them as C-strings,
with the caveat that that such algorithms must, for lack of
information to the contrary, assume that the encoding is UTF-8 and
that the pointer's member is NUL-terminated. If that is _not_ the case
for a given string member, do not use "s", use "p" instead and do any
string handling yourself.

The following APIs handle "s" specially:

- [StructType][].`setMemberCString()`
- [StructType][].`memberToJsString()`

[StructType][].`memberIsString()` returns true if a given member is
legal for those methods, and those methods both throw if
`memberIsString()` is false.


<a id='type-p'></a>
### Type `"p"` (pointer) vs `"P"` (pointer plus)

The signature letter `p` means "pointer", which, in WASM, means either
int32 or [int64][64bit]. A capital `P` changes the semantics of plain
member pointers (but not, as of this writing, function pointer
members) as follows:

When a `P`-type member is set via `myStruct.x=y`, if
[`(y instanceof StructType)`][StructType] then the value of
`y.pointer` is stored in `myStruct.x`. If `y` is neither a pointer nor a
[StructType][], an exception is triggered regardless of whether `p` or
`P` is used. `P` has no further effect when fetching a property, only
when setting.


<a id='adaptors'></a>
Adaptors
-------------------------------

Adaptors, an optional part of a [struct descriptor](#descriptors),
provide a way to customize the result of fetching struct properties
and the value which get written to their memory when they are
assigned. This enables transparent installation of custom "JS-to-WASM"
and "WASM-to-JS" conversions for struct properties.

The following optional [struct descriptor](#descriptors) properties
may be used on [Object-type entries of the `members`
property](#member-descr).

- `getter` (Function)  
  When fetching this struct member, this getter is passed `(K,V)`,
  where `K` is the struct member's name, _without_ any
  [prefix/suffix](#member-prefix), and `V` is the _native_
  value. `get()`'s return value becomes the value of the property
  access operation, enabling custom "native-to-JS" conversions. In the
  context of the getter call, "this" is the object upon which the get
  is being performed.  
  If the member is a [nested struct](#nested-structs), the value
  passed to the getter is a proxy object which provides access to that
  struct's own members, a subset of the parent object's memory.

- `getter` (String)  
  The JSON-friendly form of `getter`, this maps a client-provided key
  to a getter function. For this to work, the key used here must have
  been registered with [StructBinder][].`adaptGetter('getterKey',func)`
  before the struct descriptor is processed. The key used there is
  independent of any struct property names.

- `setter` (Function)  
  When setting this struct member, this setter is passed `(K,V)`,
  where `K` is the struct member's name, _without_ any
  [prefix/suffix](#member-prefix), and `V` is the _JS_ value the
  property is being assigned to. `set()`'s return value gets assigned
  to the _native_ struct member.  Thus `set()` _must_ return an
  appropriate numeric value and may perform arbitrary "JS-to-native"
  conversions.  In the context of the setter call, "this" is the
  object upon which the set is being performed.  
  `setter` is not currently legal for writing to a [nested
  struct](#nested-structs) member.

- `setter` (String)  
  The JSON-friendly form of `setter`, this works the same as the
  string form of `getter` except that the registration function is
  [StructBinder][].`adaptSetter('setterKey',func)`.


Getter and setter implementations ***MUST NOT*** try to look up that
struct property using Jaccwabyt's property interceptor or they will
cause endless recursion and a stack overflow.


A rather contrived example...

First, map getter/setter names to functions for use in the JSON
descriptors:

>  
```javascript
MyStructBinder.adaptGetter('truncateFloat',(k,v)=>v | 0);
MyStructBinder.adaptSetter('parseFloat',(k,v)=>{
  v = parseFloat(v);
  return isNan(v) ? 0 : v;
);
```

> Sidebar: most implementations will not need the key but it is
provided for cases which want to share an implementation across keys
and dispatch on their name. The key should arguably be the second
argument, allowing it to be elided from most implementations, but
experimentation with that caused more confusion than it solved.

With that in place, we can map those to members using something
like:

>  
```javascript
const MyStruct = MyStructBinder({
  ...
  memberPrefix: "$",
  members: {
    foo: {
      ...
      signature: "d", // => double
      getter: "truncateFloat",
      setter: "parseFloat"
    }
  }
});
```

Then fetching `x.$foo` will always return a 32-bit-truncated int and
assigning `x.$foo` will coerce its arguments to a number, making
`x.$foo="3"` both legal and well-behaved ("feature or bug?" is for the
implementor to decide).

> Sidebar: the test app demonstrates a struct member adaptor, for
  installing proxies for [nested structs](#nested-structs), but is not
  yet part of the public API. It also demonstrates how to plug
  adaptors into the struct descriptor "inline", but that's also not
  yet part of the public API.

-----
<a id='api'></a>
Classes and APIs
====================

<a id='StructBinderFactory'></a>
StructBinderFactory
-------------------

The top-most function of the API, from which all other functions and
types are generated. It accepts a configuration object to customize it
for a given WASM environment and returns a [StructBinder][] factory
function.

**Signature:**

* `Function StructBinderFactory(object configOptions)`

**Configuration Options:**

- `pointerSize` (integer)  
  Specify the WASM pointer size as `4` (32-bit) or `8` (64-bit). Any
  other truthy value triggers an exception. If omitted, it guesses the
  size by allocating 1 byte, checking the result type, and
  deallocating it.

- `heap` (WebAssembly.Memory | Function)  
  Must be either a `WebAssembly.Memory` instance representing the WASM
  heap OR a function returning an `Int8Array` or `Uint8Array` view of
  the WASM heap.

- `alloc` (Function)  
  Function semantically compatible with C's `malloc(3)`. Passed the
  number of bytes to allocate and returns a pointer. On failure,
  returns `0` (32-bit) or `0n` (64-bit) or throws an exception. Must
  use the same heap as `heap`.

- `dealloc` (Function)  
  Function semantically compatible with C's `free(3)`. Takes a pointer
  returned from `alloc()` and releases memory. Must accept
  `0`/`0n`/`null` as a no-op and must never throw.

- `bigIntEnabled` (boolean, optional)  
  Defaults to `true` if `BigInt64Array` is available, otherwise
  `false`. If true, WASM build must support int64.

- `memberPrefix` and `memberSuffix` (string, optional)  
  <a id='member-prefix'></a> String prefix/suffix applied when binding
  struct properties to JS.  e.g. the member `X` may be made accessible
  to JS as `$X` with a `memberPrefix` of `"$"`. A prefix is
  ***strongly recommended*** because symbol names may otherwise
  collide with other JS-side symbols, inherited or
  instance-specific. `"$"` is a good choice because it's a legal (but
  seldom-used) identifier character in JS, and `"$$"` would be even
  more explicit about the property being unusual in some way.

* `log` (Function, optional)  
  Logging function used for debugging output (defaults to
  `console.debug`).

-----

<a id='StructBinder'></a>
StructBinder
--------------------

Factories created by [StructBinderFactory][] which convert [struct
descriptions](#descriptors) into [StructType][] constructors.

See also: [](#using-StructBinder)

**Signature:**

- `Function StructBinder([string structName,] object structDescriptor)`

**Members & APIs:**

- `allocCString(str)`  
  Allocates a new UTF-8-encoded, NUL-terminated copy of the given JS
  string and returns its address relative to `config.heap()`. Throws
  if allocation fails. Ownership transfers to the caller, who must
  eventually deallocate it via `config.dealloc()`.

- `config` (Object)  
  The configuration object passed to [StructBinderFactory][].

- `adaptGetter(key [,func])`  
  Gets or sets a "getter adaptor" function by name.
  When called as a getter, returns a falsy value if no such getter
  is mapped. Getters mapped this way can be used by any structs
  generated by this `StructBinder`.  See [](#adaptors).

- `adaptSetter(key [,func])`  
  Gets or sets a "setter adaptor" function by name.
  When called as a getter, returns a falsy value if no such setter
  is mapped. Setters mapped this way can be used by any structs
  generated by this `StructBinder`.  See [](#adaptors).

- `ptrAdd(...)`  
  Coerces all arguments to the WASM pointer type, adds them together,
  and returns the resulting sum as the current WASM pointer type.


Quirks:

- It may modify `structDescriptor` its input object, recursively and
  perhaps extensively. Clients which do not want that should pass a
  JSON-serialized copy.

- When called with two arguments, if `structDescriptor.name` is not
  set, then it gets set to the first argument.

-----

<a id='StructType'></a>
StructType
--------------------

The base class from which all generated struct instances
inherit. Accessible via [StructBinder][].StructType.

### Static Members

- `allocCString(str)`  
  As documented for [StructBinder][].allocCString.

- `isA(value)`  
  Returns `true` if the argument is a [StructType][] instance from the
  same [StructBinder][].

- `memberKey(string)`  
  Returns the given string wrapped in the configured `memberPrefix`
  and `memberSuffix` values.


### Prototype / Instance APIs

- `addOnDispose(...value)`  
  Creates or appends to an [`ondispose` handler](#dispose) array on the
  object. Returns `this`.

- `dispose()`  
  Frees WASM-allocated heap memory owned by the object and executes
  registered `ondispose` cleanup handlers. See [](#dispose) for details.

- `extraBytes` (integer, read-only)  
  Extra bytes allocated along with this instance, or `0`/`0n`.

- `lookupMember(memberName [, throwIfNotFNotFound=true])`  
  Returns the member descriptor object. If not found, it throws if
  `throwIfNotFNotFound` is truthy, else it returns
  `undefined`. `memberName` may be in either the name from its [struct
  descriptor](#descriptors) or its JS property name, as modified by
  [the memberPrefix/memberSuffix options](#member-prefix). That is: if
  the prefix is `"$"` and the member name is `X` then this function
  accepts either `X` or `$X`.

- `memberToJsString(memberName)`  
  Decodes a NUL-terminated, UTF-8-encoded string at the address stored
  in a member with signature "s", and throws for any unknown or
  non-"s" member. `memberName` is interpreted as for `lookupMember()`.
  See [](#type-s).

- `memberIsString(memberName [, throwIfNotFNotFound=true])`  
  Returns the member descriptor object if signature is "s", else
  `false`. Both arguments are interpreted as for `lookupMember()`.

- `memberKey(string)`  
  As documented for [StructType][] static / [StructBinder][].`memberKey()`.

- `memberKeys()`  
  Returns an array of property names mapped to C struct members, without
  their [configured prefix/suffix](#member-prefix).

- `memberSignature(memberName [,throwIfNotFNotFound=true])`  
  Returns the signature for the given member. Both arguments are
  interpreted as for `lookupMember()`.

- `memoryDump()`  
  Returns a `Uint8Array` snapshot of the object's raw memory buffer,
  including any `extraBytes` memory requested via the constructor.

- `ownsOwnMemory()`  
  Returns `true` if `this.pointer` owns its WASM-side memory.

- `pointer` (Number \| BigInt, read-only)  
  Numeric WASM heap address allocated for the object, or `undefined`
  after disposal. This is of type `Number` in in 32-bit builds and
  `BigInt` in [64-bit](#64bit).

- `ptrAdd(args...)`  
  Adds the instance's `pointer` value to the arguments via
  `StructBinder.ptrAdd(this.pointer, args...)`.

- `setMemberCString(memberName, str)`  
  Allocates a C-string, assigns its address to the member, and
  registers that C-string with this object's [`ondispose`](#dispose)
  for cleanup (which means the client _must not free
  it_). `memberName` is interpreted as for `lookupMember()`. Throws if
  the given member does not have signature type "s". This does not
  free any memory pointed to by that member, so may leak when
  overwriting. See [](#type-s).

- `zeroOnDispose` (boolean, read-only)  
  `true` if configured to zero out memory upon calling `dispose()`.


<a id='StructCtor'></a>
### Struct Constructors (`StructCtor`)

See also: [](#using-StructCtor)

API overview:

**Instantiation Signatures:**

- `new MyStruct()`  
  Allocates new WASM heap memory for the struct.

- `new MyStruct(pointer)`  
  Wraps existing WASM memory at `pointer` without taking ownership.

- `new MyStruct({ wrap, takeOwnership, zeroOnDispose, extraBytes, ondispose })`  
  Instantiates wrapping `wrap` pointer or newly allocated heap memory
  with options for ownership, tail padding, zero-on-dispose, and
  disposal callbacks.

**Static Members:**

- `isA(value)`  
  Returns `true` if argument was created by this constructor.

- `memberKey(string)`  
  as documented for [StructType][] static.

- `memberKeys()`  
  as documented for [StructType][] prototype.

- `ptrAdd(args...)`  
  as documented for [StructBinder][].ptrAdd.

- `structInfo` (Object, read-only)  
  The struct descriptor metadata object passed when generating the
  constructor.

- `structName` (string, read-only)  
  The structure name passed when generating the constructor.

<a id='StructCtor.prototype'></a>
### Struct Prototypes

Specific instances of [StructType][] serving as the prototype for
generated struct constructors.

**Properties:**

- `structInfo`  
  As documented for [StructCtor][].structInfo.

* `structName`  
  As documented for [StructCtor][].structName.


<a id='StructInstance'></a>
### Struct Instances

See also: [](#using-StructInstance)

Concrete JS objects representing individual bound struct instances
created via `new MyStruct(...)`.

Inherits all methods and properties directly from
[StructType.prototype][] and its struct prototype.

All properties from the underlying C struct are accessible via their
configured named. e.g. `myObj.$x = 3` will convert `3` to the type
configured for `$x` and set that value at the appropriate memory
offset in the corresponding C struct, depending on [the configured
member name prefix and/or suffix](#member-prefix).


-----

<a id='using'></a>
Using the Various Classes
=========================

<a id='using-StructBinder'></a>
Using StructBinder
--------------------

[StructBinder][] is called one time per distinct struct and has a
trivial interface:

```
const MyStruct = MyStructBinder(structDefinition);
// or:
const MyStruct = MyStructBinder('MyStruct', structDefinition);
```

Where `structDefinition` is as documented in [](#descriptors).

The result of those calls is a constructor function for instances of
that struct, as described in the following section.

<a id='using-StructCtor'></a>
Using StructCtor
--------------------

Jaccwabyt structs have two [constructor][StructCtor] signatures:

- `new Struct(pointer=null)`
- `new Struct(optionsObject)`

And three modes of operation with regard to ownership:

- `new Struct()`  
  Allocates WASM heap memory owned by this object.
- `new Struct({wrap: ptr})` and `new Struct(ptr)`  
  Uses the WASM memory provided via `ptr`, which must outlive
  the struct object.
- `new Struct({wrap: ptr, takeOwnership:true})`  
  Uses the WASM memory provided via `ptr` and takes over its
  ownership.

The options object accepts:

- If `wrap` is set then (A) it must be at least
  `MyStruct.structInfo.sizeof` of memory owned by the caller, (B) it
  _must_ have been allocated using the same allocator as Jaccwabyt is
  configured for, and (C) ownership is transfered (or not) as
  specified by `takeOwnership` (see below). `zeroOnDispose` and
  `extraBytes` are disregarded if `wrap` is set. A falsy `wrap` value
  is equivalent to not providing one, in which case this instance will
  allocate one from the WASM heap and take over ownership of it.  A
  non-integer value, or a number less than 0, triggers an error.

- If `takeOwnership` is truthy then this object takes over ownership
  of the `wrap` pointer (if any). This flag is otherwise ignored.
  This flag is implicitly true when this object allocates the memory,
  so has no effect if passed a falsy `wrap` value.

- `extraBytes` specifies a number of bytes to allocate along with the
  instance, as part of the same memory block.  This setting is exposed
  as the instance-level `extraBytes` property. Explained in more
  detail below.

- `zeroOnDispose` causes the `dispose()` method to zero out this
  object's memory, but _only_ if it owns the WASM pointer.  This
  setting is later exposed as the instance-level `zeroOnDispose`
  property.

- `ondispose`: if set it is passed to the new object's inherited
  `addOnDispose()` before the constructor returns, so may have any
  value legal for that method. Results are undefined if `ondispose`
  and `wrap` have the same pointer value (because `wrap` will already
  be cleaned up via `dispose()`, as if it had been passed to
  `addOnDispose()`).

### Extra Trailing Bytes

The `extraBytes` constructor option pads the struct object with extra
memory. A pointer to the tail of the struct memory can be obtained by
adding `theInstance.structInfo.sizeof` to `theInstance.pointer`.
Doing so portably, however, requires care. See [](#64bit) for details
and an example.

The API does not currently expose a way to get the _number_ of extra
bytes - it is assumed that the caller knows that, or attached it as
a property of the instance after construction.


<a id='using-StructInstance'></a>
Using StructType Instances
--------------------------

Once [constructed][StructCtor], the C-side properties of [StructType][] instances
are accessed via normal property access, accounting for any configured
property name prefix and/or suffix. e.g. C-side member `x` might be
accessed via `myStruct.$x`.

Most use cases typically do not need to use any of [the inherited
members][StructType] beyond `dispose()`, which they are required to use as
described in the following section.

<a id='lifecycle'></a><a id='dispose'></a>
### Lifecycle and Memory Management (`dispose()` and `ondispose`)

It cannot be overstated that WASM heap memory must be a first-class
consideration for Jaccwabyt client code. It is critical that heap
memory one owns be released when one is done with it, and it's
critical that we do not use stale pointers to heap memory.

It is also critical that memory from WASM heaps other than the one
which a given [StructBinderFactory][] is configured with are never,
ever passed into Jaccwabyt. Differing heaps have different contents at
the same addresses, and Jaccwabyt always reads and writes its
configured heap, so passing addresses from different heaps _will_
corrupt memory.

When a client is done with a struct instance, they must free all
associated resources by calling **`theInstance.dipose()`**. That will, if
the struct instance owns its memory, free it, otherwise it will simply
disassociate the object from it.

It is important to call `dispose()` even for non-memory-owning
instances so that Jaccwabyt-internal mappings of pointers to objects
remain up-to-date.

Struct instances may optionally have an `ondispose` member. If set, it
must be an array. During disposal, the array is walked as follows:

- Each Function-type entry is called with no arguments. Its `this`
  is the struct instance being disposed of.
- Each [StructType][] value's `dispose()` method is called.
- Each pointer-looking value is assumed to be a WASM heap pointer and
  is passed to the `dealloc()` method provided to the
  [StructBinderFactory][].
- All other types are ignored. It is sometimes useful to annotate
  entries in the list by adding a string-type entry before or after
  them, describing the value. e.g. `["memory from X", xPtr]`.

The inherited `addOnDispose()` method creates the `ondispose`
property, if needed, and then appends its arguments to it, making it a
convenient way to incrementally add on-dispose state to a struct
instance.

The try/finally construct is ideal for managing short-lived instances:

>  
```javascript
const x = new MyStruct;
try{
 ...
}finally{
  x.dispose();
}
```

Alas, longer-lived instances must be managed more closely by the client.

-----
<a id='nested-structs'></a>
Nested Structs
===============================

TODO. The feature exists but is not yet well documented.  The [test
app][test-app] is currently the authority but the API is also still
subject to change.

-----
<a id='64bit'></a>
64-bit WASM
===============================

Jaccwabyt supports both 32- and 64-bit WASM builds, but 64-bit WASM
has portability caveats unrelated to Jaccwabyt which affect how it is
used.

Specifically: mixed-number-type math is illegal, triggering a
`TypeError` exception. Ergo, on 64-bit builds the following will fail:

>  
```javascript
const offset = myStruct.pointer + 8;
```

Because `myStruct.pointer` is of type `Number` in 32-bit builds
and `BigInt` in 64-bit builds.

The `ptrAdd()` method was added to several places in the API as an
alternative to adding and subtracting pointers:

>  
```javascript
const offset = MyStructBinder.ptrAdd(myStruct.pointer, 8);
// Or:
const offset = MyStructCtor.ptrAdd(myStruct.pointer, 8);
// Or, more simply:
const offset = myStruct.ptrAdd(8);
```

------

Appendices
====================

<a id='appendix-b'></a>
Appendix B: Building
--------------------

In order to support both vanilla JS and ESM (ES6 module) builds from a
single source, this project uses [a preprocessor][c-pp], which requires
only a C compiler:

> $ make

The makefile requires GNU make, not BSD or POSIX make.

This tree [includes all of the requisite sources](/dir/tool) and
requires no out-of-tree dependencies beyond the system's libc.

The resulting files are in the `js/` subdirectory, in both "vanilla"
JS and ESM formats and they have no dependencies.


<a id='appendix-d'></a>
Appendix D: Debug Info
--------------------

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
Appendix G: Generating Struct Descriptors
--------------------

Struct definitions _really, really must_ be generated from
WASM-compiled C, as opposed to simply guessing the sizeofs and
offsets, so that the sizeof and offset information can be collected
using C's `sizeof()` and `offsetof()` features. Struct padding may
impact offsets in ways which might not be immediately obvious, so
writing them by hand is _most certainly not recommended_. (WASM
retains the shape of C structs, including their padding, unless the
compiler is specifically directed otherwise.)

How exactly the desciption is generated is necessarily
project-dependent. It's tempting say, "oh, that's easy! We'll just
write it by hand!" but that would be folly. The struct sizes and byte
offsets into the struct _must_ be _precisely_ how C-side code sees the
struct or the runtime results are completely undefined.

Disregarding the previous two paragraphs' suggestion will eventually
lead to WASM memory corruption, a notoriously difficult thing to debug
browser-side.

The approach used in developing and testing _this_ software is...

Below is a complete copy/pastable example of how we can use a small
set of macros to generate struct descriptors from C99 or later into
static string memory. Simply add such a file to your WASM build,
arrange for its function to be exported[^export-func], and call it
from JS (noting that it requires environment-specific JS glue to
convert the returned pointer to a JS-side string). Use `JSON.parse()`
to process it, then feed the included struct descriptors into the
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
       protect against a small race condition. */;
  char const * const zEnd = pos + sizeof(strBuf)
    /* one-past-the-end cursor (virtual EOF) */;
  if(strBuf[0]) return strBuf; // Was set up in a previous call.

  //////////////////////////////
  // First we need to build up our macro framework...

  //////////////////////////////
  // Core output-generating macros...
#define lenCheck assert(pos < zEnd - 100)
#define outf(format,...) \
  pos += snprintf(pos, ((size_t)(zEnd - pos)), format, __VA_ARGS__); \
  lenCheck
#define out(TXT) outf("%s",TXT)
#define CloseBrace(LEVEL) \
  assert(LEVEL<5); memset(pos, '}', LEVEL); pos+=LEVEL; lenCheck

  //////////////////////////////
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
  //////////////////////////////

  //////////////////////////////
  // With that out of the way, we can do what we came here to do.
  out("\"structs\": ["); {

// For each struct descriptor, do...
#define CurrentStruct ExampleStruct
    StructBinder {
      M(v4,"i");
      M(ppV,"p");
      M(v8,"j");
      M(xFunc,"v(p)");
    } _StructBinder;
#undef CurrentStruct

  } out( "]"/*structs*/);
  //////////////////////////////
  // Done! Finalize the output...
  out("}"/*top-level wrapper*/);
  *pos = 0;
  strBuf[0] = '{'/*end of the race-condition workaround*/;
  return strBuf;

// If this file will ever be concatenated or #included with others,
// it's good hygiene to clean up our macros:
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

A much richer example of that, spanning many C structs and non-struct
data, can be found at:

- <https://sqlite.org/src/file/ext/wasm/api/sqlite3-wasm.c>
  in the function `sqlite3__wasm_enum_json()`.


<a id='appendix-l'></a>
Appendix L: License
--------------------

Jaccwabyt, including its documentation and test code, are released
under the terms of [the SQLite Blessing][blessing]:

>
```
  2022-06-30

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.
```



<nowiki><style>
/* Reminder: greater-than gets mangled to HTML */
div.content {}
div.content h1, div.content h2, div.content h3 {
  border-radius: 0.25em;
  border-bottom: 1px solid #70707070;
}
div.content h1 {}
div.content {
  margin-right: 0.5em;
}
div.content h2 {}
div.content h3 {border-left-width: 2.5em}
div.content code {
  border: 1px dotted;
  border-radius: 0.25em;
  padding: 0 0.1em;
}
div.content pre code {
  border: unset;
  padding: unset;
}
</style></nowiki>

[appendix-g]: #appendix-g
[blessing]: https://spdx.org/licenses/blessing.html
[BigInt64Array]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/BigInt64Array
[c-pp]: https://fossil.wanderinghorse.net/r/c-pp
[Emscripten]: https://emscripten.org
[StructBinder]: #StructBinder
[StructBinderFactory]:  #StructBinderFactory
[StructCtor]: #StructCtor
[StructInstance]: #StructInstance
[StructType.prototype]: #StructType.prototype
[StructType]: #StructType
[test-app]: /file/test/tester.c-pp.js

[^export-func]: In Emscripten, add its name, prefixed with `_`, to the
  project's `EXPORT_FUNCTIONS` list. Exports can also be declared from
  C, as demonstrated in [](/file/jaccwabyt/jaccwabyt_test.c).
