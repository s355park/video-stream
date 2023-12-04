#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _RecvData {
  GstElement *receive_pipeline;
  GstElement *udp_src;
  GstElement *x264_dec;
  GstElement *rtph_264_dep;
  GstElement *vid_sink;
} RecvData;

/* Handler for the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *pad, RecvData *recvData);

int main(int argc, char *argv[]) {
    RecvData recvData;
    GstBus *bus;
    GstMessage *msg;
    GstStateChangeReturn ret;
    gboolean terminate = FALSE;

    /* Initialize GStreamer */
    gst_init (&argc, &argv);

    /* Initialize caps*/
    GstCaps *caps = gst_caps_new_simple(
    "application/x-rtp",
    "media", G_TYPE_STRING, "video",
    "payload", G_TYPE_INT, 96,
    NULL
    );

    /* Create the elements */
    recvData.udp_src = gst_element_factory_make ("udpsrc", "udp_src");
    recvData.x264_dec = gst_element_factory_make ("avdec_h264", "x264_dec");
    recvData.rtph_264_dep = gst_element_factory_make ("rtph264depay", "rtph_264_dep");
    recvData.vid_sink = gst_element_factory_make ("autovideosink", "vid_sink");

    g_object_set(recvData.udp_src, "address", "127.0.0.1", "port", 5000, "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Create the empty receive_pipeline */
    recvData.receive_pipeline = gst_pipeline_new ("receive_pipeline");

  if (!recvData.receive_pipeline || !recvData.udp_src || !recvData.x264_dec || !recvData.rtph_264_dep || !recvData.vid_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }
    /* Create pipeline and link the elements*/
  gst_bin_add_many (GST_BIN (recvData.receive_pipeline), recvData.udp_src, recvData.rtph_264_dep, recvData.x264_dec, recvData.vid_sink, NULL);
  if (!gst_element_link_many (recvData.udp_src, recvData.rtph_264_dep, recvData.x264_dec, recvData.vid_sink, NULL)) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (recvData.receive_pipeline);
    return -1;
  }

  /* Start playing */
  ret = gst_element_set_state (recvData.receive_pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the receive_pipeline to the playing state.\n");
    gst_object_unref (recvData.receive_pipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (recvData.receive_pipeline);
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
          /* We are only interested in state-changed messages from the receive_pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (recvData.receive_pipeline)) {
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
  gst_element_set_state (recvData.receive_pipeline, GST_STATE_NULL);
  gst_object_unref (recvData.receive_pipeline);
  return 0;
}
