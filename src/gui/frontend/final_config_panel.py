# -*- coding: utf-8 -*-

#
# final_config_panel.py
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
import xml.etree.ElementTree as ET
import re
from backend.facade_xml import *
from backend.user_config import *
from backend.pmc_extract import *
from frontend.multiapp_control_frame import *
from frontend.monitoring_frame import *
from frontend.graph_style_dialog import GraphStyleDialog
from frontend.advanced_settings_dialog import AdvancedSettingsDialog

class FinalConfigPanel():
    def __init__(self, config_frame):
        self.config_frame = config_frame
	self.panel = wx.Panel(self.config_frame, -1)
        
        # List of monitoring frames actually in memory.
	self.mon_frames = []
	# Stores the object manager to extract all information of the pmctrack commands.
	self.pmc_extract = None
        
        self.tab_appmode = wx.Notebook(self.panel, -1, style=0)
        self.singleapp_panel = wx.Panel(self.tab_appmode, -1)
	self.multiapp_panel = wx.Panel(self.tab_appmode, -1)
        
        cpu_choices = [_("No binding")]
	(vendor, flags, cores) = self.config_frame.facade_xml.getMachineInfo()
	for i in range(cores):
		cpu_choices.append(str(i))

        self.label_type_app = wx.StaticText(self.singleapp_panel, -1, _("Is the app to monitor running?"))
        self.radio_btn_app_running = wx.RadioButton(self.singleapp_panel, -1, _("Yes"))
        self.radio_btn_app_norunning = wx.RadioButton(self.singleapp_panel, -1, _("No"))
        self.label_path_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Path to application to monitor") + ": ")
	self.browse_path_singleapp = wx.FilePickerCtrl(self.singleapp_panel, -1,"", _("Select application"))
        self.text_path_singleapp = wx.TextCtrl(self.singleapp_panel, -1, "")

	if self.config_frame.user_config.machine.type_machine == "local":
		self.ctrl_path_singleapp = self.browse_path_singleapp
		self.text_path_singleapp.Hide()
	else:
		self.ctrl_path_singleapp = self.text_path_singleapp
		self.browse_path_singleapp.Hide()
	
        self.label_args_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Application's arguments") + ": ")
	self.text_args_singleapp = wx.TextCtrl(self.singleapp_panel, -1, "")
	self.label_cpu_norunning_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Select CPU to bind or type mask") + ": ")
	self.combo_cpu_norunning_singleapp = wx.ComboBox(self.singleapp_panel, -1, choices=cpu_choices, style=wx.CB_DROPDOWN)
        self.grid_sizer_singleapp = wx.FlexGridSizer(3, 2, 5, 0)

        self.label_pid_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Application's PID") + ": ")
	self.text_pid_singleapp = wx.TextCtrl(self.singleapp_panel, -1, "")
	self.label_cpu_running_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Select CPU to bind or type mask") + ": ")
	self.combo_cpu_running_singleapp = wx.ComboBox(self.singleapp_panel, -1, choices=cpu_choices, style=wx.CB_DROPDOWN)
        self.grid_running_singleapp = wx.FlexGridSizer(2, 2, 5, 0)

        self.label_multiapp_file = wx.StaticText(self.multiapp_panel, -1, _("Select multi-application file") + ": ")
	self.browse_multiapp_file = wx.FilePickerCtrl(self.multiapp_panel, -1,"", _("Select multi-application file"))
	self.label_cpu_multiapp = wx.StaticText(self.multiapp_panel, -1, _("Select CPU to bind or type mask") + ": ")
        self.combo_cpu_multiapp = wx.ComboBox(self.multiapp_panel, -1, choices=cpu_choices, style=wx.CB_DROPDOWN)
        self.grid_sizer_multiapp = wx.FlexGridSizer(3, 2, 5, 0)
        self.sizer_singleapp = wx.BoxSizer(wx.VERTICAL)

        self.sizer_graph_style_staticbox = wx.StaticBox(self.panel, -1, _("Graph style mode"))
        self.button_graph_style = wx.Button(self.panel, -1, _("Default"))
        self.sizer_advanced_settings_staticbox = wx.StaticBox(self.panel, -1, _("Expert mode"))
        self.button_advanced_settings = wx.Button(self.panel, -1, _("Advanced settings"))
        self.multiapp_frame = None

	self.graph_style_dialog = GraphStyleDialog(None, -1, "")
	self.advanced_settings_dialog = AdvancedSettingsDialog(None, -1, "")
        self.button_prev = wx.Button(self.panel, -1, "< " + _("Back"))
        self.button_monitoring = wx.Button(self.panel, -1, _("Start monitoring"))

        self.__set_properties()
        self.__do_layout()

	self.radio_btn_app_running.Bind(wx.EVT_RADIOBUTTON, self.on_change_type_app)
        self.radio_btn_app_norunning.Bind(wx.EVT_RADIOBUTTON, self.on_change_type_app)
	self.ctrl_path_singleapp.Bind(wx.EVT_FILEPICKER_CHANGED, self.on_change_path_singleapp)
	self.browse_multiapp_file.Bind(wx.EVT_FILEPICKER_CHANGED, self.on_change_multiapp_file)
	self.button_graph_style.Bind(wx.EVT_BUTTON, self.on_click_graph_style)
	self.button_advanced_settings.Bind(wx.EVT_BUTTON, self.on_click_advanced_settings)
	self.button_prev.Bind(wx.EVT_BUTTON, self.on_click_prev)
	self.button_monitoring.Bind(wx.EVT_BUTTON, self.on_click_monitoring)

    def __set_properties(self):
        self.tab_appmode.AddPage(self.singleapp_panel, _("Single-app mode"))
        self.tab_appmode.AddPage(self.multiapp_panel, _("Multi-app mode"))
        self.tab_appmode.SetSelection(0)
        self.combo_cpu_norunning_singleapp.SetSelection(0)
        self.combo_cpu_running_singleapp.SetSelection(0)
        self.combo_cpu_multiapp.SetSelection(0)
        self.radio_btn_app_norunning.SetValue(1)
	self.button_graph_style.SetMinSize((115, 33))
	self.button_advanced_settings.SetMinSize((115, 33))
        self.button_prev.SetMinSize((225, 42))
        self.button_monitoring.SetMinSize((225, 42))
	font_button_monitoring = self.button_monitoring.GetFont();
	font_button_monitoring.SetWeight(wx.FONTWEIGHT_BOLD);
	self.button_monitoring.SetFont(font_button_monitoring)

    def __do_layout(self):
        separator = wx.BoxSizer(wx.VERTICAL)
	sub_separator = wx.BoxSizer(wx.HORIZONTAL)
        sizer_controls = wx.BoxSizer(wx.HORIZONTAL)
	self.sizer_graph_style_staticbox.Lower()
        sizer_graph_style = wx.StaticBoxSizer(self.sizer_graph_style_staticbox, wx.VERTICAL)
        grid_sizer_graph_style = wx.FlexGridSizer(1, 1, 0, 0)
	self.sizer_advanced_settings_staticbox.Lower()
        sizer_advanced_settings = wx.StaticBoxSizer(self.sizer_advanced_settings_staticbox, wx.VERTICAL)
        grid_sizer_advanced_settings = wx.FlexGridSizer(1, 1, 0, 0)

	grid_sizer_type_app = wx.FlexGridSizer(1, 2, 0, 0)
        sizer_radio_type_app = wx.BoxSizer(wx.HORIZONTAL)

	grid_sizer_type_app.Add(self.label_type_app, 0, wx.RIGHT | wx.BOTTOM | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_radio_type_app.Add(self.radio_btn_app_running, 0, 0, 0)
        sizer_radio_type_app.Add(self.radio_btn_app_norunning, 0, wx.LEFT, 10)
        grid_sizer_type_app.Add(sizer_radio_type_app, 1, wx.EXPAND, 0)
        grid_sizer_type_app.AddGrowableCol(1)
        self.sizer_singleapp.Add(grid_sizer_type_app, 0, wx.ALL | wx.EXPAND, 5)

        self.grid_sizer_singleapp.Add(self.label_path_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_singleapp.Add(self.ctrl_path_singleapp, 0, wx.EXPAND, 0)
        self.grid_sizer_singleapp.Add(self.label_args_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_singleapp.Add(self.text_args_singleapp, 0, wx.EXPAND, 0)
        self.grid_sizer_singleapp.Add(self.label_cpu_norunning_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_singleapp.Add(self.combo_cpu_norunning_singleapp, 0, wx.EXPAND, 0)
        self.grid_sizer_singleapp.AddGrowableCol(1)
        self.sizer_singleapp.Add(self.grid_sizer_singleapp, 1, wx.ALL | wx.EXPAND, 5)

        self.grid_running_singleapp.Add(self.label_pid_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_running_singleapp.Add(self.text_pid_singleapp, 0, wx.EXPAND, 0)
        self.grid_running_singleapp.Add(self.label_cpu_running_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_running_singleapp.Add(self.combo_cpu_running_singleapp, 0, wx.EXPAND, 0)
        self.grid_running_singleapp.AddGrowableCol(1)
        self.sizer_singleapp.Add(self.grid_running_singleapp, 1, wx.ALL | wx.EXPAND, 5)
	self.sizer_singleapp.Show(self.grid_running_singleapp, False, True)

        self.singleapp_panel.SetSizer(self.sizer_singleapp)
        sizer_multiapp = wx.BoxSizer(wx.VERTICAL)
        self.grid_sizer_multiapp.Add(self.label_multiapp_file, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_multiapp.Add(self.browse_multiapp_file, 0, wx.EXPAND, 0)
        self.grid_sizer_multiapp.Add(self.label_cpu_multiapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_multiapp.Add(self.combo_cpu_multiapp, 0, wx.EXPAND, 0)
        self.grid_sizer_multiapp.AddGrowableCol(1)
        sizer_multiapp.Add(self.grid_sizer_multiapp, 1, wx.ALL | wx.EXPAND, 5)
        self.multiapp_panel.SetSizer(sizer_multiapp)
        separator.Add(self.tab_appmode, 7, wx.ALL | wx.EXPAND, 5)
        grid_sizer_graph_style.Add(self.button_graph_style, 0, wx.TOP | wx.EXPAND, 5)
        grid_sizer_graph_style.AddGrowableCol(0)
        sizer_graph_style.Add(grid_sizer_graph_style, 1, wx.ALL | wx.EXPAND, 5)
        sub_separator.Add(sizer_graph_style, 1, wx.RIGHT | wx.EXPAND, 5)
        grid_sizer_advanced_settings.Add(self.button_advanced_settings, 0, wx.TOP | wx.EXPAND, 5)
        grid_sizer_advanced_settings.AddGrowableCol(0)
        sizer_advanced_settings.Add(grid_sizer_advanced_settings, 1, wx.ALL | wx.EXPAND, 5)
        sub_separator.Add(sizer_advanced_settings, 1, wx.EXPAND, 0)
        separator.Add(sub_separator, 4, wx.ALL | wx.EXPAND, 5)

        sizer_controls.Add(self.button_prev, 0, wx.RIGHT | wx.BOTTOM, 5)
        sizer_controls.Add(self.button_monitoring, 0, wx.RIGHT | wx.BOTTOM, 5)
        separator.Add(sizer_controls, 0, wx.ALIGN_RIGHT, 0)
	self.panel.SetSizer(separator)

    def SaveUserConfig(self):
        del self.config_frame.user_config.applications[:]
	self.config_frame.user_config.pid_app_running = None
        if self.tab_appmode.GetSelection() == 0:
	    if self.radio_btn_app_norunning.GetValue():
            	self.config_frame.user_config.applications.append(self.text_path_singleapp.GetValue() + " " + self.text_args_singleapp.GetValue())
	    else:
		app_info = self.config_frame.pmc_connect.ReadFile("/proc/" + self.text_pid_singleapp.GetValue() + "/status")
		app_name = re.findall(r'Name:\t([\w\.\-\_\ \:\\]+)', app_info)[0]
            	self.config_frame.user_config.applications.append("/" + app_name)
	    	self.config_frame.user_config.pid_app_running = self.text_pid_singleapp.GetValue()
        else:
            multiapp_file = open(self.browse_multiapp_file.GetPath(), "r")
            for line in multiapp_file:
                self.config_frame.user_config.applications.append(line.strip())
            multiapp_file.close()

	# Save correct value for self.config_frame.user_config.cpu
	if self.tab_appmode.GetSelection() == 0:
		if self.radio_btn_app_norunning.GetValue() and self.combo_cpu_norunning_singleapp.FindString(self.combo_cpu_norunning_singleapp.GetValue()) != 0:
			self.config_frame.user_config.cpu = self.combo_cpu_norunning_singleapp.GetValue()
		elif not self.radio_btn_app_norunning.GetValue() and self.combo_cpu_running_singleapp.FindString(self.combo_cpu_running_singleapp.GetValue()) != 0:
			self.config_frame.user_config.cpu = self.combo_cpu_running_singleapp.GetValue()
		else:
			self.config_frame.user_config.cpu = None
	elif self.tab_appmode.GetSelection() == 1 and self.combo_cpu_multiapp.FindString(self.combo_cpu_multiapp.GetValue()) != 0:
		self.config_frame.user_config.cpu = self.combo_cpu_multiapp.GetValue()
        else:
		self.config_frame.user_config.cpu = None
	
	# Save advanced settings configuration
	self.config_frame.user_config.pmctrack_path = self.advanced_settings_dialog.GetPmctrackCommandPath()
	self.config_frame.user_config.time = self.advanced_settings_dialog.GetTimeBetweenSamples()
	self.config_frame.user_config.buffer_size = self.advanced_settings_dialog.GetSamplesBufferSize()
        self.config_frame.user_config.save_counters_log = self.advanced_settings_dialog.GetIfSaveCountersLog()
        self.config_frame.user_config.save_metrics_log = self.advanced_settings_dialog.GetIfSaveMetricsLog()
	self.config_frame.user_config.path_outfile_logs = self.advanced_settings_dialog.GetLogfilePath()
        self.config_frame.user_config.system_wide = self.advanced_settings_dialog.GetIfSystemWideMode()
	
	# Save graph style configuration
	bg_color = self.graph_style_dialog.GetBgColor()
	grid_color = self.graph_style_dialog.GetGridColor()
	line_color = self.graph_style_dialog.GetLineColor()
	line_style = self.graph_style_dialog.GetLineStyle()
	line_width = self.graph_style_dialog.GetLineWidth()
        line_style_number = self.graph_style_dialog.GetLineStyleNumber()
        mode_number = -1
        if self.graph_style_dialog.GetModeNumber() != wx.NOT_FOUND:
            mode_number = self.graph_style_dialog.GetModeNumber()
	self.config_frame.user_config.graph_style = GraphStyleConfig(bg_color, grid_color, line_color, line_style,
                line_width, line_style_number, mode_number)

    def __show_correct_sizer_singleapp(self):
	self.sizer_singleapp.Show(self.grid_running_singleapp, self.radio_btn_app_running.GetValue(), True)
	self.sizer_singleapp.Show(self.grid_sizer_singleapp, self.radio_btn_app_norunning.GetValue(), True)
	self.singleapp_panel.Layout()

    def on_change_type_app(self, event):
    	self.__show_correct_sizer_singleapp()
	
    def on_change_path_singleapp(self, event):
	self.text_path_singleapp.SetValue(self.ctrl_path_singleapp.GetPath())

    def on_change_multiapp_file(self, event):
        correct = True
        remote_str = ""
        multiapp_file = open(self.browse_multiapp_file.GetPath(), "r")
        for line in multiapp_file:
            if correct and (line == "" or not self.config_frame.pmc_connect.CheckFileExists(line.split()[0])):
                    correct = False
                    break
        multiapp_file.close()
        
        if not correct:
            if self.config_frame.user_config.machine.type_machine != "local":
                remote_str = _(" on remote machine")
            dlg = wx.MessageDialog(parent=None, message=_("Some of the applications specified in the multi-application file does not exist{0}.\nCheck the multi-application file.").format(remote_str), caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
            dlg.ShowModal()
            dlg.Destroy()
            self.browse_multiapp_file.SetPath("")

    def on_click_graph_style(self, event):
	if self.graph_style_dialog.ShowModal() == 0:
		self.button_graph_style.SetLabel(self.graph_style_dialog.GetModeName())

    def on_click_advanced_settings(self, event):
	self.advanced_settings_dialog.ShowModal()

    def on_click_prev(self, event):
        self.config_frame.GoToPanel(1)

    def on_click_monitoring(self, event):
    	if self.config_frame.panels[1].ValidateCountersAndMetricsConfig():
    		self.StartStopMonitoring()

    def StartStopMonitoring(self):
	if self.multiapp_frame == None and len(self.mon_frames) == 0:
		msg_error = ""
		if self.advanced_settings_dialog.GetPmctrackCommandPath() == "" or (self.advanced_settings_dialog.GetPmctrackCommandPath().find("/") >= 0 and not self.config_frame.pmc_connect.CheckFileExists(self.advanced_settings_dialog.GetPmctrackCommandPath())) or (self.advanced_settings_dialog.GetPmctrackCommandPath().find("/") < 0 and not self.config_frame.pmc_connect.CheckPkgInstalled(self.advanced_settings_dialog.GetPmctrackCommandPath(), not self.config_frame.user_config.machine.type_machine == "local")):
	            msg_error = _("Pmctrack command path is invalid.")
	        elif self.tab_appmode.GetSelection() == 0 and self.radio_btn_app_norunning.GetValue():
			if self.text_path_singleapp.GetValue() == "" or (self.text_path_singleapp.GetValue().find("/") >= 0 and not self.config_frame.pmc_connect.CheckFileExists(self.text_path_singleapp.GetValue())) or (self.text_path_singleapp.GetValue().find("/") < 0 and not self.config_frame.pmc_connect.CheckPkgInstalled(self.text_path_singleapp.GetValue(), not self.config_frame.user_config.machine.type_machine == "local")):
				msg_error = _("Can not find the application to monitoring.")
		elif self.tab_appmode.GetSelection() == 0 and self.radio_btn_app_running.GetValue() and not self.config_frame.pmc_connect.CheckFileExists("/proc/" + self.text_pid_singleapp.GetValue() + "/status"):
			msg_error = _("No application running with the PID indicated.")
		elif self.tab_appmode.GetSelection() == 1 and self.browse_multiapp_file.GetPath() == "":
			msg_error = _("You must select a valid multi-application file.")
		
		if msg_error != "":
			dlg = wx.MessageDialog(parent=None, message=msg_error, caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
			dlg.ShowModal()
			dlg.Destroy()
		else:
			self.StartMonitoring()
	else:
		self.StopMonitoring()

    def UpdateCtrlPathSingleApp(self):
	if self.config_frame.user_config.machine.type_machine == "local":
		self.ctrl_path_singleapp = self.browse_path_singleapp
		self.text_path_singleapp.Hide()
	else:
		self.ctrl_path_singleapp = self.text_path_singleapp
		self.browse_path_singleapp.Hide()
	self.grid_sizer_singleapp.Layout()

    def UpdateComboCPU(self):
	(vendor, flags, cores) = self.config_frame.facade_xml.getMachineInfo()
        self.combo_cpu_norunning_singleapp.Clear()
        self.combo_cpu_running_singleapp.Clear()
        self.combo_cpu_multiapp.Clear()
        self.combo_cpu_norunning_singleapp.Append(_("No binding"))
        self.combo_cpu_running_singleapp.Append(_("No binding"))
        self.combo_cpu_multiapp.Append(_("No binding"))
	for i in range(cores):
		self.combo_cpu_norunning_singleapp.Append(str(i))
		self.combo_cpu_running_singleapp.Append(str(i))
		self.combo_cpu_multiapp.Append(str(i))
        self.combo_cpu_norunning_singleapp.SetSelection(0)
        self.combo_cpu_running_singleapp.SetSelection(0)
        self.combo_cpu_multiapp.SetSelection(0)

    def StartMonitoring(self):
    	self.config_frame.SaveMonitoringSessionConfig()
	cp_usr_conf = self.config_frame.user_config.GetCopy()
        self.pmc_extract = PMCExtract(cp_usr_conf)
        if self.tab_appmode.GetSelection() == 0:
            app = self.pmc_extract.data.keys()[0]
	    mon_frame = MonitoringFrame(None, -1, "", app_name=app, final_panel=self, user_config=cp_usr_conf)
    	    mon_frame.Show()
        else: 
            self.multiapp_frame = MultiAppControlFrame(None, -1, "", final_panel=self, user_config=cp_usr_conf)
    	    self.multiapp_frame.Show()
	self.button_monitoring.SetLabel(_("Cancel monitoring"))

    def StopMonitoring(self, alert=True):
	continue_stop = True
	if self.pmc_extract != None and self.pmc_extract.app_running != None:
		if alert:
			msg = ""
			if self.pmc_extract.user_config.pid_app_running == None:
				msg = _("You are about to cancel monitoring, killing the current application and closing windows monitoring.\n\nAre you sure you wanna do this?.")
			else:
				msg = _("You are about to cancel monitoring, closing windows monitoring but without killing the monitored application.\n\nAre you sure you wanna do this?.")
			dlg = wx.MessageDialog(parent=None, message=msg, caption=_("Advertisement"), style=wx.YES_NO | wx.ICON_EXCLAMATION)
            		if dlg.ShowModal() == wx.ID_YES:
				self.pmc_extract.KillMonitoringRequest()
			else:
				continue_stop = False
		else:
			self.pmc_extract.KillMonitoringRequest()
	if continue_stop:
		for mon_frame in self.mon_frames:
                        mon_frame.graph_style_dialog.Destroy()
			mon_frame.Destroy()
		del self.mon_frames[:]
                if self.multiapp_frame != None:
                    self.multiapp_frame.Destroy()
                    self.multiapp_frame = None
		self.button_monitoring.SetLabel(_("Start monitoring"))

    def ConfigToXML(self, xml_root):
	if self.tab_appmode.GetSelection() == 0:
    		ET.SubElement(xml_root, "appmode_index").text = "0"
		if self.radio_btn_app_norunning.GetValue():
    			ET.SubElement(xml_root, "singleapp_running").text = "False"
    			ET.SubElement(xml_root, "singleapp_path").text = self.text_path_singleapp.GetValue()
    			ET.SubElement(xml_root, "singleapp_args").text = self.text_args_singleapp.GetValue()
			if self.combo_cpu_norunning_singleapp.FindString(self.combo_cpu_norunning_singleapp.GetValue()) == 0:
    				ET.SubElement(xml_root, "cpu_bind").text = None
			else:
				ET.SubElement(xml_root, "cpu_bind").text = self.combo_cpu_norunning_singleapp.GetValue()
		else:
    			ET.SubElement(xml_root, "singleapp_running").text = "True"
    			ET.SubElement(xml_root, "singleapp_pid").text = self.text_pid_singleapp.GetValue()
			if self.combo_cpu_running_singleapp.FindString(self.combo_cpu_running_singleapp.GetValue()) == 0:
    				ET.SubElement(xml_root, "cpu_bind").text = None
			else:
				ET.SubElement(xml_root, "cpu_bind").text = self.combo_cpu_running_singleapp.GetValue()
	else:
    		ET.SubElement(xml_root, "appmode_index").text = "1"
    		ET.SubElement(xml_root, "multiapp_file_path").text = self.browse_multiapp_file.GetPath()
		if self.combo_cpu_multiapp.FindString(self.combo_cpu_multiapp.GetValue()) == 0:
    			ET.SubElement(xml_root, "cpu_bind").text = None
		else:
    			ET.SubElement(xml_root, "cpu_bind").text = self.combo_cpu_multiapp.GetValue()

    	# Save advanced settings configuration
	ET.SubElement(xml_root, "pmctrack_command_path").text = self.advanced_settings_dialog.GetPmctrackCommandPath()
	ET.SubElement(xml_root, "time").text = str(self.advanced_settings_dialog.GetTimeBetweenSamples())
	ET.SubElement(xml_root, "buffer_size").text = str(self.advanced_settings_dialog.GetSamplesBufferSize())
	ET.SubElement(xml_root, "save_counters_log").text = str(self.advanced_settings_dialog.GetIfSaveCountersLog())
	ET.SubElement(xml_root, "save_metrics_log").text = str(self.advanced_settings_dialog.GetIfSaveMetricsLog())
	ET.SubElement(xml_root, "logfile_path").text = self.advanced_settings_dialog.GetLogfilePath()
	ET.SubElement(xml_root, "system_wide").text = str(self.advanced_settings_dialog.GetIfSystemWideMode())

	# Save graph style configuration
	mode_number = -1
	if self.graph_style_dialog.GetModeNumber() != wx.NOT_FOUND:
		mode_number = self.graph_style_dialog.GetModeNumber()
	ET.SubElement(xml_root, "mode_number").text = str(mode_number)
	if mode_number < 0:
		ET.SubElement(xml_root, "bg_color").text = self.graph_style_dialog.GetBgColor()
		ET.SubElement(xml_root, "grid_color").text = self.graph_style_dialog.GetGridColor()
		ET.SubElement(xml_root, "line_color").text = self.graph_style_dialog.GetLineColor()
		ET.SubElement(xml_root, "line_style_index").text = str(self.graph_style_dialog.GetLineStyleNumber())
		ET.SubElement(xml_root, "line_width").text = str(self.graph_style_dialog.GetLineWidth())

    def ConfigFromXML(self, xml_root):
    	appmode_index = int(xml_root.find("appmode_index").text)
	cpu_bind = xml_root.find("cpu_bind").text
	self.tab_appmode.SetSelection(appmode_index)
	if appmode_index == 0: # Single app mode
		if xml_root.find("singleapp_running").text == "False":
			self.radio_btn_app_norunning.SetValue(True)
			if xml_root.find("singleapp_path").text != None:
				if self.browse_path_singleapp.IsShown():
					self.browse_path_singleapp.SetPath(xml_root.find("singleapp_path").text)
				self.text_path_singleapp.SetValue(xml_root.find("singleapp_path").text)
			if xml_root.find("singleapp_args").text != None:
				self.text_args_singleapp.SetValue(xml_root.find("singleapp_args").text)
			if cpu_bind != None:
				self.combo_cpu_norunning_singleapp.SetValue(cpu_bind)
		else:
			self.radio_btn_app_running.SetValue(True)
			if xml_root.find("singleapp_pid").text != None:
				self.text_pid_singleapp.SetValue(xml_root.find("singleapp_pid").text)
			if cpu_bind != None:
				self.combo_cpu_running_singleapp.SetValue(cpu_bind)
    		self.__show_correct_sizer_singleapp()
	else: # Multi app mode
		if xml_root.find("multiapp_file_path").text != None:
			self.browse_multiapp_file.SetPath(xml_root.find("multiapp_file_path").text)
		if cpu_bind != None:
			self.combo_cpu_multiapp.SetValue(cpu_bind)

    	# Load advanced settings configuration
	self.advanced_settings_dialog.SetPmctrackCommandPath(xml_root.find("pmctrack_command_path").text)
	self.advanced_settings_dialog.SetTimeBetweenSamples(int(xml_root.find("time").text))
	self.advanced_settings_dialog.SetSamplesBufferSize(int(xml_root.find("buffer_size").text))
	self.advanced_settings_dialog.SetIfSaveCountersLog(xml_root.find("save_counters_log").text == "True")
	self.advanced_settings_dialog.SetIfSaveMetricsLog(xml_root.find("save_metrics_log").text == "True")
	self.advanced_settings_dialog.SetLogfilePath(xml_root.find("logfile_path").text)
	self.advanced_settings_dialog.SetIfSystemWideMode(xml_root.find("system_wide").text == "True")
	self.advanced_settings_dialog.UpdateEnabledPathSaveLogs()

	# Load graph style configuration
	mode_number = int(xml_root.find("mode_number").text)
	if mode_number >= 0:
		self.graph_style_dialog.SetModeNumber(mode_number)
	else:
		bg_color = xml_root.find("bg_color").text
		grid_color = xml_root.find("grid_color").text
		line_color = xml_root.find("line_color").text
		line_style_index = int(xml_root.find("line_style_index").text)
		line_width = int(xml_root.find("line_width").text)
		self.graph_style_dialog.SetCustomizedMode(bg_color, grid_color, line_color, line_style_index, line_width)
	self.button_graph_style.SetLabel(self.graph_style_dialog.GetModeName())

    def DestroyComponents(self):
	self.StopMonitoring(False)
	self.graph_style_dialog.Destroy()
	self.advanced_settings_dialog.Destroy()
	self.panel.Destroy()
