#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
# pmc_frame_final_conf.py
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
from frames.pmc_frame_counters import *
from frames.pmc_frame_graph import *
from frames.pmc_dialog_graph_style import PMCStyleGraph

class PMCFrameFinalConf(wx.Frame):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Frame.__init__(self, *args, **kwds)

	self.version = kwargs.get("version")
	self.user_config = kwargs.get("user_config")
	self.facade_xml = kwargs.get("facade_xml")

	# List of monitoring frames actually in memory.
	self.graph_frames = []
	# Stores the object manager to extract all the information pmctrack command.
	self.pmc_extract = None

	self.panel = wx.Panel(self, -1)
        self.label_benchmark = wx.StaticText(self.panel, -1, _("Path to application") + ": ")
	self.browse_benchmark = wx.FilePickerCtrl(self.panel, -1,"", _("Select application to monitor"))
        self.text_benchmark = wx.TextCtrl(self.panel, -1, "")

	if self.user_config.machine.is_remote:
		self.ctrl_benchmark = self.text_benchmark
		self.browse_benchmark.Hide()
	else:
		self.ctrl_benchmark = self.browse_benchmark
		self.text_benchmark.Hide()
	
        self.label_args_benchmark = wx.StaticText(self.panel, -1, _("Application's arguments") + ": ")
	self.text_args_benchmark = wx.TextCtrl(self.panel, -1, "")
	self.label_cpu = wx.StaticText(self.panel, -1, _("Select CPU to bind or type mask") + ": ")

	cpu_choices = [_("No binding")]
	(vendor, flags, cores) = self.facade_xml.getMachineInfo()
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
        self.label_counter_mode = wx.StaticText(self.sizer_counter_mode_staticbox, -1, _("What counter mode you want to use?"))
        self.radio_btn_per_thread = wx.RadioButton(self.sizer_counter_mode_staticbox, -1, _("Per-thread mode"))
        self.radio_btn_system_wide = wx.RadioButton(self.sizer_counter_mode_staticbox, -1, _("System-wide mode"))
        self.label_save = wx.StaticText(self.panel, -1, _("Save monitoring results?"))
        self.radio_btn_save_yes = wx.RadioButton(self.panel, -1, _("Yes"))
        self.radio_btn_save_no = wx.RadioButton(self.panel, -1, _("No"))
        self.label_path_save = wx.StaticText(self.panel, -1, _("Path to the output file") + ": ")
        self.path_save = wx.DirPickerCtrl(self.panel, -1, "", _("Select directory where to save the output file"))
        self.sizer_save_staticbox = wx.StaticBox(self.panel, -1, _("Save monitoring results into a file"))
        self.label_graph_style = wx.StaticText(self.panel, -1, _("Graph style mode") + ": ")
        self.button_graph_style = wx.Button(self.panel, -1, _("Default"))
        self.sizer_graph_style_staticbox = wx.StaticBox(self.panel, -1, _("Select graph style mode or customize one"))
	self.dialog_graph_style = PMCStyleGraph(None, -1, "")
        self.button_prev = wx.Button(self.panel, -1, "< " + _("Back"))
        self.button_monitoring = wx.Button(self.panel, -1, _("Start monitoring"))
	self.prev_frame = None

        self.__set_properties()
        self.__do_layout()

	self.Bind(wx.EVT_FILEPICKER_CHANGED, self.on_change_benchmark, self.ctrl_benchmark)
	self.Bind(wx.EVT_RADIOBUTTON, self.on_change_radio_outfile, self.radio_btn_save_yes)
	self.Bind(wx.EVT_RADIOBUTTON, self.on_change_radio_outfile, self.radio_btn_save_no)
	self.Bind(wx.EVT_BUTTON, self.on_click_graph_style, self.button_graph_style)
	self.Bind(wx.EVT_BUTTON, self.on_click_prev, self.button_prev)
	self.Bind(wx.EVT_BUTTON, self.on_click_monitoring, self.button_monitoring)
	self.Bind(wx.EVT_CLOSE, self.on_close_frame)

    def UpdateCtrlBenchmark(self):
	if self.user_config.machine.is_remote:
		self.ctrl_benchmark = self.text_benchmark
		self.browse_benchmark.Hide()
	else:
		self.ctrl_benchmark = self.browse_benchmark
		self.text_benchmark.Hide()
	self.grid_sizer_benchmark.Layout()

    def __set_properties(self):
        self.SetTitle("PMCTrack-GUI v" + self.version + " - " + _("Final monitoring configuration"))
        self.combo_cpu.SetSelection(0)
        self.radio_btn_save_no.SetValue(1)
        self.radio_btn_per_thread.SetValue(1)
        self.path_save.Enable(False)
	self.path_save.SetPath("/tmp")
        self.button_prev.SetMinSize((160, 42))
        self.button_monitoring.SetMinSize((250, 42))
	font_button_monitoring = self.button_monitoring.GetFont();
	font_button_monitoring.SetWeight(wx.FONTWEIGHT_BOLD);
	self.button_monitoring.SetFont(font_button_monitoring)

    def __do_layout(self):
	sizer_panel = wx.BoxSizer(wx.VERTICAL)
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
        sizer_radio_save.Add(self.radio_btn_save_yes, 0, 0, 0)
        sizer_radio_save.Add(self.radio_btn_save_no, 0, wx.LEFT, 10)
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
	sizer_panel.Add(self.panel, 1, wx.EXPAND, 0)
        self.SetSizer(sizer_panel)
        self.Layout()

    def __save_user_config(self):
	self.user_config.path_benchmark = self.text_benchmark.GetValue()
	self.user_config.args_benchmark = self.text_args_benchmark.GetValue()

	if self.combo_cpu.FindString(self.combo_cpu.GetValue()) != 0:
		self.user_config.cpu = self.combo_cpu.GetValue()
	
	self.user_config.time = self.spin_ctrl_time_samples.GetValue()
	self.user_config.buffer_size = self.spin_ctrl_buffer_size.GetValue()

	if self.radio_btn_save_yes.GetValue():
		self.user_config.path_outfile = self.path_save.GetPath()
		
        self.user_config.system_wide = self.radio_btn_system_wide.GetValue()
	
	# Save graph style user configuration
	bg_color = self.dialog_graph_style.GetBgColor()
	grid_color = self.dialog_graph_style.GetGridColor()
	line_color = self.dialog_graph_style.GetLineColor()
	line_style = self.dialog_graph_style.GetLineStyle()
	line_width = self.dialog_graph_style.GetLineWidth()
	self.user_config.graph_style = GraphStyleConfig(bg_color, grid_color, line_color, line_style, line_width)
	
    def StartMonitoring(self):
	self.__save_user_config()
	cp_usr_conf = self.user_config.GetCopy()
        self.pmc_extract = PMCExtract(cp_usr_conf)
	if self.pmc_extract.error is None:
		graph_frame = PMCFrameGraph(None, -1, "", version=self.version, final_frame=self, user_config=cp_usr_conf)
    		graph_frame.Show()
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
		for graph_frame in self.graph_frames:
			graph_frame.Destroy()
		del self.graph_frames[:]
		self.button_monitoring.SetLabel(_("Start monitoring"))

    def on_change_benchmark(self, event):
	self.text_benchmark.SetValue(self.ctrl_benchmark.GetPath())

    def on_change_radio_outfile(self, event):
	self.path_save.Enable(self.radio_btn_save_yes.GetValue())

    def on_click_graph_style(self, event):
	if self.dialog_graph_style.ShowModal() == 0:
		self.button_graph_style.SetLabel(self.dialog_graph_style.GetModeName())

    def on_click_prev(self, event):
        self.prev_frame.SetPosition(self.GetPosition())
        self.prev_frame.SetSize(self.GetSize())
        self.Hide()
        self.prev_frame.Show()

    def on_click_monitoring(self, event):
	if len(self.graph_frames) == 0:
		self.StartMonitoring()
	else:
		self.StopMonitoring()
	
    def on_close_frame(self, event):
	if self.prev_frame != None:
		self.prev_frame.next_frame = None
		self.prev_frame.Close()
	self.StopMonitoring(False)
	self.dialog_graph_style.Destroy()
	self.Destroy()
