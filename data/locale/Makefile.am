installdir = $(LOCALEDIR)

locale = \
	deutsch.locale \
	english.locale

install_DATA  = $(locale)


if MAINTAINER_MODE

master.locale=english.locale

locals: sort-locals work-locals locals.h locals_intern.h

$(master.locale) \
sort-locals:
	for locale in $(locale); do \
		cat $(top_srcdir)/data/locale/$${locale} | LC_ALL=C sort | uniq > $${locale}; \
	done

work-locals: $(master.locale)
	for locale in $(locale); do \
		( cd $(top_srcdir)/data/locale; helpers/create-locals-work $${locale}; ); \
	done

ordercheck: $(master.locale)
	cut -d' ' -f1 $(top_srcdir)/data/locale/$(master.locale) | LC_ALL=C sort | uniq > /tmp/log
	cut -d' ' -f1 $(top_srcdir)/data/locale/$(master.locale) | uniq  | diff - /tmp/log || \
		(echo "ERROR: $(master.locale) not ordered or contains empty lines" && false)

locals.h: ordercheck
	cut -d' ' -f1 $(top_srcdir)/data/locale/$(master.locale) | LC_ALL=C sort | uniq | tr [:lower:] [:upper:] | tr \. \_  | tr \- \_ | tr -d \? | \
		$(top_srcdir)/data/locale/helpers/create-locals.h

locals_intern.h: ordercheck
	cut -d' ' -f1 $(top_srcdir)/data/locale/$(master.locale) | LC_ALL=C sort | uniq | \
		$(top_srcdir)/data/locale/helpers/create-locals_intern.h

check: locals.h locals_intern.h
	diff locals.h $(top_srcdir)/src/system
	diff locals_intern.h $(top_srcdir)/src/system

install-locals: $(locale) locals.h locals_intern.h
	cp locals.h locals_intern.h $(top_srcdir)/src/system
	@echo "Consider committing src/system/[locals.h locals_intern.h]"
	## ??? cp -f $(locale) $(top_srcdir)/data/locale
	## @echo "Consider committing data/locale/[$(locale)]"

locals-clean:
	rm -f locals.h locals_intern.h $(locale)

endif
