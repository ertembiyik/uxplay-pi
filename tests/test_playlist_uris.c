#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airplay_video.h"

static char *owned(const char *value) {
    char *copy = strdup(value);
    assert(copy);
    return copy;
}

static void expect_resolved(const char *playlist, const char *playlist_url, const char *expected) {
    char *resolved = resolve_playlist_resource_uris(owned(playlist), playlist_url);
    if (strcmp(resolved, expected)) {
        fprintf(stderr, "playlist mismatch\nexpected:\n%s\nactual:\n%s\n", expected, resolved);
        abort();
    }
    free(resolved);
}

static void test_media_resource_uris(void) {
    expect_resolved(
        "#EXTM3U\r\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\r\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"/keys/1.bin\"\r\n"
        "#EXTINF:4.0,\r\n"
        "11_5.mp4?token=abc\r\n"
        "#EXTINF:4.0,\n"
        "//media.example.net/12.ts\n"
        "#EXTINF:4.0,\n"
        "https://other.example/13.ts\n",
        "https://video.example/vod/720/index.m3u8?playlist-token=xyz",
        "#EXTM3U\r\n"
        "#EXT-X-MAP:URI=\"https://video.example/vod/720/init.mp4\"\r\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"https://video.example/keys/1.bin\"\r\n"
        "#EXTINF:4.0,\r\n"
        "https://video.example/vod/720/11_5.mp4?token=abc\r\n"
        "#EXTINF:4.0,\n"
        "https://media.example.net/12.ts\n"
        "#EXTINF:4.0,\n"
        "https://other.example/13.ts\n");

    expect_resolved(
        "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://license\"\n",
        "https://video.example/master.m3u8",
        "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://license\"\n");
}

static void test_master_rendition_uris(void) {
    expect_resolved(
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",URI=\"audio/12_2.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1800000,RESOLUTION=1920x1080,AUDIO=\"audio\"\n"
        "video/11_5.m3u8\n",
        "https://video.example/vod/master.m3u8?token=xyz",
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",URI=\"https://video.example/vod/audio/12_2.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1800000,RESOLUTION=1920x1080,AUDIO=\"audio\"\n"
        "https://video.example/vod/video/11_5.m3u8\n");
}

static void test_resolution_limit(void) {
    int removed = 0;
    char *filtered = limit_master_playlist_resolution(
        owned("#EXTM3U\n"
              "#EXT-X-STREAM-INF:BANDWIDTH=4000000,RESOLUTION=3840x2160\n"
              "4k.m3u8\n"
              "#EXT-X-STREAM-INF:BANDWIDTH=1800000,RESOLUTION=1920x1080\n"
              "1080.m3u8\n"),
        1920, 1080, &removed);
    assert(removed == 1);
    assert(!strstr(filtered, "3840x2160"));
    assert(!strstr(filtered, "4k.m3u8"));
    assert(strstr(filtered, "1920x1080"));
    assert(strstr(filtered, "1080.m3u8"));
    free(filtered);
}

int main(void) {
    test_media_resource_uris();
    test_master_rendition_uris();
    test_resolution_limit();
    return 0;
}
