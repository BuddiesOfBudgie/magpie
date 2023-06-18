/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013-2017 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "backends/meta-crtc.h"
#include "config.h"

#include "backends/x11/meta-gpu-xrandr.h"

#include <string.h>
#include <X11/extensions/dpms.h>
#include <X11/Xatom.h>
#include <X11/Xlibint.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-output.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-crtc-xrandr.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"
#include "backends/x11/meta-output-xrandr.h"

struct _MetaGpuXrandr
{
  MetaGpu parent;

  XRRScreenResources *resources;

  int min_screen_width;
  int min_screen_height;
  int max_screen_width;
  int max_screen_height;
};

G_DEFINE_TYPE (MetaGpuXrandr, meta_gpu_xrandr, META_TYPE_GPU)

XRRScreenResources *
meta_gpu_xrandr_get_resources (MetaGpuXrandr *gpu_xrandr)
{
  return gpu_xrandr->resources;
}

void
meta_gpu_xrandr_get_min_screen_size (MetaGpuXrandr *gpu_xrandr,
                                     int           *min_width,
                                     int           *min_height)
{
  *min_width = gpu_xrandr->min_screen_width;
  *min_height = gpu_xrandr->min_screen_height;
}

void
meta_gpu_xrandr_get_max_screen_size (MetaGpuXrandr *gpu_xrandr,
                                     int           *max_width,
                                     int           *max_height)
{
  *max_width = gpu_xrandr->max_screen_width;
  *max_height = gpu_xrandr->max_screen_height;
}

static int
compare_outputs (const void *one,
                 const void *two)
{
  MetaOutput *o_one = (MetaOutput *) one;
  MetaOutput *o_two = (MetaOutput *) two;
  const MetaOutputInfo *output_info_one = meta_output_get_info (o_one);
  const MetaOutputInfo *output_info_two = meta_output_get_info (o_two);

  return strcmp (output_info_one->name, output_info_two->name);
}

static char *
get_xmode_name (XRRModeInfo *xmode)
{
  int width = xmode->width;
  int height = xmode->height;

  return g_strdup_printf ("%dx%d", width, height);
}

static float
calculate_xrandr_refresh_rate (XRRModeInfo *xmode)
{
  float h_total;
  float v_total;

  h_total = (float) xmode->hTotal;
  v_total = (float) xmode->vTotal;

  if (h_total == 0.0 || v_total == 0.0)
    return 0.0;

  if (xmode->modeFlags & RR_DoubleScan)
    v_total *= 2.0;

  if (xmode->modeFlags & RR_Interlace)
    v_total /= 2.0;

  return xmode->dotClock / (h_total * v_total);
}

static int
get_current_dpi_scale (MetaMonitorManagerXrandr *manager_xrandr,
                       MetaGpuXrandr            *gpu_xrandr)
{
  Atom actual;
  int result, format;
  unsigned long n, left;
  g_autofree unsigned char *data = NULL;
  g_auto(GStrv) resources = NULL;
  Display *dpy;
  int i;

  if (gpu_xrandr->resources->timestamp ==
      meta_monitor_manager_xrandr_get_config_timestamp (manager_xrandr))
    {
      MetaMonitorManager *monitor_manager = META_MONITOR_MANAGER (manager_xrandr);
      MetaBackend *backend = meta_monitor_manager_get_backend (monitor_manager);
      MetaSettings *settings = meta_backend_get_settings (backend);

      return meta_settings_get_ui_scaling_factor (settings);
    }

  dpy = meta_monitor_manager_xrandr_get_xdisplay (manager_xrandr);
  result = XGetWindowProperty (dpy, DefaultRootWindow (dpy),
                               XA_RESOURCE_MANAGER, 0L, 65536, False,
                               XA_STRING, &actual, &format,
                               &n, &left, &data);

  if (result != Success || !data || actual != XA_STRING)
    return 1;

  resources = g_strsplit ((char *) data, "\n", -1);

  for (i = 0; resources && resources[i]; ++i)
    {
      if (g_str_has_prefix (resources[i], "Xft.dpi:"))
        {
          g_auto(GStrv) res = g_strsplit (resources[i], "\t", 2);

          if (res && res[0] && res[1])
            {
              guint64 dpi;
              dpi = g_ascii_strtoull (res[1], NULL, 10);

              if (dpi > 0 && dpi < 96 * 10)
                return MAX (1, roundf ((float) dpi / 96.0f));
            }
        }
    }

  return 1;
}

static gboolean
meta_gpu_xrandr_read_current (MetaGpu  *gpu,
                              GError  **error)
{
  MetaGpuXrandr *gpu_xrandr = META_GPU_XRANDR (gpu);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay =
    meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
  XRRScreenResources *resources;
  RROutput primary_output;
  unsigned int i, j;
  GList *l;
  Screen *screen;
  GList *outputs = NULL;
  GList *modes = NULL;
  GList *crtcs = NULL;
  gboolean has_transform;
  int dpi_scale = 1;

  if (gpu_xrandr->resources)
    XRRFreeScreenResources (gpu_xrandr->resources);
  gpu_xrandr->resources = NULL;

  XRRGetScreenSizeRange (xdisplay, DefaultRootWindow (xdisplay),
                         &gpu_xrandr->min_screen_width,
                         &gpu_xrandr->min_screen_height,
                         &gpu_xrandr->max_screen_width,
                         &gpu_xrandr->max_screen_height);

  screen = ScreenOfDisplay (xdisplay, DefaultScreen (xdisplay));
  /* This is updated because we called XRRUpdateConfiguration. */
  monitor_manager->screen_width = WidthOfScreen (screen);
  monitor_manager->screen_height = HeightOfScreen (screen);

  resources = XRRGetScreenResourcesCurrent (xdisplay,
                                            DefaultRootWindow (xdisplay));
  if (!resources)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to retrieve Xrandr screen resources");
      return FALSE;
    }

  gpu_xrandr->resources = resources;

  outputs = NULL;
  modes = NULL;
  crtcs = NULL;

  for (i = 0; i < (unsigned)resources->nmode; i++)
    {
      XRRModeInfo *xmode = &resources->modes[i];
      g_autofree char *crtc_mode_name = NULL;
      MetaCrtcMode *mode;
      g_autoptr (MetaCrtcModeInfo) crtc_mode_info = NULL;

      crtc_mode_info = meta_crtc_mode_info_new ();
      crtc_mode_info->width = xmode->width;
      crtc_mode_info->height = xmode->height;
      crtc_mode_info->refresh_rate = calculate_xrandr_refresh_rate (xmode);
      crtc_mode_info->flags = xmode->modeFlags;

      crtc_mode_name = get_xmode_name (xmode);
      mode = g_object_new (META_TYPE_CRTC_MODE,
                           "id", (uint64_t) xmode->id,
                           "name", crtc_mode_name,
                           "info", crtc_mode_info,
                           NULL);
      modes = g_list_append (modes, mode);
    }
  meta_gpu_take_modes (gpu, modes);

  has_transform = !!(meta_monitor_manager_get_capabilities (monitor_manager) &
                     META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING);

  if (has_transform &&
      meta_monitor_manager_get_default_layout_mode (monitor_manager) ==
      META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL)
    dpi_scale = get_current_dpi_scale (monitor_manager_xrandr, gpu_xrandr);

  for (i = 0; i < (unsigned)resources->ncrtc; i++)
    {
      XRRCrtcInfo *xrandr_crtc;
      XRRCrtcTransformAttributes *transform_attributes;
      RRCrtc crtc_id;
      MetaCrtcXrandr *crtc_xrandr;

      crtc_id = resources->crtcs[i];
      xrandr_crtc = XRRGetCrtcInfo (xdisplay,
                                    resources, crtc_id);

      if (!has_transform ||
          !XRRGetCrtcTransform (xdisplay, crtc_id, &transform_attributes))
        transform_attributes = NULL;

      crtc_xrandr = meta_crtc_xrandr_new (gpu_xrandr,
                                          xrandr_crtc, crtc_id, resources,
                                          transform_attributes, dpi_scale);
      XFree (transform_attributes);
      XRRFreeCrtcInfo (xrandr_crtc);

      crtcs = g_list_append (crtcs, crtc_xrandr);
    }

  if (has_transform && dpi_scale == 1 &&
      meta_monitor_manager_get_default_layout_mode (monitor_manager) ==
        META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL)
    {
      dpi_scale =
        ceilf (meta_monitor_manager_get_maximum_crtc_scale (monitor_manager));

      if (dpi_scale > 1)
        {
          for (l = crtcs; l; l = l->next)
            {
              MetaCrtc *crtc = l->data;
              const MetaCrtcConfig *crtc_config = meta_crtc_get_config (crtc);

              if (!crtc_config)
                continue;

              meta_crtc_set_config_scale (crtc, crtc_config->scale * dpi_scale);
            }
        }
    }

  meta_gpu_take_crtcs (gpu, crtcs);

  primary_output = XRRGetOutputPrimary (xdisplay,
                                        DefaultRootWindow (xdisplay));

  for (i = 0; i < (unsigned)resources->noutput; i++)
    {
      RROutput output_id;
      XRROutputInfo *xrandr_output;

      output_id = resources->outputs[i];
      xrandr_output = XRRGetOutputInfo (xdisplay,
                                        resources, output_id);
      if (!xrandr_output)
        continue;

      if (xrandr_output->connection != RR_Disconnected)
        {
          MetaOutputXrandr *output_xrandr;

          output_xrandr = meta_output_xrandr_new (gpu_xrandr,
                                                  xrandr_output,
                                                  output_id,
                                                  primary_output);
          if (output_xrandr)
            outputs = g_list_prepend (outputs, output_xrandr);
        }

      XRRFreeOutputInfo (xrandr_output);
    }

  /* Sort the outputs for easier handling in MetaMonitorConfig */
  outputs = g_list_sort (outputs, compare_outputs);

  meta_gpu_take_outputs (gpu, outputs);

  /* Now fix the clones */
  for (l = outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;
      const MetaOutputInfo *output_info = meta_output_get_info (output);
      GList *k;

      for (j = 0; j < output_info->n_possible_clones; j++)
        {
          RROutput clone = GPOINTER_TO_INT (output_info->possible_clones[j]);

          for (k = outputs; k; k = k->next)
            {
              MetaOutput *possible_clone = k->data;

              if (clone == (XID) meta_output_get_id (possible_clone))
                {
                  output_info->possible_clones[j] = possible_clone;
                  break;
                }
            }
        }
    }

  return TRUE;
}

MetaGpuXrandr *
meta_gpu_xrandr_new (MetaBackendX11 *backend_x11)
{
  return g_object_new (META_TYPE_GPU_XRANDR,
                       "backend", backend_x11,
                       NULL);
}

static void
meta_gpu_xrandr_finalize (GObject *object)
{
  MetaGpuXrandr *gpu_xrandr = META_GPU_XRANDR (object);

  g_clear_pointer (&gpu_xrandr->resources,
                   XRRFreeScreenResources);

  G_OBJECT_CLASS (meta_gpu_xrandr_parent_class)->finalize (object);
}

static void
meta_gpu_xrandr_init (MetaGpuXrandr *gpu_xrandr)
{
}

static void
meta_gpu_xrandr_class_init (MetaGpuXrandrClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaGpuClass *gpu_class = META_GPU_CLASS (klass);

  object_class->finalize = meta_gpu_xrandr_finalize;

  gpu_class->read_current = meta_gpu_xrandr_read_current;
}
