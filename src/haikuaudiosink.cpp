/* Haiku audio sink plugin for GStreamer
 * Copyright (C) <2017> Gerasim Troeglazov <3dEyes@gmail.com>
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

#include "haikuaudiosink.h"
#include <string.h>
#include <unistd.h>

#define GST_PACKAGE_NAME "Gstreamer"
#define GST_PACKAGE_ORIGIN "GStreamer community"
#define VERSION "0.10.36"
#define PACKAGE "gstreamer"

#define DEFAULT_MUTE        FALSE
#define DEFAULT_VOLUME      0.6
#define MAX_VOLUME          1.0

#define HAIKU_SND_BUFFER_SIZE 1920
#define HAIKU_SND_RATE "48000"
#define HAIKU_SND_CHANNELS "2"
#define HAIKU_SND_WIDTH "16"

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
    "haiku",
    "Haiku MediaKit plugin for GStreamer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
}

static void gst_haikuaudio_sink_dispose (GObject * object);
static GstCaps *gst_haikuaudio_sink_getcaps (GstBaseSink * bsink);
static gboolean gst_haikuaudio_sink_open (GstAudioSink * asink);
static gboolean gst_haikuaudio_sink_close (GstAudioSink * asink);
static gboolean gst_haikuaudio_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec);
static gboolean gst_haikuaudio_sink_unprepare (GstAudioSink * asink);
static guint gst_haikuaudio_sink_write (GstAudioSink * asink, gpointer data, guint length);
static void gst_haikuaudio_sink_set_volume (GstHaikuAudioSink * sink);
static void gst_haikuaudio_sink_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_haikuaudio_sink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

enum
{
	LAST_SIGNAL
};

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
	GST_STATIC_CAPS ("audio/x-raw-int, "
		"endianness = (int) BYTE_ORDER, "
		"signed = (boolean) TRUE, "
		"width = (int) " HAIKU_SND_WIDTH ", "
		"depth = (int) " HAIKU_SND_WIDTH ", "
		"rate = (int) " HAIKU_SND_RATE ", "
		"channels = (int) " HAIKU_SND_CHANNELS)
	);

GST_BOILERPLATE (GstHaikuAudioSink, gst_haikuaudio_sink, GstAudioSink, GST_TYPE_AUDIO_SINK);

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
	
	gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_dispose);
	
	gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_getcaps);
	
	gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_open);
	gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_close);
	gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_prepare);
	gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_unprepare);
	gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_haikuaudio_sink_write);

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
	haikuaudiosink->buffer = NULL;
	haikuaudiosink->nodeName = new BString("GStreamer");
	if (be_app != NULL)  {
		app_info appinfo;
		if (be_app->GetAppInfo(&appinfo) == B_OK) {
  			BPath apppath(&appinfo.ref);
  			if (apppath.InitCheck() == B_OK)
  				haikuaudiosink->nodeName->SetTo(apppath.Leaf());
		}
	}
	haikuaudiosink->volume = DEFAULT_VOLUME;
	haikuaudiosink->mute = DEFAULT_MUTE;
}

static GstCaps *
gst_haikuaudio_sink_getcaps (GstBaseSink * bsink)
{
	return gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD(bsink)));
}

static void
gst_haikuaudio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
	GstHaikuAudioSink *sink = GST_HAIKUAUDIOSINK (object);

	switch (prop_id) {
		case ARG_VOLUME:
			sink->volume = g_value_get_double (value);
			gst_haikuaudio_sink_set_volume (sink);
			break;
		case ARG_MUTE:
			sink->mute = g_value_get_boolean (value);
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
			g_value_set_double (value, sink->volume);
			break;
		case ARG_MUTE:
			g_value_set_boolean (value, sink->mute);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_haikuaudio_sink_set_volume (GstHaikuAudioSink * sink)
{
	if (sink->m_player == NULL)
		return;
	if (sink->m_player->InitCheck() != B_OK)
		return;
	sink->m_player->SetVolume((float)sink->volume);
}

static void
playerProc(void *cookie, void *buffer, size_t len, const media_raw_audio_format &format)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK ((GstAudioSink*)cookie);
	bigtime_t timeout = ((1000000 / (format.channel_count * (atoi(HAIKU_SND_WIDTH) / 8))) * len) / format.frame_rate;
	if (acquire_sem_etc(haikuaudio->block_sem, 1, B_RELATIVE_TIMEOUT, timeout) == B_TIMED_OUT) {
		memset(buffer, 0, len);
		return;
	}
	memcpy(buffer, haikuaudio->buffer, len);
	release_sem(haikuaudio->unblock_sem);
}

static gboolean
gst_haikuaudio_sink_open (GstAudioSink * asink)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	uint32 segsize = HAIKU_SND_BUFFER_SIZE * atoi(HAIKU_SND_CHANNELS);

	haikuaudio->mediaKitFormat = {
		(float)atof(HAIKU_SND_RATE),
		(uint32)atoi(HAIKU_SND_CHANNELS),
		media_raw_audio_format::B_AUDIO_SHORT,
		B_MEDIA_LITTLE_ENDIAN,
		(uint32)segsize
  	};

	haikuaudio->buffer = (unsigned char*)g_malloc (segsize);
	memset (haikuaudio->buffer, 0, segsize);

	haikuaudio->m_player = new BSoundPlayer(&haikuaudio->mediaKitFormat,
	haikuaudio->nodeName->String(), playerProc, NULL, (void*)haikuaudio);

	if(haikuaudio->m_player->InitCheck() != B_OK) {
		delete haikuaudio->m_player;
		haikuaudio->m_player = NULL;
		free(haikuaudio->buffer);
		haikuaudio->buffer = NULL;
		return FALSE;
	}

	haikuaudio->block_sem = create_sem(0, "blocker");
	haikuaudio->unblock_sem = create_sem(1, "unblocker");

	haikuaudio->m_player->Start();
  	haikuaudio->m_player->SetHasData(true);

	return TRUE;
}

static gboolean
gst_haikuaudio_sink_close (GstAudioSink * asink)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	if(haikuaudio->m_player != NULL) {
		haikuaudio->m_player->SetHasData(false);
		haikuaudio->m_player->Stop();

		delete_sem(haikuaudio->block_sem);
		delete_sem(haikuaudio->unblock_sem);

		delete haikuaudio->m_player;

		haikuaudio->m_player = NULL;
	}

	if(haikuaudio->buffer != NULL) {
		free(haikuaudio->buffer);
		haikuaudio->buffer = NULL;
	}

	return TRUE;
}

static guint
gst_haikuaudio_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	if (length > haikuaudio->mediaKitFormat.buffer_size) {
		length = haikuaudio->mediaKitFormat.buffer_size;
	}
	bigtime_t timeout = ((1000000 / ((atoi(HAIKU_SND_WIDTH) / 8 ) * atoi(HAIKU_SND_CHANNELS))) * length) / atoi(HAIKU_SND_RATE);

	if (acquire_sem_etc(haikuaudio->unblock_sem, 1, B_RELATIVE_TIMEOUT, timeout) == B_TIMED_OUT) {
		return 0;
	}
	memcpy(haikuaudio->buffer, data, length);
	release_sem(haikuaudio->block_sem);

	return length;
}

static gboolean
gst_haikuaudio_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	if (spec->format != GST_S16_LE)
		return FALSE;
	if (spec->width != atoi(HAIKU_SND_WIDTH))
		return FALSE;

	spec->segsize = HAIKU_SND_BUFFER_SIZE * spec->channels;

	return TRUE;
}

static gboolean
gst_haikuaudio_sink_unprepare (GstAudioSink * asink)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	return TRUE;
}
