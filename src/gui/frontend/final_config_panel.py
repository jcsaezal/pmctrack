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
from frontend.monitoring_frame import *
from frontend.graph_style_dialog import GraphStyleDialog

class FinalConfigPanel():
    def __init__(self, config_frame):
        self.config_frame = config_frame
	self.panel = wx.Panel(self.config_frame, -1)

	# List of monitoring frames actually in memory.
	self.mon_frames = []
	# Stores the object manager to extract all the information pmctrack command.
	self.pmc_extract = None

        self.label_benchmark = wx.StaticText(self.panel, -1, _("Path to application") + ": ")
	self.browse_benchmark = wx.FilePickerCtrl(self.panel, -1,"", _("Select application to monitor"))
        self.text_benchmark = wx.TextCtrl(self.panel, -1, "")

	if self.config_frame.user_config.machine.is_remote:
		self.ctrl_benchmark = self.text_benchmark
		self.browse_benchmark.Hide()
	else:
		self.ctrl_benchmark = self.browse_benchmark
		self.text_benchmark.Hide()
	
        self.label_args_benchmark = wx.StaticText(self.panel, -1, _("Application's arguments") + ": ")
	self.text_args_benchmark = wx.TextCtrl(self.panel, -1, "")
	self.label_cpu = wx.StaticText(self.panel, -1, _("Select CPU to bind or type mask") + ": ")

	cpu_choices = [_("No binding")]
	(vendor, flags, cores) = self.config_frame.facade_xml.getMachineInfo()
	for i in range(cores):
		cpu_choices.append(str(i))

        self.combo_cpu = wx.ComboBox(self.panel, -1, choices=cpu_choices, style=wx.CB_DROPDOWN)
        self.sizer_benchmark_staticbox = wx.StaticBox(self.panel, -1, _("Select application to monitor"))
        self.grid_sizer_benchmark = wx.FlexGridSizer(3, 2, 5, 0)
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

	self.graph_style_dialog = GraphStyleDialog(None, -1, "")
        self.button_prev = wx.Button(self.panel, -1, "< " + _("Back"))
        self.button_monitoring = wx.Button(self.panel, -1, _("Start monitoring"))

        self.__set_properties()
        self.__do_layout()

	self.ctrl_benchmark.Bind(wx.EVT_FILEPICKER_CHANGED, self.on_change_benchmark)
	self.checkbox_save_counters.Bind(wx.EVT_CHECKBOX, self.on_change_log_checkboxs)
	self.checkbox_save_metrics.Bind(wx.EVT_CHECKBOX, self.on_change_log_checkboxs)
	self.button_graph_style.Bind(wx.EVT_BUTTON, self.on_click_graph_style)
	self.button_prev.Bind(wx.EVT_BUTTON, self.on_click_prev)
	self.button_monitoring.Bind(wx.EVT_BUTTON, self.on_click_monitoring)

    def __set_properties(self):
        self.combo_cpu.SetSelection(0)
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
        self.sizer_benchmark_staticbox.Lower()
        sizer_benchmark = wx.StaticBoxSizer(self.sizer_benchmark_staticbox, wx.VERTICAL)
        self.grid_sizer_benchmark.Add(self.label_benchmark, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_benchmark.Add(self.ctrl_benchmark, 0, wx.EXPAND, 0)
        self.grid_sizer_benchmark.Add(self.label_args_benchmark, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_benchmark.Add(self.text_args_benchmark, 0, wx.EXPAND, 0)
        self.grid_sizer_benchmark.Add(self.label_cpu, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        self.grid_sizer_benchmark.Add(self.combo_cpu, 0, wx.EXPAND, 0)
        self.grid_sizer_benchmark.AddGrowableCol(1)
        sizer_benchmark.Add(self.grid_sizer_benchmark, 1, wx.ALL | wx.EXPAND, 5)
        separator.Add(sizer_benchmark, 6, wx.ALL | wx.EXPAND, 5)
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
	self.config_frame.user_config.path_benchmark = self.text_benchmark.GetValue()
	self.config_frame.user_config.args_benchmark = self.text_args_benchmark.GetValue()

	if self.combo_cpu.FindString(self.combo_cpu.GetValue()) != 0:
		self.config_frame.user_config.cpu = self.combo_cpu.GetValue()
	
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
	
    def on_change_benchmark(self, event):
	self.text_benchmark.SetValue(self.ctrl_benchmark.GetPath())

    def on_change_log_checkboxs(self, event):
	self.path_save.Enable(self.checkbox_save_counters.GetValue() or self.checkbox_save_metrics.GetValue())

    def on_click_graph_style(self, event):
	if self.graph_style_dialog.ShowModal() == 0:
		self.button_graph_style.SetLabel(self.graph_style_dialog.GetModeName())

    def on_click_prev(self, event):
        self.config_frame.GoToPanel(1)

    def on_click_monitoring(self, event):
	if len(self.mon_frames) == 0:
		self.StartMonitoring()
	else:
		self.StopMonitoring()

    def UpdateCtrlBenchmark(self):
	if self.config_frame.user_config.machine.is_remote:
		self.ctrl_benchmark = self.text_benchmark
		self.browse_benchmark.Hide()
	else:
		self.ctrl_benchmark = self.browse_benchmark
		self.text_benchmark.Hide()
	self.grid_sizer_benchmark.Layout()

    def StartMonitoring(self):
	self.__save_user_config()
	cp_usr_conf = self.config_frame.user_config.GetCopy()
        self.pmc_extract = PMCExtract(cp_usr_conf)
	if self.pmc_extract.error is None:
		mon_frame = MonitoringFrame(None, -1, "", version=self.config_frame.version, final_panel=self, user_config=cp_usr_conf)
    		mon_frame.Show()
		self.button_monitoring.SetLabel(_("Cancel monitoring"))
	else:
		if self.pmc_extract.error != "": error_msg = _("PMCTrack error") + ":\n" + self.pmc_extract.error
		else: error_msg = _("The current configuration precludes monitoring.\nCheck all the settings are correct.")
		dlg = wx.MessageDialog(parent=None, message=error_msg, caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
		dlg.ShowModal()

    def StopMonitoring(self, alert=True):
	continue_stop = True
	if self.pmc_extract != None and self.pmc_extract.state in ['R', 'S']:
		if alert:
			dlg = wx.MessageDialog(parent=None, message=_("You are about to cancel monitoring, killing the application and closing windows monitoring.\n\nAre you sure you wanna do this?."),
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
		self.button_monitoring.SetLabel(_("Start monitoring"))

    def DestroyComponents(self):
	self.StopMonitoring(False)
	self.graph_style_dialog.Destroy()
