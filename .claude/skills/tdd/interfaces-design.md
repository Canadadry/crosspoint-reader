# Interface Design for Testability

Good interfaces make testing natural. Names are illustrative.

1. **Accept dependencies, don't create them**

   ```c
   /* Testable */
   void hud_draw(const hud_t *hud, const p8_graphics_t *gfx, p8_vram_t *mem);

   /* Hard to test */
   void hud_draw(const hud_t *hud);   /* draws into a global screen */
   ```

2. **Return results, don't produce side effects**

   ```c
   /* Testable */
   int trade_route_income(const trade_route_t *route);

   /* Hard to test */
   void trade_route_pay(const trade_route_t *route);  /* adds into global credits */
   ```

3. **Small surface area**
   - Fewer functions in the header = fewer tests needed
   - Fewer parameters = simpler test setup

4. **Make illegal states unrepresentable**

   A field meaningful only in some cases says the type is wrong. Here, presence is a bit in `has`, never a NULL pointer (see `warn-review`'s pointer rules):

   ```c
   /* Allows an impossible state: a target pointer that outlived its entity */
   typedef struct { entity_t *target; } probe_t;

   /* A checked id and a presence bit */
   typedef struct { uint16_t has; entity_id_t target; } probe_t;
   ```

5. **Keep functions pure; push state to the top**

   Core logic is pure: same input, same output, no I/O, no global. A thin top layer holds the state and calls into it.

   ```c
   /* Pure: trivial to test */
   vec_t camera_clamp(vec_t wanted, box_t bounds);

   /* Stateful: orchestrates, stays at the top */
   void camera_update(game_t *game) {
       game->camera = camera_clamp(camera_follow(game), game->bounds);
   }
   ```

6. **Assert preconditions at the boundary**

   Every public function asserts what it requires, so invalid input fails at the call site, not three frames later.

   ```c
   void slipway_connect(game_t *game, entity_id_t from, entity_id_t to) {
       assert(from != to && "a slipway joins two different planets");
       assert(planet_by_id(game, from) != NULL && "from is dead");
       /* ... */
   }
   ```

   Assertions are not input validation for player data — they are contracts between modules. Key invariants inside a module ("no dead id in a live list") belong as assertions too, not just as tests.
