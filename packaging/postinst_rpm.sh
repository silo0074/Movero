#!/bin/sh

# Refresh desktop database after removal
echo "Refreshing desktop database"
update-desktop-database -q /usr/share/applications || true

# Refresh icon cache
echo "Refreshing icon cache"
gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true

# Refresh the KDE/Dolphin service menu cache
echo "Refreshing the KDE/Dolphin service menu cache"
kbuildsycoca6 --noincremental

# Touching the system applications folder often triggers 
# modern desktops to notice the change and auto-refresh.
touch /usr/share/applications
touch /usr/share/icons/hicolor