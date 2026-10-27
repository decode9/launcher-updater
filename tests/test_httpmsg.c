#include "testlib.h"
#include "../src/core/httpmsg.h"

static http_head h;

static httpmsg_error head(const char *text)
{
    size_t n = http_head_length(text, strlen(text));
    return http_parse_head(text, n ? n : strlen(text), &h);
}

TEST(build_requests)
{
    char buf[512];
    strbuf sb;
    http_range r = {100, 199, 0};
    sb_init(&sb, buf, sizeof buf);
    CHECK(http_build_get(&sb, "play.example.com", 5103, "/v1/status", 0));
    CHECK_STR(buf, "GET /v1/status HTTP/1.1\r\nHost: play.example.com:5103\r\nUser-Agent: launcher\r\n"
                   "Accept-Encoding: identity\r\nConnection: close\r\n\r\n");
    sb_init(&sb, buf, sizeof buf);
    CHECK(http_build_get(&sb, "h", 443, "/v1/blob/ab", &r));
    CHECK(strstr(buf, "Host: h\r\n") != 0);
    CHECK(strstr(buf, "Range: bytes=100-199\r\n") != 0);
    r.open_end = 1;
    r.first = 5000000000ULL;
    sb_init(&sb, buf, sizeof buf);
    CHECK(http_build_get(&sb, "h", 443, "/x", &r));
    CHECK(strstr(buf, "Range: bytes=5000000000-\r\n") != 0);
    sb_init(&sb, buf, sizeof buf);
    CHECK(!http_build_get(&sb, "h", 443, "/a b", 0));
    CHECK(!http_build_get(&sb, "h", 443, "/a\r\nX: y", 0));
    CHECK(!http_build_get(&sb, "h", 443, "relative", 0));
    sb_init(&sb, buf, 10);
    CHECK(!http_build_get(&sb, "h", 443, "/x", 0));
}

TEST(head_length)
{
    CHECK_EQ_U64(http_head_length("HTTP/1.1 200 OK\r\n\r\nbody", 23), 19);
    CHECK_EQ_U64(http_head_length("HTTP/1.1 200 OK\r\n\r", 18), 0);
}

TEST(ok_response)
{
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\ncontent-length:  1600000000 \r\n\r\n") == HTTPMSG_OK);
    CHECK_EQ_U64(h.status, 200);
    CHECK(h.has_length);
    CHECK_EQ_U64(h.content_length, 1600000000ULL);
    CHECK(!h.has_range);
    CHECK(head("HTTP/1.0 404 Not Found\r\n\r\n") == HTTPMSG_OK);
    CHECK_EQ_U64(h.status, 404);
    CHECK(!h.has_length);
    CHECK(head("HTTP/1.1 204\r\n\r\n") == HTTPMSG_OK);
}

TEST(partial_content)
{
    CHECK(head("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 8388608-16777215/1600000000\r\n"
               "Content-Length: 8388608\r\n\r\n") == HTTPMSG_OK);
    CHECK_EQ_U64(h.range_first, 8388608);
    CHECK_EQ_U64(h.range_last, 16777215);
    CHECK(h.has_total);
    CHECK_EQ_U64(h.range_total, 1600000000ULL);
    CHECK(head("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-9/*\r\nContent-Length: 10\r\n\r\n") == HTTPMSG_OK);
    CHECK(!h.has_total);
    CHECK(head("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */1000\r\n\r\n") == HTTPMSG_OK);
    CHECK(!h.has_range && h.has_total && h.range_total == 1000);
}

TEST(bad_responses)
{
    CHECK(head("HTTP/2 200\r\n\r\n") == HTTPMSG_ERR_STATUS_LINE);
    CHECK(head("HTTP/1.1 20 OK\r\n\r\n") == HTTPMSG_ERR_STATUS_LINE);
    CHECK(head("HTTP/1.1 200OK\r\n\r\n") == HTTPMSG_ERR_STATUS_LINE);
    CHECK(head("HTTP/1.1 200 OK\r\nNoColon\r\n\r\n") == HTTPMSG_ERR_HEADER);
    CHECK(head("HTTP/1.1 200 OK\r\nBad Name: x\r\n\r\n") == HTTPMSG_ERR_HEADER);
    CHECK(head("HTTP/1.1 200 OK\r\nX: a\nb\r\n\r\n") == HTTPMSG_ERR_HEADER);
    CHECK(head("HTTP/1.1 200 OK\r\nX: a\r\n") == HTTPMSG_ERR_HEADER); /* no empty line */
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n") == HTTPMSG_ERR_LENGTH);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 1, 1\r\n\r\n") == HTTPMSG_ERR_LENGTH);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n") == HTTPMSG_ERR_LENGTH);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n") == HTTPMSG_ERR_LENGTH);
    CHECK(head("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") == HTTPMSG_ERR_UNSUPPORTED);
    CHECK(head("HTTP/1.1 206 Partial\r\nContent-Length: 10\r\n\r\n") == HTTPMSG_ERR_RANGE);
    CHECK(head("HTTP/1.1 206 Partial\r\nContent-Range: bytes 0-9/100\r\nContent-Length: 11\r\n\r\n") == HTTPMSG_ERR_RANGE);
    CHECK(head("HTTP/1.1 206 Partial\r\nContent-Range: bytes 9-0/100\r\nContent-Length: 10\r\n\r\n") == HTTPMSG_ERR_RANGE);
    CHECK(head("HTTP/1.1 206 Partial\r\nContent-Range: bytes 0-100/100\r\nContent-Length: 101\r\n\r\n") == HTTPMSG_ERR_RANGE);
    CHECK(head("HTTP/1.1 206 Partial\r\nContent-Range: items 0-9/100\r\nContent-Length: 10\r\n\r\n") == HTTPMSG_ERR_RANGE);
}

int main(void)
{
    RUN(build_requests);
    RUN(head_length);
    RUN(ok_response);
    RUN(partial_content);
    RUN(bad_responses);
    return test_report();
}
