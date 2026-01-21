![ProtoScript](../../header.png)

# Chapter 4 — Regular Expressions

Regular expressions are patterns used to match character combinations in strings. In ProtoScript, regular expressions are objects.

Regular expressions are used with the `RegExp` methods `exec` and `test`, and with the `String` methods `match`, `replace`, `search`, and `split`.

---

## Creating a Regular Expression

You can create a regular expression in one of two ways.

### Using a Literal

```js
re = /ab+c/;
```

Regular expression literals are compiled when the script is evaluated. This form is recommended when the pattern is constant.

### Using the `RegExp` Constructor

```js
re = new RegExp("ab+c");
```

This form compiles the pattern at run time and is useful when the pattern is dynamic or obtained from another source.

---

## Writing a Regular Expression Pattern

A regular expression pattern may consist of simple characters or a combination of simple and special characters:

```js
/abc/
/ab*c/
/Chapter (\d+)\.\d*/
```

Parentheses group parts of a pattern and cause the matched substring to be remembered for later use.

---

### Using Simple Patterns

Simple patterns match exact character sequences.

```js
/abc/
```

This pattern matches the substring `abc` only when those characters occur together and in that order.

---

### Using Special Characters

Special characters extend pattern matching beyond literal characters.

```js
/ab*c/
```

This matches an `a` followed by zero or more `b` characters and then a `c`.

---

### Special Characters Reference

| Character | Meaning |
|----------|--------|
| `\` | Escapes a character or removes special meaning |
| `^` | Beginning of input or line |
| `$` | End of input or line |
| `*` | 0 or more occurrences |
| `+` | 1 or more occurrences |
| `?` | 0 or 1 occurrence |
| `.` | Any character except newline |
| `(x)` | Capture and remember `x` |
| `x|y` | Match `x` or `y` |
| `{n}` | Exactly `n` occurrences |
| `{n,}` | At least `n` occurrences |
| `{n,m}` | Between `n` and `m` occurrences |
| `[xyz]` | Character set |
| `[^xyz]` | Negated character set |
| `\b` | Word boundary |
| `\B` | Non-word boundary |
| `\d` | Digit (`[0-9]`) |
| `\D` | Non-digit |
| `\s` | Whitespace |
| `\S` | Non-whitespace |
| `\w` | Word character (`[A-Za-z0-9_]`) |
| `\W` | Non-word character |
| `\n`, `\r`, `\t`, `\f`, `\v` | Control characters |

---

## Working with Regular Expressions

### Methods

| Method | Description |
|-------|-------------|
| `exec` | Executes a search and returns match details |
| `test` | Tests for a match and returns `true` or `false` |
| `match` | Returns match details or `null` |
| `search` | Returns the index of the match or `-1` |
| `replace` | Replaces matched substrings |
| `split` | Splits a string using a pattern |

---

### Example: Using `exec`

```js
re = /d(b+)d/g;
result = re.exec("cdbbdbsbz");
```

The returned array contains the full match and remembered substrings, along with properties such as `index` and `input`.

---

## Using Parenthesized Substring Matches

Parenthesized submatches are accessible through the returned array or through the predefined `RegExp` properties `$1` through `$9` (for example `RegExp.$1`).

```js
re = /(\w+)\s(\w+)/;
str = "John Smith";
newstr = str.replace(re, "$2, $1");
```

This produces:

```
Smith, John
```

---

## Global and Case-Insensitive Searches

Flags modify how a regular expression operates:

- `g` — global search
- `i` — case-insensitive search

Note: ProtoScript uses a limited case-folding table for `i` (ASCII + Latin-1 +
Latin Extended-A + basic Greek + basic Cyrillic, plus a few special-case folds).
It does not implement full Unicode case folding.

```js
re = /\w+\s/g;
words = "fee fi fo fum".match(re);
```

Flags are part of the regular expression and cannot be changed after creation.

---

## Examples

### Reformatting a Name List

```js
names = "Harry Trump; Fred Barney; Helen Rigby";
list = names.split(/\s*;\s*/);
for (i = 0; i < list.length; i++)
    list[i] = list[i].replace(/(\w+)\s+(\w+)/, "$2, $1");
list.sort();
```
