# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Executable_Executable,unoidl-v8))

$(eval $(call gb_Executable_add_exception_objects,unoidl-v8, \
    unoidl/source/v8/writer-map-entities \
    unoidl/source/v8/writer \
    unoidl/source/v8/writer-v8 \
    unoidl/source/v8/writer-v8-internal \
    unoidl/source/v8/writer-typescript \
    unoidl/source/unoidl-v8 \
))

$(eval $(call gb_Executable_use_libraries,unoidl-v8, \
    unoidl \
    $(if $(filter TRUE,$(DISABLE_DYNLOADING)),reg) \
    $(if $(filter TRUE,$(DISABLE_DYNLOADING)),store) \
    salhelper \
    sal \
))

ifeq ($(DISABLE_DYNLOADING),TRUE)
$(eval $(call gb_Executable_use_externals,unoidl-v8,\
    dtoa \
    zlib \
))
endif

# vim: set noet sw=4 ts=4:
