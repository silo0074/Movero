#!/bin/sh
# Refresh the desktop database
update-desktop-database -q /usr/share/applications
# Refresh the KDE/Dolphin service menu cache
kbuildsycoca6 --noincremental > /dev/null 2>&1 || true