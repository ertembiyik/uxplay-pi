#undef NDEBUG
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

static void unexpected_audio(void *cls, raop_ntp_t *ntp, audio_decode_struct *data) {
    abort();
}

static void unexpected_video(void *cls, raop_ntp_t *ntp, video_decode_struct *data) {
    abort();
}

static void test_signed_media_playlist_round_trip(void) {
    const char origin[] = "https://video.example";
    char proxy[] = "http://localhost:45595";
    char master[] =
        "#EXTM3U\r\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",URI=\"https://video.example/audio.m3u8?sig=a,b%2B\"\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1800000,AUDIO=\"audio\"\r\n"
        "https://video.example/match/media.m3u8?sig=one&next=https://video.example/next\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900000\n"
        "https://video.example/match/media.m3u8?sig=one\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=450000\n"
        "https://video.example/plain.m3u8";
    const struct {
        const char *remote;
        const char *target;
        const char *media;
    } variants[] = {
        {"https://video.example/audio.m3u8?sig=a,b%2B", "/audio.m3u8?sig=a,b%2B",
         "#EXTM3U\n#EXTINF:4,\naudio.aac\n#EXT-X-ENDLIST\n"},
        {"https://video.example/match/media.m3u8?sig=one&next=https://video.example/next",
         "/match/media.m3u8?sig=one&next=https://video.example/next",
         "#EXTM3U\n#EXTINF:4,\nhigh.ts\n#EXT-X-ENDLIST\n"},
        {"https://video.example/match/media.m3u8?sig=one", "/match/media.m3u8?sig=one",
         "#EXTM3U\n#EXTINF:4,\nlow.ts\n#EXT-X-ENDLIST\n"},
        {"https://video.example/plain.m3u8", "/plain.m3u8",
         "#EXTM3U\n#EXTINF:4,\nplain.ts\n#EXT-X-ENDLIST\n"},
    };

    raop_callbacks_t callbacks = {.audio_process = unexpected_audio, .video_process = unexpected_video};
    raop_t *raop = raop_init(&callbacks);
    assert(raop);
    assert(raop_init2(raop, 0, "02:00:00:00:00:01", "") == 0);
    airplay_video_t *video = airplay_video_init(raop, 45595, NULL, NULL, NULL);
    assert(video);
    set_uri_prefix(video, origin, strlen(origin));

    char **uris = NULL;
    int num_uris = 0;
    assert(create_media_uri_table(origin, master, strlen(master), &uris, &num_uris) == 0);
    assert(num_uris == sizeof(variants) / sizeof(variants[0]));
    create_media_data_store(video, uris, num_uris);
    free(uris);

    char *local_master = adjust_master_playlist(master, strlen(master), origin, proxy);
    assert(local_master);
    for (int i = 0; i < num_uris; i++) {
        assert(strcmp(get_media_uri_by_num(video, i), variants[i].remote) == 0);
        char local_uri[256];
        snprintf(local_uri, sizeof(local_uri), "%s%s", proxy, variants[i].target);
        assert(strstr(local_master, local_uri));

        int count = 1;
        float duration = 4;
        bool endlist = true;
        assert(store_media_playlist(video, owned(variants[i].media), &count, &duration, &endlist, i) == 0);
    }
    free(local_master);

    for (int i = 0; i < num_uris; i++) {
        int count = 0;
        float duration = 0;
        bool should_fetch;
        char *remote_uri;
        char *media = get_media_playlist(video, &count, &duration, variants[i].target, &should_fetch, &remote_uri);
        assert(media && strcmp(media, variants[i].media) == 0);
        assert(!should_fetch && !remote_uri);
        free(media);
    }

    int count = 0;
    float duration = 0;
    bool should_fetch;
    char *remote_uri;
    assert(!get_media_playlist(video, &count, &duration, "/match/media.m3u8", &should_fetch, &remote_uri));
    assert(!should_fetch && !remote_uri);
    airplay_video_destroy(video);
    raop_destroy(raop);
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
    test_signed_media_playlist_round_trip();
    test_resolution_limit();
    return 0;
}
