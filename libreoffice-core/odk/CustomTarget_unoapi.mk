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
	$(WORKDIR)/uno-bindings-cpp.done \
	$(WORKDIR)/uno-bindings-v8.done

odk_uno_api := offapi udkapi
$(eval $(call gb_LinkTarget_use_internal_comprehensive_api,odk/unoapi,$(odk_uno_api)))

$(WORKDIR)/uno-bindings-cpp.done : $(foreach rdb,$(odk_uno_api),$(call gb_UnoApiTarget_get_target,$(rdb))) \
		| $(call gb_Package_get_target,odk_headers) \
		$(call gb_Package_get_target,odk_headers_generated)
	$(call gb_Output_announce,$^,$(true),HPC,3)
	$(call gb_Trace_StartRange,$^,HPC)
	$(gb_UnoApiHeadersTarget_CPPUMAKERCOMMAND) \
		-Gc -C -O$(INSTDIR)/$(SDKDIRNAME)/include $^ \
		 && \
	touch $@
	$(call gb_Trace_EndRange,$*,HPC)

$(WORKDIR)/uno-bindings-v8.done : $(foreach rdb,$(odk_uno_api),$(call gb_UnoApiTarget_get_target,$(rdb))) $(call gb_Executable_get_runtime_dependencies,unoidl-v8) \
		| $(call gb_Package_get_target,odk_headers) \
		$(call gb_Package_get_target,odk_headers_generated)
	$(call gb_Output_announce,$^,$(true),V8,3)
	$(call gb_Trace_StartRange,$^,V8)
	$(if $(gb_UnoApiTarget_UNOBINDING_ENTITIES),RESPONSEFILE=$(call gb_var2file,$(shell $(gb_MKTEMP)),500,$(gb_UnoApiTarget_UNOBINDING_ENTITIES))) \
	&& $(gb_UnoApiTarget_UNOIDLV8COMMAND) \
		$(foreach rdb,$(odk_uno_api),$(SRCDIR)/$(rdb)) \
		$(if $(gb_UnoApiTarget_UNOBINDING_ENTITIES),@$${RESPONSEFILE}) $(INSTDIR)/$(SDKDIRNAME) \
	&& \
	touch $@
	$(call gb_Trace_EndRange,$*,V8)

$(call gb_CustomTarget_get_clean_target,odk/unoapi):
	$(call gb_Output_announce,$(subst $(WORKDIR)/Clean/,,$@),$(false),ENV,2)
	rm -rf $(INSTDIR)/$(SDKDIRNAME)/include && \
	rm -rf $(INSTDIR)/$(SDKDIRNAME)/typescript && \
	rm -rf $(INSTDIR)/$(SDKDIRNAME)/v8 && \
	rm -f $(WORKDIR)/uno-bindings-cpp.done && \
	rm -f $(WORKDIR)/uno-bindings-v8.done

# vim: set noet sw=4 ts=4:
