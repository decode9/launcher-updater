#include "testlib.h"
#include "../src/core/str.h"

TEST(decimal)
{
    char out[21];
    uint64_t v;
    u64_to_dec(0, out);
    CHECK_STR(out, "0");
    u64_to_dec(1600000000ULL, out);
    CHECK_STR(out, "1600000000");
    u64_to_dec(18446744073709551615ULL, out);
    CHECK_STR(out, "18446744073709551615");
    CHECK(dec_to_u64("18446744073709551615", 20, &v) && v == 18446744073709551615ULL);
    CHECK(!dec_to_u64("18446744073709551616", 20, &v));
    CHECK(!dec_to_u64("", 0, &v));
    CHECK(!dec_to_u64("12a", 3, &v));
}

TEST(hex)
{
    uint8_t b[2];
    char out[5];
    CHECK(hex_decode("a1ff", 4, b, 2) && b[0] == 0xa1 && b[1] == 0xff);
    CHECK(!hex_decode("A1FF", 4, b, 2)); /* uppercase is not the spec's format */
    CHECK(!hex_decode("a1f", 3, b, 2));
    hex_encode(b, 2, out);
    CHECK_STR(out, "a1ff");
}

TEST(builder)
{
    char buf[8];
    strbuf sb;
    sb_init(&sb, buf, sizeof buf);
    sb_add(&sb, "ab");
    sb_add_i64(&sb, -42);
    CHECK(sb.ok);
    CHECK_STR(buf, "ab-42");
    sb_add(&sb, "xyz"); /* 8 bytes + NUL does not fit */
    CHECK(!sb.ok);
    CHECK_STR(buf, "ab-42");
}

int main(void)
{
    RUN(decimal);
    RUN(hex);
    RUN(builder);
    return test_report();
}
