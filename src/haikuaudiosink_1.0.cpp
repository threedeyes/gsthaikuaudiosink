/* Haiku audio sink plugin for GStreamer
 * Copyright (C) <2017-2023> Gerasim Troeglazov <3dEyes@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "haikuaudiosink_1.0.h"
#include <string.h>
#include <unistd.h>

#define GST_PACKAGE_NAME "Gstreamer"
#define GST_PACKAGE_ORIGIN "GStreamer community"
#define VERSION "1.20.4"
#define PACKAGE "gstreamer"

#define DEFAULT_MUTE        FALSE
#define DEFAULT_VOLUME      0.6
#define MAX_VOLUME          1.0

static gboolean
plugin_init (GstPlugin * plugin)
{
	if (!gst_element_register (plugin, "haikuaudiosink", GST_RANK_PRIMARY, GST_TYPE_HAIKUAUDIOSINK))
		return FALSE;

	return TRUE;
}

extern "C" {
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    haikuaudiosink,
    "Haiku MediaKit plugin for GStreamer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
}

static void gst_haikuaudio_sink_dispose (GObject * object);
static GstCaps *gst_haikuaudio_sink_getcaps (GstBaseSink * bsink, GstCaps * filter);
static gboolean gst_haikuaudio_sink_open (GstAudioSink * asink);
static gboolean gst_haikuaudio_sink_close (GstAudioSink * asink);
static gboolean gst_haikuaudio_sink_prepare (GstAudioSink * asink, GstAudioRingBufferSpec * spec);
static gboolean gst_haikuaudio_sink_unprepare (GstAudioSink * asink);
static void gst_haikuaudio_sink_base_init (gpointer g_class);
static void gst_haikuaudio_sink_class_init (GstHaikuAudioSinkClass * klass);
static void gst_haikuaudio_sink_init (GstHaikuAudioSink * haikuaudiosink, GstHaikuAudioSinkClass * g_class);
static gint gst_haikuaudio_sink_write (GstAudioSink * asink, gpointer data, guint length);
static void gst_haikuaudio_sink_finalize (GObject * object);

static void gst_haikuaudio_sink_set_volume (GstHaikuAudioSink * sink, gdouble volume, gboolean store);
static gdouble gst_haikuaudio_sink_get_volume (GstHaikuAudioSink * sink);
static void gst_haikuaudio_sink_set_mute (GstHaikuAudioSink * sink, gboolean mute);
static gboolean gst_haikuaudio_sink_get_mute (GstHaikuAudioSink * sink);

static void gst_haikuaudio_sink_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_haikuaudio_sink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_haikuaudio_sink_soundplayer_delete (GstHaikuAudioSink * sink);

enum
{
  ARG_0,
  ARG_VOLUME,
  ARG_MUTE
};

static GstStaticPadTemplate haikuaudiosink_sink_factory =
	GST_STATIC_PAD_TEMPLATE ("sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) { S16LE, S32LE, F32LE, S8, U8 }, "
        "channels = (int) [1, 2], "
        "rate = (int) [1, MAX ], "
        "layout = (string) interleaved")
	);

static GstElementClass *parent_class = NULL;

GType
gst_haikuaudio_sink_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstHaikuAudioSinkClass),
      gst_haikuaudio_sink_base_init,
      NULL,
      (GClassInitFunc) gst_haikuaudio_sink_class_init,
      NULL,
      NULL,
      sizeof (GstHaikuAudioSink),
      0,
      (GInstanceInitFunc) gst_haikuaudio_sink_init,
    };

    plugin_type = g_type_register_static (GST_TYPE_AUDIO_SINK,
        "GstHaikuAudioSink", &plugin_info, (GTypeFlags)0);
  }
  return plugin_type;
}

static void
gst_haikuaudio_sink_dispose (GObject * object)
{
	GstHaikuAudioSink *haikuaudiosink = GST_HAIKUAUDIOSINK (object);
	if (haikuaudiosink->nodeName != NULL)
		delete haikuaudiosink->nodeName;
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_haikuaudio_sink_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

	gst_element_class_set_details_simple (element_class, "Haiku audio sink",
		"Sink/Audio",
		"Output to a sound card via Haiku MediaKit",
		"Gerasim Troeglazov <3dEyes@gmail.com>");

	gst_element_class_add_static_pad_template (element_class, &haikuaudiosink_sink_factory);
}

static void
gst_haikuaudio_sink_class_init (GstHaikuAudioSinkClass * klass)
{
	GObjectClass *gobject_class  = (GObjectClass *) klass;
	GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;
	GstAudioSinkClass *gstaudiosink_class = (GstAudioSinkClass *) klass;
	
	parent_class = (GstElementClass*)g_type_class_peek_parent (klass);
	
	gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_dispose);
	
	gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_getcaps);
	
	gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_open);
	gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_close);
	gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_prepare);
	gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_unprepare);
	gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_write);

	gobject_class->finalize = gst_haikuaudio_sink_finalize;
	gobject_class->set_property = gst_haikuaudio_sink_set_property;
	gobject_class->get_property = gst_haikuaudio_sink_get_property;

	g_object_class_install_property (gobject_class,
		ARG_VOLUME,
		g_param_spec_double ("volume", "Volume",
			"Linear volume of this stream, 1.0=100%", 0.0, MAX_VOLUME,
			DEFAULT_VOLUME, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

	g_object_class_install_property (gobject_class,
		ARG_MUTE,
		g_param_spec_boolean ("mute", "Mute",
			"Mute state of this stream", DEFAULT_MUTE,
			(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_haikuaudio_sink_init (GstHaikuAudioSink * haikuaudiosink,
    GstHaikuAudioSinkClass * g_class)
{
	haikuaudiosink->is_webapp = FALSE;
	haikuaudiosink->buffer = NULL;
	haikuaudiosink->nodeName = new BString("GStreamer");
	if (be_app != NULL)  {
		app_info appinfo;
		app_info parentinfo;
		if (be_app->GetAppInfo(&appinfo) == B_OK) {
			BPath apppath(&appinfo.ref);
			if (apppath.InitCheck() == B_OK) {
				haikuaudiosink->nodeName->SetTo(apppath.Leaf());
				if (strncmp(appinfo.signature, "application/x-vnd.gtk-webkit-webprocess", B_MIME_TYPE_LENGTH) == 0 ||
					strncmp(appinfo.signature, "application/x-vnd.otter-browser", B_MIME_TYPE_LENGTH) == 0 ||
					strncmp(appinfo.signature, "application/x-vnd.qutebrowser", B_MIME_TYPE_LENGTH) == 0 ||
					strncmp(appinfo.signature, "application/x-vnd.dooble", B_MIME_TYPE_LENGTH) == 0) {
						haikuaudiosink->is_webapp = TRUE;
				}
			}
		}
		if (be_roster->GetRunningAppInfo(getppid(), &parentinfo) == B_OK) {
			BPath apppath(&parentinfo.ref);
			if (apppath.InitCheck() == B_OK) {
				if (strncmp(appinfo.signature, "application/x-vnd.gtk-webkit-webprocess", B_MIME_TYPE_LENGTH) == 0) {
					haikuaudiosink->nodeName->SetTo(apppath.Leaf());
					haikuaudiosink->is_webapp = TRUE;
				}
			}
		}
	}
	haikuaudiosink->volume = DEFAULT_VOLUME;
	haikuaudiosink->mute = DEFAULT_MUTE;
}

static void
gst_haikuaudio_sink_finalize (GObject * object)
{
	GstHaikuAudioSink *sink = GST_HAIKUAUDIOSINK (object);
	gst_haikuaudio_sink_soundplayer_delete(sink);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_haikuaudio_sink_getcaps (GstBaseSink * bsink, GstCaps * filter)
{
  GstHaikuAudioSink *sink = GST_HAIKUAUDIOSINK (bsink);
  GstCaps *caps = gst_pad_get_pad_template_caps (bsink->sinkpad);;

  if (filter) {
    GstCaps *filtered =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = filtered;
  }

  return caps;
}

static void
gst_haikuaudio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
	GstHaikuAudioSink *sink = GST_HAIKUAUDIOSINK (object);

	switch (prop_id) {
		case ARG_VOLUME:
			gst_haikuaudio_sink_set_volume (sink, g_value_get_double (value), TRUE);
			break;
		case ARG_MUTE:
			gst_haikuaudio_sink_set_mute (sink, g_value_get_boolean (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_haikuaudio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
	GstHaikuAudioSink *sink = GST_HAIKUAUDIOSINK (object);

	switch (prop_id) {
		case ARG_VOLUME:
			g_value_set_double (value, gst_haikuaudio_sink_get_volume (sink));
			break;
		case ARG_MUTE:
			g_value_set_boolean (value, gst_haikuaudio_sink_get_mute (sink));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_haikuaudio_sink_set_volume (GstHaikuAudioSink * sink, gdouble dvolume, gboolean store)
{
	if (store)
		sink->volume = dvolume;

	if (sink->soundPlayer == NULL)
		return;
	if (sink->soundPlayer->InitCheck() != B_OK)
		return;

	sink->soundPlayer->SetVolume((float)dvolume);
}

gdouble
gst_haikuaudio_sink_get_volume (GstHaikuAudioSink * sink)
{
	if (sink->soundPlayer != NULL)
		sink->volume = (gdouble)sink->soundPlayer->Volume();

	return (gdouble)sink->volume;
}

static void
gst_haikuaudio_sink_set_mute (GstHaikuAudioSink * sink, gboolean mute)
{
  if (mute) {
    gst_haikuaudio_sink_set_volume (sink, 0, FALSE);
    sink->mute = TRUE;
  } else {
    gst_haikuaudio_sink_set_volume (sink, gst_haikuaudio_sink_get_volume (sink), FALSE);
    sink->mute = FALSE;
  }
}

static gboolean
gst_haikuaudio_sink_get_mute (GstHaikuAudioSink * sink)
{
  return sink->mute;
}

static void
gst_haikuaudio_sink_soundplayer_callback(void *cookie, void *buffer, size_t length, const media_raw_audio_format &format)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK ((GstAudioSink*)cookie);

	if (acquire_sem_etc(haikuaudio->block_sem, 1, B_RELATIVE_TIMEOUT, haikuaudio->latency_time) == B_TIMED_OUT) {
		memset(buffer, 0, length);
		return;
	}

	memcpy(buffer, haikuaudio->buffer, length);
	release_sem(haikuaudio->unblock_sem);
}

static void
gst_haikuaudio_sink_soundplayer_create (GstHaikuAudioSink * sink)
{
	if(sink->soundPlayer == NULL) {
		sink->soundPlayer = new BSoundPlayer(&sink->mediaKitFormat,
			sink->nodeName->String(), gst_haikuaudio_sink_soundplayer_callback, NULL, (void*)sink);

		if(sink->soundPlayer->InitCheck() != B_OK) {
			delete sink->soundPlayer;
			sink->soundPlayer = NULL;
			return;
		}

		sink->block_sem = create_sem(0, "blocker");
		sink->unblock_sem = create_sem(1, "unblocker");

		sink->soundPlayer->Start();
		sink->soundPlayer->SetHasData(true);

		gst_haikuaudio_sink_set_volume (sink, gst_haikuaudio_sink_get_volume (sink), FALSE);
		gst_haikuaudio_sink_set_mute (sink, sink->mute);
	}
}

static void
gst_haikuaudio_sink_soundplayer_delete (GstHaikuAudioSink * sink)
{
	if (sink->soundPlayer != NULL) {
		gst_haikuaudio_sink_get_volume (sink);

		sink->soundPlayer->SetHasData(false);
		sink->soundPlayer->Stop();

		delete_sem(sink->block_sem);
		delete_sem(sink->unblock_sem);

		delete sink->soundPlayer;

		sink->soundPlayer = NULL;
	}
}

static int32
gst_haikuaudio_sink_monitor_thread (void *data)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK ((GstAudioSink*)data);
	while(true) {
		if (system_time() - haikuaudio->lastWriteTime > G_USEC_PER_SEC && haikuaudio->soundPlayer != NULL)
			gst_haikuaudio_sink_soundplayer_delete(haikuaudio);
		snooze(G_USEC_PER_SEC / 100);
	}
}

static gboolean
gst_haikuaudio_sink_open (GstAudioSink * asink)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	return TRUE;
}

static gboolean
gst_haikuaudio_sink_close (GstAudioSink * asink)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	return TRUE;
}

static gint
gst_haikuaudio_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	if (haikuaudio->is_webapp && haikuaudio->soundPlayer == NULL)
		gst_haikuaudio_sink_soundplayer_create(haikuaudio);

	if (length > haikuaudio->mediaKitFormat.buffer_size) {
		length = haikuaudio->mediaKitFormat.buffer_size;
	}

	if (acquire_sem_etc(haikuaudio->unblock_sem, 1, B_RELATIVE_TIMEOUT, haikuaudio->latency_time) == B_TIMED_OUT)
		return 0;

	memcpy(haikuaudio->buffer, data, length);
	haikuaudio->lastWriteTime = system_time();
	release_sem(haikuaudio->block_sem);

	return length;
}

static uint32
mediakit_format_from_gst (GstAudioFormat format)
{
	switch (format) {
		case GST_AUDIO_FORMAT_S8:
			return media_raw_audio_format::B_AUDIO_CHAR;

		case GST_AUDIO_FORMAT_U8:
			return media_raw_audio_format::B_AUDIO_UCHAR;

		case GST_AUDIO_FORMAT_S16LE:
			return media_raw_audio_format::B_AUDIO_SHORT;

		case GST_AUDIO_FORMAT_S32LE:
			return media_raw_audio_format::B_AUDIO_INT;

		case GST_AUDIO_FORMAT_F32LE:
			return media_raw_audio_format::B_AUDIO_FLOAT;

		default:
			g_assert_not_reached ();
	}
}

static gboolean
gst_haikuaudio_sink_prepare (GstAudioSink * asink, GstAudioRingBufferSpec * spec)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	haikuaudio->latency_time = spec->latency_time;
	haikuaudio->bytesPerFrame = GST_AUDIO_INFO_BPF (&spec->info);
	spec->segsize = (spec->latency_time * GST_AUDIO_INFO_RATE (&spec->info) / G_USEC_PER_SEC) *
		GST_AUDIO_INFO_BPF (&spec->info);

	haikuaudio->mediaKitFormat = {
		(float)GST_AUDIO_INFO_RATE (&spec->info),
		(uint32)GST_AUDIO_INFO_CHANNELS (&spec->info),
		mediakit_format_from_gst(GST_AUDIO_INFO_FORMAT (&spec->info)),
		B_MEDIA_LITTLE_ENDIAN,
		(uint32)spec->segsize
  	};

	haikuaudio->buffer = (unsigned char*)g_malloc (spec->segsize);
	memset (haikuaudio->buffer, 0, spec->segsize);

	if (haikuaudio->is_webapp) {
		haikuaudio->monitorThread = spawn_thread(gst_haikuaudio_sink_monitor_thread,
			"monitor_thread", B_NORMAL_PRIORITY, (void*)haikuaudio);
		resume_thread(haikuaudio->monitorThread);
	} else {
		gst_haikuaudio_sink_soundplayer_create(haikuaudio);
	}

	return TRUE;
}

static gboolean
gst_haikuaudio_sink_unprepare (GstAudioSink * asink)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	if (haikuaudio->is_webapp)
		kill_thread(haikuaudio->monitorThread);

	gst_haikuaudio_sink_soundplayer_delete(haikuaudio);

	if(haikuaudio->buffer != NULL) {
		free(haikuaudio->buffer);
		haikuaudio->buffer = NULL;
	}

	return TRUE;
}
