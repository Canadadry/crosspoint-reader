# When to Use a Test Double

Replace only **system boundaries**:

- Memory: the `allocator_t` a module receives (`tests/heap_allocator.h` backs it with `malloc`)
- Input: a scripted events file loaded into `p8_input_t`, never a real mouse
- Output: `p8_graphics_t` and `p8_vram_t`, read back as a framebuffer — never a window
- The raylib backend: never linked into a unit test

Don't replace:

- Your own modules
- Internal collaborators
- Anything in `src/game/` or `src/engine/` a test can call for real
- Randomness: `p8_fixed_rnd` already starts from a fixed seed, so a run is reproducible as is

## Designing for Test Doubles

**1. Pass the dependency in**

The module receives its allocator, its graphics or its input as a parameter instead of reaching for a global:

```c
/* Easy to test: the caller chooses the allocator (real signature) */
void entity_list_init(entity_list_t *entity_list, const allocator_t *zone);

/* Hard to test: hidden dependency on the game's zone */
void entity_list_init(entity_list_t *entity_list);
```

The test then passes `&heap_allocator`; the game passes a zone allocator.

**2. One narrow function per boundary operation**

An `allocator_t` has `alloc`, `grow` and `release` — not one `do(op, ...)` entry point with a switch. Each double then implements one plain behavior:

- No conditional logic in the test setup
- Easy to see which operations a test exercises
- A refusing double (`allocator_grow_refused`) tests the full-zone path in one line
