var count = 0;
function f() { for (var i = 0; i < 10; i++) count += helper(i); }
function helper(n) { return n % 2 ? Math.floor(Math.random() * 3) : n; }
