#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
# pmc_frame_machine.py
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
#
#  2015-05-16 Modified by Abel Serrano to improve translations.
#

import wx

from backend.facade_xml import *
from backend.user_config import *
from backend.pmc_connect import *
from frames.pmc_frame_counters import *

class PMCFrameMachine(wx.Frame):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Frame.__init__(self, *args, **kwds)
	
	self.panel = wx.Panel(self, -1)
	self.version = kwargs.get("version")
	self.user_config = kwargs.get("user_config")
        self.select_machine = wx.RadioBox(self.panel, -1, _("Select machine to monitor"), choices=[_("Local"), _("Remote")], majorDimension=0, style=wx.RA_SPECIFY_ROWS)
        self.label_address = wx.StaticText(self.panel, -1, _("Address") + ": ")
        self.text_address = wx.TextCtrl(self.panel, -1, "")
        self.label_port = wx.StaticText(self.panel, -1, " " + _("Port") + ": ")
        self.text_port = wx.TextCtrl(self.panel, -1, "22")
        self.label_user = wx.StaticText(self.panel, -1, _("Username") + ": ")
        self.text_user = wx.TextCtrl(self.panel, -1, "")
	self.label_auth_quiz = wx.StaticText(self.panel, -1, _("How do you want to authenticate?"))
        self.radio_auth_pass = wx.RadioButton(self.panel, -1, _("Using password"))
        self.radio_auth_key = wx.RadioButton(self.panel, -1, _("Using SSH key"))
	self.sizer_rm_auth = wx.BoxSizer(wx.HORIZONTAL) # It is part of the frame that must be modified at runtime.
        self.label_auth_opt = wx.StaticText(self.panel, -1, _("Password") + ": ")
        self.text_pass = wx.TextCtrl(self.panel, -1, "", style=wx.TE_PASSWORD | wx.TE_PROCESS_ENTER)
        self.path_key = wx.FilePickerCtrl(self.panel, -1, "", _("Select your SSH key"), _("SSH Key") + " |*")
        self.sizer_remote_machine_staticbox = wx.StaticBox(self.panel, -1, _("Remote machine parameters"))
        self.button_next = wx.Button(self.panel, -1, _("Next") + " >")
	self.next_frame = None
	# Contains the reference to the final frame configuration when such reference is not in the frame of counters (because it has been removed)
	self.final_frame_ref_temp = None

        self.__set_properties()
        self.__do_layout()

	self.text_pass.Bind(wx.EVT_TEXT_ENTER, self.on_click_next)
	self.Bind(wx.EVT_RADIOBUTTON, self.on_change_auth, self.radio_auth_key)
	self.Bind(wx.EVT_RADIOBUTTON, self.on_change_auth, self.radio_auth_pass)
	self.Bind(wx.EVT_RADIOBOX, self.on_change_remote_machine, self.select_machine)
	self.Bind(wx.EVT_BUTTON, self.on_click_next, self.button_next)
	self.Bind(wx.EVT_CLOSE, self.on_close_frame)

    def __set_properties(self):
        self.SetTitle("PMCTrack-GUI v" + self.version + " - " + _("Machine selection"))
        self.SetSize((717, 600))
        self.select_machine.SetSelection(0)
	self.path_key.SetPath("")
	self.path_key.Hide()
	self.text_pass.SetValue("")
        self.label_address.SetMinSize((80, 22))
        self.label_user.SetMinSize((80, 22))
        self.label_auth_opt.SetMinSize((80, 22))
        self.button_next.SetMinSize((160, 42))
	self.__change_enable_controls_remote_machine(False)

    def __do_layout(self):
	sizer_panel = wx.BoxSizer(wx.VERTICAL)
        separator = wx.BoxSizer(wx.VERTICAL)
        self.sizer_remote_machine_staticbox.Lower()
        sizer_remote_machine = wx.StaticBoxSizer(self.sizer_remote_machine_staticbox, wx.VERTICAL)
        sizer_rm_address = wx.BoxSizer(wx.HORIZONTAL)
        sizer_rm_user = wx.BoxSizer(wx.HORIZONTAL)
        sizer_rm_auth_quiz = wx.BoxSizer(wx.HORIZONTAL)
        separator.Add(self.select_machine, 5, wx.ALL | wx.EXPAND, 5)
        sizer_rm_address.Add(self.label_address, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_rm_address.Add(self.text_address, 4, wx.ALIGN_CENTER_VERTICAL, 0)
        sizer_rm_address.Add(self.label_port, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 15)
        sizer_rm_address.Add(self.text_port, 1, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_remote_machine.Add(sizer_rm_address, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
        sizer_rm_user.Add(self.label_user, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_rm_user.Add(self.text_user, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_remote_machine.Add(sizer_rm_user, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
	sizer_rm_auth_quiz.Add(self.label_auth_quiz, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_rm_auth_quiz.Add(self.radio_auth_pass, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_rm_auth_quiz.Add(self.radio_auth_key, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_remote_machine.Add(sizer_rm_auth_quiz, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
        self.sizer_rm_auth.Add(self.label_auth_opt, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        self.sizer_rm_auth.Add(self.text_pass, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_remote_machine.Add(self.sizer_rm_auth, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
        separator.Add(sizer_remote_machine, 9, wx.ALL | wx.EXPAND, 5)
        separator.Add(self.button_next, 0, wx.RIGHT | wx.BOTTOM | wx.ALIGN_RIGHT, 5)
	self.panel.SetSizer(separator)
	sizer_panel.Add(self.panel, 1, wx.EXPAND, 0)
        self.SetSizer(sizer_panel)
        self.Layout()

    def __change_enable_controls_remote_machine(self, enable):
	self.label_address.Enable(enable)
	self.text_address.Enable(enable)
	self.label_port.Enable(enable)
	self.text_port.Enable(enable)
	self.label_user.Enable(enable)
	self.text_user.Enable(enable)
	self.label_auth_quiz.Enable(enable)
	self.radio_auth_key.Enable(enable)
	self.radio_auth_pass.Enable(enable)
	self.label_auth_opt.Enable(enable)
	self.text_pass.Enable(enable)
	self.path_key.Enable(enable)
	self.sizer_remote_machine_staticbox.Enable(enable)

    def __save_user_config(self):
	is_remote = (self.select_machine.GetSelection() == 1)
	if(is_remote):
		address = self.text_address.GetValue()
		port = self.text_port.GetValue()
		user = self.text_user.GetValue()
		passwd = self.text_pass.GetValue()
		key = self.path_key.GetPath()
		self.user_config.machine = MachineConfig(True, address, port, user, passwd, key)
	else:
		self.user_config.machine = MachineConfig(False)

    def __detect_other_machine_config(self):
        detected = False
        if self.user_config.machine != None:
            if self.user_config.machine.is_remote != (self.select_machine.GetSelection() == 1):
                detected = True
	    elif self.user_config.machine.is_remote:
		if self.user_config.machine.remote_address != self.text_address.GetValue(): detected = True
		if self.user_config.machine.remote_port != self.text_port.GetValue(): detected = True
		if self.user_config.machine.remote_user != self.text_user.GetValue(): detected = True
		if self.user_config.machine.remote_password != self.text_pass.GetValue(): detected = True
		if self.user_config.machine.path_key != self.path_key.GetPath(): detected = True
        return detected

    def __validate(self, pmc_connect):
	dlg = None
	if self.user_config.machine.is_remote:
		if self.user_config.machine.remote_password != "" and not pmc_connect.CheckPkgInstalled("sshpass", False):
			dlg = wx.MessageDialog(parent=None, message=_("'sshpass' package is required to authenticate by password."), caption=_("Information"), style=wx.OK | wx.ICON_INFORMATION)
		else:
			err_connect = pmc_connect.CheckConnectivity()
			if err_connect != "":
				dlg = wx.MessageDialog(parent=None, message=_("Unable to access to '{0}'").format(self.user_config.machine.remote_address) + ": \n\n" + err_connect, caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
	if dlg == None:
		if not pmc_connect.CheckPkgInstalled("pmctrack", self.user_config.machine.is_remote):
			dlg = wx.MessageDialog(parent=None, message=_("PMCTrack not detected on the machine selected for monitoring.\n\nMake sure you have it installed and added to PATH."), caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
		elif not pmc_connect.CheckFileExists("/proc/pmc/info"):
			dlg = wx.MessageDialog(parent=None, message=_("PMCTrack configuration file not found.\n\nMake sure you have loaded the pmctrack module on the machine to be monitored."), caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
	if dlg != None:
		dlg.ShowModal()
	return (dlg == None)

    def on_change_remote_machine(self, event):
	self.__change_enable_controls_remote_machine(self.select_machine.GetSelection() == 1)
	if self.select_machine.GetSelection() == 1:
		self.text_address.SetFocus()

    def on_change_auth(self, event):
	self.sizer_rm_auth.Remove(1)
	if self.radio_auth_pass.GetValue():
		self.path_key.Hide()
		self.path_key.SetPath("")
        	self.sizer_rm_auth.Add(self.text_pass, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
		self.text_pass.Show()
		self.label_auth_opt.SetLabel(_("Password") + ": ")
	else:
		self.text_pass.Hide()
		self.text_pass.SetValue("")
        	self.sizer_rm_auth.Add(self.path_key, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
		self.path_key.Show()
		self.label_auth_opt.SetLabel(_("SSH Key") + ": ")
	self.sizer_rm_auth.Layout()


    def on_click_next(self, event):
        # If the machine to monitor changes, and user accept it, remove the frame counters and metrics previously generated.
        if self.next_frame != None and self.__detect_other_machine_config():
            dlg = wx.MessageDialog(parent=None, message=_("By changing the machine to monitor settings previously made will be deleted.\n\nAre you sure you wanna do this?."), 
	        caption=_("Advertisement"), style=wx.YES_NO | wx.ICON_EXCLAMATION)
            if dlg.ShowModal() == wx.ID_YES:
		self.final_frame_ref_temp = self.next_frame.next_frame
		self.next_frame.prev_frame = None
		self.next_frame.next_frame = None
                self.next_frame.Close()
                self.next_frame = None
	# If there is a configured machine and the user has not changed, continue without opposition to counters and metrics frame.
        elif self.next_frame != None:
            self.next_frame.SetPosition(self.GetPosition())
            self.next_frame.SetSize(self.GetSize())
            self.Hide()
            self.next_frame.Show()
	# If no counters and metrics frame loaded, load into memory.
        if self.next_frame == None:
	    self.__save_user_config()
	    pmc_connect = PMCConnect(self.user_config.machine)
	    if self.__validate(pmc_connect):
	    	self.next_frame = PMCFrameCounters(None, -1, "", self.GetPosition(), self.GetSize(), version=self.version, user_config=self.user_config, facade_xml=FacadeXML(pmc_connect))
    	    	self.Hide()
	    	self.next_frame.prev_frame = self
                # If there is a final configuration frame, do the appropriate links with the new counters and metrics frame.
		if self.final_frame_ref_temp != None:
	    		self.next_frame.next_frame = self.final_frame_ref_temp
			self.final_frame_ref_temp.prev_frame = self.next_frame
			self.final_frame_ref_temp = None
	    	self.next_frame.Show()

    def on_close_frame(self, event):
	if self.next_frame != None:
	    self.next_frame.prev_frame = None
	    self.next_frame.Close()
	if self.final_frame_ref_temp != None:
	    self.final_frame_ref_temp.prev_frame = None
	    self.final_frame_ref_temp.Close()
	self.Destroy()
