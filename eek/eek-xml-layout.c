/*
 * Copyright (C) 2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:eek-xml-layout
 * @short_description: Layout engine which loads layout information from XML
 */

#include "config.h"

#include "eek-keyboard.h"
#include "src/keyboard.h"
#include "src/layout.h"

#include "eek-xml-layout.h"

LevelKeyboard *
eek_xml_layout_real_create_keyboard (const char *keyboard_type,
                                     EekboardContextService *manager,
                                     enum squeek_arrangement_kind t)
{
    struct squeek_layout *layout = squeek_load_layout(keyboard_type, t);
    squeek_layout_place_contents(layout);
    return level_keyboard_new(manager, layout);
}
