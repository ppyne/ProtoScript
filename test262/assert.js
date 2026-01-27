// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of assertion functions used throughout test262
defines: [assert]
---*/

function Test262Error(message) {
  this.message = message === undefined ? '' : String(message);
  this.name = 'Test262Error';
}
Test262Error.prototype = new Error();
Test262Error.prototype.constructor = Test262Error;
Test262Error.thrower = function () {
  throw new Test262Error();
};

function $DONOTEVALUATE() {
  throw new Test262Error('Test262: This statement should not be evaluated.');
}

function assert(mustBeTrue, message) {
  if (mustBeTrue === true) {
    return;
  }

  if (message === undefined) {
    message = 'Expected true but got ' + assert._toString(mustBeTrue);
  }
  throw new Test262Error(message);
}

assert._isSameValue = function (a, b) {
  if (a === b) {
    // Handle +/-0 vs. -/+0
    return a !== 0 || 1 / a === 1 / b;
  }

  // Handle NaN vs. NaN
  return a !== a && b !== b;
};

assert.sameValue = function (actual, expected, message) {
  try {
    if (assert._isSameValue(actual, expected)) {
      return;
    }
  } catch (error) {
    throw new Test262Error((message || '') + ' (_isSameValue operation threw) ' + error);
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(expected) + '») to be true';

  throw new Test262Error(message);
};

assert.notSameValue = function (actual, unexpected, message) {
  if (!assert._isSameValue(actual, unexpected)) {
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(unexpected) + '») to be false';

  throw new Test262Error(message);
};

assert.throws = function (expectedErrorConstructor, func, message) {
  var expectedName, actualName;
  if (typeof func !== "function") {
    throw new Test262Error('assert.throws requires two arguments: the error constructor and a function to run');
  }
  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  try {
    func();
  } catch (thrown) {
    if (typeof thrown !== 'object' || thrown === null) {
      message += 'Thrown value was not an object!';
      throw new Test262Error(message);
    } else if (thrown.constructor !== expectedErrorConstructor) {
      expectedName = expectedErrorConstructor.name;
      actualName = thrown.constructor.name;
      if (expectedName === actualName) {
        message += 'Expected a ' + expectedName + ' but got a different error constructor with the same name';
      } else {
        message += 'Expected a ' + expectedName + ' but got a ' + actualName;
      }
      throw new Test262Error(message);
    }
    return;
  }

  message += 'Expected a ' + expectedErrorConstructor.name + ' to be thrown but no exception was thrown at all';
  throw new Test262Error(message);
};

function isPrimitive(value) {
  return value === null || (typeof value !== 'object' && typeof value !== 'function');
}

assert.compareArray = function (actual, expected, message) {
  message = message === undefined ? '' : message;

  if (isPrimitive(actual)) {
    assert(false, 'Actual argument [' + actual + '] should not be primitive. ' + message);
  } else if (isPrimitive(expected)) {
    assert(false, 'Expected argument [' + expected + '] should not be primitive. ' + message);
  }
  var result = compareArray(actual, expected);
  if (result) return;

  var format = compareArray.format;
  assert(false, 'Actual ' + format(actual) + ' and expected ' + format(expected) + ' should have the same contents. ' + message);
};

function compareArray(a, b) {
  if (b.length !== a.length) {
    return false;
  }
  for (var i = 0; i < a.length; i++) {
    if (!assert._isSameValue(b[i], a[i])) {
      return false;
    }
  }
  return true;
}

compareArray.format = function (arrayLike) {
  return '[' + Array.prototype.map.call(arrayLike, String).join(', ') + ']';
};

assert._formatIdentityFreeValue = function formatIdentityFreeValue(value) {
  switch (value === null ? 'null' : typeof value) {
    case 'string':
      return typeof JSON !== "undefined" ? JSON.stringify(value) : '"' + value + '"';
    case 'number':
      if (value === 0 && 1 / value === Number.NEGATIVE_INFINITY) return '-0';
      // falls through
    case 'boolean':
    case 'undefined':
    case 'null':
      return String(value);
  }
};

assert._toString = function (value) {
  var basic = assert._formatIdentityFreeValue(value);
  if (basic) return basic;
  try {
    return String(value);
  } catch (err) {
    if (err && err.name === 'TypeError') {
      return Object.prototype.toString.call(value);
    }
    throw err;
  }
};

// Minimal $262 host object for test262 compatibility.
if (typeof $262 === "undefined") {
  var $262 = {};
  $262.global = this;
  $262.evalScript = function (source) {
    return eval(source);
  };
  $262.createRealm = function () {
    var realm = {};
    realm.global = this;
    realm.evalScript = function (source) {
      return eval(source);
    };
    return realm;
  };
  $262.gc = function () {};
  $262.detachArrayBuffer = function () {
    throw new Test262Error("detachArrayBuffer not supported");
  };
}

// Date constants (ES1-compatible).
if (typeof date_1899_end === "undefined") {
  var date_1899_end = -2208988800001;
  var date_1900_start = -2208988800000;
  var date_1969_end = -1;
  var date_1970_start = 0;
  var date_1999_end = 946684799999;
  var date_2000_start = 946684800000;
  var date_2099_end = 4102444799999;
  var date_2100_start = 4102444800000;
  var start_of_time = -8.64e15;
  var end_of_time = 8.64e15;
}

// Minimal helpers for test262 includes (ES1-friendly).
if (typeof NaNs === "undefined") {
  var NaNs = [
    NaN,
    Number.NaN,
    NaN * 0,
    0 / 0,
    Infinity / Infinity,
    -(0 / 0),
    Math.pow(-1, 0.5),
    -Math.pow(-1, 0.5),
    Number("Not-a-Number")
  ];
}

if (typeof decimalToHexString === "undefined") {
  function decimalToHexString(n) {
    var hex = "0123456789ABCDEF";
    n >>>= 0;
    var s = "";
    while (n) {
      s = hex[n & 0xf] + s;
      n >>>= 4;
    }
    while (s.length < 4) {
      s = "0" + s;
    }
    return s;
  }

  function decimalToPercentHexString(n) {
    var hex = "0123456789ABCDEF";
    return "%" + hex[(n >> 4) & 0xf] + hex[n & 0xf];
  }
}

if (typeof assertRelativeDateMs === "undefined") {
  function assertRelativeDateMs(date, expectedMs) {
    var actualMs = date.valueOf();
    var localOffset = date.getTimezoneOffset() * 60000;
    if (actualMs - localOffset !== expectedMs) {
      throw new Test262Error(
        "Expected " + date + " to be " + expectedMs +
        " milliseconds from the Unix epoch"
      );
    }
  }
}
