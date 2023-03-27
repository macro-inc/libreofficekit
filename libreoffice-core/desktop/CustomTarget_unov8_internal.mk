# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CustomTarget_CustomTarget,desktop/unov8_internal))

odk_uno_api := offapi udkapi
$(eval $(call gb_CustomTarget_use_internal_comprehensive_api,desktop/unov8_internal,$(odk_uno_api)))

$(call gb_CustomTarget_get_target,desktop/unov8_internal) : $(foreach rdb,$(odk_uno_api),$(call gb_UnoApiTarget_get_target,$(rdb))) $(call gb_Executable_get_runtime_dependencies,unoidl-v8) $(call gb_CustomTarget_get_target,desktop/unov8_internal_entities)
	$(call gb_Output_announce,$^,$(true),V8-I,3)
	$(call gb_Trace_StartRange,$^,V8-I)
	$(if $(gb_UnoApiTarget_UNOBINDING_ENTITIES),RESPONSEFILE=$(call gb_var2file,$(shell $(gb_MKTEMP)),500,$(gb_UnoApiTarget_UNOBINDING_ENTITIES))) \
	&& $(gb_UnoApiTarget_UNOIDLV8COMMAND) \
		internal \
		$(foreach rdb,$(odk_uno_api),$(SRCDIR)/$(rdb)) \
		$(if $(gb_UnoApiTarget_UNOBINDING_ENTITIES),@$${RESPONSEFILE}) $(call gb_CustomTarget_get_workdir,desktop/unov8_internal) \
	&& \
	touch $@
	$(call gb_Trace_EndRange,$*,V8-I)

$(call gb_CustomTarget_get_clean_target,desktop/unov8_internal):
	$(call gb_Output_announce,$(subst $(WORKDIR)/Clean/,,$@),$(false),ENV,2)
	rm -rf $(call gb_CustomTarget_get_workdir,desktop/unov8_internal)

# vim: set noet sw=4 ts=4:
