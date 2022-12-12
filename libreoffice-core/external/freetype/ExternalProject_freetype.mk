# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_ExternalProject_ExternalProject,freetype))

$(eval $(call gb_ExternalProject_register_targets,freetype,\
	build \
))

ifeq ($(OS),WNT)
$(call gb_ExternalProject_get_state_target,freetype,build) :
	$(call gb_Trace_StartRange,freetype,EXTERNAL)
	$(call gb_ExternalProject_run,build,\
		'$(DEVENV)' "builds/windows/vc2010/freetype.sln" -upgrade && \
		unset UCRTVersion && \
		sed -i 's/MultiThreaded</MultiThreadedDLL</g' builds/windows/vc2010/freetype.vcxproj && \
		'$(DEVENV)' "builds/windows/vc2010/freetype.sln" -build "Release Static|x64" && \
		mkdir -p "$(call gb_UnpackedTarball_get_dir,freetype/instdir/lib)" && \
		cp -rf "objs/x64/Release Static/freetype.lib" "$(call gb_UnpackedTarball_get_dir,freetype/instdir/lib)" && \
		touch $@ )
	$(call gb_Trace_EndRange,freetype,EXTERNAL)
else # !WINDOWS
$(call gb_ExternalProject_get_state_target,freetype,build) :
	$(call gb_Trace_StartRange,freetype,EXTERNAL)
	$(call gb_ExternalProject_run,build,\
		$(gb_RUN_CONFIGURE) ./configure \
			--disable-shared \
			--without-zlib \
			--without-brotli \
			--without-bzip2 \
			--without-harfbuzz \
			--without-png \
			--prefix=$(call gb_UnpackedTarball_get_dir,freetype/instdir) \
			--build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM) \
			CFLAGS="$(CFLAGS) $(if $(debug),-g) $(gb_VISIBILITY_FLAGS)" \
		&& $(MAKE) install \
		$(if $(filter MACOSX,$(OS)), \
			&& cp $(call gb_UnpackedTarball_get_dir,freetype/instdir/lib)/libfreetype.a $(call gb_UnpackedTarball_get_dir,freetype/instdir/lib)/freetype.a \
			&& cp $(call gb_UnpackedTarball_get_dir,freetype/instdir/lib)/libfreetype.la $(call gb_UnpackedTarball_get_dir,freetype/instdir/lib)/freetype.la \
			) \
		&& touch $@	)
	$(call gb_Trace_EndRange,freetype,EXTERNAL)
endif

# vim: set noet sw=4 ts=4:
