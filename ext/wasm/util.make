#
# Common vars and utilities for the SQLite WASM build.
#
# The "b." prefix on some APIs is for "build". It was initially used
# only for build-specific features, but that's no longer the came, but
# the naming convention has stuck.
#

loud ?= 0
ifeq (1,$(loud))
  $(info $(emo.megaphone) Emitting loud build info. Pass loud=0 to disable it.)
  b.cmd@ =
else
  $(info $(emo.mute) Eliding loud build info. Pass loud=1 to enable it.)
  b.cmd@ = @
endif

#
# Emoji for log messages.
#
emo.bug = 🐞
emo.compile = ⏳
emo.disk = 💾
emo.done = 🏆
emo.fire = 🔥
emo.folder = 📁
emo.garbage =🗑
emo.megaphone = 📣
emo.mute = 🔇
emo.stop =🛑
emo.strip =🪚
emo.tool = 🔨
 # 👷🪄🧮🧫🧪🧽🍿⛽🚧🎱

#
# logtag.X value for log context labeling. longtag.OTHERX can be
# assigned to customize it for a given X. This tag is used by the
# b.call.X and b.eval.X for logging.
#
logtag.@ = [$@]
logtag.filter = [🚧 $@]

#
# $(call b.call.mkdir@)
#
# $1 = optional build name
#
b.call.mkdir@ = if [ ! -d $(dir $@) ]; then \
  echo '[$(emo.folder)+] $(if $(1),$(logtag.$(1)),[$(dir $@)])'; \
  mkdir -p $(dir $@) || exit $$?; fi

#
# $(call b.call.cp,@,src,dest)
#
# $1 = build name, $2 = src file(s). $3 = dest dir
b.call.cp = $(call b.call.mkdir@); \
  echo '$(logtag.$(1)) $(emo.disk) $(2) ==> $3'; \
  cp -p $(2) $(3) || exit $$$$?

#
# $(eval $(call b.eval.c-pp,@,src,dest,-Dx=y...))
#
# $1 = build name
# $2 = Input file(s): cat $(2) | $(bin.c-pp) ...
# $3 = Output file: $(bin.c-pp) -o $(3)
# $4 = optional $(bin.c-pp) -D... flags */
#
define b.eval.c-pp
$(3): $$(MAKEFILE_LIST) $$(bin.c-pp) $(2)
	@$$(call b.call.mkdir@); \
	echo '$$(logtag.$(1)) $$(emo.disk) $$(bin.c-pp) $(4)'; \
	cat $(2) | $$(bin.c-pp) -o $(3) $(4) $$(SQLITE.CALL.C-PP.FILTER.global) \
		|| exit $$$$?\n
CLEAN_FILES += $(3)
endef

#
# $(call b.call.strip-emcc-js-cruft)
#
# Our JS code installs bindings of each sqlite3_...() WASM export. The
# generated Emscripten JS file does the same using its own framework,
# but we don't use those results and can speed up lib init, and reduce
# memory cost a bit, by stripping them out. Emscripten-side changes
# can "break" this, causing this to be a no-op, but the worst that can
# happen in that case is that it doesn't actually strip anything,
# leading to slightly larger JS files.
#
# This snippet is intended to be used in makefile targets which
# generate an Emscripten module and where $@ is the module's .js/.mjs
# file.
#
# $1 = an optional log message prefix
b.call.strip-emcc-js-cruft = \
  sed -i -e '/^.*= \(_sqlite3\|_fiddle\)[^=]*=.*createExportWrapper/d' \
  -e '/^var \(_sqlite3\|_fiddle\)[^=]*=.*makeInvalidEarlyAccess/d' $@ || exit; \
  echo '$(1) $(emo.garbage) (Probably) /createExportWrapper()/d and /makeInvalidEarlyAccess()/d'
