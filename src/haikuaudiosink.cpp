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

enum
{
	LAST_SIGNAL
};

static GstStaticPadTemplate haikuaudiosink_sink_factory =
	GST_STATIC_PAD_TEMPLATE ("sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS ("audio/x-raw-int, "
		"endianness = (int) BYTE_ORDER, "
		"signed = (boolean) TRUE, "
		"width = (int) 16, "
		"depth = (int) 16, "
		"rate = (int) 44100, "
		"channels = (int) [ 1, 2 ]")
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
}

static GstCaps *
gst_haikuaudio_sink_getcaps (GstBaseSink * bsink)
{
	return gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD(bsink)));
}

static gboolean
gst_haikuaudio_sink_open (GstAudioSink * asink)
{
	return TRUE;
}

static gboolean
gst_haikuaudio_sink_close (GstAudioSink * asink)
{
	return TRUE;
}

static guint
gst_haikuaudio_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	if (length > haikuaudio->mediaKitFormat.buffer_size) {
		length = haikuaudio->mediaKitFormat.buffer_size;
	}

	acquire_sem(haikuaudio->unblock_sem);
	memcpy(haikuaudio->buffer, data, length);
	release_sem(haikuaudio->block_sem);  

	return length;
}

static void playerProc(void *cookie, void *buffer, size_t len, const media_raw_audio_format &format)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK ((GstAudioSink*)cookie);
	
	acquire_sem(haikuaudio->block_sem);
	memcpy(buffer, haikuaudio->buffer, len);
	release_sem(haikuaudio->unblock_sem);
}

static gboolean
gst_haikuaudio_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);

	if (spec->format != GST_S16_LE)
		return FALSE;
	if (spec->width != 16)
		return FALSE;
  
	spec->segsize = 2048 * spec->channels;

	haikuaudio->mediaKitFormat = {
		(float)spec->rate,
		(uint32)spec->channels,
		media_raw_audio_format::B_AUDIO_SHORT,
		B_MEDIA_LITTLE_ENDIAN,
		(uint32)spec->segsize
  	};

	haikuaudio->buffer = (unsigned char*)g_malloc (spec->segsize);
	memset (haikuaudio->buffer, 0, spec->segsize);

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
gst_haikuaudio_sink_unprepare (GstAudioSink * asink)
{
	GstHaikuAudioSink *haikuaudio = GST_HAIKUAUDIOSINK (asink);  	

	if(haikuaudio->m_player != NULL) {
		haikuaudio->m_player->SetHasData(false);
		delete_sem(haikuaudio->block_sem);
		delete_sem(haikuaudio->unblock_sem);
		haikuaudio->m_player->Stop();
		delete haikuaudio->m_player;
		haikuaudio->m_player = NULL;
	}

	if(haikuaudio->buffer != NULL) {
		free(haikuaudio->buffer);
		haikuaudio->buffer = NULL;
	}

	return TRUE;
}
