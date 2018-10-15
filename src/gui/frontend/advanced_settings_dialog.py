# -*- coding: utf-8 -*-

#
# advanced_settings_dialog.py
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

class AdvancedSettingsDialog(wx.Dialog):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Dialog.__init__(self, *args, **kwds)
	
        self.label_path_pmctrack = wx.StaticText(self, -1, _("Path to pmctrack command") + ": ")
	self.text_path_pmctrack = wx.TextCtrl(self, -1, "pmctrack")
        self.sizer_path_pmctrack_staticbox = wx.StaticBox(self, -1, _("Pmctrack command"))
	
	self.label_time_samples = wx.StaticText(self, -1, _("Time between samples (in milliseconds)") + ": ")
        self.spin_ctrl_time_samples = wx.SpinCtrl(self, -1, "1000", min=100, max=5000)
        self.label_buffer_size = wx.StaticText(self, -1, _("Samples buffer size (in bytes, 0 for unspecified)") + ": ")
        self.spin_ctrl_buffer_size = wx.SpinCtrl(self, -1, "0", min=0, max=10000000)	
        self.sizer_samples_staticbox = wx.StaticBox(self, -1, _("Samples configuration"))

        self.sizer_counter_mode_staticbox = wx.StaticBox(self, -1, _("Select counter mode"))
        self.label_counter_mode = wx.StaticText(self, -1, _("What counter mode you want to use?"))
        self.radio_btn_per_thread = wx.RadioButton(self, -1, _("Per-thread mode"))
        self.radio_btn_system_wide = wx.RadioButton(self, -1, _("System-wide mode"))
	
        self.label_save = wx.StaticText(self, -1, _("What monitoring logs want to save?"))
        self.checkbox_save_counters = wx.CheckBox(self, -1, _("Counters samples log"))
        self.checkbox_save_metrics = wx.CheckBox(self, -1, _("Metrics samples log"))
        self.label_path_save = wx.StaticText(self, -1, _("Path to save monitoring logs") + ": ")
        self.path_save = wx.DirPickerCtrl(self, -1, "", _("Select directory where to save the output file"))
        self.sizer_save_staticbox = wx.StaticBox(self, -1, _("Save monitoring logs into files"))

	self.button_accept = wx.Button(self, -1, _("Save advanced settings"))
	
	self.__set_properties()
        self.__do_layout()

	self.Bind(wx.EVT_BUTTON, self.on_accept, self.button_accept)
        self.Bind(wx.EVT_CHECKBOX, self.on_change_log_checkboxs, self.checkbox_save_counters)
        self.Bind(wx.EVT_CHECKBOX, self.on_change_log_checkboxs, self.checkbox_save_metrics)

    def __set_properties(self):
        self.SetTitle(_("Advanced settings"))
        self.SetSize((700, 530))
        self.radio_btn_per_thread.SetValue(1)
        self.path_save.Enable(False)
        self.path_save.SetPath("/tmp")
        self.button_accept.SetMinSize((230, 37))

    def __do_layout(self):
        separator = wx.BoxSizer(wx.VERTICAL)
	self.sizer_path_pmctrack_staticbox.Lower()
        sizer_path_pmctrack = wx.StaticBoxSizer(self.sizer_path_pmctrack_staticbox, wx.VERTICAL)
        grid_sizer_path_pmctrack = wx.FlexGridSizer(1, 2, 0, 0)
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

	grid_sizer_path_pmctrack.Add(self.label_path_pmctrack, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_path_pmctrack.Add(self.text_path_pmctrack, 1, wx.EXPAND, 0)
        grid_sizer_path_pmctrack.AddGrowableCol(1)
        sizer_path_pmctrack.Add(grid_sizer_path_pmctrack, 1, wx.ALL | wx.EXPAND, 5)
        separator.Add(sizer_path_pmctrack, 4, wx.ALL | wx.EXPAND, 5)
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
        separator.Add(sizer_counter_mode, 4, wx.ALL | wx.EXPAND, 5)
        grid_sizer_save.Add(self.label_save, 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 5)
        sizer_radio_save.Add(self.checkbox_save_counters, 0, 0, 0)
        sizer_radio_save.Add(self.checkbox_save_metrics, 0, wx.LEFT, 10)
        grid_sizer_save.Add(sizer_radio_save, 1, wx.EXPAND, 0)
        grid_sizer_save.Add(self.label_path_save, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_save.Add(self.path_save, 0, wx.EXPAND, 0)
        grid_sizer_save.AddGrowableCol(1)
        sizer_save.Add(grid_sizer_save, 1, wx.ALL | wx.EXPAND, 5)
        separator.Add(sizer_save, 5, wx.ALL | wx.EXPAND, 5)
        separator.Add(self.button_accept, 0, wx.RIGHT | wx.BOTTOM | wx.ALIGN_RIGHT, 5)
        self.SetSizer(separator)
        self.Layout()

    def on_change_log_checkboxs(self, event):
    	self.UpdateEnabledPathSaveLogs()

    def on_accept(self, event):
        self.EndModal(0)

    def UpdateEnabledPathSaveLogs(self):
        self.path_save.Enable(self.checkbox_save_counters.GetValue() or self.checkbox_save_metrics.GetValue())

    def GetPmctrackCommandPath(self):
	return self.text_path_pmctrack.GetValue()

    def SetPmctrackCommandPath(self, pmctrack_cmd_path):
	self.text_path_pmctrack.SetValue(pmctrack_cmd_path)

    def GetTimeBetweenSamples(self):
	return self.spin_ctrl_time_samples.GetValue()

    def SetTimeBetweenSamples(self, time):
	self.spin_ctrl_time_samples.SetValue(time)

    def GetSamplesBufferSize(self):
	return self.spin_ctrl_buffer_size.GetValue()

    def SetSamplesBufferSize(self, size):
	self.spin_ctrl_buffer_size.SetValue(size)

    def GetIfSaveCountersLog(self):
	return self.checkbox_save_counters.GetValue()

    def SetIfSaveCountersLog(self, save):
	self.checkbox_save_counters.SetValue(save)

    def GetIfSaveMetricsLog(self):
	return self.checkbox_save_metrics.GetValue()

    def SetIfSaveMetricsLog(self, save):
	self.checkbox_save_metrics.SetValue(save)

    def GetLogfilePath(self):
	return self.path_save.GetPath()

    def SetLogfilePath(self, path):
	self.path_save.SetPath(path)

    def GetIfSystemWideMode(self):
    	return self.radio_btn_system_wide.GetValue()

    def SetIfSystemWideMode(self, wide_mode):
    	if wide_mode:
	    self.radio_btn_system_wide.SetValue(True)
