# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

include $(SRCDIR)/odk/build-examples_common.mk

$(eval $(call gb_Module_Module,odk))

$(eval $(call gb_Module_add_targets,odk,\
	$(if $(DOXYGEN),\
		CustomTarget_doxygen \
		GeneratedPackage_odk_doxygen \
	) \
	CustomTarget_unoapi \
	Package_odk_headers \
	Package_odk_headers_generated \
))

# vim: set noet sw=4 ts=4:
