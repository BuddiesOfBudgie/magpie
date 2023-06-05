/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016-2020 Red Hat
 * Copyright (c) 2018,2019 DisplayLink (UK) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "config.h"

#include "backends/native/meta-onscreen-native.h"

#include <drm_fourcc.h>

#include "backends/meta-egl-ext.h"
#include "backends/native/meta-cogl-utils.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer-dumb.h"
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-drm-buffer-import.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-output-kms.h"
#include "backends/native/meta-render-device-gbm.h"
#include "backends/native/meta-render-device.h"
#include "backends/native/meta-renderer-native-gles3.h"
#include "backends/native/meta-renderer-native-private.h"

typedef enum _MetaSharedFramebufferImportStatus
{
  /* Not tried importing yet. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE,
  /* Tried before and failed. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_FAILED,
  /* Tried before and succeeded. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK
} MetaSharedFramebufferImportStatus;

typedef struct _MetaOnscreenNativeSecondaryGpuState
{
  MetaGpuKms *gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;

  EGLSurface egl_surface;

  struct {
    struct gbm_surface *surface;
  } gbm;

  struct {
    MetaDrmBufferDumb *current_dumb_fb;
    MetaDrmBufferDumb *dumb_fbs[3];
    MetaDrmBuffer *source_fbs[3];
  } cpu;

  gboolean noted_primary_gpu_copy_ok;
  gboolean noted_primary_gpu_copy_failed;
  MetaSharedFramebufferImportStatus import_status;
} MetaOnscreenNativeSecondaryGpuState;

struct _MetaOnscreenNative
{
  CoglOnscreenEgl parent;

  MetaRendererNative *renderer_native;
  MetaGpuKms *render_gpu;
  MetaOutput *output;
  MetaCrtc *crtc;

  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  struct {
    struct gbm_surface *surface;
    MetaDrmBuffer *next_fb;
    MetaDrmBuffer *stalled_fb;
  } gbm;

#ifdef HAVE_EGL_DEVICE
  struct {
    EGLStreamKHR stream;

    MetaDrmBufferDumb *dumb_fb;
  } egl;
#endif

  MetaRendererView *view;

  unsigned int swaps_pending;
  struct {
    int *rectangles;  /* 4 x n_rectangles */
    int n_rectangles;
  } next_post;
};

G_DEFINE_TYPE (MetaOnscreenNative, meta_onscreen_native,
               COGL_TYPE_ONSCREEN_EGL)

static void
try_post_latest_swap (CoglOnscreen *onscreen);

static gboolean
init_secondary_gpu_state (MetaRendererNative  *renderer_native,
                          CoglOnscreen        *onscreen,
                          GError             **error);

static void
maybe_update_frame_info (MetaCrtc         *crtc,
                         CoglFrameInfo    *frame_info,
                         int64_t           time_us,
                         CoglFrameInfoFlag flags,
                         unsigned int      sequence)
{
  const MetaCrtcConfig *crtc_config;
  const MetaCrtcModeInfo *crtc_mode_info;
  float refresh_rate;

  g_return_if_fail (crtc);

  crtc_config = meta_crtc_get_config (crtc);
  if (!crtc_config)
    return;

  crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);
  refresh_rate = crtc_mode_info->refresh_rate;
  if (refresh_rate >= frame_info->refresh_rate)
    {
      frame_info->presentation_time_us = time_us;
      frame_info->refresh_rate = refresh_rate;
      frame_info->flags |= flags;
      frame_info->sequence = sequence;
    }
}

static void
meta_onscreen_native_notify_frame_complete (CoglOnscreen *onscreen)
{
  CoglFrameInfo *info;

  info = cogl_onscreen_pop_head_frame_info (onscreen);

  g_assert (info);

  _cogl_onscreen_notify_frame_sync (onscreen, info);
  _cogl_onscreen_notify_complete (onscreen, info);
  cogl_object_unref (info);
}

static void
notify_view_crtc_presented (MetaRendererView *view,
                            MetaKmsCrtc      *kms_crtc,
                            int64_t           time_us,
                            CoglFrameInfoFlag flags,
                            unsigned int      sequence)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (view);
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (stage_view);
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglFrameInfo *frame_info;
  MetaCrtc *crtc;

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);

  g_return_if_fail (frame_info != NULL);

  crtc = META_CRTC (meta_crtc_kms_from_kms_crtc (kms_crtc));
  maybe_update_frame_info (crtc, frame_info, time_us, flags, sequence);

  meta_onscreen_native_notify_frame_complete (onscreen);
  try_post_latest_swap (onscreen);
}

static int64_t
timeval_to_microseconds (const struct timeval *tv)
{
  return ((int64_t) tv->tv_sec) * G_USEC_PER_SEC + tv->tv_usec;
}

static void
page_flip_feedback_flipped (MetaKmsCrtc  *kms_crtc,
                            unsigned int  sequence,
                            unsigned int  tv_sec,
                            unsigned int  tv_usec,
                            gpointer      user_data)
{
  MetaRendererView *view = user_data;
  struct timeval page_flip_time;
  MetaKmsDevice *kms_device;
  int64_t presentation_time_us;
  CoglFrameInfoFlag flags = COGL_FRAME_INFO_FLAG_VSYNC;

  page_flip_time = (struct timeval) {
    .tv_sec = tv_sec,
    .tv_usec = tv_usec,
  };

  kms_device = meta_kms_crtc_get_device (kms_crtc);
  if (meta_kms_device_uses_monotonic_clock (kms_device))
    {
      presentation_time_us = timeval_to_microseconds (&page_flip_time);
      flags |= COGL_FRAME_INFO_FLAG_HW_CLOCK;
    }
  else
    {
      /*
       * Other parts of the code assume MONOTONIC timestamps. So, if the device
       * timestamp isn't MONOTONIC, don't use it.
       */
      presentation_time_us = g_get_monotonic_time ();
    }

  notify_view_crtc_presented (view, kms_crtc,
                              presentation_time_us,
                              flags,
                              sequence);
}

static void
page_flip_feedback_ready (MetaKmsCrtc *kms_crtc,
                          gpointer     user_data)
{
  MetaRendererView *view = user_data;
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (CLUTTER_STAGE_VIEW (view));
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglFrameInfo *frame_info;

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;

  meta_onscreen_native_notify_frame_complete (onscreen);
  try_post_latest_swap (onscreen);
}

static void
page_flip_feedback_mode_set_fallback (MetaKmsCrtc *kms_crtc,
                                      gpointer     user_data)
{
  MetaRendererView *view = user_data;
  int64_t now_us;

  /*
   * We ended up not page flipping, thus we don't have a presentation time to
   * use. Lets use the next best thing: the current time.
   */

  now_us = g_get_monotonic_time ();

  notify_view_crtc_presented (view,
                              kms_crtc,
                              now_us,
                              COGL_FRAME_INFO_FLAG_NONE,
                              0);
}

static void
page_flip_feedback_discarded (MetaKmsCrtc  *kms_crtc,
                              gpointer      user_data,
                              const GError *error)
{
  MetaRendererView *view = user_data;
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (CLUTTER_STAGE_VIEW (view));
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglFrameInfo *frame_info;

  /*
   * Page flipping failed, but we want to fail gracefully, so to avoid freezing
   * the frame clock, emit a symbolic flip.
   */

  if (error &&
      !g_error_matches (error,
                        G_IO_ERROR,
                        G_IO_ERROR_PERMISSION_DENIED))
    g_warning ("Page flip discarded: %s", error->message);

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;

  meta_onscreen_native_notify_frame_complete (onscreen);
  try_post_latest_swap (onscreen);
}

static const MetaKmsPageFlipListenerVtable page_flip_listener_vtable = {
  .flipped = page_flip_feedback_flipped,
  .ready = page_flip_feedback_ready,
  .mode_set_fallback = page_flip_feedback_mode_set_fallback,
  .discarded = page_flip_feedback_discarded,
};

static MetaEgl *
meta_onscreen_native_get_egl (MetaOnscreenNative *onscreen_native)
{
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;

  return meta_renderer_native_get_egl (renderer_native);
}

#ifdef HAVE_EGL_DEVICE
static int
custom_egl_stream_page_flip (gpointer custom_page_flip_data,
                             gpointer user_data)
{
  CoglOnscreen *onscreen = custom_page_flip_data;
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererView *view = user_data;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRenderDevice *render_device;
  EGLDisplay *egl_display;
  EGLAttrib *acquire_attribs;
  g_autoptr (GError) error = NULL;

  acquire_attribs = (EGLAttrib[]) {
    EGL_DRM_FLIP_EVENT_DATA_NV,
    (EGLAttrib) view,
    EGL_NONE
  };

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (onscreen_native->renderer_native,
                                       onscreen_native->render_gpu);
  render_device = renderer_gpu_data->render_device;

  egl_display = meta_render_device_get_egl_display (render_device);
  if (!meta_egl_stream_consumer_acquire_attrib (egl,
                                                egl_display,
                                                onscreen_native->egl.stream,
                                                acquire_attribs,
                                                &error))
    {
      if (g_error_matches (error, META_EGL_ERROR, EGL_RESOURCE_BUSY_EXT))
        return -EBUSY;
      else
        return -EINVAL;
    }

  return 0;
}
#endif /* HAVE_EGL_DEVICE */

static void
drop_stalled_swap (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglFrameInfo *frame_info;

  /* Remember we can't compare stalled_fb because it's not used by
   * META_RENDERER_NATIVE_MODE_EGL_DEVICE. So we judge stalled to be whenever
   * swaps_pending > 1.
   */
  if (onscreen_native->swaps_pending <= 1)
    return;

  onscreen_native->swaps_pending--;

  g_clear_object (&onscreen_native->gbm.stalled_fb);

  frame_info = cogl_onscreen_peek_tail_frame_info (onscreen);
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;
  meta_onscreen_native_notify_frame_complete (onscreen);
}

void
meta_onscreen_native_dummy_power_save_page_flip (CoglOnscreen *onscreen)
{
  drop_stalled_swap (onscreen);

  /* If the monitor just woke up and the shell is fully idle (has nothing
   * more to swap) then we just woke to an indefinitely black screen. Let's
   * fix that using the last swap (which is never classified as "stalled").
   */
  try_post_latest_swap (onscreen);
}

static void
meta_onscreen_native_flip_crtc (CoglOnscreen                *onscreen,
                                MetaRendererView            *view,
                                MetaCrtc                    *crtc,
                                MetaKmsPageFlipListenerFlag  flags,
                                const int                   *rectangles,
                                int                          n_rectangles)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaGpuKms *gpu_kms;
  MetaKmsDevice *kms_device;
  MetaKms *kms;
  MetaKmsUpdate *kms_update;
  g_autoptr (MetaDrmBuffer) buffer = NULL;
  MetaKmsPlaneAssignment *plane_assignment;

  COGL_TRACE_BEGIN_SCOPED (MetaOnscreenNativeFlipCrtcs,
                           "Onscreen (flip CRTCs)");

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  kms = meta_kms_device_get_kms (kms_device);
  kms_update = meta_kms_ensure_pending_update_for_crtc (kms, kms_crtc);

  g_assert (meta_gpu_kms_is_crtc_active (gpu_kms, crtc));

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      buffer = g_steal_pointer (&onscreen_native->gbm.next_fb);

      plane_assignment = meta_crtc_kms_assign_primary_plane (crtc_kms,
                                                             buffer,
                                                             kms_update);

      if (rectangles != NULL && n_rectangles != 0)
        {
          meta_kms_plane_assignment_set_fb_damage (plane_assignment,
                                                   rectangles, n_rectangles);
        }

      g_object_set_data_full (G_OBJECT (buffer),
                              "gbm_surface owner",
                              g_object_ref (onscreen),
                              (GDestroyNotify) g_object_unref);
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      meta_kms_update_set_custom_page_flip (kms_update,
                                            custom_egl_stream_page_flip,
                                            onscreen_native);
      break;
#endif
    }

  meta_kms_update_add_page_flip_listener (kms_update,
                                          kms_crtc,
                                          &page_flip_listener_vtable,
                                          flags,
                                          g_object_ref (view),
                                          g_object_unref);
}

static void
meta_onscreen_native_set_crtc_mode (CoglOnscreen              *onscreen,
                                    MetaRendererNativeGpuData *renderer_gpu_data)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  MetaKms *kms = meta_kms_device_get_kms (kms_device);
  MetaKmsUpdate *kms_update;

  COGL_TRACE_BEGIN_SCOPED (MetaOnscreenNativeSetCrtcModes,
                           "Onscreen (set CRTC modes)");

  kms_update = meta_kms_ensure_pending_update_for_crtc (kms, kms_crtc);

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      {
        MetaDrmBuffer *buffer;

        buffer = META_DRM_BUFFER (onscreen_native->egl.dumb_fb);
        meta_crtc_kms_assign_primary_plane (crtc_kms, buffer, kms_update);
        break;
      }
#endif
    }

  meta_crtc_kms_set_mode (crtc_kms, kms_update);
  meta_output_kms_set_underscan (META_OUTPUT_KMS (onscreen_native->output),
                                 kms_update);
  meta_output_kms_set_max_bpc (META_OUTPUT_KMS (onscreen_native->output),
                               kms_update);
}

static void
hold_primary_gpu_fb_for_secondary_gpu_scanout (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                                               MetaDrmBuffer                       *primary_gpu_fb,
                                               MetaDrmBuffer                       *secondary_gpu_fb)
{
  if (META_IS_DRM_BUFFER_DUMB (secondary_gpu_fb))
    {
      MetaDrmBufferDumb *dumb_fb = META_DRM_BUFFER_DUMB (secondary_gpu_fb);
      int i;
      const int n = G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs);

      for (i = 0; i < n; i++)
        {
          if (dumb_fb == secondary_gpu_state->cpu.dumb_fbs[i])
            {
              g_set_object (&secondary_gpu_state->cpu.source_fbs[i],
                            primary_gpu_fb);
              break;
            }
        }

      g_warn_if_fail (i < n);
    }
}

static void
secondary_gpu_release_dumb (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  unsigned i;

  for (i = 0; i < G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs); i++)
    {
      g_clear_object (&secondary_gpu_state->cpu.dumb_fbs[i]);
      g_clear_object (&secondary_gpu_state->cpu.source_fbs[i]);
    }
}

static void
secondary_gpu_state_free (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  MetaGpu *gpu = META_GPU (secondary_gpu_state->gpu_kms);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaEgl *egl = meta_backend_get_egl (backend);

  if (secondary_gpu_state->egl_surface != EGL_NO_SURFACE)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;
      MetaRenderDevice *render_device;
      EGLDisplay egl_display;

      renderer_gpu_data = secondary_gpu_state->renderer_gpu_data;
      render_device = renderer_gpu_data->render_device;
      egl_display = meta_render_device_get_egl_display (render_device);
      meta_egl_destroy_surface (egl,
                                egl_display,
                                secondary_gpu_state->egl_surface,
                                NULL);
    }

  g_clear_pointer (&secondary_gpu_state->gbm.surface, gbm_surface_destroy);

  secondary_gpu_release_dumb (secondary_gpu_state);

  g_free (secondary_gpu_state);
}

static MetaDrmBuffer *
import_shared_framebuffer (CoglOnscreen                        *onscreen,
                           MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                           MetaDrmBuffer                       *primary_gpu_fb)
{
  MetaRenderDevice *render_device;
  g_autoptr (GError) error = NULL;
  MetaDrmBuffer *imported_buffer;

  render_device = secondary_gpu_state->renderer_gpu_data->render_device;
  imported_buffer =
    meta_render_device_import_dma_buf (render_device,
                                       primary_gpu_fb,
                                       &error);
  if (!imported_buffer)
    {
      meta_topic (META_DEBUG_KMS,
                  "Zero-copy disabled for %s, "
                  "meta_drm_buffer_import_new failed: %s",
                  meta_render_device_get_name (render_device),
                  error->message);

      g_warn_if_fail (secondary_gpu_state->import_status ==
                      META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE);
      secondary_gpu_state->import_status =
        META_SHARED_FRAMEBUFFER_IMPORT_STATUS_FAILED;
      return NULL;
    }

  if (secondary_gpu_state->import_status ==
      META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE)
    {
      /*
       * Clean up the cpu-copy part of
       * init_secondary_gpu_state_cpu_copy_mode ()
       */
      secondary_gpu_release_dumb (secondary_gpu_state);

      meta_topic (META_DEBUG_KMS,
                  "Using zero-copy for %s succeeded once.",
                  meta_render_device_get_name (render_device));
    }

  secondary_gpu_state->import_status =
    META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK;
  return imported_buffer;
}

static MetaDrmBuffer *
copy_shared_framebuffer_gpu (CoglOnscreen                        *onscreen,
                             MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                             MetaRendererNativeGpuData           *renderer_gpu_data,
                             gboolean                            *egl_context_changed,
                             MetaDrmBuffer                       *primary_gpu_fb)
{
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  MetaGles3 *gles3 = meta_renderer_native_get_gles3 (renderer_native);
  MetaRenderDevice *render_device;
  EGLDisplay egl_display;
  GError *error = NULL;
  gboolean use_modifiers;
  MetaDeviceFile *device_file;
  MetaDrmBufferFlags flags;
  MetaDrmBufferGbm *buffer_gbm;
  struct gbm_bo *bo;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferSecondaryGpu,
                           "FB Copy (secondary GPU)");

  render_device = renderer_gpu_data->render_device;
  egl_display = meta_render_device_get_egl_display (render_device);

  if (!meta_egl_make_current (egl,
                              egl_display,
                              secondary_gpu_state->egl_surface,
                              secondary_gpu_state->egl_surface,
                              renderer_gpu_data->secondary.egl_context,
                              &error))
    {
      g_warning ("Failed to make current: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  *egl_context_changed = TRUE;


  buffer_gbm = META_DRM_BUFFER_GBM (primary_gpu_fb);
  bo = meta_drm_buffer_gbm_get_bo (buffer_gbm);
  if (!meta_renderer_native_gles3_blit_shared_bo (egl,
                                                  gles3,
                                                  egl_display,
                                                  renderer_gpu_data->secondary.egl_context,
                                                  secondary_gpu_state->egl_surface,
                                                  bo,
                                                  &error))
    {
      g_warning ("Failed to blit shared framebuffer: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  if (!meta_egl_swap_buffers (egl,
                              egl_display,
                              secondary_gpu_state->egl_surface,
                              &error))
    {
      g_warning ("Failed to swap buffers: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  use_modifiers = meta_renderer_native_use_modifiers (renderer_native);
  device_file = meta_render_device_get_device_file (render_device);

  flags = META_DRM_BUFFER_FLAG_NONE;
  if (!use_modifiers)
    flags |= META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS;

  buffer_gbm =
    meta_drm_buffer_gbm_new_lock_front (device_file,
                                        secondary_gpu_state->gbm.surface,
                                        flags,
                                        &error);
  if (!buffer_gbm)
    {
      g_warning ("meta_drm_buffer_gbm_new_lock_front failed: %s",
                 error->message);
      g_error_free (error);
      return NULL;
    }

  return META_DRM_BUFFER (buffer_gbm);
}

static MetaDrmBufferDumb *
secondary_gpu_get_next_dumb_buffer (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  MetaDrmBufferDumb *current_dumb_fb;
  const int n_dumb_fbs = G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs);
  int i;

  current_dumb_fb = secondary_gpu_state->cpu.current_dumb_fb;
  for (i = 0; i < n_dumb_fbs; i++)
    {
      if (current_dumb_fb == secondary_gpu_state->cpu.dumb_fbs[i])
        return secondary_gpu_state->cpu.dumb_fbs[(i + 1) % n_dumb_fbs];
    }

  return secondary_gpu_state->cpu.dumb_fbs[0];
}

static MetaDrmBuffer *
copy_shared_framebuffer_primary_gpu (CoglOnscreen                        *onscreen,
                                     MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                                     const int                           *rectangles,
                                     int                                  n_rectangles)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaGpuKms *primary_gpu;
  MetaRendererNativeGpuData *primary_gpu_data;
  MetaDrmBufferDumb *buffer_dumb;
  MetaDrmBuffer *buffer;
  int width, height, stride;
  uint32_t drm_format;
  CoglFramebuffer *dmabuf_fb;
  int dmabuf_fd;
  g_autoptr (GError) error = NULL;
  CoglPixelFormat cogl_format;
  int ret;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferPrimaryGpu,
                           "FB Copy (primary GPU)");

  if (!secondary_gpu_state ||
      secondary_gpu_state->egl_surface == EGL_NO_SURFACE)
    return NULL;

  primary_gpu = meta_renderer_native_get_primary_gpu (renderer_native);
  primary_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native, primary_gpu);
  if (!primary_gpu_data->secondary.has_EGL_EXT_image_dma_buf_import_modifiers)
    return NULL;

  buffer_dumb = secondary_gpu_get_next_dumb_buffer (secondary_gpu_state);
  buffer = META_DRM_BUFFER (buffer_dumb);

  width = meta_drm_buffer_get_width (buffer);
  height = meta_drm_buffer_get_height (buffer);
  stride = meta_drm_buffer_get_stride (buffer);
  drm_format = meta_drm_buffer_get_format (buffer);

  g_assert (cogl_framebuffer_get_width (framebuffer) == width);
  g_assert (cogl_framebuffer_get_height (framebuffer) == height);

  ret = meta_cogl_pixel_format_from_drm_format (drm_format,
                                                &cogl_format,
                                                NULL);
  g_assert (ret);

  dmabuf_fd = meta_drm_buffer_dumb_ensure_dmabuf_fd (buffer_dumb, &error);
  if (!dmabuf_fd)
    {
      meta_topic (META_DEBUG_KMS,
                  "Failed to create DMA buffer: %s", error->message);
      return NULL;
    }

  dmabuf_fb =
    meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                     dmabuf_fd,
                                                     width,
                                                     height,
                                                     stride,
                                                     0, DRM_FORMAT_MOD_LINEAR,
                                                     drm_format,
                                                     &error);

  if (error)
    {
      meta_topic (META_DEBUG_KMS,
                  "Failed to create DMA buffer for blitting: %s",
                  error->message);
      return NULL;
    }
  /* Limit the number of individual copies to 16 */
#define MAX_RECTS 16

  if (n_rectangles == 0 || n_rectangles > MAX_RECTS)
    {
      if (!cogl_blit_framebuffer (framebuffer, COGL_FRAMEBUFFER (dmabuf_fb),
                                  0, 0, 0, 0,
                                  width, height,
                                  &error))
        {
          g_object_unref (dmabuf_fb);
          return NULL;
        }
    }
  else
    {
      int i;

      for (i = 0; i < n_rectangles; ++i)
        {
          if (!cogl_blit_framebuffer (framebuffer, COGL_FRAMEBUFFER (dmabuf_fb),
                                      rectangles[i * 4], rectangles[i * 4 + 1],
                                      rectangles[i * 4], rectangles[i * 4 + 1],
                                      rectangles[i * 4 + 2],
                                      rectangles[i * 4 + 3],
                                      &error))
            {
              g_object_unref (dmabuf_fb);
              return NULL;
            }
        }
    }

  g_object_unref (dmabuf_fb);

  secondary_gpu_state->cpu.current_dumb_fb = buffer_dumb;

  return g_object_ref (buffer);
}

static MetaDrmBuffer *
copy_shared_framebuffer_cpu (CoglOnscreen                        *onscreen,
                             MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                             MetaRendererNativeGpuData           *renderer_gpu_data)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  MetaDrmBufferDumb *buffer_dumb;
  MetaDrmBuffer *buffer;
  int width, height, stride;
  uint32_t drm_format;
  void *buffer_data;
  CoglBitmap *dumb_bitmap;
  CoglPixelFormat cogl_format;
  gboolean ret;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferCpu,
                           "FB Copy (CPU)");

  buffer_dumb = secondary_gpu_get_next_dumb_buffer (secondary_gpu_state);
  buffer = META_DRM_BUFFER (buffer_dumb);

  width = meta_drm_buffer_get_width (buffer);
  height = meta_drm_buffer_get_height (buffer);
  stride = meta_drm_buffer_get_stride (buffer);
  drm_format = meta_drm_buffer_get_format (buffer);
  buffer_data = meta_drm_buffer_dumb_get_data (buffer_dumb);

  g_assert (cogl_framebuffer_get_width (framebuffer) == width);
  g_assert (cogl_framebuffer_get_height (framebuffer) == height);

  ret = meta_cogl_pixel_format_from_drm_format (drm_format,
                                                &cogl_format,
                                                NULL);
  g_assert (ret);

  dumb_bitmap = cogl_bitmap_new_for_data (cogl_context,
                                          width,
                                          height,
                                          cogl_format,
                                          stride,
                                          buffer_data);

  if (!cogl_framebuffer_read_pixels_into_bitmap (framebuffer,
                                                 0 /* x */,
                                                 0 /* y */,
                                                 COGL_READ_PIXELS_COLOR_BUFFER,
                                                 dumb_bitmap))
    g_warning ("Failed to CPU-copy to a secondary GPU output");

  cogl_object_unref (dumb_bitmap);

  secondary_gpu_state->cpu.current_dumb_fb = buffer_dumb;

  return g_object_ref (buffer);
}

static MetaDrmBuffer *
update_secondary_gpu_state_pre_swap_buffers (CoglOnscreen *onscreen,
                                             const int    *rectangles,
                                             int           n_rectangles)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaDrmBuffer *copy = NULL;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeGpuStatePreSwapBuffers,
                           "Onscreen (secondary gpu pre-swap-buffers)");

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (secondary_gpu_state)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;
      MetaRenderDevice *render_device;

      renderer_gpu_data = secondary_gpu_state->renderer_gpu_data;
      render_device = renderer_gpu_data->render_device;
      switch (renderer_gpu_data->secondary.copy_mode)
        {
        case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
          /* Done after eglSwapBuffers. */
          break;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
          /* Done after eglSwapBuffers. */
          if (secondary_gpu_state->import_status ==
              META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK)
            break;
          /* prepare fallback */
          G_GNUC_FALLTHROUGH;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
          copy = copy_shared_framebuffer_primary_gpu (onscreen,
                                                      secondary_gpu_state,
                                                      rectangles,
                                                      n_rectangles);
          if (!copy)
            {
              if (!secondary_gpu_state->noted_primary_gpu_copy_failed)
                {
                  meta_topic (META_DEBUG_KMS,
                              "Using primary GPU to copy for %s failed once.",
                              meta_render_device_get_name (render_device));
                  secondary_gpu_state->noted_primary_gpu_copy_failed = TRUE;
                }

              copy = copy_shared_framebuffer_cpu (onscreen,
                                                  secondary_gpu_state,
                                                  renderer_gpu_data);
            }
          else if (!secondary_gpu_state->noted_primary_gpu_copy_ok)
            {
              meta_topic (META_DEBUG_KMS,
                          "Using primary GPU to copy for %s succeeded once.",
                          meta_render_device_get_name (render_device));
              secondary_gpu_state->noted_primary_gpu_copy_ok = TRUE;
            }
          break;
        }
    }

  return copy;
}

static void
update_secondary_gpu_state_post_swap_buffers (CoglOnscreen   *onscreen,
                                              gboolean       *egl_context_changed,
                                              MetaDrmBuffer  *primary_gpu_fb,
                                              MetaDrmBuffer **secondary_gpu_fb)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeGpuStatePostSwapBuffers,
                           "Onscreen (secondary gpu post-swap-buffers)");

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (secondary_gpu_state)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;
      g_autoptr (MetaDrmBuffer) next_fb = NULL;

      renderer_gpu_data =
        meta_renderer_native_get_gpu_data (renderer_native,
                                           secondary_gpu_state->gpu_kms);
      switch (renderer_gpu_data->secondary.copy_mode)
        {
        case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
          next_fb = import_shared_framebuffer (onscreen,
                                               secondary_gpu_state,
                                               primary_gpu_fb);
          if (next_fb)
            break;
          /* The fallback was prepared in pre_swap_buffers and is currently
           * in secondary_gpu_fb.
           */
          renderer_gpu_data->secondary.copy_mode =
            META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY;
          G_GNUC_FALLTHROUGH;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
          next_fb = g_object_ref (*secondary_gpu_fb);
          break;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
          next_fb = copy_shared_framebuffer_gpu (onscreen,
                                                 secondary_gpu_state,
                                                 renderer_gpu_data,
                                                 egl_context_changed,
                                                 primary_gpu_fb);
          break;
        }

      g_set_object (secondary_gpu_fb, next_fb);
    }
}

static void
ensure_crtc_modes (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  if (meta_renderer_native_pop_pending_mode_set (renderer_native,
                                                 onscreen_native->view))
    meta_onscreen_native_set_crtc_mode (onscreen, renderer_gpu_data);
}

static void
meta_onscreen_native_swap_buffers_with_damage (CoglOnscreen  *onscreen,
                                               const int     *rectangles,
                                               int            n_rectangles,
                                               CoglFrameInfo *frame_info,
                                               gpointer       user_data)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  MetaDeviceFile *render_device_file;
  ClutterFrame *frame = user_data;
  CoglOnscreenClass *parent_class;
  gboolean egl_context_changed = FALSE;
  g_autoptr (GError) error = NULL;
  MetaDrmBufferFlags buffer_flags;
  MetaDrmBufferGbm *buffer_gbm;
  g_autoptr (MetaDrmBuffer) primary_gpu_fb = NULL;
  g_autoptr (MetaDrmBuffer) secondary_gpu_fb = NULL;
  size_t rectangles_size;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeSwapBuffers,
                           "Onscreen (swap-buffers)");

  if (meta_is_topic_enabled (META_DEBUG_KMS))
    {
      unsigned int frames_pending =
        cogl_onscreen_count_pending_frames (onscreen);

      meta_topic (META_DEBUG_KMS,
                  "Swap buffers: %u frames pending (%s-buffering)",
                  frames_pending,
                  frames_pending == 1 ? "double" :
                  frames_pending == 2 ? "triple" :
                  "?");
    }

  secondary_gpu_fb =
    update_secondary_gpu_state_pre_swap_buffers (onscreen,
                                                 rectangles,
                                                 n_rectangles);

  parent_class = COGL_ONSCREEN_CLASS (meta_onscreen_native_parent_class);
  parent_class->swap_buffers_with_damage (onscreen,
                                          rectangles,
                                          n_rectangles,
                                          frame_info,
                                          user_data);

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);
  render_device_file =
    meta_render_device_get_device_file (renderer_gpu_data->render_device);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      buffer_flags = META_DRM_BUFFER_FLAG_NONE;
      if (!meta_renderer_native_use_modifiers (renderer_native))
        buffer_flags |= META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS;

      buffer_gbm =
        meta_drm_buffer_gbm_new_lock_front (render_device_file,
                                            onscreen_native->gbm.surface,
                                            buffer_flags,
                                            &error);
      if (!buffer_gbm)
        {
          g_warning ("Failed to lock front buffer on %s: %s",
                     meta_device_file_get_path (render_device_file),
                     error->message);
          return;
        }

      primary_gpu_fb = META_DRM_BUFFER (g_steal_pointer (&buffer_gbm));
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      break;
#endif
    }

  update_secondary_gpu_state_post_swap_buffers (onscreen,
                                                &egl_context_changed,
                                                primary_gpu_fb,
                                                &secondary_gpu_fb);

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (onscreen_native->gbm.next_fb != NULL)
        {
          g_warn_if_fail (onscreen_native->gbm.stalled_fb == NULL);
          drop_stalled_swap (onscreen);
          g_assert (onscreen_native->gbm.stalled_fb == NULL);
          onscreen_native->gbm.stalled_fb =
            g_steal_pointer (&onscreen_native->gbm.next_fb);
        }

      if (onscreen_native->secondary_gpu_state)
        {
          g_set_object (&onscreen_native->gbm.next_fb, secondary_gpu_fb);
          hold_primary_gpu_fb_for_secondary_gpu_scanout (
            onscreen_native->secondary_gpu_state,
            primary_gpu_fb,
            secondary_gpu_fb);
        }
      else
        {
          g_set_object (&onscreen_native->gbm.next_fb, primary_gpu_fb);
        }
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      break;
#endif
    }

  clutter_frame_set_result (frame,
                            CLUTTER_FRAME_RESULT_PENDING_PRESENTED);

  /*
   * If we changed EGL context, cogl will have the wrong idea about what is
   * current, making it fail to set it when it needs to. Avoid that by making
   * EGL_NO_CONTEXT current now, making cogl eventually set the correct
   * context.
   */
  if (egl_context_changed)
    _cogl_winsys_egl_ensure_current (cogl_display);

  rectangles_size = n_rectangles * 4 * sizeof (int);
  onscreen_native->next_post.rectangles =
    g_realloc (onscreen_native->next_post.rectangles, rectangles_size);
  memcpy (onscreen_native->next_post.rectangles, rectangles, rectangles_size);
  onscreen_native->next_post.n_rectangles = n_rectangles;

  onscreen_native->swaps_pending++;
  try_post_latest_swap (onscreen);
}

static void
try_post_latest_swap (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaPowerSave power_save_mode;
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  MetaKmsUpdateFlag flags;
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
  const GError *feedback_error;
  unsigned int frames_pending = cogl_onscreen_count_pending_frames (onscreen);

  if (meta_kms_is_shutting_down (kms))
    {
      meta_onscreen_native_discard_pending_swaps (onscreen);
      return;
    }

  if (onscreen_native->swaps_pending == 0)
    return;

  g_assert (frames_pending >= onscreen_native->swaps_pending);

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode == META_POWER_SAVE_ON)
    {
      unsigned int posts_pending;

      posts_pending = frames_pending - onscreen_native->swaps_pending;
      if (posts_pending > 0)
        return;  /* wait for the next frame notification and then try again */

      drop_stalled_swap (onscreen);
      g_return_if_fail (onscreen_native->swaps_pending > 0);
      onscreen_native->swaps_pending--;

      ensure_crtc_modes (onscreen);
      meta_onscreen_native_flip_crtc (onscreen,
                                      onscreen_native->view,
                                      onscreen_native->crtc,
                                      META_KMS_PAGE_FLIP_LISTENER_FLAG_NONE,
                                      onscreen_native->next_post.rectangles,
                                      onscreen_native->next_post.n_rectangles);
    }
  else
    {
      meta_renderer_native_queue_power_save_page_flip (renderer_native,
                                                       onscreen);
      return;
    }

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativePostKmsUpdate,
                           "Onscreen (post pending update)");
  kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (onscreen_native->crtc));
  kms_device = meta_kms_crtc_get_device (kms_crtc);

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (meta_renderer_native_has_pending_mode_sets (renderer_native))
        {
          meta_topic (META_DEBUG_KMS,
                      "Postponing primary plane composite update for CRTC %u (%s)",
                      meta_kms_crtc_get_id (kms_crtc),
                      meta_kms_device_get_path (kms_device));
          return;
        }
      else if (meta_renderer_native_has_pending_mode_set (renderer_native))
        {
          meta_topic (META_DEBUG_KMS, "Posting global mode set updates on %s",
                      meta_kms_device_get_path (kms_device));

          meta_renderer_native_notify_mode_sets_reset (renderer_native);
          meta_renderer_native_post_mode_set_updates (renderer_native);
          return;
        }
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      if (meta_renderer_native_has_pending_mode_set (renderer_native))
        {
          meta_renderer_native_notify_mode_sets_reset (renderer_native);
          meta_renderer_native_post_mode_set_updates (renderer_native);
          return;
        }
      break;
#endif
    }

  meta_topic (META_DEBUG_KMS,
              "Posting primary plane composite update for CRTC %u (%s)",
              meta_kms_crtc_get_id (kms_crtc),
              meta_kms_device_get_path (kms_device));

  flags = META_KMS_UPDATE_FLAG_NONE;
  kms_feedback = meta_kms_post_pending_update_for_crtc_sync (kms,
                                                             kms_crtc,
                                                             flags);
  g_return_if_fail (kms_feedback != NULL);

  switch (meta_kms_feedback_get_result (kms_feedback))
    {
    case META_KMS_FEEDBACK_PASSED:
      break;
    case META_KMS_FEEDBACK_FAILED:
      feedback_error = meta_kms_feedback_get_error (kms_feedback);
      if (!g_error_matches (feedback_error,
                            G_IO_ERROR,
                            G_IO_ERROR_PERMISSION_DENIED))
        g_warning ("Failed to post KMS update: %s", feedback_error->message);
      break;
    }
}

gboolean
meta_onscreen_native_is_buffer_scanout_compatible (CoglOnscreen  *onscreen,
                                                   MetaDrmBuffer *fb)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtc *crtc = onscreen_native->crtc;
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
  MetaGpuKms *gpu_kms;
  MetaKmsDevice *kms_device;
  MetaKms *kms;
  MetaKmsUpdate *test_update;
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
  MetaKmsFeedbackResult result;

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  kms = meta_kms_device_get_kms (kms_device);
  test_update = meta_kms_update_new (kms_device);

  meta_crtc_kms_assign_primary_plane (crtc_kms, fb, test_update);
  kms_feedback = meta_kms_post_test_update_sync (kms, test_update);
  meta_kms_update_free (test_update);

  result = meta_kms_feedback_get_result (kms_feedback);
  return result == META_KMS_FEEDBACK_PASSED;
}

static gboolean
meta_onscreen_native_direct_scanout (CoglOnscreen   *onscreen,
                                     CoglScanout    *scanout,
                                     CoglFrameInfo  *frame_info,
                                     gpointer        user_data,
                                     GError        **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaPowerSave power_save_mode;
  ClutterFrame *frame = user_data;
  MetaDrmBuffer *scanout_buffer;
  GError *fill_timings_error = NULL;
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsUpdateFlag flags;
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
  const GError *feedback_error;

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode != META_POWER_SAVE_ON)
    {
      g_set_error_literal (error,
                           COGL_SCANOUT_ERROR,
                           COGL_SCANOUT_ERROR_INHIBITED,
                           "Direct scanout is inhibited during power saving mode");
      return FALSE;
    }

  if (meta_renderer_native_has_pending_mode_set (renderer_native))
    {
      g_set_error_literal (error,
                           COGL_SCANOUT_ERROR,
                           COGL_SCANOUT_ERROR_INHIBITED,
                           "Direct scanout is inhibited when a mode set is pending");
      return FALSE;
    }

  /* Our direct scanout frame counts as 1, so more than that means we would
   * be jumping the queue (and post would fail).
   */
  if (cogl_onscreen_count_pending_frames (onscreen) > 1)
    {
      g_set_error_literal (error,
                           COGL_SCANOUT_ERROR,
                           COGL_SCANOUT_ERROR_INHIBITED,
                           "Direct scanout is inhibited during triple buffering");
      return FALSE;
    }

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);

  g_warn_if_fail (renderer_gpu_data->mode == META_RENDERER_NATIVE_MODE_GBM);
  g_warn_if_fail (!onscreen_native->gbm.next_fb);

  g_set_object (&onscreen_native->gbm.next_fb, META_DRM_BUFFER (scanout));

  /* Try to get a measurement of GPU rendering time on the scanout buffer.
   *
   * The successful operation here adds ~0.4 ms to a ~0.1 ms total frame clock
   * dispatch duration when displaying an unredirected client, thus
   * unfortunately bringing it more in line with duration of the regular
   * non-unredirected frame clock dispatch. However, measuring GPU rendering
   * time is important for computing accurate max render time without
   * underestimating. Also this operation should be optimizable by caching
   * EGLImage for each buffer instead of re-creating it every time it's needed.
   * This should also help all other cases which convert the buffer to a
   * EGLImage.
   */
  if (META_IS_DRM_BUFFER (scanout))
    {
      scanout_buffer = META_DRM_BUFFER (scanout);
      if (meta_drm_buffer_supports_fill_timings (scanout_buffer))
        {
          if (!meta_drm_buffer_fill_timings (scanout_buffer, frame_info,
                                             &fill_timings_error))
            {
              g_warning ("Failed to fill timings for a scanout buffer: %s",
                         fill_timings_error->message);
              g_error_free (fill_timings_error);
            }
        }
    }

  ensure_crtc_modes (onscreen);
  meta_onscreen_native_flip_crtc (onscreen,
                                  onscreen_native->view,
                                  onscreen_native->crtc,
                                  META_KMS_PAGE_FLIP_LISTENER_FLAG_DROP_ON_ERROR,
                                  NULL,
                                  0);

  kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (onscreen_native->crtc));
  kms_device = meta_kms_crtc_get_device (kms_crtc);

  meta_topic (META_DEBUG_KMS,
              "Posting direct scanout update for CRTC %u (%s)",
              meta_kms_crtc_get_id (kms_crtc),
              meta_kms_device_get_path (kms_device));

  flags = META_KMS_UPDATE_FLAG_PRESERVE_ON_ERROR;
  kms_feedback = meta_kms_post_pending_update_for_crtc_sync (kms,
                                                             kms_crtc,
                                                             flags);
  switch (meta_kms_feedback_get_result (kms_feedback))
    {
    case META_KMS_FEEDBACK_PASSED:
      clutter_frame_set_result (frame,
                                CLUTTER_FRAME_RESULT_PENDING_PRESENTED);
      break;
    case META_KMS_FEEDBACK_FAILED:
      feedback_error = meta_kms_feedback_get_error (kms_feedback);

      if (g_error_matches (feedback_error,
                           G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
        break;

      g_propagate_error (error, g_error_copy (feedback_error));
      return FALSE;
    }

  return TRUE;
}

static void
add_onscreen_frame_info (MetaCrtc *crtc)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterStageWindow *stage_window = _clutter_stage_get_window (stage);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererView *view = meta_renderer_get_view_for_crtc (renderer, crtc);

  meta_stage_impl_add_onscreen_frame_info (META_STAGE_IMPL (stage_window),
                                           CLUTTER_STAGE_VIEW (view));
}

void
meta_onscreen_native_finish_frame (CoglOnscreen *onscreen,
                                   ClutterFrame *frame)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtc *crtc = onscreen_native->crtc;
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);;
  MetaKms *kms = meta_kms_device_get_kms (kms_device);
  MetaKmsUpdateFlag flags;
  MetaKmsUpdate *kms_update;
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
  const GError *error;

  if (cogl_onscreen_count_pending_frames (onscreen) > 0)
    return;

  kms_update = meta_kms_get_pending_update_for_crtc (kms, kms_crtc);
  if (!kms_update)
    {
      clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_IDLE);
      return;
    }

  meta_kms_update_add_page_flip_listener (kms_update,
                                          kms_crtc,
                                          &page_flip_listener_vtable,
                                          META_KMS_PAGE_FLIP_LISTENER_FLAG_NONE,
                                          g_object_ref (onscreen_native->view),
                                          g_object_unref);

  flags = META_KMS_UPDATE_FLAG_NONE;
  kms_feedback = meta_kms_post_pending_update_for_crtc_sync (kms,
                                                             kms_crtc,
                                                             flags);
  switch (meta_kms_feedback_get_result (kms_feedback))
    {
    case META_KMS_FEEDBACK_PASSED:
      add_onscreen_frame_info (crtc);
      clutter_frame_set_result (frame,
                                CLUTTER_FRAME_RESULT_PENDING_PRESENTED);
      break;
    case META_KMS_FEEDBACK_FAILED:
      add_onscreen_frame_info (crtc);
      clutter_frame_set_result (frame,
                                CLUTTER_FRAME_RESULT_PENDING_PRESENTED);

      error = meta_kms_feedback_get_error (kms_feedback);
      if (!g_error_matches (error,
                            G_IO_ERROR,
                            G_IO_ERROR_PERMISSION_DENIED))
        g_warning ("Failed to post KMS update: %s", error->message);
      break;
    }
}

void
meta_onscreen_native_discard_pending_swaps (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);

  onscreen_native->swaps_pending = 0;

  g_clear_object (&onscreen_native->gbm.stalled_fb);
  g_clear_object (&onscreen_native->gbm.next_fb);
}

static gboolean
should_surface_be_sharable (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);

  if (META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc)) ==
      onscreen_native->render_gpu)
    return FALSE;
  else
    return TRUE;
}

static uint32_t
get_gbm_format_from_egl (MetaEgl    *egl,
                         EGLDisplay  egl_display,
                         EGLConfig   egl_config)
{
  uint32_t gbm_format;
  EGLint native_visual_id;

  if (meta_egl_get_config_attrib (egl,
                                  egl_display,
                                  egl_config,
                                  EGL_NATIVE_VISUAL_ID,
                                  &native_visual_id,
                                  NULL))
    gbm_format = (uint32_t) native_visual_id;
  else
    g_assert_not_reached ();

  return gbm_format;
}

static GArray *
get_supported_kms_modifiers (MetaCrtcKms *crtc_kms,
                             uint32_t     format)
{
  GArray *modifiers;
  GArray *crtc_mods;
  unsigned int i;

  crtc_mods = meta_crtc_kms_get_modifiers (crtc_kms, format);
  if (!crtc_mods)
    return NULL;

  modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));

  /*
   * For each modifier from base_crtc, check if it's available on all other
   * CRTCs.
   */
  for (i = 0; i < crtc_mods->len; i++)
    {
      uint64_t modifier = g_array_index (crtc_mods, uint64_t, i);

      g_array_append_val (modifiers, modifier);
    }

  if (modifiers->len == 0)
    {
      g_array_free (modifiers, TRUE);
      return NULL;
    }

  return modifiers;
}

static GArray *
get_supported_egl_modifiers (CoglOnscreen *onscreen,
                             MetaCrtcKms  *crtc_kms,
                             uint32_t      format)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaGpu *gpu;
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRenderDevice *render_device;
  EGLDisplay egl_display;
  EGLint num_modifiers;
  GArray *modifiers;
  GError *error = NULL;
  gboolean ret;

  gpu = meta_crtc_get_gpu (META_CRTC (crtc_kms));
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         META_GPU_KMS (gpu));
  render_device = renderer_gpu_data->render_device;
  egl_display = meta_render_device_get_egl_display (render_device);

  if (!meta_egl_has_extensions (egl, egl_display, NULL,
                                "EGL_EXT_image_dma_buf_import_modifiers",
                                NULL))
    return NULL;

  ret = meta_egl_query_dma_buf_modifiers (egl, egl_display,
                                          format, 0, NULL, NULL,
                                          &num_modifiers, NULL);
  if (!ret || num_modifiers == 0)
    return NULL;

  modifiers = g_array_sized_new (FALSE, FALSE, sizeof (uint64_t),
                                 num_modifiers);
  ret = meta_egl_query_dma_buf_modifiers (egl, egl_display,
                                          format, num_modifiers,
                                          (EGLuint64KHR *) modifiers->data, NULL,
                                          &num_modifiers, &error);

  if (!ret)
    {
      g_warning ("Failed to query DMABUF modifiers: %s", error->message);
      g_error_free (error);
      g_array_free (modifiers, TRUE);
      return NULL;
    }

  return modifiers;
}

static GArray *
get_supported_modifiers (CoglOnscreen *onscreen,
                         uint32_t      format)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaGpu *gpu;
  g_autoptr (GArray) modifiers = NULL;

  gpu = meta_crtc_get_gpu (META_CRTC (crtc_kms));
  if (gpu == META_GPU (onscreen_native->render_gpu))
    modifiers = get_supported_kms_modifiers (crtc_kms, format);
  else
    modifiers = get_supported_egl_modifiers (onscreen, crtc_kms, format);

  return g_steal_pointer (&modifiers);
}

static GArray *
get_supported_kms_formats (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);

  return meta_crtc_kms_copy_drm_format_list (crtc_kms);
}

static gboolean
create_surfaces_gbm (CoglOnscreen        *onscreen,
                     int                  width,
                     int                  height,
                     struct gbm_surface **gbm_surface,
                     EGLSurface          *egl_surface,
                     GError             **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRenderDeviceGbm *render_device_gbm;
  struct gbm_device *gbm_device;
  struct gbm_surface *new_gbm_surface = NULL;
  EGLNativeWindowType egl_native_window;
  EGLSurface new_egl_surface;
  uint32_t format;
  GArray *modifiers;

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       onscreen_native->render_gpu);
  render_device_gbm = META_RENDER_DEVICE_GBM (renderer_gpu_data->render_device);
  gbm_device = meta_render_device_gbm_get_gbm_device (render_device_gbm);

  format = get_gbm_format_from_egl (egl,
                                    cogl_renderer_egl->edpy,
                                    cogl_display_egl->egl_config);

  if (meta_renderer_native_use_modifiers (renderer_native))
    modifiers = get_supported_modifiers (onscreen, format);
  else
    modifiers = NULL;

  if (modifiers)
    {
      new_gbm_surface =
        gbm_surface_create_with_modifiers (gbm_device,
                                           width, height, format,
                                           (uint64_t *) modifiers->data,
                                           modifiers->len);
      g_array_free (modifiers, TRUE);
    }

  if (!new_gbm_surface)
    {
      uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;

      if (should_surface_be_sharable (onscreen))
        flags |= GBM_BO_USE_LINEAR;

      new_gbm_surface = gbm_surface_create (gbm_device,
                                            width, height,
                                            format,
                                            flags);
    }

  if (!new_gbm_surface)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                   "Failed to allocate surface");
      return FALSE;
    }

  egl_native_window = (EGLNativeWindowType) new_gbm_surface;
  new_egl_surface =
    meta_egl_create_window_surface (egl,
                                    cogl_renderer_egl->edpy,
                                    cogl_display_egl->egl_config,
                                    egl_native_window,
                                    NULL,
                                    error);
  if (new_egl_surface == EGL_NO_SURFACE)
    {
      gbm_surface_destroy (new_gbm_surface);
      return FALSE;
    }

  *gbm_surface = new_gbm_surface;
  *egl_surface = new_egl_surface;

  return TRUE;
}

#ifdef HAVE_EGL_DEVICE
static gboolean
create_surfaces_egl_device (CoglOnscreen  *onscreen,
                            int            width,
                            int            height,
                            EGLStreamKHR  *out_egl_stream,
                            EGLSurface    *out_egl_surface,
                            GError       **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRenderDevice *render_device;
  MetaEgl *egl =
    meta_renderer_native_get_egl (renderer_gpu_data->renderer_native);
  EGLDisplay egl_display;
  EGLConfig egl_config;
  EGLStreamKHR egl_stream;
  EGLSurface egl_surface;
  EGLint num_layers;
  EGLOutputLayerEXT output_layer;
  EGLAttrib output_attribs[3];
  EGLint stream_attribs[] = {
    EGL_STREAM_FIFO_LENGTH_KHR, 0,
    EGL_CONSUMER_AUTO_ACQUIRE_EXT, EGL_FALSE,
    EGL_NONE
  };
  EGLint stream_producer_attribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_NONE
  };

  render_device = renderer_gpu_data->render_device;
  egl_display = meta_render_device_get_egl_display (render_device);
  egl_stream = meta_egl_create_stream (egl, egl_display, stream_attribs, error);
  if (egl_stream == EGL_NO_STREAM_KHR)
    return FALSE;

  output_attribs[0] = EGL_DRM_CRTC_EXT;
  output_attribs[1] = meta_crtc_get_id (onscreen_native->crtc);
  output_attribs[2] = EGL_NONE;

  if (!meta_egl_get_output_layers (egl, egl_display,
                                   output_attribs,
                                   &output_layer, 1, &num_layers,
                                   error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  if (num_layers < 1)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to find output layers.");
      return FALSE;
    }

  if (!meta_egl_stream_consumer_output (egl, egl_display,
                                        egl_stream, output_layer,
                                        error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  egl_config = cogl_display_egl->egl_config;
  egl_surface = meta_egl_create_stream_producer_surface (egl,
                                                         egl_display,
                                                         egl_config,
                                                         egl_stream,
                                                         stream_producer_attribs,
                                                         error);
  if (egl_surface == EGL_NO_SURFACE)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  *out_egl_stream = egl_stream;
  *out_egl_surface = egl_surface;

  return TRUE;
}
#endif /* HAVE_EGL_DEVICE */

void
meta_onscreen_native_set_view (CoglOnscreen     *onscreen,
                               MetaRendererView *view)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  onscreen_native->view = view;
}

static gboolean
meta_onscreen_native_allocate (CoglFramebuffer  *framebuffer,
                               GError          **error)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (onscreen);
  MetaRendererNativeGpuData *renderer_gpu_data;
  struct gbm_surface *gbm_surface;
  EGLSurface egl_surface;
  int width;
  int height;
#ifdef HAVE_EGL_DEVICE
  MetaRenderDevice *render_device;
  MetaDrmBuffer *dumb_buffer;
  EGLStreamKHR egl_stream;
#endif
  CoglFramebufferClass *parent_class;

  if (META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc)) !=
      onscreen_native->render_gpu)
    {
      if (!init_secondary_gpu_state (onscreen_native->renderer_native,
                                     onscreen, error))
        return FALSE;
    }

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (onscreen_native->renderer_native,
                                       onscreen_native->render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (!create_surfaces_gbm (onscreen,
                                width, height,
                                &gbm_surface,
                                &egl_surface,
                                error))
        return FALSE;

      onscreen_native->gbm.surface = gbm_surface;
      cogl_onscreen_egl_set_egl_surface (onscreen_egl, egl_surface);
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      render_device = renderer_gpu_data->render_device;
      dumb_buffer = meta_render_device_allocate_dumb_buf (render_device,
                                                          width, height,
                                                          DRM_FORMAT_XRGB8888,
                                                          error);
      if (!dumb_buffer)
        return FALSE;

      onscreen_native->egl.dumb_fb = META_DRM_BUFFER_DUMB (dumb_buffer);

      if (!create_surfaces_egl_device (onscreen,
                                       width, height,
                                       &egl_stream,
                                       &egl_surface,
                                       error))
        return FALSE;

      onscreen_native->egl.stream = egl_stream;
      cogl_onscreen_egl_set_egl_surface (onscreen_egl, egl_surface);
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  parent_class = COGL_FRAMEBUFFER_CLASS (meta_onscreen_native_parent_class);
  return parent_class->allocate (framebuffer, error);
}

static gboolean
init_secondary_gpu_state_gpu_copy_mode (MetaRendererNative         *renderer_native,
                                        CoglOnscreen               *onscreen,
                                        MetaRendererNativeGpuData  *renderer_gpu_data,
                                        GError                    **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaRenderDevice *render_device;
  MetaRenderDeviceGbm *render_device_gbm;
  struct gbm_device *gbm_device;
  EGLDisplay egl_display;
  int width, height;
  EGLNativeWindowType egl_native_window;
  struct gbm_surface *gbm_surface;
  EGLSurface egl_surface;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaGpuKms *gpu_kms;
  uint32_t format;

  render_device = renderer_gpu_data->render_device;
  egl_display = meta_render_device_get_egl_display (render_device);
  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);
  format = get_gbm_format_from_egl (egl,
                                    egl_display,
                                    renderer_gpu_data->secondary.egl_config);

  render_device_gbm = META_RENDER_DEVICE_GBM (render_device);
  gbm_device = meta_render_device_gbm_get_gbm_device (render_device_gbm);
  gbm_surface = gbm_surface_create (gbm_device,
                                    width, height,
                                    format,
                                    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  if (!gbm_surface)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create gbm_surface: %s", strerror (errno));
      return FALSE;
    }

  egl_native_window = (EGLNativeWindowType) gbm_surface;
  egl_surface =
    meta_egl_create_window_surface (egl,
                                    egl_display,
                                    renderer_gpu_data->secondary.egl_config,
                                    egl_native_window,
                                    NULL,
                                    error);
  if (egl_surface == EGL_NO_SURFACE)
    {
      gbm_surface_destroy (gbm_surface);
      return FALSE;
    }

  secondary_gpu_state = g_new0 (MetaOnscreenNativeSecondaryGpuState, 1);

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc));
  secondary_gpu_state->gpu_kms = gpu_kms;
  secondary_gpu_state->renderer_gpu_data = renderer_gpu_data;
  secondary_gpu_state->gbm.surface = gbm_surface;
  secondary_gpu_state->egl_surface = egl_surface;

  onscreen_native->secondary_gpu_state = secondary_gpu_state;

  return TRUE;
}

static uint32_t
pick_secondary_gpu_framebuffer_format_for_cpu (CoglOnscreen *onscreen)
{
  /*
   * cogl_framebuffer_read_pixels_into_bitmap () supported formats in
   * preference order. Ideally these should depend on the render buffer
   * format copy_shared_framebuffer_cpu () will be reading from but
   * alpha channel ignored.
   */
  static const uint32_t preferred_formats[] =
    {
      /*
       * DRM_FORMAT_XBGR8888 a.k.a GL_RGBA, GL_UNSIGNED_BYTE on
       * little-endian is possibly the most optimized glReadPixels
       * output format. glReadPixels cannot avoid manufacturing an alpha
       * channel if the render buffer does not have one and converting
       * to ABGR8888 may be more optimized than ARGB8888.
       */
      DRM_FORMAT_XBGR8888,
      /* The rest are other fairly commonly used formats in OpenGL. */
      DRM_FORMAT_XRGB8888,
    };
  g_autoptr (GArray) formats = NULL;
  size_t k;
  unsigned int i;
  uint32_t drm_format;

  formats = get_supported_kms_formats (onscreen);

  /* Check if any of our preferred formats are supported. */
  for (k = 0; k < G_N_ELEMENTS (preferred_formats); k++)
    {
      g_assert (meta_cogl_pixel_format_from_drm_format (preferred_formats[k],
                                                        NULL,
                                                        NULL));

      for (i = 0; i < formats->len; i++)
        {
          drm_format = g_array_index (formats, uint32_t, i);

          if (drm_format == preferred_formats[k])
            return drm_format;
        }
    }

  /*
   * Otherwise just pick an arbitrary format we recognize. The formats
   * list is not in any specific order and we don't know any better
   * either.
   */
  for (i = 0; i < formats->len; i++)
    {
      drm_format = g_array_index (formats, uint32_t, i);

      if (meta_cogl_pixel_format_from_drm_format (drm_format, NULL, NULL))
        return drm_format;
    }

  return DRM_FORMAT_INVALID;
}

static void
dumb_toggle_notify (gpointer  data,
                    GObject  *object,
                    gboolean  is_last_ref)
{
  MetaDrmBuffer **source_fb = data;

  g_return_if_fail (source_fb != NULL);
  if (is_last_ref && *source_fb)
    {
      g_return_if_fail (META_IS_DRM_BUFFER (*source_fb));
      g_clear_object (source_fb);
    }
}

static gboolean
init_secondary_gpu_state_cpu_copy_mode (MetaRendererNative         *renderer_native,
                                        CoglOnscreen               *onscreen,
                                        MetaRendererNativeGpuData  *renderer_gpu_data,
                                        GError                    **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaRenderDevice *render_device;
  MetaGpuKms *gpu_kms;
  int width, height;
  unsigned int i;
  uint32_t drm_format;
  MetaDrmFormatBuf tmp;

  drm_format = pick_secondary_gpu_framebuffer_format_for_cpu (onscreen);
  if (drm_format == DRM_FORMAT_INVALID)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not find a suitable pixel format in CPU copy mode");
      return FALSE;
    }

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc));
  render_device = renderer_gpu_data->render_device;
  meta_topic (META_DEBUG_KMS,
              "Secondary GPU %s using DRM format '%s' (0x%x) for a %dx%d output.",
              meta_render_device_get_name (render_device),
              meta_drm_format_to_string (&tmp, drm_format),
              drm_format,
              width, height);

  secondary_gpu_state = g_new0 (MetaOnscreenNativeSecondaryGpuState, 1);
  secondary_gpu_state->renderer_gpu_data = renderer_gpu_data;
  secondary_gpu_state->gpu_kms = gpu_kms;
  secondary_gpu_state->egl_surface = EGL_NO_SURFACE;

  for (i = 0; i < G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs); i++)
    {
      MetaDrmBuffer *dumb_buffer;

      dumb_buffer = meta_render_device_allocate_dumb_buf (render_device,
                                                          width, height,
                                                          drm_format,
                                                          error);
      if (!dumb_buffer)
        {
          secondary_gpu_state_free (secondary_gpu_state);
          return FALSE;
        }

      secondary_gpu_state->cpu.dumb_fbs[i] = META_DRM_BUFFER_DUMB (dumb_buffer);
      g_object_add_toggle_ref (G_OBJECT (dumb_buffer),
                               dumb_toggle_notify,
                               &secondary_gpu_state->cpu.source_fbs[i]);

      /* It was incremented higher than we need by add_toggle_ref */
      g_object_unref (dumb_buffer);
    }

  /*
   * This function initializes everything needed for
   * META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO as well.
   */
  secondary_gpu_state->import_status =
    META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE;

  onscreen_native->secondary_gpu_state = secondary_gpu_state;

  return TRUE;
}

static gboolean
init_secondary_gpu_state (MetaRendererNative  *renderer_native,
                          CoglOnscreen        *onscreen,
                          GError             **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaGpu *gpu = meta_crtc_get_gpu (onscreen_native->crtc);
  MetaRendererNativeGpuData *renderer_gpu_data;
  g_autoptr (GError) local_error = NULL;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         META_GPU_KMS (gpu));

  switch (renderer_gpu_data->secondary.copy_mode)
    {
    case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
      if (init_secondary_gpu_state_gpu_copy_mode (renderer_native,
                                                  onscreen,
                                                  renderer_gpu_data,
                                                  &local_error))
        return TRUE;

      g_warning ("Secondary GPU initialization failed (%s). "
                 "Falling back to GPU-less mode instead, so the "
                 "secondary monitor may be slow to update.",
                 local_error->message);

      renderer_gpu_data->secondary.copy_mode =
        META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO;

      G_GNUC_FALLTHROUGH;
    case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
      /*
       * Initialize also the primary copy mode, so that if zero-copy
       * path fails, which is quite likely, we can simply continue
       * with the primary copy path on the very first frame.
       */
      G_GNUC_FALLTHROUGH;
    case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
      if (!init_secondary_gpu_state_cpu_copy_mode (renderer_native,
                                                   onscreen,
                                                   renderer_gpu_data,
                                                   error))
        return FALSE;
      break;
    }

  return TRUE;
}

MetaOnscreenNative *
meta_onscreen_native_new (MetaRendererNative *renderer_native,
                          MetaGpuKms         *render_gpu,
                          MetaOutput         *output,
                          MetaCrtc           *crtc,
                          CoglContext        *cogl_context,
                          int                 width,
                          int                 height)
{
  MetaOnscreenNative *onscreen_native;
  CoglFramebufferDriverConfig driver_config;

  driver_config = (CoglFramebufferDriverConfig) {
    .type = COGL_FRAMEBUFFER_DRIVER_TYPE_BACK,
  };
  onscreen_native = g_object_new (META_TYPE_ONSCREEN_NATIVE,
                                  "context", cogl_context,
                                  "driver-config", &driver_config,
                                  "width", width,
                                  "height", height,
                                  NULL);

  onscreen_native->renderer_native = renderer_native;
  onscreen_native->render_gpu = render_gpu;
  onscreen_native->output = output;
  onscreen_native->crtc = g_object_ref (crtc);

  return onscreen_native;
}

static void
meta_onscreen_native_dispose (GObject *object)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       onscreen_native->render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      g_clear_object (&onscreen_native->gbm.next_fb);
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      g_clear_object (&onscreen_native->egl.dumb_fb);

      if (onscreen_native->egl.stream != EGL_NO_STREAM_KHR)
        {
          MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
          CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
          CoglRenderer *cogl_renderer = cogl_context->display->renderer;
          CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

          meta_egl_destroy_stream (egl,
                                   cogl_renderer_egl->edpy,
                                   onscreen_native->egl.stream,
                                   NULL);
          onscreen_native->egl.stream = EGL_NO_STREAM_KHR;
        }
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  G_OBJECT_CLASS (meta_onscreen_native_parent_class)->dispose (object);

  g_clear_object (&onscreen_native->crtc);
  g_clear_pointer (&onscreen_native->gbm.surface, gbm_surface_destroy);
  g_clear_pointer (&onscreen_native->secondary_gpu_state,
                   secondary_gpu_state_free);
  g_clear_pointer (&onscreen_native->next_post.rectangles, g_free);
  onscreen_native->next_post.n_rectangles = 0;
}

static void
meta_onscreen_native_init (MetaOnscreenNative *onscreen_native)
{
}

static void
meta_onscreen_native_class_init (MetaOnscreenNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglFramebufferClass *framebuffer_class = COGL_FRAMEBUFFER_CLASS (klass);
  CoglOnscreenClass *onscreen_class = COGL_ONSCREEN_CLASS (klass);

  object_class->dispose = meta_onscreen_native_dispose;

  framebuffer_class->allocate = meta_onscreen_native_allocate;

  onscreen_class->swap_buffers_with_damage =
    meta_onscreen_native_swap_buffers_with_damage;
  onscreen_class->direct_scanout = meta_onscreen_native_direct_scanout;
}

MetaCrtc *
meta_onscreen_native_get_crtc (MetaOnscreenNative *onscreen_native)
{
  return onscreen_native->crtc;
}
