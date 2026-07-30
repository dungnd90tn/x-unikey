if COND_UNIKEY_GTK3
  MAYBE_UNIKEY_GTK3 = unikey-gtk3
endif

if COND_UNIKEY_GTK4
  MAYBE_UNIKEY_GTK4 = unikey-gtk4
endif

if COND_UNIKEY_QT6
  MAYBE_UNIKEY_QT6 = unikey-qt
endif

if COND_UNIKEY_IBUS
  MAYBE_UNIKEY_IBUS = unikey-ibus
endif

SUBDIRS = byteio vnconv ukengine ukinterface ukbridge \
	$(MAYBE_UNIKEY_GTK3) $(MAYBE_UNIKEY_GTK4) $(MAYBE_UNIKEY_QT6) $(MAYBE_UNIKEY_IBUS)
