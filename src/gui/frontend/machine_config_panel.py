# -*- coding: utf-8 -*-

#
# machine_config_panel.py
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
from backend.user_config import *
from backend.pmc_connect import *

class MachineConfigPanel():
    def __init__(self, config_frame):
        self.config_frame = config_frame
	self.panel = wx.Panel(self.config_frame, -1)
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

        self.__set_properties()
        self.__do_layout()

	self.text_pass.Bind(wx.EVT_TEXT_ENTER, self.on_click_next)
	self.radio_auth_key.Bind(wx.EVT_RADIOBUTTON, self.on_change_auth)
	self.radio_auth_pass.Bind(wx.EVT_RADIOBUTTON, self.on_change_auth)
	self.select_machine.Bind(wx.EVT_RADIOBOX, self.on_change_remote_machine)
	self.button_next.Bind(wx.EVT_BUTTON, self.on_click_next)

    def __set_properties(self):
        self.select_machine.SetSelection(0)
	self.path_key.SetPath("")
	self.path_key.Hide()
	self.text_pass.SetValue("")
        self.label_address.SetMinSize((80, 22))
        self.label_user.SetMinSize((80, 22))
        self.label_auth_opt.SetMinSize((80, 22))
        self.button_next.SetMinSize((225, 42))
	self.__change_enable_controls_remote_machine(False)

    def __do_layout(self):
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
		self.config_frame.user_config.machine = MachineConfig(True, address, port, user, passwd, key)
	else:
		self.config_frame.user_config.machine = MachineConfig(False)

    def __detect_other_machine_config(self):
        detected = False
        if self.config_frame.user_config.machine != None:
            if self.config_frame.user_config.machine.is_remote != (self.select_machine.GetSelection() == 1):
                detected = True
	    elif self.config_frame.user_config.machine.is_remote:
		if self.config_frame.user_config.machine.remote_address != self.text_address.GetValue(): detected = True
		if self.config_frame.user_config.machine.remote_port != self.text_port.GetValue(): detected = True
		if self.config_frame.user_config.machine.remote_user != self.text_user.GetValue(): detected = True
		if self.config_frame.user_config.machine.remote_password != self.text_pass.GetValue(): detected = True
		if self.config_frame.user_config.machine.path_key != self.path_key.GetPath(): detected = True
        return detected

    def __validate(self, pmc_connect):
	dlg = None
	if self.config_frame.user_config.machine.is_remote:
		if self.config_frame.user_config.machine.remote_password != "" and not pmc_connect.CheckPkgInstalled("sshpass", False):
			dlg = wx.MessageDialog(parent=None, message=_("'sshpass' package is required to authenticate by password."), caption=_("Information"), style=wx.OK | wx.ICON_INFORMATION)
		else:
			err_connect = pmc_connect.CheckConnectivity()
			if err_connect != "":
				dlg = wx.MessageDialog(parent=None, message=_("Unable to access to '{0}'").format(self.config_frame.user_config.machine.remote_address) + ": \n\n" + err_connect, caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
	if dlg == None:
		if not pmc_connect.CheckPkgInstalled("pmctrack", self.config_frame.user_config.machine.is_remote):
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
        # If the machine to monitor changes, and user accept it, remove the counters and metrics panel previously generated.
        if self.config_frame.panels[1] != None and self.__detect_other_machine_config():
            dlg = wx.MessageDialog(parent=None, message=_("By changing the machine to monitor settings previously made will be deleted.\n\nAre you sure you wanna do this?."), 
	        caption=_("Advertisement"), style=wx.YES_NO | wx.ICON_EXCLAMATION)
            if dlg.ShowModal() == wx.ID_YES:
                self.config_frame.panels[1].DestroyComponents()
                self.config_frame.panels[1] = None
	# If there is a configured machine and the user has not changed, continue without opposition to counters and metrics panel.
        elif self.config_frame.panels[1] != None:
            self.config_frame.GoToPanel(1)
	# If no counters and metrics panel loaded, load into memory.
        if self.config_frame.panels[1] == None:
	    self.__save_user_config()
	    pmc_connect = PMCConnect(self.config_frame.user_config.machine)
	    if self.__validate(pmc_connect):
                self.config_frame.pmc_connect = pmc_connect
                self.config_frame.facade_xml = FacadeXML(pmc_connect)
                self.config_frame.GoToPanel(1)
                if self.config_frame.panels[2] != None:
                    self.config_frame.panels[2].UpdateCtrlPathSingleApp()
                    self.config_frame.panels[2].UpdateComboCPU()
