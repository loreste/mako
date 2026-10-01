// Keep the map reachable: LSan must detect lost temporaries, not map retention.
#define _GNU_SOURCE
#include "mako_rt.h"
#include "mako_cmap.h"
#include <assert.h>

static MakoCMap *cache;

int main(void) {
    cache = mako_cmap_new();
    MakoString key = mako_str_view("counter", 7);
    MakoString prefix = mako_str_view("prefix|", 7);
    MakoString composite = mako_str_view("prefix|counter", 14);
    MakoString missing = mako_str_view("missing", 7);
    MakoString empty = mako_str_view("", 0);
    const int64_t values[] = {0, -1, 42, INT64_MIN, INT64_MAX};

    for (int i = 0; i < 1000; ++i) {
        int64_t value = values[i % 5];
        assert(mako_cmap_get_int(cache, missing, -17) == -17);
        assert(mako_cmap_get_int2(cache, prefix, missing, -17) == -17);

        // Replacements and decimal round trips, including both int64 limits.
        mako_cmap_set_int(cache, key, value);
        mako_cmap_set_int2(cache, prefix, key, value);
        assert(mako_cmap_get_int(cache, key, -17) == value);
        assert(mako_cmap_get_int2(cache, prefix, key, -17) == value);

        // Unlike a missing key, each empty read allocates an owned byte (#77).
        mako_cmap_set(cache, key, empty);
        mako_cmap_set(cache, composite, empty);
        assert(mako_cmap_get_int(cache, key, -17) == -17);
        assert(mako_cmap_get_int2(cache, prefix, key, -17) == -17);
    }
    assert(mako_cmap_len(cache) == 2);
    puts("CMap integer ownership: 1000 iterations passed");
    return 0;
}
