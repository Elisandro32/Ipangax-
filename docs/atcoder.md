# Using Ipangax for AtCoder-style problems

AtCoder does not accept Ipangax, so you solve locally: save the sample input in a file and run

```
./ipx run solution.ipa < sample.txt
```

Reading patterns:

```
n = int(input())                 // one integer on a line
parts = split(input())           // "3 4" -> ["3", "4"]
a = int(parts[0])
nums = split(input())            // n numbers on one line
for i in range(n) {
    x = int(nums[i])
}
```

Printing a list of numbers on one line:

```
out = []        // not allowed: needs a type
string[] out = []
for x in values {
    push(out, string(x))
}
print(join(out, " "))
```

Compound assignment works: `total += x`, `a[i] -= 1`.

Available now: maps and sets (`map[string, int]`, `set[int]`), `heap[int]` / `maxheap[int]`,
`deque[int]`, structs, bit operators, `gcd`, `lcm`, `powmod`, `floor`, `ceil`, `log`.
See `examples/dijkstra.ipa` for shortest paths with a heap.

Not available yet: tuples and pairs, custom sort order, reading all input at once, big integers,
recursion deeper than 1000.
