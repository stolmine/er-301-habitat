PROJECTS = plaits grids stratos clouds kryos warps rings commotio peaks scope spreadsheet biome marbles catchall

all: $(PROJECTS)

$(PROJECTS):
	+$(MAKE) -f mods/$@/mod.mk PKGNAME=$@

$(addsuffix -clean,$(PROJECTS)):
	$(eval PROJECT := $(@:-clean=))
	+$(MAKE) -f mods/$(PROJECT)/mod.mk clean PKGNAME=$(PROJECT)

$(addsuffix -install,$(PROJECTS)):
	$(eval PROJECT := $(@:-install=))
	+$(MAKE) -f mods/$(PROJECT)/mod.mk install PKGNAME=$(PROJECT)

clean: $(addsuffix -clean,$(PROJECTS))

.PHONY: all clean $(PROJECTS)
