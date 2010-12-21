# useful.make, v0.1
# (C) 2010 Elliott Hird
# This code is licensed under the WTFPL, version 2:
#   http://sam.zoy.org/wtfpl/COPYING

.SUFFIXES:
.DEFAULT:
.DELETE_ON_ERROR:

#### Miscellaneous utilities

EMPTY :=
SPACE := $(EMPTY) #
TAB := $(EMPTY)	#
COMMA := ,

cat = $(foreach x,$(1),$(x))

joinwith = $(subst $(SPACE),$(1),$(2))

define define-default-body
	ifeq ($$(origin $(1)), undefined)
		$(1) := $(2)
	endif
endef

define-default = $(eval $(call define-default-body,$(strip $(1)),$(strip $(2))))

#### Useful internal variables

to-clean :=
to-install :=

ifeq ($(MAKECMDGOALS),clean)
	cleaning := 1
else
	cleaning :=
endif

#### Command execution

ifeq ($(findstring n,$(MAKEFLAGS)),n)
	V := 1
endif

ifdef V
	do = $(3)
else
define do
	@echo '$(SPACE)$(SPACE)$(1)$(TAB)$(2)'; \
	$(strip $(3)) || ( \
		exit=$$?; \
		echo '  (command was: $(strip $(3)))'; \
		exit $$exit \
	)
endef
endif

#### Generic configuration variables

ifndef OBJDIR
	OBJDIR := build
endif
$(call define-default, objdir, $(OBJDIR))

$(objdir):
	$(call do,MKDIR,$@,mkdir -p $@)

#### Cleaning

.PHONY: clean

define clean-recipe
$(foreach x,$(to-clean),$(call do,RM,$(objdir)/$(x),rm -f $(objdir)/$(x))
)
$(call do,RMDIR,$(objdir),rmdir $(objdir) 2>/dev/null || true)
endef

clean: ; $(clean-recipe)

#### C

### Configuration variables

$(call define-default, cc, $(CC))
$(call define-default, cc.flags, $(CFLAGS))

ifeq ($(origin CPP), undefined)
	CPP := $(CC) -E
endif
$(call define-default, cpp, $(CPP))
$(call define-default, cpp.flags, $(CPPFLAGS))

$(call define-default, ld, $(LD))
$(call define-default, ld.flags, $(LDFLAGS))

### Internal variables

cc.invoke = $(call cat, $(cc) $(cc.flags) $(cpp.flags))
cc.link = $(call cat, $(cc.invoke) $(ld.flags))

### Rules

define c-program-body
to-install += $(1)
to-clean += $(1) $(2:.c=.o) $(2:.c=.d)
$(objdir)/$(1): $(2:%.c=$(objdir)/%.o) | $(objdir) Makefile ; \
	$(call do,LINK,$(objdir)/$(1),$(cc.link) -o $(objdir)/$(1) \
		$(2:%.c=$(objdir)/%.o))
$(if $(cleaning),,-include $(2:%.c=$(objdir)/%.d))
endef

## $(call c-program,foo,foo.c bar.c) -- compiles foo.c and bar.c into foo
c-program = $(eval $(call c-program-body,$(strip $(1)),$(strip $(2))))

define c-object-body
to-clean += $(1:.c=.o) $(1:.c=.d)
$(if $(cleaning),,-include $(1:%.c=$(objdir)/%.d))
endef

## $(call c-object,foo.c) -- compiles foo.c into foo.o
c-object = $(eval $(call c-object-body,$(strip $(1))))

$(objdir)/%.o: %.c | $(objdir) Makefile
	$(call do,CC,$<,$(cc.invoke) -c -o $@ $<)

$(objdir)/%.d: %.c | $(objdir) Makefile
	$(call do,DEP,$<,$(cc.invoke) -M -MG -MF $@ $<)

#### End

.PHONY: all
.DEFAULT_GOAL := all
