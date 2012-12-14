// rippit - A no-nonsense program to rip audio CDs
//
// Copyright (C) 2011 Trever Fischer <tdfischer@fedoraproject.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include <glib.h>
#include <gst/gstinfo.h>
#include "rippitversion.h"

GST_DEBUG_CATEGORY_STATIC(rippit);
#define GST_CAT_DEFAULT rippit

#define RIPPIT_ERROR rippit_error_quark ()
#define RIPPIT_ERROR_PARAMS 1

GQuark rippit_error_quark();
