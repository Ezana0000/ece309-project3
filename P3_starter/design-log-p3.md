# Project 3 Design Log

**Author:** Ezana Enquobahrie

I used the instructor's P2 reference code, copied from `reference/p2/` into `include/core/` and `src/core/`. AI (Claude) helped with the design and implementation of the code.

## 1. Hash table

I used separate chaining for the tool registry, where each bucket is a `std::vector<std::unique_ptr<Tool>>`. I chose chaining over linear probing because it handles collisions more gracefully. A crowded bucket only slows lookups for names that hash to it instead of spilling into neighboring slots. Chaining also lets the load factor go up to 1.0, while linear probing needs a threshold well under 1 or a `find` on a missing name could loop forever.

For the hash function I used 64-bit FNV-1a. It XORs in each character and then multiplies by the FNV prime, so it uses every character and is order-sensitive, meaning "ab" and "ba" hash to different values. The `hash()` function returns the full 64-bit value, and a separate helper, `index_for()`, turns it into a bucket index with `hash & (bucket_count - 1)`. This works like `%` because the bucket count is always a power of two.

The table starts with 8 buckets. Before inserting a new tool, `add()` checks whether `(size + 1) / bucket_count` would go over 1.0. If it would, `rehash()` doubles the number of buckets and moves every `unique_ptr` into its new bucket, recomputing each index. Because the check happens before the insert, `load_factor()` is never above 1.0 after `add()` returns. I picked 1.0 because with chaining and a good hash, the average chain at that load is only about one entry long.

When I inserted the 10,000 names `tool_0` through `tool_9999`, the table ended with 16,384 buckets, a load factor of 0.610, and a longest chain of only 6. Using the weak hash `s[0] + s.size()` on the same names and bucket count gave a longest chain of 9,000. The weak hash only looks at the first letter and the length, so every four-digit name from `tool_1000` to `tool_9999` lands in the same bucket, which turns `find` into an O(n) search.

`add()` is amortized O(1) because a resize from n to 2n buckets costs O(n), but it only happens after about n/2 cheap inserts since the last resize. Spreading that cost across those inserts adds a constant amount to each one, so the average cost of `add()` stays O(1).

## 2. Pointer stability

The buckets store `std::unique_ptr<Tool>` instead of `Tool` objects. When the table resizes, only the `unique_ptr`s are moved into the new buckets, and moving a `unique_ptr` just copies the address it holds while the `Tool` itself stays in the same place in memory. That means a `Tool*` returned by `find()` before a resize still points to the same object afterwards. My `reg_10000_tools` test checks this by saving the pointer to `tool_0` before inserting 9,999 more tools and confirming it is the same pointer at the end.

## 3. Calculator trace

For `2 * (3 + 4) - 5`, the evaluator pushes 2 onto the values stack, then pushes `*` and `(` onto the ops stack. It pushes 3, then `+` (the top of ops is `(`, so nothing is applied), then 4, leaving values as `2 3 4` and ops as `* ( +`.

When the `)` arrives, the evaluator applies operators until it reaches the `(`. It pops `+`, pops 4 as the right operand and 3 as the left, and pushes 7, so values becomes `2 7`. Then it pops the `(`, leaving `*` on ops. When `-` arrives, the `*` on top has precedence 2, which is greater than or equal to the 1 of `-`, so it is applied first: 2 times 7 gives 14. Then `-` and 5 are pushed, leaving values as `14 5`. At the end, the remaining `-` is applied to get 14 minus 5, and the single value left, 9, is the answer. The greater-than-or-equal rule makes multiplication happen first and makes `10 - 4 - 3` go left to right.

## 4. Errors as values

In an agent loop, a tool failing is normal: the model might ask for a missing file, try to leave the workspace, or divide by zero. These are expected outcomes that the model needs to read and respond to, so they are returned as plain values, like `ToolResult::failure` or a `false` with a message from `resolve` and `calc_evaluate`. Exceptions are saved for programmer mistakes, like calling `as_string()` on a JSON number, because those mean there is a bug in the C++ code and the program should fail loudly. The dispatcher still catches anything a tool throws, so one bad tool cannot crash the harness.

## 5. What I would design differently

I would store each tool's name or hash next to its `unique_ptr` in the bucket. Right now `find()` and `rehash()` call the virtual `name()` function for every entry they look at, and that creates a new `std::string` each time. Caching it would make lookups and resizes faster without changing the public interface.
