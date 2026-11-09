#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "http_request.h"

static http_request_t *event_request(void) {
    http_request_t *request = http_request_init();
    assert(request && http_request_upgrade_data(request) == 0);
    return request;
}

static void test_fragmented_and_coalesced_frames(void) {
    http_request_t *request = event_request();
    const char start[] = " \r\nHTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nA";
    const char end[] = "BCEVENT/1.0 200 OK\r\nX-Note: Content-Length: 9\r\n\r\n";
    char *header, *body;
    size_t header_len, body_len;

    assert(http_request_add_event_data(request, start, sizeof(start) - 1) == 0);
    assert(http_request_parse_event_data(request, &header, &header_len, &body, &body_len) == 0);
    assert(http_request_add_event_data(request, end, sizeof(end) - 1) == 0);
    assert(http_request_parse_event_data(request, &header, &header_len, &body, &body_len) == 1);
    assert(strncmp(header, "HTTP/1.1", 8) == 0 && body_len == 3 && memcmp(body, "ABC", 3) == 0);
    free(header);
    free(body);

    assert(http_request_parse_event_data(request, &header, &header_len, &body, &body_len) == 1);
    assert(strncmp(header, "EVENT/1.0", 9) == 0 && body_len == 0 && body == NULL);
    free(header);
    assert(http_request_parse_event_data(request, &header, &header_len, &body, &body_len) == 0);
    http_request_destroy(request);
}

static void test_invalid_lengths(void) {
    const char *frames[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551615\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 1\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        http_request_t *request = event_request();
        char *header, *body;
        size_t header_len, body_len;
        assert(http_request_add_event_data(request, frames[i], strlen(frames[i])) == 0);
        assert(http_request_parse_event_data(request, &header, &header_len, &body, &body_len) == -1);
        http_request_destroy(request);
    }
}

int main(void) {
    test_invalid_lengths();
    test_fragmented_and_coalesced_frames();
}
