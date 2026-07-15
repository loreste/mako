# Package-per-directory

All non-test `.mko` files in a directory form **one package** (same `pack` name).

```
util/
  lib.mko      # pack util · greet
  more.mko     # pack util · shout (calls greet)
  mako.toml
app/
  main.mko     # pulls util as one unit
  mako.toml
```

```bash
mako check examples/pkg_per_dir -p app
mako run examples/pkg_per_dir -p app
# prints: hi mako!
```
