#include "testlib.h"
#include "../src/core/sha256.h"
#include "../src/core/str.h"

static void hex_of(const void *data, size_t n, char out[65])
{
    uint8_t digest[SHA256_BYTES];
    sha256(data, n, digest);
    hex_encode(digest, SHA256_BYTES, out);
}

TEST(known_vectors)
{
    char hex[65];
    hex_of("", 0, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    hex_of("abc", 3, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(million_a_incremental)
{
    static uint8_t chunk[1001];
    sha256_ctx ctx;
    uint8_t digest[SHA256_BYTES];
    char hex[65];
    int i;
    memset(chunk, 'a', sizeof chunk);
    sha256_init(&ctx);
    for (i = 0; i < 1000; i++) sha256_update(&ctx, chunk, i % 2 ? 999 : 1001); /* odd splits */
    sha256_final(&ctx, digest);
    hex_encode(digest, SHA256_BYTES, hex);
    CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(block_boundaries)
{
    /* 55, 56 and 64 bytes exercise the padding edge cases; compare one-shot with byte-wise. */
    static const size_t sizes[] = {55, 56, 63, 64, 65, 119, 120};
    uint8_t data[128], a[SHA256_BYTES], b[SHA256_BYTES];
    size_t s, i;
    for (i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7);
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        sha256_ctx ctx;
        sha256(data, sizes[s], a);
        sha256_init(&ctx);
        for (i = 0; i < sizes[s]; i++) sha256_update(&ctx, data + i, 1);
        sha256_final(&ctx, b);
        CHECK(memcmp(a, b, SHA256_BYTES) == 0);
    }
}

int main(void)
{
    RUN(known_vectors);
    RUN(million_a_incremental);
    RUN(block_boundaries);
    return test_report();
}
