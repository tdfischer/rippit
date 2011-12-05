// rippit - A no-nonsense program to rip multimedia
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

#include "rippit.h"

#include "love.h"
#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <string.h>
#include <musicbrainz3/mb_c.h>
#include <stdio.h>
#include <glib/gprintf.h>

static GMainLoop *loop;
static GstElement *pipeline;
static GstElement *filesink;
static GstElement *cdsrc;
static GstElement *dvdsrc;
static GstTagSetter *tag_setter;
static MbRelease discData = 0;
static gboolean gotData = FALSE;
static int curTrack = 0;
static gint64 trackCount = 0;
static gchar *discID;
static int singleTrack = -1;
static gchar *outputMessage = 0;
static gchar *device = 0;
static guint timeoutSource = 0;

static gboolean printVersion = FALSE;
static gboolean forceRip = FALSE;
static gboolean ignoreStall = FALSE;
static gboolean showSomeLove = FALSE;

static void startNextTrack();
static void printProgress(gboolean updateTicker, gboolean newline);
static gboolean isStalled();
static gboolean checkForStall();

static gchar **extraArgs = 0;

GQuark rippit_error_quark()
{
    return g_quark_from_static_string("rippit-error-quark");
}

static GOptionEntry entries[] =
{
    { "version", 'v', 0, G_OPTION_ARG_NONE, &printVersion, "Display version", NULL },
    { "force-rip", 'f', 0, G_OPTION_ARG_NONE, &forceRip, "Rip the disc, even if there might be big bad errors", NULL},
    { "ignore-bad-tracks", 'i', 0, G_OPTION_ARG_NONE, &ignoreStall, "Skip damanged tracks that would otherwise take ages to recover", NULL},
    { "track", 't', 0, G_OPTION_ARG_INT, &singleTrack, "Only rip the given track", "track"},
    { "love", 'l', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &showSomeLove, "Show some love", NULL},
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &extraArgs, NULL, NULL},
    {NULL}
};

static void linkDecodebin(GstElement *decodebin, gpointer data)
{
    gst_element_link(decodebin, GST_ELEMENT(data));
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "decodebin2-link");
}

static void setOutputMessage(const gchar *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    g_free(outputMessage);
    outputMessage = g_strdup_vprintf(msg, ap);
    va_end(ap);
    printProgress(FALSE, TRUE);
}

static void uncorrectedError_cb(GstElement *element, gint sector, gpointer data)
{
    GST_DEBUG("Disk error in sector %d", sector);
    setOutputMessage("Disk is scratched at sector %d. Data was lost. I'm sorry :(", sector);
}

static void transportError_cb(GstElement *element, gint sector, gpointer data)
{
    GST_DEBUG("Possible disk error in sector %d", sector);
    setOutputMessage("Disk is scratched at sector %d. Recovering...", sector);
}

static guint64 getPos()
{
    gint64 pos = 0;
    GstFormat format = GST_FORMAT_TIME;
    gst_element_query_position (GST_ELEMENT(pipeline), &format, &pos);
    return pos;
}

static guint64 getDuration()
{
    gint64 duration = 0;
    GstFormat format = GST_FORMAT_TIME;
    if (pipeline == NULL)
        return 0;
    if (!gst_element_query_duration(GST_ELEMENT(pipeline), &format, &duration))
        return 0;
    return duration;
}

static gboolean skipIfStalled()
{
    GST_DEBUG("Skipping?");
    if (!isStalled()) {
        g_timeout_add_seconds(5, checkForStall, NULL);
    } else if (ignoreStall) {
        setOutputMessage("Skipping track in the hopes that others may work. Sorry it didn't work out.");
        startNextTrack();
    }
    return FALSE;
}

static gboolean checkForStall()
{
    GST_DEBUG("stall check");
    if (isStalled()) {
        setOutputMessage("Still waiting to decode track. Is the disc scratched?");
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "stalled");
        g_timeout_add_seconds(5, skipIfStalled, NULL);
        return FALSE;
    }
    return TRUE;
}

static gboolean isStalled()
{
    static guint64 lastTrack = -1;
    static guint64 lastPos = 0;

    if (lastTrack != curTrack) {
        lastTrack = curTrack;
        lastPos = 0;
        GST_DEBUG("lastTrack != curTrack");
        return FALSE;
    }

    if (lastPos != getPos()) {
        lastPos = getPos();
        GST_DEBUG("lastPos != getPos");
        return FALSE;
    }
    return TRUE;
}

static gchar *ticker[] = {"-", "\\", "|", "/", '\0'};

static gboolean cb_progress(gpointer data)
{
    printProgress(TRUE, FALSE);
    return TRUE;
}

static void printProgress(gboolean updateTicker, gboolean newline)
{
    int pos = 0;
    static int tickerPos = 0;
    GST_DEBUG("Position %ld/%ld", getPos(), getDuration());
    if (getDuration() > 0)
        pos = ((double)getPos()/(double)getDuration())*100;
    if (updateTicker)
        tickerPos++;
    if (ticker[tickerPos] == '\0')
        tickerPos = 0;
    g_printf("\r%s %3.d%% %s", ticker[tickerPos], pos, outputMessage);
    if (newline)
        g_printf("\n");
    else
        g_printf("\r");
    fflush(stdout);
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

    // Reset the stall detector
    isStalled();

    if (timeoutSource > 0)
        g_source_remove(timeoutSource);
    timeoutSource = g_timeout_add_seconds(5, checkForStall, NULL);

    curTrack++;
    if (curTrack > trackCount || (singleTrack > -1 && curTrack > singleTrack)) {
        g_print("\n");
        setOutputMessage("Complete!");
        g_print("\n");
        g_main_loop_quit(loop);
        return;
    }


    GST_DEBUG("Starting with track %d", curTrack);

    if (!discData) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        outname = g_strdup_printf("DVD %d.mkv", curTrack);
    } else {
        if (!forceRip) {
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
        } else {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            outname = g_strdup_printf("%s - %d.flac", discID, curTrack);
        }
    }

    if (cdsrc) {
        g_object_set(G_OBJECT(cdsrc), "track", curTrack, NULL);
    } else {
        gint64 titleLength = 0;
        gst_element_set_state(dvdsrc, GST_STATE_NULL);
        g_object_set(G_OBJECT(dvdsrc), "title", curTrack, NULL);
        g_object_set(G_OBJECT(dvdsrc), "chapter", 1, NULL);
        gst_element_set_state(dvdsrc, GST_STATE_PAUSED);
        titleLength = getDuration();
        gst_element_set_state(dvdsrc, GST_STATE_NULL);
        if (titleLength == 0) {
            setOutputMessage("Skipping title %d, it appears to be a dummy title.", curTrack);
            startNextTrack();
            g_free(outname);
            return;
        }
    }

    g_print("\n");
    setOutputMessage("Ripping to %s", outname);

    gst_element_set_state(filesink, GST_STATE_NULL);
    g_object_set(G_OBJECT(filesink), "location", outname, NULL);
    gst_element_set_state(filesink, GST_STATE_READY);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, outname);
    g_free(outname);
}

static gboolean element_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    const GstStructure *str = gst_message_get_structure(msg);
    GST_DEBUG("Got element message %s", gst_structure_get_name(str));
    return TRUE;
}

static gboolean eos_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GST_DEBUG("End of track, advancing");
    startNextTrack();
    return TRUE;
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
    return TRUE;
}

static gboolean tag_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GstTagList *tags = NULL;
    gst_message_parse_tag(msg, &tags);
    if (!gotData && gst_tag_list_get_string(tags, GST_TAG_CDDA_MUSICBRAINZ_DISCID, &discID)) {
        setOutputMessage("Looking up disc information...");
        int releases;
        gotData = TRUE;
        GST_DEBUG("Got MusicBrainz id %s", discID);
        MbWebService svc = mb_webservice_new();
        MbQuery q = mb_query_new(svc, "rippit-" RIPPIT_VERSION_STRING);
        MbReleaseFilter filter = mb_release_filter_disc_id(mb_release_filter_new(), discID);
        MbResultList results = mb_query_get_releases(q, filter);
        releases = mb_result_list_get_size(results);
        if (releases > 0) {
            discData = mb_result_list_get_release(results, 0);
        } else if(!forceRip) {
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
            g_print("http://musicbrainz.org/bare/cdlookup.html?id=%s&tracks=%d&toc=%s\n", discID, (int)trackCount, encodedToc->str);
            g_print("If you want to rip anyways, re-run with the -f flag\n");

            g_string_free(encodedToc, TRUE);
            g_main_loop_quit(loop);
            return TRUE;
        }
        GST_DEBUG("Got %d results", releases);
        mb_result_list_free(results);
        mb_release_filter_free(filter);
        mb_query_free(q);
        mb_webservice_free(svc);
        startNextTrack();
    }
    gst_tag_list_free(tags);
    return TRUE;
}

static gboolean error_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GError *err;
    gchar *debug;
    gst_message_parse_error(msg, &err, &debug);
    g_free(debug);
    g_warning("%d %d: %s", err->domain, err->code, err->message);
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "quit");
    g_main_loop_quit(loop);
    g_error_free(err);
    return TRUE;
}

static gboolean warning_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    GError *err;
    gchar *debug;
    gst_message_parse_warning(msg, &err, &debug);
    g_free(debug);
    g_warning("%d %d: %s", err->domain, err->code, err->message);
    g_error_free(err);
    return TRUE;
}

#define PARANOIA_MODE_FULL 0xff

static GstElement *buildCDPipeline()
{
    GstElement *pipe = gst_pipeline_new(NULL);

    GstElement *cdSource = gst_element_factory_make("cdparanoiasrc", NULL);
    GstElement *encoder = gst_element_factory_make("flacenc", NULL);
    GstElement *tagger = gst_element_factory_make("flactag", NULL);
    GstElement *output = gst_element_factory_make("filesink", NULL);

    if (device) {
        g_object_set(G_OBJECT(cdSource), "device", device, NULL);
    }

    g_object_set(G_OBJECT(cdSource), "paranoia-mode", PARANOIA_MODE_FULL, NULL);
    g_signal_connect(G_OBJECT(cdSource), "uncorrected-error", G_CALLBACK(uncorrectedError_cb), NULL); 
    g_signal_connect(G_OBJECT(cdSource), "transport-error", G_CALLBACK(transportError_cb), NULL); 

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
    g_signal_connect(bus, "message::element", G_CALLBACK(element_cb), pipe);

    return pipe;
}

static GstElement *buildDVDPipeline()
{
    GstElement *pipe = gst_pipeline_new(NULL);
    GstElement *dvdSource = gst_element_factory_make("dvdreadsrc", NULL);
    dvdsrc = dvdSource;
    GstElement *dvdDemux = gst_element_factory_make("dvddemux", NULL);
    GstElement *videoDecoder = gst_element_factory_make("decodebin2", NULL);
    GstElement *dvdSpu = gst_element_factory_make("dvdspu", NULL);

    GstElement *videoQueue = gst_element_factory_make("queue", NULL);
    GstElement *audioQueue = gst_element_factory_make("queue", NULL);
    GstElement *audioOutQueue = gst_element_factory_make("queue", NULL);

    GstElement *videoEncoder = gst_element_factory_make("x264enc", NULL);
    GstElement *audioEncoder = gst_element_factory_make("ffenc_ac3", NULL);
    GstElement *audioDecoder = gst_element_factory_make("decodebin2", NULL);
    GstElement *muxer = gst_element_factory_make("matroskamux", NULL);
    GstElement *output = gst_element_factory_make("filesink", NULL);
    filesink = output;

    if (!videoEncoder || !audioEncoder || !dvdDemux) {
        g_print("Error: You're missing some vital gstreamer elements!\n");
        exit(1);
    }

    if (device) {
        g_object_set(G_OBJECT(dvdSource), "device", device, NULL);
    }

    // high10 profile
    //g_object_set(G_OBJECT(videoEncoder), "profile", 4, NULL);
    // two-pass encoding
    //g_object_set(G_OBJECT(videoEncoder), "pass", 18, NULL);
    g_object_set(G_OBJECT(videoEncoder), "quantizer", 40, NULL);

    g_object_set(G_OBJECT(output), "location", "/dev/null", NULL);
    g_object_set(G_OBJECT(muxer), "writing-app", "Rippit " RIPPIT_VERSION_STRING, NULL);

    gst_bin_add_many(GST_BIN(pipe), audioOutQueue, audioQueue, videoQueue, videoDecoder, dvdSpu, dvdSource, dvdDemux, videoEncoder, audioDecoder, audioEncoder, muxer, output, NULL);

    gst_element_link(dvdSource, dvdDemux);
    gst_element_link_pads(dvdDemux, "current_subpicture", dvdSpu, "subpicture");
    gst_element_link(dvdSpu, videoQueue);
    gst_element_link(videoQueue, videoEncoder);
    gst_element_link(videoEncoder, muxer);

    gst_element_link_pads(dvdDemux, "current_video", videoDecoder, "sink");
    g_signal_connect(G_OBJECT(videoDecoder), "no-more-pads", G_CALLBACK(linkDecodebin), dvdSpu);

    gst_element_link_pads(dvdDemux, "current_audio", audioQueue, "sink");
    gst_element_link(audioQueue, audioDecoder);
    g_signal_connect(G_OBJECT(audioDecoder), "no-more-pads", G_CALLBACK(linkDecodebin), audioEncoder);
    gst_element_link(audioEncoder, audioOutQueue);
    gst_element_link(audioOutQueue, muxer);


    gst_element_link(muxer, output);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipe));
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message::error", G_CALLBACK(error_cb), pipe);
    g_signal_connect(bus, "message::warning", G_CALLBACK(warning_cb), pipe);
    g_signal_connect(bus, "message::state-changed", G_CALLBACK(state_cb), pipe);
    g_signal_connect(bus, "message::tag", G_CALLBACK(tag_cb), pipe);
    g_signal_connect(bus, "message::eos", G_CALLBACK(eos_cb), pipe);
    g_signal_connect(bus, "message::element", G_CALLBACK(element_cb), pipe);

    return pipe;
}

static gboolean probeElement(const gchar *name)
{
    gboolean ret = TRUE;
    GstElement *probe = gst_element_factory_make(name, NULL);
    if (!probe)
        return FALSE;
    g_object_set(G_OBJECT(probe), "device", device, NULL);
    if (gst_element_set_state(probe, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE)
        ret = FALSE;
    gst_element_set_state(probe, GST_STATE_NULL);
    gst_object_unref(probe);
    return ret;
}

static GstElement *buildPipeline()
{
    GstElement *pipeline;
    if (probeElement("cdparanoiasrc")) {
        setOutputMessage("Reading CD...");
        pipeline = buildCDPipeline();
    } else if (probeElement("dvdreadsrc")) {
        setOutputMessage("Reading DVD...");
        pipeline = buildDVDPipeline();
    } else {
        setOutputMessage("No disks found :'(");
        return NULL;
    }
    return pipeline;
}

int main(int argc, char* argv[])
{
    GError *error = NULL;
    GOptionContext *context = NULL;

    g_thread_init(NULL);
    GST_DEBUG_CATEGORY_INIT(rippit, "rippit", 0, "Rippit Debugging");

    context = g_option_context_new("[device-or-file] - Rip an audio CD, without any nonsense.");
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gst_init_get_option_group());

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("Bad arguments: %s\n", error->message);
        exit(1);
    }

    if (extraArgs && g_strv_length(extraArgs) > 0) {
        struct stat buf;
        device = extraArgs[0];
        if (stat(device, &buf) != 0) {
            g_print("Could not find '%s'\n", device);
            exit(1);
        }
        g_print("Will attempt to read from '%s'\n", device);
    }

    if (!gst_init_check(&argc, &argv, &error)) {
        g_print("Could not initialize GStreamer: %s\n", error->message);
        exit(1);
    }

    if (printVersion) {
        g_print("rippit version %s\n", RIPPIT_VERSION_STRING);
        exit(0);
    }

    if (showSomeLove) {
        rippit_show_some_love();
        exit(0);
    }

    if (singleTrack == 0) {
        g_print("Tracks start at 1. Sorry for any confusion.\n");
        exit(0);
    }

    if (singleTrack > 0)
        curTrack = singleTrack-1;

    setOutputMessage("Probing devices...");

    pipeline = buildPipeline();
    if (pipeline == NULL) {
        g_print("\n");
        return 1;
    }


    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    GstFormat format;
    if (dvdsrc)
        format = gst_format_get_by_nick("title");
    else
        format = gst_format_get_by_nick("track");
    gst_element_query_duration(GST_ELEMENT(pipeline), &format, &trackCount);
    g_timeout_add_full(G_PRIORITY_LOW, 200, cb_progress, NULL, NULL);
    g_debug("Found %d tracks", trackCount);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "init");

    if (dvdsrc)
        startNextTrack();

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
