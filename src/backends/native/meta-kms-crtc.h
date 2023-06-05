/*
 * Copyright (C) 2019 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_KMS_CRTC_H
#define META_KMS_CRTC_H

#include <glib-object.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-drm-buffer.h"
#include "core/util-private.h"
#include "meta/boxes.h"

typedef struct _MetaKmsCrtcState
{
  gboolean is_active;

  MetaRectangle rect;
  gboolean is_drm_mode_valid;
  drmModeModeInfo drm_mode;

  struct {
    uint16_t *red;
    uint16_t *green;
    uint16_t *blue;

    int size;
  } gamma;
} MetaKmsCrtcState;

typedef struct _MetaKmsCrtcGamma
{
  MetaKmsCrtc *crtc;
  int size;
  uint16_t *red;
  uint16_t *green;
  uint16_t *blue;
} MetaKmsCrtcGamma;

#define META_TYPE_KMS_CRTC (meta_kms_crtc_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaKmsCrtc, meta_kms_crtc,
                      META, KMS_CRTC,
                      GObject)

META_EXPORT_TEST
MetaKmsDevice * meta_kms_crtc_get_device (MetaKmsCrtc *crtc);

META_EXPORT_TEST
const MetaKmsCrtcState * meta_kms_crtc_get_current_state (MetaKmsCrtc *crtc);

META_EXPORT_TEST
uint32_t meta_kms_crtc_get_id (MetaKmsCrtc *crtc);

int meta_kms_crtc_get_idx (MetaKmsCrtc *crtc);

gboolean meta_kms_crtc_has_gamma (MetaKmsCrtc *crtc);

META_EXPORT_TEST
gboolean meta_kms_crtc_is_active (MetaKmsCrtc *crtc);

void meta_kms_crtc_gamma_free (MetaKmsCrtcGamma *gamma);

MetaKmsCrtcGamma * meta_kms_crtc_gamma_new (MetaKmsCrtc    *crtc,
                                            int             size,
                                            const uint16_t *red,
                                            const uint16_t *green,
                                            const uint16_t *blue);

void meta_kms_crtc_remember_plane_buffer (MetaKmsCrtc   *crtc,
                                          uint32_t       plane_id,
                                          MetaDrmBuffer *buffer);

void meta_kms_crtc_on_scanout_started (MetaKmsCrtc *crtc);

void meta_kms_crtc_release_buffers (MetaKmsCrtc *crtc);

#endif /* META_KMS_CRTC_H */
