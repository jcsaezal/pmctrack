# -*- coding: utf-8 -*-

#
# config_frame.py
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
import xml.etree.ElementTree as ET
import os.path
from backend.user_config import *
from frontend.machine_config_panel import *
from frontend.counters_config_panel import *
from frontend.final_config_panel import *
from frontend.about_dialog import *

# Configuration frame that contains the 3 configuration panels (Machine config, Counters and metrics config and final config)
class ConfigFrame(wx.Frame):
    def __init__(self, *args, **kwargs):
        kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Frame.__init__(self, *args, **kwds)

        self.version = kwargs.get("version")
        self.user_config = UserConfig()
        self.facade_xml = None
        self.pmc_connect = None
        self.panels = [None, None, None]
        self.actual_panel = 0
        self.sizer_panel = wx.BoxSizer(wx.VERTICAL)
	
	menubar = wx.MenuBar()
	fileMenu = wx.Menu()
	actionMenu = wx.Menu()
	helpMenu = wx.Menu()

	openMenuItem = wx.MenuItem(fileMenu, wx.ID_ANY, "&" + _("Open configuration") + "\tCtrl+O")
	fileMenu.AppendItem(openMenuItem)
	saveMenuItem = wx.MenuItem(fileMenu, wx.ID_ANY, "&" + _("Save configuration") + "\tCtrl+G")
	fileMenu.AppendItem(saveMenuItem)
	saveAsMenuItem = wx.MenuItem(fileMenu, wx.ID_ANY, "&" + _("Save configuration as...") + "\tCtrl+S")
	fileMenu.AppendItem(saveAsMenuItem)
	fileMenu.AppendSeparator()
	exitMenuItem = wx.MenuItem(fileMenu, wx.ID_ANY, "&" + _("Quit") + "\tCtrl+Q")
        fileMenu.AppendItem(exitMenuItem)

	machineMenuItem = wx.MenuItem(actionMenu, wx.ID_ANY, "&" + _("Go to machine configuration") + "\tF1")
	actionMenu.AppendItem(machineMenuItem)
	countersMenuItem = wx.MenuItem(actionMenu, wx.ID_ANY, "&" + _("Go to counters and metrics configuration") + "\tF2")
	actionMenu.AppendItem(countersMenuItem)
	finalMenuItem = wx.MenuItem(actionMenu, wx.ID_ANY, "&" + _("Go to final configuration") + "\tF3")
	actionMenu.AppendItem(finalMenuItem)
	actionMenu.AppendSeparator()
	monitoringMenuItem = wx.MenuItem(actionMenu, wx.ID_ANY, "&" + _("Start/Stop monitoring") + "\tF5")
	actionMenu.AppendItem(monitoringMenuItem)

	aboutMenuItem = wx.MenuItem(helpMenu, wx.ID_ANY, "&" + _("About") + "\tCtrl+A")
	helpMenu.AppendItem(aboutMenuItem)

        menubar.Append(fileMenu, "&" + _("File"))
        menubar.Append(actionMenu, "&" + _("Actions"))
        menubar.Append(helpMenu, "&" + _("Help"))
        self.SetMenuBar(menubar)

        self.SetSize((717, 570))
        self.SetSizer(self.sizer_panel)
        self.GoToPanel(0)

	wx.CallAfter(self.__ask_load_default_config)

        self.Bind(wx.EVT_MENU, self.on_open_config, openMenuItem)
        self.Bind(wx.EVT_MENU, self.on_save_config, saveMenuItem)
        self.Bind(wx.EVT_MENU, self.on_save_config_as, saveAsMenuItem)
        self.Bind(wx.EVT_MENU, self.on_close, exitMenuItem)
        self.Bind(wx.EVT_MENU, self.on_go_to_machine_config, machineMenuItem)
        self.Bind(wx.EVT_MENU, self.on_go_to_counters_config, countersMenuItem)
        self.Bind(wx.EVT_MENU, self.on_go_to_final_config, finalMenuItem)
        self.Bind(wx.EVT_MENU, self.on_start_stop_monitoring, monitoringMenuItem)
        self.Bind(wx.EVT_MENU, self.on_about, aboutMenuItem)
        self.Bind(wx.EVT_CLOSE, self.on_close)


    def __destroy_components(self):
    	for i in range(3):
        	if self.panels[i] != None:
            		self.panels[i].DestroyComponents()
        self.Destroy()

    def __save_config_to_xml(self, xml_path):
	xml_root = ET.Element("pmctrack-gui")
	xml_root_panels = ["machine_config", "counters_and_metrics_config", "final_config"]
	for i in range(3):
        	if self.panels[i] != None:
            		self.panels[i].ConfigToXML(ET.SubElement(xml_root, xml_root_panels[i]))
	ET.ElementTree(xml_root).write(xml_path)

    def __ask_load_default_config(self):
	if os.path.exists("/tmp/pmctrack-gui_save.xml"):
		msg = _("It found a default configuration file. Do you want to load it?")
		dlg = wx.MessageDialog(parent=None, message=msg, caption=_("Load default config"), style=wx.YES_NO | wx.ICON_INFORMATION)
		if dlg.ShowModal() == wx.ID_YES:
			self.__load_config_from_xml("/tmp/pmctrack-gui_save.xml")
		dlg.Destroy()

    def __load_config_from_xml(self, xml_path):
	xml_root = ET.parse(xml_path).getroot()
	xml_machine_config = xml_root.find("machine_config")
	xml_counters_config = xml_root.find("counters_and_metrics_config")
	xml_final_config = xml_root.find("final_config")

	if xml_machine_config != None:
		self.panels[0].ConfigFromXML(xml_machine_config)
		self.panels[0].SaveUserConfig()
		panel_to_show = self.actual_panel
		self.GoToPanel(0)
		self.pmc_connect = PMCConnect(self.user_config.machine)
		if self.__validate_machine_config():
			self.facade_xml = FacadeXML(self.pmc_connect)
			self.CreatePanel(1)
			self.CreatePanel(2)
			self.GoToPanel(panel_to_show)
			if xml_counters_config != None:
				self.panels[1].ConfigFromXML(xml_counters_config)
			if xml_final_config != None:
				self.panels[2].ConfigFromXML(xml_final_config)

    def __validate_machine_config(self):
        dlg = None
        if self.user_config.machine.type_machine == "ssh":
                if self.user_config.machine.remote_password != "" and not self.pmc_connect.CheckPkgInstalled("sshpass", False):
                        dlg = wx.MessageDialog(parent=None, message=_("'sshpass' package is required to authenticate by password."), caption=_("Information"), style=wx.OK | wx.ICON_INFORMATION)
        elif self.user_config.machine.type_machine == "adb" and not self.pmc_connect.CheckPkgInstalled("adb", False):
                        dlg = wx.MessageDialog(parent=None, message=_("'android-tools-adb' package is required to perform remote monitoring using ADB."), caption=_("Information"), style=wx.OK | wx.ICON_INFORMATION)
        if dlg == None and (self.user_config.machine.type_machine == "ssh" or self.user_config.machine.type_machine == "adb"): 
                err_connect = self.pmc_connect.CheckConnectivity()
                if err_connect != "": 
                        dlg = wx.MessageDialog(parent=None, message=_("Unable to access to '{0}'").format(self.user_config.machine.remote_address) + ": \n\n" + err_connect, caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
        if dlg == None and not self.pmc_connect.CheckFileExists("/proc/pmc/info"):
                        dlg = wx.MessageDialog(parent=None, message=_("PMCTrack configuration file not found.\n\nMake sure you have loaded the pmctrack module on the machine to be monitored."), caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
        if dlg != None:
                dlg.ShowModal()
        return (dlg == None)

    def CheckMachineConfigAndGoToMonitoringPanel(self, nr_panel):
	# If the machine to monitor changes, and user accept it, remove the counters and metrics panel previously generated.
        if self.panels[1] != None and self.panels[0].MachineConfigHasChanged():
            dlg = wx.MessageDialog(parent=None, message=_("By changing the machine to monitor settings previously made will be deleted.\n\nAre you sure you wanna do this?."),
                caption=_("Changes detected in the machine config"), style=wx.YES_NO | wx.ICON_EXCLAMATION)
            if dlg.ShowModal() == wx.ID_YES:
                self.RemovePanel(1)
        # If there is a configured machine and the user has not changed, continue without opposition to panel nr_panel.
        elif self.panels[1] != None:
            self.GoToPanel(nr_panel)
        # If no counters and metrics panel loaded, load into memory, along with final panel.
        if self.panels[1] == None:
            self.panels[0].SaveUserConfig()
            self.pmc_connect = PMCConnect(self.user_config.machine)
            if self.__validate_machine_config():
                self.facade_xml = FacadeXML(self.pmc_connect)
                self.CreatePanel(1)
                self.panels[1].BuildExperiment()
                if self.panels[2] != None:
                    self.panels[2].UpdateCtrlPathSingleApp()
                    self.panels[2].UpdateComboCPU()
                else:
                    self.CreatePanel(2)
                self.GoToPanel(nr_panel)

    def on_close(self, event):
    	msg = _("Do you want to save the current monitoring configuration as default configuration?")
    	dlg = wx.MessageDialog(parent=None, message=msg, caption=_("Save config"), style=wx.YES_NO | wx.CANCEL | wx.ICON_QUESTION)
	result = dlg.ShowModal() 
	dlg.Destroy()
	if result == wx.ID_YES:
    		self.__save_config_to_xml("/tmp/pmctrack-gui_save.xml")
		self.__destroy_components()
	elif result == wx.ID_NO:
		self.__destroy_components()

    def on_save_config(self, event):
    	self.__save_config_to_xml("/tmp/pmctrack-gui_save.xml")

    def on_save_config_as(self, event):
	msg = _("Save PMCTrack-GUI config file as")
	wildcard = _("PMCTrack-GUI config file") + " (*.xml)|*.xml|" + _("All files") + " (*.*)|*.*"
	dlg = wx.FileDialog(self, msg, "./", "", wildcard, wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT)
	dlg.SetFilename("pmctrack-gui_save.xml")
	if dlg.ShowModal() == wx.ID_OK:
    		self.__save_config_to_xml(dlg.GetPath())
	dlg.Destroy()

    def on_open_config(self, event):
    	msg = _("Open PMCTrack-GUI config file")
	wildcard = _("PMCTrack-GUI config file") + " (*.xml)|*.xml|" + _("All files") + " (*.*)|*.*"
    	dlg = wx.FileDialog(None, msg, "./", "", wildcard)
	if dlg.ShowModal() == wx.ID_OK:
		self.__load_config_from_xml(dlg.GetPath())
	dlg.Destroy()

    def on_go_to_machine_config(self, event):
    	self.GoToPanel(0)

    def on_go_to_counters_config(self, event):
    	if self.actual_panel == 0:
		self.CheckMachineConfigAndGoToMonitoringPanel(1)
	else:
		self.GoToPanel(1)

    def on_go_to_final_config(self, event):
    	if self.actual_panel == 0:
		self.CheckMachineConfigAndGoToMonitoringPanel(2)
	else:
		self.GoToPanel(2)

    def on_start_stop_monitoring(self, event):
    	if self.panels[1] != None:
    		if self.panels[1].ValidateCountersAndMetricsConfig():
			self.panels[2].StartStopMonitoring()
	else:
		dlg = wx.MessageDialog(parent=None, message=_("To start monitoring is necessary to fill the fields in the counters and metrics configuration panel and the final panel."), caption=_("Monitoring setup is incomplete"), style=wx.OK|wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()
		

    def on_about(self, event):
	about_dialog = AboutDialog(None, -1, "", version=self.version)
	about_dialog.ShowModal()
	about_dialog.Destroy()

    def SaveMonitoringSessionConfig(self):
    	self.panels[1].SaveUserConfig()
    	self.panels[2].SaveUserConfig()

    # Public function that allows to remove a configuration panel.
    def RemovePanel(self, nr_panel):
    	self.sizer_panel.Detach(self.panels[nr_panel].panel)
	self.panels[nr_panel].DestroyComponents()
	self.panels[nr_panel] = None

    # Public function that allows to create a configuration panel without to go to him.
    def CreatePanel(self, nr_panel):
    	# If panel exists, we remove it.
        if self.panels[nr_panel] != None:
		self.RemovePanel(nr_panel)

	if nr_panel == 0:
                self.panels[0] = MachineConfigPanel(self)
        elif nr_panel == 1:
                self.panels[1] = CountersConfigPanel(self)
        elif nr_panel == 2:
                self.panels[2] = FinalConfigPanel(self)
                
	self.sizer_panel.Insert(nr_panel, self.panels[nr_panel].panel, 1, wx.EXPAND, 0)
        self.sizer_panel.Show(self.panels[nr_panel].panel, False, True)

    # Public function that allows to show another configuration panel.
    def GoToPanel(self, nr_panel):
        if self.panels[nr_panel] == None:
		self.CreatePanel(nr_panel)

        self.sizer_panel.Show(self.panels[self.actual_panel].panel, False, True)
        self.sizer_panel.Show(self.panels[nr_panel].panel, True, True)
        self.actual_panel = nr_panel

        if self.actual_panel == 0:
            self.SetTitle(_("Machine selection") + "  —  PMCTrack-GUI")
        elif self.actual_panel == 1:
            self.SetTitle(_("Counters & metrics configuration") + "  —  PMCTrack-GUI")
        elif self.actual_panel == 2:
            self.SetTitle(_("Final monitoring configuration") + "  —  PMCTrack-GUI")

        self.Layout()
