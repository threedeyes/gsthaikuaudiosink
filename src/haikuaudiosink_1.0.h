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

#ifndef __GST_HAIKUAUDIOSINK_H__
#define __GST_HAIKUAUDIOSINK_H__


#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>
//#include <gst/interfaces/mixer.h>

#include <Application.h>
#include <Roster.h>
#include <Path.h>
#include <SoundPlayer.h>
#include <SupportKit.h>
#include <MediaDefs.h>
#include <String.h>
#include <OS.h>

G_BEGIN_DECLS

#define GST_TYPE_HAIKUAUDIOSINK            (gst_haikuaudio_sink_get_type())
#define GST_HAIKUAUDIOSINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HAIKUAUDIOSINK,GstHaikuAudioSink))
#define GST_HAIKUAUDIOSINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HAIKUAUDIOSINK,GstHaikuAudioSinkClass))
#define GST_IS_HAIKUAUDIOSINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HAIKUAUDIOSINK))
#define GST_IS_HAIKUAUDIOSINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HAIKUAUDIOSINK))

typedef struct _GstHaikuAudioSink GstHaikuAudioSink;
typedef struct _GstHaikuAudioSinkClass GstHaikuAudioSinkClass;

struct _GstHaikuAudioSink {
	GstAudioSink sink;

	guint8 *buffer;

	media_raw_audio_format mediaKitFormat;
	guint32 bytesPerFrame;
	bigtime_t latency_time;

	sem_id block_sem;
	sem_id unblock_sem;

	thread_id monitorThread;

	BSoundPlayer *soundPlayer;
	BString *nodeName;
	GstCaps caps;

	bigtime_t lastWriteTime;

	double volume;
	gboolean mute;

	gboolean is_webapp;
};

struct _GstHaikuAudioSinkClass {
	GstAudioSinkClass parent_class;
};

GType gst_haikuaudio_sink_get_type(void);

G_END_DECLS

#endif /* __GST_HAIKUAUDIOSINK_H__ */
