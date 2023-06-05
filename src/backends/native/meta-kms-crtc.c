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

#include "config.h"

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-crtc-private.h"

#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-update-private.h"

typedef struct _MetaKmsCrtcPropTable
{
  MetaKmsProp props[META_KMS_CRTC_N_PROPS];
} MetaKmsCrtcPropTable;

typedef struct
{
  MetaDrmBuffer *front, *back;
  gboolean back_is_set;
} PlaneState;

struct _MetaKmsCrtc
{
  GObject parent;

  MetaKmsDevice *device;

  uint32_t id;
  int idx;

  MetaKmsCrtcState current_state;

  MetaKmsCrtcPropTable prop_table;

  GHashTable *plane_states;
};

G_DEFINE_TYPE (MetaKmsCrtc, meta_kms_crtc, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_crtc_get_device (MetaKmsCrtc *crtc)
{
  return crtc->device;
}

const MetaKmsCrtcState *
meta_kms_crtc_get_current_state (MetaKmsCrtc *crtc)
{
  return &crtc->current_state;
}

uint32_t
meta_kms_crtc_get_id (MetaKmsCrtc *crtc)
{
  return crtc->id;
}

int
meta_kms_crtc_get_idx (MetaKmsCrtc *crtc)
{
  return crtc->idx;
}

uint32_t
meta_kms_crtc_get_prop_id (MetaKmsCrtc     *crtc,
                           MetaKmsCrtcProp  prop)
{
  return crtc->prop_table.props[prop].prop_id;
}

const char *
meta_kms_crtc_get_prop_name (MetaKmsCrtc     *crtc,
                             MetaKmsCrtcProp  prop)
{
  return crtc->prop_table.props[prop].name;
}

uint64_t
meta_kms_crtc_get_prop_drm_value (MetaKmsCrtc     *crtc,
                                  MetaKmsCrtcProp  property,
                                  uint64_t         value)
{
  MetaKmsProp *prop = &crtc->prop_table.props[property];
  return meta_kms_prop_convert_value (prop, value);
}

gboolean
meta_kms_crtc_is_active (MetaKmsCrtc *crtc)
{
  return crtc->current_state.is_active;
}


gboolean
meta_kms_crtc_has_gamma (MetaKmsCrtc *crtc)
{
  return crtc->current_state.gamma.size > 0;
}

static void
read_gamma_state (MetaKmsCrtc       *crtc,
                  MetaKmsCrtcState  *crtc_state,
                  MetaKmsImplDevice *impl_device,
                  drmModeCrtc       *drm_crtc)
{
  g_assert (!crtc_state->gamma.red &&
            !crtc_state->gamma.green &&
            !crtc_state->gamma.blue);

  crtc_state->gamma.size = drm_crtc->gamma_size;
  crtc_state->gamma.red = g_new0 (uint16_t, drm_crtc->gamma_size);
  crtc_state->gamma.green = g_new0 (uint16_t, drm_crtc->gamma_size);
  crtc_state->gamma.blue = g_new0 (uint16_t, drm_crtc->gamma_size);

  drmModeCrtcGetGamma (meta_kms_impl_device_get_fd (impl_device),
                       crtc->id,
                       crtc_state->gamma.size,
                       crtc_state->gamma.red,
                       crtc_state->gamma.green,
                       crtc_state->gamma.blue);
}

static MetaKmsResourceChanges
meta_kms_crtc_state_changes (MetaKmsCrtcState *state,
                             MetaKmsCrtcState *other_state)
{
  if (state->is_active != other_state->is_active)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (!meta_rectangle_equal (&state->rect, &other_state->rect))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->is_drm_mode_valid != other_state->is_drm_mode_valid)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (!meta_drm_mode_equal (&state->drm_mode, &other_state->drm_mode))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->gamma.size != other_state->gamma.size)
    return META_KMS_RESOURCE_CHANGE_GAMMA;

  if (memcmp (state->gamma.blue, other_state->gamma.blue,
              state->gamma.size * sizeof (uint16_t)) != 0)
    return META_KMS_RESOURCE_CHANGE_GAMMA;

  if (memcmp (state->gamma.green, other_state->gamma.green,
              state->gamma.size * sizeof (uint16_t)) != 0)
    return META_KMS_RESOURCE_CHANGE_GAMMA;

  if (memcmp (state->gamma.red, other_state->gamma.red,
              state->gamma.size * sizeof (uint16_t)) != 0)
    return META_KMS_RESOURCE_CHANGE_GAMMA;

  return META_KMS_RESOURCE_CHANGE_NONE;
}

static void
clear_gamma_state (MetaKmsCrtcState *crtc_state)
{
  crtc_state->gamma.size = 0;
  g_clear_pointer (&crtc_state->gamma.red, g_free);
  g_clear_pointer (&crtc_state->gamma.green, g_free);
  g_clear_pointer (&crtc_state->gamma.blue, g_free);
}

static MetaKmsResourceChanges
meta_kms_crtc_read_state (MetaKmsCrtc             *crtc,
                          MetaKmsImplDevice       *impl_device,
                          drmModeCrtc             *drm_crtc,
                          drmModeObjectProperties *drm_props)
{
  MetaKmsCrtcState crtc_state = {0};
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;
  MetaKmsProp *active_prop;

  meta_kms_impl_device_update_prop_table (impl_device,
                                          drm_props->props,
                                          drm_props->prop_values,
                                          drm_props->count_props,
                                          crtc->prop_table.props,
                                          META_KMS_CRTC_N_PROPS);

  crtc_state.rect = (MetaRectangle) {
    .x = drm_crtc->x,
    .y = drm_crtc->y,
    .width = drm_crtc->width,
    .height = drm_crtc->height,
  };

  crtc_state.is_drm_mode_valid = drm_crtc->mode_valid;
  crtc_state.drm_mode = drm_crtc->mode;

  active_prop = &crtc->prop_table.props[META_KMS_CRTC_PROP_ACTIVE];

  if (active_prop->prop_id)
    crtc_state.is_active = !!active_prop->value;
  else
    crtc_state.is_active = drm_crtc->mode_valid;

  read_gamma_state (crtc, &crtc_state, impl_device, drm_crtc);

  if (!crtc_state.is_active)
    {
      if (crtc->current_state.is_active)
        changes |= META_KMS_RESOURCE_CHANGE_FULL;
    }
  else
    {
      changes = meta_kms_crtc_state_changes (&crtc->current_state, &crtc_state);
    }

  clear_gamma_state (&crtc->current_state);
  crtc->current_state = crtc_state;

  meta_topic (META_DEBUG_KMS,
              "Read CRTC %u state: active: %d, mode: %s, changed: %s",
              crtc->id, crtc->current_state.is_active,
              crtc->current_state.is_drm_mode_valid
                ? crtc->current_state.drm_mode.name
                : "(nil)",
              changes == META_KMS_RESOURCE_CHANGE_NONE
                ? "no"
                : "yes");

  return changes;
}

MetaKmsResourceChanges
meta_kms_crtc_update_state (MetaKmsCrtc *crtc)
{
  MetaKmsImplDevice *impl_device;
  MetaKmsResourceChanges changes;
  int fd;
  drmModeCrtc *drm_crtc;
  drmModeObjectProperties *drm_props;

  impl_device = meta_kms_device_get_impl_device (crtc->device);
  fd = meta_kms_impl_device_get_fd (impl_device);

  drm_crtc = drmModeGetCrtc (fd, crtc->id);
  drm_props = drmModeObjectGetProperties (fd, crtc->id, DRM_MODE_OBJECT_CRTC);

  if (!drm_crtc || !drm_props)
    {
      crtc->current_state.is_active = FALSE;
      crtc->current_state.rect = (MetaRectangle) { };
      crtc->current_state.is_drm_mode_valid = FALSE;
      changes = META_KMS_RESOURCE_CHANGE_FULL;
      goto out;
    }

  changes = meta_kms_crtc_read_state (crtc, impl_device, drm_crtc, drm_props);

out:
  g_clear_pointer (&drm_props, drmModeFreeObjectProperties);
  g_clear_pointer (&drm_crtc, drmModeFreeCrtc);

  return changes;
}

void
meta_kms_crtc_disable (MetaKmsCrtc *crtc)
{
  crtc->current_state.is_active = FALSE;
  crtc->current_state.rect = (MetaRectangle) { 0 };
  crtc->current_state.is_drm_mode_valid = FALSE;
  crtc->current_state.drm_mode = (drmModeModeInfo) { 0 };
}

void
meta_kms_crtc_predict_state (MetaKmsCrtc   *crtc,
                             MetaKmsUpdate *update)
{
  GList *mode_sets;
  GList *crtc_gammas;
  GList *l;

  mode_sets = meta_kms_update_get_mode_sets (update);
  for (l = mode_sets; l; l = l->next)
    {
      MetaKmsModeSet *mode_set = l->data;

      if (mode_set->crtc != crtc)
        continue;

      if (mode_set->mode)
        {
          MetaKmsPlaneAssignment *plane_assignment;
          const drmModeModeInfo *drm_mode;

          plane_assignment =
            meta_kms_update_get_primary_plane_assignment (update, crtc);
          drm_mode = meta_kms_mode_get_drm_mode (mode_set->mode);

          crtc->current_state.is_active = TRUE;
          crtc->current_state.rect =
            meta_fixed_16_rectangle_to_rectangle (plane_assignment->src_rect);
          crtc->current_state.is_drm_mode_valid = TRUE;
          crtc->current_state.drm_mode = *drm_mode;
        }
      else
        {
          crtc->current_state.is_active = FALSE;
          crtc->current_state.rect = (MetaRectangle) { 0 };
          crtc->current_state.is_drm_mode_valid = FALSE;
          crtc->current_state.drm_mode = (drmModeModeInfo) { 0 };
        }

      break;
    }

  crtc_gammas = meta_kms_update_get_crtc_gammas (update);
  for (l = crtc_gammas; l; l = l->next)
    {
      MetaKmsCrtcGamma *gamma = l->data;

      if (gamma->crtc != crtc)
        continue;

      clear_gamma_state (&crtc->current_state);
      crtc->current_state.gamma.size = gamma->size;
      crtc->current_state.gamma.red =
        g_memdup2 (gamma->red, gamma->size * sizeof (uint16_t));
      crtc->current_state.gamma.green =
        g_memdup2 (gamma->green, gamma->size * sizeof (uint16_t));
      crtc->current_state.gamma.blue =
        g_memdup2 (gamma->blue, gamma->size * sizeof (uint16_t));

      break;
    }
}

static void
init_properties (MetaKmsCrtc       *crtc,
                 MetaKmsImplDevice *impl_device,
                 drmModeCrtc       *drm_crtc)
{
  MetaKmsCrtcPropTable *prop_table = &crtc->prop_table;

  *prop_table = (MetaKmsCrtcPropTable) {
    .props = {
      [META_KMS_CRTC_PROP_MODE_ID] =
        {
          .name = "MODE_ID",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_CRTC_PROP_ACTIVE] =
        {
          .name = "ACTIVE",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CRTC_PROP_GAMMA_LUT] =
        {
          .name = "GAMMA_LUT",
          .type = DRM_MODE_PROP_BLOB,
        },
    }
  };
}

MetaKmsCrtc *
meta_kms_crtc_new (MetaKmsImplDevice  *impl_device,
                   drmModeCrtc        *drm_crtc,
                   int                 idx,
                   GError            **error)
{
  int fd;
  drmModeObjectProperties *drm_props;
  MetaKmsCrtc *crtc;

  fd = meta_kms_impl_device_get_fd (impl_device);
  drm_props = drmModeObjectGetProperties (fd, drm_crtc->crtc_id,
                                          DRM_MODE_OBJECT_CRTC);
  if (!drm_props)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "drmModeObjectGetProperties: %s", g_strerror (errno));
      return NULL;
    }

  crtc = g_object_new (META_TYPE_KMS_CRTC, NULL);
  crtc->device = meta_kms_impl_device_get_device (impl_device);
  crtc->id = drm_crtc->crtc_id;
  crtc->idx = idx;

  init_properties (crtc, impl_device, drm_crtc);

  meta_kms_crtc_read_state (crtc, impl_device, drm_crtc, drm_props);

  drmModeFreeObjectProperties (drm_props);

  return crtc;
}

void
meta_kms_crtc_remember_plane_buffer (MetaKmsCrtc   *crtc,
                                     uint32_t       plane_id,
                                     MetaDrmBuffer *buffer)
{
  gpointer key = GUINT_TO_POINTER (plane_id);
  PlaneState *plane_state;

  plane_state = g_hash_table_lookup (crtc->plane_states, key);
  if (plane_state == NULL)
    {
      plane_state = g_new0 (PlaneState, 1);
      g_hash_table_insert (crtc->plane_states, key, plane_state);
    }

  plane_state->back_is_set = TRUE;  /* note buffer may be NULL */
  g_set_object (&plane_state->back, buffer);
}

static void
swap_plane_buffers (gpointer key,
                    gpointer value,
                    gpointer user_data)
{
  PlaneState *plane_state = value;

  if (plane_state->back_is_set)
    {
      g_set_object (&plane_state->front, plane_state->back);
      g_clear_object (&plane_state->back);
      plane_state->back_is_set = FALSE;
    }
}

void
meta_kms_crtc_on_scanout_started (MetaKmsCrtc *crtc)
{
  g_hash_table_foreach (crtc->plane_states, swap_plane_buffers, NULL);
}

void
meta_kms_crtc_release_buffers (MetaKmsCrtc *crtc)
{
  g_hash_table_remove_all (crtc->plane_states);
}

static void
meta_kms_crtc_dispose (GObject *object)
{
  MetaKmsCrtc *crtc = META_KMS_CRTC (object);

  meta_kms_crtc_release_buffers (crtc);

  G_OBJECT_CLASS (meta_kms_crtc_parent_class)->dispose (object);
}

static void
meta_kms_crtc_finalize (GObject *object)
{
  MetaKmsCrtc *crtc = META_KMS_CRTC (object);

  clear_gamma_state (&crtc->current_state);
  g_hash_table_unref (crtc->plane_states);

  G_OBJECT_CLASS (meta_kms_crtc_parent_class)->finalize (object);
}

static void
destroy_plane_state (gpointer data)
{
  PlaneState *plane_state = data;

  g_clear_object (&plane_state->front);
  g_clear_object (&plane_state->back);
  g_free (plane_state);
}

static void
meta_kms_crtc_init (MetaKmsCrtc *crtc)
{
  crtc->current_state.gamma.size = 0;
  crtc->plane_states = g_hash_table_new_full (NULL,
                                              NULL,
                                              NULL,
                                              destroy_plane_state);
}

static void
meta_kms_crtc_class_init (MetaKmsCrtcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_kms_crtc_dispose;
  object_class->finalize = meta_kms_crtc_finalize;
}
