import os
from gi.repository import Nautilus, GObject

class MoveroExtension(GObject.GObject, Nautilus.MenuProvider):
    def menu_activate_cb(self, menu, folder):
        path = folder.get_location().get_path()
        os.system(f"Movero --paste-to {path} &")

    def get_background_items(self, *args):
        # This handles right-clicking the empty space in a folder
        item = Nautilus.MenuItem(
            name="Movero::Paste",
            label="Paste with Movero",
            tip="Use Movero to transfer files here"
        )
        item.connect("activate", self.menu_activate_cb, args[-1])
        return [item]