
# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Package_Package,unov8_headers_generated,$(WORKDIR)/CustomTarget/desktop/unov8_internal))

$(call gb_Package_get_preparation_target,unov8_headers_generated) :| $(WORKDIR)/CustomTarget/desktop/unov8_internal.done

$(eval $(call gb_Package_set_outdir,unov8_headers_generated,$(INSTDIR)))

$(eval $(call gb_Package_add_file,unov8_headers_generated,$(SDKDIRNAME)/include/unov8.h,unov8.h))

# vim: set noet sw=4 ts=4:
