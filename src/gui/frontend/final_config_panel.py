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
from backend.facade_xml import *
from backend.user_config import *
from backend.pmc_extract import *
from frontend.multiapp_control_frame import *
from frontend.monitoring_frame import *
from frontend.graph_style_dialog import GraphStyleDialog

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

        self.label_path_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Path to application to monitor") + ": ")
	self.browse_path_singleapp = wx.FilePickerCtrl(self.singleapp_panel, -1,"", _("Select application"))
        self.text_path_singleapp = wx.TextCtrl(self.singleapp_panel, -1, "")

	if self.config_frame.user_config.machine.is_remote:
		self.ctrl_path_singleapp = self.text_path_singleapp
		self.browse_path_singleapp.Hide()
	else:
		self.ctrl_path_singleapp = self.browse_path_singleapp
		self.text_path_singleapp.Hide()
	
        self.label_args_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Application's arguments") + ": ")
	self.text_args_singleapp = wx.TextCtrl(self.singleapp_panel, -1, "")
	self.label_cpu_singleapp = wx.StaticText(self.singleapp_panel, -1, _("Select CPU to bind or type mask") + ": ")
	self.combo_cpu_singleapp = wx.ComboBox(self.singleapp_panel, -1, choices=cpu_choices, style=wx.CB_DROPDOWN)
        self.grid_sizer_singleapp = wx.FlexGridSizer(3, 2, 5, 0)

        self.label_multiapp_file = wx.StaticText(self.multiapp_panel, -1, _("Select multi-application file") + ": ")
	self.browse_multiapp_file = wx.FilePickerCtrl(self.multiapp_panel, -1,"", _("Select multi-application file"))
	self.label_cpu_multiapp = wx.StaticText(self.multiapp_panel, -1, _("Select CPU to bind or type mask") + ": ")
        self.combo_cpu_multiapp = wx.ComboBox(self.multiapp_panel, -1, choices=cpu_choices, style=wx.CB_DROPDOWN)
        self.grid_sizer_multiapp = wx.FlexGridSizer(3, 2, 5, 0)

        self.label_time_samples = wx.StaticText(self.panel, -1, _("Time between samples (in milliseconds)") + ": ")
        self.spin_ctrl_time_samples = wx.SpinCtrl(self.panel, -1, "1000", min=100, max=5000)
        self.label_buffer_size = wx.StaticText(self.panel, -1, _("Samples buffer size (in bytes, 0 for unspecified)") + ": ")
        self.spin_ctrl_buffer_size = wx.SpinCtrl(self.panel, -1, "0", min=0, max=10000000)
        self.sizer_samples_staticbox = wx.StaticBox(self.panel, -1, _("Samples configuration"))
        self.sizer_counter_mode_staticbox = wx.StaticBox(self.panel, -1, _("Select counter mode"))
        self.label_counter_mode = wx.StaticText(self.panel, -1, _("What counter mode you want to use?"))
        self.radio_btn_per_thread = wx.RadioButton(self.panel, -1, _("Per-thread mode"))
        self.radio_btn_system_wide = wx.RadioButton(self.panel, -1, _("System-wide mode"))
        self.label_save = wx.StaticText(self.panel, -1, _("What monitoring logs want to save?"))
        self.checkbox_save_counters = wx.CheckBox(self.panel, -1, _("Counters samples log"))
        self.checkbox_save_metrics = wx.CheckBox(self.panel, -1, _("Metrics samples log"))
        self.label_path_save = wx.StaticText(self.panel, -1, _("Path to save monitoring logs") + ": ")
        self.path_save = wx.DirPickerCtrl(self.panel, -1, "", _("Select directory where to save the output file"))
        self.sizer_save_staticbox = wx.StaticBox(self.panel, -1, _("Save monitoring logs into files"))
        self.label_graph_style = wx.StaticText(self.panel, -1, _("Graph style mode") + ": ")
        self.button_graph_style = wx.Button(self.panel, -1, _("Default"))
        self.sizer_graph_style_staticbox = wx.StaticBox(self.panel, -1, _("Select graph style mode or customize one"))
        self.multiapp_frame = None

	self.graph_style_dialog = GraphStyleDialog(None, -1, "")
        self.button_prev = wx.Button(self.panel, -1, "< " + _("Back"))
        self.button_monitoring = wx.Button(self.panel, -1, _("Start monitoring"))

        self.__set_properties()
        self.__do_layout()

	self.ctrl_path_singleapp.Bind(wx.EVT_FILEPICKER_CHANGED, self.on_change_path_singleapp)
	self.browse_multiapp_file.Bind(wx.EVT_FILEPICKER_CHANGED, self.on_change_multiapp_file)
	self.checkbox_save_counters.Bind(wx.EVT_CHECKBOX, self.on_change_log_checkboxs)
	self.checkbox_save_metrics.Bind(wx.EVT_CHECKBOX, self.on_change_log_checkboxs)
	self.button_graph_style.Bind(wx.EVT_BUTTON, self.on_click_graph_style)
	self.button_prev.Bind(wx.EVT_BUTTON, self.on_click_prev)
	self.button_monitoring.Bind(wx.EVT_BUTTON, self.on_click_monitoring)

    def __set_properties(self):
        self.tab_appmode.AddPage(self.singleapp_panel, _("Single-app mode"))
        self.tab_appmode.AddPage(self.multiapp_panel, _("Multi-app mode"))
        self.tab_appmode.SetSelection(0)
        self.combo_cpu_singleapp.SetSelection(0)
        self.combo_cpu_multiapp.SetSelection(0)
        self.radio_btn_per_thread.SetValue(1)
        self.path_save.Enable(False)
	self.path_save.SetPath("/tmp")
        self.button_prev.SetMinSize((225, 42))
        self.button_monitoring.SetMinSize((225, 42))
	font_button_monitoring = self.button_monitoring.GetFont();
	font_button_monitoring.SetWeight(wx.FONTWEIGHT_BOLD);
	self.button_monitoring.SetFont(font_button_monitoring)

    def __do_layout(self):
        separator = wx.BoxSizer(wx.VERTICAL)
        sizer_controls = wx.BoxSizer(wx.HORIZONTAL)
        self.sizer_save_staticbox.Lower()
        sizer_save = wx.StaticBoxSizer(self.sizer_save_staticbox, wx.VERTICAL)
        grid_sizer_save = wx.FlexGridSizer(2, 2, 5, 0)
        sizer_radio_save = wx.BoxSizer(wx.HORIZONTAL)
        self.sizer_samples_staticbox.Lower()
        sizer_samples = wx.StaticBoxSizer(self.sizer_samples_staticbox, wx.VERTICAL)
        grid_sizer_samples = wx.FlexGridSizer(2, 2, 5, 0)
	self.sizer_counter_mode_staticbox.Lower()
        sizer_counter_mode = wx.StaticBoxSizer(self.sizer_counter_mode_staticbox, wx.VERTICAL)
        grid_sizer_counter_mode = wx.FlexGridSizer(1, 2, 0, 0)
        sizer_radio_counter_mode = wx.BoxSizer(wx.HORIZONTAL)
	self.sizer_graph_style_staticbox.Lower()
        sizer_graph_style = wx.StaticBoxSizer(self.sizer_graph_style_staticbox, wx.VERTICAL)
        grid_sizer_graph_style = wx.FlexGridSizer(1, 2, 0, 0)
        sizer_singleapp = wx.BoxSizer(wx.VERTICAL)
        self.grid_sizer_singleapp.Add(self.label_path_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_singleapp.Add(self.ctrl_path_singleapp, 0, wx.EXPAND, 0)
        self.grid_sizer_singleapp.Add(self.label_args_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_singleapp.Add(self.text_args_singleapp, 0, wx.EXPAND, 0)
        self.grid_sizer_singleapp.Add(self.label_cpu_singleapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_singleapp.Add(self.combo_cpu_singleapp, 0, wx.EXPAND, 0)
        self.grid_sizer_singleapp.AddGrowableCol(1)
        sizer_singleapp.Add(self.grid_sizer_singleapp, 1, wx.ALL | wx.EXPAND, 5)
        self.singleapp_panel.SetSizer(sizer_singleapp)
        sizer_multiapp = wx.BoxSizer(wx.VERTICAL)
        self.grid_sizer_multiapp.Add(self.label_multiapp_file, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_multiapp.Add(self.browse_multiapp_file, 0, wx.EXPAND, 0)
        self.grid_sizer_multiapp.Add(self.label_cpu_multiapp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_multiapp.Add(self.combo_cpu_multiapp, 0, wx.EXPAND, 0)
        self.grid_sizer_multiapp.AddGrowableCol(1)
        sizer_multiapp.Add(self.grid_sizer_multiapp, 1, wx.ALL | wx.EXPAND, 5)
        self.multiapp_panel.SetSizer(sizer_multiapp)
        separator.Add(self.tab_appmode, 6, wx.ALL | wx.EXPAND, 5)
        grid_sizer_samples.Add(self.label_time_samples, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_samples.Add(self.spin_ctrl_time_samples, 0, wx.EXPAND, 0)
        grid_sizer_samples.Add(self.label_buffer_size, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_samples.Add(self.spin_ctrl_buffer_size, 0, wx.EXPAND, 0)
        grid_sizer_samples.AddGrowableCol(1)
        sizer_samples.Add(grid_sizer_samples, 1, wx.ALL | wx.EXPAND, 5)
        separator.Add(sizer_samples, 5, wx.ALL | wx.EXPAND, 5)
        grid_sizer_counter_mode.Add(self.label_counter_mode, 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_radio_counter_mode.Add(self.radio_btn_per_thread, 0, 0, 0)
        sizer_radio_counter_mode.Add(self.radio_btn_system_wide, 0, wx.LEFT, 10)
        grid_sizer_counter_mode.Add(sizer_radio_counter_mode, 1, wx.EXPAND, 0)
        grid_sizer_counter_mode.AddGrowableCol(1)
        sizer_counter_mode.Add(grid_sizer_counter_mode, 1, wx.ALL | wx.EXPAND, 5)
        separator.Add(sizer_counter_mode, 5, wx.ALL | wx.EXPAND, 5)
        grid_sizer_save.Add(self.label_save, 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_radio_save.Add(self.checkbox_save_counters, 0, 0, 0)
        sizer_radio_save.Add(self.checkbox_save_metrics, 0, wx.LEFT, 10)
        grid_sizer_save.Add(sizer_radio_save, 1, wx.EXPAND, 0)
        grid_sizer_save.Add(self.label_path_save, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_save.Add(self.path_save, 0, wx.EXPAND, 0)
        grid_sizer_save.AddGrowableCol(1)
        sizer_save.Add(grid_sizer_save, 1, wx.ALL | wx.EXPAND, 5)
        separator.Add(sizer_save, 5, wx.ALL | wx.EXPAND, 5)
	grid_sizer_graph_style.Add(self.label_graph_style, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_graph_style.Add(self.button_graph_style, 0, wx.EXPAND, 0)
        grid_sizer_graph_style.AddGrowableCol(1)
        sizer_graph_style.Add(grid_sizer_graph_style, 1, wx.ALL | wx.EXPAND, 5)
        separator.Add(sizer_graph_style, 4, wx.ALL | wx.EXPAND, 5)
        sizer_controls.Add(self.button_prev, 0, wx.RIGHT | wx.BOTTOM, 5)
        sizer_controls.Add(self.button_monitoring, 0, wx.RIGHT | wx.BOTTOM, 5)
        separator.Add(sizer_controls, 0, wx.ALIGN_RIGHT, 0)
	self.panel.SetSizer(separator)

    def __save_user_config(self):
        del self.config_frame.user_config.applications[:]
        if self.tab_appmode.GetSelection() == 0:
            self.config_frame.user_config.applications.append(self.text_path_singleapp.GetValue() + " " + self.text_args_singleapp.GetValue())
        else:
            multiapp_file = open(self.browse_multiapp_file.GetPath(), "r")
            for line in multiapp_file:
                self.config_frame.user_config.applications.append(line.strip())
            multiapp_file.close()

	if self.tab_appmode.GetSelection() == 0 and self.combo_cpu_singleapp.FindString(self.combo_cpu_singleapp.GetValue()) != 0:
		self.config_frame.user_config.cpu = self.combo_cpu_singleapp.GetValue()
	elif self.tab_appmode.GetSelection() == 1 and self.combo_cpu_multiapp.FindString(self.combo_cpu_multiapp.GetValue()) != 0:
		self.config_frame.user_config.cpu = self.combo_cpu_multiapp.GetValue()
        else:
		self.config_frame.user_config.cpu = None
	
	self.config_frame.user_config.time = self.spin_ctrl_time_samples.GetValue()
	self.config_frame.user_config.buffer_size = self.spin_ctrl_buffer_size.GetValue()
        self.config_frame.user_config.save_counters_log = self.checkbox_save_counters.GetValue()
        self.config_frame.user_config.save_metrics_log = self.checkbox_save_metrics.GetValue()
	self.config_frame.user_config.path_outfile_logs = self.path_save.GetPath()
        self.config_frame.user_config.system_wide = self.radio_btn_system_wide.GetValue()
	
	# Save graph style user configuration
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
            if self.config_frame.user_config.machine.is_remote:
                remote_str = _(" on the remote machine")
            dlg = wx.MessageDialog(parent=None, message=_("Some of the applications specified in the multi-application file does not exist{0}.\nCheck the multi-application file.").format(remote_str), caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
            dlg.ShowModal()
            dlg.Destroy()
            self.browse_multiapp_file.SetPath("")

    def on_change_log_checkboxs(self, event):
	self.path_save.Enable(self.checkbox_save_counters.GetValue() or self.checkbox_save_metrics.GetValue())

    def on_click_graph_style(self, event):
	if self.graph_style_dialog.ShowModal() == 0:
		self.button_graph_style.SetLabel(self.graph_style_dialog.GetModeName())

    def on_click_prev(self, event):
        self.config_frame.GoToPanel(1)

    def on_click_monitoring(self, event):
	if self.multiapp_frame == None and len(self.mon_frames) == 0:
		self.StartMonitoring()
	else:
		self.StopMonitoring()

    def UpdateCtrlPathSingleApp(self):
	if self.config_frame.user_config.machine.is_remote:
		self.ctrl_path_singleapp = self.text_path_singleapp
		self.browse_path_singleapp.Hide()
	else:
		self.ctrl_path_singleapp = self.browse_path_singleapp
		self.text_path_singleapp.Hide()
	self.grid_sizer_singleapp.Layout()

    def UpdateComboCPU(self):
	(vendor, flags, cores) = self.config_frame.facade_xml.getMachineInfo()
        self.combo_cpu_singleapp.Clear()
        self.combo_cpu_multiapp.Clear()
        self.combo_cpu_singleapp.Append(_("No binding"))
        self.combo_cpu_multiapp.Append(_("No binding"))
	for i in range(cores):
		self.combo_cpu_singleapp.Append(str(i))
		self.combo_cpu_multiapp.Append(str(i))
        self.combo_cpu_singleapp.SetSelection(0)
        self.combo_cpu_multiapp.SetSelection(0)

    def StartMonitoring(self):
        msg_error = ""
        if self.tab_appmode.GetSelection() == 0 and not self.config_frame.pmc_connect.CheckFileExists(self.text_path_singleapp.GetValue()):
            msg_error = _("Can not find any application in the specified path.")
        elif self.tab_appmode.GetSelection() == 1 and self.browse_multiapp_file.GetPath() == "":
            msg_error = _("You must select a valid multi-application file.")
        if msg_error != "":
            dlg = wx.MessageDialog(parent=None, message=msg_error, caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
            dlg.ShowModal()
            dlg.Destroy()
        else:
	    self.__save_user_config()
	    cp_usr_conf = self.config_frame.user_config.GetCopy()
            self.pmc_extract = PMCExtract(cp_usr_conf)
            if self.tab_appmode.GetSelection() == 0:
                app = self.pmc_extract.data.keys()[0]
	        mon_frame = MonitoringFrame(None, -1, "", app_name=app, version=self.config_frame.version, final_panel=self, user_config=cp_usr_conf)
    	        mon_frame.Show()
            else: 
                self.multiapp_frame = MultiAppControlFrame(None, -1, "", version=self.config_frame.version, final_panel=self, user_config=cp_usr_conf)
    	        self.multiapp_frame.Show()
	    self.button_monitoring.SetLabel(_("Cancel monitoring"))

    def StopMonitoring(self, alert=True):
	continue_stop = True
	if self.pmc_extract != None and self.pmc_extract.app_running != None:
		if alert:
			dlg = wx.MessageDialog(parent=None, message=_("You are about to cancel monitoring, killing the current application and closing windows monitoring.\n\nAre you sure you wanna do this?."),
                		caption=_("Advertisement"), style=wx.YES_NO | wx.ICON_EXCLAMATION)
            		if dlg.ShowModal() == wx.ID_YES:
				self.pmc_extract.KillMonitoring()
			else:
				continue_stop = False
		else:
			self.pmc_extract.KillMonitoring()
	if continue_stop:
		for mon_frame in self.mon_frames:
                        mon_frame.graph_style_dialog.Destroy()
			mon_frame.Destroy()
		del self.mon_frames[:]
                if self.multiapp_frame != None:
                    self.multiapp_frame.Destroy()
                    self.multiapp_frame = None
		self.button_monitoring.SetLabel(_("Start monitoring"))

    def DestroyComponents(self):
	self.StopMonitoring(False)
	self.graph_style_dialog.Destroy()
