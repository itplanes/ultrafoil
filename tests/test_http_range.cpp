#include "util/http_range.hpp"
#include <cassert>
int main() {
    using inst::http::ValidContentRange;
    assert(ValidContentRange("bytes 100-199/1000\r\n", 100, 100));
    assert(ValidContentRange("bytes 0-9/*", 0, 10));
    assert(!ValidContentRange("bytes 0-99/1000", 100, 100));
    assert(!ValidContentRange("bytes 100-198/1000", 100, 100));
    assert(!ValidContentRange("bytes 100-199/199", 100, 100));
    assert(!ValidContentRange("bytes 100-199/1000junk", 100, 100));
    assert(!ValidContentRange("bytes -1-199/1000", 100, 100));
    assert(!ValidContentRange("bytes 0-0/1", UINT64_MAX, 2));
}
