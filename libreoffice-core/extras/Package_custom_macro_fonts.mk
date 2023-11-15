# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# $(eval $(call gb_Package_Package,custom_macro_fonts,$(call gb_CustomTarget_get_workdir,extras/macro_fonts)))

# $(eval $(call gb_Package_use_customtarget,custom_macro_fonts,extras/macro_fonts))

$(eval $(call gb_Package_Package,custom_macro_fonts,$(SRCDIR)/extras/source/macro_fonts))

# this copies stuff into the correct folder?
$(eval $(call gb_Package_add_files_with_dir,custom_macro_fonts,$(LIBO_SHARE_RESOURCE_FOLDER)/macro_fonts,\
	Carlito.ttf \
	Carlito-Bold.ttf \
	Carlito-Italic.ttf \
	Carlito-BoldItalic.ttf \
	Caladea.ttf \
	Caladea-Bold.ttf \
	Caladea-Italic.ttf \
	Caladea-BoldItalic.ttf \
))

# vim: set noet sw=4 ts=4:
