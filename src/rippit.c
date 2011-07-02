// rippit - A no-nonsense program to rip audio CDs
//
// Copyright (C) 2011 Trever Fischer <tdfischer@fedoraproject.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <string.h>
#include <musicbrainz3/mb_c.h>
#include <stdio.h>

#define RIPPIT_VERSION_MAJOR 0
#define RIPPIT_VERSION_MINOR 0
#define RIPPIT_VERSION_MICRO 1

#define RIPPIT_VERSION \
    ((RIPPIT_VERSION_MAJOR << 8) | \
     (RIPPIT_VERSION_MINOR << 4) | \
     (RIPPIT_VERSION_MICRO))

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

#define RIPPIT_VERSION_STRING \
    STRINGIZE(RIPPIT_VERSION_MAJOR) "." \
    STRINGIZE(RIPPIT_VERSION_MINOR) "." \
    STRINGIZE(RIPPIT_VERSION_MICRO)

static GMainLoop *loop;
static GstElement *pipeline;
static GstElement *filesink;
static GstElement *cdsrc;
static GstTagSetter *tag_setter;
static MbRelease discData;
static gboolean gotData = FALSE;
static int curTrack = 0;
static guint64 trackCount = 0;

static gboolean printVersion = FALSE;

static GOptionEntry entries[] =
{
    { "version", 'v', 0, G_OPTION_ARG_NONE, &printVersion, "Display version", NULL }
};

static guint64 getPos()
{
    guint64 pos;
    GstFormat format = GST_FORMAT_TIME;
    gst_element_query_position (GST_ELEMENT(pipeline), &format, &pos);
    return pos;
}

static guint64 getDuration()
{
    guint64 duration;
    GstFormat format = GST_FORMAT_TIME;
    gst_element_query_duration(GST_ELEMENT(pipeline), &format, &duration);
    return duration;
}

static int printProgress(gpointer data)
{
    int pos = ((double)getPos()/(double)getDuration())*100;
    g_printf("%3.d%%\r", pos);
}

static void startNextTrack()
{
    gchar artistName[256];
    gchar trackName[256];
    gchar albumName[256];
    gchar *outname;
    MbTrack track;
    MbArtist artist;
    GstTagList *tags;
    curTrack++;
    if (curTrack > trackCount) {
        g_main_loop_quit(loop);
        return;
    }

    if (curTrack > 1) {
        printProgress(NULL);
        g_print("\n");
    }

    GST_DEBUG("Starting with track %d", curTrack);

    track = mb_release_get_track(discData, curTrack-1);
    artist = mb_track_get_artist(track);
    if (!artist) {
        artist = mb_release_get_artist(discData);
    }
    mb_artist_get_name(artist, artistName, 256);
    mb_track_get_title(track, trackName, 256);
    mb_release_get_title(discData, albumName, 256);
    outname = g_strdup_printf("%s - %s.flac", artistName, trackName);

    gst_element_set_state(pipeline, GST_STATE_NULL);

    g_object_set(G_OBJECT(filesink), "location", outname, NULL);
    g_print("Writing to %s:\n", outname);

    tags = gst_tag_list_new_full(
        GST_TAG_TITLE, trackName,
        GST_TAG_ARTIST, artistName,
        GST_TAG_ALBUM, albumName,
        GST_TAG_APPLICATION_NAME, "rippit",
        GST_TAG_TRACK_NUMBER, curTrack,
        NULL
    );

    gst_element_set_state(pipeline, GST_STATE_READY);
    gst_tag_setter_merge_tags(tag_setter, tags, GST_TAG_MERGE_REPLACE_ALL);

    gst_tag_list_free(tags);

    g_free(outname);
    g_object_set(G_OBJECT(cdsrc), "track", curTrack, NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_idle_add(printProgress, NULL);
}

static gboolean eos_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    startNextTrack();
}

static gboolean state_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GstState oldState;
    GstState newState;
    GstState pendingState;
    gchar *name;

    gst_message_parse_state_changed(msg, &oldState, &newState, &pendingState);
    name = gst_element_get_name(msg->src);
    GST_DEBUG("Element %s changed state from %s to %s", name, gst_element_state_get_name(oldState), gst_element_state_get_name(newState));
    g_free(name);
}

static gboolean tag_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GstTagList *tags = NULL;
    gchar *id_str;
    gst_message_parse_tag(msg, &tags);
    if (!gotData && gst_tag_list_get_string(tags, GST_TAG_CDDA_MUSICBRAINZ_DISCID, &id_str)) {
        int releases;
        gotData = TRUE;
        GST_DEBUG("Got MusicBrainz id %s", id_str);
        MbWebService svc = mb_webservice_new();
        MbQuery q = mb_query_new(svc, "rippit-0.1");
        MbReleaseFilter filter = mb_release_filter_disc_id(mb_release_filter_new(), id_str);
        MbResultList results = mb_query_get_releases(q, filter);
        releases = mb_result_list_get_size(results);
        if (releases > 0) {
            discData = mb_result_list_get_release(results, 0);
        } else {
            gchar *toc;
            gchar **tocParts;
            GString *encodedToc;
            int i = 0;

            gst_tag_list_get_string(tags, GST_TAG_CDDA_MUSICBRAINZ_DISCID_FULL, &toc);
            encodedToc = g_string_new(0);
            tocParts = g_strsplit(toc, " ", 0);
            i = 0;
            while(tocParts[i]) {
                int framePos;
                sscanf(tocParts[i], "%x", &framePos);
                g_string_append_printf(encodedToc, "%d+", framePos);
                i++;
            }
            g_strfreev(tocParts);
            g_string_truncate(encodedToc, encodedToc->len-1);

            g_print("Could not get musicbrainz information for this disc.\n");
            g_print("Please visit the following url to contribute disc information:\n");
            g_print("http://musicbrainz.org/bare/cdlookup.html?id=%s&tracks=%d&toc=%s\n", id_str, (int)trackCount, encodedToc->str);

            g_string_free(encodedToc, TRUE);
            g_main_loop_quit(loop);
            return;
        }
        GST_DEBUG("Got %d results", releases);
        mb_result_list_free(results);
        mb_release_filter_free(filter);
        mb_query_free(q);
        mb_webservice_free(svc);
        g_free(id_str);
        startNextTrack();
    }
    gst_tag_list_free(tags);
}

static gboolean error_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GError *err;
    gchar *debug;
    gst_message_parse_error(msg, &err, &debug);
    g_free(debug);
    g_warning("%d %d: %s", err->domain, err->code, err->message);
    g_main_loop_quit(loop);
    g_error_free(err);
}

static gboolean warning_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GError *err;
    gchar *debug;
    gst_message_parse_warning(msg, &err, &debug);
    g_free(debug);
    g_warning("%d %d: %s", err->domain, err->code, err->message);
    g_error_free(err);
}


static GstElement *buildPipeline()
{
    GstElement *pipe = gst_pipeline_new(NULL);

    GstElement *cdSource = gst_element_factory_make("cdparanoiasrc", NULL);
    GstElement *encoder = gst_element_factory_make("flacenc", NULL);
    GstElement *tagger = gst_element_factory_make("flactag", NULL);
    GstElement *output = gst_element_factory_make("filesink", NULL);

    g_object_set(G_OBJECT(output), "location", "/dev/null", NULL);

    tag_setter = GST_TAG_SETTER(tagger);
    filesink = output;
    cdsrc = cdSource;

    gst_bin_add_many(GST_BIN(pipe), cdSource, encoder, tagger, output, NULL);
    gst_element_link_many(cdSource, encoder, tagger, output, NULL);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipe));
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message::error", G_CALLBACK(error_cb), pipe);
    g_signal_connect(bus, "message::warning", G_CALLBACK(warning_cb), pipe);
    g_signal_connect(bus, "message::state-changed", G_CALLBACK(state_cb), pipe);
    g_signal_connect(bus, "message::tag", G_CALLBACK(tag_cb), pipe);
    g_signal_connect(bus, "message::eos", G_CALLBACK(eos_cb), pipe);

    return pipe;
}

int main(int argc, char* argv[])
{
    GError *error;
    GOptionContext *context;

    g_thread_init(NULL);

    context = g_option_context_new(" - Rip an audio CD, without any nonsense.");
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gst_init_get_option_group());

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("Bad arguments: %s\n", error->message);
        exit(1);
    }

    if (!gst_init_check(&argc, &argv, &error)) {
        g_print("Could not initialize GStreamer: %s\n", error->message);
        exit(1);
    }

    if (printVersion) {
        g_print("rippit version %s\n", RIPPIT_VERSION_STRING);
        exit(0);
    }

    pipeline = buildPipeline();

    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    GstFormat format = gst_format_get_by_nick("track");
    gst_element_query_duration(pipeline, &format, &trackCount);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
}
