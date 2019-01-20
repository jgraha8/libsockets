header-links: remove-links
	if test -n "$(AM_HEADER_PREFIX)"; then \
		$(MKDIR_P) "../$(AM_HEADER_PREFIX)"; \
		$(LN_S) $(PWD) "../$(AM_HEADER_PREFIX)/libsockets"; \
	fi
	HEADERLIST="$(top_srcdir)/include/*.h"; \
	for h in $$HEADERLIST; do \
	  BASENAME=`basename $$h`; \
	  test -r $$BASENAME || $(LN_S) $$h $$BASENAME; \
	done

remove-links:
	if test -n "$(AM_HEADER_PREFIX)"; then \
		rm -rf "../$(AM_HEADER_PREFIX)"; \
	fi
	rm -f *.h

all: all-am header-links

clean-local: remove-links
distclean-local: remove-links


