# -*- coding: utf-8 -*-

#
# assign_event_dialog.py
#
##############################################################################
#
# Copyright (c) 2015 Jorge Casas <jorcasas@ucm.es>
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
from backend.facade_xml import *

class AssignEventDialog(wx.Dialog):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Dialog.__init__(self, *args, **kwds)

	self.facade_xml = kwargs.get("facade_xml")
        self.list_event = wx.ListBox(self, -1)
        self.sizer_event_staticbox = wx.StaticBox(self, -1, _("Select event"))
        self.list_subevents = wx.ListBox(self, -1, style=wx.LB_MULTIPLE)
        self.sizer_subevent_staticbox = wx.StaticBox(self, -1, _("Select subevents"))
        self.checkbox_usr = wx.CheckBox(self, -1, "USR")
        self.checkbox_os = wx.CheckBox(self, -1, "OS")
        self.label_umask = wx.StaticText(self, -1, "UMask: ")
        self.text_umask = wx.TextCtrl(self, -1, "")
	self.label_cmask = wx.StaticText(self, -1, "CMask: ")
        self.text_cmask = wx.TextCtrl(self, -1, "")
	self.label_ebs = wx.StaticText(self, -1, "EBS: ")
        self.text_ebs = wx.TextCtrl(self, -1, "")
        self.sizer_advanced_staticbox = wx.StaticBox(self, -1, _("Advanced options"))
        self.button_assign_event = wx.Button(self, -1, _("Assign event to counter"))
	self.selected_event = 0 # Used to prevent the user does not select any event

	# Used to load configuration subevents correctly
	self.selected_subevents = []
	self.first_time = True
	
	self.selected_flags = {}

	for event in self.facade_xml.getAvailableEvents():
		self.list_event.Append(event.name, event)
	
        self.__set_properties()
        self.__do_layout()
	self.UpdateListSubevents()

	self.Bind(wx.EVT_BUTTON, self.on_assign_event, self.button_assign_event)
	self.Bind(wx.EVT_LISTBOX_DCLICK, self.on_assign_event, self.list_event)
	self.Bind(wx.EVT_LISTBOX_DCLICK, self.on_assign_event, self.list_subevents)
	self.Bind(wx.EVT_LISTBOX, self.on_select_event, self.list_event)


    def __set_properties(self):
        self.SetTitle(_("Assign event to counter"))
        self.SetSize((700, 530))
        self.list_event.SetSelection(0)
        self.button_assign_event.SetMinSize((230, 37))

    def __do_layout(self):
        separator = wx.BoxSizer(wx.VERTICAL)
        sizer_div = wx.BoxSizer(wx.HORIZONTAL)
        sizer_div2 = wx.BoxSizer(wx.VERTICAL)
        self.sizer_advanced_staticbox.Lower()
        sizer_advanced = wx.StaticBoxSizer(self.sizer_advanced_staticbox, wx.VERTICAL)
	grid_advanced_inputs = wx.FlexGridSizer(4, 2, 5, 0)
        grid_advanced_inputs.AddGrowableCol(1)
        sizer_advanced_checkboxs = wx.BoxSizer(wx.HORIZONTAL)
        self.sizer_subevent_staticbox.Lower()
        sizer_subevent = wx.StaticBoxSizer(self.sizer_subevent_staticbox, wx.HORIZONTAL)
        self.sizer_event_staticbox.Lower()
        sizer_event = wx.StaticBoxSizer(self.sizer_event_staticbox, wx.VERTICAL)
        sizer_event.Add(self.list_event, 1, wx.ALL | wx.EXPAND, 5)
        sizer_div.Add(sizer_event, 1, wx.ALL | wx.EXPAND, 5)
        sizer_subevent.Add(self.list_subevents, 1, wx.ALL | wx.EXPAND, 5)
        sizer_div2.Add(sizer_subevent, 8, wx.ALL | wx.EXPAND, 5)
        sizer_advanced_checkboxs.Add(self.checkbox_usr, 0, wx.RIGHT | wx.ALIGN_CENTER_HORIZONTAL | wx.ALIGN_CENTER_VERTICAL, 20)
        sizer_advanced_checkboxs.Add(self.checkbox_os, 0, wx.ALIGN_CENTER_HORIZONTAL | wx.ALIGN_CENTER_VERTICAL, 0)
        sizer_advanced.Add(sizer_advanced_checkboxs, 0, wx.LEFT | wx.RIGHT | wx.EXPAND, 5)
	grid_advanced_inputs.Add(self.label_umask, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_advanced_inputs.Add(self.text_umask, 0, wx.EXPAND, 0)
	grid_advanced_inputs.Add(self.label_cmask, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_advanced_inputs.Add(self.text_cmask, 0, wx.EXPAND, 0)
	grid_advanced_inputs.Add(self.label_ebs, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_advanced_inputs.Add(self.text_ebs, 0, wx.EXPAND, 0)
        sizer_advanced.Add(grid_advanced_inputs, 1, wx.EXPAND | wx.ALL, 5)
        sizer_div2.Add(sizer_advanced, 5, wx.ALL | wx.EXPAND, 5)
        sizer_div.Add(sizer_div2, 1, wx.EXPAND, 0)
        separator.Add(sizer_div, 1, wx.EXPAND, 0)
        separator.Add(self.button_assign_event, 0, wx.RIGHT | wx.BOTTOM | wx.ALIGN_RIGHT, 5)
        self.SetSizer(separator)
        self.Layout()

    def UpdateListSubevents(self):
	event_obj = self.list_event.GetClientData(self.list_event.GetSelection())
	self.selected_flags.clear()
	for key in event_obj.flags.keys():
		self.selected_flags[key] = event_obj.flags[key]

	self.list_subevents.Clear()
        self.list_subevents.Enable(True)
	for subevent in event_obj.subevts:
		self.list_subevents.Append(subevent.name, subevent)
	if self.list_subevents.GetCount() == 0:
		self.list_subevents.Enable(False)
		self.list_subevents.Append(" " + _("No subevents for the selected event."))

    def on_select_event(self, event):
	if self.list_event.GetSelection() == wx.NOT_FOUND:
		self.list_event.SetSelection(self.selected_event) 
	else:
		self.selected_event = self.list_event.GetSelection()
		self.UpdateListSubevents()

    	if self.first_time:
		self.first_time = False
		for index_event in self.selected_subevents:
			self.list_subevents.SetSelection(index_event)

    def on_assign_event(self, event):
	self.EndModal(0)

    def SetLabelTitle(self, title):
        self.SetTitle(title)
        self.button_assign_event.SetLabel(title)

    def GetStringEvent(self):
	return self.list_event.GetString(self.list_event.GetSelection())

    def GetCodeEvent(self):
	return self.list_event.GetClientData(self.list_event.GetSelection()).code

    def __get_umask(self):
	    umask = None
	    if self.text_umask.GetValue() != "":
		umask = self.text_umask.GetValue()
	    elif len(self.list_subevents.GetSelections()) > 0:
	    	umask_int = 0
	    	for i in self.list_subevents.GetSelections():
	    	        subevent = self.list_subevents.GetClientData(i)
	    	        umask_int |= int(subevent.flags["umask"], 16)
	    	umask = hex(umask_int)
	    elif self.selected_flags.has_key("umask"):
		umask = self.selected_flags["umask"]
	    return umask

    def GetFlags(self):
	    flags = {}
	    umask = self.__get_umask()
	    if umask != '0x0' and umask != None:
		    flags["umask"] = umask
	    if self.checkbox_usr.GetValue() == 1:
		    flags["usr"] = "1"
	    if self.checkbox_os.GetValue() == 1:
		    flags["os"] = "1"
	    if self.text_cmask.GetValue() != "":
		    flags["cmask"] = self.text_cmask.GetValue()
            for key in self.selected_flags.keys():
		    if not flags.has_key(key):
			    flags[key] = self.selected_flags[key]
	    return flags

    def GetEBS(self):
	    return self.text_ebs.GetValue()
