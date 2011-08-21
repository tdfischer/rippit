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

#include <glib/gquark.h>
#include <gst/gstinfo.h>

#define RIPPIT_VERSION_MAJOR 0
#define RIPPIT_VERSION_MINOR 0
#define RIPPIT_VERSION_MICRO 1

#define RIPPIT_VERSION \
    ((RIPPIT_VERSION_MAJOR << 8) | \
     (RIPPIT_VERSION_MINOR << 4) | \
     (RIPPIT_VERSION_MICRO))

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

#define RIPPIT_VERSION_STRING \
    STRINGIZE(RIPPIT_VERSION_MAJOR) "." \
    STRINGIZE(RIPPIT_VERSION_MINOR) "." \
    STRINGIZE(RIPPIT_VERSION_MICRO)

GST_DEBUG_CATEGORY_STATIC(rippit);
#define GST_CAT_DEFAULT rippit

#define RIPPIT_ERROR rippit_error_quark ()
#define RIPPIT_ERROR_PARAMS 1

GQuark rippit_error_quark();
