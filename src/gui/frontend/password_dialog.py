# -*- coding: utf-8 -*-

#
# password_dialog.py
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

class PasswordDialog(wx.Dialog):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Dialog.__init__(self, *args, **kwds)
	
        self.info_label = wx.StaticText(self, wx.ID_ANY, (_("'{0}' password on '{1}' machine:")).format(kwargs.get("user"), kwargs.get("machine")))
        self.pass_text = wx.TextCtrl(self, wx.ID_ANY, "", style=wx.TE_PASSWORD | wx.TE_PROCESS_ENTER)
        self.ok_button = wx.Button(self, wx.ID_ANY, _("Load configuration"))

        self.__set_properties()
        self.__do_layout()

	self.Bind(wx.EVT_BUTTON, self.on_accept, self.ok_button)
	self.Bind(wx.EVT_TEXT_ENTER, self.on_accept, self.pass_text)

    def __set_properties(self):
        self.SetTitle(_("Password required"))
        self.pass_text.SetMinSize((480, 33))
	self.ok_button.SetMinSize((230, 33))

    def __do_layout(self):
        separator = wx.BoxSizer(wx.VERTICAL)
        separator.Add(self.info_label, 0, wx.ALL, 5)
        separator.Add(self.pass_text, 0, wx.ALL | wx.EXPAND, 5)
        separator.Add(self.ok_button, 0, wx.ALL | wx.ALIGN_RIGHT, 5)
        self.SetSizer(separator)
        separator.Fit(self)
        self.Layout()

    def on_accept(self, event):
    	self.EndModal(0)

    def GetPassword(self):
    	return self.pass_text.GetValue()
