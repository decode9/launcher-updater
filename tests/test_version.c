#include "testlib.h"
#include "../src/core/version.h"

TEST(parse)
{
    version v;
    CHECK(version_parse("0.1.0", &v) && v.major == 0 && v.minor == 1 && v.patch == 0);
    CHECK(version_parse("10.20.4294967295", &v) && v.patch == 4294967295u);
    CHECK(!version_parse("1.2", &v));
    CHECK(!version_parse("1.2.3.4", &v));
    CHECK(!version_parse("01.2.3", &v));
    CHECK(!version_parse("1.2.x", &v));
    CHECK(!version_parse("1..3", &v));
    CHECK(!version_parse("1.2.4294967296", &v));
    CHECK(!version_parse("", &v));
}

TEST(compare)
{
    int r;
    CHECK(version_compare_str("0.1.0", "0.1.0", &r) && r == 0);
    CHECK(version_compare_str("0.1.0", "0.2.0", &r) && r == -1);
    CHECK(version_compare_str("0.10.0", "0.9.9", &r) && r == 1);
    CHECK(version_compare_str("1.0.0", "0.99.99", &r) && r == 1);
    CHECK(version_compare_str("1.0.1", "1.0.10", &r) && r == -1);
    CHECK(!version_compare_str("1.0", "1.0.0", &r));
}

int main(void)
{
    RUN(parse);
    RUN(compare);
    return test_report();
}
