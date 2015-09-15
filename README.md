The Quaint programming language
===============================

An experimental, static, strongly typed, procedural, VM-based, memory-unsafe
language with first-class resumable functions.

Quaint inherits the memory model and operational semantics of C, adding some
modernised "21st century" syntactic features, a strong typing discipline,
built-in types of an exact specified width, well-defined behaviour w.r.t. struct
padding, and seamless support for resumable functions. The runtime is currently
implemented as a GP-register-free, memory-to-memory machine. The performance
should be decent enough for most non-CPU-intensive tasks.

This is a work in progress. Currently, the language does not aim to be rich of
convenience "sugary" features, nor be "fast" nor ready for practical use. It
rather attempts to fix some of the murky aspects of C – namely, the optional
curly braces, the arcane type declaration syntax, the frustrating implicit
integer conversion and promotion rules, the confusing array-to-pointer decaying.

The main goal of Quaint is to demonstrate the viability of a new idiom for
"sane" concurrent programming that bears resemblance to promises and generators.

In essence, every function can be called either synchronously (just as in C) or
asynchronously via a `quaint` object. The called function does not have any
explicit "yield" statements, but merely only optional "wait labels" that mark
eventual suspension points in which the caller might be interested in. The
caller decides whether to wait with a timeout or wait until reaching a certain
"wait label". The caller can also query the quaint to see whether it has lastly
passed a particular wait label. The callee can have optional `noint` blocks of
statements during which it cannot be interrupted even if the caller is waiting
with a timeout and that timeout has elapsed.

Quaint is entirely single-threaded – asynchronous functions run only when the
caller is blocked on a `wait` statement or on a "run-till-end & reap-value"
operator. During all other times, the resumable function is in a "frozen" state
which holds its execution context and stack.

## Table of contents

  * [Building](#building)
  * [Basic syntax](#basic-syntax)
  * [Significant differences from C](#c-differences)
  * [Built-in type table](#built-in-type-table)
  * [Operator table](#operator-table)
  * [The interesting part: resumable functions](#resumable-functions)
    * [What is a `quaint()`?](#what-is-a-quaint)
    * [The `~` "quaintify" operator](#tilde-operator)
    * [The `@` query operator](#at-operator)
    * [The `wait` statement](#wait-stmt)
    * [The `*` "run-till-end & reap-value" operator](#rterv-operator)
    * [Wait labels](#wait-labels)
    * [The `noint` block](#noint-block)
    * [An example](#an-example)
  * [Currently lacking features compared to C](#lacking-features)
  * [Future directions](#future-directions)
  * [Feedback](#feedback)

<a id="building"></a>
## Building

`make` should succeed on Linux and OS X with a recent-enough version of either
Clang or GCC. Additionally, an Xcode project can be built and debugged under OS
X. When building with make, the executable will be put in `./build/make/quaint`.
When building with Xcode, the executable will be in `./build/xcode/quaint`.

Other Unixes have not been tested, but Quaint should very likely be able to work
there as it depends only on the C standard library and POSIX system calls.

The Makefile supports a few parameters:

* `make` with no parameters builds the project in release mode (assertions
removed) with `-O2` and link-time optimisation, which is rather slow
* `make DEBUG=1` builds it with no optimisations and assertions turned on
* `make 32BIT=1` builds it as a 32-bit executable

<a id="basic-syntax"></a>
## Basic syntax

Curly braces are mandatory and no parentheses are required around the conditions
of control-flow statements. The three current control-flow statements look like
this:

```
if EXPR {
    ...
} elif EXPR {
    ...
} else {
    ...
}

while EXPR {
    ...
}

do {
    ...
} while EXPR;
```

The variable and type declaration syntax is much more "human-oriented" than C.
The equivalent of `uint8_t a = 0;` is written as follows:

```
a: byte = 0;
```

More examples:

```
arr: int[5]; /* arr is an array of 5 ints */
p: ptr(int); /* p is a pointer to an int */
p: ptr[3](int); /* p is an array of 3 pointers to int */
p: ptr[3](int[5]) /* p is an array of 3 pointers to array of 5 ints */

/* fp is a function pointer to a function taking a byte, returning an int */
fp: fptr(b: byte): int;

/* point is an anonymous struct having 2 members of type u32 (alias of int) */
point: struct(x: u32, y: u32);

/* q is a quaint (promise) to a value of type int */
q: quaint(int);

/* q is a quaint (promise) to a value of type void */
q: quaint();
```

Functions are defined as follows (function prototypes do not exist):

```
f1(a: uint, b: uint): ulong
{
    const addend: ulong = 5:ulong;
    return a:ulong + b:ulong + addend;
}

/* a function with no parameters and no return value */
f2
{
    return;
}
```

Automatic variables may be declared and initialised anywhere within a lexical
block. Global variables are declared only at top-level file scope. User-defined
types can be defined at file scope via the `type` statement:

```
type mytype: struct(
    member_one: ptr(quaint(byte)),
    member_two: vptr,
    member_three: u32,
    member_four: fptr(x: int[12]): u64
);
```

<a id="c-differences"></a>
## Significant differences from C

* Quaint is as strongly typed as it gets. Implicit type conversions are not
taking place at all. All binary arithmetic and logical operations require that
both operands are of the same size and signedness. Explicit casts must be used
to equalise the types whenever there is a mismatch. The `as` and `:` typecast
operators are aliases, except that `as` has a slightly weaker precedence.

* Another peculiarity is that number literals have an unsigned integral type
of the least size that is able to hold the value:

| Literal Range                          | Type        | Alias     |
| -------------------------------------- | ----------- | --------- |
| `0` to `255`                           | `byte`      | `u8`      |
| `256` to `65535`                       | `ushort`    | `u16`     |
| `65536` to `4294967295`                | `uint`      | `u32`     |
| `4294967296` to `18446744073709551615` | `ulong`     | `u64`     |
| String Literals                        | `ptr(byte)` | `ptr(u8)` |

* String literals are null-terminated and *modifiable*, but not extensible
beyond their end.

* Arrays always act and look like arrays, never pretending to be pointers. The
expression `&arr[0]` gets a pointer to the first element, while `&arr` gets a
pointer to the entire array. The expression `arr` is of type
`whatever_type[whatever_constant_size]`. Two array types are considered equal
only if their types and sizes are equal. Arrays of equal types may be assigned
to each other with the `=` operator. Arrays can be passed and returned by value
from functions.

<a id="built-in-type-table"></a>
## Built-in type table

| Type ID     | Alias      | Signed     | Unsigned   | Size       | Alignment  |
| ----------- | ---------- | ---------- | ---------- | ---------- | ---------- |
| byte        | u8         |            | ✓          | 1          | 1          |
| sbyte       | i8         | ✓          |            | 1          | 1          |
| ushort      | u16        |            | ✓          | 2          | 2          |
| short       | i16        | ✓          |            | 2          | 2          |
| uint        | u32        |            | ✓          | 4          | 4          |
| int         | i32        | ✓          |            | 4          | 4          |
| ulong       | u64        |            | ✓          | 8          | 8          |
| long        | i64        | ✓          |            | 8          | 8          |
| usize       |            |            | ✓          | 8          | 8          |
| ssize       |            | ✓          |            | 8          | 8          |
| uptr        |            |            | ✓          | 8          | 8          |
| iptr        |            | ✓          |            | 8          | 8          |
| ptr         |            |            | ✓          | 8          | 8          |
| vptr        |            |            | ✓          | 8          | 8          |
| fptr        |            |            | ✓          | 8          | 8          |
| quaint      |            |            | ✓          | 8          | 8          |
| struct      |            |            |            | -          | -          |
| union       |            |            |            | -          | -          |

The types `usize` and `ssize` are preferred for pointer arithmetic (analogous
to `size_t` and `ssize_t`).

`uptr` and `iptr` are the counterparts of `uintptr_t` and `intptr_t`,
respectively.

`vptr` is a void pointer that cannot be dereferenced and cannot be used for
pointer arithmetic, just like in C. The built-in constant `null` is of type
`vptr`.

`fptr` is a function pointer that can neither be dereferenced nor be used in
pointer arithmetic.

Structs and unions have an appropriate size and alignment, based on the
size, alignment and order of their members.

<a id="operator-table"></a>
## Operator table

| Operator  | Binary                         | Unary                           |
| --------- | ------------------------------ | ------------------------------- |
| `=`       | Assignment                     |                                 |
| `+=`      | Assignment-add                 |                                 |
| `-=`      | Assignment-subtract            |                                 |
| `*=`      | Assignment-multiply            |                                 |
| `/=`      | Assignment-divide              |                                 |
| `%=`      | Assignment-modulo              |                                 |
| `<<=`     | Assignment-left shift          |                                 |
| `>>=`     | Assignment-right shift         |                                 |
| `&=`      | Assignment-bitwise and         |                                 |
| `^=`      | Assignment-bitwise xor         |                                 |
| `|=`      | Assignment-bitwise or          |                                 |
| `:`       | Typecast                       |                                 |
| `as`      | Typecast                       |                                 |
| `::`      | Scope resolution               |                                 |
| `@`       | Quaint is at label test, `u8`  |                                 |
| `.`       | Struct/union member            |                                 |
| `->`      | Deref struct/union member      |                                 |
| `==`      | Equality, `u8`                 |                                 |
| `!=`      | Inequality, `u8`               |                                 |
| `<`       | Less than, `u8`                |                                 |
| `>`       | Greater than, `u8`             |                                 |
| `<=`      | Less or equal than, `u8`       |                                 |
| `>=`      | Greater or equal than, `u8`    |                                 |
| `&&`      | Logical and, `u8`              |                                 |
| `||`      | Logical or, `u8`               |                                 |
| `+`       | Addition                       | Identity (id)                   |
| `-`       | Subtraction                    | Arithmetic negation             |
| `*`       | Multiplication                 | Pointer dereference/Quaint RTE  |
| `/`       | Integral Division              |                                 |
| `%`       | Modulo                         |                                 |
| `<<`      | Left shift                     |                                 |
| `>>`      | Right shift                    |                                 |
| `&`       | Bitwise and                    | Address of, `ptr(...)`          |
| `|`       | Bitwise or                     |                                 |
| `^`       | Bitwise xor                    | Bitwise negation (id)           |
| `,`       | Comma (rightmost id)           |                                 |
| `!`       |                                | Logical negation (id)           |
| `~`       |                                | Quaintify call/value, `quaint()`|
| `++`      |                                | Prefix/postfix increment (id)   |
| `--`      |                                | Prefix/postfix decrement (id)   |
| `sizeof`  |                                | Size of type, `usize`           |
| `alignof` |                                | Alignment of type, `usize`      |
| `()`      | Function call                  |                                 |
| `[]`      | Array subscription             |                                 |
| `?:`      | Conditional                                                      |

The semantics and precedence levels of the operators inherited from C should be
the same. If you spot any "unintuitive" precedence parsing, send me feedback.

<a id="resumable-functions"></a>
## The interesting part: resumable functions

<a id="what-is-a-quaint"></a>
### What is a `quaint(...)`?

A `quaint` denotes a built-in, reference-like type that stores the "frozen"
execution context of a function invocation. Like a pointer, it can either have a
null value (a billion-dollar mistake, sure :smiley:) or point to a function
invocation. The internal structure of a `quaint` is managed by the VM and is not
directly accessible to the programmer.

A `quaint` type must specify a subtype in parentheses. This subtype specifies
the type of the value that is expected upon termination of the asynchronous
invocation. In case of invoking a function that returns no value, `quaint()`
will suffice.

<a id="tilde-operator"></a>
### The `~` "quaintify" operator

When applied to a function-call expression or even a pure value, the `~` unary
operator returns an appropriately allocated and initialised `quaint` with a
subtype that corresponds to the type of its operand.

For example, if a function `f` takes two `byte` arguments and returns an `int`,
the expression `~f(3, 5)` will return a `quaint(int)`. If `g` takes an argument
but does not return a value, `~g(7)` will return `quaint()`. At this point, the
quaint is in a state where the arguments have been pushed to its stack and its
instruction pointer points to the first instruction of the function.

When `~` is applied to a value, the returned quaint merely holds that value and
does not point to any function. This may seem rather pointless, but it is a nice
feature since it makes pure values and function invocations interchangeable and
compatible with the rest of the operators listed here.

<a id="at-operator"></a>
### The `@` query operator

The `@` binary operator requires that its first operand is a quaint of any
subtype and its second operand is the name of a wait label. The returned value
is of type `byte` (alias of `u8`) and is `1` if the quaint has lastly passed
that label, `0` otherwise.

Two special built-in label names exist: `start` and `end`. The expression
`q@start` returns `1` only when the quaint has just been created with the `~`
operator and has never been `wait`ed on. The expression `q@end` returns `1` when
the quaint has reached a return statement and its value can be subsequently
"reaped" without blocking. Both `q@start` and `q@end` return `1` when applied
to a pure-value quaint.

Within the context of a `@` operator, a wait label is specified by concatenating
a function name and a label name that is defined anywhere within the scope of
that function, for example `q@my_function::a_label`.

When applied to a null quaint, this operator returns `0`.

<a id="wait-stmt"></a>
### The `wait` statement

The `wait` statement has six basic syntactic forms and it only has side effects,
not returning a value:

* `wait quaint_expr`
* `wait quaint_expr noblock`
* `wait quaint_expr for timeout_expr [msec|sec]`
* `wait quaint_expr for timeout_expr [msec|sec] noblock`
* `wait quaint_expr until function_name::label_name`
* `wait quaint_expr until function_name::label_name noblock`

`quaint_expr` is any expression that evaluates to a `quaint(...)` type.
`timeout_expr` is an unsigned integral expression that specifies either the
number of seconds when `sec` is immediately used, or the number of milliseconds
when `msec` is used or omitted.

During a `wait` statement, the execution of the quaint is resumed and it is run
until the timeout has passed, the specified label has been reached, or the
function has returned.

A `wait` statement is a no-op (without side effects) in any of the following
situations:

* When `quaint_expr` evaluates to `null`
* When `quaint_expr@end` evaluates to `1`
* When `timeout_expr` evaluates to `0`
* When `quaint_expr` is a pure-value quaint (`quaint_expr@start && quaint_expr@end`)

The `noblock` option is currently unused and not implemented, but syntactically
valid. The idea behind it is that whenever the quaint enters a blocking IO
built-in function, the wait should return even if the timeout has not still
passed or the label has not been reached.

<a id="rterv-operator"></a>
### The `*` "run-till-end & reap-value" operator

When applied to an expression of a quaint type, the unary `*` resumes the
quaint, waits until it terminates, and returns its value. If the subtype of the
quaint is void, this operator does not return a value either.

The operand expression must be an lvalue, too, because `*` will also free it
*and* nullify it. This operator can be thought of as a counterpart of the `~`
quantify operator. The former allocates the quaint, the latter releases it. The
VM does not release any orphaned quaints, so a failure to release the quaint via
`*` causes a memory leak in your program.

When applied over a quaint that has reached the end (`q@end`), this operator
immediately returns the value (or nothing in case of `quaint()`), releases the
quaint and nullifies it.

When applied over a null quaint, `*` returns a zero value of the appropriate
subtype or nothing in case of a void subtype.

<a id="wait-labels"></a>
### Wait labels

A wait label is a statement that can appear anywhere within the body of a
function and has the following syntax (semicolons or colons *must* be omitted):

```
[an_alphanumeric_label_name]
```

Wait labels can be guarded by control-flow statements and will fire up only when
the body of that control-flow statement is executed.

Duplicate wait labels are allowed within a single function; each of them will
fire up with the same label name when it is reached.

<a id="noint-block"></a>
### The `noint` block

The `noint` block is an ordinary lexical block of statements that disables the
possibility of interrupting the statement sequence by a `wait` up in the call
stack. This can be thought of as a "critical section", even that Quaint is
entirely single-threaded.

This is usually useful when modifying some global state variables which may be
shared with the caller:

```
/* swap two global variables safely, regardless of any waits up in the call stack */
noint {
    const temp: int = global_a;
    global_a = global_b;
    global_b = temp;
}
```

<a id="an-example"</a>
### An example

```
entry
{
    /*
     * Our recursive fibonacci function has exponential complexity and is very
     * computationally-intensive. A synchronous call to fibonacci(32:u32) takes
     * about 6 seconds on my i5 MacBook Air:
     */
    ps("Synchronous call: "), pu32(fibonacci(32 as u32)), pnl();

    /* Now, call the same function asynchronously, in steps of 1 second */
    fibq: quaint(u32) = ~fibonacci(32 as u32);

    ps("At start: "), pu8(fibq@start), pnl();
    iter: u32 = 0:u32;

    do {
        wait fibq for 1000 msec;
        ps("Iteration "), pu32(iter++:u32), pnl();
    } while !fibq@end;

    ps("At end: "), pu8(fibq@end), pnl();
    const value: u32 = *fibq;
    ps("Reaped value: "), pu32(value), pnl();
}

fibonacci(number: u32): u32
{
    if number == 0:u32 || number == 1:u32 {
        return number;
    } else {
        return fibonacci(number - 1:u32) + fibonacci(number - 2:u32);
    }
}
```

The way of printing output via the comma operator is indeed rather clunky, but
that will be the way until variadic functions are implemented and `printf`-like
functions appear.

<a id="lacking-features"></a>
## Currently lacking features compared to C

* No `for`, `switch`, `goto`, `break` and `continue` control-flow statements
* No multidimensional arrays
* No `float` and `double` types
* No enums
* No struct/union/array initialisers or compound literals
* No escape sequences within string literals
* No hex, octal or binary number literals
* No bit-fields in structs and unions
* No variadic functions
* No `const`-qualified pointer subtypes, only top-level `const`
* No ability to declare a struct member that is a pointer to the same struct
* The `sizeof` and `alignof` operators accept only types, not variable names

<a id="future-directions"></a>
## Future directions

In order to make Quaint a pleasant and usable general-purpose language, these
are the things I am planning to implement in the upcoming years, in decreasing
priority:

* Unit-based compilation and linking
* Implicit namespaces based on the name of the source file
* Hygienic enums
* Producing stand-alone native executables with the VM (exec.c) embedded
* "Safe" (slow) and "unsafe" (fast) execution modes of the VM
* Additional syntax for array/struct/union literals
* A richer set of control-flow statements, probably also statement expressions
* Type inference
* Slightly more relaxed type checking in some contexts
* More built-in functions and perhaps runtime-loadable bundles of functions
* Friendlier and more descriptive error messages
* Some basic optimisation passes
* Debugging facilities

As a firm OO-nonbeliever, I will never steer the language into OO land. Quaint
will remain a small, pragmatic and conservative language that can be learned in
a day by an experienced C programmer.

Quaint will never adopt the idiotic and perverse callback-based concurrent
programming model of Node.js, either. A part of the reason I am designing and
implementing this language is my frustration with the recent proliferation of
half-assed concurrent paradigms.

I also think that threads should be of a very limited use (if used at all) in
general-purpose programming. In the future, Quaint will support forking the VM
process a number of times that corresponds to the number of CPU cores.
Communication among the forked processes will happen through message queues.
Shared memory regions among the processes would be used only as a last resort.

No compromises will be made with the quality of the implementation, even if it
takes much longer to implement things cleanly and properly.

<a id="feedback">
## Feedback

Feedback and discussion regarding any flaws, bugs or potentially useful and
interesting features is very welcome. Any interesting example code that you came
up with can be added to the examples.

In addition to opening up issues in GitHub, you may contact me at
blagovest.buyukliev at gmail_com.
