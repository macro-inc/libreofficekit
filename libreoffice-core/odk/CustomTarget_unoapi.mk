# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CustomTarget_CustomTarget,odk/unoapi))

$(call gb_CustomTarget_get_target,odk/unoapi) : \
	$(WORKDIR)/UnoBindings.done

odk_uno_api := offapi udkapi cppuhelper scaddins 
$(eval $(call gb_LinkTarget_use_internal_comprehensive_api,odk/unoapi,$(odk_uno_api)))

$(WORKDIR)/UnoBindings.done : $(foreach rdb,$(odk_uno_api),$(call gb_UnoApiTarget_get_target,$(rdb))) \
		| $(call gb_Package_get_target,odk_headers) \
		$(call gb_Package_get_target,odk_headers_generated)
	$(call gb_Output_announce,$^,$(true),HPC,3)
	$(call gb_Trace_StartRange,$^,HPC)
	$(gb_UnoApiHeadersTarget_CPPUMAKERCOMMAND) \
		-Gc -C -O$(INSTDIR)/$(SDKDIRNAME)/include $^ \
		 && \
	touch $@
	$(call gb_Trace_EndRange,$*,HPC)

# vim: set noet sw=4 ts=4:
