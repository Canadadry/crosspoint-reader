# Good and Bad Tests

Tests live in `tests/test_<module>.c`: `static void` test functions using `assert`, called from `main`. Real APIs below where they exist; other names are illustrative.

## Good Tests

**Integration-style**: test through the module's public header, not its internals.

```c
/* GOOD: observable behavior, through the public interface */
static void test_add_component_wise(void) {
    vec_t a = vec_make(fixed_test_double_from_double(1.0), fixed_test_double_from_double(2.0));
    vec_t b = vec_make(fixed_test_double_from_double(3.5), fixed_test_double_from_double(-1.0));
    vec_t result = vec_add(a, b);
    assert(result.x == fixed_test_double_from_double(4.5));
    assert(result.y == fixed_test_double_from_double(1.0));
}
```

Characteristics:

- Tests behavior callers care about
- Includes only the public header
- Survives internal refactors
- Name describes WHAT, not HOW
- One logical assertion per test

For a path only a player reaches (a click, a rendered frame), drive it headless through the same entry point the player hits — input, update, draw — and assert on the framebuffer or the game state, never by calling the inner function directly.

## Bad Tests

**Implementation-detail tests**: coupled to internal structure.

```c
/* BAD: reaches into a static helper by including the .c */
#include "game/trade.c"
static void test_trade_uses_route_cache(void) {
    route_cache_fill(&cache, planets, 3);
    assert(cache.count == 3);
}
```

Red flags:

- Including a `.c` file, or a test-only header exposing statics
- Asserting on call counts or call order
- Test breaks when refactoring without behavior change
- Test name describes HOW, not WHAT
- Verifying through a side channel instead of the interface
- Replaying a recorded session to reach a state a few calls could build

## Assertions and Tests: Complementary Layers

Tests verify behavior from the outside. Assertions enforce contracts from the inside. Both are needed.

When production code asserts, a failing test tells you not just _that_ something broke but _where_ the contract was violated:

```c
/* Without assertion: a bad index reads past the table, fails frames later */
int planet_kind_price(int kind) {
    return kind_prices[kind];
}

/* With assertion: fails at the violated contract */
int planet_kind_price(int kind) {
    assert(kind >= 0 && kind < PLANET_KIND_COUNT && "planet kind out of range");
    return kind_prices[kind];
}
```

Rule of thumb: if a test setup must respect a constraint to call a function, that constraint is asserted inside the function.

```c
/* BAD: verifies by poking the registry's storage */
static void test_spawn_adds_entity(void) {
    entity_id_t id = planet_spawn(&game, at);
    assert(game.registry.live.items[game.registry.live.count - 1] == id);
}

/* GOOD: verifies through the interface */
static void test_spawned_planet_is_found_by_id(void) {
    entity_id_t id = planet_spawn(&game, at);
    assert(planet_by_id(&game, id) != NULL);
}
```
