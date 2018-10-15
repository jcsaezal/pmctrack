# -*- coding: utf-8 -*-

#
# about_dialog.py
#
##############################################################################
#
# Copyright (c) 2016 Jorge Casas <jorcasas@ucm.es>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA 02110-1301, USA.
#
##############################################################################

import wx

class AboutDialog(wx.Dialog):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Dialog.__init__(self, *args, **kwds)
        self.title_label = wx.StaticText(self, wx.ID_ANY, "PMCTrack-GUI", style=wx.ALIGN_CENTRE)
        self.version_label = wx.StaticText(self, wx.ID_ANY, _("Version") + " " +  kwargs.get("version"))
        self.horizontal_line = wx.StaticLine(self, wx.ID_ANY)
        self.content_label = wx.StaticText(self, wx.ID_ANY, _("PMCTrack-GUI is part of the PMCTrack performance monitoring tool.\nIt is licensed under the GNU GENERAL PUBLIC LICENSE Version 2.\n\nMore information in the official website."))
        self.hyperlink = wx.HyperlinkCtrl(self, wx.ID_ANY, "https://pmctrack.dacya.ucm.es", "https://pmctrack.dacya.ucm.es")
        self.ok_button = wx.Button(self, wx.ID_ANY, _("Close"))

        self.__set_properties()
        self.__do_layout()

	self.Bind(wx.EVT_BUTTON, self.on_close, self.ok_button)

    def __set_properties(self):
        self.SetTitle(_("About"))
        #self.SetSize((500, 380))
        self.title_label.SetFont(wx.Font(23, wx.DEFAULT, wx.NORMAL, wx.BOLD, 0, ""))
        self.ok_button.SetMinSize((230, 37))

    def __do_layout(self):
        separator = wx.BoxSizer(wx.VERTICAL)
        separator.Add(self.title_label, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.ALIGN_CENTER_HORIZONTAL | wx.ALIGN_CENTER_VERTICAL, 30)
        separator.Add(self.version_label, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.ALIGN_CENTER_HORIZONTAL | wx.ALIGN_CENTER_VERTICAL, 30)
        separator.Add(self.horizontal_line, 0, wx.EXPAND, 0)
        separator.Add(self.content_label, 0, wx.ALL | wx.ALIGN_CENTER_HORIZONTAL, 20)
        separator.Add(self.hyperlink, 0, wx.BOTTOM | wx.ALIGN_CENTER_HORIZONTAL, 20)
        separator.Add(self.ok_button, 0, wx.ALL | wx.ALIGN_RIGHT, 5)
        self.SetSizer(separator)
	separator.Fit(self)
        self.Layout()

    def on_close(self, event):
        self.EndModal(0)
