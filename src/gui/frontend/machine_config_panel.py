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
        self.select_machine = wx.RadioBox(self.panel, -1, _("Select machine to monitor"), choices=[_("Local"), _("Remote (using SSH)"), _("Remote (using ADB)")], majorDimension=0, style=wx.RA_SPECIFY_ROWS)

        self.label_address_ssh = wx.StaticText(self.panel, -1, _("Address") + ": ")
        self.text_address_ssh = wx.TextCtrl(self.panel, -1, "")
        self.label_port_ssh = wx.StaticText(self.panel, -1, " " + _("Port") + ": ")
        self.text_port_ssh = wx.TextCtrl(self.panel, -1, "22")
        self.label_user_ssh = wx.StaticText(self.panel, -1, _("Username") + ": ")
        self.text_user_ssh = wx.TextCtrl(self.panel, -1, "")
	self.label_auth_ssh_quiz = wx.StaticText(self.panel, -1, _("How do you want to authenticate?"))
        self.radio_auth_ssh_pass = wx.RadioButton(self.panel, -1, _("Using password"))
        self.radio_auth_ssh_key = wx.RadioButton(self.panel, -1, _("Using SSH key"))
	self.sizer_rm_auth = wx.BoxSizer(wx.HORIZONTAL) # It is part of the frame that must be modified at runtime.
        self.label_auth_ssh_opt = wx.StaticText(self.panel, -1, _("Password") + ": ")
        self.text_ssh_pass = wx.TextCtrl(self.panel, -1, "", style=wx.TE_PASSWORD | wx.TE_PROCESS_ENTER)
        self.path_ssh_key = wx.FilePickerCtrl(self.panel, -1, "", _("Select your SSH key"), _("SSH Key") + " |*")

	self.label_address_adb = wx.StaticText(self.panel, -1, _("Address") + ": ")
        self.text_address_adb = wx.TextCtrl(self.panel, -1, "")
        self.label_port_adb = wx.StaticText(self.panel, -1, " " + _("Port") + ": ")
        self.text_port_adb = wx.TextCtrl(self.panel, -1, "5037")

        self.button_next = wx.Button(self.panel, -1, _("Next") + " >")

        self.separator = wx.BoxSizer(wx.VERTICAL)
        self.sizer_ssh_machine_staticbox = wx.StaticBox(self.panel, -1, _("Remote machine SSH parameters"))
        self.sizer_ssh_machine = wx.StaticBoxSizer(self.sizer_ssh_machine_staticbox, wx.VERTICAL)
        self.sizer_adb_machine_staticbox = wx.StaticBox(self.panel, -1, _("Remote ADB-server parameters"))
        self.sizer_adb_machine = wx.StaticBoxSizer(self.sizer_adb_machine_staticbox, wx.VERTICAL)

        self.__set_properties()
        self.__do_layout()

	self.text_ssh_pass.Bind(wx.EVT_TEXT_ENTER, self.on_click_next)
	self.radio_auth_ssh_key.Bind(wx.EVT_RADIOBUTTON, self.on_change_auth)
	self.radio_auth_ssh_pass.Bind(wx.EVT_RADIOBUTTON, self.on_change_auth)
	self.select_machine.Bind(wx.EVT_RADIOBOX, self.on_change_remote_machine)
	self.button_next.Bind(wx.EVT_BUTTON, self.on_click_next)

    def __set_properties(self):
        self.select_machine.SetSelection(0)
	self.path_ssh_key.SetPath("")
	self.path_ssh_key.Hide()
	self.text_ssh_pass.SetValue("")
        self.label_address_ssh.SetMinSize((80, 22))
        self.label_user_ssh.SetMinSize((80, 22))
        self.label_auth_ssh_opt.SetMinSize((80, 22))
        self.button_next.SetMinSize((225, 42))

    def __do_layout(self):
        self.sizer_ssh_machine_staticbox.Lower()
        self.sizer_adb_machine_staticbox.Lower()
        sizer_ssh_address = wx.BoxSizer(wx.HORIZONTAL)
        sizer_ssh_user = wx.BoxSizer(wx.HORIZONTAL)
        sizer_ssh_auth_quiz = wx.BoxSizer(wx.HORIZONTAL)
        sizer_adb_address = wx.BoxSizer(wx.HORIZONTAL)

        self.separator.Add(self.select_machine, 1, wx.ALL | wx.EXPAND, 5)

        sizer_ssh_address.Add(self.label_address_ssh, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_ssh_address.Add(self.text_address_ssh, 4, wx.ALIGN_CENTER_VERTICAL, 0)
        sizer_ssh_address.Add(self.label_port_ssh, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 15)
        sizer_ssh_address.Add(self.text_port_ssh, 1, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        self.sizer_ssh_machine.Add(sizer_ssh_address, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
        sizer_ssh_user.Add(self.label_user_ssh, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_ssh_user.Add(self.text_user_ssh, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        self.sizer_ssh_machine.Add(sizer_ssh_user, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
	sizer_ssh_auth_quiz.Add(self.label_auth_ssh_quiz, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_ssh_auth_quiz.Add(self.radio_auth_ssh_pass, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_ssh_auth_quiz.Add(self.radio_auth_ssh_key, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        self.sizer_ssh_machine.Add(sizer_ssh_auth_quiz, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
        self.sizer_rm_auth.Add(self.label_auth_ssh_opt, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        self.sizer_rm_auth.Add(self.text_ssh_pass, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        self.sizer_ssh_machine.Add(self.sizer_rm_auth, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
        self.separator.Add(self.sizer_ssh_machine, 1, wx.ALL | wx.EXPAND, 5)

	sizer_adb_address.Add(self.label_address_adb, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_adb_address.Add(self.text_address_adb, 4, wx.ALIGN_CENTER_VERTICAL, 0)
        sizer_adb_address.Add(self.label_port_adb, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 15)
        sizer_adb_address.Add(self.text_port_adb, 1, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        self.sizer_adb_machine.Add(sizer_adb_address, 0, wx.LEFT | wx.RIGHT | wx.TOP | wx.EXPAND, 5)
        self.separator.Add(self.sizer_adb_machine, 1, wx.ALL | wx.EXPAND, 5)

        self.separator.Add(self.button_next, 0, wx.RIGHT | wx.BOTTOM | wx.ALIGN_RIGHT, 5)
	self.panel.SetSizer(self.separator)
        self.separator.Show(self.sizer_ssh_machine, False, True)
        self.separator.Show(self.sizer_adb_machine, False, True)
        self.separator.Layout()

    def __save_user_config(self):
	if self.select_machine.GetSelection() == 0: # Local
		self.config_frame.user_config.machine = MachineConfig("local")
	elif self.select_machine.GetSelection() == 1: # SSH
		address = self.text_address_ssh.GetValue()
		port = self.text_port_ssh.GetValue()
		user = self.text_user_ssh.GetValue()
		passwd = self.text_ssh_pass.GetValue()
		key = self.path_ssh_key.GetPath()
		self.config_frame.user_config.machine = MachineConfig("ssh", address, port, user, passwd, key)
	else: # ADB
		address = self.text_address_adb.GetValue()
		port = self.text_port_adb.GetValue()
		self.config_frame.user_config.machine = MachineConfig("adb", address, port)

    def __detect_other_machine_config(self):
        detected = False
	type_machine = "local"
        if self.config_frame.user_config.machine != None:
	    if self.select_machine.GetSelection() == 1:
	    	type_machine = "ssh"
	    elif self.select_machine.GetSelection() == 2:
	    	type_machine = "adb"
            if self.config_frame.user_config.machine.type_machine != type_machine:
                detected = True
	    elif self.config_frame.user_config.machine.type_machine == "ssh":
		if self.config_frame.user_config.machine.remote_address != self.text_address_ssh.GetValue(): detected = True
		if self.config_frame.user_config.machine.remote_port != self.text_port_ssh.GetValue(): detected = True
		if self.config_frame.user_config.machine.remote_user != self.text_user_ssh.GetValue(): detected = True
		if self.config_frame.user_config.machine.remote_password != self.text_ssh_pass.GetValue(): detected = True
		if self.config_frame.user_config.machine.path_key != self.path_ssh_key.GetPath(): detected = True
	    elif self.config_frame.user_config.machine.type_machine == "adb":
		if self.config_frame.user_config.machine.remote_address != self.text_address_adb.GetValue(): detected = True
		if self.config_frame.user_config.machine.remote_port != self.text_port_adb.GetValue(): detected = True
        return detected

    def __validate(self, pmc_connect):
	dlg = None
	if self.config_frame.user_config.machine.type_machine == "ssh":
		if self.config_frame.user_config.machine.remote_password != "" and not pmc_connect.CheckPkgInstalled("sshpass", False):
			dlg = wx.MessageDialog(parent=None, message=_("'sshpass' package is required to authenticate by password."), caption=_("Information"), style=wx.OK | wx.ICON_INFORMATION)
	elif self.config_frame.user_config.machine.type_machine == "adb" and not pmc_connect.CheckPkgInstalled("adb", False):
			dlg = wx.MessageDialog(parent=None, message=_("'android-tools-adb' package is required to perform remote monitoring using ADB."), caption=_("Information"), style=wx.OK | wx.ICON_INFORMATION)
	if dlg == None and (self.config_frame.user_config.machine.type_machine == "ssh" or self.config_frame.user_config.machine.type_machine == "adb"):
		err_connect = pmc_connect.CheckConnectivity()
		if err_connect != "":
			dlg = wx.MessageDialog(parent=None, message=_("Unable to access to '{0}'").format(self.config_frame.user_config.machine.remote_address) + ": \n\n" + err_connect, caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
	if dlg == None and not pmc_connect.CheckFileExists("/proc/pmc/info"):
			dlg = wx.MessageDialog(parent=None, message=_("PMCTrack configuration file not found.\n\nMake sure you have loaded the pmctrack module on the machine to be monitored."), caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
	if dlg != None:
		dlg.ShowModal()
	return (dlg == None)

    def on_change_remote_machine(self, event):
	if self.select_machine.GetSelection() == 0:
        	self.separator.Show(self.sizer_ssh_machine, False, True)
        	self.separator.Show(self.sizer_adb_machine, False, True)
	elif self.select_machine.GetSelection() == 1:
        	self.separator.Show(self.sizer_ssh_machine, True, True)
        	self.separator.Show(self.sizer_adb_machine, False, True)
		self.text_address_ssh.SetFocus()
	elif self.select_machine.GetSelection() == 2:
        	self.separator.Show(self.sizer_adb_machine, True, True)
        	self.separator.Show(self.sizer_ssh_machine, False, True)
		self.text_address_adb.SetFocus()
	self.separator.Layout()

    def on_change_auth(self, event):
	self.sizer_rm_auth.Remove(1)
	if self.radio_auth_ssh_pass.GetValue():
		self.path_ssh_key.Hide()
		self.path_ssh_key.SetPath("")
        	self.sizer_rm_auth.Add(self.text_ssh_pass, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
		self.text_ssh_pass.Show()
		self.label_auth_ssh_opt.SetLabel(_("Password") + ": ")
	else:
		self.text_ssh_pass.Hide()
		self.text_ssh_pass.SetValue("")
        	self.sizer_rm_auth.Add(self.path_ssh_key, 6, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
		self.path_ssh_key.Show()
		self.label_auth_ssh_opt.SetLabel(_("SSH Key") + ": ")
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
