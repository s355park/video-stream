#include <gst/gst.h>
#include <stdlib.h>
#include <stdio.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _SrcData {
  GstElement *source_pipeline;
  GstElement *source;
  GstElement *vid_scale;
  GstElement *vid_scale_caps_filter;
  GstElement *vid_convert;
  GstElement *x264_enc;
  GstElement *rtph_264_pay;
  GstElement *udp_sink;
} SrcData;

/* Handler for the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *pad, SrcData *srcData);

int main(int argc, char *argv[]) {
  SrcData srcData;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;
  //by default, resolution of 1920 x 1080
  int width = 1920 , height = 1080;

  if(argc>1){
    height = atoi(argv[1]);
    switch (height){
      case 2160:
        width = 3840;
        break;
      case 1080:
        width = 1920;
        break;
      case 720:
        width = 1280;
        break;
      default:
        printf("illegal resolution");
        return 0;
    }
  }

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  srcData.source = gst_element_factory_make ("uridecodebin", "source");
  srcData.vid_scale = gst_element_factory_make ("videoscale", "vid_scale");
  srcData.vid_scale_caps_filter = gst_element_factory_make ("capsfilter", "vid_scale_caps_filter");
  srcData.vid_convert = gst_element_factory_make ("videoconvert", "vid_convert");
  srcData.x264_enc = gst_element_factory_make ("x264enc", "x264_enc");
  srcData.rtph_264_pay = gst_element_factory_make ("rtph264pay", "rtph_264_pay");
  srcData.udp_sink = gst_element_factory_make ("udpsink", "udp_sink");

  /* set the host and port of sink*/
  g_object_set(srcData.udp_sink, "host", "127.0.0.1", "port", 5000, NULL);

  GstCaps *vid_scale_caps = gst_caps_new_simple(
    "video/x-raw",
    "width", G_TYPE_INT, width,
    "height", G_TYPE_INT, height,
    NULL
    );

  /* convert the video resolution*/
  g_object_set(srcData.vid_scale_caps_filter, "caps", vid_scale_caps, NULL);
  gst_caps_unref(vid_scale_caps);

  /* specifying the bitrate for encoder*/
  g_object_set(srcData.x264_enc, "bitrate", 3000, NULL);

  /* Create the empty source_pipeline */
  srcData.source_pipeline = gst_pipeline_new ("source_pipeline");

  if (!srcData.source_pipeline || !srcData.source || !srcData.vid_scale || !srcData.vid_scale_caps_filter || !srcData.vid_convert || !srcData.x264_enc || !srcData.udp_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the source_pipeline. Note that we are NOT linking the source at this
   * point. We will do it later. */
  gst_bin_add_many (GST_BIN (srcData.source_pipeline), srcData.source, srcData.vid_scale, srcData.vid_scale_caps_filter, srcData.vid_convert, srcData.x264_enc, srcData.rtph_264_pay, srcData.udp_sink, NULL);
  if (!gst_element_link_many (srcData.vid_scale, srcData.vid_scale_caps_filter, srcData.vid_convert, srcData.x264_enc,srcData.rtph_264_pay, srcData.udp_sink, NULL)) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (srcData.source_pipeline);
    return -1;
  }

  /* Set the URI to play */
  g_object_set (srcData.source, "uri", "https://media.githubusercontent.com/media/RMUS-Canada/video-repo/main/assesment-video.webm", NULL);

  /* Connect to the pad-added signal */
  g_signal_connect (srcData.source, "pad-added", G_CALLBACK (pad_added_handler), &srcData);

  /* Start playing */
  ret = gst_element_set_state (srcData.source_pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the source_pipeline to the playing state.\n");
    gst_object_unref (srcData.source_pipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (srcData.source_pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the source_pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (srcData.source_pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          }
          break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (srcData.source_pipeline, GST_STATE_NULL);
  gst_object_unref (srcData.source_pipeline);
  return 0;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *new_pad, SrcData *srcData) {
  GstPad *sink_pad = gst_element_get_static_pad (srcData->vid_scale, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "video/x-raw")) {
    g_print ("It has type '%s' which is not raw video. Ignoring.\n", new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}
