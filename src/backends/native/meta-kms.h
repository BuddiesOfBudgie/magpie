/*
 * Copyright (C) 2018 Red Hat
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

#ifndef META_KMS_H
#define META_KMS_H

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-kms-types.h"

typedef enum _MetaKmsFlags
{
  META_KMS_FLAG_NONE = 0,
  META_KMS_FLAG_NO_MODE_SETTING = 1 << 0,
} MetaKmsFlags;

#define META_TYPE_KMS (meta_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaKms, meta_kms, META, KMS, GObject)

void meta_kms_discard_pending_updates (MetaKms *kms);

MetaKmsUpdate * meta_kms_ensure_pending_update (MetaKms       *kms,
                                                MetaKmsDevice *device);

MetaKmsUpdate * meta_kms_ensure_pending_update_for_crtc (MetaKms     *kms,
                                                         MetaKmsCrtc *crtc);

MetaKmsUpdate * meta_kms_get_pending_update (MetaKms       *kms,
                                             MetaKmsDevice *device);

MetaKmsUpdate * meta_kms_get_pending_update_for_crtc (MetaKms     *kms,
                                                      MetaKmsCrtc *crtc);

MetaKmsFeedback * meta_kms_post_pending_update_sync (MetaKms           *kms,
                                                     MetaKmsDevice     *device,
                                                     MetaKmsUpdateFlag  flags);

MetaKmsFeedback * meta_kms_post_test_update_sync (MetaKms       *kms,
                                                  MetaKmsUpdate *update);

MetaKmsFeedback * meta_kms_post_pending_update_for_crtc_sync (MetaKms           *kms,
                                                              MetaKmsCrtc       *device,
                                                              MetaKmsUpdateFlag  flags);

void meta_kms_discard_pending_page_flips (MetaKms *kms);

void meta_kms_notify_modes_set (MetaKms *kms);

META_EXPORT_TEST
MetaBackend * meta_kms_get_backend (MetaKms *kms);

META_EXPORT_TEST
GList * meta_kms_get_devices (MetaKms *kms);

void meta_kms_resume (MetaKms *kms);

MetaKmsDevice * meta_kms_create_device (MetaKms            *kms,
                                        const char         *path,
                                        MetaKmsDeviceFlag   flags,
                                        GError            **error);

void meta_kms_prepare_shutdown (MetaKms *kms);

gboolean meta_kms_is_shutting_down (MetaKms *kms);

MetaKms * meta_kms_new (MetaBackend   *backend,
                        MetaKmsFlags   flags,
                        GError       **error);

#endif /* META_KMS_H */
