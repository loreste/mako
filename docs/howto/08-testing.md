# Testing

`TestXxx` in `*_test.mko`:

```bash
mako test examples/testing
mako test examples/testing -r TestAdd -v
mako test examples/testing --count 2
mako test examples/testing/tooling_quality_test.mko --coverage -v
```

```mko
fn TestAdd() {
    assert_eq(1 + 1, 2)
}
```

Helpers: `assert`, `assert_eq`, `assert_eq_str`, `t_run`.
Category-style zero-arg functions also run in the same harness:
`FuzzXxx`, `PropertyXxx`, `SnapshotXxx`, `MockXxx`, and `FixtureXxx`.
Live optional smokes use env flags (see RELEASE.md) and skip without deps.
